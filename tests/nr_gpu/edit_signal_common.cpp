#include "pch.h"
#include "edit_signal_probe_fixture.h"

std::array<double, 4> CommonReference(const EditImage& image, const Parameters& c)
{
    double channels[3] {}, sumMin = 0, sumMiddle = 0, sumMax = 0;
    UINT active = 0;
    for (UINT y = 0; y < c.gridY; ++y)
        for (UINT x = 0; x < c.gridX; ++x)
        {
            const UINT px = UINT(UINT64(x * 16 + c.phaseX * 2 + 1) * image.width / (c.gridX * 16));
            const UINT py = UINT(UINT64(y * 16 + c.phaseY * 2 + 1) * image.height / (c.gridY * 16));
            std::array<double, 3> sorted {};
            for (unsigned channel = 0; channel < 3; ++channel)
            {
                const float input = image.rgba[(size_t(py) * image.width + px) * 4 + channel];
                const double v = std::isfinite(input) ? std::max(0.0, double(input)) : 0;
                channels[channel] += v;
                sorted[channel] = v;
            }
            std::sort(sorted.begin(), sorted.end());
            sumMin += sorted[0];
            sumMiddle += sorted[1];
            sumMax += sorted[2];
            active += sorted[1] > 0;
        }
    const double count = c.gridX * c.gridY;
    std::sort(std::begin(channels), std::end(channels));
    // Algebraically expanded independent expression, double sums, CPU sorting,
    // separate max accumulation. No GPU helper or shader-generated reference.
    const double score =
        (.125 * sumMax + sumMiddle - 2 * sumMin + 1.625 * channels[2] + channels[1] - 2.625 * channels[0]) / count +
        (active ? sumMiddle / active : 0);
    return { score, active / count, sumMax / count, count };
}
EditImage CommonSynthetic(unsigned kind)
{
    if (kind < 5)
        return Synthetic(kind);
    auto image = Synthetic(0);
    image.name = "uniform" + std::to_string(kind);
    const float gains[] { .01f, .5f, 1.f, 16.f, 1024.f };
    const unsigned profile = (kind - 5) / 5;
    const float value = .125f * gains[(kind - 5) % 5];
    for (size_t i = 0; i < image.rgba.size(); i += 4)
    {
        image.rgba[i] = value;
        image.rgba[i + 1] = profile == 1 ? 0 : value;
        image.rgba[i + 2] = profile == 0 ? value : 0;
    }
    const auto ref = CommonReference(image, { image.width, image.height });
    const double exact = value * (profile == 0 ? .125 : profile == 1 ? 1.75 : 4.75);
    if (std::abs(ref[0] - exact) > 1e-10)
        throw std::runtime_error("Uniform analytic reference failed");
    return image;
}
int main(int argc, char** argv)
{
    try
    {
        bool hardware = true;
        std::string timingPath;
        std::vector<std::string> paths;
        for (int i = 1; i < argc; ++i)
            if (std::string(argv[i]) == "--warp")
                hardware = false;
            else if (std::string(argv[i]) == "--timing" && i + 1 < argc)
                timingPath = argv[++i];
            else
                paths.push_back(argv[i]);
        Runner r(hardware);
        const std::string dir = "artifacts/edit-analysis";
        const std::string names[] { "common64", "common256", "common512", "common1024", "common_tiled64" };
        ComPtr<ID3D12PipelineState> pipelines[5];
        for (unsigned i = 0; i < 5; ++i)
            pipelines[i] = r.pipeline(dir, names[i]);
        auto reduce = r.pipeline(dir, "common_reduce");
        std::ofstream timing;
        if (!timingPath.empty())
        {
            timing.open(timingPath);
            if (!timing)
                throw std::runtime_error("Cannot create timing CSV");
            timing << std::setprecision(9)
                   << "scene,width,height,quantile_permille,method,block,quartet,position,variant,gpu_ms\n";
        }
        UINT cases = 0, labels = 0, correct = 0;
        std::set<std::pair<UINT, UINT>> timedSizes;
        auto verify = [&](const std::array<float, 4>& got, const std::array<double, 4>& expected)
        {
            for (unsigned c = 0; c < 4; ++c)
                if (!std::isfinite(got[c]) || std::abs(got[c] - expected[c]) > 1e-6 + std::abs(expected[c]) * 2e-5)
                    throw std::runtime_error("Common score disagrees with independent CPU reference");
            ++cases;
        };
        for (size_t n = 0; n < paths.size() + 20; ++n)
        {
            auto image = n < 20 ? CommonSynthetic(unsigned(n)) : Load(paths[n - 20]);
            ProbeFixture f(r, image);
            f.constants.quantilePermille = 0; // unused by this score, not a percentile.
            for (UINT py = 0; py < 8; ++py)
                for (UINT px = 0; px < 8; ++px)
                {
                    f.update(px, py);
                    const auto expected = CommonReference(image, f.constants);
                    for (unsigned method = 0; method < 5; ++method)
                    {
                        auto got = f.run(pipelines[method].Get(), method == 4 ? reduce.Get() : nullptr);
                        verify(got, expected);
                        if (n >= 20)
                        {
                            const bool cnn = image.name.find("-cnn-") != std::string::npos;
                            const bool ghost = image.name.find("noghosting") == std::string::npos;
                            ++labels;
                            correct += (got[0] < (cnn ? .052f : .084f)) == ghost;
                        }
                    }
                }
            if (n >= 20)
                for (float gain : { .5f, 2.f })
                {
                    auto scaled = image;
                    for (auto& v : scaled.rgba)
                        if (v > 0 && std::isfinite(v))
                            v *= gain;
                    ProbeFixture scaledFixture(r, scaled);
                    scaledFixture.update(3, 3);
                    f.update(3, 3);
                    const auto base = CommonReference(image, f.constants);
                    const auto expected = CommonReference(scaled, scaledFixture.constants);
                    if (std::abs(expected[0] - gain * base[0]) > 1e-10 * (1 + expected[0]))
                        throw std::runtime_error("Positive gain homogeneity failed");
                    for (unsigned method = 0; method < 5; ++method)
                        verify(scaledFixture.run(pipelines[method].Get(), method == 4 ? reduce.Get() : nullptr),
                               expected);
                }
            std::cout << image.name << ": numeric reference PASS\n" << std::flush;
            if (timing && n >= 20 && timedSizes.insert({ image.width, image.height }).second)
                for (unsigned method = 0; method < 5; ++method)
                    f.timing(timing, image.name, names[method], pipelines[method].Get(),
                             method == 4 ? reduce.Get() : nullptr);
        }
        std::cout << "Common score numeric assertions: " << cases << " PASS; supplied-label agreement: " << correct
                  << '/' << labels << " (same fitted dataset, not unseen validation)\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
