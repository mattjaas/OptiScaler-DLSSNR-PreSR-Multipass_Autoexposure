#include "pch.h"
#include "edit_signal_probe_fixture.h"

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
        std::array<std::string, 5> names { "count64", "count256", "hist256", "tiled64", "tiledhist64" };
        std::array<ComPtr<ID3D12PipelineState>, 5> pipelines;
        for (unsigned i = 0; i < 5; ++i)
            pipelines[i] = r.pipeline(dir, names[i]);
        auto reduce = r.pipeline(dir, "reduce");
        auto reducehist = r.pipeline(dir, "reducehist");
        auto reduction = [&](unsigned method)
        {
            return method == 4 ? reducehist.Get() : method == 3 ? reduce.Get() : nullptr;
        };
        std::ofstream timing;
        if (!timingPath.empty())
        {
            timing.open(timingPath);
            if (!timing)
                throw std::runtime_error("Cannot create timing CSV");
            timing << std::setprecision(9)
                   << "scene,width,height,quantile_permille,method,block,quartet,position,variant,gpu_ms\n";
        }
        unsigned cases = 0;
        std::set<std::pair<UINT, UINT>> timedSizes;
        for (size_t n = 0; n < paths.size() + 5; ++n)
        {
            auto image = n < 5 ? Synthetic(unsigned(n)) : Load(paths[n - 5]);
            ProbeFixture f(r, image);
            for (UINT quantile : { 500u, 900u })
            {
                f.constants.quantilePermille = quantile;
                f.constants.threshold = quantile == 500 ? .021f : .14f;
                for (UINT py = 0; py < 8; ++py)
                    for (UINT px = 0; px < 8; ++px)
                    {
                        f.update(px, py);
                        const auto expected = Reference(image, f.constants);
                        for (unsigned method = 0; method < 5; ++method)
                        {
                            auto got = f.run(pipelines[method].Get(), reduction(method));
                            if (got[0] != expected.first || got[3] != 2304 ||
                                ((method == 2 || method == 4) &&
                                 !(got[1] <= expected.second && expected.second < got[2])))
                                throw std::runtime_error("Count/quantile assertion: " + image.name + "/" +
                                                         names[method]);
                            ++cases;
                        }
                    }
            }
            std::cout << image.name << ": 2 quantiles x 64 phases x 5 probe methods PASS\n" << std::flush;
            if (n == 1)
                for (UINT invalid : { 0u, 1001u, 0xffffffffu })
                {
                    f.constants.quantilePermille = invalid;
                    f.update(3, 3);
                    const auto expected = Reference(image, f.constants);
                    for (unsigned method = 0; method < 5; ++method)
                    {
                        auto got = f.run(pipelines[method].Get(), reduction(method));
                        if (got[0] != expected.first || got[3] != 2304 ||
                            ((method == 2 || method == 4) && !(got[1] <= expected.second && expected.second < got[2])))
                            throw std::runtime_error("Invalid quantile did not fall back to P90");
                        ++cases;
                    }
                }
            if (timing && n >= 5 && timedSizes.insert({ image.width, image.height }).second)
                for (UINT quantile : { 500u, 900u })
                {
                    f.constants.quantilePermille = quantile;
                    f.constants.threshold = quantile == 500 ? .021f : .14f;
                    for (unsigned method = 0; method < 5; ++method)
                        f.timing(timing, image.name, names[method], pipelines[method].Get(), reduction(method));
                }
        }
        std::cout << "Edit signal probe: " << cases << " independent CPU/GPU assertions PASS\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
