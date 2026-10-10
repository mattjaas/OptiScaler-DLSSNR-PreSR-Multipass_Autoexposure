#pragma once
// Single-click DX12 six-pass NR A/B: driven by completed GPU timestamp samples,
// not ImGui frames. Engine-side implementation lives in DlssNr_Status.cpp.
#include <cstdint>
#include <string>
struct ID3D12Device;
namespace DlssNr
{
struct BenchmarkProgress
{
    bool active = false;
    unsigned current = 0;
    unsigned total = 0;
    unsigned warmed = 0;
    unsigned collected = 0;
    unsigned warmupTarget = 0;
    unsigned sampleTarget = 0;
    std::string variant;
    std::string message;
};
void StartInterPassBenchmark(unsigned warmupSamples, unsigned measuredSamples, bool below50 = false);
void CancelInterPassBenchmark();
int BenchmarkInterPassOverride(); // -1 outside benchmark, no user-facing implementation switches.
void BenchmarkReportInterPassPath(const char* path);
BenchmarkProgress ReadInterPassBenchmark();
void BenchmarkGpuSample(double rawGpuMs, bool modelRunning);
void BenchmarkReportGeometry(unsigned nativeW, unsigned nativeH, unsigned workW, unsigned workH,
                             unsigned effectivePasses);
void BenchmarkReportDevice(ID3D12Device* device);
} // namespace DlssNr
