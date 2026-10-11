#include "pch.h"
#include "../../OptiScaler/dlssnr/DlssNr_BenchmarkCases.h"
#include "../../OptiScaler/dlssnr/DlssNr_GuidedShaping.h"
#include <cmath>
#include <limits>

using namespace DlssNr::InterPass;
void require(bool condition) { if (!condition) throw std::runtime_error("Inter-pass policy check failed"); }
int main()
{
    try {
        for (const auto& scale : DlssNr::kInterPassBenchmarkScales) require(scale.percent <= 65);
        for (const int scale : DlssNr::kInterPassLowVerificationScales) require(scale <= 65);
        for (unsigned percent=25;percent<=200;++percent)
        for (unsigned mode=0;mode<=7;++mode)
        for (unsigned passes=0;passes<=32;++passes)
        for (bool spatial : {false,true})
        {
            const unsigned expected = mode==0 || spatial || percent>84 ? 0u : percent>=74 ? 2u : 1u;
            const unsigned stride = Cadence(mode, float(percent)/100.0f, passes, spatial);
            require(stride == (passes>expected ? expected : 0u));
            unsigned corrections=0;
            for (unsigned pass=0;pass<passes;++pass)
            {
                const bool due=expected!=0 && (pass+1)%expected==0 && pass+1<passes;
                require(Due(pass,passes,stride)==due);
                corrections+=Due(pass,passes,stride);
            }
            require(corrections == (stride ? (passes-1)/stride : 0u));
            require(!Due(passes,passes,stride));
        }
        require(Cadence(4,std::nextafter(0.74f,0.0f),6,false)==1);
        require(Cadence(4,0.74f,6,false)==2);
        require(Cadence(4,std::nextafter(0.74f,1.0f),6,false)==2);
        require(Cadence(4,std::nextafter(0.84f,0.0f),6,false)==2);
        require(Cadence(4,0.84f,6,false)==2);
        require(Cadence(4,std::nextafter(0.84f,1.0f),6,false)==0);
        require(Cadence(4,std::numeric_limits<float>::quiet_NaN(),6,false)==0);
        require(Cadence(4,std::numeric_limits<float>::infinity(),6,false)==0);
        require(Cadence(4,-1.0f,6,false)==0 && Cadence(4,0.0f,6,false)==0);
        require(!Due(UINT32_MAX,UINT32_MAX,2));
        require(!Due(0,6,0));
        require(!Due(0,6,2) && Due(1,6,2) && !Due(2,6,2) && Due(3,6,2) && !Due(4,6,2));
        using DlssNr::Profiles::StructureAdjustedHigh;
        using DlssNr::Profiles::ProductGuidedHigh;
        require(std::abs(StructureAdjustedHigh(0.8f,0.5f)-0.9f)<1e-6f);
        require(std::abs(StructureAdjustedHigh(1.2f,0.5f)-1.1f)<1e-6f);
        require(StructureAdjustedHigh(0.8f,0)==1.0f);
        require(StructureAdjustedHigh(0.8f,1)==0.8f);
        require(StructureAdjustedHigh(0.8f,1.5f)==0.8f);
        require(StructureAdjustedHigh(0.8f,-1)==1.0f);
        require(StructureAdjustedHigh(1.2f,2)==1.2f);
        require(StructureAdjustedHigh(std::numeric_limits<float>::quiet_NaN(),0.5f)==1.0f);
        require(StructureAdjustedHigh(0.8f,std::numeric_limits<float>::quiet_NaN())==0.8f);
        const std::array<float,6> high {{0.8f,0.6f,1.2f,0.5f,0.9f,0.7f}};
        const std::array<float,6> structure {{1.0f,0.25f,0.25f,0.0f,2.0f,0.5f}};
        const auto adjusted=[&](unsigned pass) { return StructureAdjustedHigh(high.at(pass),structure.at(pass)); };
        require(std::abs(ProductGuidedHigh(0,2,true,adjusted)-0.72f)<1e-6f);
        require(std::abs(ProductGuidedHigh(2,4,true,adjusted)-1.05f)<1e-6f);
        require(std::abs(ProductGuidedHigh(4,6,true,adjusted)-0.765f)<1e-6f);
        require(ProductGuidedHigh(4,5,true,adjusted)==0.9f); // five passes: final single model
        require(std::abs(ProductGuidedHigh(0,2,false,adjusted)-0.9f)<1e-6f);
        require(ProductGuidedHigh(0,0,true,adjusted)==1.0f);
        require(ProductGuidedHigh(0,2,true,[](unsigned) { return std::numeric_limits<float>::max(); })==1.0f);
        require(ProductGuidedHigh(0,2,true,[](unsigned) { return 0.0f; })==0.0f);
        require(ProductGuidedHigh(0,2,true,[](unsigned) { return std::numeric_limits<float>::quiet_NaN(); })==1.0f);
        for (unsigned passes=1;passes<=32;++passes)
        for (unsigned stride : {0u,1u,2u})
        for (unsigned failure : {0u,1u,2u,3u})
        {
            unsigned covered=0, accounted=0;
            double product=1.0;
            std::array<unsigned,32> visits {};
            const auto variableGain=[&](unsigned pass) {
                return StructureAdjustedHigh(0.75f+float(pass%4)*0.05f,0.25f+float(pass%5)*0.25f);
            };
            const auto countedGain=[&](unsigned pass) { ++visits.at(pass); return variableGain(pass); };
            for (unsigned pass=0;pass<passes;++pass)
            {
                if (!Due(pass,passes,stride) || pass+1==failure) continue;
                accounted+=PendingPasses(pass+1,covered);
                product*=ProductGuidedHigh(covered,pass+1,true,countedGain);
                covered=pass+1;
            }
            const unsigned finalSpan=PendingPasses(passes,covered);
            accounted+=finalSpan;
            product*=ProductGuidedHigh(covered,passes,true,countedGain);
            require(accounted==passes);
            for (unsigned pass=0;pass<passes;++pass) require(visits.at(pass)==1);
            // Gain bookkeeping only: NGX/guiding/low/clamp do not commute with shaping.
            require(std::abs(product-ProductGuidedHigh(0,passes,true,variableGain))<1e-6);
            if (passes==5 && stride==2 && failure==0) require(covered==4 && finalSpan==1);
            if (passes==6 && stride==2 && failure==0) require(covered==4 && finalSpan==2);
            if (stride==0) require(covered==0 && finalSpan==passes);
        }
        require(DlssNr::kInterPassLowBenchmarkScales == std::array<int,4>{33,40,45,49});
        require(DlssNr::kInterPassBoundaryBenchmarkScales == std::array<int,2>{41,42});
        const auto& order = DlssNr::kInterPassLowBenchmarkOrder;
        require(order.size()==6);
        for (size_t i=0;i<order.size();++i) require(order[i]==order[order.size()-1-i]);
        for (auto path : {Path::ClassicOptimized,Path::FusedOptimized,Path::Rgb20})
            require(std::count(order.begin(),order.end(),path)==2);
        require(DlssNr::kInterPassLowVerificationScales == std::array<int,5>{33,40,41,42,49});
        for (const auto& v : DlssNr::kInterPassMeasuredLowScales)
        {
            const unsigned ww=(3840*v.percent+50)/100,wh=(2160*v.percent+50)/100;
            require(Select(3,3840,2160,ww,wh,0,1,1)==v.expected);
            for (unsigned mode=0;mode<3;++mode)
                require(Select(mode,3840,2160,ww,wh,0,1,1)==static_cast<Path>(mode));
        }
        require(Select(0,3840,2160,2496,1404,0,1,1)==Path::Off);
        require(Select(1,3840,2160,2496,1404,0,1,1)==Path::ClassicReference);
        require(Select(2,3840,2160,2496,1404,0,1,1)==Path::FusedReference);
        require(Select(3,3840,2160,1920,1080,0,1,1)==Path::FusedOptimized);
        require(Select(3,3840,2160,2266,1274,0,1,1)==Path::Rgb20);
        require(Select(3,3840,2160,2304,1296,0,1,1)==Path::Rgb16);
        require(Select(3,65,37,39,22,0,1,1)==Path::Rgb20); // nominal P60, rounded height below threshold
        require(Select(3,3840,2160,2496,1404,0,1,1)==Path::Rgb16);
        for (const auto& scale : DlssNr::kInterPassBenchmarkScales)
        {
            const unsigned ww=(3840*scale.percent+50)/100,wh=(2160*scale.percent+50)/100;
            require(Select(4,3840,2160,ww,wh,0,1,1)==Path::FastCombinedCross5);
            require(Fused(Path::FastGuided) && Optimized(Path::FastGuided));
        }
        require(Select(3,1000,1000,505,505,0,1,1)==Path::Rgb20);
        require(Select(3,1000,1000,506,506,0,1,1)==Path::Rgb20);
        require(Select(3,1000,1000,900,900,0,1,1)==Path::Rgb16);
        require(Select(3,1000,1000,901,901,0,1,1)==Path::ClassicOptimized);
        require(Select(3,1000,1000,400,400,0,1,1)==Path::Rgb20);
        require(Select(3,1000,1000,400,401,0,1,1)==Path::ClassicOptimized);
        require(Select(3,1000,1000,399,400,0,1,1)==Path::ClassicOptimized);
        require(Select(3,3840,2160,1574,886,0,1,1)==Path::ClassicOptimized); // P41
        require(Select(3,3840,2160,1613,907,0,1,1)==Path::Rgb20); // rounded P42
        require(Select(3,3840,2160,1612,907,0,1,1)==Path::ClassicOptimized);
        require(Select(3,3840,2160,1613,906,0,1,1)==Path::ClassicOptimized);
        require(Select(3,3840,2160,1728,972,0,1,1)==Path::Rgb20); // measured P45
        require(Select(3,3840,2160,1882,1058,0,1,1)==Path::Rgb20); // measured P49
        require(Select(3,4000000000u,3000000000u,1680000000u,1260000000u,0,1,1)==Path::Rgb20);
        require(Select(3,0,0,0,0,0,1,1)==Path::ClassicOptimized);
        require(Select(3,3840,2160,2496,1404,0,1,0)==Path::ClassicOptimized);
        for (unsigned filter=1;filter<=11;++filter)
            require(Select(3,3840,2160,2496,1404,filter,1,1)==Path::ClassicOptimized);
        for (unsigned radius : {2u,3u})
            require(Select(3,3840,2160,2496,1404,0,radius,1)==Path::ClassicOptimized);
        for (unsigned percent=25;percent<=200;++percent)
        for (unsigned filter=0;filter<=11;++filter)
        for (unsigned radius : {1u,2u,3u})
        for (float guide : {0.0f,1.0f})
        for (unsigned mode : {4u,5u,6u,7u})
        {
            const unsigned ww=(3840*percent+50)/100,wh=(2160*percent+50)/100;
            const bool eligible=percent<100 && filter==0 && radius==1 && guide>0;
            const auto expected = Path::FastCombinedCross5;
            require(Fast(expected) && Fused(expected) && Optimized(expected));
            require(Select(mode,3840,2160,ww,wh,filter,radius,guide)==
                (eligible?expected:Select(3,3840,2160,ww,wh,filter,radius,guide)));
        }
        require(Select(4,65,37,33,18,0,1,1)==Path::FastCombinedCross5);
        require(Select(4,3840,2160,1920,1080,0,1,1)==Path::FastCombinedCross5);
        require(Select(4,65,37,33,19,0,1,1)==Path::FastCombinedCross5);
        require(Select(4,64,37,32,19,0,1,1)==Path::FastCombinedCross5);
        require(Select(4,4000000000u,3000000000u,2000000000u,1500000000u,0,1,1)==Path::FastCombinedCross5);
        require(Select(7,3840,2160,1920,1080,0,1,1)==Path::FastCombinedCross5);
        require(Select(7,65,37,33,19,0,1,1)==Path::FastCombinedCross5); // rounded P50 on both axes
        require(Select(7,64,37,32,19,0,1,1)==Path::FastCombinedCross5); // one odd native axis
        require(Select(7,3840,2160,1921,1080,0,1,1)==Path::FastCombinedCross5); // not P50 geometry
        require(Select(7,3840,2160,1920,1081,0,1,1)==Path::FastCombinedCross5);
        require(Select(7,65,37,34,19,0,1,1)==Path::FastCombinedCross5);
        require(Select(7,65,37,33,18,0,1,1)==Path::FastCombinedCross5); // below half on Y, not P50
        require(Select(7,4000000000u,3000000000u,2000000000u,1500000000u,0,1,1)==Path::FastCombinedCross5);
        require(Select(7,3840,2160,2266,1274,0,1,1)==Path::FastCombinedCross5);
        require(Select(7,3840,2160,2496,1404,0,1,1)==Path::FastCombinedCross5);
        for (unsigned mode : {4u,7u})
        {
            require(Select(mode,3840,2160,960,540,0,1,1)==Path::FastCombinedCross5);
            require(Select(mode,3840,2160,959,540,0,1,1)==Path::FastCombinedCross5);
            require(Select(mode,3840,2160,960,539,0,1,1)==Path::FastCombinedCross5);
            require(Select(mode,65,37,17,9,0,1,1)==Path::FastCombinedCross5); // rounded P25 can be below P25 on one axis
            require(Select(mode,65,37,64,37,0,1,1)==Path::FastCombinedCross5); // only X reduced at P99
            require(Select(mode,65,37,65,36,0,1,1)==Path::FastCombinedCross5); // only Y reduced
            require(Select(mode,65,37,65,37,0,1,1)==Select(3,65,37,65,37,0,1,1)); // no actual reduction
            require(Select(mode,65,37,66,36,0,1,1)==Select(3,65,37,66,36,0,1,1)); // supersampling on X
            require(Select(mode,65,37,64,38,0,1,1)==Select(3,65,37,64,38,0,1,1));
            require(Select(mode,65,37,1,1,0,1,1)==Path::FastCombinedCross5);
            require(Select(mode,65,37,0,1,0,1,1)==Select(3,65,37,0,1,0,1,1));
        }
        unsigned groups=0,guarded=0;
        for (auto dims : {std::pair<unsigned,unsigned>{65,37},{127,73},{1920,1080},{3840,2160}})
        for (unsigned percent=25;percent<100;++percent)
        {
            const unsigned ww=std::max(1u,(dims.first*percent+50)/100),wh=std::max(1u,(dims.second*percent+50)/100);
            const auto path=Select(3,dims.first,dims.second,ww,wh,0,1,1);
            if (path!=Path::Rgb16 && path!=Path::Rgb20) continue;
            const unsigned pitch=path==Path::Rgb16?16:20;
            for (auto axis : {std::pair<unsigned,unsigned>{dims.first,ww},{dims.second,wh}})
            for (unsigned start=0;start<axis.second;start+=8)
            {
                unsigned first=uint64_t(start)*axis.first/axis.second;
                unsigned end=(uint64_t(std::min(start+8,axis.second))*axis.first+axis.second-1)/axis.second;
                require(end>first);
                if (end-first>pitch)
                {
                    // Rounded P42 deliberately permits partial RGB20 coverage;
                    // the unchanged group-uniform shader guard takes direct Mode28.
                    require(path==Path::Rgb20 && uint64_t(axis.second)*19<uint64_t(axis.first)*8);
                    ++guarded;
                }
                ++groups;
            }
        }
        require(guarded>0);
        std::cout<<"Inter-pass policy PASS: measured winners, aligned P40, rounded P42, all percentages, per-pass Structure/style products and failure accounting, reference isolation, filters/radii and "<<groups<<" tile axes ("<<guarded<<" guarded overflow axes)\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
