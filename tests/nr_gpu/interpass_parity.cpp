#include "pch.h"
#include "interpass_fixture.h"
#include <iomanip>

int main(int argc, char** argv)
{
    try {
        if (argc < 3) throw std::runtime_error("Usage: interpass_parity new-dir v16-reference-dir [--hardware] [--timing]");
        Runner runner(argc > 3 && std::string(argv[3]) == "--hardware");
        auto current = runner.pipeline(argv[1], "DlssNr");
        auto rgb16 = runner.pipeline(argv[1], "dlssnr_interpass_rgb16");
        auto rgb20 = runner.pipeline(argv[1], "dlssnr_interpass_rgb20");
        auto previous = runner.pipeline(argv[2], "DlssNr");
        auto previous16 = runner.pipeline(argv[2], "dlssnr_tiled_v14_rgb16");
        auto previous20 = runner.pipeline(argv[2], "dlssnr_tiled_v14_rgb20");
        if (argc > 4 && std::string(argv[4]) == "--timing")
        {
            std::ofstream out("NR-interpass-cleanup.samples.csv");
            if (!out) throw std::runtime_error("Cannot open timing CSV");
            out << "scale_percent,variant,window,gpu_ms\n" << std::setprecision(9);
            for (int scale : {50,59,65})
            {
                const UINT nw=1920,nh=1080,ww=nw*scale/100,wh=nh*scale/100;
                DlssNrConstants c{};c.Mode=28;c.Width=ww;c.Height=wh;c.ResidualHistoryValid=1;
                c.ResidualScale=1.2f;c.ResidualBlend=0.015f;c.ResidualConfidenceSensitivity=1.0f;
                c.DirectResolveFlags=16|64|128;
                const float rx=float(ww)/nw,ry=float(wh)/nh;
                std::memcpy(&c.ResidualMotionBaseX,&rx,4);std::memcpy(&c.ResidualMotionBaseY,&ry,4);
                if (scale!=50)c.DirectResolveFlags|=2048;
                if (scale==65)c.DirectResolveFlags|=8192;
                auto* a=scale==50?previous.Get():scale==59?previous20.Get():previous16.Get();
                auto* b=scale==50?current.Get():scale==59?rgb20.Get():rgb16.Get();
                double ms;
                runner.run(a,nw,nh,ww,wh,c,0,true,8,0,0,&ms);
                runner.run(b,nw,nh,ww,wh,c,0,true,8,0,0,&ms);
                for (int block=0;block<3;++block) for (int k : {0,1,1,0})
                {
                    runner.run(k?b:a,nw,nh,ww,wh,c,0,true,8,0,0,&ms);
                    out << scale << ',' << (k?"v17":"previous") << ',' << block << ',' << ms << '\n';
                }
            }
            out.flush();
            if (!out) throw std::runtime_error("Timing CSV write failed");
            return 0;
        }
        UINT64 checked = 0; unsigned fixtures = 0;
        for (auto dims : {std::pair<UINT,UINT>{65,37},{127,73},{17,11},{64,40}})
        for (int scale : {25,33,40,45,49,50,51,55,59,60,61,65,90,95,99})
        for (int stress = 0; stress < 4; ++stress)
        for (float guide : {0.35f, 1.0f})
        for (bool half : {false, true})
        for (int variant = 0; variant < 6; ++variant) {
            UINT nw=dims.first,nh=dims.second,ww=std::max(1u,nw*scale/100),wh=std::max(1u,nh*scale/100);
            DlssNrConstants c{};c.Mode=variant<2?27:28;c.Width=variant<2?nw:ww;c.Height=variant<2?nh:wh;
            c.ResidualHistoryValid=1;c.ResidualScale=0.7f;c.ResidualBlend=stress==2?0.00001f:0.2f;
            c.ResidualConfidenceSensitivity=guide;
            c.DirectDetailMode=ww;c.DirectResolveUpscaler=wh;
            float rx=float(ww)/nw,ry=float(wh)/nh;
            std::memcpy(&c.ResidualMotionBaseX,&rx,4);std::memcpy(&c.ResidualMotionBaseY,&ry,4);
            c.DirectResolveFlags=(variant==0 || variant==2)?0:16|64|128;
            if (variant==1)c.DirectResolveFlags|=1024;
            if (variant>=4)c.DirectResolveFlags|=2048;
            if (stress==1 && c.DirectResolveFlags){c.DirectResolveFlags|=32;c.MvScaleX=1.37f;}
            if (stress==3){c.GuideWidth=1;c.MvScaleX=1.13f;c.MvScaleY=0.71f;}
            if (stress==2){
                c.GuideHeight=1;float low=0.02f,high=0.08f,floor=0.15f;
                std::memcpy(&c.ExposureSourceWidth,&low,4);std::memcpy(&c.ExposureSourceHeight,&high,4);
                std::memcpy(&c.ExposurePadding,&floor,4);
            }
            auto* a=variant==4?rgb16.Get():variant==5?rgb20.Get():current.Get();
            auto* b=variant==4?previous16.Get():variant==5?previous20.Get():previous.Get();
            auto actual=runner.run(a,nw,nh,c.Width,c.Height,c,stress,half,8,ww,wh);
            // Below P50 compare against the same isolated-weights shader without tiles.
            // Keeps the compiler specialization identical while checking tiled/overflow paths.
            auto referenceConstants=c;
            if (variant==5 && scale<50)
            {
                b=previous20.Get();
                referenceConstants.DirectResolveFlags &= ~2048u;
            }
            auto expected=runner.run(b,nw,nh,c.Width,c.Height,referenceConstants,stress,half,8,ww,wh);
            for (size_t k=0;k<actual.size();++k) if (actual[k]!=expected[k]) {
                bool equal=true;
                if (half) {
                    for (int shift:{0,16}) {
                        uint32_t x=(actual[k]>>shift)&0xffff,y=(expected[k]>>shift)&0xffff;
                        if (x!=y && !((x&0x7fff)>0x7c00 && (y&0x7fff)>0x7c00))equal=false;
                    }
                } else equal=(actual[k]&0x7fffffff)>0x7f800000 && (expected[k]&0x7fffffff)>0x7f800000;
                if (!equal){std::cerr<<"Mismatch variant="<<variant<<" scale="<<scale<<" stress="<<stress
                    <<" half="<<half<<" component="<<k<<" expected="<<std::hex<<expected[k]<<" actual="<<actual[k]<<"\n";return 1;}
            }
            checked+=actual.size()*(half?2:1);++fixtures;
        }
        std::cout<<"Retained inter-pass compiled parity PASS: "<<fixtures<<" comparisons, "<<checked<<" RGBA components\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
