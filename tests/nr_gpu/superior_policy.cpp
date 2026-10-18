#include "pch.h"
#include "../../OptiScaler/dlssnr/DlssNr_SuperiorScaling.h"
#include "../../OptiScaler/shaders/dlssnr/DlssNr_Common.h"

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
        for(uint32_t mode=0;mode<=10;++mode) {
            cfg.DlssNrTransfer.value=mode;
            require(!Enabled(cfg) && InterPassMode(cfg)==0 && DownscaleFilter(cfg)==11);
            require(Radius(cfg)==3 && RangeSigma(cfg)==.2f && SpatialSigma(cfg)==.5f && GuideStrength(cfg)==0);
            require(ShapingMode(cfg)==0);
        }
        require(cfg.DlssNrInterPassReconstruction.value==0 && cfg.DlssNrProxyDownscaleFilter.value==11);
        std::cout<<"Superior policy PASS: fixed settings, A/K, all styles, native fallback and legacy preservation\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
