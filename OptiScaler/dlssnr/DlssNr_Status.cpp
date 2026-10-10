#include "pch.h"
#include "DlssNr_Status.h"
#include "DlssNr_Benchmark.h"
#include "DlssNr_BenchmarkCases.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
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
#include <map>

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
    bool enabled;
    float scale;
    uint32_t passes, radius, proxyFilter, upscaledFilter, debug;
    bool unlock, spatial;
    float strength;
    uint32_t mode;
    bool exact, shared, dynamic, paired, downClamp, classicStencil;
    bool tiled, strided, compact, weights, cache;
    bool v11Spatial, v11QuadFill;
    bool v12Interior, v12Axes;
    bool v13Area, v13Mode28;
    bool v14RgbTile, v14Linear20, v14GuideOne;
};
struct BenchmarkVariant
{
    int percent, mode;
    bool exact, tiled, strided, compact, weights, cache;
    const char* name;
    std::vector<double> samples;
    unsigned nativeW = 0, nativeH = 0, workW = 0, workH = 0, effectivePasses = 0;
    bool v11Spatial = false, v11QuadFill = false;
    bool v12Interior = false, v12Axes = false;
    bool v13Area = false;
    bool v13Mode28 = false;
    bool v14RgbTile = false, v14Linear20 = false, v14GuideOne = false;
};
std::mutex benchmarkMutex;
std::atomic<bool> benchmarkActive { false };
BenchmarkProgress benchmarkProgress;
BenchmarkConfig savedBenchmarkConfig {};
std::vector<BenchmarkVariant> benchmarkVariants;
unsigned benchmarkIndex = 0, benchmarkWarm = 0;
unsigned benchmarkWarmup = 90, benchmarkSamples = 160;
std::string benchmarkSetup;
std::string benchmarkAdapterInfo;
unsigned benchmarkNativeW = 0, benchmarkNativeH = 0, benchmarkWorkW = 0,
         benchmarkWorkH = 0, benchmarkEffectivePasses = 0;

