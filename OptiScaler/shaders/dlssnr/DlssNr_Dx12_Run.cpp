#include "pch.h"
#include "DlssNr_Dx12_State.h"

namespace
{
Scaler AsyncDetailSpatialScaler(uint32_t index)
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
    // 0 = bilinear, 7 = Area, 8 = MAGIC, 9 = the branch's current FSR1 path.
    // Other spatial kernels keep their materialized NR100 fallback until their exact kernels are embedded.
    return selector == 0 || selector == 7 || selector == 8 || selector == 9;
}

bool DirectAnswerCanFeedUpscaler(ID3D12Resource* answer)
{
    return answer && answer->GetDesc().Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
}
} // namespace

auto DlssNr_Dx12::State::Run(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* colour, ID3D12Resource* depth,
                             ID3D12Resource* motion, ID3D12Resource* output, const DlssNrFrameInfo& frame,
                             ID3D12CommandQueue* timingQueue, LateContext::Slot* asyncSlot,
                             ID3D12Resource* finishedEncodedColor, bool asyncOwnsFinishedPrep) -> void
{
    std::lock_guard<std::recursive_mutex> nrLock(mutex);
    const Config& cfg = *Config::Instance();
    nr.spatialActive = false;

    if (nr.failed || cmdList == nullptr || colour == nullptr || depth == nullptr || motion == nullptr ||
        output == nullptr)
    {
        ReportSkipOnce(nr.failed ? "it already failed this session" : "a resource was missing");
        return;
    }

    ID3D12Resource* target = output;
    ID3D12GraphicsCommandList* const resolveCmd = cmdList;
    const bool ownsFinishedPrep = asyncSlot != nullptr && asyncOwnsFinishedPrep;
    const bool ownsPrivateAsyncLists = asyncSlot != nullptr;

    // Guard creation and dispatch together: either can record GPU work and alter compute bindings.
    const bool restoreRequired =
        cfg.RestoreComputeSignature.value_or_default() || cfg.RestoreGraphicSignature.value_or_default();
    if (restoreRequired && !frame.IndependentCommands && !D3D12Hooks::CanRestoreRootSignature(cmdList))
    {
        ReportSkipOnce("the upscaler could not restore state this frame");
        return;
    }
    lifetime.Record(cmdList);
    ScopedNrStateEnvelope stateEnvelope(cmdList);

    // A completed upscaler output normally arrives as a UAV. The pre-SR colour input instead arrives
    // readable. Track every transition so both paths return the resource exactly as their caller gave
    // it to us; a pre-SR resource without UAV support is written through a scratch-and-copy fallback.
    const D3D12_RESOURCE_STATES outputArrival =
        frame.PipelineManagedStates ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
        : frame.FinishedPicture     ? (D3D12_RESOURCE_STATES) frame.OutputArrivalState
        : frame.BeforeUpscale       ? (!frame.PrivateColorCopy && Config::Instance()->ColorResourceBarrier.has_value()
                                           ? (D3D12_RESOURCE_STATES) Config::Instance()->ColorResourceBarrier.value()
                                           : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
        : Config::Instance()->OutputResourceBarrier.has_value()
            ? (D3D12_RESOURCE_STATES) Config::Instance()->OutputResourceBarrier.value()
            : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES targetState = outputArrival;
    const auto TransitionTarget = [&](D3D12_RESOURCE_STATES to)
    {
        Barrier(cmdList, target, targetState, to);
        targetState = to;
    };

    Microsoft::WRL::ComPtr<ID3D12Device> deviceRef;

    if (FAILED(target->GetDevice(IID_PPV_ARGS(&deviceRef))))
    {
        ReportSkipOnce("the output texture belongs to no D3D12 device");
        return;
    }

    auto* device = deviceRef.Get();
    const D3D12_RESOURCE_DESC desc = target->GetDesc();
    const auto active =
        frame.BeforeUpscale
            ? DlssNr::PreSrColorExtent(desc, frame.RenderSubrectWidth, frame.RenderSubrectHeight)
            : std::optional<DlssNr::ColorExtent> { DlssNr::ColorExtent { (unsigned int) desc.Width, desc.Height } };
    if (!active)
    {
        ReportSkipOnce("the pre-SR active colour size is invalid");
        return;
    }
    const auto width = active->width;
    const auto height = active->height;
    if (frame.Reset)
        nr.finalColorHistoryValid = false;
    const bool cropColor = frame.BeforeUpscale && (width != desc.Width || height != desc.Height);
    const bool targetSupportsUav = cropColor || (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0;

    const auto guideDesc = depth->GetDesc();
    const auto motionDesc = motion->GetDesc();
    const auto guides = DlssNr::ResolveGuideRegions(
        { (unsigned int) guideDesc.Width, guideDesc.Height }, { (unsigned int) motionDesc.Width, motionDesc.Height },
        { frame.RenderSubrectWidth, frame.RenderSubrectHeight }, { frame.OutputWidth, frame.OutputHeight },
        frame.MotionVectorsLowResolution, frame.DepthSubrectBaseX, frame.DepthSubrectBaseY, frame.MotionSubrectBaseX,
        frame.MotionSubrectBaseY);
    if (!guides.depth.valid() || !guides.motion.valid())
    {
        ReportSkipOnce("depth or motion-vector subrect is empty");
        return;
    }
    const auto guideWidth = guides.depth.width, guideHeight = guides.depth.height;

    if (frame.Reset)
    {
        nr.reset = true;

        ++resets;

        if (resets <= 3 || resets % 100 == 0)
            LOG_INFO("DLSS-NR: the game asked for a history reset ({} so far)", resets);
    }

    // Guide dimensions can change without rebuilding the model; report changes as they occur.

    const GuideReport guidesNow { true,       frame.DepthInverted, frame.MvScaleX, frame.MvScaleY,
                                  guideWidth, guideHeight,         width,          (unsigned int) height };

    if (loggedGuides != guidesNow)
    {
        loggedGuides = guidesNow;
        LOG_INFO("DLSS-NR guides: depth {}, motion vector scale {} x {}, guides {}x{} for a {}x{} frame",
                 frame.DepthInverted ? "inverted" : "not inverted", frame.MvScaleX, frame.MvScaleY, guideWidth,
                 guideHeight, width, height);
    }

    const unsigned int requestedPasses =
        std::clamp(cfg.DlssNrPasses.value_or_default(), 1u,
                   cfg.DlssNrUnlockPasses.value_or_default() ? DlssNr::MaxPassCount : DlssNr::DefaultMaxPassCount);
    for (auto& model : nr.models)
        model.Collect();
    if ((!NVNGXProxy::IsDx12Inited() && !NVNGXProxy::InitDx12(device)) || !DlssNr::Proxy::Context::Available())
    {
        nr.failed = true;
        nr.reason = "the NVIDIA NGX driver does not provide Neural Rendering";
        LOG_ERROR("DLSS-NR unavailable: {}", nr.reason);
        return;
    }

    // Only the model runs at working resolution; source and composition remain at native size.
    float workScale = cfg.DlssNrWorkingScale.value_or_default();
    if (!std::isfinite(workScale))
        workScale = 1.0f;
    workScale = std::clamp(workScale, 0.25f, 2.0f);
    const auto workWidth = (unsigned int) (width * workScale + 0.5f);
    const auto workHeight = (unsigned int) (height * workScale + 0.5f);
    const bool reduced = workWidth != width || workHeight != height;

    const auto spatialSettings = DlssNr::Spatial::ReadSettings(cfg);
    const auto spatialLayout = DlssNr::Spatial::Build(spatialSettings, width, height, workScale);
    const bool spatialSignatureChanged =
        nr.spatialSignatureValid &&
        (nr.spatialLayout != spatialLayout || nr.spatialColorFormat != desc.Format ||
         nr.spatialDepthFormat != guideDesc.Format || nr.spatialMotionFormat != motionDesc.Format ||
         nr.spatialDepthW != guideDesc.Width || nr.spatialDepthH != guideDesc.Height ||
         nr.spatialMotionW != motionDesc.Width || nr.spatialMotionH != motionDesc.Height);
    if (spatialSignatureChanged)
    {
        if (nr.spatialLayout.requested || spatialLayout.requested)
            nr.reset = true;
        nr.spatialFallback = false;
        nr.spatialFallbackReason = "";
    }
    nr.spatialLayout = spatialLayout;
    nr.spatialSignatureValid = true;
    nr.spatialColorFormat = desc.Format;
    nr.spatialDepthFormat = guideDesc.Format;
    nr.spatialMotionFormat = motionDesc.Format;
    nr.spatialDepthW = (unsigned) guideDesc.Width;
    nr.spatialDepthH = guideDesc.Height;
    nr.spatialMotionW = (unsigned) motionDesc.Width;
    nr.spatialMotionH = motionDesc.Height;
    const bool spatial = spatialLayout.active && !nr.spatialFallback;
    if (!spatial && nr.spatialColor)
        ReleaseSpatialResources();
    if (spatial && (!shader.SpatialReady() || !PrepareSpatialResources(device, spatialLayout)))
    {
        nr.spatialFallback = true;
        nr.spatialFallbackReason = "the spatial shader or textures could not be created";
        nr.reset = true;
        modelRunning = false;
        ReportSkipOnce(nr.spatialFallbackReason);
        return;
    }
    const unsigned modelWidth = spatial ? spatialLayout.modelW : workWidth;
    const unsigned modelHeight = spatial ? spatialLayout.modelH : workHeight;
    if (!PrepareRunModels(cmdList, device, frame, desc, { width, height }, { modelWidth, modelHeight }, workScale,
                          requestedPasses, spatial))
        return;

    const uint32_t transferMode = cfg.DlssNrTransfer.value_or_default();
    const bool upscaledResidualMode = transferMode == 6;
    const uint32_t detailExecution =
        std::min(upscaledResidualMode ? cfg.DlssNrUpscaledResidualReferenceExecutionMode.value_or_default()
                                     : cfg.DlssNrDirectDetailReferenceExecutionMode.value_or_default(),
                 2u);
    const uint32_t detailReferenceSelector =
        std::min(upscaledResidualMode ? cfg.DlssNrUpscaledResidualReferenceUpscaler.value_or_default()
                                     : cfg.DlssNrDirectDetailReferenceUpscaler.value_or_default(),
                 10u);
    const bool directFinalReferenceExperiment =
        transferMode == 5 && cfg.DlssNrExperimentStructureTransfer.value_or_default() != 0;
    const bool needsP50Reference =
        upscaledResidualMode ||
        (transferMode == 5 &&
         (cfg.DlssNrDirectDetailRecovery.value_or_default() != 0 || directFinalReferenceExperiment));

    // Async P50 reconstruction and NR are both read-only consumers of the same reduced image. Give only
    // that P50 scratch simultaneous-access semantics while async is requested, so the two queues can share
    // it directly instead of copying the whole P50 raster every frame. Serial recreates the ordinary
    // non-simultaneous texture, preserving the previous resource policy and its compression/cache behaviour.
    const bool wantSharedP50 =
        ownsPrivateAsyncLists && detailExecution != 1 && frame.IndependentCommands && !spatial && workScale < 0.999f &&
        needsP50Reference && detailReferenceSelector < 10 && timingQueue &&
        timingQueue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT && !late.asyncDetailQueueFailed;

    if (reduced && !spatial)
    {
        const auto smallDesc = nr.colorSmall ? nr.colorSmall->GetDesc() : D3D12_RESOURCE_DESC {};
        const bool hasSharedP50 =
            nr.colorSmall &&
            (smallDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS) != 0;
        const bool smallMatches =
            nr.colorSmall && smallDesc.Width == workWidth && smallDesc.Height == workHeight &&
            smallDesc.Format == desc.Format && hasSharedP50 == wantSharedP50;
        if (!smallMatches)
        {
            ParkNrResource(nr.colorSmall);
            auto flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            if (wantSharedP50)
                flags |= D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
            nr.colorSmall = CreateScratch(device, desc.Format, workWidth, workHeight, flags);
        }
    }

    bool asyncDetailActive =
        wantSharedP50 && nr.colorSmall != nullptr && !cfg.DlssNrHoldFrame.value_or_default() &&
        cfg.DlssNrDebugView.value_or_default() == 0 && cfg.DlssNrCompare.value_or_default() == 0 &&
        !cfg.DlssNrShowSkinMask.value_or_default() && !captureFrames.isActive() &&
        !styleAnalysisCapture.active && !::State::Instance().isShuttingDown;
    bool asyncExternalDetail = false;
    bool asyncNrSubmitted = false;
    uint64_t asyncDetailDoneValue = 0;
    bool ownedGuidesReadable = false;

    const auto closeAsyncLists = [&]()
    {
        if (!asyncSlot)
            return;
        for (auto* list : { asyncSlot->asyncPrefixCommands.Get(), asyncSlot->asyncDetailCommands.Get(),
                            asyncSlot->asyncNrCommands.Get() })
            if (list)
                list->Close();
    };

    if (asyncDetailActive)
    {
        ID3D12CommandQueue* realQueue = nullptr;
        auto* directIdentity = timingQueue;
        if (timingQueue && Util::CheckForRealObject(__FUNCTION__, timingQueue, (IUnknown**) &realQueue))
            directIdentity = realQueue;
        Microsoft::WRL::ComPtr<ID3D12Device> queueDevice;
        if (!timingQueue || timingQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT ||
            FAILED(timingQueue->GetDevice(IID_PPV_ARGS(&queueDevice))) || queueDevice.Get() != device ||
            (late.asyncDirectQueue && late.asyncDirectQueue.Get() != directIdentity) || late.asyncDetailQueueFailed)
        {
            asyncDetailActive = false;
        }
        else
        {
            if (!late.asyncDetailQueue)
            {
                D3D12_COMMAND_QUEUE_DESC q {};
                q.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
                if (FAILED(device->CreateCommandQueue(&q, IID_PPV_ARGS(&late.asyncDetailQueue))) ||
                    FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&late.asyncP50Fence))) ||
                    FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&late.asyncDetailFence))))
                {
                    late.asyncDetailQueue.Reset();
                    late.asyncP50Fence.Reset();
                    late.asyncDetailFence.Reset();
                    late.asyncDetailQueueFailed = true;
                    asyncDetailActive = false;
                }
                else
                {
                    late.asyncDirectQueue = directIdentity;
                    LOG_INFO("DLSS-NR P50 detail reference: separate async compute queue created.");
                }
            }
            else if (!late.asyncDirectQueue)
                late.asyncDirectQueue = directIdentity;

            const auto resetList = [&](D3D12_COMMAND_LIST_TYPE type,
                                       Microsoft::WRL::ComPtr<ID3D12CommandAllocator>& allocator,
                                       Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& list) -> bool
            {
                if (!allocator && FAILED(device->CreateCommandAllocator(type, IID_PPV_ARGS(&allocator))))
                    return false;
                if (!list)
                {
                    if (FAILED(device->CreateCommandList(0, type, allocator.Get(), nullptr, IID_PPV_ARGS(&list))))
                        return false;
                    if (FAILED(list->Close()))
                        return false;
                }
                return SUCCEEDED(allocator->Reset()) && SUCCEEDED(list->Reset(allocator.Get(), nullptr));
            };

            if (asyncDetailActive &&
                (!resetList(D3D12_COMMAND_LIST_TYPE_DIRECT, asyncSlot->asyncPrefixAllocator,
                            asyncSlot->asyncPrefixCommands) ||
                 !resetList(D3D12_COMMAND_LIST_TYPE_COMPUTE, asyncSlot->asyncDetailAllocator,
                            asyncSlot->asyncDetailCommands) ||
                 !resetList(D3D12_COMMAND_LIST_TYPE_DIRECT, asyncSlot->asyncNrAllocator, asyncSlot->asyncNrCommands)))
            {
                closeAsyncLists();
                asyncDetailActive = false;
            }

            if (asyncDetailActive)
            {
                if (asyncSlot->asyncDetailReference)
                {
                    const auto have = asyncSlot->asyncDetailReference->GetDesc();
                    if (have.Width != width || have.Height != height ||
                        have.Format != DXGI_FORMAT_R16G16B16A16_FLOAT)
                    {
                        asyncSlot->asyncDetailReference.Reset();
                        asyncSlot->asyncDetailScaler.reset();
                        asyncSlot->asyncDetailScalerSelector = UINT32_MAX;
                        asyncSlot->asyncDetailReferenceReadable = false;
                    }
                }
                if (!asyncSlot->asyncDetailReference)
                    asyncSlot->asyncDetailReference.Attach(
                        CreateScratch(device, DXGI_FORMAT_R16G16B16A16_FLOAT, width, height));
                if (!asyncSlot->asyncDetailReference)
                {
                    closeAsyncLists();
                    asyncDetailActive = false;
                }
                else if (asyncSlot->asyncDetailScalerSelector != detailReferenceSelector)
                {
                    asyncSlot->asyncDetailScaler.reset();
                    asyncSlot->asyncDetailScalerSelector = detailReferenceSelector;
                }
            }
        }
    }

    if (ownsPrivateAsyncLists && detailExecution == 2 && !asyncDetailActive)
    {
        static bool warnedForcedAsyncFallback = false;
        if (!warnedForcedAsyncFallback)
        {
            warnedForcedAsyncFallback = true;
            LOG_WARN("DLSS-NR P50 detail reference: Async compute requested but this path cannot be split; using Serial.");
        }
    }

    const auto convertFinishedInput = [&](ID3D12GraphicsCommandList* list) -> bool
    {
        if (!finishedEncodedColor)
            return true;
        DlssNrConstants conversion {};
        conversion.Width = width;
        conversion.Height = height;
        Barrier(list, finishedEncodedColor, D3D12_RESOURCE_STATE_PRESENT,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        const bool ok =
            shader.DispatchResidualPass(list, conversion, finishedEncodedColor, nullptr, nullptr, nullptr, target, true);
        Barrier(list, finishedEncodedColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_PRESENT);
        return ok;
    };

    const auto prepareOwnedGuides = [&](ID3D12GraphicsCommandList* list)
    {
        if (!ownsFinishedPrep || ownedGuidesReadable)
            return;
        Barrier(list, depth, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier(list, motion, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ownedGuidesReadable = true;
    };
    const auto restoreOwnedGuides = [&](ID3D12GraphicsCommandList* list)
    {
        if (!ownedGuidesReadable)
            return;
        Barrier(list, motion, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        Barrier(list, depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        ownedGuidesReadable = false;
    };
    // The parameter adapter already combined the HDR flag with the active color format.
    const bool isHdrBuffer = frame.ColourIsLinearHdr;

    if (!reportedHdr || reportedHdrValue != isHdrBuffer || reportedBefore != frame.BeforeUpscale)
    {
        reportedHdr = true;
        reportedHdrValue = isHdrBuffer;
        reportedBefore = frame.BeforeUpscale;
        LOG_INFO("DLSS-NR {} SR: the game's DLSS colour space is {} so the colour transform is {}",
                 frame.BeforeUpscale ? "before" : "after", isHdrBuffer ? "linear HDR" : "already tone-mapped",
                 isHdrBuffer ? "on" : "off");
    }

    if (!shader.IsInit())
    {
        nr.failed = true;
        nr.reason = "the colour codec would not compile";
        LOG_ERROR("DLSS-NR unavailable: {}", nr.reason);
        return;
    }

    // Advance capture scheduling only once the codec and models are ready.
    ++frames;
    lifetime.Collect();
    CheckCaptureTrigger();

    if (styleAnalysisCapture.active && styleAnalysisCapture.copiesRecorded &&
        styleAnalysisCapture.readback.Ready())
    {
        const auto directory = styleAnalysisCapture.readback.directory;
        const bool written = styleAnalysisCapture.readback.Write();
        if (written)
            LOG_INFO("NR style analysis capture saved: {}", directory.string());
        else
            LOG_WARN("NR style analysis capture PNG/RAW write failed: {}", directory.string());
        ReleaseStyleAnalysisCapture();
    }

    if (captureFrames.isActive())
    {
        const auto captureDir = Util::DllPath().remove_filename() / "dlssnr-capture";
        const auto written = captureFrames.write(captureDir);

        if (!written.empty())
            LOG_INFO("DLSS-NR wrote matched before/after frames to {}", written);
    }

    ResTrack_Dx12::HookLateNrQueue(device);
    if (gpuTime == nullptr)
        gpuTime = std::make_unique<DlssNrGpuTime>(device);

    if (ngxTime == nullptr)
        ngxTime = std::make_unique<DlssNrGpuTime>(device);

    if (asyncDetailActive && !convertFinishedInput(asyncSlot->asyncPrefixCommands.Get()))
    {
        closeAsyncLists();
        asyncDetailActive = false;
    }
    if (!asyncDetailActive)
    {
        if (ownsFinishedPrep)
        {
            if (!convertFinishedInput(resolveCmd))
            {
                ReportSkipOnce("the finished HDR input could not be decoded for NR");
                return;
            }
            prepareOwnedGuides(resolveCmd);
        }
        gpuTime->Start(resolveCmd);
    }
    else
    {
        // Keep PQ/scRGB conversion outside the reported NR interval, matching the serial finished-picture path.
        gpuTime->Start(asyncSlot->asyncPrefixCommands.Get());
    }

    // Copy just the live image, not the stale right/bottom margins. Do this only after model
    // creation/pending-submission early returns, and inside the measured GPU interval. The compact
    // texture lets every existing codec/compare/hold/capture path use unmodified pixel coordinates.
    ID3D12Resource* const gameColor = target;
    if (cropColor)
    {
        TransitionTarget(D3D12_RESOURCE_STATE_COPY_SOURCE);
        Barrier(cmdList, nr.activeColor, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        DlssNr::CopyActiveColor(cmdList, nr.activeColor, gameColor, *active);
        TransitionTarget(outputArrival);
        Barrier(cmdList, nr.activeColor, D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        target = nr.activeColor;
        targetState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }

    const auto FinishColor = [&](bool copyBack)
    {
        if (cropColor)
        {
            if (copyBack)
            {
                TransitionTarget(D3D12_RESOURCE_STATE_COPY_SOURCE);
                Barrier(cmdList, gameColor, outputArrival, D3D12_RESOURCE_STATE_COPY_DEST);
                DlssNr::CopyActiveColor(cmdList, gameColor, target, *active);
                Barrier(cmdList, gameColor, D3D12_RESOURCE_STATE_COPY_DEST, outputArrival);
            }
            TransitionTarget(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        else
        {
            TransitionTarget(outputArrival);
        }
    };

    auto* encodeCmd = asyncDetailActive ? asyncSlot->asyncPrefixCommands.Get() : resolveCmd;
    EncodeContext encoded { encodeCmd, device, target, targetState, frame, workScale, targetSupportsUav, spatial };
    EncodeInput(encoded);
    targetState = encoded.targetState;
    const bool cancelInputPreparationFootprint =
        encoded.inputPreparationActive &&
        cfg.DlssNrExperimentCancelInputPreparationFootprint.value_or_default();

    if (asyncDetailActive)
    {
        // The prefix already wrote P50 and transitioned it to SRV. Because the async P50 scratch was created
        // with ALLOW_SIMULTANEOUS_ACCESS, direct NR and the compute detail pass can now read that same texture
        // concurrently after the prefix fence -- no per-frame CopyResource or duplicate P50 allocation.
        // Upscaled NR residual must subtract a reconstruction of the same sharp base S used by the
        // rebased NR50 answer. Otherwise C100 - B100 would add -(B-S) back after cancellation.
        auto* const sharedDetailInput =
            cancelInputPreparationFootprint && nr.colorSmall
                ? nr.colorSmall
                : encoded.referenceInput ? encoded.referenceInput : encoded.modelInput;
        auto* detailCmd = asyncSlot->asyncDetailCommands.Get();
        if (asyncSlot->asyncDetailReferenceReadable)
        {
            Barrier(detailCmd, asyncSlot->asyncDetailReference.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            asyncSlot->asyncDetailReferenceReadable = false;
        }

        bool detailRecorded = false;
        if (detailReferenceSelector == 0)
        {
            DlssNrConstants up {};
            up.Mode = DlssNrMode_UpscaleBilinear;
            up.Width = width;
            up.Height = height;
            detailRecorded = shader.DispatchPass(detailCmd, up, sharedDetailInput, nullptr, nullptr, nullptr, nullptr,
                                                 asyncSlot->asyncDetailReference.Get(), nullptr);
        }
        else if (detailReferenceSelector == 7)
        {
            // Reuse the exact area-weighted resize already used by proxy downsampling. Its footprint
            // math is direction-agnostic: P50 -> P100 becomes point-like, while non-integer ratios
            // blend source pixels according to covered area.
            DlssNrConstants area {};
            area.Mode = DlssNrMode_Downsample;
            area.Width = width;
            area.Height = height;
            area.Transfer = 0;
            detailRecorded =
                shader.DispatchPass(detailCmd, area, sharedDetailInput, nullptr, nullptr, nullptr, nullptr,
                                    asyncSlot->asyncDetailReference.Get(), nullptr);
        }
        else
        {
            const Scaler kernel = AsyncDetailSpatialScaler(detailReferenceSelector);
            if (kernel != Scaler::Count)
            {
                if (!asyncSlot->asyncDetailScaler)
                    asyncSlot->asyncDetailScaler =
                        std::make_unique<OS_Dx12>("DLSS-NR async P50 detail-reference upscale", device, false, kernel);
                detailRecorded = asyncSlot->asyncDetailScaler &&
                                 asyncSlot->asyncDetailScaler->DispatchResources(
                                     detailCmd, sharedDetailInput, asyncSlot->asyncDetailReference.Get(), false);
            }
        }
        if (detailRecorded)
        {
            Barrier(detailCmd, asyncSlot->asyncDetailReference.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            asyncSlot->asyncDetailReferenceReadable = true;
            asyncExternalDetail = true;
        }

        if (FAILED(asyncSlot->asyncPrefixCommands->Close()) || FAILED(asyncSlot->asyncDetailCommands->Close()))
        {
            late.asyncDetailQueueFailed = true;
            nr.failed = true;
            nr.reason = "the async P50 detail-reference command lists could not be closed";
            ReportSkipOnce(nr.reason);
            return;
        }

        ID3D12CommandList* prefixLists[] = { asyncSlot->asyncPrefixCommands.Get() };
        timingQueue->ExecuteCommandLists(1, prefixLists);
        const uint64_t p50Ready = ++late.asyncP50Serial;
        if (FAILED(timingQueue->Signal(late.asyncP50Fence.Get(), p50Ready)) ||
            FAILED(late.asyncDetailQueue->Wait(late.asyncP50Fence.Get(), p50Ready)))
        {
            late.asyncDetailQueueFailed = true;
            nr.failed = true;
            nr.reason = "the async P50 detail-reference queues could not be synchronized";
            return;
        }

        ID3D12CommandList* detailLists[] = { asyncSlot->asyncDetailCommands.Get() };
        late.asyncDetailQueue->ExecuteCommandLists(1, detailLists);
        asyncDetailDoneValue = ++late.asyncDetailSerial;
        if (FAILED(late.asyncDetailQueue->Signal(late.asyncDetailFence.Get(), asyncDetailDoneValue)))
        {
            late.asyncDetailQueueFailed = true;
            nr.failed = true;
            nr.reason = "the async P50 detail-reference queue stopped";
            return;
        }

        cmdList = asyncSlot->asyncNrCommands.Get();
        lifetime.Record(cmdList);
        prepareOwnedGuides(cmdList);
    }

    // Close/submit the owned direct NR list and join the async reference only at the resolve seam.
    // This helper is deliberately available to early-failure paths too, so submitted compute detail
    // work can never outlive the slot fence that protects its per-frame resources.
    const auto finishAsyncNr = [&]() -> bool
    {
        if (!asyncDetailActive || asyncNrSubmitted)
            return true;
        restoreOwnedGuides(cmdList);
        asyncNrSubmitted = true;
        if (FAILED(cmdList->Close()))
        {
            cmdList = resolveCmd;
            nr.failed = true;
            nr.reason = "the async NR command list could not be closed";
            // Detail work may already be executing. Put its completion behind the slot fence.
            timingQueue->Wait(late.asyncDetailFence.Get(), asyncDetailDoneValue);
            asyncSlot->done = std::max(asyncSlot->done, asyncSlot->ready) + 1;
            if (SUCCEEDED(timingQueue->Signal(asyncSlot->fence.Get(), asyncSlot->done)))
                asyncSlot->asyncProvisional = true;
            return false;
        }
        ID3D12CommandList* nrLists[] = { asyncSlot->asyncNrCommands.Get() };
        timingQueue->ExecuteCommandLists(1, nrLists);
        if (FAILED(timingQueue->Wait(late.asyncDetailFence.Get(), asyncDetailDoneValue)))
        {
            cmdList = resolveCmd;
            nr.failed = true;
            nr.reason = "the direct queue could not wait for the async P50 detail reference";
            return false;
        }
        // Do not signal the direct queue here on a successful frame. The final resolve/compose
        // submission signals this slot once, and that later signal covers the NR list, the compute
        // detail wait, and the resolve itself. Failure paths above still issue provisional protection.
        cmdList = resolveCmd;
        return true;
    };
    if (spatial && !encoded.encodeSucceeded)
    {
        nr.spatialFallback = true;
        nr.spatialFallbackReason = "the spatial frame's colour encode dispatch failed";
        nr.reset = true;
        modelRunning = false;
        Barrier(cmdList, nr.hdrCopy, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier(cmdList, nr.colorCopy, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        FinishColor(false);
        EndGpuTiming(cmdList);
        restoreOwnedGuides(cmdList);
        return;
    }
    auto* modelInput = encoded.modelInput;

    ID3D12Resource* depthIn = ReadableGuide(device, cmdList, depth, &nr.depthClone);
    ID3D12Resource* motionIn = ReadableGuide(device, cmdList, motion, &nr.motionClone);

    if (depthIn == nullptr || motionIn == nullptr)
    {
        nr.reset = true;
        ReportSkipOnce("the game's depth or motion vectors could not be made readable this frame");
        Barrier(cmdList, nr.hdrCopy, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier(cmdList, nr.colorCopy, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        FinishColor(false);
        EndGpuTiming(cmdList);
        if (asyncDetailActive)
            finishAsyncNr();
        else
            restoreOwnedGuides(cmdList);
        if (depthIn == nr.depthClone)
            Barrier(cmdList, nr.depthClone, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_COPY_DEST);
        if (motionIn == nr.motionClone)
            Barrier(cmdList, nr.motionClone, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_COPY_DEST);
        return;
    }
    ID3D12Resource* const originalDepthIn = depthIn;
    ID3D12Resource* const originalMotionIn = motionIn;

    if (spatial)
    {
        const auto colorConstants =
            DlssNr::Spatial::MakeConstants(spatialLayout, 100, guides, frame.MvScaleX, frame.MvScaleY, width, height);
        const auto guideConstants =
            DlssNr::Spatial::MakeConstants(spatialLayout, 101, guides, frame.MvScaleX, frame.MvScaleY, width, height);
        const bool packedColor =
            shader.DispatchSpatial(cmdList, colorConstants, nr.colorCopy, nullptr, nullptr, nr.spatialColor);
        const bool packedGuides = packedColor && shader.DispatchSpatial(cmdList, guideConstants, nr.colorCopy, depthIn,
                                                                        motionIn, nr.spatialDepth, nr.spatialMotion);
        if (!packedGuides)
        {
            nr.spatialFallback = true;
            nr.spatialFallbackReason = "a spatial packing dispatch failed";
            nr.reset = true;
            modelRunning = false;
            ReportSkipOnce(nr.spatialFallbackReason);
            Barrier(cmdList, nr.hdrCopy, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Barrier(cmdList, nr.colorCopy, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            FinishColor(false);
            EndGpuTiming(cmdList);
            if (ownsFinishedPrep)
                restoreOwnedGuides(cmdList);
            if (originalDepthIn == nr.depthClone)
                Barrier(cmdList, nr.depthClone, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_COPY_DEST);
            if (originalMotionIn == nr.motionClone)
                Barrier(cmdList, nr.motionClone, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_COPY_DEST);
            return;
        }
        for (auto* packed : { nr.spatialColor, nr.spatialDepth, nr.spatialMotion })
            Barrier(cmdList, packed, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        modelInput = nr.spatialColor;
        depthIn = nr.spatialDepth;
        motionIn = nr.spatialMotion;
    }

    // Below native (and not packed by spatial compression, which brings its own guides), the model
    // was handed a colour at the working size and depth and motion at the frame's size, with only
    // the vector magnitudes rescaled. Nothing says the model resamples a guide that is larger than
    // its colour, and the picture said it does not: every scale below 100% flickered and settled for
    // frames after the camera stopped, while the same model at 100% of a frame the game had already
    // shrunk was steady. So give it guides at its own size -- the same point resample the DLSS
    // enlargement path already builds for its private upscaler -- and describe them as a full,
    // zero-origin region. The vectors keep the game's units; the scale below converts them.
    bool matchedGuides = false;
    if (reduced && !spatial && workWidth < width && cfg.DlssNrMatchGuides.value_or_default())
    {
        const auto depthSmallDesc = nr.depthSmall ? nr.depthSmall->GetDesc() : D3D12_RESOURCE_DESC {};
        if (!nr.depthSmall || depthSmallDesc.Width != workWidth || depthSmallDesc.Height != workHeight ||
            depthSmallDesc.Format != DXGI_FORMAT_R32_FLOAT)
        {
            ParkNrResource(nr.depthSmall);
            nr.depthSmall = CreateScratch(device, DXGI_FORMAT_R32_FLOAT, workWidth, workHeight);
        }
        const auto motionSmallDesc = nr.motionSmall ? nr.motionSmall->GetDesc() : D3D12_RESOURCE_DESC {};
        if (!nr.motionSmall || motionSmallDesc.Width != workWidth || motionSmallDesc.Height != workHeight ||
            motionSmallDesc.Format != DXGI_FORMAT_R32G32_FLOAT)
        {
            ParkNrResource(nr.motionSmall);
            nr.motionSmall = CreateScratch(device, DXGI_FORMAT_R32G32_FLOAT, workWidth, workHeight);
        }
        if (nr.depthSmall != nullptr && nr.motionSmall != nullptr)
        {
            DlssNrConstants resize {};
            resize.Mode = DlssNrMode_ResizePrivateGuides;
            resize.Width = workWidth;
            resize.Height = workHeight;
            resize.GuideWidth = guides.depth.width;
            resize.GuideHeight = guides.depth.height;
            resize.DebugView = guides.depth.x;
            resize.CompareMode = guides.depth.y;
            resize.TransferStrength = float(guides.motion.width);
            resize.ColourStrength = float(guides.motion.height);
            resize.CompareSwap = guides.motion.x;
            resize.Transfer = guides.motion.y;
            resize.MvScaleX = resize.MvScaleY = 1.0f;
            if (shader.DispatchPass(cmdList, resize, depthIn, motionIn, nullptr, nullptr, nullptr, nr.depthSmall,
                                    nr.motionSmall))
            {
                Barrier(cmdList, nr.depthSmall, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                Barrier(cmdList, nr.motionSmall, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                depthIn = nr.depthSmall;
                motionIn = nr.motionSmall;
                matchedGuides = true;
            }
        }
        static bool loggedMatch = false;
        if (!loggedMatch)
        {
            loggedMatch = true;
            if (matchedGuides)
                LOG_INFO("DLSS-NR guides matched to the working size: depth and motion {}x{} for a {}x{} model "
                         "(the frame's guides are {}x{})",
                         workWidth, workHeight, workWidth, workHeight, guides.depth.width, guides.depth.height);
            else
                LOG_WARN("DLSS-NR guides could not be matched to the working size; the model keeps the frame's "
                         "{}x{} guides for its {}x{} colour",
                         guides.depth.width, guides.depth.height, workWidth, workHeight);
        }
    }

    // The game's scale turns its vectors into pixels of the size they are measured in: the render
    // size for low-resolution vectors, the output size otherwise. The model reads the scale it is
    // given in pixels of the motion texture it is handed, so the conversion is "texture the model
    // reads / size the vectors are measured in" -- the DLSS-enlargement path's formula, whose
    // texture is always its own resample. Here the texture is the matched working-size resample
    // when there is one and the game's own region otherwise. Measuring against the working size
    // in both cases (v0.8.92) was right only with matched guides: at 100% the model kept the
    // game's render-size vectors and got a scale for a frame-size texture -- twice too large in
    // a game upscaling 2x with low-resolution vectors (Onimusha: 3840 where 1920 was right), and
    // 100% flickered where it had been steady. Before v0.8.92 the reference was the frame size,
    // which halved every vector below 100% in the same game; RenderMotionScale=false keeps that
    // for an A/B.
    const bool renderMotionScale = cfg.DlssNrRenderMotionScale.value_or_default();
    const unsigned int mvGuideW = !renderMotionScale ? workWidth : matchedGuides ? workWidth : guides.motion.width;
    const unsigned int mvGuideH = !renderMotionScale ? workHeight : matchedGuides ? workHeight : guides.motion.height;
    const unsigned int mvRefW = !renderMotionScale                 ? width
                                : frame.MotionVectorsLowResolution ? frame.RenderSubrectWidth
                                                                   : frame.OutputWidth;
    const unsigned int mvRefH = !renderMotionScale                 ? height
                                : frame.MotionVectorsLowResolution ? frame.RenderSubrectHeight
                                                                   : frame.OutputHeight;
    const float mvToWorkX = mvRefW != 0 ? (float) mvGuideW / (float) mvRefW : 1.0f;
    const float mvToWorkY = mvRefH != 0 ? (float) mvGuideH / (float) mvRefH : 1.0f;
    {
        static unsigned int loggedRefW = 0, loggedGuideW = 0, loggedWorkW = 0;
        if (loggedRefW != mvRefW || loggedGuideW != mvGuideW || loggedWorkW != workWidth)
        {
            loggedRefW = mvRefW;
            loggedGuideW = mvGuideW;
            loggedWorkW = workWidth;
            LOG_INFO("DLSS-NR model motion scale {:.1f} x {:.1f}: game scale {} x {} measured against {}x{} ({}), "
                     "motion texture {}x{} ({}), model {}x{}",
                     frame.MvScaleX * mvToWorkX, frame.MvScaleY * mvToWorkY, frame.MvScaleX, frame.MvScaleY, mvRefW,
                     mvRefH,
                     !renderMotionScale                 ? "frame size, RenderMotionScale=false"
                     : frame.MotionVectorsLowResolution ? "render size, low-resolution vectors"
                                                        : "output size",
                     mvGuideW, mvGuideH, matchedGuides ? "matched to the working size" : "the game's region", workWidth,
                     workHeight);
        }
    }

    bool styleAnalysisModelsReady = false;
    if (styleAnalysisCapture.active && !styleAnalysisCapture.copiesRecorded)
    {
        if (!reduced || spatial || workScale >= 1.0f || nr.colorSmall == nullptr || nr.colorCopy == nullptr)
        {
            LOG_WARN("NR style analysis capture cancelled: requires below-100% ordinary (non-spatial) working mode.");
            ReleaseStyleAnalysisCapture();
        }
        else
        {
            bool allReady = true;
            bool failed = false;
            const auto p50Desc = nr.colorSmall->GetDesc();
            const auto p100Desc = nr.colorCopy->GetDesc();

            for (unsigned style = 0; style < 3; ++style)
            {
                auto ensureOutput = [&](ID3D12Resource*& out, const D3D12_RESOURCE_DESC& desc) -> bool
                {
                    if (!out || out->GetDesc().Width != desc.Width || out->GetDesc().Height != desc.Height ||
                        out->GetDesc().Format != desc.Format)
                    {
                        ParkNrResource(out);
                        out = CreateScratch(device, desc.Format, (unsigned) desc.Width, desc.Height);
                    }
                    return out != nullptr;
                };

                if (!ensureOutput(styleAnalysisCapture.outputs[style], p50Desc) ||
                    !ensureOutput(styleAnalysisCapture.nativeOutputs[style], p100Desc))
                {
                    failed = true;
                    break;
                }

                auto settings = DlssNr::Profiles::BasePassSettings(cfg, 0);
                settings.style = style; // 0 Standard, 1 Natural, 2 Cinematic.

                bool p50Ready = false;
                const auto prepared50 =
                    styleAnalysisCapture.models[style].Prepare(cmdList, device, workWidth, workHeight, settings,
                                                               frame.SubmissionEpoch, &p50Ready);
                bool p100Ready = false;
                const auto prepared100 =
                    styleAnalysisCapture.nativeModels[style].Prepare(cmdList, device, width, height, settings,
                                                                     frame.SubmissionEpoch, &p100Ready);
                if (prepared50 != NVSDK_NGX_Result_Success || prepared100 != NVSDK_NGX_Result_Success)
                {
                    LOG_WARN("NR style analysis capture: style {} feature preparation failed P50=0x{:X}, P100=0x{:X}.",
                             style, prepared50, prepared100);
                    failed = true;
                    break;
                }
                allReady &= p50Ready && p100Ready;
            }

            if (failed)
                ReleaseStyleAnalysisCapture();
            else
            {
                styleAnalysisCapture.modelsPrepared = true;
                styleAnalysisModelsReady = allReady;
                styleAnalysisCapture.status =
                    allReady ? "Ready to capture P50/P100" : "Preparing six diagnostic NR features";
            }
        }
    }
    ngxTime->Start(cmdList);

    // Count only a contiguous set of ready, separate feature histories. A failed extra creation never
    // falls back to reusing the main feature: that tells one temporal model several frames elapsed in
    // one game frame and makes its history fight the later layers.
    unsigned int effectivePasses = 1;
    if (nr.passScratch != nullptr)
    {
        for (unsigned int pass = 1; pass < requestedPasses; ++pass)
        {
            if (!nr.models[pass].Ready(frame.SubmissionEpoch))
                break;
            ++effectivePasses;
        }
    }

    if (loggedConfigured != requestedPasses || loggedEffective != effectivePasses)
    {
        loggedConfigured = requestedPasses;
        loggedEffective = effectivePasses;
        LOG_INFO("DLSS-NR model passes: configured {}, effective {}", requestedPasses, effectivePasses);
    }

    const uint32_t configuredInterPassMode =
        std::min(cfg.DlssNrInterPassReconstruction.value_or_default(), 2u);
    if (!nr.interPassModeInitialized)
    {
        nr.interPassMode = configuredInterPassMode;
        nr.interPassModeInitialized = true;
    }
    else if (nr.interPassMode != configuredInterPassMode)
    {
        // The later temporal features now see a different input distribution. Reset history without
        // recreating NGX features; Off <-> Guided and reference <-> fused both start cleanly.
        nr.interPassMode = configuredInterPassMode;
        nr.reset = true;
    }

    const bool interPassActive =
        configuredInterPassMode != 0u && effectivePasses > 1u && workScale < 0.999f && !spatial;

    // Keep the encoded base immutable; ping-pong model outputs and compose the final delta once.
    // Inter-pass residuals are ALWAYS Ncurrent-originalPassBase, never Ncurrent-previousCorrected.
    ID3D12Resource* const originalPassBase = modelInput;
    ID3D12Resource* passInput = originalPassBase;
    ID3D12Resource* passOutput = nr.output;
    ID3D12Resource* finalAnswer = nullptr;
    bool outputReadable = false;
    bool scratchReadable = false;
    bool clampReadable = false;
    bool interPassWorkingReadable = false;
    bool interPassP100Readable = false;
    bool interPassLowReadable = false;
    bool interPassDebugReadable = false;
    bool clampFailed = false;
    bool interPassFailed = false;
    bool interPassShapingApplied = false;
    uint32_t clampSlots[2] = { UINT32_MAX, UINT32_MAX };

    const auto MakeModelReadable = [&](ID3D12Resource* resource)
    {
        bool& readable = resource == nr.output      ? outputReadable
                         : resource == nr.passClamp ? clampReadable
                                                    : scratchReadable;
        if (!readable)
        {
            Barrier(cmdList, resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            readable = true;
        }
    };

    const auto MakeModelWritable = [&](ID3D12Resource* resource)
    {
        bool& readable = resource == nr.output      ? outputReadable
                         : resource == nr.passClamp ? clampReadable
                                                    : scratchReadable;
        if (readable)
        {
            Barrier(cmdList, resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            readable = false;
        }
    };

    const auto EnsureInterPassScratch =
        [&](ID3D12Resource*& resource, DXGI_FORMAT format, unsigned w, unsigned h, bool& readable)
    {
        const bool matches =
            resource && resource->GetDesc().Format == format &&
            resource->GetDesc().Width == w && resource->GetDesc().Height == h;
        if (!matches)
        {
            ParkNrResource(resource);
            readable = false;
            resource = CreateScratch(device, format, w, h);
        }
        return resource != nullptr;
    };

    const auto MakeInterPassConstants = [&](unsigned int pass, DlssNrMode mode, unsigned outW, unsigned outH)
    {
        DlssNrConstants guided {};
        guided.Mode = mode;
        guided.Width = outW;
        guided.Height = outH;
        guided.ResidualHistoryValid =
            std::clamp(cfg.DlssNrGuidedResidualRadius.value_or_default(), 1u, 3u);
        const float rangeSigma = cfg.DlssNrGuidedResidualRangeSigma.value_or_default();
        guided.ResidualBlend = std::isfinite(rangeSigma) ? rangeSigma : 0.015f;
        const float spatialSigma = cfg.DlssNrGuidedResidualSpatialSigma.value_or_default();
        guided.ResidualScale = std::isfinite(spatialSigma) ? spatialSigma : 1.20f;
        const float guideStrength = cfg.DlssNrGuidedResidualGuideStrength.value_or_default();
        guided.ResidualConfidenceSensitivity = std::isfinite(guideStrength) ? guideStrength : 0.75f;

        const uint32_t shapingMode = std::min(cfg.DlssNrGuidedResidualShaping.value_or_default(), 2u);
        const uint32_t style = PassSettings(cfg, pass).style;
        const auto shaping =
            DlssNr::Profiles::EffectiveGuidedResidualGainsForInterPass(cfg, shapingMode, style, workScale);
        guided.MvScaleX = shaping.high;
        guided.MvScaleY = shaping.low;
        guided.GuideWidth = shaping.active ? 1u : 0u;
        guided.GuideHeight = cfg.DlssNrGuidedResidualShadowGate.value_or_default() ? 1u : 0u;
        // Only inter-pass Mode 27/28 reads these flags. Bit 4 uses cheaper weights
        // and shared P50 Area candidate loads; bit 5 removes an identically-zero
        // low-band contribution, including the small low texture dispatch.
        // Off remains the byte-for-byte original inter-pass shader path.
        if (cfg.DlssNrInterPassExactOptimized.value_or_default())
        {
            guided.DirectResolveFlags = 16u;
            if (shaping.active && shaping.high == shaping.low)
            {
                guided.GuideWidth = 0u;
                guided.DirectResolveFlags |= 32u; // shader still applies high gain
            }
        }

        float shadowLow = cfg.DlssNrGuidedResidualShadowLow.value_or_default();
        float shadowHigh = cfg.DlssNrGuidedResidualShadowHigh.value_or_default();
        float shadowFloor = cfg.DlssNrGuidedResidualShadowFloor.value_or_default();
        if (!std::isfinite(shadowLow))
            shadowLow = 0.02f;
        if (!std::isfinite(shadowHigh))
            shadowHigh = 0.08f;
        if (!std::isfinite(shadowFloor))
            shadowFloor = 0.15f;
        std::memcpy(&guided.ExposureSourceWidth, &shadowLow, sizeof(shadowLow));
        std::memcpy(&guided.ExposureSourceHeight, &shadowHigh, sizeof(shadowHigh));
        std::memcpy(&guided.ExposurePadding, &shadowFloor, sizeof(shadowFloor));
        return guided;
    };

    const auto InterPassProxyFilter = [&]()
    {
        const bool upscaledResidualBase =
            workScale < 1.0f && cfg.DlssNrTransfer.value_or_default() == 6u;
        return std::min(upscaledResidualBase
                            ? cfg.DlssNrUpscaledResidualDownscaleFilter.value_or_default()
                            : cfg.DlssNrProxyDownscaleFilter.value_or_default(),
                        11u);
    };

    const auto InterPassExactScaler = [](uint32_t filter)
    {
        switch (filter)
        {
        case 2u: return Scaler::CatmullRom;
        case 3u: return Scaler::Lanczos2;
        case 5u: return Scaler::FSR1;
        case 6u: return Scaler::Bicubic;
        case 7u: return Scaler::Lanczos3;
        case 8u: return Scaler::Kaiser2;
        case 9u: return Scaler::Kaiser3;
        case 10u: return Scaler::Magic;
        default: return Scaler::Count;
        }
    };

    const auto BuildInterPassReference =
        [&](unsigned int pass, ID3D12Resource* currentAnswer, ID3D12Resource* lowField) -> bool
    {
        if (!nr.colorCopy || !nr.passClamp)
            return false;
        const auto nativeDesc = nr.colorCopy->GetDesc();
        if (!EnsureInterPassScratch(nr.interPassP100, nativeDesc.Format, width, height, interPassP100Readable))
            return false;
        if (interPassP100Readable)
        {
            Barrier(cmdList, nr.interPassP100, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            interPassP100Readable = false;
        }

        auto guided = MakeInterPassConstants(pass, DlssNrMode_InterPassGuidedP100, width, height);
        guided.Transfer = 0u; // signed residual = Ncurrent - immutable Boriginal
        if (!shader.DispatchPassAux2(cmdList, guided, originalPassBase, currentAnswer, nr.colorCopy,
                                     nullptr, nullptr, lowField, nr.interPassP100, nullptr))
            return false;
        Barrier(cmdList, nr.interPassP100, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        interPassP100Readable = true;

        // Ground-truth leg: use the exact filter that built the original reduced proxy. External Output
        // Scaling filters are reused when available; failed external dispatch falls back to Area exactly
        // like EncodeWorkingInput does.
        MakeModelWritable(nr.passClamp);
        const uint32_t proxyFilter = InterPassProxyFilter();
        const Scaler exactScaler = InterPassExactScaler(proxyFilter);
        bool downscaled = false;
        if (exactScaler != Scaler::Count && nr.proxyDown && nr.proxyDownScaler == exactScaler)
            downscaled = nr.proxyDown->DispatchResources(cmdList, nr.interPassP100, nr.passClamp);
        if (!downscaled)
        {
            DlssNrConstants down {};
            down.Mode = DlssNrMode_Downsample;
            down.Width = modelWidth;
            down.Height = modelHeight;
            down.Transfer = exactScaler == Scaler::Count ? proxyFilter : 0u;
            downscaled = shader.DispatchPass(cmdList, down, nr.interPassP100, nullptr, nullptr, nullptr,
                                             nullptr, nr.passClamp, nullptr);
        }
        if (!downscaled)
            return false;
        MakeModelReadable(nr.passClamp);

        const auto workingDesc = originalPassBase->GetDesc();
        if (!EnsureInterPassScratch(nr.interPassWorking, workingDesc.Format, modelWidth, modelHeight,
                                    interPassWorkingReadable))
            return false;
        if (interPassWorkingReadable)
        {
            Barrier(cmdList, nr.interPassWorking, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            interPassWorkingReadable = false;
        }

        DlssNrConstants clamp {};
        clamp.Mode = DlssNrMode_ClampProxy;
        clamp.Width = modelWidth;
        clamp.Height = modelHeight;
        if (!shader.DispatchPass(cmdList, clamp, nr.passClamp, nullptr, nullptr, nullptr, nullptr,
                                 nr.interPassWorking, nullptr))
            return false;
        Barrier(cmdList, nr.interPassWorking, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        interPassWorkingReadable = true;
        return true;
    };

    const auto BuildInterPassInput =
        [&](unsigned int pass, ID3D12Resource* currentAnswer) -> bool
    {
        if (!interPassActive || !nr.colorCopy)
            return false;

        auto guided = MakeInterPassConstants(pass,
                                             configuredInterPassMode == 2u
                                                 ? DlssNrMode_InterPassGuidedWorking
                                                 : DlssNrMode_InterPassGuidedP100,
                                             configuredInterPassMode == 2u ? modelWidth : width,
                                             configuredInterPassMode == 2u ? modelHeight : height);

        ID3D12Resource* lowField = nullptr;
        if (guided.GuideWidth != 0u)
        {
            const unsigned lowW = std::max(1u, (modelWidth + 7u) / 8u);
            const unsigned lowH = std::max(1u, (modelHeight + 7u) / 8u);
            const auto workingDesc = originalPassBase->GetDesc();
            if (!EnsureInterPassScratch(nr.interPassResidualLow, workingDesc.Format, lowW, lowH,
                                        interPassLowReadable))
                return false;
            if (interPassLowReadable)
            {
                Barrier(cmdList, nr.interPassResidualLow, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                interPassLowReadable = false;
            }
            DlssNrConstants low {};
            low.Mode = DlssNrMode_GuidedResidualLow;
            low.Width = lowW;
            low.Height = lowH;
            low.Transfer = 0u; // ALWAYS current cumulative N - immutable original base.
            if (!shader.DispatchPass(cmdList, low, originalPassBase, currentAnswer, nullptr, nullptr, nullptr,
                                     nr.interPassResidualLow, nullptr))
                return false;
            Barrier(cmdList, nr.interPassResidualLow, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            interPassLowReadable = true;
            lowField = nr.interPassResidualLow;
        }

        const uint32_t proxyFilter = InterPassProxyFilter();
        const bool fusedLocalFilter = proxyFilter == 0u || proxyFilter == 1u || proxyFilter == 4u;
        if (configuredInterPassMode == 2u && fusedLocalFilter)
        {
            const auto workingDesc = originalPassBase->GetDesc();
            if (!EnsureInterPassScratch(nr.interPassWorking, workingDesc.Format, modelWidth, modelHeight,
                                        interPassWorkingReadable))
                return false;
            if (interPassWorkingReadable)
            {
                Barrier(cmdList, nr.interPassWorking, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                interPassWorkingReadable = false;
            }
            guided.Mode = DlssNrMode_InterPassGuidedWorking;
            guided.Width = modelWidth;
            guided.Height = modelHeight;
            guided.Transfer = proxyFilter;
            if (!shader.DispatchPassAux2(cmdList, guided, originalPassBase, currentAnswer, nr.colorCopy,
                                         nullptr, nullptr, lowField, nr.interPassWorking, nullptr))
                return false;
            Barrier(cmdList, nr.interPassWorking, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            interPassWorkingReadable = true;
            return true;
        }

        if (configuredInterPassMode == 2u && !nr.interPassFusedReferenceWarned)
        {
            nr.interPassFusedReferenceWarned = true;
            LOG_INFO("DLSS-NR inter-pass fused: filter {} is external/non-local; using exact reference reconstruction "
                     "for this filter instead of approximating its kernel.", proxyFilter);
        }
        return BuildInterPassReference(pass, currentAnswer, lowField);
    };

    int result = NVSDK_NGX_Result_Success;
    const bool enlargementReset = nr.reset;
    bool compositionSucceeded = false;

    DlssNr::Proxy::Frame modelFrame {};
    modelFrame.depth = depthIn;
    modelFrame.motion = motionIn;
    modelFrame.size = { modelWidth, modelHeight };
    modelFrame.guides = (spatial || matchedGuides)
                            ? DlssNr::GuideRegions { { 0, 0, modelWidth, modelHeight }, { 0, 0, modelWidth, modelHeight } }
                            : guides;
    modelFrame.depthInverted = frame.DepthInverted;
    modelFrame.reset = nr.reset;
    modelFrame.mvScaleX = spatial ? 1.0f : frame.MvScaleX * mvToWorkX;
    modelFrame.mvScaleY = spatial ? 1.0f : frame.MvScaleY * mvToWorkY;

    for (unsigned int pass = 0; pass < effectivePasses && result == NVSDK_NGX_Result_Success; ++pass)
    {
        MakeModelWritable(passOutput);
        bool evaluated = false;
        modelFrame.color = passInput;
        modelFrame.output = passOutput;
        result = static_cast<int>(nr.models[pass].Run(cmdList, device, modelFrame, PassSettings(cfg, pass),
                                                      frame.SubmissionEpoch, &evaluated));
        modelRunning = evaluated && result == NVSDK_NGX_Result_Success;
        if (!evaluated || result != NVSDK_NGX_Result_Success)
            break;

        finalAnswer = passOutput;
        MakeModelReadable(finalAnswer);

        if (pass + 1 < effectivePasses)
        {
            bool reconstructed = false;
            if (interPassActive)
                reconstructed = BuildInterPassInput(pass, finalAnswer);

            if (reconstructed)
            {
                interPassShapingApplied = true;
                passInput = nr.interPassWorking;

                // Debug view 7 must remain the exact input of pass 2 even when a later transition rewrites
                // interPassWorking for pass 3. Pay for this copy only while that debug view is selected.
                if (pass == 0u && cfg.DlssNrDebugView.value_or_default() == 7u)
                {
                    const auto workingDesc = originalPassBase->GetDesc();
                    if (EnsureInterPassScratch(nr.interPassDebug, workingDesc.Format, modelWidth, modelHeight,
                                               interPassDebugReadable))
                    {
                        if (interPassDebugReadable)
                        {
                            Barrier(cmdList, nr.interPassDebug, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                            interPassDebugReadable = false;
                        }
                        DlssNrConstants copy {};
                        copy.Mode = DlssNrMode_TemporalCarrierDebugCopy;
                        copy.Width = modelWidth;
                        copy.Height = modelHeight;
                        if (shader.DispatchPass(cmdList, copy, nr.interPassWorking, nullptr, nullptr, nullptr, nullptr,
                                                nr.interPassDebug, nullptr))
                        {
                            Barrier(cmdList, nr.interPassDebug, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                            interPassDebugReadable = true;
                        }
                    }
                }
            }
            else
            {
                if (interPassActive)
                {
                    interPassFailed = true;
                    modelFrame.reset = true; // later histories in this same frame must not consume stale history.
                    if (!nr.interPassWarned)
                    {
                        nr.interPassWarned = true;
                        LOG_WARN("DLSS-NR inter-pass reconstruction failed; falling back to ClampProxy.");
                    }
                }

                // Original behaviour and mandatory failure fallback.
                MakeModelWritable(nr.passClamp);
                DlssNrConstants clamp {};
                clamp.Mode = DlssNrMode_ClampProxy;
                clamp.Width = modelWidth;
                clamp.Height = modelHeight;
                if (!shader.DispatchPass(cmdList, clamp, finalAnswer, nullptr, nullptr, nullptr, nullptr, nr.passClamp,
                                         nullptr, &clampSlots[pass % 2]))
                {
                    // Keep this frame's last valid answer; later histories skipped a frame.
                    clampFailed = true;
                    effectivePasses = pass + 1;
                    break;
                }
                MakeModelReadable(nr.passClamp);
                passInput = nr.passClamp;
            }
            passOutput = passOutput == nr.output ? nr.passScratch : nr.output;
        }
    }

    ngxTime->End(cmdList);

    if (styleAnalysisCapture.active && !styleAnalysisCapture.copiesRecorded && styleAnalysisModelsReady)
    {
        static constexpr const char* kP50StyleNames[3] = {
            "standard_nr50", "natural_nr50", "cinematic_nr50"
        };
        static constexpr const char* kP100StyleNames[3] = {
            "standard_nr100", "natural_nr100", "cinematic_nr100"
        };

        // Reduced diagnostics: exact sharp reduced proxy before any Detail Quality Lab input preparation.
        DlssNr::Proxy::Frame diagnostic50 = modelFrame;
        diagnostic50.color = nr.colorSmall;
        diagnostic50.reset = true;

        // Native diagnostics: true model-at-P100 ground truth, not any enlargement of NR50.
        DlssNr::Proxy::Frame diagnostic100 {};
        diagnostic100.color = nr.colorCopy;
        diagnostic100.depth = originalDepthIn;
        diagnostic100.motion = originalMotionIn;
        diagnostic100.size = { width, height };
        diagnostic100.guides = guides;
        diagnostic100.depthInverted = frame.DepthInverted;
        diagnostic100.reset = true;

        const unsigned nativeMvGuideW = !renderMotionScale ? width : guides.motion.width;
        const unsigned nativeMvGuideH = !renderMotionScale ? height : guides.motion.height;
        diagnostic100.mvScaleX =
            frame.MvScaleX * (mvRefW != 0 ? (float) nativeMvGuideW / (float) mvRefW : 1.0f);
        diagnostic100.mvScaleY =
            frame.MvScaleY * (mvRefH != 0 ? (float) nativeMvGuideH / (float) mvRefH : 1.0f);

        bool allEvaluated = true;
        for (unsigned style = 0; style < 3; ++style)
        {
            auto settings = DlssNr::Profiles::BasePassSettings(cfg, 0);
            settings.style = style;

            diagnostic50.output = styleAnalysisCapture.outputs[style];
            bool evaluated50 = false;
            const auto result50 =
                styleAnalysisCapture.models[style].Run(cmdList, device, diagnostic50, settings,
                                                       frame.SubmissionEpoch, &evaluated50);

            diagnostic100.output = styleAnalysisCapture.nativeOutputs[style];
            bool evaluated100 = false;
            const auto result100 =
                styleAnalysisCapture.nativeModels[style].Run(cmdList, device, diagnostic100, settings,
                                                             frame.SubmissionEpoch, &evaluated100);

            if (result50 != NVSDK_NGX_Result_Success || !evaluated50 ||
                result100 != NVSDK_NGX_Result_Success || !evaluated100)
            {
                LOG_WARN("NR style analysis capture: style {} evaluation failed/not-ready "
                         "(P50=0x{:X}/{}, P100=0x{:X}/{}).",
                         style, result50, evaluated50, result100, evaluated100);
                allEvaluated = false;
                break;
            }
        }

        if (allEvaluated)
        {
            SYSTEMTIME time {};
            GetLocalTime(&time);
            char folder[96];
            std::snprintf(folder, sizeof(folder), "%04u%02u%02u-%02u%02u%02u-%03u",
                          time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
                          time.wSecond, time.wMilliseconds);

            auto& capture = styleAnalysisCapture.readback;
            capture.Reset();
            capture.directory = Util::DllPath().parent_path() / "nr-style-analysis-captures" / folder;
            capture.metadata
                << "purpose compare_true_NR50_and_NR100_styles_for_scaling_analysis\n"
                << "same_game_frame 1\n"
                << "nr50_input sharp_reduced_proxy_before_detail_quality_input_preparation\n"
                << "nr100_input native_proxy_no_enlargement\n"
                << "working_scale " << workScale << "\n"
                << "p100_width " << width << " p100_height " << height << "\n"
                << "p50_width " << workWidth << " p50_height " << workHeight << "\n"
                << "proxy_downscale_filter " << cfg.DlssNrProxyDownscaleFilter.value_or_default() << "\n"
                << "match_guides " << cfg.DlssNrMatchGuides.value_or_default() << "\n"
                << "render_motion_scale " << renderMotionScale << "\n"
                << "nr50_mv_scale " << diagnostic50.mvScaleX << " " << diagnostic50.mvScaleY << "\n"
                << "nr100_mv_scale " << diagnostic100.mvScaleX << " " << diagnostic100.mvScaleY << "\n"
                << "styles standard=0 natural=1 cinematic=2\n";

            lifetime.Record(cmdList);
            bool copied = true;
            copied &= capture.Add(cmdList, device, "proxy_p100", nr.colorCopy,
                                  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            copied &= capture.Add(cmdList, device, "proxy_p50", nr.colorSmall,
                                  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            for (unsigned style = 0; style < 3; ++style)
            {
                copied &= capture.Add(cmdList, device, kP50StyleNames[style], styleAnalysisCapture.outputs[style],
                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                copied &= capture.Add(cmdList, device, kP100StyleNames[style],
                                      styleAnalysisCapture.nativeOutputs[style],
                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }

            if (copied)
            {
                capture.complete = lifetime.CompletionProbe(cmdList);
                capture.recorded = true;
                styleAnalysisCapture.copiesRecorded = true;
                styleAnalysisCapture.status = "GPU readback pending";
                LOG_INFO("NR P50/P100 style analysis capture recorded; waiting for GPU completion: {}",
                         capture.directory.string());
            }
            else
            {
                LOG_WARN("NR P50/P100 style analysis capture failed to allocate/record readbacks.");
                ReleaseStyleAnalysisCapture();
            }
        }
    }
    nr.reset = clampFailed || interPassFailed || finalAnswer == nullptr;
    bool spatialUnpacked = false;
    ID3D12Resource* ordinaryProxy = modelInput;
    ID3D12Resource* ordinaryAnswer = finalAnswer;
    if (spatial && result == NVSDK_NGX_Result_Success && finalAnswer)
    {
        const auto unpackConstants =
            DlssNr::Spatial::MakeConstants(spatialLayout, 102, guides, frame.MvScaleX, frame.MvScaleY, width, height);
        spatialUnpacked = shader.DispatchSpatial(cmdList, unpackConstants, modelInput, finalAnswer, nullptr,
                                                 nr.spatialProxy, nr.spatialAnswer);
        if (spatialUnpacked)
        {
            for (auto* unpacked : { nr.spatialProxy, nr.spatialAnswer })
                Barrier(cmdList, unpacked, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            ordinaryProxy = nr.spatialProxy;
            ordinaryAnswer = nr.spatialAnswer;
        }
        else
        {
            nr.spatialFallback = true;
            nr.spatialFallbackReason = "a spatial unpacking dispatch failed";
            nr.reset = true;
            modelRunning = false;
            ReportSkipOnce(nr.spatialFallbackReason);
            finalAnswer = nullptr;
        }
    }

    // The model pair and the reconstruction pair may intentionally use different reduced inputs.
    // Spatial compression owns its own unpacked proxy, so experiments fall back to that common source there.
    ID3D12Resource* ordinaryReference =
        (!spatial && encoded.referenceInput) ? encoded.referenceInput : ordinaryProxy;
    if (!spatial && cancelInputPreparationFootprint && nr.colorSmall)
    {
        // After rebasing, the logical reduced pair is S -> C, where C = S + processed(N-B).
        // Every later P50-derived reference must therefore reconstruct S. Using B here would either
        // reintroduce B-S (Upscaled NR residual / Direct detail recovery) or measure structure gain
        // against the wrong baseline (Direct structure transfer).
        ordinaryReference = nr.colorSmall;
    }

    // Detail-quality lab post-NR artifact control. The sequence inside this ONE low-resolution dispatch is:
    // raw N-B -> optional NR50 edge treatment -> optional ghost-band suppression -> optional Ghost Guard
    // -> optional rebase onto sharp S. Ghost/cancellation work is skipped unless Mode 16 actually produced B.
    const uint32_t nrEdgeFilter = std::min(cfg.DlssNrExperimentNrEdgeFilter.value_or_default(), 4u);
    const float nrStrengthRaw = cfg.DlssNrExperimentNrEdgeStrength.value_or_default();
    const float nrStrength = std::isfinite(nrStrengthRaw) ? nrStrengthRaw : 1.0f;
    const uint32_t ghostGuard = std::min(cfg.DlssNrExperimentGhostGuard.value_or_default(), 3u);
    const float ghostStrengthRaw = cfg.DlssNrExperimentGhostGuardStrength.value_or_default();
    const float ghostStrength = std::isfinite(ghostStrengthRaw) ? ghostStrengthRaw : 1.0f;
    const float ghostMaxRaw = cfg.DlssNrExperimentGhostMaxSuppression.value_or_default();
    const float ghostMax = std::isfinite(ghostMaxRaw) ? ghostMaxRaw : 1.0f;
    const uint32_t ghostBands = std::min(cfg.DlssNrExperimentGhostBandSuppression.value_or_default(), 2u);
    const float bandStrengthRaw = cfg.DlssNrExperimentBandSuppressionStrength.value_or_default();
    const float bandStrength = std::isfinite(bandStrengthRaw) ? bandStrengthRaw : 1.0f;

    const bool nrFilterActive = nrEdgeFilter != 0u && nrStrength != 0.0f;
    const bool ghostGuardActive =
        encoded.inputPreparationActive &&
        ghostGuard != 0u && ghostStrength != 0.0f && ghostMax != 0.0f;
    const bool ghostBandsActive =
        encoded.inputPreparationActive &&
        ghostBands != 0u && bandStrength != 0.0f;
    const bool preparationRebaseActive =
        encoded.inputPreparationActive && cancelInputPreparationFootprint;
    const bool artifactControlActive =
        nrFilterActive || ghostGuardActive || ghostBandsActive || preparationRebaseActive;

    if (!spatial && workScale < 1.0f && ordinaryAnswer && nr.colorSmall &&
        (transferMode == 5u || transferMode == 6u) && artifactControlActive)
    {
        const auto answerDesc = ordinaryAnswer->GetDesc();
        const bool filteredMatches =
            nr.outputFiltered && nr.outputFiltered->GetDesc().Width == answerDesc.Width &&
            nr.outputFiltered->GetDesc().Height == answerDesc.Height &&
            nr.outputFiltered->GetDesc().Format == answerDesc.Format;
        if (!filteredMatches)
        {
            ParkNrResource(nr.outputFiltered);
            nr.outputFilteredReadable = false;
            nr.outputFiltered =
                CreateScratch(device, answerDesc.Format, (unsigned) answerDesc.Width, answerDesc.Height);
        }

        if (nr.outputFiltered)
        {
            if (nr.outputFilteredReadable)
            {
                Barrier(cmdList, nr.outputFiltered, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                nr.outputFilteredReadable = false;
            }

            DlssNrConstants filter {};
            filter.Mode = DlssNrMode_ExperimentNrPostfilter;
            filter.Width = modelWidth;
            filter.Height = modelHeight;

            // Mode 17 mode-local mapping (all numeric settings pass through verbatim after finite checks):
            // Transfer/TransferStrength/ColourStrength/DebugScale = NR edge mode/radius/strength/threshold
            // CompareMode/DebugView/CompareSwap = Ghost Guard mode / band mode / cancel B-S
            // MaxRatio = Ghost Guard strength
            // SkinDetail/SkinColour/EnvironmentDetail/EnvironmentColour =
            //   detection threshold / sharp-edge threshold / edge-band radius / max suppression
            // ReplaceDetailStrength/ModelWorkScale/ResidualConfidenceSensitivity =
            //   band strength / low radius / mid radius
            filter.Transfer = nrFilterActive ? nrEdgeFilter : 0u;
            const float nrRadius = cfg.DlssNrExperimentNrEdgeRadius.value_or_default();
            const float edgeThreshold = cfg.DlssNrExperimentEdgeThreshold.value_or_default();
            filter.TransferStrength = std::isfinite(nrRadius) ? nrRadius : 0.75f;
            filter.ColourStrength = nrFilterActive ? nrStrength : 0.0f;
            filter.DebugScale = std::isfinite(edgeThreshold) ? edgeThreshold : 0.04f;

            filter.CompareMode = ghostGuardActive ? ghostGuard : 0u;
            filter.DebugView = ghostBandsActive ? ghostBands : 0u;
            filter.CompareSwap = preparationRebaseActive ? 1u : 0u;
            filter.MaxRatio = ghostStrength;
            const float ghostDetection = cfg.DlssNrExperimentGhostDetectionThreshold.value_or_default();
            const float ghostEdge = cfg.DlssNrExperimentGhostEdgeThreshold.value_or_default();
            const float ghostRadius = cfg.DlssNrExperimentGhostBandRadius.value_or_default();
            filter.SkinDetail = std::isfinite(ghostDetection) ? ghostDetection : 0.01f;
            filter.SkinColour = std::isfinite(ghostEdge) ? ghostEdge : 0.04f;
            filter.EnvironmentDetail = std::isfinite(ghostRadius) ? ghostRadius : 2.0f;
            filter.EnvironmentColour = ghostMax;
            filter.ReplaceDetailStrength = bandStrength;
            const float lowRadius = cfg.DlssNrExperimentLowBandRadius.value_or_default();
            const float midRadius = cfg.DlssNrExperimentMidBandRadius.value_or_default();
            filter.ModelWorkScale = std::isfinite(lowRadius) ? lowRadius : 3.0f;
            filter.ResidualConfidenceSensitivity = std::isfinite(midRadius) ? midRadius : 1.5f;

            // t0=N (NR50), t1=B (exact input shown to NR), t2=S (sharp P50 before Mode 16).
            if (shader.DispatchPass(cmdList, filter, ordinaryAnswer, ordinaryProxy, nr.colorSmall, nullptr, nullptr,
                                    nr.outputFiltered, nullptr))
            {
                Barrier(cmdList, nr.outputFiltered, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                nr.outputFilteredReadable = true;
                ordinaryAnswer = nr.outputFiltered;
            }
        }
    }

    // Supersampling probe: report the model working ABOVE native so a test log tells us whether NGX even
    // accepts a super-native evaluate and what it returns. Once per working-size change, or on any error.
    if (modelWidth > width || modelHeight > height)
    {

        if (lastSuper != modelWidth || result != 1)
        {
            lastSuper = modelWidth;
            LOG_INFO("DLSS-NR SUPERSAMPLE: model at {}x{} = {:.2f}x native {}x{}, evaluate result {} ({})", modelWidth,
                     modelHeight, (float) modelWidth / (float) width, width, height, result,
                     NgxResultName((unsigned int) result));
        }
    }

    if (result == NVSDK_NGX_Result_Success && finalAnswer != nullptr)
    {
        // Resolve takes the difference between what the model returned and what it was shown, and adds
        // that back to the frame. At strength zero the result is what the upscaler produced, exactly, and
        // anything the model left alone is untouched rather than round-tripped through the curve.
        auto resolveParams = MakeResolveConstants(encoded, effectivePasses, interPassShapingApplied);

        const float finalChromaticityRecovery =
            std::clamp(cfg.DlssNrFinalChromaticityRecovery.value_or_default(), 0.0f, 100.0f);
        const float finalSaturationRecovery =
            std::clamp(cfg.DlssNrFinalSaturationRecovery.value_or_default(), 0.0f, 100.0f);
        const bool finalColourRequested = finalChromaticityRecovery > 0.0f || finalSaturationRecovery > 0.0f;
        const bool finalColourViewCompatible = resolveParams.CompareMode == 0u && resolveParams.DebugView == 0u &&
                                               resolveParams.ShowSkinMask == 0u && resolveParams.ApplyModel != 0u;
        const bool finalColourEnabled = finalColourRequested && finalColourViewCompatible;
        if (!finalColourEnabled)
            nr.finalColorHistoryValid = false;

        // For spatial supersampling, both halves of the pair use the same filter. A failed paired
        // downsample skips composition this frame so a mismatched proxy cannot create an edit.
        bool superDownOk = false;
        bool spatialDownFailed = false;
        if (spatial && workScale > 1.0f)
        {
            const Scaler scaler = cfg.DlssNrScalingDownscaler.value_or_default();
            if (nr.nrScaler != scaler)
            {
                ReleaseSupersamplers();
                nr.nrScaler = scaler;
            }
            if (nr.superDown == nullptr)
                nr.superDown = new OS_Dx12("DLSS-NR supersample down", device, false, scaler);
            if (nr.spatialProxyDown == nullptr)
                nr.spatialProxyDown = new OS_Dx12("DLSS-NR spatial proxy down", device, false, scaler);
        }
        if (spatial && workScale > 1.0f)
        {
            const bool proxyDown =
                nr.superDown && nr.spatialProxyDown && nr.spatialProxyNative && nr.spatialAnswerNative &&
                nr.spatialProxyDown->DispatchResources(cmdList, ordinaryProxy, nr.spatialProxyNative);
            const bool answerDown =
                proxyDown && nr.superDown->DispatchResources(cmdList, ordinaryAnswer, nr.spatialAnswerNative);
            if (answerDown)
            {
                for (auto* nativePair : { nr.spatialProxyNative, nr.spatialAnswerNative })
                    Barrier(cmdList, nativePair, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                superDownOk = true;
            }
            else
            {
                spatialDownFailed = true;
                nr.spatialFallback = true;
                nr.spatialFallbackReason = "a spatial supersample downsampling dispatch failed";
                nr.reset = true;
                modelRunning = false;
                ReportSkipOnce(nr.spatialFallbackReason);
            }
        }
        else if (!spatial && workScale > 1.0f && nr.superDown != nullptr && nr.outputNative != nullptr &&
                 nr.superDown->DispatchResources(cmdList, ordinaryAnswer, nr.outputNative))
        {
            Barrier(cmdList, nr.outputNative, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            superDownOk = true;
        }

        ID3D12Resource* resolveProxy = superDownOk ? (spatial ? nr.spatialProxyNative : nr.colorCopy) : ordinaryProxy;
        ID3D12Resource* resolveAnswer =
            superDownOk ? (spatial ? nr.spatialAnswerNative : nr.outputNative) : ordinaryAnswer;
        bool enlargementReady = !spatialDownFailed;
        const auto transfer = cfg.DlssNrTransfer.value_or_default();
        bool resizeFieldReadable = false;
        ID3D12Resource* directDetailInfo = nullptr;
        ID3D12Resource* directDetailReference = nullptr;
        ID3D12Resource* upscaledResidualReference = nullptr;
        ID3D12Resource* upscaledResidualAnswer = nullptr;
        if (transfer != 7u && resolveParams.DebugView != 4 && !spatialDownFailed &&
            DlssNrUsesDlssEnlargement(transfer) && reduced && (transfer == 2 || workScale < 1.0f))
        {
            // With input-footprint cancellation, Direct NR's logical small-raster pair is S -> corrected NR.
            // Feed S to Direct's NR-gated detail mask too; the separately selected referenceProxy remains unchanged
            // for full-resolution detail recovery / structure-transfer experiments.
            ID3D12Resource* const enlargementProxy =
                transfer == 5u && cancelInputPreparationFootprint && nr.colorSmall
                    ? nr.colorSmall
                    : ordinaryProxy;
            auto* enlarged =
                EnlargeMatchedResidual(cmdList, device, enlargementProxy, ordinaryReference, ordinaryAnswer,
                                       originalDepthIn, originalMotionIn, frame, resolveParams, transfer, enlargementReset,
                                       timingQueue, asyncExternalDetail);
            enlargementReady = enlarged != nullptr;
            if (enlarged)
            {
                resolveAnswer = enlarged;
                // Direct NR pairs the immutable P100 proxy with either a materialized NR100
                // (private DLSS / legacy spatial fallback) or, for the exact embedded spatial kernels,
                // the original NR50 sampled and composed in this same resolve dispatch.
                if (transfer == 5)
                {
                    resolveProxy = nr.colorCopy;

                    const uint32_t selectedOutputUpscaler =
                        std::min(cfg.DlssNrDirectOutputUpscaler.value_or_default(), 10u);
                    const bool fusedDirect =
                        DirectAnswerCanFeedUpscaler(ordinaryAnswer) &&
                        DirectFusedResolveUpscaler(selectedOutputUpscaler) && enlarged == ordinaryAnswer;
                    resolveParams.Transfer = fusedDirect ? 7u : 0u;
                    resolveParams.DirectResolveUpscaler = selectedOutputUpscaler;
                    resolveParams.DirectResolveFlags |= 1u; // final resolve is the Direct NR family

                    const uint32_t detailMode =
                        std::min(cfg.DlssNrDirectDetailRecovery.value_or_default(), 2u);
                    const bool finalExperimentNeedsReference =
                        resolveParams.ResidualMotionBaseX != 0u;
                    ID3D12Resource* selectedDetailReference =
                        asyncExternalDetail && asyncSlot && asyncSlot->asyncDetailReferenceReadable
                            ? asyncSlot->asyncDetailReference.Get()
                            : enlarger && enlarger->detailReference && enlarger->detailReferenceReadable
                                  ? enlarger->detailReference.Get()
                                  : nullptr;
                    const bool detailMaskReady =
                        detailMode == 1 ||
                        (enlarger && enlarger->detailInfo && enlarger->detailReadable);

                    // Aux2 is also the P50 reconstruction used by the final P100-guided experiments.
                    // Bind it independently of Direct detail recovery so the experiments can be tested alone.
                    if (selectedDetailReference && (detailMode != 0 || finalExperimentNeedsReference))
                        directDetailReference = selectedDetailReference;
                    else if (finalExperimentNeedsReference)
                    {
                        // Never let a failed/lazy reconstruction turn the experiment into an unbound Aux2 read.
                        // The frame falls back to ordinary Direct NR and the next frame can retry normally.
                        resolveParams.ResidualHistoryValid = 0u;
                        resolveParams.ResidualMotionBaseX = 0u;
                    }

                    // Limiter-only Direct mode is intentionally reference-free. Bind the existing P100
                    // proxy to Aux2 as a harmless valid alias too, so no compiler resource-selection strategy can
                    // turn a logically dead Aux2 branch into an unbound descriptor read.
                    if (resolveParams.ResidualHistoryValid != 0u &&
                        resolveParams.ResidualMotionBaseX == 0u && !directDetailReference)
                        directDetailReference = resolveProxy;

                    if (detailMode != 0 && detailMaskReady && selectedDetailReference)
                    {
                        // Full-lost-detail intentionally binds no detailInfo; the resolve never samples t4.
                        // NR-gated mode binds the alpha-only mask.
                        directDetailInfo =
                            detailMode == 2 && enlarger ? enlarger->detailInfo.Get() : nullptr;
                        resolveParams.DirectDetailMode = detailMode;
                        resolveParams.DirectDetailMaskStrength =
                            std::clamp(cfg.DlssNrDirectDetailMaskStrength.value_or_default() / 100.0f, 0.0f, 1.0f);
                    }
                }
                else if (transfer == 6)
                {
                    // Keep the two P100 branches separate until the queue join. P50 -> P100 may be
                    // running concurrently on the async compute queue, while NR50 -> NR100 stays on
                    // the NR/direct path (or its own private DLSS feature). Their difference is formed
                    // only after finishAsyncNr() has joined those producers.
                    upscaledResidualReference =
                        asyncExternalDetail && asyncSlot && asyncSlot->asyncDetailReferenceReadable
                            ? asyncSlot->asyncDetailReference.Get()
                            : enlarger && enlarger->detailReference && enlarger->detailReferenceReadable
                                  ? enlarger->detailReference.Get()
                                  : nullptr;
                    upscaledResidualAnswer = enlarged;
                }
                else
                {
                    resolveParams.Transfer = transfer;
                }
            }
            if (enlarged && transfer == 4)
            {
                // Retain the spatial field for the inverse-HDR range guard.
                resolveProxy = enlarger->input.Get();
                Barrier(cmdList, resolveProxy, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                resizeFieldReadable = true;
            }
            if (enlarged && transfer != 6 &&
                (resolveParams.DebugView == 2 || (transfer == 4 && resolveParams.DebugView == 1)))
            {
                resolveProxy = ordinaryProxy;
                resolveAnswer = ordinaryAnswer;
                resolveParams.Transfer = DlssNrSpatialTransfer(transfer); // Inspect the actual model pair.
            }
        }
        else if (!reduced || workScale >= 1.0f ||
                 (!DlssNrUsesDlssEnlargement(transfer) && transfer != 8u && transfer != 9u && transfer != 10u))
        {
            // Below native, temporal modes 8/9/10 keep their private history alive frame-to-frame.
            // At native scale no private enlargement/DLAA work is needed, so release those NGX
            // resources instead of reserving VRAM for an idle feature.
            ReleaseEnlarger();
            enlargementStatus.clear();
        }

        // P100-guided residual family. Transfer 7 uses NR50 directly. Transfer 8 temporalizes NR50;
        // 9 temporalizes a residual carrier; 10 temporalizes Proxy50+K*E50. Modes 9/10 are decoded
        // back to one signed E50 texture before this point. DLSS never performs the P50->P100 resize.
        const bool guidedFamily = transfer >= 7u && transfer <= 10u;
        if (guidedFamily && !spatialDownFailed && reduced && workScale < 1.0f)
        {
            if (!spatial && ordinaryProxy && ordinaryAnswer)
            {
                resolveProxy = ordinaryProxy;
                resolveAnswer = ordinaryAnswer;
                resolveParams.Transfer = 8u;
                enlargementReady = true;

                if (transfer == 8u || transfer == 9u || transfer == 10u)
                {
                    auto* temporal =
                        EnlargeMatchedResidual(cmdList, device, ordinaryProxy, ordinaryReference, ordinaryAnswer,
                                               originalDepthIn, originalMotionIn, frame, resolveParams, transfer,
                                               enlargementReset, timingQueue, false);
                    enlargementReady = temporal != nullptr;
                    if (temporal)
                    {
                        resolveAnswer = temporal;
                        // Internal Transfer 9 means Model is an already-decoded signed residual.
                        // Both residual-carrier variants use that common representation.
                        resolveParams.Transfer = transfer >= 9u ? 9u : 8u;

                        // Debug view 5 needs the exact pre-DLAA carrier. EnlargeMatchedResidual snapshots
                        // it only while that view is active, so normal gameplay pays no full-P50 debug copy.
                        if (resolveParams.DebugView == 5u && transfer >= 9u && enlarger &&
                            enlarger->carrierDebug && enlarger->carrierDebugReadable)
                        {
                            directDetailInfo = enlarger->carrierDebug.Get(); // t4 / gPrevEdit
                            resolveParams.DirectResolveFlags |= 4u;          // valid temporal-carrier debug binding
                            const bool customCarrierDebug =
                                std::abs(enlarger->carrierRangeLow) > 1.0e-6f ||
                                std::abs(enlarger->carrierRangeHigh - 1.0f) > 1.0e-6f;
                            if (customCarrierDebug)
                            {
                                resolveParams.DirectResolveFlags |= 8u; // normalize the active custom range for display
                                static_assert(sizeof(enlarger->carrierRangeLow) ==
                                              sizeof(resolveParams.ResidualMotionBaseX));
                                std::memcpy(&resolveParams.ResidualMotionBaseX, &enlarger->carrierRangeLow,
                                            sizeof(enlarger->carrierRangeLow));
                                std::memcpy(&resolveParams.ResidualMotionBaseY, &enlarger->carrierRangeHigh,
                                            sizeof(enlarger->carrierRangeHigh));
                            }
                        }
                    }
                }

                // Capture-derived frequency shaping. Generate only the low residual field (about mip3);
                // final resolve already owns P50 plus the current guided-family answer and native P100,
                // so no full-resolution intermediate is introduced.
                if (enlargementReady && resolveParams.GuideWidth != 0u)
                {
                    const unsigned lowW = std::max(1u, (workWidth + 7u) / 8u);
                    const unsigned lowH = std::max(1u, (workHeight + 7u) / 8u);
                    const auto pairDesc = resolveAnswer->GetDesc();
                    const bool lowMatches =
                        nr.guidedResidualLow &&
                        nr.guidedResidualLow->GetDesc().Width == lowW &&
                        nr.guidedResidualLow->GetDesc().Height == lowH &&
                        nr.guidedResidualLow->GetDesc().Format == pairDesc.Format;
                    if (!lowMatches)
                    {
                        ParkNrResource(nr.guidedResidualLow);
                        nr.guidedResidualLowReadable = false;
                        nr.guidedResidualLow = CreateScratch(device, pairDesc.Format, lowW, lowH);
                    }

                    bool lowReady = nr.guidedResidualLow != nullptr;
                    if (lowReady)
                    {
                        if (nr.guidedResidualLowReadable)
                        {
                            Barrier(cmdList, nr.guidedResidualLow,
                                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                            nr.guidedResidualLowReadable = false;
                        }

                        DlssNrConstants low {};
                        low.Mode = DlssNrMode_GuidedResidualLow;
                        low.Width = lowW;
                        low.Height = lowH;
                        low.Passthrough = resolveParams.Passthrough;
                        // Modes 9/10 already provide a decoded signed residual texture.
                        low.Transfer = transfer >= 9u ? 1u : 0u;
                        lowReady = shader.DispatchPass(cmdList, low, resolveProxy, resolveAnswer,
                                                       nullptr, nullptr, nullptr,
                                                       nr.guidedResidualLow, nullptr);
                    }

                    if (lowReady)
                    {
                        Barrier(cmdList, nr.guidedResidualLow,
                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                        nr.guidedResidualLowReadable = true;
                        directDetailReference = nr.guidedResidualLow; // Aux2 in final resolve.
                    }
                    else
                    {
                        // The guided geometry still works; only the optional frequency shaping is
                        // disabled for this frame rather than failing composition.
                        resolveParams.GuideWidth = 0u;
                        enlargementStatus =
                            "P100-guided residual: low-frequency shaping scratch failed; guided-only fallback.";
                    }
                }
            }
            else
            {
                enlargementReady = false;
                enlargementStatus =
                    transfer == 7u
                        ? "P100-guided residual requires below-100% ordinary non-spatial NR."
                        : "Temporal DLAA + P100-guided requires below-100% post-upscale ordinary DX12 NR.";
            }
        }

        // Reuse proxy display with the immutable input actually passed to NR, before unpacking.
        if (resolveParams.DebugView == 4)
        {
            resolveProxy = modelInput;
            resolveParams.DebugView = 1;
        }
        else if (resolveParams.DebugView == 7)
        {
            // The snapshot is taken immediately after pass1->pass2 correction, before pass2 can overwrite
            // the shared inter-pass working scratch for a possible pass3.
            resolveProxy = interPassDebugReadable && nr.interPassDebug ? nr.interPassDebug : modelInput;
            resolveParams.DebugView = 1;
        }

        const bool asyncSubmissionReady = finishAsyncNr();

        // Upscaled NR residual: both P100 branches are complete after the async join. Feed them
        // directly to the final resolve so there is no intermediate full-resolution residual carrier pass.
        if (transfer == 6 && enlargementReady && asyncSubmissionReady)
        {
            const bool residualReady = upscaledResidualReference && upscaledResidualAnswer;
            if (residualReady)
            {
                resolveProxy = upscaledResidualReference; // reconstructed P100
                resolveAnswer = upscaledResidualAnswer;   // NR100
                resolveParams.Transfer = 6;               // direct full-resolution residual
            }

            enlargementReady = residualReady;
        }

        // Resolve pre-SR inputs without UAV support through an owned scratch and copy-back. With final colour
        // matching active, resolve into a dedicated full-size scratch first so the statistics compare the untouched
        // pre-NR frame against the exact fully-composed NR result.
        ID3D12Resource* resolveOriginal = targetSupportsUav ? nr.hdrCopy : target;
        ID3D12Resource* normalResolveTarget = targetSupportsUav ? target : nr.hdrCopy;

        constexpr unsigned kFinalColorStatsW = 32u;
        const unsigned finalColorStatsH = std::max(
            1u, (unsigned) (((uint64_t) kFinalColorStatsW * height + std::max(width, 1u) / 2u) /
                            std::max(width, 1u)));
        const auto matchesTexture = [](ID3D12Resource* resource, DXGI_FORMAT format, unsigned w, unsigned h)
        {
            if (!resource)
                return false;
            const auto d = resource->GetDesc();
            return d.Format == format && d.Width == w && d.Height == h;
        };

        bool finalColourReady = finalColourEnabled;
        if (finalColourReady && !matchesTexture(nr.finalColorOutput, desc.Format, width, height))
        {
            ParkNrResource(nr.finalColorOutput);
            nr.finalColorOutput = CreateScratch(device, desc.Format, width, height);
            nr.finalColorOutputReadable = false;
            nr.finalColorHistoryValid = false;
        }
        for (auto** stats : { &nr.finalColorStatsOriginal, &nr.finalColorStatsNr })
        {
            if (finalColourReady && !matchesTexture(*stats, DXGI_FORMAT_R32G32B32A32_FLOAT,
                                                    kFinalColorStatsW, finalColorStatsH))
            {
                ParkNrResource(*stats);
                *stats = CreateScratch(device, DXGI_FORMAT_R32G32B32A32_FLOAT,
                                       kFinalColorStatsW, finalColorStatsH);
                nr.finalColorStatsReadable = false;
                nr.finalColorHistoryValid = false;
            }
        }
        for (unsigned i = 0; i < 2; ++i)
        {
            if (finalColourReady && !matchesTexture(nr.finalColorHistory[i], DXGI_FORMAT_R32G32B32A32_FLOAT, 1u, 1u))
            {
                ParkNrResource(nr.finalColorHistory[i]);
                nr.finalColorHistory[i] = CreateScratch(device, DXGI_FORMAT_R32G32B32A32_FLOAT, 1u, 1u);
                nr.finalColorHistoryReadable[i] = false;
                nr.finalColorHistoryValid = false;
            }
        }
        finalColourReady = finalColourReady && nr.finalColorOutput && nr.finalColorStatsOriginal &&
                           nr.finalColorStatsNr && nr.finalColorHistory[0] && nr.finalColorHistory[1];

        ID3D12Resource* resolveTarget = finalColourReady ? nr.finalColorOutput : normalResolveTarget;
        if (finalColourReady)
        {
            if (nr.finalColorOutputReadable)
            {
                Barrier(cmdList, nr.finalColorOutput, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                nr.finalColorOutputReadable = false;
            }
        }
        else if (targetSupportsUav)
        {
            TransitionTarget(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        else
        {
            Barrier(cmdList, nr.hdrCopy, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }

        const bool resolved =
            enlargementReady && asyncSubmissionReady &&
            shader.DispatchPassAux2(cmdList, resolveParams, resolveProxy, resolveAnswer, resolveOriginal,
                                    encoded.exposure, directDetailInfo, directDetailReference, resolveTarget, nullptr);
        compositionSucceeded = resolved;
        if (resizeFieldReadable)
            Barrier(cmdList, enlarger->input.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        bool finalColourApplied = false;
        if (resolved && finalColourReady)
        {
            Barrier(cmdList, nr.finalColorOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            nr.finalColorOutputReadable = true;

            if (nr.finalColorStatsReadable)
            {
                Barrier(cmdList, nr.finalColorStatsOriginal, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                Barrier(cmdList, nr.finalColorStatsNr, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                nr.finalColorStatsReadable = false;
            }

            DlssNrConstants stats {};
            stats.Mode = DlssNrMode_FinalColourStats;
            stats.Width = kFinalColorStatsW;
            stats.Height = finalColorStatsH;
            stats.Passthrough = resolveParams.Passthrough;
            const bool statsOk = shader.DispatchPass(resolveCmd, stats, resolveOriginal, nr.finalColorOutput,
                                                     nullptr, nullptr, nullptr, nr.finalColorStatsOriginal,
                                                     nr.finalColorStatsNr);
            if (statsOk)
            {
                Barrier(cmdList, nr.finalColorStatsOriginal, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                Barrier(cmdList, nr.finalColorStatsNr, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                nr.finalColorStatsReadable = true;

                const unsigned historyWrite = nr.finalColorHistoryCursor & 1u;
                const unsigned historyRead = historyWrite ^ 1u;
                if (nr.finalColorHistoryReadable[historyWrite])
                {
                    Barrier(cmdList, nr.finalColorHistory[historyWrite], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    nr.finalColorHistoryReadable[historyWrite] = false;
                }

                DlssNrConstants reduce {};
                reduce.Mode = DlssNrMode_FinalColourReduce;
                reduce.Width = 1;
                reduce.Height = 1;
                reduce.GuideWidth = kFinalColorStatsW;
                reduce.GuideHeight = finalColorStatsH;
                reduce.MaxRatio = std::clamp(cfg.DlssNrFinalColourSmoothingMs.value_or_default(), 0.0f, 5000.0f);
                reduce.DebugScale = std::clamp(frame.FrameTimeMs, 0.01f, 1000.0f);
                reduce.ApplyModel = nr.finalColorHistoryValid ? 1u : 0u;
                ID3D12Resource* previousHistory =
                    nr.finalColorHistoryValid && nr.finalColorHistoryReadable[historyRead]
                        ? nr.finalColorHistory[historyRead]
                        : nr.finalColorStatsOriginal;
                const bool reduceOk = shader.DispatchPass(resolveCmd, reduce, nr.finalColorStatsOriginal,
                                                           nr.finalColorStatsNr, previousHistory, nullptr, nullptr,
                                                           nr.finalColorHistory[historyWrite], nullptr);
                if (reduceOk)
                {
                    Barrier(cmdList, nr.finalColorHistory[historyWrite], D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    nr.finalColorHistoryReadable[historyWrite] = true;
                    nr.finalColorHistoryValid = true;
                    nr.finalColorHistoryCursor = historyRead;

                    DlssNrConstants apply {};
                    apply.Mode = DlssNrMode_FinalColourApply;
                    apply.Width = width;
                    apply.Height = height;
                    apply.Passthrough = resolveParams.Passthrough;
                    apply.TransferStrength = finalChromaticityRecovery * 0.01f;
                    apply.ColourStrength = finalSaturationRecovery * 0.01f;
                    apply.ApplyModel = cfg.DlssNrFinalChromaticityPreserveSaturation.value_or_default() ? 1u : 0u;
                    apply.Transfer = std::min(cfg.DlssNrFinalSaturationMode.value_or_default(), 1u);
                    apply.MaxDarkening = std::clamp(
                        cfg.DlssNrFinalHighSaturationProtection.value_or_default() * 0.01f, 0.0f, 1.0f);

                    if (targetSupportsUav)
                    {
                        TransitionTarget(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                        finalColourApplied = shader.DispatchPass(resolveCmd, apply, nr.finalColorOutput,
                                                                 nr.finalColorHistory[historyWrite], nullptr, nullptr,
                                                                 nullptr, target, nullptr);
                    }
                    else
                    {
                        Barrier(cmdList, nr.hdrCopy, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                        finalColourApplied = shader.DispatchPass(resolveCmd, apply, nr.finalColorOutput,
                                                                 nr.finalColorHistory[historyWrite], nullptr, nullptr,
                                                                 nullptr, nr.hdrCopy, nullptr);
                    }
                }
            }
        }

        // Optional colour matching must never turn a successful NR resolve into a missing frame.
        if (resolved && finalColourReady && !finalColourApplied)
        {
            Barrier(cmdList, nr.finalColorOutput, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_COPY_SOURCE);
            const D3D12_RESOURCE_STATES priorTargetState = targetState;
            TransitionTarget(D3D12_RESOURCE_STATE_COPY_DEST);
            cmdList->CopyResource(target, nr.finalColorOutput);
            TransitionTarget(priorTargetState);
            Barrier(cmdList, nr.finalColorOutput, D3D12_RESOURCE_STATE_COPY_SOURCE,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        else if (resolved && !targetSupportsUav)
        {
            // Either the legacy resolve wrote hdrCopy directly, or the colour-recovery pass did.
            Barrier(cmdList, nr.hdrCopy, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            const D3D12_RESOURCE_STATES priorTargetState = targetState;
            TransitionTarget(D3D12_RESOURCE_STATE_COPY_DEST);
            cmdList->CopyResource(target, nr.hdrCopy);
            TransitionTarget(priorTargetState);
            Barrier(cmdList, nr.hdrCopy, D3D12_RESOURCE_STATE_COPY_SOURCE,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        else if (!targetSupportsUav && !finalColourReady)
        {
            // The legacy resolve target was made writable even while private DLSS was warming up.
            Barrier(cmdList, nr.hdrCopy, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }

        if (superDownOk)
        {
            if (spatial)
            {
                for (auto* nativePair : { nr.spatialProxyNative, nr.spatialAnswerNative })
                    Barrier(cmdList, nativePair, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
            else
                Barrier(cmdList, nr.outputNative, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }

        // Schedule matched proxy/output capture for delayed readback.
        if (captureFrames.isActive())
        {
            captureFrames.record(cmdList, device, nr.colorCopy, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, target,
                                 targetState);
        }
    }
    else if (result != NVSDK_NGX_Result_Success)
    {
        finishAsyncNr();
        if (spatial)
        {
            nr.spatialFallback = true;
            nr.spatialFallbackReason = "NGX rejected the packed model input";
            nr.reset = true;
            modelRunning = false;
            for (auto& model : nr.models)
                model.RetryAfterFailure();
            LOG_WARN("DLSS-NR spatial evaluate returned 0x{:X} ({}); trying ordinary NR next frame", (uint32_t) result,
                     NgxResultName((unsigned int) result));
        }
        else
        {
            nr.failed = true;
            nr.reason = "the model refused to run";
            LOG_ERROR("DLSS-NR evaluate returned 0x{:X} ({}); use Retry to recreate the model", (uint32_t) result,
                      NgxResultName((unsigned int) result));
        }
    }

    if (asyncDetailActive && !asyncNrSubmitted)
        finishAsyncNr();

    // Restore all intermediate surfaces to the UAV state expected by the next frame.
    MakeModelWritable(nr.output);
    if (nr.passScratch != nullptr)
        MakeModelWritable(nr.passScratch);

    Barrier(cmdList, nr.hdrCopy, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    if (nr.passClamp != nullptr)
        MakeModelWritable(nr.passClamp);

    if (interPassWorkingReadable && nr.interPassWorking)
        Barrier(cmdList, nr.interPassWorking, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (interPassP100Readable && nr.interPassP100)
        Barrier(cmdList, nr.interPassP100, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (interPassLowReadable && nr.interPassResidualLow)
        Barrier(cmdList, nr.interPassResidualLow, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (interPassDebugReadable && nr.interPassDebug)
        Barrier(cmdList, nr.interPassDebug, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    if (spatial)
    {
        for (auto* packed : { nr.spatialColor, nr.spatialDepth, nr.spatialMotion })
            Barrier(cmdList, packed, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (spatialUnpacked)
            for (auto* unpacked : { nr.spatialProxy, nr.spatialAnswer })
                Barrier(cmdList, unpacked, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    // The matched guides were written this frame and read by the model; back to UAV for the next.
    if (matchedGuides)
    {
        Barrier(cmdList, nr.depthSmall, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier(cmdList, nr.motionSmall, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    // Failed evaluations leave the game's original image intact. A successful copy-back writes
    // only the active rectangle and restores both resources before DLSS consumes the image.
    FinishColor(compositionSucceeded);
    if (compositionSucceeded)
        ++nr.successfulDispatches;
    nr.spatialActive = spatial && compositionSucceeded;

    EndGpuTiming(cmdList);
    if (ownsFinishedPrep && !asyncDetailActive)
        restoreOwnedGuides(resolveCmd);

    // Restore guide clones to COPY_DEST for the next frame's refresh.
    if (originalDepthIn == nr.depthClone)
        Barrier(cmdList, nr.depthClone, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);

    if (originalMotionIn == nr.motionClone)
        Barrier(cmdList, nr.motionClone, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_COPY_DEST);

    if (reduced && !spatial && nr.colorSmall != nullptr &&
        (nr.colorSmall->GetDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS) == 0)
    {
        Barrier(cmdList, nr.colorSmall, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    // Leave the staging copy as the next frame expects to find it.
    Barrier(cmdList, nr.colorCopy, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}
