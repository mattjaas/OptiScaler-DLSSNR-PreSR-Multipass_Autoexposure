#include <cassert>
#include <cmath>
#include <limits>
#include "../OptiScaler/dlssnr/DlssNr_PrivateExposure.h"

int main()
{
    using namespace DlssNr::PrivateExposure;
    for (auto mode : { LegacyCNN, LegacyK, Transformer2 })
    {
        assert(SampleExtent(1920, 3840, mode) == 1920);
        assert(SampleExtent(1280, 3840, mode) == (mode == LegacyK ? 1920u : 640u));
        assert(SampleExtent(720, 2160, mode) == (mode == LegacyK ? 1080u : 360u));
        assert(SampleExtent(320, 1920, mode) == (mode == LegacyK ? 960u : 256u));
        assert(SampleExtent(65, 65, mode) == 32);
        assert(SampleExtent(1, 1, mode) == 2);
        for (uint64_t i = 0; i < 8; ++i)
        {
            assert(Measure(mode, i, false) == (mode == Transformer2 || i % 2 == 0));
            assert(Measure(mode, i, true));
        }
    }
    assert(PositiveGain(0) == 1 && PositiveGain(9999) == 1024);
    assert(PositiveGain(std::numeric_limits<float>::quiet_NaN()) == 1);
    assert(ManualValue(-1) == 1 && ManualValue(INFINITY) == 1 && ManualValue(0) == 1);
    assert(ManualValue(99999) == 65504 && ManualValue(.00000001f) == .000001f);
    assert(SanitizeMode(999) == Native);
}
