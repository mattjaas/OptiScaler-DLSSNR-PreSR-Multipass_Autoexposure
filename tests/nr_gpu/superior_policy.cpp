#include "pch.h"
#include "../../OptiScaler/dlssnr/DlssNr_SuperiorScaling.h"
#include "../../OptiScaler/shaders/dlssnr/DlssNr_Common.h"
#include "../../OptiScaler/shaders/dlssnr/DlssNr_Guides.h"

template<class T> struct Setting { T value; T value_or_default() const { return value; } };
struct SavedSettings {
    Setting<uint32_t> DlssNrTransfer {11}, DlssNrInterPassReconstruction {0},
        DlssNrProxyDownscaleFilter {11}, DlssNrGuidedResidualRadius {3}, DlssNrGuidedResidualShaping {0};
    Setting<float> DlssNrGuidedResidualRangeSigma {.2f}, DlssNrGuidedResidualSpatialSigma {.5f},
        DlssNrGuidedResidualGuideStrength {0};
};
void require(bool condition) { if(!condition)throw std::runtime_error("Superior policy check failed"); }
int main() {
    try {
        using namespace DlssNr::SuperiorScaling;
        SavedSettings cfg;
        require(Enabled(cfg) && InterPassMode(cfg)==8 && DownscaleFilter(cfg)==0);
        require(Radius(cfg)==1 && RangeSigma(cfg)==.015f && SpatialSigma(cfg)==3 && GuideStrength(cfg)==1);
        require(ShapingMode(cfg)==2);
        require(High(0)==.8f && Low(0)==.661f && High(1)==.75f && Low(1)==.787f);
        require(High(2)==.95f && Low(2)==.784f && High(99)==High(2));
        for(int i=-5;i<30;++i)require(Preset(i)==(i==11?11:1));
        require(DlssNrSpatialTransfer(11)==0); // native-scale fallback is ordinary composition
        DlssNr::GuideRegion region {3,5,2560,1440};
        require(ReuseDepth(true,true,1920,1080,1920,1080,region,region));
        require(!ReuseDepth(false,true,1920,1080,1920,1080,region,region));
        require(!ReuseDepth(true,false,1920,1080,1920,1080,region,region));
        require(!ReuseDepth(true,true,1921,1080,1920,1080,region,region));
        require(!ReuseDepth(true,true,1920,1081,1920,1080,region,region));
        require(!ReuseDepth(true,true,0,1080,0,1080,region,region));
        require(!ReuseDepth(true,true,1920,0,1920,0,region,region));
        for(auto field : {&DlssNr::GuideRegion::x,&DlssNr::GuideRegion::y,
                          &DlssNr::GuideRegion::width,&DlssNr::GuideRegion::height}) {
            auto changed=region;++(changed.*field);
            require(!ReuseDepth(true,true,1920,1080,1920,1080,changed,region));
        }
        region.width=0;require(!ReuseDepth(true,true,1920,1080,1920,1080,region,region));
        region.width=2560;region.height=0;
        require(!ReuseDepth(true,true,1920,1080,1920,1080,region,region));
        DlssNrConstants resolve{};resolve.Mode=1;resolve.Transfer=9;resolve.Width=3840;resolve.Height=2160;
        resolve.ResidualHistoryValid=1;resolve.ApplyModel=1;resolve.DirectResolveFlags=ShaderFlag;
        require(NormalResolve(resolve,1920,1080) && NormalResolve(resolve,3840,1080) && NormalResolve(resolve,1920,2160));
        require(!NormalResolve(resolve,3840,2160) && !NormalResolve(resolve,7680,4320));
        require(!NormalResolve(resolve,0,1080) && !NormalResolve(resolve,1920,0) && !NormalResolve(resolve,7680,1080));
        for(auto field : {&DlssNrConstants::DebugView,&DlssNrConstants::CompareMode,&DlssNrConstants::ShowSkinMask}) {
            resolve.*field=1;require(!NormalResolve(resolve,1920,1080));resolve.*field=0;
        }
        resolve.ApplyModel=0;require(!NormalResolve(resolve,1920,1080));resolve.ApplyModel=1;
        resolve.Transfer=8;require(!NormalResolve(resolve,1920,1080));resolve.Transfer=9;
        resolve.Mode=18;require(!NormalResolve(resolve,1920,1080));resolve.Mode=1;
        resolve.ResidualHistoryValid=2;require(!NormalResolve(resolve,1920,1080));resolve.ResidualHistoryValid=1;
        resolve.DirectResolveFlags|=4;require(!NormalResolve(resolve,1920,1080));resolve.DirectResolveFlags=ShaderFlag;
        for(uint32_t mode=0;mode<12;++mode) {
            resolve.ReversibleMode=mode;resolve.Passthrough=1;require(NormalColorProfile(resolve)==(mode<=4?1:0));
            resolve.Passthrough=0;require(NormalColorProfile(resolve)==(mode==2?2:mode==4?3:0));
            resolve.Passthrough=2;require(NormalColorProfile(resolve)==0);
        }
        for(uint32_t mode : {2u,4u}) {
            resolve.ReversibleMode=mode;resolve.Passthrough=0;resolve.ReplaceDetailStrength=.5f;
            resolve.ModelWorkScale=.5f;require(NormalColorProfile(resolve)==0);
            resolve.Passthrough=1;require(NormalColorProfile(resolve)==0);
            resolve.ModelWorkScale=.999f;require(NormalColorProfile(resolve)==1);
            resolve.ModelWorkScale=1;require(NormalColorProfile(resolve)==1);
            resolve.ModelWorkScale=0;require(NormalColorProfile(resolve)==1);
            resolve.ModelWorkScale=.5f;resolve.ReplaceDetailStrength=0;require(NormalColorProfile(resolve)==1);
        }
        resolve.Width=resolve.Height=1;require(!NormalResolve(resolve,1,1)); // A rounded 1x1 is not reduced.
        for(uint32_t mode=0;mode<=10;++mode) {
            cfg.DlssNrTransfer.value=mode;
            require(!Enabled(cfg) && InterPassMode(cfg)==0 && DownscaleFilter(cfg)==11);
            require(Radius(cfg)==3 && RangeSigma(cfg)==.2f && SpatialSigma(cfg)==.5f && GuideStrength(cfg)==0);
            require(ShapingMode(cfg)==0);
        }
        require(cfg.DlssNrInterPassReconstruction.value==0 && cfg.DlssNrProxyDownscaleFilter.value==11);
        std::cout<<"Superior policy PASS: fixed settings, A/K, all styles, depth reuse/normal PSO guards, native fallback and legacy preservation\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
