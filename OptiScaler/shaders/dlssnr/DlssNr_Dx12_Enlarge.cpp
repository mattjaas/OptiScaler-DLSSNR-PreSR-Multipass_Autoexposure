#include "pch.h"
#include "DlssNr_Dx12_State.h"

namespace
{
Scaler DirectSpatialScaler(uint32_t index)
{
    switch (index)
    {
    case 1: return Scaler::Bicubic;
    case 2: return Scaler::CatmullRom;
    case 3: return Scaler::Lanczos2;
    case 4: return Scaler::Lanczos3;
    case 5: return Scaler::Kaiser2;
    case 6: return Scaler::Kaiser3;
    case 8: return Scaler::Magic;
    case 9: return Scaler::FSR1;
    default: return Scaler::Count;
    }
}

bool DirectFusedResolveUpscaler(uint32_t selector)
{
    return selector == 0 || selector == 7 || selector == 8 || selector == 9;
}

bool DirectAnswerCanFeedUpscaler(ID3D12Resource* answer)
{
    return answer && answer->GetDesc().Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
}
} // namespace

void DlssNr_Dx12::State::ReleaseEnlarger()
{
    if (!enlarger)
        return;
    retiredEnlargers.push_back(std::move(enlarger));
    CollectEnlargers();
}

void DlssNr_Dx12::State::CollectEnlargers()
{
    if (collectingEnlargers)
        return;
    collectingEnlargers = true;
    // Release NGX only after container mutation: its destruction can re-enter queue hooks.
    std::vector<std::unique_ptr<Enlarger>> completed;
    for (auto& old : retiredEnlargers)
        if (old->lifetime.Idle())
            completed.push_back(std::move(old));
    std::erase_if(retiredEnlargers, [](const auto& old) { return !old; });
    completed.clear();
    collectingEnlargers = false;
}