BenchmarkConfig TakeBenchmarkConfig(const Config& c)
{
    return {
        c.DlssNrEnabled.value_or_default(),
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
        c.DlssNrInterPassV9SourceCache.value_or_default(),
        c.DlssNrInterPassV11Spatial.value_or_default(),
        c.DlssNrInterPassV11QuadFill.value_or_default(),
        c.DlssNrInterPassV12Interior.value_or_default(),
        c.DlssNrInterPassV12Axes.value_or_default(),
        c.DlssNrInterPassV13Area.value_or_default(),
        c.DlssNrInterPassV13Mode28.value_or_default(),
        c.DlssNrInterPassV14RgbTile.value_or_default(),
        c.DlssNrInterPassV14Linear20.value_or_default(),
        c.DlssNrInterPassV14GuideOne.value_or_default()
    };
}
void RestoreBenchmarkConfig(Config& c, const BenchmarkConfig& v)
{
    c.DlssNrEnabled = v.enabled;
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
    c.DlssNrInterPassV11Spatial = v.v11Spatial;
    c.DlssNrInterPassV11QuadFill = v.v11QuadFill;
    c.DlssNrInterPassV12Interior = v.v12Interior;
    c.DlssNrInterPassV12Axes = v.v12Axes;
    c.DlssNrInterPassV13Area = v.v13Area;
    c.DlssNrInterPassV13Mode28 = v.v13Mode28;
    c.DlssNrInterPassV14RgbTile = v.v14RgbTile;
    c.DlssNrInterPassV14Linear20 = v.v14Linear20;
    c.DlssNrInterPassV14GuideOne = v.v14GuideOne;
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
    c.DlssNrInterPassV11Spatial = v.v11Spatial;
    c.DlssNrInterPassV11QuadFill = v.v11QuadFill;
    c.DlssNrInterPassV12Interior = v.v12Interior;
    c.DlssNrInterPassV12Axes = v.v12Axes;
    c.DlssNrInterPassV13Area = v.v13Area;
    c.DlssNrInterPassV13Mode28 = v.v13Mode28;
    c.DlssNrInterPassV14RgbTile = v.v14RgbTile;
    c.DlssNrInterPassV14Linear20 = v.v14Linear20;
    c.DlssNrInterPassV14GuideOne = v.v14GuideOne;
}
void AddBenchmark(int scale, int mode, bool exact, bool tiled, bool strided, bool compact,
                  bool weights, bool cache, const char* name)
{
    benchmarkVariants.push_back({ scale, mode, exact, tiled, strided, compact, weights, cache, name, {} });
}
void BuildBenchmarkVariants()
{
    benchmarkVariants.clear();
    for (const auto& v : kInterPassBenchmarkCases)
    {
        AddBenchmark(v.percent, v.mode, v.exact, v.tiled, false, v.compact,
                     v.weights, false, v.name);
        benchmarkVariants.back().v13Mode28 = v.mode28;
        benchmarkVariants.back().v14RgbTile = v.rgb;
        benchmarkVariants.back().v14Linear20 = v.linear20;
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
        name << "NR-v15-" << std::put_time(&local, "%Y%m%d-%H%M%S");
        const auto csv = directory / (name.str() + ".csv");
        const auto info = directory / (name.str() + ".txt");
        const auto raw = directory / (name.str() + ".samples.csv");
        std::ofstream out(csv);
        std::ofstream individual(raw);
        std::ofstream description(info);
        if (!out || !description || !individual)
            throw std::runtime_error("Cannot create benchmark report.");
        out << "scale_percent,variant,mode,exact,tiled,strided,compact,weights,source_cache,v11_spatial,v11_quadfill,v12_interior,v12_axes,v13_area,v13_mode28,v14_rgb_tile,v14_linear20,v14_guide_one,"
               "native_width,native_height,work_width,work_height,effective_passes,"
               "samples,mean_ms,median_ms,p95_ms,min_ms,max_ms,stddev_ms,delta_vs_off_median_ms\n";
        individual << "scale_percent,variant,sample_index,total_nr_gpu_ms\n";
        out << std::fixed << std::setprecision(5);
        individual << std::fixed << std::setprecision(6);
        std::map<int, double> baselines;
        for (const auto& v : benchmarkVariants)
        {
            if (v.mode != 0 || v.samples.empty())
                continue;
            auto sorted = v.samples;
            std::sort(sorted.begin(), sorted.end());
            const size_t n = sorted.size();
            const double median = n % 2 ? sorted[n/2] : (sorted[n/2 - 1] + sorted[n/2]) * 0.5;
            baselines[v.percent] = median;
        }
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
            const double baseline = baselines.at(v.percent);
            out << v.percent << ',' << '"' << v.name << '"' << ',' << v.mode << ',' << v.exact << ','
                << v.tiled << ',' << v.strided << ',' << v.compact << ',' << v.weights << ',' << v.cache
                << ',' << v.v11Spatial << ',' << v.v11QuadFill
                << ',' << v.v12Interior << ',' << v.v12Axes
                << ',' << v.v13Area << ',' << v.v13Mode28
                << ',' << v.v14RgbTile << ',' << v.v14Linear20 << ',' << v.v14GuideOne
                << ',' << v.nativeW << ',' << v.nativeH << ',' << v.workW << ',' << v.workH << ','
                << v.effectivePasses << ',' << n << ',' << mean << ',' << median << ',' << p95 << ','
                << sorted.front() << ',' << sorted.back() << ',' << std::sqrt(variance/n)
                << ',' << median - baseline << '\n';
            for (size_t i = 0; i < v.samples.size(); ++i)
                individual << v.percent << ',' << '"' << v.name << '"' << ',' << i << ',' << v.samples[i] << '\n';
        }
        description << "OptiScaler DLSS NR v15 short six-pass GPU benchmark\n"
                    << "ONE measurement per configuration: no ABBA and no repeated sweeps.\n"
                    << "P50/P59/P65; all variants: 6 passes, Area, radius 1, "
                       "dynamic/shared bilinear ON, paired OFF, shadow/frequency shaping left unchanged.\n"
                    << "Each case has " << benchmarkWarmup << " fresh warmup timestamps, then "
                    << benchmarkSamples << " fresh measured timestamps.\n"
                    << "Raw completed DX12 timestamp sample, NOT UI time-window average.\n"
                    << "Elapsed GPU NR interval includes NGX passes, inter-pass, queue waits/overlap; "
                       "not isolated shader time.\n"
                    << "Original user settings restored after benchmark.\n"
                    << "GPU adapter: " << (benchmarkAdapterInfo.empty() ? "unavailable" : benchmarkAdapterInfo) << "\n"
                    << "v15 short sweep: 14 cases (3 P50, 5 P59, 6 P65), each measured once.\n"
                    << "Per scale: No inter-pass and unoptimized Fused reference retained.\n"
                    << "P50: Fused optimized. P59: Linear20 weights OFF, isolated Mode28 weights, and RGB.\n"
                    << "P65: Linear16 weights OFF, weights v10, Mode28 v13, and RGB v14.\n"
                    << "Cache, strided, v11/v12, bounded Area and guide-one excluded from the routine sweep; settings remain available.\n"
                    << "Configured experiment flags are reported; PSO creation failure logs a warning "
                       "and may fall back. Check the game log before accepting a speed comparison.\n"
                    << "Source cache: v10 Linear16 Fused uses 16x16 float3 FP32 source/model arrays; "
                       "v10 Classic uses 12x12 float3 FP32 arrays. "
                       "Actual LDS allocation/register occupancy require external GPU profiler.\n"
                    << "No new inter-pass GPU timestamp pairs: isolated inter-pass time is not measured. "
                       "The delta_vs_off_median_ms column compares full NR intervals at the same scale.\n"
                    << "Do not assume same GPU clocks/temperature: not measured. "
                       "Maintain a static game scene and examine the raw distribution.\n"
                    << "Actual native/working dimensions and effective pass count: CSV per case.\n"
                    << "User settings at start: " << benchmarkSetup << "\n"
                    << "Summary CSV: " << csv.filename().string() << "\n"
                    << "Raw samples CSV: " << raw.filename().string() << "\n";
        out.flush();
        individual.flush();
        description.flush();
        if (!out || !description || !individual)
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
    benchmarkAdapterInfo.clear();
    benchmarkNativeW = benchmarkNativeH = benchmarkWorkW = benchmarkWorkH = benchmarkEffectivePasses = 0;
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
    // Do not accidentally benchmark a partially prepared six-feature chain.
    if (benchmarkEffectivePasses != 6u || benchmarkNativeW == 0u || benchmarkWorkW == 0u)
    {
        benchmarkProgress.message = "Waiting for all 6 DLSS NR features and actual texture dimensions.";
        return;
    }
    auto& variant = benchmarkVariants[benchmarkIndex];
    variant.nativeW = benchmarkNativeW;
    variant.nativeH = benchmarkNativeH;
    variant.workW = benchmarkWorkW;
    variant.workH = benchmarkWorkH;
    variant.effectivePasses = benchmarkEffectivePasses;
    if (benchmarkWarm < benchmarkWarmup)
        ++benchmarkWarm;
    else
        variant.samples.push_back(rawGpuMs);
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

void BenchmarkReportGeometry(unsigned nativeW, unsigned nativeH, unsigned workW, unsigned workH,
                             unsigned effectivePasses)
{
    if (!benchmarkActive.load(std::memory_order_relaxed))
        return;
    std::lock_guard lock(benchmarkMutex);
    benchmarkNativeW = nativeW;
    benchmarkNativeH = nativeH;
    benchmarkWorkW = workW;
    benchmarkWorkH = workH;
    benchmarkEffectivePasses = effectivePasses;
}
void BenchmarkReportDevice(ID3D12Device* device)
{
    if (!benchmarkActive.load(std::memory_order_relaxed) || !device)
        return;
    std::lock_guard lock(benchmarkMutex);
    if (!benchmarkAdapterInfo.empty())
        return;
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    const auto luid = device->GetAdapterLuid();
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) ||
        FAILED(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter))))
    {
        benchmarkAdapterInfo = "DXGI adapter unavailable";
        return;
    }
    DXGI_ADAPTER_DESC1 desc {};
    if (FAILED(adapter->GetDesc1(&desc)))
    {
        benchmarkAdapterInfo = "DXGI adapter description unavailable";
        return;
    }
    char displayName[512] {};
    WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, displayName,
                        static_cast<int>(sizeof(displayName)), nullptr, nullptr);
    std::ostringstream details;
    details << displayName << " VendorID=0x" << std::hex << desc.VendorId
            << " DeviceID=0x" << desc.DeviceId << std::dec
            << " DedicatedVRAM_MB=" << (desc.DedicatedVideoMemory / (1024 * 1024))
            << " LUID=" << luid.HighPart << ":" << luid.LowPart;
    LARGE_INTEGER version {};
    if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &version)))
        details << " DXGI_driver_version=" << HIWORD(version.HighPart) << "."
                << LOWORD(version.HighPart) << "."
                << HIWORD(version.LowPart) << "." << LOWORD(version.LowPart);
    else
        details << " DXGI_driver_version=unavailable";
    benchmarkAdapterInfo = details.str();
}

std::optional<double> LastGpuTime() { return ReadStatus(Backend::Dx12).gpuTime; }
} // namespace DlssNr
