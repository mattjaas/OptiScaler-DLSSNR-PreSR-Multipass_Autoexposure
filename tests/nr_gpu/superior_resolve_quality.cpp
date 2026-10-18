#include "pch.h"
#include "interpass_fixture.h"
#include "../../OptiScaler/dlssnr/DlssNr_SuperiorScaling.h"

int main(int argc,char** argv) {
    try {
        if(argc!=3 && argc!=4)throw std::runtime_error("Usage: superior_resolve_quality baseline-dir candidate-dir [normal|normal-pair|color|sdr|hdr2|hdr4]");
        const std::string profile=argc==4?argv[3]:"general";
        const bool normal=argc==4,paired=normal && profile!="normal";
        const int hdr=profile.size()==4 && profile.substr(0,3)=="hdr"?profile[3]-'0':-1;
        if(normal && profile!="normal" && profile!="normal-pair" && profile!="color" && profile!="sdr" && hdr!=2 && hdr!=4)
            throw std::runtime_error("Unknown quality profile");
        Runner r(true);
        auto before=r.pipeline(argv[1],paired?"dlssnr_superior_normal":"dlssnr_superior_resolve");
        auto after=r.pipeline(argv[2],normal?"dlssnr_superior_normal":"dlssnr_superior_resolve");
        auto generalNormal=after;
        std::vector<ComPtr<ID3D12PipelineState>> colors;
        if(profile=="color")for(auto name : {"sdr","hdr2","hdr4"})
            colors.push_back(r.pipeline(argv[2],std::string("dlssnr_superior_")+name));
        if(profile=="sdr" || hdr>=0)after=r.pipeline(argv[2],std::string("dlssnr_superior_")+profile);
        unsigned checks=0;size_t words=0;
        struct Geometry { UINT nw,nh,sw,sh; };
        for(auto d : {Geometry{1,1,1,1},{3,3,1,1},{33,17,17,9},{64,32,32,16},{79,43,47,25},{129,73,84,47},
                      {33,17,33,9},{33,17,17,17},{17,19,1,9},{33,1,17,1}})
        for(bool half : {false,true})
        for(unsigned scenario=0;scenario<18;++scenario)
        for(int fixture=0;fixture<4;++fixture)for(bool detail : {false,true}) {
            if(normal && d.nw==d.sw && d.nh==d.sh)continue; // Normal PSO requires actual reduction.
            const UINT lw=(d.sw+7)/8,lh=(d.sh+7)/8;
            std::vector<uint32_t> low(size_t(lw)*lh*(half?2:4));
            for(size_t i=0;i<low.size();++i) {
                if(half)low[i]=i%2?0x3c003000u:0x3c003400u; // finite, nonzero RGB and alpha
                else { float value=i%4==3?1.0f:float(int(i%7)-3)*.025f;std::memcpy(&low[i],&value,4); }
            }
            DlssNrConstants c{};c.Mode=1;c.Width=d.nw;c.Height=d.nh;c.Transfer=9;
            c.Passthrough=scenario%2;c.WhitePoint=2;c.TransferStrength=.7f;c.ColourStrength=.9f;
            c.ApplyModel=scenario==17?0:1;c.MaxRatio=2;c.MaxDarkening=scenario%3?100:40;
            c.ResidualHistoryValid=1;c.ResidualScale=3;c.ResidualBlend=.015f;c.ResidualConfidenceSensitivity=1;
            c.DirectResolveFlags=65536;c.GuideWidth=scenario==8?0:1;
            const float high[]={.8f,.75f,.95f},lowGain[]={.661f,.787f,.784f};
            const unsigned style=scenario%3;
            c.MvScaleX=high[style];c.MvScaleY=lowGain[style];
            c.ReversibleMode=(scenario/2)%5;c.ReplaceDetailStrength=detail?.5f:0.0f;
            c.ModelWorkScale=float(d.sw)/d.nw;
            const unsigned debugViews[]={0,1,2,3,4,6,7};
            c.DebugView=scenario>=10 && scenario<=16?debugViews[scenario-10]:0;
            c.DebugScale=20;c.CompareMode=scenario%3;c.CompareSplit=.4f;c.CompareZoom=1.3f;c.CompareSwap=scenario%2;
            c.SkinProtection=scenario%2;c.SkinDetail=.75f;c.SkinColour=1.2f;
            c.EnvironmentDetail=1;c.EnvironmentColour=.8f;
            if(normal) { c.DebugView=0;c.CompareMode=0;c.ApplyModel=1; }
            if(profile=="sdr" && c.Passthrough==0)continue;
            if(hdr>=0 && (c.Passthrough!=0 || c.ReversibleMode!=UINT(hdr)))continue;
            const int pattern=fixture<2?2:0;
            const auto a=r.run(before.Get(),d.nw,d.nh,d.nw,d.nh,c,fixture,half,8,d.sw,d.sh,nullptr,pattern,&low,lw,lh);
            const auto color=DlssNr::SuperiorScaling::NormalColorProfile(c);
            auto* selected=profile=="color"?(color?colors.at(color-1).Get():generalNormal.Get()):after.Get();
            if((profile=="sdr" || hdr>=0) && color==0)selected=generalNormal.Get();
            const auto b=r.run(selected,d.nw,d.nh,d.nw,d.nh,c,fixture,half,8,d.sw,d.sh,nullptr,pattern,&low,lw,lh);
            if(a!=b) {
                for(size_t i=0;i<a.size();++i)if(a[i]!=b[i]) {
                    std::cerr<<"Mismatch native="<<d.nw<<'x'<<d.nh<<" work="<<d.sw<<'x'<<d.sh
                        <<" half="<<half<<" scenario="<<scenario<<" fixture="<<fixture<<" word="<<i
                        <<" before="<<std::hex<<a[i]<<" after="<<b[i]<<std::dec<<'\n';break;
                }
                throw std::runtime_error("Superior resolve is not bit-exact");
            }
            words+=a.size();++checks;
        }
        std::cout<<"Superior "<<profile<<" resolve GPU parity PASS: "<<checks<<" cases, "<<words
            <<" words, FP16/FP32, HDR mappings, "<<(normal?"normal rendering, ":"compare/debug, ")
            <<"signed/non-finite data and edges\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
