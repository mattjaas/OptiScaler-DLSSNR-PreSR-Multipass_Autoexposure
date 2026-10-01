#include "pch.h"
#include "DlssNr_Dx12_State.h"

auto DlssNr_Dx12::State::ReportSkipOnce(const char* reason) -> void
{

    if (seen.insert(reason).second)
        LOG_INFO("DLSS-NR did not run: {}", reason);
}

auto DlssNr_Dx12::State::DeferredDlssStatus() -> std::string
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return deferredSr.status;
}

auto DlssNr_Dx12::State::RetryAfterFailure() -> void
{
    ReleaseEnlarger();
    enlargementStatus.clear();
    nr.failed = false;
    nr.reason = "";
    nr.reset = true;
    nr.spatialFallback = false;
    nr.spatialFallbackReason = "";
    nr.spatialActive = false;
}

void DlssNr_Dx12::State::ReleaseStyleAnalysisCapture()
{
    for (auto& model : styleAnalysisCapture.models)
        model.Release();
    for (auto& model : styleAnalysisCapture.nativeModels)
        model.Release();
    for (auto*& output : styleAnalysisCapture.outputs)
        ParkNrResource(output);
    for (auto*& output : styleAnalysisCapture.nativeOutputs)
        ParkNrResource(output);
    styleAnalysisCapture.readback.Reset();
    styleAnalysisCapture.active = false;
    styleAnalysisCapture.modelsPrepared = false;
    styleAnalysisCapture.copiesRecorded = false;
    styleAnalysisCapture.status.clear();
}

auto DlssNr_Dx12::State::ConsumeControls() -> void
{
    const auto& cfg = *Config::Instance();
    const uint32_t transfer = cfg.DlssNrTransfer.value_or_default();
    // Transfers 8/9/10 also own a persistent private DLSS feature/history, even though they do not
    // use the ordinary P50->P100 enlargement path. Do not destroy that 1:1 DLAA history every frame.
    const bool ownsPrivateTemporalHistory =
        DlssNrUsesDlssEnlargement(transfer) || transfer == 8u || transfer == 9u || transfer == 10u;
    if (!cfg.DlssNrEnabled.value_or_default() || !ownsPrivateTemporalHistory ||
        cfg.DlssNrWorkingScale.value_or_default() >= 1.0f)
    {
        // Native-scale NR does not use the private DLSS/DLAA feature. Release it instead of
        // keeping its NGX history resident in VRAM while it is idle.
        ReleaseEnlarger();
        enlargementStatus.clear();
    }
    const auto requested = DlssNr::ReadControlRequests();
    if (requested.retryGeneration != controls.retryGeneration)
    {
        for (auto& model : nr.models)
            model.RetryAfterFailure();
        std::fill(std::begin(nr.passCreateFailed), std::end(nr.passCreateFailed), false);
        modelRunning = false;
        RetryAfterFailure();
    }
    if (requested.captureGeneration != controls.captureGeneration)
        captureFrames.request(requested.captureFrames);
    if (requested.styleAnalysisCaptureGeneration != controls.styleAnalysisCaptureGeneration)
    {
        if (styleAnalysisCapture.active)
        {
            LOG_WARN("NR style analysis capture request ignored: a capture is already in progress.");
        }
        else
        {
            styleAnalysisCapture.requestGeneration = requested.styleAnalysisCaptureGeneration;
            styleAnalysisCapture.active = true;
            styleAnalysisCapture.status = "Armed";
            LOG_INFO("NR style analysis capture armed: Standard/Natural/Cinematic P50 and P100 will use one identical game frame.");
        }
    }
    controls = requested;
}

auto DlssNr_Dx12::State::Publish() -> void
{
    std::string spatialStatus = "Off";
    if (nr.spatialLayout.requested)
    {
        if (!nr.spatialLayout.active)
            spatialStatus = "Unavailable: " + nr.spatialLayout.reason;
        else if (nr.spatialFallback)
            spatialStatus = std::string("Fallback to ordinary NR: ") + nr.spatialFallbackReason;
        else
            spatialStatus = "DX12 peripheral compression: " + std::to_string(nr.spatialLayout.ordinaryW) + "x" +
                            std::to_string(nr.spatialLayout.ordinaryH) + " -> " +
                            std::to_string(nr.spatialLayout.modelW) + "x" + std::to_string(nr.spatialLayout.modelH);
    }
    DlssNr::StatusSnapshot snapshot;
    snapshot.running = !nr.failed && modelRunning && enlargementStatus.empty();
    snapshot.failureReason = nr.failed ? nr.reason : enlargementStatus;
    snapshot.gpuTime = lastGpuTime;
    snapshot.frames = frames;
    snapshot.spatialStatus = spatialStatus;
    snapshot.spatialActive = nr.spatialActive;
    snapshot.temporalCarrierTelemetryValid = temporalCarrierTelemetryValid;
    snapshot.temporalCarrierAppliedK = temporalCarrierAppliedK;
    snapshot.temporalCarrierSafeK = temporalCarrierSafeK;
    snapshot.temporalCarrierPositiveLimit = temporalCarrierPositiveLimit;
    snapshot.temporalCarrierNegativeLimit = temporalCarrierNegativeLimit;
    DlssNr::PublishStatus(&shader, DlssNr::Backend::Dx12, snapshot);
}

void DlssNr_Dx12::State::EndGpuTiming(ID3D12GraphicsCommandList* cmdList)
{
    gpuTime->End(cmdList);

    const auto rawGpu = gpuTime->ReadGpuTime();
    const auto rawNgx = ngxTime->ReadGpuTime();
    const uint32_t averageWindowMs =
        std::min(Config::Instance()->DlssNrGpuTimeAverageWindowMs.value_or_default(), 9999u);

    if (rawGpu)
        lastGpuTime = gpuTime->ReadGpuTime(averageWindowMs);
    if (rawNgx)
        lastNgxTime = rawNgx;

    // Keep diagnostics/vitals based on raw per-frame samples. UI smoothing must not turn the
    // diagnostic rolling window into an average of averages.
    if (rawGpu && rawNgx)
        vitals.Push(*rawGpu, *rawNgx);
    if (lastGpuTime && lastNgxTime && frames - lastSplitLog > 600)
    {
        lastSplitLog = frames;
        const auto window = vitals.Read();
        LOG_INFO("DLSS-NR GPU window: {} samples, total mean {:.2f} / p99 {:.2f} ms, model mean {:.2f} / p99 {:.2f} ms",
                 window.samples, window.totalMean, window.totalP99, window.modelMean, window.modelP99);
        const double total = *lastGpuTime, ngx = *lastNgxTime;
        LOG_INFO("DLSS-NR elapsed: {:.2f} ms total, {:.2f} ms model, {:.2f} ms surrounding work ({:.0f}%; "
                 "intervals may include other GPU work)",
                 total, ngx, total - ngx, total > 0.0 ? 100.0 * (total - ngx) / total : 0.0);
    }
}
