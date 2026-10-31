#include <cassert>
#include <iostream>
#include <limits>
#include <vector>
#include "../OptiScaler/dlssnr/DlssNr_GainPreview.h"
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
int main()
{
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
    std::cout << "NR gain preview CPU checks passed\n";
}
