#include "pch.h"
#include "DlssNr_Status.h"
#include "DlssNr_Benchmark.h"
#include "DlssNr_BenchmarkCases.h"
#include "PassProfiles.h"
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
#include <limits>

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


// Six-pass ABBA engine. Runs on the NR render thread, driven by fresh,
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
};
struct BenchmarkVariant
{
    int percent, mode, forcedPath;
    const char* name;
    std::vector<double> samples;
    unsigned nativeW = 0, nativeH = 0, workW = 0, workH = 0, effectivePasses = 0;
    std::string path;
    double historicalGain = 0.0;
};
std::atomic<int> benchmarkOverride { -1 };
std::string benchmarkPath;
std::mutex benchmarkMutex;
std::atomic<bool> benchmarkActive { false };
BenchmarkProgress benchmarkProgress;
BenchmarkConfig savedBenchmarkConfig {};
std::vector<BenchmarkVariant> benchmarkVariants;
unsigned benchmarkIndex = 0, benchmarkWarm = 0;
unsigned benchmarkWarmup = 90, benchmarkSamples = 160;
InterPassBenchmarkProfile benchmarkProfile = InterPassBenchmarkProfile::Regular;
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
        c.DlssNrInterPassReconstruction.value_or_default()
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
    c.DlssNrGuidedResidualGuideStrength = 1.0f;
    c.DlssNrInterPassReconstruction = uint32_t(v.mode);
    benchmarkOverride.store(v.forcedPath, std::memory_order_relaxed);
    benchmarkPath.clear();
}
void AddBenchmarkScale(int scale, InterPass::Path expected, double historicalGain)
{
    const auto add = [&](int mode, int path, const char* name)
    {
        benchmarkVariants.push_back({ scale, mode, path, name, {} });
        benchmarkVariants.back().historicalGain = historicalGain;
    };
    add(0, -1, "No inter-pass");
    add(1, -1, "Classic reference");
    add(2, -1, "Fused reference");
    if (expected != InterPass::Path::ClassicOptimized)
        add(3, int(InterPass::Path::ClassicOptimized), "Classic optimized control");
    else if (scale < 50)
        add(3, int(InterPass::Path::Rgb20), "RGB20 optimized control");
    else
        add(3, int(InterPass::Path::FusedOptimized), "Fused optimized control");
    // ABBA checks the same retained winner against automatic selection.
    add(3, int(expected), "Expected path control");
    add(3, -1, "Inter-pass optimized");
    add(3, -1, "Inter-pass optimized");
    add(3, int(expected), "Expected path control");
}
void AddLowBenchmarkScale(int scale)
{
    const auto measured = std::find_if(kInterPassMeasuredLowScales.begin(),
        kInterPassMeasuredLowScales.end(), [&](const auto& v) { return v.percent == scale; });
    if (measured != kInterPassMeasuredLowScales.end())
        AddBenchmarkScale(scale, measured->expected, measured->historicalGainVsFusedMs);
    else
    {
        // Unmeasured slider scales: predicted control, not historical evidence.
        const auto expected = scale >= 42 ? InterPass::Path::Rgb20 : InterPass::Path::ClassicOptimized;
        AddBenchmarkScale(scale, expected, std::numeric_limits<double>::quiet_NaN());
    }
}
void BuildBenchmarkVariants()
{
    benchmarkVariants.clear();
    const int current = std::clamp(int(std::lround(savedBenchmarkConfig.scale * 100.0f)), 25, 99);
    if (benchmarkProfile == InterPassBenchmarkProfile::Boundary)
    {
        for (const int scale : kInterPassBoundaryBenchmarkScales) AddLowBenchmarkScale(scale);
        return; // Exactly P41/P42, regardless of the current slider setting.
    }
    if (benchmarkProfile == InterPassBenchmarkProfile::Below50)
    {
        for (const int scale : kInterPassLowVerificationScales) AddLowBenchmarkScale(scale);
        if (current < 50 && std::find(kInterPassLowVerificationScales.begin(),
            kInterPassLowVerificationScales.end(), current) == kInterPassLowVerificationScales.end())
            AddLowBenchmarkScale(current);
        return;
    }
    for (const auto& v : kInterPassBenchmarkScales)
        AddBenchmarkScale(v.percent, v.expected, v.historicalGainVsFusedMs);
    if (current != 50 && current != 59 && current != 65)
    {
        if (current < 50) AddLowBenchmarkScale(current);
        else
        {
            const auto expected = current >= 51 && current <= 90
                ? (current >= 60 ? InterPass::Path::Rgb16 : InterPass::Path::Rgb20)
                : InterPass::Path::ClassicOptimized;
            AddBenchmarkScale(current, expected, std::numeric_limits<double>::quiet_NaN());
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
    benchmarkOverride.store(-1, std::memory_order_relaxed);
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
        const char* prefix = benchmarkProfile == InterPassBenchmarkProfile::Boundary ? "NR-v20-boundary-" :
                             benchmarkProfile == InterPassBenchmarkProfile::Below50 ? "NR-v20-low-" : "NR-v20-auto-";
        name << prefix << std::put_time(&local, "%Y%m%d-%H%M%S");
        const auto csv = directory / (name.str() + ".csv");
        const auto info = directory / (name.str() + ".txt");
        const auto raw = directory / (name.str() + ".samples.csv");
        std::ofstream out(csv);
        std::ofstream individual(raw);
        std::ofstream description(info);
        if (!out || !description || !individual)
            throw std::runtime_error("Cannot create benchmark report.");
        out << "scale_percent,variant,mode,forced_path,selected_path,native_width,native_height,"
               "work_width,work_height,effective_passes,samples,mean_ms,median_ms,p95_ms,min_ms,max_ms,stddev_ms,"
               "delta_vs_off_median_ms,delta_vs_fused_reference_ms,delta_vs_expected_path_ms,"
               "historical_gain_vs_fused_ms,actual_gain_vs_fused_ms,path_matches_control,"
               "window_medians_ms,window_median_spread_ms\n";
        individual << "scale_percent,variant,window,sample_index,total_nr_gpu_ms\n";
        out << std::fixed << std::setprecision(5);
        individual << std::fixed << std::setprecision(6);
        std::vector<BenchmarkVariant> combined;
        for (size_t window = 0; window < benchmarkVariants.size(); ++window)
        {
            const auto& v = benchmarkVariants[window];
            if (v.samples.empty()) continue;
            for (size_t i = 0; i < v.samples.size(); ++i)
                individual << v.percent << ',' << '"' << v.name << '"' << ',' << window << ',' << i << ',' << v.samples[i] << '\n';
            auto it = std::find_if(combined.begin(), combined.end(), [&](const auto& existing)
                { return existing.percent == v.percent && std::string(existing.name) == v.name; });
            if (it == combined.end()) combined.push_back(v);
            else
            {
                it->samples.insert(it->samples.end(), v.samples.begin(), v.samples.end());
                if (it->path != v.path) it->path = "Mixed routes";
            }
        }
        const auto medianOf = [](const BenchmarkVariant& v)
        {
            auto sorted = v.samples;
            std::sort(sorted.begin(), sorted.end());
            const size_t n = sorted.size();
            return n % 2 ? sorted[n/2] : (sorted[n/2-1] + sorted[n/2]) * 0.5;
        };
        std::map<int, double> off, fused, expected;
        std::map<int, std::string> expectedPath;
        for (const auto& v : combined)
        {
            if (v.mode == 0) off[v.percent] = medianOf(v);
            if (v.mode == 2) fused[v.percent] = medianOf(v);
            if (std::string(v.name) == "Expected path control")
            {
                expected[v.percent] = medianOf(v);
                expectedPath[v.percent] = v.path;
            }
        }
        for (const auto& v : combined)
        {
            auto sorted = v.samples;
            std::sort(sorted.begin(), sorted.end());
            const size_t n = sorted.size();
            const double mean = std::accumulate(sorted.begin(), sorted.end(), 0.0) / n;
            double variance = 0.0;
            for (double ms : sorted) variance += (ms - mean) * (ms - mean);
            const double median = medianOf(v);
            const double p95 = sorted[std::min(n-1, size_t(std::ceil(n * 0.95))-1)];
            std::vector<double> windows;
            for (const auto& window : benchmarkVariants)
                if (window.percent == v.percent && std::string(window.name) == v.name && !window.samples.empty())
                    windows.push_back(medianOf(window));
            out << v.percent << ',' << '"' << v.name << '"' << ',' << v.mode << ',' << v.forcedPath << ','
                << '"' << v.path << '"' << ',' << v.nativeW << ',' << v.nativeH << ',' << v.workW << ',' << v.workH << ','
                << v.effectivePasses << ',' << n << ',' << mean << ',' << median << ',' << p95 << ','
                << sorted.front() << ',' << sorted.back() << ',' << std::sqrt(variance/n) << ','
                << median - off.at(v.percent) << ',' << median - fused.at(v.percent) << ','
                << median - expected.at(v.percent) << ',' << v.historicalGain << ','
                << fused.at(v.percent) - median << ',' << (v.path == expectedPath.at(v.percent)) << ",\"";
            for (size_t i=0;i<windows.size();++i)
                out << (i ? "|" : "") << windows[i];
            const auto range = std::minmax_element(windows.begin(), windows.end());
            out << "\"," << *range.second - *range.first << '\n';
        }
        description << "OptiScaler six-pass inter-pass GPU comparison\n"
                    << (benchmarkProfile == InterPassBenchmarkProfile::Boundary ?
                        "Boundary profile: exactly P41/P42; current scale is not appended.\n" :
                        benchmarkProfile == InterPassBenchmarkProfile::Below50 ?
                        "Below-50 verification: P33/P40/P41/P42/P49 plus current lower scale if different.\n" :
                        "Regular profile: P50/P59/P65 plus current rounded percentage when different.\n")
                    << "Off, Classic reference and Fused reference retained.\n"
                    << "All scales: expected path A / automatic B / B / A plus alternate retained path.\n"
                    << "Measured low controls: P33/P41 Classic; P40/P42/P45/P49 RGB20. Unmeasured scales are predictions.\n"
                    << "Generic Fused optimized below 50 omitted: slower at every supplied P33/P40/P41/P42/P45/P49.\n"
                    << "Independent warmup for each window; symmetric order reduces linear drift.\n"
                    << "Warmup=" << benchmarkWarmup << ", samples/window=" << benchmarkSamples << "\n"
                    << "Repeated windows pooled in summary; raw CSV retains window and sample indices.\n"
                    << "Whole NR GPU interval includes NGX, transitions and overlap, not isolated shader time.\n"
                    << "Area, radius 1, guide strength 1, 6 passes; user shadow/frequency shaping unchanged.\n"
                    << "GPU adapter: " << benchmarkAdapterInfo << "\n"
                    << "User settings at start: " << benchmarkSetup << "\n"
                    << "Historical gain is last RTX4090/4K/guide-one v16/v18/v19 result vs Fused reference, not absolute time.\n"
                    << "Historical values do not apply to other scenes/GPUs/settings. Current control is decisive.\n"
                    << "Automatic should match the independent forced control in ABBA; compare the alternate and references too.\n"
                    << "Controls describe measured 4K geometry. Other dimensions, especially rounded P40, may select differently.\n"
                    << "RGB20 below 50 is a guarded PSO: oversized groups use direct reconstruction with isolated weights.\n"
                    << "selected_path reports actual PSO, not whether each group used LDS.\n"
                    << "Actual geometry rounding may affect compact eligibility at nominal 60%; inspect selected_path.\n"
                    << "PSO failure reports fallback; no extra per-frame GPU timing outside benchmark.\n"
                    << "Clocks/temperature uncontrolled. Keep scene fixed, inspect median, P95 and dispersion.\n"
                    << "Original settings restored, including inter-pass selection.\n";
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

void StartInterPassBenchmark(unsigned warmupSamples, unsigned measuredSamples, InterPassBenchmarkProfile profile)
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
    benchmarkProfile = profile;
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
    const auto& first = benchmarkVariants.front();
    if ((first.nativeW && (first.nativeW != benchmarkNativeW || first.nativeH != benchmarkNativeH)) ||
        (variant.nativeW && (variant.workW != benchmarkWorkW || variant.workH != benchmarkWorkH)))
    {
        FinishBenchmark(true);
        benchmarkProgress.message = "Cancelled: texture dimensions changed during a measurement; settings restored.";
        return;
    }
    variant.nativeW = benchmarkNativeW;
    variant.nativeH = benchmarkNativeH;
    variant.workW = benchmarkWorkW;
    variant.workH = benchmarkWorkH;
    variant.effectivePasses = benchmarkEffectivePasses;
    variant.path = benchmarkPath;
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

int BenchmarkInterPassOverride()
{
    return benchmarkActive.load(std::memory_order_relaxed) ? benchmarkOverride.load(std::memory_order_relaxed) : -1;
}
void BenchmarkReportInterPassPath(const char* path)
{
    if (!benchmarkActive.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(benchmarkMutex);
    benchmarkPath = path;
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
