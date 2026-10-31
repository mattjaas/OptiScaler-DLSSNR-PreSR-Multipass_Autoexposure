#include <cassert>
#include "../OptiScaler/shaders/dlssnr/DlssNr_Guides.h"
#include <cmath>
#include <limits>
#include "../OptiScaler/dlssnr/DlssNr_PrivateExposure.h"
#include "../OptiScaler/dlssnr/DlssNr_SuperiorScaling.h"

int main()
{
    namespace DT = DlssNr::DepthTransport;
    DT::Settings ds;
    assert(DT::Flags(ds) == 0 && DT::Weight(.5f, false, ds) == 0);
    ds.weightedGain = true; ds.curve = 1;
    assert(DT::Weight(0, false, ds) == 1 && DT::Weight(1, false, ds) == 0);
    assert(DT::Weight(0, true, ds) == 0 && DT::Weight(1, true, ds) == 1);
    assert(DT::Gain(10, .5f, ds) == 5.5f && DT::Gain(.5f, 1, ds) == 1);
    ds.fullRange = false; ds.nearLimit = .25f; ds.farLimit = .75f;
    assert(DT::Weight(.25f, false, ds) == 1 && DT::Weight(.5f, false, ds) == .5f && DT::Weight(.75f, false, ds) == 0);
    for (bool inverted : {false, true})
    {
        for (float z : {NAN, INFINITY, -INFINITY, -1.f, 2.f}) assert(DT::Weight(z, inverted, ds) == 0);
        for (int i = 0; i <= 100; ++i)
        {
            const float z = float(i)/100;
            assert(std::abs(DT::Weight(z, inverted, ds)-DT::Weight(1-z, !inverted, ds)) < 1e-6f);
        }
    }
    auto bad = ds; bad.curve = NAN; bad.nearLimit = INFINITY; bad.farLimit = -1; bad.bias = NAN;
    bad = DT::Sanitize(bad);
    assert(bad.curve == .25f && bad.nearLimit == 0 && bad.farLimit > 0 && bad.bias == .1f);
    const DlssNr::GuideRegion region{3,5,64,32};
    using DlssNr::SuperiorScaling::OriginalGuideFits;
    assert(OriginalGuideFits(67,37,region,64,32));
    assert(!OriginalGuideFits(66,37,region,64,32));
    assert(!OriginalGuideFits(67,36,region,64,32));
    assert(!OriginalGuideFits(67,37,region,65,32));
    assert(!OriginalGuideFits(67,37,region,64,33));
    assert(!OriginalGuideFits(67,37,region,0,32));
    assert(!OriginalGuideFits(2,37,region,1,1));
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
    assert(PositiveGain(0) == .01f && PositiveGain(-1) == .01f && PositiveGain(9999) == 1024);
    assert(PositiveGain(.01f) == .01f && PositiveGain(.125f) == .125f && PositiveGain(.5f) == .5f);
    assert(PositiveGain(std::numeric_limits<float>::quiet_NaN()) == 1);
    assert(EditSaturation(0) == 1 && EditSaturation(9999) == 64);
    assert(EditSaturation(INFINITY) == 1 && EditSaturation(NAN) == 1 && EditSaturation(4.f) == 4.f);
    assert(DepthStrength(-1) == 0 && DepthStrength(99) == 16 && DepthStrength(NAN) == 1);
    assert(DepthCurve(0) == .03125f && DepthCurve(999) == 64 && DepthCurve(INFINITY) == 1);
    assert(DepthPair(0) == 0 && DepthPair(2) == 2 && DepthPair(3) == 0);
    assert(ManualValue(-1) == 1 && ManualValue(INFINITY) == 1 && ManualValue(0) == 1);
    assert(ManualValue(99999) == 65504 && ManualValue(.00000001f) == .000001f);
    assert(SanitizeMode(999) == Native);
}
