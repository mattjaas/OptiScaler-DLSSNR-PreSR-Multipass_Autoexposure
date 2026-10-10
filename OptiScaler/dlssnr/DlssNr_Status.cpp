#include <pch.h>
#include "DlssNr_Status.h"
#include "DlssNr_Benchmark.h"
#include <Config.h>
#include <Util.h>

#include <array>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <vector>

namespace DlssNr
{
namespace
{
struct PublishedStatus
{
    const void* owner = nullptr; // Identity only; never dereferenced.
    StatusSnapshot value;
};

std::mutex statusMutex;
std::array<PublishedStatus, 2> published;
ControlRequests requests;
} // namespace

StatusSnapshot ReadStatus(Backend backend)
{
    std::lock_guard lock(statusMutex);
    return published[static_cast<size_t>(backend)].value;
}

void PublishStatus(const void* owner, Backend backend, const StatusSnapshot& status)
{
    std::lock_guard lock(statusMutex);
    published[static_cast<size_t>(backend)] = { owner, status };
}

void ClearStatus(const void* owner)
{
    std::lock_guard lock(statusMutex);
    for (auto& status : published)
        if (status.owner == owner)
            status = {};
}

ControlRequests ReadControlRequests()
{
    std::lock_guard lock(statusMutex);
    return requests;
}

void RetryAfterFailure()
{
    std::lock_guard lock(statusMutex);
    ++requests.retryGeneration;
}

void RequestCapture(unsigned int frames)
{
    std::lock_guard lock(statusMutex);
    requests.captureFrames = frames;
    ++requests.captureGeneration;
}

void RequestStyleAnalysisCapture()
{
    std::lock_guard lock(statusMutex);
    ++requests.styleAnalysisCaptureGeneration;
}


// v9 six-pass A/B engine. Runs on the NR render thread, driven by fresh,
// fence-completed GPU timestamps. UI is only needed to arm or cancel it.
// Warmup includes the delayed query-ring samples from the previous variant.
// The test does NOT claim to measure isolated dispatch durations.
namespace
{
struct BenchmarkConfig
{
    float scale;
    uint32_t passes, radius, proxyFilter, upscaledFilter, debug;
    bool unlock, spatial;
    float strength;
    uint32_t mode;
    bool exact, shared, dynamic, paired, downClamp, classicStencil;
    bool tiled, strided, compact, weights, cache;
};
struct BenchmarkVariant
{
    int percent, mode;
    bool exact, tiled, strided, compact, weights, cache;
    const char* name;
    std::vector<double> samples;
};
std::mutex benchmarkMutex;
std::atomic<bool> benchmarkActive { false };
BenchmarkProgress benchmarkProgress;
BenchmarkConfig savedBenchmarkConfig {};
std::vector<BenchmarkVariant> benchmarkVariants;
unsigned benchmarkIndex = 0, benchmarkWarm = 0;
unsigned benchmarkWarmup = 90, benchmarkSamples = 160;
std::string benchmarkSetup;

BenchmarkConfig TakeBenchmarkConfig(const Config& c)
{
    return {
        c.DlssNrWorkingScale.value_or_default(),
        c.DlssNrPasses.value_or_default(),
        c.DlssNrGuidedResidualRadius.value_or_default(),
        c.DlssNrProxyDownscaleFilter.value_or_default(),
        c.DlssNrUpscaledResidualDownscaleFilter.value_or_default(),
        c.DlssNrDebugView.value_or_default(),
        c.DlssNrUnlockPasses.value_or_default(), c.DlssNrSpatialCompression.value_or_default(),
        c.DlssNrGuidedResidualGuideStrength.value_or_default(),
        c.DlssNrInterPassReconstruction.value_or_default(),
        c.DlssNrInterPassExactOptimized.value_or_default(),
        c.DlssNrInterPassSharedBilinear.value_or_default(),
        c.DlssNrInterPassDynamicSharedTaps.value_or_default(),
        c.DlssNrInterPassPairedArea.value_or_default(),
        c.DlssNrInterPassDownsampleClamp.value_or_default(),
        c.DlssNrInterPassClassicSharedStencil.value_or_default(),
        c.DlssNrInterPassTiledFusedArea.value_or_default(),
        c.DlssNrInterPassTiledStridedLoads.value_or_default(),
        c.DlssNrInterPassTiledCompact16.value_or_default(),
        c.DlssNrInterPassV9Weights.value_or_default(),
        c.DlssNrInterPassV9SourceCache.value_or_default()
    };
}
void RestoreBenchmarkConfig(Config& c, const BenchmarkConfig& v)
{
    c.DlssNrWorkingScale = v.scale;
    c.DlssNrPasses = v.passes;
    c.DlssNrGuidedResidualRadius = v.radius;
    c.DlssNrProxyDownscaleFilter = v.proxyFilter;
    c.DlssNrUpscaledResidualDownscaleFilter = v.upscaledFilter;
    c.DlssNrDebugView = v.debug;
    c.DlssNrUnlockPasses = v.unlock;
    c.DlssNrSpatialCompression = v.spatial;
    c.DlssNrGuidedResidualGuideStrength = v.strength;
    c.DlssNrInterPassReconstruction = v.mode;
    c.DlssNrInterPassExactOptimized = v.exact;
    c.DlssNrInterPassSharedBilinear = v.shared;
    c.DlssNrInterPassDynamicSharedTaps = v.dynamic;
    c.DlssNrInterPassPairedArea = v.paired;
    c.DlssNrInterPassDownsampleClamp = v.downClamp;
    c.DlssNrInterPassClassicSharedStencil = v.classicStencil;
    c.DlssNrInterPassTiledFusedArea = v.tiled;
    c.DlssNrInterPassTiledStridedLoads = v.strided;
    c.DlssNrInterPassTiledCompact16 = v.compact;
    c.DlssNrInterPassV9Weights = v.weights;
    c.DlssNrInterPassV9SourceCache = v.cache;
}
void ApplyBenchmarkVariant(Config& c, const BenchmarkVariant& v)
{
    c.DlssNrEnabled = true;
    c.DlssNrUnlockPasses = true;
    c.DlssNrPasses = 6u;
    c.DlssNrWorkingScale = float(v.percent) / 100.0f;
    c.DlssNrSpatialCompression = false;
    c.DlssNrDebugView = 0u;
    c.DlssNrProxyDownscaleFilter = 0u;
    c.DlssNrUpscaledResidualDownscaleFilter = 0u;
    c.DlssNrGuidedResidualRadius = 1u;
    c.DlssNrInterPassReconstruction = uint32_t(v.mode);
    c.DlssNrInterPassExactOptimized = v.exact;
    c.DlssNrInterPassSharedBilinear = true;
    c.DlssNrInterPassDynamicSharedTaps = true;
    c.DlssNrInterPassPairedArea = false;
    c.DlssNrInterPassDownsampleClamp = true;
    c.DlssNrInterPassClassicSharedStencil = true;
    c.DlssNrInterPassTiledFusedArea = v.tiled;
    c.DlssNrInterPassTiledStridedLoads = v.strided;
    c.DlssNrInterPassTiledCompact16 = v.compact;
    c.DlssNrInterPassV9Weights = v.weights;
    c.DlssNrInterPassV9SourceCache = v.cache;
}
void AddBenchmark(int scale, int mode, bool exact, bool tiled, bool strided, bool compact,
                  bool weights, bool cache, const char* name)
{
    benchmarkVariants.push_back({ scale, mode, exact, tiled, strided, compact, weights, cache, name, {} });
}
void BuildBenchmarkVariants()
{
    benchmarkVariants.clear();
    for (int scale : { 50, 65 })
    {
        AddBenchmark(scale, 0, false, false, false, false, false, false, "No inter-pass");
        AddBenchmark(scale, 1, false, false, false, false, false, false, "Classic reference");
        AddBenchmark(scale, 1, true, false, false, false, false, false, "Classic optimized");
        AddBenchmark(scale, 1, true, false, false, false, false, true,  "Classic source cache");
        AddBenchmark(scale, 2, false, false, false, false, false, false, "Fused reference");
        AddBenchmark(scale, 2, true, false, false, false, false, false, "Fused optimized");
        if (scale == 65)
        {
            AddBenchmark(scale, 2, true, true, false, false, false, false, "Tiled linear 20");
            AddBenchmark(scale, 2, true, true, false, true, false, false, "Tiled linear 16");
            AddBenchmark(scale, 2, true, true, true, false, false, false, "Tiled strided 20");
            AddBenchmark(scale, 2, true, true, true, true, false, false, "Tiled strided 16");
            AddBenchmark(scale, 2, true, true, false, true, true, false, "Linear 16 + weights");
            AddBenchmark(scale, 2, true, true, false, true, false, true, "Linear 16 + source cache");
            AddBenchmark(scale, 2, true, true, false, true, true, true, "Linear 16 + both");
        }
    }
}
void UpdateBenchmarkProgress()
{
    benchmarkProgress.current = unsigned(benchmarkIndex + 1);
    benchmarkProgress.total = unsigned(benchmarkVariants.size());
    benchmarkProgress.warmed = benchmarkWarm;
    benchmarkProgress.collected = unsigned(benchmarkVariants[benchmarkIndex].samples.size());
    benchmarkProgress.warmupTarget = benchmarkWarmup;
    benchmarkProgress.sampleTarget = benchmarkSamples;
    const auto& v = benchmarkVariants[benchmarkIndex];
    benchmarkProgress.variant = std::string("P") + std::to_string(v.percent) + " " + v.name;
}
void FinishBenchmark(bool cancelled)
{
    if (!benchmarkActive.exchange(false))
        return;
    RestoreBenchmarkConfig(*Config::Instance(), savedBenchmarkConfig);
    benchmarkProgress.active = false;
    if (cancelled)
    {
        benchmarkProgress.message = "Cancelled; original settings restored.";
        benchmarkVariants.clear();
        return;
    }
    try
    {
        const auto directory = Util::ExePath().parent_path() / "OptiScaler-NR-Benchmarks";
        std::filesystem::create_directories(directory);
        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm local {};
        localtime_s(&local, &now);
        std::ostringstream name;
        name << "NR-v9-" << std::put_time(&local, "%Y%m%d-%H%M%S");
        const auto csv = directory / (name.str() + ".csv");
        const auto info = directory / (name.str() + ".txt");
        std::ofstream out(csv);
        std::ofstream description(info);
        if (!out || !description)
            throw std::runtime_error("Cannot create benchmark report.");
        out << "scale_percent,variant,mode,exact,tiled,strided,compact,weights,source_cache,"
               "samples,mean_ms,median_ms,p95_ms,min_ms,max_ms,stddev_ms\n";
        out << std::fixed << std::setprecision(5);
        for (const auto& v : benchmarkVariants)
        {
            if (v.samples.empty())
                continue;
            auto sorted = v.samples;
            std::sort(sorted.begin(), sorted.end());
            const double mean = std::accumulate(sorted.begin(), sorted.end(), 0.0) / sorted.size();
            double variance = 0.0;
            for (double ms : sorted)
                variance += (ms - mean) * (ms - mean);
            const size_t n = sorted.size();
            const double median = n % 2 ? sorted[n/2] : (sorted[n/2-1] + sorted[n/2]) * 0.5;
            const double p95 = sorted[std::min(n-1, size_t(std::ceil(n * 0.95))-1)];
            out << v.percent << ',' << '"' << v.name << '"' << ',' << v.mode << ',' << v.exact << ','
                << v.tiled << ',' << v.strided << ',' << v.compact << ',' << v.weights << ',' << v.cache
                << ',' << n << ',' << mean << ',' << median << ',' << p95 << ','
                << sorted.front() << ',' << sorted.back() << ',' << std::sqrt(variance/n) << '\n';
        }
        description << "OptiScaler DLSS NR v9 automatic six-pass GPU benchmark\n"
                    << "P50 and P65 only; all variants: 6 passes, Area, radius 1, "
                       "dynamic/shared bilinear ON, paired OFF, shadow/frequency shaping left unchanged.\n"
                    << "Each case has " << benchmarkWarmup << " fresh warmup timestamps, then "
                    << benchmarkSamples << " fresh measured timestamps.\n"
                    << "Raw completed DX12 timestamp sample, NOT UI time-window average.\n"
                    << "Elapsed GPU NR interval includes NGX passes, inter-pass, queue waits/overlap; "
                       "not isolated shader time.\n"
                    << "Original user settings restored after benchmark.\n"
                    << "Ensure static game scene, consistent GPU clocks, and compare median/variance.\n"
                    << "User settings at start: " << benchmarkSetup << "\n"
                    << "CSV: " << csv.filename().string() << "\n";
        out.flush();
        description.flush();
        if (!out || !description)
            throw std::runtime_error("Benchmark file write failed.");
        benchmarkProgress.message = std::string("Saved: ") + csv.string();
    }
    catch (const std::exception& ex)
    {
        benchmarkProgress.message = std::string("Benchmark completed, report write FAILED: ") + ex.what();
    }
    benchmarkVariants.clear();
}
} // namespace

void StartInterPassBenchmark(unsigned warmupSamples, unsigned measuredSamples)
{
    std::lock_guard lock(benchmarkMutex);
    if (benchmarkActive.load())
        return;
    auto& c = *Config::Instance();
    if (!c.DlssNrEnabled.value_or_default())
    {
        benchmarkProgress.message = "Enable Neural Rendering first.";
        return;
    }
    savedBenchmarkConfig = TakeBenchmarkConfig(c);
    benchmarkWarmup = std::clamp(warmupSamples, 40u, 600u);
    benchmarkSamples = std::clamp(measuredSamples, 40u, 2000u);
    benchmarkIndex = benchmarkWarm = 0;
    BuildBenchmarkVariants();
    benchmarkSetup = "guide strength=" + std::to_string(savedBenchmarkConfig.strength) +
                     ", source scale=" + std::to_string(savedBenchmarkConfig.scale);
    ApplyBenchmarkVariant(c, benchmarkVariants.front());
    benchmarkProgress = {};
    benchmarkProgress.active = true;
    UpdateBenchmarkProgress();
    benchmarkProgress.message = "Running; keep the scene static. You may close the menu.";
    benchmarkActive.store(true);
}
void CancelInterPassBenchmark()
{
    std::lock_guard lock(benchmarkMutex);
    FinishBenchmark(true);
}
BenchmarkProgress ReadInterPassBenchmark()
{
    std::lock_guard lock(benchmarkMutex);
    return benchmarkProgress;
}
void BenchmarkGpuSample(double rawGpuMs, bool modelRunning)
{
    if (!benchmarkActive.load(std::memory_order_relaxed))
        return;
    std::lock_guard lock(benchmarkMutex);
    if (!benchmarkActive.load() || !modelRunning || !std::isfinite(rawGpuMs) ||
        rawGpuMs <= 0.0 || rawGpuMs > 1000.0)
        return;
    if (benchmarkWarm < benchmarkWarmup)
        ++benchmarkWarm;
    else
        benchmarkVariants[benchmarkIndex].samples.push_back(rawGpuMs);
    if (benchmarkVariants[benchmarkIndex].samples.size() == benchmarkSamples)
    {
        if (++benchmarkIndex == benchmarkVariants.size())
        {
            FinishBenchmark(false);
            return;
        }
        benchmarkWarm = 0;
        ApplyBenchmarkVariant(*Config::Instance(), benchmarkVariants[benchmarkIndex]);
    }
    UpdateBenchmarkProgress();
}

std::optional<double> LastGpuTime() { return ReadStatus(Backend::Dx12).gpuTime; }
} // namespace DlssNr
