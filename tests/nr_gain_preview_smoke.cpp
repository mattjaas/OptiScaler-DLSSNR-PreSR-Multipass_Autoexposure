#include <cassert>
#include <iostream>
#include <limits>
#include <vector>
#include "../OptiScaler/dlssnr/DlssNr_GainPreview.h"
#include "../OptiScaler/dlssnr/DlssNr_AutoGain.h"
using namespace DlssNr::GainPreview;
Reading Signal(float value)
{
    Reading s;
    s.gate = 1;
    s.samples = s.validEdits = 36000;
    s.moving = s.changed = s.validMotion = MotionSamples;
    s.r[Bin(value)] = 12000;
    s.g[Bin(value)] = 24000;
    return s;
}
void AutomaticGainChecks()
{
    using DlssNr::AutoGain::Controller;
    Controller c; DlssNr::GainPreview::Status e;
    int deviceA = 1, deviceB = 2;
    e.valid = true; e.paused = false; e.smoothed = 2; e.measurements = 3; e.measuredAt = 0;
    assert(c.Update(false, true, false, .25f, e, &deviceA, 1, 0) == .25f);
    assert(c.Update(true, true, false, .25f, e, &deviceA, 1, 0) == 1 && !c.state.controlled);
    e.measurements = 4; e.measuredAt = .5;
    assert(c.Update(true, true, false, .25f, e, &deviceA, 1, .5) == 2 && c.state.controlled);
    e.smoothed = 3; e.measurements = 5; e.measuredAt = .75;
    assert(c.Update(true, true, false, .25f, e, &deviceA, 1, .75) == 2); // cooldown
    e.measurements = 6; e.measuredAt = 1;
    assert(c.Update(true, true, false, .25f, e, &deviceA, 1, 1) == 3);
    e.smoothed = 3.03f; e.measurements = 7; e.measuredAt = 2;
    assert(c.Update(true, true, false, .25f, e, &deviceA, 1, 2) == 3); // upward deadband
    e.smoothed = 2.9f; e.measurements = 8; e.measuredAt = 3;
    assert(c.Update(true, true, false, .25f, e, &deviceA, 1, 3) == 3); // downward deadband
    e.smoothed = 2.8f; e.measurements = 9; e.measuredAt = 4;
    assert(c.Update(true, true, false, .25f, e, &deviceA, 1, 4) == 2.8f);
    e.smoothed = .2f; e.measurements = 10; e.measuredAt = 5;
    assert(c.Update(true, true, false, .25f, e, &deviceA, 1, 5) == 1); // automatic floor
    e.smoothed = 10; e.paused = true; e.measuredAt = 6; e.measurements = 11;
    assert(c.Update(true, true, false, .25f, e, &deviceA, 1, 6) == 1);
    e.paused = false; e.measuredAt = 6;
    assert(c.Update(true, true, false, .25f, e, &deviceA, 1, 8) == 1); // stale reading
    e.measuredAt = 8;
    assert(c.Update(true, true, true, .25f, e, &deviceA, 1, 8) == 1); // frame hold
    e.smoothed = NAN;
    assert(c.Update(true, true, false, .25f, e, &deviceA, 1, 8) == 1);
    assert(c.Update(true, false, false, .25f, e, &deviceA, 1, 8) == 1 && !c.state.controlled);
    assert(c.Update(false, true, false, .25f, e, &deviceA, 1, 8) == .25f); // manual preference restored
    // Context/device changes restart on manual (at least 1x), awaiting matching measurements.
    e = {}; assert(c.Update(true, true, false, 4, e, &deviceA, 2, 9) == 4);
    e.valid = true; e.paused = false; e.smoothed = 6; e.measurements = 4; e.measuredAt = 10;
    assert(c.Update(true, true, false, 4, e, &deviceA, 2, 10) == 6);
    e = {}; assert(c.Update(true, true, false, .5f, e, &deviceB, 2, 11) == 1);
    // At 60 rendering calls/s and 8 measurements/s, small measurement noise must
    // not cause repeated history changes. Large alternating targets obey cooldown.
    c.Reset(); double lastApplied = -10; unsigned changes = 0;
    float before = 1;
    for (unsigned frame = 0; frame < 1200; ++frame)
    {
        const double time = frame / 60.;
        e.valid = true; e.paused = false; e.measurements = 4 + frame / 8;
        e.measuredAt = (frame / 8) * (8. / 60.);
        e.smoothed = frame < 600 ? 2.f + .01f * float(int(frame / 8 % 3) - 1) : (frame / 8 % 2 ? 4.f : 2.f);
        const float gain = c.Update(true, true, false, .25f, e, &deviceA, 1, time);
        if (gain != before)
        {
            if (frame < 600) assert(changes == 0);
            assert(time - lastApplied >= Controller::Cooldown - 1e-10);
            lastApplied = time; ++changes; before = gain;
        }
        assert(gain >= 1 && gain <= 1024);
    }
    assert(changes > 5 && changes < 25);
}
int main()
{
    AutomaticGainChecks();
    // Independent sorted weighted distribution: unequal positive counts in R and G
    // must still contribute equal total weights. Conservative bin edge must bracket
    // the exact inverse threshold, with at most one 3.125% histogram interval.
    for (bool transformer : { false, true })
    {
        std::array<double, Bins> r {}, g {};
        std::vector<std::pair<float, double>> sorted;
        for (unsigned i = 0; i < 12000; ++i)
        {
            const float v = std::exp2(-16.f + float(i) / 12000 * 12.f);
            ++r[Bin(v)];
            sorted.emplace_back(v, .5 / 12000);
        }
        for (unsigned i = 0; i < 2500; ++i)
        {
            const float v = std::exp2(-14.f + float(i) / 2500 * 11.f);
            ++g[Bin(v)];
            sorted.emplace_back(v, .5 / 2500);
        }
        std::sort(sorted.begin(), sorted.end());
        double sum = 0, exact = 0;
        for (auto [v, weight] : sorted)
            if ((sum += weight) > 1 - Target(transformer))
            {
                exact = Epsilon / v;
                break;
            }
        auto estimate = Quantile(r, g, transformer);
        assert(estimate.valid && estimate.value >= exact * (1 - 1e-6));
        assert(estimate.low <= exact * (1 + 1e-6) && estimate.value <= exact * 1.031251);
        for (auto& n : r)
            n *= 3;
        auto rescaled = Quantile(r, g, transformer);
        assert(rescaled.value == estimate.value);
    }
    std::array<double, Bins> empty {}, over {};
    over[1023] = 1000;
    assert(!Quantile(empty, empty, false).valid && !Quantile(over, over, false).valid);
    over = {};
    over[0] = 1000;
    assert(!Quantile(over, over, false).valid);
    Settings bad;
    bad.slowSeconds = NAN;
    bad.fastSeconds = INFINITY;
    bad.motionThreshold = NAN;
    auto safe = Sanitize(bad);
    assert(safe.slowSeconds == 1.5f && safe.fastSeconds == .25f && safe.motionThreshold == .1f);
    Filter f;
    auto a = Signal(.002f);
    f.Update(a, false, {}, 0);
    assert(f.state.valid && !f.state.paused);
    const float first = f.state.smoothed;
    for (unsigned i = 1; i <= 32; ++i)
        f.Update(a, false, {}, i * .125);
    assert(std::abs(f.state.smoothed - first) < 1e-5 && f.state.measurements == 33);
    a.gate = 2;
    f.Update(a, false, {}, 300);
    assert(f.state.paused && f.state.smoothed == first);
    a.gate = 3;
    f.Update(a, false, {}, 301);
    assert(f.state.paused && f.state.smoothed == first);
    auto b = Signal(.0005f);
    f.Update(b, false, {}, 301.125);
    assert(!f.state.catchingUp && f.state.smoothed < first * 1.2f);
    f.Update(b, false, {}, 301.250);
    assert(f.state.catchingUp);
    for (unsigned i = 3; i <= 96; ++i)
        f.Update(b, false, {}, 301 + i * .125);
    assert(std::abs(f.state.smoothed / f.state.raw - 1) < .001 && f.state.smoothed > first * 3.9);
    const auto held = f.state.smoothed;
    b.validEdits = 0;
    f.Update(b, false, {}, 314);
    assert(f.state.paused && f.state.smoothed == held);
    f.Reset();
    assert(!f.state.valid && f.state.measurements == 0);
    // Padded GPU rows and the final motion pair crossing rows 2/3.
    constexpr size_t pitch = 4352;
    std::vector<char> data(pitch * TextureHeight, 0);
    uint32_t meta[] { 12, 576, 36864, 0x89abcdef }, meta1[] { 36860, 1, 9, 0x12345678 };
    std::memcpy(data.data() + 4096, meta, 16);
    std::memcpy(data.data() + pitch + 4096, meta1, 16);
    float pair[] { 1, 2, 3, 4 };
    std::memcpy(data.data() + (Groups * 2 + 1) * pitch + 30 * 16, pair, 16);
    uint32_t countA = 10, countB = 20;
    std::memcpy(data.data() + 7 * 4, &countA, 4);
    std::memcpy(data.data() + 2 * pitch + 7 * 4, &countB, 4);
    auto unpacked = Unpack(data.data(), pitch);
    assert(unpacked.r[7] == 30 && unpacked.validEdits == 36860);
    assert(unpacked.serial == 0x1234567889abcdefULL && unpacked.changed == 9 && unpacked.samples == 36864);
    assert(unpacked.motion[1148] == 1 && unpacked.motion[1151] == 4);

    Settings edges;edges.method=DepthBoundary;
    assert(BoundaryGain(.02,.01,.08)==6);
    assert(BoundaryGain(.1,0,.08)==1&&BoundaryGain(0,0,.08)==0&&BoundaryGain(NAN,.1,.08)==0);
    assert(BoundaryGain(0,.00000001,.08)==0);
    assert(Context(123,edges)!=Context(123,{}));
    auto other=edges;other.boundaryTarget*=2;assert(Context(123,other)!=Context(123,edges));
    Reading pairs;pairs.gate=1;pairs.samples=36864;pairs.moving=pairs.changed=12;pairs.validMotion=576;
    pairs.boundaryPairs=pairs.boundaryCandidates=128;
    pairs.boundaryOffset=.02*128;pairs.boundarySlope=.01*128;
    edges.boundaryTarget=.08f;Filter edgeFilter;edgeFilter.Update(pairs,true,edges,0);
    assert(edgeFilter.state.valid&&std::abs(edgeFilter.state.smoothed-6)<1e-5);
    for(unsigned i=1;i<64;++i)edgeFilter.Update(pairs,false,edges,i*.125);
    assert(std::abs(edgeFilter.state.smoothed-6)<1e-5);
    pairs.boundaryPairs=63;edgeFilter.Update(pairs,false,edges,8);
    assert(edgeFilter.state.paused&&std::abs(edgeFilter.state.smoothed-6)<1e-5);
    pairs.boundaryPairs=128;pairs.boundarySlope=.001*128;
    edgeFilter.Update(pairs,false,edges,8.125);assert(!edgeFilter.state.catchingUp);
    edgeFilter.Update(pairs,false,edges,8.25);assert(edgeFilter.state.catchingUp);
    for(unsigned i=3;i<256;++i)edgeFilter.Update(pairs,false,edges,8+i*.125);
    assert(std::abs(edgeFilter.state.smoothed-60)<.001);
    const auto saved=edgeFilter.state.smoothed;pairs.boundarySlope=0;
    edgeFilter.Update(pairs,false,edges,41);assert(edgeFilter.state.paused&&edgeFilter.state.smoothed==saved);
    Filter jitter; Reading probe=pairs; probe.boundarySlope=.01*128;
    for(unsigned i=0;i<256;++i)
    {
        const double g=i==0?6.:i%2?5.1:7.9;
        probe.boundarySlope=(double(edges.boundaryTarget)-.02)/g*128;
        jitter.Update(probe,true,edges,i*.125);
        assert(!jitter.state.catchingUp && jitter.state.smoothed>=5 && jitter.state.smoothed<=8);
    }
    assert(jitter.state.smoothed>5.5&&jitter.state.smoothed<6.8);

    Settings asym; asym.fastSeconds=.1f; asym.downSeconds=1.f;
    assert(ResponseSeconds(true,true,asym)==double(.1f)&&ResponseSeconds(true,false,asym)==1);
    auto edgeAsym=asym;edgeAsym.method=DepthBoundary;edgeAsym.boundaryTarget=.1f;
    Reading step=pairs;step.boundaryPairs=step.boundaryCandidates=128;step.boundaryOffset=0;
    Filter rise,fall;
    step.boundarySlope=.1/3*128;rise.Update(step,true,edgeAsym,0);
    step.boundarySlope=.1/12*128;fall.Update(step,true,edgeAsym,0);
    for(unsigned i=1;i<=12;++i)
    {
        step.boundarySlope=.1/12*128;rise.Update(step,true,edgeAsym,i*.125);
        step.boundarySlope=.1/3*128;fall.Update(step,true,edgeAsym,i*.125);
    }
    const double upProgress=std::log(rise.state.smoothed/3.)/std::log(4.),
                 downProgress=std::log(12./fall.state.smoothed)/std::log(4.);
    assert(upProgress>downProgress+.2);
    Filter coverageRise,coverageFall;
    auto low=Signal(.004f),high=Signal(.0005f);
    coverageRise.Update(low,true,asym,0);coverageFall.Update(high,true,asym,0);
    const double loStart=coverageRise.state.smoothed,hiStart=coverageFall.state.smoothed;
    for(unsigned i=1;i<=12;++i){coverageRise.Update(high,true,asym,i*.125);coverageFall.Update(low,true,asym,i*.125);}
    assert(std::log(coverageRise.state.smoothed/loStart)/std::log(hiStart/loStart)>
           std::log(hiStart/coverageFall.state.smoothed)/std::log(hiStart/loStart)+.1);
    auto fewer=edgeAsym;fewer.boundaryMinPairs=8;step.boundaryPairs=step.boundaryCandidates=16;step.boundarySlope=.01*16;
    Filter sparse;sparse.Update(step,true,fewer,0);assert(sparse.state.valid);
    auto flags=edges;flags.boundaryUseMotion=false;flags.boundaryMotionGate=false;
    Constants opts;SetBoundaryDepth(opts,false,0);SetBoundaryOptions(opts,flags);assert(opts.reserved0==6);
    SetBoundaryDepth(opts,true,0);SetBoundaryOptions(opts,flags);assert(opts.reserved0==7);
    assert(Context(1,flags)!=Context(1,edges));
    // Compact result respects row padding, distinct sum/count words and 64-bit serial.
    constexpr size_t compactPitch=768;
    std::vector<char> compact(compactPitch*BoundaryTextureHeight,0);
    auto ptr=[&](unsigned i){return compact.data()+(i/BoundaryTextureWidth)*compactPitch+(i%BoundaryTextureWidth)*16;};
    std::memcpy(ptr(0),meta,16);std::memcpy(ptr(1),meta1,16);
    float sums[2]={1.25f,.5f};uint32_t counts[2]={64,65};
    std::memcpy(ptr(2),sums,8);std::memcpy(ptr(2)+8,counts,8);
    std::memcpy(ptr(2+Groups+287),pair,16);
    const auto unpackEdge=UnpackBoundary(compact.data(),compactPitch);
    assert(unpackEdge.boundaryPairs==64&&unpackEdge.boundaryCandidates==65);
    assert(unpackEdge.boundaryOffset==1.25&&unpackEdge.boundarySlope==.5);
    assert(unpackEdge.motion[1148]==1&&unpackEdge.motion[1151]==4&&unpackEdge.serial==0x1234567889abcdefULL);
    std::cout << "NR gain preview CPU checks passed\n";
}
