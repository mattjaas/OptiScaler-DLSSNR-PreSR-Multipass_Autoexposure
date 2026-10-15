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
    uint32_t passes, radius, proxyFilter, upscaledFilter, debug, transfer;
    bool unlock, spatial;
    float strength;
    uint32_t mode;
    bool neutralInterPassLow;
};
struct BenchmarkVariant
{
    int percent, mode, forcedPath;
    const char* name;
    std::vector<double> samples;
    unsigned nativeW = 0, nativeH = 0, workW = 0, workH = 0, effectivePasses = 0;
    unsigned expectedMask = 0, mask = 0, corrections = 0;
    std::string path;
    double historicalGain = 0.0;
    int comparison = 0; // Experimental ABBA block; 0 denotes unpaired reference windows.
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
bool benchmarkInspectEachWindow = false;
std::string benchmarkSetup;
std::string benchmarkAdapterInfo;
unsigned benchmarkNativeW = 0, benchmarkNativeH = 0, benchmarkWorkW = 0,
         benchmarkWorkH = 0, benchmarkEffectivePasses = 0;
unsigned benchmarkExpectedMask = 0, benchmarkMask = 0, benchmarkCorrections = 0;

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
        c.DlssNrTransfer.value_or_default(),
        c.DlssNrUnlockPasses.value_or_default(), c.DlssNrSpatialCompression.value_or_default(),
        c.DlssNrGuidedResidualGuideStrength.value_or_default(),
        c.DlssNrInterPassReconstruction.value_or_default(),
        c.DlssNrInterPassNeutralLow.value_or_default()
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
    c.DlssNrTransfer = v.transfer;
    c.DlssNrUnlockPasses = v.unlock;
    c.DlssNrSpatialCompression = v.spatial;
    c.DlssNrGuidedResidualGuideStrength = v.strength;
    c.DlssNrInterPassReconstruction = v.mode;
    c.DlssNrInterPassNeutralLow = v.neutralInterPassLow;
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
    c.DlssNrInterPassNeutralLow = savedBenchmarkConfig.neutralInterPassLow;
    if (benchmarkProfile == InterPassBenchmarkProfile::FastQuality ||
        benchmarkProfile == InterPassBenchmarkProfile::FastBelow50 ||
        benchmarkProfile == InterPassBenchmarkProfile::FastCachedLow)
        c.DlssNrTransfer = 9u; // Temporal DLAA residual + P100-guided, as requested.
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
    if (benchmarkProfile == InterPassBenchmarkProfile::FastCachedLow)
    {
        for (const auto& scale : kInterPassBenchmarkScales)
        {
            const auto add = [&](int mode, const char* name, int comparison)
            {
                benchmarkVariants.push_back({scale.percent, mode, -1, name, {}});
                benchmarkVariants.back().historicalGain = std::numeric_limits<double>::quiet_NaN();
                benchmarkVariants.back().comparison = comparison;
            };
            add(0, "No inter-pass", 0);
            add(1, "Classic reference", 0);
            add(2, "Fused reference", 0);
            add(3, "Inter-pass optimized", 0);
            add(4, "Inter-pass fast", 1);
            add(8, "Fast cached low", 1);
            add(8, "Fast cached low", 1);
            add(4, "Inter-pass fast", 1);
        }
        return; // P50/P59/P65: 18 configurations / 24 windows, separate warmup and Fast/cache ABBA.
    }
    if (benchmarkProfile == InterPassBenchmarkProfile::FastQuality ||
        benchmarkProfile == InterPassBenchmarkProfile::FastBelow50)
    {
        const auto addScale = [&](int percent)
        {
            const auto add = [&](int mode, const char* name, int comparison)
            {
                benchmarkVariants.push_back({percent, mode, -1, name, {}});
                auto& v = benchmarkVariants.back();
                v.historicalGain = std::numeric_limits<double>::quiet_NaN();
                v.comparison = comparison;
            };
            add(0, "No inter-pass", 0);
            add(1, "Classic reference", 0);
            add(2, "Fused reference", 0);
            add(3, "Inter-pass optimized", 1);
            add(4, "Inter-pass fast", 1);
            add(4, "Inter-pass fast", 1);
            add(3, "Inter-pass optimized", 1);
        };
        const int current = std::clamp(int(std::lround(savedBenchmarkConfig.scale * 100.0f)), 25, 200);
        if (benchmarkProfile == InterPassBenchmarkProfile::FastBelow50)
        {
            for (const int percent : kInterPassLowVerificationScales) addScale(percent);
            if (current < 50 && std::find(kInterPassLowVerificationScales.begin(),
                kInterPassLowVerificationScales.end(), current) == kInterPassLowVerificationScales.end())
                addScale(current);
        }
        else
            for (const auto& scale : kInterPassBenchmarkScales) addScale(scale.percent);
        return; // Five configurations / seven independent windows per scale; one Optimized/Fast ABBA.
    }
    const int current = std::clamp(int(std::lround(savedBenchmarkConfig.scale * 100.0f)), 25, 200);
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
    if (current <= 65 && current != 50 && current != 59 && current != 65)
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
        const char* prefix = benchmarkProfile == InterPassBenchmarkProfile::FastCachedLow ? "NR-v37-cached-low-" :
                             benchmarkProfile == InterPassBenchmarkProfile::FastBelow50 ? "NR-v35-fast-low-" :
                             benchmarkProfile == InterPassBenchmarkProfile::FastQuality ? "NR-v35-fast-" :
                             benchmarkProfile == InterPassBenchmarkProfile::Boundary ? "NR-v20-boundary-" :
                             benchmarkProfile == InterPassBenchmarkProfile::Below50 ? "NR-v20-low-" : "NR-v20-auto-";
        name << prefix << std::put_time(&local, "%Y%m%d-%H%M%S");
        const auto csv = directory / (name.str() + ".csv");
        const auto info = directory / (name.str() + ".txt");
        const auto raw = directory / (name.str() + ".samples.csv");
        const auto pairs = directory / (name.str() + ".pairs.csv");
        std::ofstream out(csv);
        std::ofstream individual(raw);
        std::ofstream description(info);
        std::ofstream paired;
        if (benchmarkProfile == InterPassBenchmarkProfile::FastQuality ||
            benchmarkProfile == InterPassBenchmarkProfile::FastBelow50 ||
            benchmarkProfile == InterPassBenchmarkProfile::FastCachedLow)
            paired.open(pairs);
        if (!out || !description || !individual)
            throw std::runtime_error("Cannot create benchmark report.");
        out << "scale_percent,variant,mode,forced_path,selected_path,native_width,native_height,"
               "work_width,work_height,effective_passes,samples,mean_ms,median_ms,p95_ms,min_ms,max_ms,stddev_ms,"
               "delta_vs_off_median_ms,delta_vs_fused_reference_ms,delta_vs_expected_path_ms,"
               "historical_gain_vs_fused_ms,actual_gain_vs_fused_ms,path_matches_control,"
               "window_medians_ms,window_median_spread_ms,gain_vs_optimized_median_ms,gain_vs_fast_median_ms,delta_vs_fast_median_ms,"
               "interpass_expected_mask,interpass_mask,interpass_corrections\n";
        individual << "scale_percent,variant,window,sample_index,total_nr_gpu_ms,comparison,interpass_expected_mask,interpass_mask,interpass_corrections\n";
        out << std::fixed << std::setprecision(5);
        individual << std::fixed << std::setprecision(6);
        std::vector<BenchmarkVariant> combined;
        for (size_t window = 0; window < benchmarkVariants.size(); ++window)
        {
            const auto& v = benchmarkVariants[window];
            if (v.samples.empty()) continue;
            for (size_t i = 0; i < v.samples.size(); ++i)
                individual << v.percent << ',' << '"' << v.name << '"' << ',' << window << ',' << i << ',' << v.samples[i]
                           << ',' << v.comparison << ',' << v.expectedMask << ',' << v.mask << ',' << v.corrections << '\n';
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
        std::map<int, double> off, fused, expected, optimized, fast;
        std::map<int, std::string> expectedPath;
        for (const auto& v : combined)
        {
            if (v.mode == 0) off[v.percent] = medianOf(v);
            if (v.mode == 2) fused[v.percent] = medianOf(v);
            if (std::string(v.name) == "Inter-pass optimized") optimized[v.percent] = medianOf(v);
            if (std::string(v.name) == "Inter-pass fast") fast[v.percent] = medianOf(v);
            if (std::string(v.name) == "Expected path control" ||
                ((benchmarkProfile == InterPassBenchmarkProfile::FastQuality ||
                  benchmarkProfile == InterPassBenchmarkProfile::FastBelow50 ||
                  benchmarkProfile == InterPassBenchmarkProfile::FastCachedLow) &&
                 std::string(v.name) == "Inter-pass optimized"))
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
            out << "\"," << *range.second - *range.first << ',' << optimized.at(v.percent) - median << ','
                << (fast.count(v.percent) ? fast.at(v.percent) - median : std::numeric_limits<double>::quiet_NaN()) << ','
                << (fast.count(v.percent) ? median - fast.at(v.percent) : std::numeric_limits<double>::quiet_NaN()) << ','
                << v.expectedMask << ',' << v.mask << ',' << v.corrections << '\n';
        }
        if (benchmarkProfile == InterPassBenchmarkProfile::FastQuality ||
            benchmarkProfile == InterPassBenchmarkProfile::FastBelow50 ||
            benchmarkProfile == InterPassBenchmarkProfile::FastCachedLow)
        {
            if (!paired) throw std::runtime_error("Cannot create paired benchmark report.");
            paired << "scale_percent,comparison,control,variant,a1_window,b1_window,b2_window,a2_window,"
                      "a1_median_ms,b1_median_ms,b2_median_ms,a2_median_ms,symmetric_gain_ms,"
                      "first_gain_ms,last_gain_ms,control_window_spread_ms,variant_window_spread_ms,"
                      "symmetric_delta_ms,first_delta_ms,last_delta_ms\n"
                   << std::fixed << std::setprecision(6);
            for (size_t i = 0; i < benchmarkVariants.size(); ++i)
            {
                const auto& a1 = benchmarkVariants[i];
                if (a1.comparison == 0) continue;
                if (i + 3 >= benchmarkVariants.size()) throw std::runtime_error("Incomplete ABBA block.");
                const auto& b1 = benchmarkVariants[i+1];
                const auto& b2 = benchmarkVariants[i+2];
                const auto& a2 = benchmarkVariants[i+3];
                if (a1.percent != a2.percent || a1.comparison != a2.comparison ||
                    a1.comparison != b1.comparison || a1.comparison != b2.comparison ||
                    a1.mode != a2.mode || b1.mode != b2.mode ||
                    a1.forcedPath != a2.forcedPath || b1.forcedPath != b2.forcedPath)
                    throw std::runtime_error("Invalid ABBA block.");
                const double a = medianOf(a1), b = medianOf(b1), c = medianOf(b2), d = medianOf(a2);
                paired << a1.percent << ',' << a1.comparison << ",\"" << a1.name << "\",\"" << b1.name << "\","
                       << i << ',' << i+1 << ',' << i+2 << ',' << i+3 << ','
                       << a << ',' << b << ',' << c << ',' << d << ',' << (a+d-b-c)*0.5 << ','
                       << a-b << ',' << d-c << ',' << std::abs(a-d) << ',' << std::abs(b-c) << ','
                       << (b+c-a-d)*0.5 << ',' << b-a << ',' << c-d << '\n';
                i += 3;
            }
            paired.flush();
            if (!paired) throw std::runtime_error("Paired benchmark file write failed.");
        }
        description << "OptiScaler six-pass inter-pass GPU comparison\n"
                    << (benchmarkProfile == InterPassBenchmarkProfile::FastCachedLow ?
                        "Cached Low36 profile: exactly P50/P59/P65, Transfer=9, six passes.\n"
                        "Off, both references, Optimized, then original Fast / cached / cached / original Fast.\n"
                        "18 unique configurations / 24 independent windows. All taps and weights retained; FP coordinate rounding differs.\n" :
                        benchmarkProfile == InterPassBenchmarkProfile::FastBelow50 ?
                        "Low-scale Fast verification: P33/P40/P41/P42/P49 plus current below 50 if different.\n"
                        "25 unique configurations / 35 independent windows; 30 / 42 with extra current scale.\n"
                        "Approved Cross5 is integrated; exact-path controls remain in the separate Below50 profile.\n" :
                        benchmarkProfile == InterPassBenchmarkProfile::FastQuality ?
                        "Fast quality profile: exactly P50/P59/P65, Transfer=9, six passes.\n"
                        "15 unique configurations / 21 independent windows.\n" :
                        benchmarkProfile == InterPassBenchmarkProfile::Boundary ?
                        "Boundary profile: exactly P41/P42; current scale is not appended.\n" :
                        benchmarkProfile == InterPassBenchmarkProfile::Below50 ?
                        "Below-50 verification: P33/P40/P41/P42/P49 plus current lower scale if different.\n" :
                        "Regular profile: P50/P59/P65 plus current rounded percentage up to P65 when different.\n")
                    << "All benchmark profiles exclude percentages above P65, including the current scale.\n"
                    << "Off, Classic reference and Fused reference retained.\n"
                    << "All modes: correct before the next model only if its effective high/low gap exceeds 0.15 and grows. No fixed stride. Off/spatial/above P90 remain inactive.\n"
                    << "Skipped transitions use ordinary ClampProxy; no inter-pass scratch/low/correction dispatch.\n"
                    << "High/mid adjustment: 1+(scale_corrected_high-1)*clamp(Structure,0,1)*clamp(Intensity,0,1); Low unchanged.\n"
                    << (savedBenchmarkConfig.neutralInterPassLow
                        ? "Inter-pass low gain forced to 1; final low averages all effective model gains.\n"
                        : "Inter-pass low averages each unconsumed model block; final low averages models after the last successful correction.\n")
                    << "All pass styles/Structure/Intensity settings are recorded; there is no repetition of the last model's gain.\n"
                    << "Mask bit N means correction after zero-based model N; expected/successful masks must match. No correction after the final model. interpass_corrections counts successes/frame.\n"
                    << "Prediction uses scale-corrected per-model gains and configured Structure/Intensity/compounding. A gap that shrinks or stays equal never triggers correction, even above 0.15.\n"
                    << (benchmarkProfile == InterPassBenchmarkProfile::FastCachedLow ?
                        "Each scale: Off, both references, Optimized, then Fast/cached/cached/Fast with separate warmup.\n"
                        "Only low filtering uses shared samples; without low shaping both variants use original Cross5.\n" :
                        benchmarkProfile == InterPassBenchmarkProfile::FastQuality ||
                        benchmarkProfile == InterPassBenchmarkProfile::FastBelow50 ?
                        "Each scale: Off, both references, Optimized/Fast/Fast/Optimized with separate warmup.\n"
                        "Fast includes Cross5 at every reduced scale and Low36 when shaping needs a low field.\n"
                        "Raw comparison 1 identifies the only Optimized/Fast ABBA; there is no without-Cross5 pair.\n"
                        "Fast rows have no historical gain field. Exact Optimized policy unchanged.\n"
                        "Negative delta means faster; positive gain means time saved. Use paired CSV for local controls.\n" :
                        "All scales: expected path A / automatic B / B / A plus alternate retained path.\n")
                    << "Manual window inspection=" << benchmarkInspectEachWindow << "; held-frame samples excluded.\n"
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
                    << (benchmarkProfile == InterPassBenchmarkProfile::FastCachedLow ?
                        "Cached Low36 compares with the original Fast in ABBA, not the Optimized pool. Inspect motion and edges.\n" :
                        benchmarkProfile == InterPassBenchmarkProfile::FastQuality ||
                        benchmarkProfile == InterPassBenchmarkProfile::FastBelow50 ?
                        "Fast compares with the local automatic exact Optimized control in ABBA; there is no forced-path pair.\n" :
                        "Automatic should match the independent forced control in ABBA; compare the alternate and references too.\n")
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

void StartInterPassBenchmark(unsigned warmupSamples, unsigned measuredSamples, InterPassBenchmarkProfile profile,
                            bool inspectEachWindow, const char* scene)
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
    benchmarkInspectEachWindow = inspectEachWindow;
    BuildBenchmarkVariants();
    benchmarkAdapterInfo.clear();
    benchmarkNativeW = benchmarkNativeH = benchmarkWorkW = benchmarkWorkH = benchmarkEffectivePasses = 0;
    benchmarkExpectedMask = benchmarkMask = benchmarkCorrections = 0;
    benchmarkSetup = "scene=" + std::string(scene ? scene : "unspecified") +
                     ", guide strength=" + std::to_string(savedBenchmarkConfig.strength) +
                     ", source scale=" + std::to_string(savedBenchmarkConfig.scale) +
                     ", transfer=" + std::to_string(savedBenchmarkConfig.transfer) +
                     ", range sigma=" + std::to_string(c.DlssNrGuidedResidualRangeSigma.value_or_default()) +
                     ", spatial sigma=" + std::to_string(c.DlssNrGuidedResidualSpatialSigma.value_or_default()) +
                     ", shaping=" + std::to_string(c.DlssNrGuidedResidualShaping.value_or_default()) +
                     ", high/mid compounding=" + std::to_string(c.DlssNrGuidedResidualCompoundPasses.value_or_default()) +
                     ", interpass low neutral=" + std::to_string(savedBenchmarkConfig.neutralInterPassLow) +
                     ", standard high=" + std::to_string(c.DlssNrGuidedResidualStandardHighGain.value_or_default()) +
                     ", standard low=" + std::to_string(c.DlssNrGuidedResidualStandardLowGain.value_or_default()) +
                     ", natural high=" + std::to_string(c.DlssNrGuidedResidualHighGain.value_or_default()) +
                     ", natural low=" + std::to_string(c.DlssNrGuidedResidualLowGain.value_or_default()) +
                     ", cinematic high=" + std::to_string(c.DlssNrGuidedResidualCinematicHighGain.value_or_default()) +
                     ", cinematic low=" + std::to_string(c.DlssNrGuidedResidualCinematicLowGain.value_or_default()) +
                     ", shadow gate=" + std::to_string(c.DlssNrGuidedResidualShadowGate.value_or_default());
    for (unsigned pass = 0; pass < 6u; ++pass)
    {
        const auto settings = Profiles::BasePassSettings(c, pass);
        benchmarkSetup += ", pass" + std::to_string(pass + 1u) + "_preset=" + std::to_string(settings.preset) +
                          "_style=" + std::to_string(settings.style) +
                          "_configured_structure=" + std::to_string(settings.localStructure) +
                          "_configured_intensity=" + std::to_string(settings.intensity);
    }
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
namespace
{
void AdvanceBenchmarkWindow()
{
    benchmarkProgress.awaitingVisualInspection = false;
    if (++benchmarkIndex == benchmarkVariants.size())
    {
        FinishBenchmark(false);
        return;
    }
    benchmarkWarm = 0;
    ApplyBenchmarkVariant(*Config::Instance(), benchmarkVariants[benchmarkIndex]);
    benchmarkProgress.message = "Running; keep the scene static. You may close the menu.";
    UpdateBenchmarkProgress();
}
}
void AdvanceInterPassBenchmark()
{
    std::lock_guard lock(benchmarkMutex);
    if (benchmarkActive.load() && benchmarkProgress.awaitingVisualInspection)
        AdvanceBenchmarkWindow();
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
    if (benchmarkProgress.awaitingVisualInspection)
        return; // Keep rendering this configuration; don't contaminate its completed timing window.
    // Do not accidentally benchmark a partially prepared six-feature chain.
    if (benchmarkEffectivePasses != 6u || benchmarkNativeW == 0u || benchmarkWorkW == 0u)
    {
        benchmarkProgress.message = "Waiting for all 6 DLSS NR features and actual texture dimensions.";
        return;
    }
    auto& variant = benchmarkVariants[benchmarkIndex];
    const auto expectedSchedule = Profiles::BuildInterPassSchedule(*Config::Instance(), unsigned(variant.mode),
        float(variant.percent) / 100.0f, benchmarkEffectivePasses, false);
    if (benchmarkExpectedMask != expectedSchedule.expectedMask || benchmarkMask != expectedSchedule.expectedMask ||
        benchmarkCorrections != InterPass::CorrectionCount(expectedSchedule.expectedMask))
    {
        benchmarkProgress.message = "Waiting for the scheduled inter-pass corrections to complete successfully.";
        return;
    }
    const auto& first = benchmarkVariants.front();
    if ((first.nativeW && (first.nativeW != benchmarkNativeW || first.nativeH != benchmarkNativeH)) ||
        (variant.nativeW && (variant.workW != benchmarkWorkW || variant.workH != benchmarkWorkH)))
    {
        FinishBenchmark(true);
        benchmarkProgress.message = "Cancelled: texture dimensions changed during a measurement; settings restored.";
        return;
    }
    if (!variant.samples.empty() && variant.expectedMask != benchmarkExpectedMask)
    {
        FinishBenchmark(true);
        benchmarkProgress.message = "Cancelled: inter-pass schedule changed within a measurement window; settings restored.";
        return;
    }
    variant.nativeW = benchmarkNativeW;
    variant.nativeH = benchmarkNativeH;
    variant.workW = benchmarkWorkW;
    variant.workH = benchmarkWorkH;
    variant.effectivePasses = benchmarkEffectivePasses;
    variant.path = benchmarkPath;
    variant.expectedMask = benchmarkExpectedMask;
    variant.mask = benchmarkMask;
    variant.corrections = benchmarkCorrections;
    if (benchmarkWarm < benchmarkWarmup)
        ++benchmarkWarm;
    else
        variant.samples.push_back(rawGpuMs);
    if (benchmarkVariants[benchmarkIndex].samples.size() == benchmarkSamples)
    {
        if (benchmarkInspectEachWindow)
        {
            benchmarkProgress.awaitingVisualInspection = true;
            benchmarkProgress.message = "Window measured. Inspect the image, then continue; held frames are not measured.";
        }
        else
        {
            AdvanceBenchmarkWindow();
            return;
        }
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
void BenchmarkReportInterPassSchedule(unsigned expectedMask, unsigned successfulMask, unsigned corrections)
{
    if (!benchmarkActive.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(benchmarkMutex);
    benchmarkExpectedMask = expectedMask;
    benchmarkMask = successfulMask;
    benchmarkCorrections = corrections;
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