ID3D12Resource* DlssNr_Dx12::State::EnlargeMatchedResidual(ID3D12GraphicsCommandList* cmd, ID3D12Device* device,
                                                           ID3D12Resource* proxy, ID3D12Resource* referenceProxy,
                                                           ID3D12Resource* answer, ID3D12Resource* depth,
                                                           ID3D12Resource* motion,
                                                           const DlssNrFrameInfo& frame, const DlssNrConstants& resolve,
                                                           uint32_t transfer, bool reset,
                                                           ID3D12CommandQueue* timingQueue,
                                                           bool externalDetailReference)
{
    auto say = [&](const std::string& message) -> ID3D12Resource*
    {
        if (enlargementStatus != message)
            LOG_INFO("NR enlargement: {}", message);
        enlargementStatus = message;
        return nullptr;
    };

    if (frame.BeforeUpscale)
    {
        ReleaseEnlarger();
        return say("Direct/private enlargement requires NR after the game upscaler.");
    }

    // The swapchain queue can be Streamline's presentation queue, not the NR producer.
    auto* queue = (frame.IndependentCommands || frame.FinishedPicture) ? timingQueue : nullptr;
    ID3D12CommandQueue* realQueue = nullptr;
    if (queue && Util::CheckForRealObject(__FUNCTION__, queue, (IUnknown**) &realQueue))
        queue = realQueue;
    if (cmd->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT ||
        (queue && queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT))
        return say("Waiting for the NR direct command queue.");

    Microsoft::WRL::ComPtr<ID3D12Device> queueDevice;
    if (queue && (FAILED(queue->GetDevice(IID_PPV_ARGS(&queueDevice))) || queueDevice.Get() != device))
        return say("NR enlargement queue/device mismatch.");

    const bool structural = transfer == 4;
    const bool direct = transfer == 5;
    const bool upscaledResidual = transfer == 6;
    const bool temporalDlaa = transfer == 8 || transfer == 9 || transfer == 10;
    const bool residualDlaa = transfer == 9 || transfer == 10;
    const bool imageAnchoredDlaa = transfer == 10;
    const auto& cfg = *Config::Instance();
    if (!referenceProxy)
        referenceProxy = proxy;
    const uint32_t directDetailMode =
        direct ? std::min(cfg.DlssNrDirectDetailRecovery.value_or_default(), 2u) : 0u;
    const bool directFinalReferenceExperiment =
        direct && cfg.DlssNrExperimentStructureTransfer.value_or_default() != 0;
    const uint32_t outputUpscaler =
        temporalDlaa ? 10u
        : direct ? std::min(cfg.DlssNrDirectOutputUpscaler.value_or_default(), 10u)
                 : upscaledResidual ? std::min(cfg.DlssNrUpscaledResidualUpscaler.value_or_default(), 10u)
                                    : 10u;
    const uint32_t detailReferenceUpscaler =
        upscaledResidual ? std::min(cfg.DlssNrUpscaledResidualReferenceUpscaler.value_or_default(), 10u)
                         : direct ? std::min(cfg.DlssNrDirectDetailReferenceUpscaler.value_or_default(), 10u) : 0u;
    const uint32_t residualEncoding =
        transfer == 9 ? std::min(cfg.DlssNrTemporalResidualEncoding.value_or_default(), 1u) : 0u;
    const uint32_t carrierType = imageAnchoredDlaa ? 2u : residualEncoding; // 0 nonlinear, 1 linear, 2 anchored
    const float configuredAnchorStrength = cfg.DlssNrTemporalAnchoredBaseStrength.value_or_default();
    const float anchorStrength =
        std::clamp(std::isfinite(configuredAnchorStrength) ? configuredAnchorStrength : 0.5f, 0.0f, 1.0f);
    const bool pairedAnchorBaseline =
        imageAnchoredDlaa && cfg.DlssNrTemporalAnchoredPairedBaseline.value_or_default();
    const bool extendedCarrierRange =
        residualDlaa && cfg.DlssNrTemporalCarrierExtendedRange.value_or_default() &&
        (imageAnchoredDlaa || residualEncoding == 1u);
    const bool temporalDlaaIsHdr = temporalDlaa && cfg.DlssNrTemporalDlaaIsHdr.value_or_default();
    const bool temporalAutoExposure =
        transfer == 8u ? cfg.DlssNrTemporalDlaaNrAutoExposure.value_or_default()
        : transfer == 9u ? cfg.DlssNrTemporalDlaaResidualAutoExposure.value_or_default()
        : transfer == 10u ? cfg.DlssNrTemporalDlaaAnchoredAutoExposure.value_or_default()
                          : false;
    const bool mainDlssActive =
        transfer == 2u || transfer == 4u || temporalDlaa ||
        ((direct || upscaledResidual) && outputUpscaler == 10u);
    const bool mainDlssAutoExposure =
        mainDlssActive && (temporalDlaa ? temporalAutoExposure
                                       : cfg.DlssNrScalingDlssAutoExposure.value_or_default());
    const bool detailDlssAutoExposure =
        (direct || upscaledResidual) && detailReferenceUpscaler == 10u &&
        cfg.DlssNrDetailReferenceDlssAutoExposure.value_or_default();
    const uint32_t baseCarrierMode =
        imageAnchoredDlaa ? 7u
        : transfer == 9 ? (residualEncoding == 0u ? 5u : 6u)
        : temporalDlaa ? 4u : upscaledResidual ? 3u : direct ? 2u : structural ? 1u : 0u;
    // Extra bits make experimental input-domain changes recreate/reset the private temporal feature.
    const uint32_t carrierMode = baseCarrierMode |
                                 (pairedAnchorBaseline ? 0x100u : 0u) |
                                 (extendedCarrierRange ? 0x200u : 0u) |
                                 (temporalDlaaIsHdr ? 0x400u : 0u);
    const bool directAnswerSource = direct && DirectAnswerCanFeedUpscaler(answer);
    const bool p100GuidedExperiment =
        direct && (cfg.DlssNrExperimentP100EdgeLimiter.value_or_default() != 0 ||
                   cfg.DlssNrExperimentStructureTransfer.value_or_default() != 0);
    // P100-guided experiments sample several neighbouring NR100 values. Re-evaluating MAGIC's
    // multi-tap kernel for every neighbour is slower than materializing MAGIC once, while the cheap
    // bilinear/Area/current-FSR1 paths still benefit from the zero-intermediate fused resolve.
    const bool materializeExpensiveFused =
        p100GuidedExperiment && (outputUpscaler == 8u || resolve.CompareMode == 1u);
    const bool fusedDirectOutput =
        directAnswerSource && DirectFusedResolveUpscaler(outputUpscaler) && !materializeExpensiveFused;
    const int dlssPreset =
        transfer == 8u ? cfg.DlssNrTemporalDlaaNrPreset.value_or_default()
        : transfer == 9u ? cfg.DlssNrTemporalDlaaResidualPreset.value_or_default()
        : transfer == 10u ? cfg.DlssNrTemporalDlaaAnchoredPreset.value_or_default()
                          : cfg.DlssNrScalingDlssPreset.value_or_default();

    const auto desc = proxy->GetDesc();
    const unsigned w = unsigned(desc.Width), h = desc.Height;
    const unsigned desiredOutW = temporalDlaa ? w : resolve.Width;
    const unsigned desiredOutH = temporalDlaa ? h : resolve.Height;
    if (enlarger && (enlarger->w != w || enlarger->h != h || enlarger->outW != desiredOutW ||
                     enlarger->outH != desiredOutH || (queue && enlarger->queue.Get() != queue) ||
                     enlarger->depthInverted != frame.DepthInverted || enlarger->carrierMode != carrierMode ||
                     enlarger->dlssPreset != dlssPreset || enlarger->outputUpscaler != outputUpscaler ||
                     enlarger->detailReferenceUpscaler != detailReferenceUpscaler ||
                     enlarger->dlssAutoExposure != mainDlssAutoExposure ||
                     enlarger->detailDlssAutoExposure != detailDlssAutoExposure))
        ReleaseEnlarger();

    if (enlarger && imageAnchoredDlaa && std::abs(enlarger->anchorStrength - anchorStrength) > 1.0e-6f)
    {
        // Changing the anchor changes the temporal input distribution. Keep allocations/features but reset both histories.
        enlarger->anchorStrength = anchorStrength;
        enlarger->reset = true;
        enlarger->baselineReset = true;
    }

    CollectEnlargers();
    if (!enlarger)
    {
        if (retiredEnlargers.size() >= 4)
            return say("Waiting for retired enlargement work.");

        enlarger = std::make_unique<Enlarger>();
        auto& g = *enlarger;
        g.w = w;
        g.h = h;
        g.outW = desiredOutW;
        g.outH = desiredOutH;
        g.depthInverted = frame.DepthInverted;
        g.queue = queue;
        g.carrierMode = carrierMode;
        g.pairedBaseline = pairedAnchorBaseline;
        g.anchorStrength = anchorStrength;
        g.dlssPreset = dlssPreset;
        g.outputUpscaler = outputUpscaler;
        g.detailReferenceUpscaler = detailReferenceUpscaler;
        g.dlssAutoExposure = mainDlssAutoExposure;
        g.detailDlssAutoExposure = detailDlssAutoExposure;

        g.input.Attach(CreateScratch(device, DXGI_FORMAT_R16G16B16A16_FLOAT, w, h));
        g.output.Attach(CreateScratch(device, DXGI_FORMAT_R16G16B16A16_FLOAT, g.outW, g.outH));
        g.depth.Attach(CreateScratch(device, DXGI_FORMAT_R32_FLOAT, w, h));
        g.motion.Attach(CreateScratch(device, DXGI_FORMAT_R32G32_FLOAT, w, h));
        g.exposure.Attach(CreateScratch(device, DXGI_FORMAT_R32_FLOAT, 1, 1));
        if (residualDlaa)
        {
            temporalCarrierTelemetryValid = false;
            const unsigned tileW = std::max(1u, (w + 31u) / 32u);
            const unsigned tileH = std::max(1u, (h + 31u) / 32u);
            g.carrierReduceA.Attach(CreateScratch(device, DXGI_FORMAT_R32G32B32A32_FLOAT, tileW, tileH));
            g.carrierReduceB.Attach(CreateScratch(device, DXGI_FORMAT_R32G32B32A32_FLOAT, tileW, tileH));
            g.carrierDiagA.Attach(CreateScratch(device, DXGI_FORMAT_R32G32B32A32_FLOAT, tileW, tileH));
            g.carrierDiagB.Attach(CreateScratch(device, DXGI_FORMAT_R32G32B32A32_FLOAT, tileW, tileH));
            g.carrierApplied.Attach(CreateScratch(device, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 1));
            if (g.carrierApplied && g.carrierDiagA)
            {
                for (auto& slot : g.carrierReadback)
                {
                    slot.image.Allocate(device, g.carrierApplied->GetDesc(), 4096);
                    slot.diagnostics.Allocate(device, g.carrierDiagA->GetDesc(), 1024 * 1024);
                }
            }
        }
        if (pairedAnchorBaseline)
        {
            g.baselineInput.Attach(CreateScratch(device, DXGI_FORMAT_R16G16B16A16_FLOAT, w, h));
            g.baselineOutput.Attach(CreateScratch(device, DXGI_FORMAT_R16G16B16A16_FLOAT, w, h));
        }
        g.failed = true;
        if (!g.input || !g.output || !g.depth || !g.motion || !g.exposure ||
            (residualDlaa && (!g.carrierReduceA || !g.carrierReduceB || !g.carrierDiagA ||
                              !g.carrierDiagB || !g.carrierApplied)) ||
            (pairedAnchorBaseline && (!g.baselineInput || !g.baselineOutput)))
            return say("enlargement resource allocation failed; use Retry.");

        lifetime.Record(cmd);
        g.lifetime.Record(cmd);

        DlssNrConstants unit {};
        unit.Mode = DlssNrMode_UnitExposure;
        unit.Width = unit.Height = 1;
        if (!shader.DispatchPass(cmd, unit, proxy, nullptr, nullptr, nullptr, nullptr, g.exposure.Get(), nullptr))
            return say("enlargement exposure initialization failed; use Retry.");
        Barrier(cmd, g.exposure.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        // Keep the original private-DLSS feature even when a spatial A/B output is selected. This
        // preserves the existing creation/submission handshake and makes switching selectors deterministic.
        g.dlss = std::make_unique<DlssNr::PrivateUpscalerDx12>(DlssNr::PrivateUpscaler::DLSS);
        DlssNr::PrivateUpscalerCreateDx12 info;
        info.width = w;
        info.height = h;
        info.outputWidth = g.outW;
        info.outputHeight = g.outH;
        info.quality = temporalDlaa ? (int) NVSDK_NGX_PerfQuality_Value_DLAA : 2;
        info.dlssPreset = dlssPreset;
        info.depthInverted = frame.DepthInverted;
        info.rayReconstruction = false;
        info.isHdr = temporalDlaaIsHdr;
        info.autoExposure = mainDlssAutoExposure;
        if (!g.dlss->Init(device, cmd, info))
            return say(std::string(temporalDlaa ? "Private DLSS DLAA: " : "Private DLSS SR: ") + g.dlss->Error());

        if (pairedAnchorBaseline)
        {
            g.baselineDlss = std::make_unique<DlssNr::PrivateUpscalerDx12>(DlssNr::PrivateUpscaler::DLSS);
            if (!g.baselineDlss->Init(device, cmd, info))
                return say("Private paired anchor DLAA baseline: " + g.baselineDlss->Error());
        }

        // P50->P100 via DLSS owns a completely separate temporal feature/history from NR50->NR100.
        // Upscaled NR residual always needs that second history when DLSS is selected because the clean
        // P50 reconstruction and the NR50 answer must never share temporal state.
        if ((direct || upscaledResidual) && detailReferenceUpscaler == 10)
        {
            g.detailDlss = std::make_unique<DlssNr::PrivateUpscalerDx12>(DlssNr::PrivateUpscaler::DLSS);
            auto detailInfo = info;
            detailInfo.isHdr = false;
            detailInfo.autoExposure = detailDlssAutoExposure;
            if (!g.detailDlss->Init(device, cmd, detailInfo))
                return say("Private P50-reference DLSS SR: " + g.detailDlss->Error());
        }

        LOG_INFO("NR enlargement created at {}x{} -> {}x{}, output upscaler {}, detail-reference upscaler {}, "
                 "DLSS preset {}, main AE {}, reference AE {}",
                 w, h, g.outW, g.outH, outputUpscaler, detailReferenceUpscaler, dlssPreset,
                 mainDlssAutoExposure ? "on" : "off", detailDlssAutoExposure ? "on" : "off");

        ID3D12GraphicsCommandList* real = nullptr;
        g.creation = Util::CheckForRealObject(__FUNCTION__, cmd, (IUnknown**) &real) ? real : cmd;
        g.failed = false;
        return say("Waiting for enlargement initialization submission.");
    }

    auto& g = *enlarger;
    if (g.failed)
        return nullptr;
    if (!g.submitted)
        return say("Waiting for enlargement initialization submission.");

    // Non-blocking telemetry: consume a completed 1x1 carrier-gain readback from an older frame.
    for (auto& slot : g.carrierReadback)
    {
        if (!slot.pending || !slot.ready || !slot.ready())
            continue;
        if (slot.image.readback)
        {
            void* mapped = nullptr;
            const SIZE_T offset = static_cast<SIZE_T>(slot.image.layout.Offset);
            D3D12_RANGE readRange { offset, offset + sizeof(float) * 4u };
            D3D12_RANGE writtenRange {};
            if (SUCCEEDED(slot.image.readback->Map(0, &readRange, &mapped)) && mapped)
            {
                const float* v = reinterpret_cast<const float*>(
                    static_cast<const uint8_t*>(mapped) + offset);
                temporalCarrierAppliedK = std::isfinite(v[0]) ? v[0] : 1.0f;
                temporalCarrierSafeK = std::isfinite(v[1]) ? v[1] : 1.0f;
                temporalCarrierPositiveLimit = std::isfinite(v[2]) && v[2] < 1.0e10f ? v[2] : -1.0f;
                temporalCarrierNegativeLimit = std::isfinite(v[3]) && v[3] < 1.0e10f ? v[3] : -1.0f;
                temporalCarrierTelemetryValid = true;
                slot.image.readback->Unmap(0, &writtenRange);

                if (slot.diagnostics.readback)
                {
                    void* diagMapped = nullptr;
                    const SIZE_T diagOffset = static_cast<SIZE_T>(slot.diagnostics.layout.Offset);
                    D3D12_RANGE diagRange { diagOffset, diagOffset + sizeof(float) * 4u };
                    if (SUCCEEDED(slot.diagnostics.readback->Map(0, &diagRange, &diagMapped)) && diagMapped)
                    {
                        const float* d = reinterpret_cast<const float*>(
                            static_cast<const uint8_t*>(diagMapped) + diagOffset);
                        temporalCarrierWhiteLimitedPixels = std::isfinite(d[0]) ? d[0] : 0.0f;
                        temporalCarrierBlackLimitedPixels = std::isfinite(d[1]) ? d[1] : 0.0f;
                        temporalCarrierProxyBelowZeroPixels = std::isfinite(d[2]) ? d[2] : 0.0f;
                        temporalCarrierProxyAboveOnePixels = std::isfinite(d[3]) ? d[3] : 0.0f;
                        slot.diagnostics.readback->Unmap(0, &writtenRange);
                    }
                }
            }
        }
        slot.pending = false;
        slot.ready = {};
    }

    lifetime.Record(cmd);
    g.lifetime.Record(cmd);

    bool detailReady = direct && directDetailMode != 0;
    bool detailMaskRequired = direct && directDetailMode == 2;
    bool referenceRequired = detailReady || upscaledResidual || directFinalReferenceExperiment;
    const bool useExternalReference = externalDetailReference && referenceRequired;
    if (referenceRequired &&
        ((detailMaskRequired && !g.detailInfo) || (!useExternalReference && !g.detailReference)))
    {
        if (detailMaskRequired && !g.detailInfo)
            g.detailInfo.Attach(CreateScratch(device, DXGI_FORMAT_R16G16B16A16_FLOAT, w, h));
        if (!useExternalReference && !g.detailReference)
            g.detailReference.Attach(CreateScratch(device, DXGI_FORMAT_R16G16B16A16_FLOAT, g.outW, g.outH));
        if ((detailMaskRequired && !g.detailInfo) || (!useExternalReference && !g.detailReference))
        {
            if (upscaledResidual)
                return say("Upscaled NR residual reference allocation failed; use Retry.");

            static bool warnedDetailAlloc = false;
            if (!warnedDetailAlloc)
            {
                warnedDetailAlloc = true;
                LOG_WARN("NR Direct detail recovery: gate or P50-reference allocation failed; "
                         "falling back to Direct NR without detail recovery.");
            }
            detailReady = false;
            detailMaskRequired = false;
            referenceRequired = false;
        }
    }

    if (detailMaskRequired && g.detailReadable)
    {
        Barrier(cmd, g.detailInfo.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        g.detailReadable = false;
    }

    // NR50 is already a readable FP16 model output. Feed it directly to the output path instead of
    // round-tripping through g.input; preserve the old FP16 carrier only as a format fallback.
    const bool stageDirectAnswer = direct && !directAnswerSource;
    bool ok = true;
    if (direct)
    {
        if (detailMaskRequired && stageDirectAnswer)
        {
            // Exact old fallback: one pass both stages NR50 and writes P50 + gate.
            DlssNrConstants legacy {};
            legacy.Mode = DlssNrMode_EncodeDirectDetail;
            legacy.Width = w;
            legacy.Height = h;
            legacy.Passthrough = resolve.Passthrough;
            ok = shader.DispatchPass(cmd, legacy, proxy, answer, nullptr, nullptr, nullptr, g.input.Get(),
                                     g.detailInfo.Get());
        }
        else
        {
            if (detailMaskRequired)
            {
                DlssNrConstants mask {};
                mask.Mode = DlssNrMode_EncodeDirectDetailMaskOnly;
                mask.Width = w;
                mask.Height = h;
                mask.Passthrough = resolve.Passthrough;
                ok = shader.DispatchPass(cmd, mask, proxy, answer, nullptr, nullptr, nullptr, g.detailInfo.Get(),
                                         nullptr);
            }
            if (stageDirectAnswer)
            {
                DlssNrConstants copy {};
                copy.Mode = DlssNrMode_Downsample;
                copy.Width = w;
                copy.Height = h;
                ok &= shader.DispatchPass(cmd, copy, answer, nullptr, nullptr, nullptr, nullptr, g.input.Get(),
                                          nullptr);
            }
        }
    }
    else
    {
        if (temporalDlaa && !residualDlaa)
        {
            DlssNrConstants encode {};
            encode.Mode = DlssNrMode_Downsample;
            encode.Width = w;
            encode.Height = h;
            encode.Passthrough = resolve.Passthrough;
            ok = shader.DispatchPass(cmd, encode, answer, nullptr, nullptr, nullptr, nullptr, g.input.Get(), nullptr);
        }
        else if (residualDlaa)
        {
            if (g.inputReadable)
            {
                Barrier(cmd, g.input.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                g.inputReadable = false;
            }
            if (g.carrierDebugReadable)
            {
                Barrier(cmd, g.carrierDebug.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                g.carrierDebugReadable = false;
            }

            const float configuredMargin = cfg.DlssNrTemporalCarrierMargin.value_or_default();
            const float margin =
                std::clamp(std::isfinite(configuredMargin) ? std::abs(configuredMargin) : 0.01f, 0.0f, 0.49f);
            const uint32_t gainMode =
                std::min(transfer == 9 ? cfg.DlssNrTemporalResidualGainMode.value_or_default()
                                       : cfg.DlssNrTemporalAnchoredGainMode.value_or_default(),
                         2u);
            const float configuredManual =
                transfer == 9 ? cfg.DlssNrTemporalResidualManualGain.value_or_default()
                              : cfg.DlssNrTemporalAnchoredManualGain.value_or_default();
            const float manualK =
                std::max(std::isfinite(configuredManual) ? std::abs(configuredManual) : 4.0f, 1.0e-6f);
            const float configuredCeiling = cfg.DlssNrTemporalCarrierAutoMaxGain.value_or_default();
            const float autoCeiling =
                std::max(std::isfinite(configuredCeiling) ? std::abs(configuredCeiling) : 32.0f, 1.0e-6f);
            const float configuredRise = cfg.DlssNrTemporalCarrierAutoRiseStopsPerSecond.value_or_default();
            const float riseStopsPerSecond =
                std::max(std::isfinite(configuredRise) ? configuredRise : 4.0f, 0.0f);
            const float frameSeconds =
                std::clamp(std::isfinite(frame.FrameTimeMs) ? frame.FrameTimeMs * 0.001f : 0.01667f,
                           0.0f, 0.25f);
            const float riseMultiplier = std::exp2(riseStopsPerSecond * frameSeconds);
            const float previousAppliedK =
                temporalCarrierTelemetryValid && std::isfinite(temporalCarrierAppliedK)
                    ? std::max(temporalCarrierAppliedK, 1.0e-6f)
                    : 1.0f;

            unsigned statW = std::max(1u, (w + 31u) / 32u);
            unsigned statH = std::max(1u, (h + 31u) / 32u);
            DlssNrConstants limits {};
            limits.Mode = DlssNrMode_TemporalCarrierLimits;
            limits.Width = statW;
            limits.Height = statH;
            limits.GuideWidth = w;
            limits.GuideHeight = h;
            limits.Transfer = carrierType;
            limits.TransferStrength = anchorStrength;
            limits.ResidualScale = margin;
            limits.CompareSwap = extendedCarrierRange ? 1u : 0u;
            limits.Passthrough = cfg.DlssNrTemporalCarrierIgnoreNvidiaWatermarks.value_or_default() ? 1u : 0u;
            limits.ExposureSourceWidth = resolve.Width;
            limits.ExposureSourceHeight = resolve.Height;
            const float configuredWatermarkMarginX = cfg.DlssNrTemporalCarrierWatermarkMarginX.value_or_default();
            const float configuredWatermarkMarginY = cfg.DlssNrTemporalCarrierWatermarkMarginY.value_or_default();
            limits.MvScaleX =
                std::max(std::isfinite(configuredWatermarkMarginX) ? configuredWatermarkMarginX : 128.0f, 0.0f);
            limits.MvScaleY =
                std::max(std::isfinite(configuredWatermarkMarginY) ? configuredWatermarkMarginY : 64.0f, 0.0f);
            ok = shader.DispatchPass(cmd, limits, proxy, answer, nullptr, nullptr, nullptr,
                                     g.carrierReduceA.Get(), g.carrierDiagA.Get());

            ID3D12Resource* statSource = g.carrierReduceA.Get();
            ID3D12Resource* statTarget = g.carrierReduceB.Get();
            ID3D12Resource* diagSource = g.carrierDiagA.Get();
            ID3D12Resource* diagTarget = g.carrierDiagB.Get();
            if (ok)
            {
                Barrier(cmd, statSource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                Barrier(cmd, diagSource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }

            while (ok && (statW > 1u || statH > 1u))
            {
                const unsigned nextW = std::max(1u, (statW + 7u) / 8u);
                const unsigned nextH = std::max(1u, (statH + 7u) / 8u);

                DlssNrConstants reduce {};
                reduce.Mode = DlssNrMode_TemporalCarrierReduce;
                reduce.Width = nextW;
                reduce.Height = nextH;
                reduce.GuideWidth = statW;
                reduce.GuideHeight = statH;
                ok = shader.DispatchPass(cmd, reduce, statSource, nullptr, nullptr, nullptr, nullptr,
                                         statTarget, nullptr);

                DlssNrConstants diagReduce = reduce;
                diagReduce.Mode = DlssNrMode_TemporalCarrierDiagnosticsReduce;
                if (ok)
                    ok = shader.DispatchPass(cmd, diagReduce, diagSource, nullptr, nullptr, nullptr, nullptr,
                                             diagTarget, nullptr);
                if (!ok)
                    break;

                Barrier(cmd, statTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                Barrier(cmd, diagTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                Barrier(cmd, statSource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                Barrier(cmd, diagSource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                std::swap(statSource, statTarget);
                std::swap(diagSource, diagTarget);
                statW = nextW;
                statH = nextH;
            }

            if (ok)
            {
                DlssNrConstants encode {};
                encode.Mode = DlssNrMode_TemporalCarrierEncode;
                encode.Width = w;
                encode.Height = h;
                encode.Transfer = carrierType;
                encode.DebugView = gainMode;
                encode.ResidualScale = manualK;
                encode.ResidualConfidenceSensitivity = autoCeiling;
                encode.TransferStrength = previousAppliedK;
                encode.ColourStrength = riseMultiplier;
                encode.MaxRatio = anchorStrength;
                encode.CompareSwap = extendedCarrierRange ? 1u : 0u;
                ok = shader.DispatchPassAux2(cmd, encode, proxy, answer, nullptr, nullptr, nullptr,
                                             statSource, g.input.Get(), g.carrierApplied.Get());
            }

            if (statSource)
                Barrier(cmd, statSource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            if (ok && pairedAnchorBaseline)
            {
                DlssNrConstants anchor {};
                anchor.Mode = DlssNrMode_TemporalAnchorEncode;
                anchor.Width = w;
                anchor.Height = h;
                anchor.TransferStrength = anchorStrength;
                ok = shader.DispatchPass(cmd, anchor, proxy, nullptr, nullptr, nullptr, nullptr,
                                         g.baselineInput.Get(), nullptr);
                if (ok)
                    Barrier(cmd, g.baselineInput.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }

            if (ok && resolve.DebugView == 5u)
            {
                if (!g.carrierDebug)
                    g.carrierDebug.Attach(CreateScratch(device, DXGI_FORMAT_R16G16B16A16_FLOAT, w, h));
                if (g.carrierDebug)
                {
                    Barrier(cmd, g.input.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    g.inputReadable = true;
                    DlssNrConstants copy {};
                    copy.Mode = DlssNrMode_TemporalCarrierDebugCopy;
                    copy.Width = w;
                    copy.Height = h;
                    ok = shader.DispatchPass(cmd, copy, g.input.Get(), nullptr, nullptr, nullptr, nullptr,
                                             g.carrierDebug.Get(), nullptr);
                    if (ok)
                    {
                        Barrier(cmd, g.carrierDebug.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                        g.carrierDebugReadable = true;
                    }
                }
            }

            if (ok)
            {
                Barrier(cmd, g.carrierApplied.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

                // Asynchronous UI telemetry. The main 1x1 readback carries K/headroom; the tile-sized
                // diagnostic readback uses only its first texel after the sum reduction.
                for (unsigned attempt = 0; attempt < g.carrierReadback.size(); ++attempt)
                {
                    const unsigned index = (g.carrierReadbackCursor + attempt) % unsigned(g.carrierReadback.size());
                    auto& slot = g.carrierReadback[index];
                    if (slot.pending || !slot.image.readback || !slot.diagnostics.readback)
                        continue;
                    slot.image.Copy(cmd, g.carrierApplied.Get(),
                                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    slot.diagnostics.Copy(cmd, diagSource,
                                          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    slot.ready = g.lifetime.CompletionProbe(cmd);
                    slot.pending = true;
                    g.carrierReadbackCursor = (index + 1u) % unsigned(g.carrierReadback.size());
                    break;
                }
            }

            if (diagSource)
                Barrier(cmd, diagSource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        else
        {
            DlssNrConstants encode {};
            encode.Mode = upscaledResidual ? DlssNrMode_Downsample
                                           : structural ? DlssNrMode_EncodeResizeField
                                                        : DlssNrMode_EncodeProxyResidual;
            encode.Width = w;
            encode.Height = h;
            encode.Passthrough = resolve.Passthrough;
            if (upscaledResidual)
                ok = shader.DispatchPass(cmd, encode, answer, nullptr, nullptr, nullptr, nullptr, g.input.Get(), nullptr);
            else
                ok = shader.DispatchPass(cmd, encode, proxy, answer, nullptr, nullptr, nullptr, g.input.Get(), nullptr);
        }
    }

    const bool needsPrivateGuides =
        outputUpscaler == 10 ||
        (referenceRequired && !useExternalReference && detailReferenceUpscaler == 10);
    if (needsPrivateGuides)
    {
        const auto dd = depth->GetDesc(), md = motion->GetDesc();
        const auto regions = DlssNr::ResolveGuideRegions(
            { unsigned(dd.Width), dd.Height }, { unsigned(md.Width), md.Height },
            { frame.RenderSubrectWidth, frame.RenderSubrectHeight }, { frame.OutputWidth, frame.OutputHeight },
            frame.MotionVectorsLowResolution, frame.DepthSubrectBaseX, frame.DepthSubrectBaseY,
            frame.MotionSubrectBaseX, frame.MotionSubrectBaseY);
        if (!regions.depth.valid() || !regions.motion.valid())
            return say("enlargement needs valid depth and motion.");

        DlssNrConstants guides {};
        guides.Mode = DlssNrMode_ResizePrivateGuides;
        guides.Width = w;
        guides.Height = h;
        guides.GuideWidth = regions.depth.width;
        guides.GuideHeight = regions.depth.height;
        guides.DebugView = regions.depth.x;
        guides.CompareMode = regions.depth.y;
        guides.TransferStrength = float(regions.motion.width);
        guides.ColourStrength = float(regions.motion.height);
        guides.CompareSwap = regions.motion.x;
        guides.Transfer = regions.motion.y;
        const auto referenceW = frame.MotionVectorsLowResolution ? frame.RenderSubrectWidth : frame.OutputWidth;
        const auto referenceH = frame.MotionVectorsLowResolution ? frame.RenderSubrectHeight : frame.OutputHeight;
        guides.MvScaleX = frame.MvScaleX * float(w) / std::max(referenceW ? referenceW : regions.motion.width, 1u);
        guides.MvScaleY = frame.MvScaleY * float(h) / std::max(referenceH ? referenceH : regions.motion.height, 1u);
        if (cfg.DlssNrHoldFrame.value_or_default())
            guides.MvScaleX = guides.MvScaleY = 0;
        ok &= shader.DispatchPass(cmd, guides, depth, motion, nullptr, nullptr, nullptr, g.depth.Get(), g.motion.Get());
    }

    if (!ok)
    {
        g.reset = true;
        g.detailReset = true;
        return say("enlargement guide/carrier preparation failed.");
    }

    if ((!direct || stageDirectAnswer) && (!residualDlaa || !g.inputReadable))
    {
        Barrier(cmd, g.input.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (residualDlaa)
            g.inputReadable = true;
    }
    if (needsPrivateGuides)
    {
        for (auto* r : { g.depth.Get(), g.motion.Get() })
            Barrier(cmd, r, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    if (detailMaskRequired)
    {
        Barrier(cmd, g.detailInfo.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        g.detailReadable = true;
    }

    if (!fusedDirectOutput && g.readable)
        Barrier(cmd, g.output.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (referenceRequired && !useExternalReference && g.detailReferenceReadable)
    {
        Barrier(cmd, g.detailReference.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        g.detailReferenceReadable = false;
    }

    DlssNr::PrivateUpscalerFrameDx12 baseFrame;
    baseFrame.depth.resource = g.depth.Get();
    baseFrame.motion.resource = g.motion.Get();
    baseFrame.exposure.resource = g.exposure.Get();
    baseFrame.width = w;
    baseFrame.height = h;
    baseFrame.outputWidth = g.outW;
    baseFrame.outputHeight = g.outH;
    baseFrame.motionScaleX = baseFrame.motionScaleY = 1;
    baseFrame.jitterX = baseFrame.jitterY = 0;
    baseFrame.frameTimeMs = std::isfinite(frame.FrameTimeMs) && frame.FrameTimeMs > 0 ? frame.FrameTimeMs : 16.67f;

    const auto runSpatial = [&](uint32_t selector, ID3D12Resource* source, ID3D12Resource* target,
                                std::unique_ptr<OS_Dx12>& scaler, const char* name) -> bool
    {
        if (selector == 0)
        {
            DlssNrConstants up {};
            up.Mode = DlssNrMode_UpscaleBilinear;
            up.Width = g.outW;
            up.Height = g.outH;
            return shader.DispatchPass(cmd, up, source, nullptr, nullptr, nullptr, nullptr, target, nullptr);
        }
        if (selector == 7)
        {
            // The existing proxy Area path computes exact source-pixel coverage and is valid for
            // enlargement too. P50 -> P100 duplicates source pixels; fractional ratios blend by area.
            DlssNrConstants area {};
            area.Mode = DlssNrMode_Downsample;
            area.Width = g.outW;
            area.Height = g.outH;
            area.Transfer = 0;
            return shader.DispatchPass(cmd, area, source, nullptr, nullptr, nullptr, nullptr, target, nullptr);
        }

        const Scaler kernel = DirectSpatialScaler(selector);
        if (kernel == Scaler::Count)
            return false;
        if (!scaler)
            scaler = std::make_unique<OS_Dx12>(name, device, false, kernel);
        return scaler && scaler->DispatchResources(cmd, source, target);
    };

    ID3D12Resource* const outputSource = directAnswerSource ? answer : g.input.Get();
    bool outputOk = false;
    if (fusedDirectOutput)
    {
        // No NR100 texture is produced. Final resolve samples NR50 with the selected local kernel.
        outputOk = true;
    }
    else if (outputUpscaler == 10)
    {
        auto f = baseFrame;
        f.color.resource = outputSource;
        f.output.resource = g.output.Get();
        f.reset = reset || frame.Reset || g.reset || frames < g.lastFrame || frames > g.lastFrame + 1;
        outputOk = g.dlss->Evaluate(cmd, f);
        if (outputOk && g.lastFrame == 0)
        {
            if (temporalDlaa)
                LOG_INFO("NR temporal DLAA: first 1:1 private DLSS evaluation succeeded on producer queue {}",
                         (void*) g.queue.Get());
            else
                LOG_INFO("NR Direct output: first private DLSS SR evaluation succeeded on producer queue {}",
                         (void*) g.queue.Get());
        }
    }
    else
    {
        outputOk = runSpatial(outputUpscaler, outputSource, g.output.Get(), g.outputSpatialScaler,
                              "DLSS-NR Direct output upscale");
    }

    if (outputOk && !fusedDirectOutput)
    {
        Barrier(cmd, g.output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        g.readable = true;
    }

    bool baselineOk = true;
    if (outputOk && pairedAnchorBaseline)
    {
        if (g.baselineDlss && g.baselineInput && g.baselineOutput)
        {
            auto baselineFrame = baseFrame;
            baselineFrame.color.resource = g.baselineInput.Get();
            baselineFrame.output.resource = g.baselineOutput.Get();
            baselineFrame.reset = reset || frame.Reset || g.baselineReset ||
                                  frames < g.baselineLastFrame || frames > g.baselineLastFrame + 1;
            baselineOk = g.baselineDlss->Evaluate(cmd, baselineFrame);
            g.baselineLastFrame = frames;
            g.baselineReset = !baselineOk;
            if (baselineOk)
                Barrier(cmd, g.baselineOutput.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        else
        {
            baselineOk = false;
        }
        outputOk = outputOk && baselineOk;
    }

    bool temporalResidualDecoded = false;
    if (outputOk && residualDlaa)
    {
        // Reuse the carrier input allocation as the decoded signed residual after DLAA. This keeps
        // the P100-guided path on one common representation and avoids another full P50 texture.
        Barrier(cmd, g.input.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        g.inputReadable = false;
        DlssNrConstants decode {};
        decode.Mode = DlssNrMode_TemporalCarrierDecode;
        decode.Width = w;
        decode.Height = h;
        decode.Transfer = carrierType;
        decode.TransferStrength = anchorStrength;
        decode.DebugView = pairedAnchorBaseline ? 1u : 0u;
        decode.CompareSwap = extendedCarrierRange ? 1u : 0u;
        ID3D12Resource* const decodeBase = pairedAnchorBaseline ? g.baselineOutput.Get() : proxy;
        const bool decoded =
            shader.DispatchPassAux2(cmd, decode, g.output.Get(), decodeBase, nullptr, nullptr, nullptr,
                                    g.carrierApplied.Get(), g.input.Get(), nullptr);
        if (decoded)
        {
            Barrier(cmd, g.input.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            g.inputReadable = true;
            temporalResidualDecoded = true;
        }
        else
        {
            outputOk = false;
        }

        Barrier(cmd, g.carrierApplied.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (pairedAnchorBaseline)
        {
            Barrier(cmd, g.baselineInput.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Barrier(cmd, g.baselineOutput.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
    }

    bool detailReferenceOk = !referenceRequired || useExternalReference;
    if (referenceRequired && !useExternalReference)
    {
        if (detailReferenceUpscaler == 10)
        {
            if (g.detailDlss)
            {
                auto f = baseFrame;
                f.color.resource = referenceProxy;
                f.output.resource = g.detailReference.Get();
                f.reset = reset || frame.Reset || g.detailReset || frames < g.detailLastFrame ||
                          frames > g.detailLastFrame + 1;
                detailReferenceOk = g.detailDlss->Evaluate(cmd, f);
                g.detailLastFrame = frames;
                g.detailReset = !detailReferenceOk;
            }
            else
            {
                detailReferenceOk = false;
            }
        }
        else
        {
            detailReferenceOk =
                runSpatial(detailReferenceUpscaler, referenceProxy, g.detailReference.Get(), g.detailSpatialScaler,
                           "DLSS-NR P50 detail-reference upscale");
            g.detailReset = false;
        }

        if (detailReferenceOk)
        {
            Barrier(cmd, g.detailReference.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            g.detailReferenceReadable = true;
        }
        else
        {
            g.detailReferenceReadable = false;
            static bool warnedDetailScale = false;
            if (!warnedDetailScale)
            {
                warnedDetailScale = true;
                LOG_WARN("{}: selected P50 -> P100 reconstruction failed.",
                         upscaledResidual ? "NR Upscaled residual" : "NR Direct detail recovery");
            }
        }
    }

    if ((!direct || stageDirectAnswer) && !temporalResidualDecoded && (!residualDlaa || g.inputReadable))
    {
        Barrier(cmd, g.input.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (residualDlaa)
            g.inputReadable = false;
    }
    if (needsPrivateGuides)
    {
        for (auto* r : { g.depth.Get(), g.motion.Get() })
            Barrier(cmd, r, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    g.lastFrame = frames;
    g.reset = !outputOk;
    if (!outputOk)
    {
        if (residualDlaa && g.carrierApplied)
        {
            // If DLSS itself failed before the decode block, carrierApplied is still readable.
            // Return it to the resting UAV state for Retry/re-entry.
            // (When decode ran, it was already restored above.)
            if (!temporalResidualDecoded)
            {
                // Decode failure also leaves it restored above; state transition is only needed when
                // output evaluation failed before decode. Keep the branch conservative by rebuilding.
                g.failed = true;
                return say("Temporal carrier DLAA/decode failed; use Retry.");
            }
        }
        if (pairedAnchorBaseline && !baselineOk)
        {
            g.failed = true;
            return say(std::string("Private paired anchor DLAA baseline: ") +
                       (g.baselineDlss ? g.baselineDlss->Error() : "feature unavailable"));
        }
        if (outputUpscaler == 10)
        {
            g.failed = true;
            return say(std::string(temporalDlaa ? "Private DLSS DLAA: " : "Private DLSS SR: ") + g.dlss->Error());
        }
        return say("selected NR50 -> NR100 spatial upscaler failed.");
    }

    if (upscaledResidual && !detailReferenceOk)
        return say("selected P50 -> P100 reference upscaler failed.");

    enlargementStatus.clear();
    return residualDlaa ? g.input.Get() : (fusedDirectOutput ? answer : g.output.Get());
}
