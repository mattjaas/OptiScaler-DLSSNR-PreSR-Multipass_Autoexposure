#include "pch.h"
#include "../../OptiScaler/dlssnr/DlssNr_InterPassPolicy.h"

using namespace DlssNr::InterPass;
void require(bool condition) { if (!condition) throw std::runtime_error("Inter-pass policy check failed"); }
int main()
{
    try {
        require(Select(0,3840,2160,2496,1404,0,1,1)==Path::Off);
        require(Select(1,3840,2160,2496,1404,0,1,1)==Path::ClassicReference);
        require(Select(2,3840,2160,2496,1404,0,1,1)==Path::FusedReference);
        require(Select(3,3840,2160,1920,1080,0,1,1)==Path::FusedOptimized);
        require(Select(3,3840,2160,2266,1274,0,1,1)==Path::Rgb20);
        require(Select(3,3840,2160,2304,1296,0,1,1)==Path::Rgb16);
        require(Select(3,65,37,39,22,0,1,1)==Path::Rgb20); // nominal P60, rounded height below threshold
        require(Select(3,3840,2160,2496,1404,0,1,1)==Path::Rgb16);
        require(Select(3,1000,1000,505,505,0,1,1)==Path::ClassicOptimized);
        require(Select(3,1000,1000,506,506,0,1,1)==Path::Rgb20);
        require(Select(3,1000,1000,900,900,0,1,1)==Path::Rgb16);
        require(Select(3,1000,1000,901,901,0,1,1)==Path::ClassicOptimized);
        require(Select(3,1000,1000,400,400,0,1,1)==Path::ClassicOptimized);
        require(Select(3,0,0,0,0,0,1,1)==Path::ClassicOptimized);
        require(Select(3,3840,2160,2496,1404,0,1,0)==Path::ClassicOptimized);
        for (unsigned filter=1;filter<=11;++filter)
            require(Select(3,3840,2160,2496,1404,filter,1,1)==Path::ClassicOptimized);
        for (unsigned radius : {2u,3u})
            require(Select(3,3840,2160,2496,1404,0,radius,1)==Path::ClassicOptimized);
        unsigned groups=0;
        for (auto dims : {std::pair<unsigned,unsigned>{65,37},{127,73},{1920,1080},{3840,2160}})
        for (unsigned percent=25;percent<100;++percent)
        {
            const unsigned ww=std::max(1u,dims.first*percent/100),wh=std::max(1u,dims.second*percent/100);
            const auto path=Select(3,dims.first,dims.second,ww,wh,0,1,1);
            if (path!=Path::Rgb16 && path!=Path::Rgb20) continue;
            const unsigned pitch=path==Path::Rgb16?16:20;
            for (auto axis : {std::pair<unsigned,unsigned>{dims.first,ww},{dims.second,wh}})
            for (unsigned start=0;start<axis.second;start+=8)
            {
                unsigned first=uint64_t(start)*axis.first/axis.second;
                unsigned end=(uint64_t(std::min(start+8,axis.second))*axis.first+axis.second-1)/axis.second;
                require(end>first && end-first<=pitch);++groups;
            }
        }
        std::cout<<"Inter-pass policy PASS: all percentages, reference isolation, P50/P60/90 boundaries, filters/radii and "<<groups<<" tile axes\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
