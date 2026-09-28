#include "pch.h"
#include "DlssNr_Dx12_State.h"

void DlssNr_Dx12::State::EncodeInput(EncodeContext& context)
{
    const auto& cfg = *Config::Instance();
    const auto& frame = context.frame;
    auto* cmdList = context.cmdList;
    auto* device = context.device;
    auto* target = context.target;
    auto& targetState = context.targetState;
    auto& whitePoint = context.whitePoint;
    auto& modelInput = context.modelInput;
    const auto width = nr.width, height = nr.height;
    const auto workWidth = nr.workWidth, workHeight = nr.workHeight;
    const auto workScale = context.workScale;
    const bool targetSupportsUav = context.targetSupportsUav;
    const bool reduced = workWidth != width || workHeight != height;
    const bool isHdrBuffer = frame.ColourIsLinearHdr;
    const auto TransitionTarget = [&](D3D12_RESOURCE_STATES to)
    {
        Barrier(cmdList, target, targetState, to);
        targetState = to;
    };
    whitePoint =
        frame.WhitePointOverride > 0.0f ? frame.WhitePointOverride : cfg.DlssNrWhitePointScale.value_or_default();

    const bool wasHeld = nr.heldActive;
    // Frame hold. Freeze the encode's input so a live setting change re-renders the same frame. This
    // is self-contained on purpose: it copies the output aside on hold-on and copies it BACK over the
    // live output before the encode reads it while held, so the encode's own path and barriers below
    // are untouched and the default (hold off) is byte-identical. See design/frame-hold.md.
    //
    // `target` is UAV here (normalised at entry). The held copy is
    // left in COPY_SOURCE after capture and stays there for every restore.
    {
        const bool hold = cfg.DlssNrHoldFrame.value_or_default();

        if (hold)
        {
            const D3D12_RESOURCE_DESC td = target->GetDesc();
            const bool needCapture = !nr.heldActive || nr.heldColor == nullptr ||
                                     (unsigned int) td.Width != nr.heldWidth || td.Height != nr.heldHeight ||
                                     td.Format != nr.heldFormat;

            if (needCapture)
            {
                // Hold-on (or the output changed shape under a hold): capture THIS frame, do not
                // restore -- target already holds the frame to freeze, and the pass runs on it.
                if (nr.heldColor != nullptr)
                    ParkNrResource(nr.heldColor);

                nr.heldColor = CreateScratch(device, td.Format, (unsigned int) td.Width, td.Height);

                if (nr.heldColor != nullptr)
                {
                    const D3D12_RESOURCE_STATES priorTargetState = targetState;
                    TransitionTarget(D3D12_RESOURCE_STATE_COPY_SOURCE);
                    Barrier(cmdList, nr.heldColor, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_COPY_DEST);
                    cmdList->CopyResource(nr.heldColor, target);
                    Barrier(cmdList, nr.heldColor, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
                    TransitionTarget(priorTargetState);

                    nr.heldActive = true;
                    nr.heldWidth = (unsigned int) td.Width;
                    nr.heldHeight = td.Height;
                    nr.heldFormat = td.Format;
                    nr.heldWhitePoint = whitePoint;
                }
            }
            else
            {
                // Held: restore the frozen frame onto the live output before the encode reads it.
                const D3D12_RESOURCE_STATES priorTargetState = targetState;
                TransitionTarget(D3D12_RESOURCE_STATE_COPY_DEST);
                cmdList->CopyResource(target, nr.heldColor);
                TransitionTarget(priorTargetState);
            }

            // Keep the captured white point for the held comparison.
            if (nr.heldActive)
            {
                whitePoint = nr.heldWhitePoint;
            }
        }
        else if (nr.heldActive)
        {
            // Released: let go of the frozen frame and resume live input next frame.
            if (nr.heldColor != nullptr)
                ParkNrResource(nr.heldColor);
            nr.heldActive = false;
        }
    }

    const auto source = cfg.DlssNrWhitePointSource.value_or_default();
    auto* gameExposure = static_cast<ID3D12Resource*>(frame.ExposureTexture);
    const bool gameValid = gameExposure && gameExposure->GetDesc().Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
                           gameExposure->GetDesc().Width == 1 && gameExposure->GetDesc().Height == 1 &&
                           gameExposure->GetDesc().DepthOrArraySize == 1 &&
                           gameExposure->GetDesc().SampleDesc.Count == 1;
    const bool exposureHeld = wasHeld && nr.heldActive && nr.exposureReadable && nr.exposureSource == source;
    if (exposureHeld)
    {
        context.exposure = nr.exposure;
        DlssNr::ExposureConstants(context.exposureConstants, cfg, source, nr.exposurePreExposure);
    }
    if (!exposureHeld && isHdrBuffer && frame.WhitePointOverride <= 0 && (source == 3 || (source == 1 && gameValid)))
    {
        if (!nr.exposure)
            nr.exposure = CreateScratch(device, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 1);
        if (source == 3 && !nr.exposureMeter)
            nr.exposureMeter = CreateScratch(device, DXGI_FORMAT_R32G32B32A32_FLOAT, 64, 64);
        if (nr.exposure && (source != 3 || nr.exposureMeter))
        {
            auto& meter = context.exposureConstants;
            DlssNr::ExposureConstants(meter, cfg, source, frame.PreExposure);
            meter.ExposureSourceWidth = width;
            meter.ExposureSourceHeight = height;
            if (nr.exposureReadable)
                Barrier(cmdList, nr.exposure, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            bool ready = false;
            if (source == 3)
            {
                const auto previous = targetState;
                TransitionTarget(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                meter.Mode = DlssNrMode_Meter;
                meter.Width = meter.Height = 64;
                ready = shader.DispatchPass(cmdList, meter, target, nullptr, nullptr, nullptr, nullptr,
                                            nr.exposureMeter, nullptr);
                TransitionTarget(previous);
                Barrier(cmdList, nr.exposureMeter, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                meter.Mode = DlssNrMode_AutoExposure;
                meter.Width = meter.Height = 1;
                ready = ready && shader.DispatchPass(cmdList, meter, nr.exposureMeter, nullptr, nullptr, nullptr,
                                                     nullptr, nr.exposure, nullptr);
                Barrier(cmdList, nr.exposureMeter, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
            else
            {
                const auto prior = static_cast<D3D12_RESOURCE_STATES>(frame.ExposureState);
                Barrier(cmdList, gameExposure, prior, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                meter.Mode = DlssNrMode_Downsample;
                meter.Width = meter.Height = 1;
                ready = shader.DispatchPass(cmdList, meter, gameExposure, nullptr, nullptr, nullptr, nullptr,
                                            nr.exposure, nullptr);
                Barrier(cmdList, gameExposure, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, prior);
            }
            Barrier(cmdList, nr.exposure, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            nr.exposureReadable = true;
            if (ready)
            {
                context.exposure = nr.exposure;
                nr.exposureSource = source;
                nr.exposurePreExposure = meter.PreExposure;
            }
        }
    }

    DlssNrConstants encodeParams = context.exposure ? context.exposureConstants : DlssNrConstants {};
    encodeParams.Mode = DlssNrMode_Encode;
    // A frame that is already display-referred is handed over untouched: the encode becomes a copy and
    // the resolve adds the model's edit back at full scale.
    encodeParams.Passthrough = isHdrBuffer ? 0u : 1u;
    encodeParams.WhitePoint = whitePoint;
    encodeParams.ReversibleMode = cfg.DlssNrReversibleMode.value_or_default();
    encodeParams.Width = width;
    encodeParams.Height = height;

    TransitionTarget(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    context.encodeSucceeded = shader.DispatchPass(cmdList, encodeParams, target, nullptr, nullptr, context.exposure,
                                                  nullptr, nr.colorCopy, nr.hdrCopy);

    if (targetSupportsUav)
        TransitionTarget(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    // The transitions double as the wait for the encode's writes.
    Barrier(cmdList, nr.colorCopy, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Barrier(cmdList, nr.hdrCopy, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    // Below full resolution the model is shown a filtered shrink of the proxy; the edit it returns is
    // enlarged during the resolve while the frame underneath stays full size and untouched.
    modelInput = nr.colorCopy;
    context.referenceInput = modelInput;

    if (reduced && !context.spatial && nr.colorSmall != nullptr)
    {
        bool built = false;

        if (workScale > 1.0f)
        {
            // Supersample: enlarge the proxy to the larger working size with a real upscaling filter
            // (the Output Scaling upsampler) so the model sees a clean super-native input, rather than
            // the box minifier which only makes sense going down. colorCopy is NON_PIXEL_SHADER_RESOURCE
            // from the encode (SRV-ready); colorSmall is UNORDERED_ACCESS from last frame's resolve.
            // (Re)build the supersample scalers when missing or when the NR downscaler changed (the
            // filter is baked at construction). Both use NR's own DlssNrScalingDownscaler, independent
            // of Output Scaling, so the two can run different filters at once. superDown is built here
            // and used after the model (the down-leg below).
            const Scaler nrScaler = cfg.DlssNrScalingDownscaler.value_or_default();
            if (nr.nrScaler != nrScaler)
            {
                ReleaseSupersamplers();
                nr.nrScaler = nrScaler;
            }
            if (nr.superUp == nullptr)
                nr.superUp = new OS_Dx12("DLSS-NR supersample up", device, true, nrScaler);
            if (nr.superDown == nullptr)
                nr.superDown = new OS_Dx12("DLSS-NR supersample down", device, false, nrScaler);

            if (nr.superUp != nullptr && nr.superUp->DispatchResources(cmdList, nr.colorCopy, nr.colorSmall))
            {
                Barrier(cmdList, nr.colorSmall, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                built = true;
            }
        }

        if (!built)
        {
            if (workScale > 1.0f)
            {
                if (!warnedSuper)
                {
                    warnedSuper = true;
                    LOG_WARN("DLSS-NR supersample: upscaler unavailable, falling back to a blocky enlarge.");
                }
            }

            const bool upscaledResidual =
                workScale < 1.0f && cfg.DlssNrTransfer.value_or_default() == 6;
            const uint32_t proxyFilter =
                std::min(upscaledResidual ? cfg.DlssNrUpscaledResidualDownscaleFilter.value_or_default()
                                          : cfg.DlssNrProxyDownscaleFilter.value_or_default(),
                         11u);
            Scaler exactScaler = Scaler::Count;
            switch (proxyFilter)
            {
            case 2: exactScaler = Scaler::CatmullRom; break;
            case 3: exactScaler = Scaler::Lanczos2; break;
            case 5: exactScaler = Scaler::FSR1; break;
            case 6: exactScaler = Scaler::Bicubic; break;
            case 7: exactScaler = Scaler::Lanczos3; break;
            case 8: exactScaler = Scaler::Kaiser2; break;
            case 9: exactScaler = Scaler::Kaiser3; break;
            case 10: exactScaler = Scaler::Magic; break;
            default: break;
            }

            if (workScale < 1.0f && exactScaler != Scaler::Count)
            {
                if (nr.proxyDownScaler != exactScaler)
                {
                    if (auto* retired = std::exchange(nr.proxyDown, nullptr))
                        lifetime.Retire([retired] { delete retired; });
                    nr.proxyDownScaler = exactScaler;
                }
                if (nr.proxyDown == nullptr)
                    nr.proxyDown = new OS_Dx12("DLSS-NR proxy downsample", device, false, exactScaler);

                if (nr.proxyDown != nullptr && nr.proxyDown->DispatchResources(cmdList, nr.colorCopy, nr.colorSmall))
                {
                    Barrier(cmdList, nr.colorSmall, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    built = true;
                }
            }
            else if (nr.proxyDown != nullptr)
            {
                if (auto* retired = std::exchange(nr.proxyDown, nullptr))
                    lifetime.Retire([retired] { delete retired; });
                nr.proxyDownScaler = Scaler::Count;
            }

            if (!built)
            {
                DlssNrConstants down {};
                down.Mode = DlssNrMode_Downsample;
                down.Width = workWidth;
                down.Height = workHeight;
                down.Transfer = exactScaler == Scaler::Count ? proxyFilter : 0u;
                shader.DispatchPass(cmdList, down, modelInput, nullptr, nullptr, nullptr, nullptr, nr.colorSmall,
                                    nullptr);
                Barrier(cmdList, nr.colorSmall, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }
        }

        modelInput = nr.colorSmall;
        context.referenceInput = modelInput;

        // Detail-quality lab: soften only the reduced model input. This is deliberately a single
        // low-resolution dispatch after the selected downscaler, so every P100->P50 filter can be
        // compared with identical preconditioning and the cost scales with the small raster.
        const uint32_t experimentTransfer = cfg.DlssNrTransfer.value_or_default();
        const uint32_t inputFilter = std::min(cfg.DlssNrExperimentInputFilter.value_or_default(), 3u);
        const bool inputExperiment =
            workScale < 1.0f && (experimentTransfer == 5u || experimentTransfer == 6u) && inputFilter != 0u;
        if (inputExperiment)
        {
            const auto rawDesc = nr.colorSmall->GetDesc();
            const bool softMatches =
                nr.colorSoft && nr.colorSoft->GetDesc().Width == rawDesc.Width &&
                nr.colorSoft->GetDesc().Height == rawDesc.Height &&
                nr.colorSoft->GetDesc().Format == rawDesc.Format &&
                nr.colorSoft->GetDesc().Flags == rawDesc.Flags;
            if (!softMatches)
            {
                ParkNrResource(nr.colorSoft);
                nr.colorSoftReadable = false;
                nr.colorSoft = CreateScratch(device, rawDesc.Format, (unsigned) rawDesc.Width, rawDesc.Height,
                                             rawDesc.Flags);
            }

            if (nr.colorSoft)
            {
                if (nr.colorSoftReadable)
                {
                    Barrier(cmdList, nr.colorSoft, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    nr.colorSoftReadable = false;
                }

                DlssNrConstants soften {};
                soften.Mode = DlssNrMode_ExperimentPrefilter;
                soften.Width = workWidth;
                soften.Height = workHeight;
                soften.Transfer = inputFilter;
                const float inputRadius = cfg.DlssNrExperimentInputRadius.value_or_default();
                const float inputStrength = cfg.DlssNrExperimentInputStrength.value_or_default();
                const float edgeThreshold = cfg.DlssNrExperimentEdgeThreshold.value_or_default();
                soften.TransferStrength = std::isfinite(inputRadius) ? inputRadius : 0.75f;
                soften.ColourStrength = std::isfinite(inputStrength) ? inputStrength : 1.0f;
                soften.DebugScale = std::isfinite(edgeThreshold) ? edgeThreshold : 0.04f;

                if (shader.DispatchPass(cmdList, soften, nr.colorSmall, nullptr, nullptr, nullptr, nullptr,
                                        nr.colorSoft, nullptr))
                {
                    Barrier(cmdList, nr.colorSoft, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    nr.colorSoftReadable = true;
                    modelInput = nr.colorSoft;

                    // 0: reconstruct exactly the softened picture NR saw. 1: keep the original sharp
                    // reduced proxy as the recovery/reference branch. Both remain independently testable.
                    context.referenceInput =
                        std::min(cfg.DlssNrExperimentReferenceSource.value_or_default(), 1u) == 0u
                            ? nr.colorSoft
                            : nr.colorSmall;
                }
            }
        }
    }
}

DlssNrConstants DlssNr_Dx12::State::MakeResolveConstants(const EncodeContext& context, unsigned int effectivePasses)
{
    const auto& cfg = *Config::Instance();
    const auto whitePoint = context.whitePoint;
    const auto width = nr.width, height = nr.height;
    const bool isHdrBuffer = context.frame.ColourIsLinearHdr;
    DlssNrConstants resolveParams = context.exposure ? context.exposureConstants : DlssNrConstants {};
    resolveParams.Mode = DlssNrMode_Resolve;
    resolveParams.WhitePoint = whitePoint;
    resolveParams.Width = width;
    resolveParams.Height = height;
    resolveParams.TransferStrength = cfg.DlssNrTransferStrength.value_or_default();
    const auto strength = [](float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 1.0f; };
    resolveParams.SkinProtection = cfg.DlssNrSkinProtection.value_or_default();
    resolveParams.ShowSkinMask = cfg.DlssNrShowSkinMask.value_or_default();
    resolveParams.SkinDetail = strength(cfg.DlssNrSkinDetail.value_or_default());
    resolveParams.SkinColour = strength(cfg.DlssNrSkinColour.value_or_default());
    resolveParams.EnvironmentDetail = strength(cfg.DlssNrEnvironmentDetail.value_or_default());
    resolveParams.EnvironmentColour = strength(cfg.DlssNrEnvironmentColour.value_or_default());
    resolveParams.ColourStrength = cfg.DlssNrColourStrength.value_or_default();
    resolveParams.DebugView = cfg.DlssNrDebugView.value_or_default();
    resolveParams.MaxRatio = cfg.DlssNrMaxRatio.value_or_default();
    resolveParams.MaxDarkening = std::clamp(cfg.DlssNrMaxDarkening.value_or_default(), 0.0f, 100.0f);
    resolveParams.Transfer = DlssNrSpatialTransfer(cfg.DlssNrTransfer.value_or_default());
    resolveParams.DebugScale = cfg.DlssNrWhitePointScale.value_or_default();
    resolveParams.Passthrough = isHdrBuffer ? 0u : 1u;
    resolveParams.ReversibleMode = cfg.DlssNrReversibleMode.value_or_default();
    resolveParams.ApplyModel = cfg.DlssNrApplyModel.value_or_default() ? 1u : 0u;
    resolveParams.CompareMode = cfg.DlssNrCompare.value_or_default();
    resolveParams.CompareSplit = cfg.DlssNrCompareSplit.value_or_default();
    resolveParams.CompareZoom = std::max(1.0f, cfg.DlssNrCompareZoom.value_or_default());
    resolveParams.CompareSwap = cfg.DlssNrCompareSwap.value_or_default() ? 1u : 0u;

    resolveParams.ReplaceDetailStrength = cfg.DlssNrReplaceDetailStrength.value_or_default();
    resolveParams.ModelWorkScale = context.workScale;
    // Enabled only after Direct DLSS successfully produced the packed P50/gate auxiliary texture.
    resolveParams.DirectDetailMode = 0;
    resolveParams.DirectDetailMaskStrength =
        std::clamp(cfg.DlssNrDirectDetailMaskStrength.value_or_default() / 100.0f, 0.0f, 1.0f);

    // The main shader intentionally leaves these legacy residual slots unused; the separate temporal
    // residual shader has its own constant buffer. Reuse them here so the experimental final-resolve
    // controls add no bytes and keep DlssNrConstants exactly 256 bytes.
    if (context.workScale < 1.0f &&
        (cfg.DlssNrTransfer.value_or_default() == 5u || cfg.DlssNrTransfer.value_or_default() == 6u))
    {
        resolveParams.ResidualHistoryValid =
            std::min(cfg.DlssNrExperimentP100EdgeLimiter.value_or_default(), 1u);
        const float limiterStrength = cfg.DlssNrExperimentP100EdgeLimiterStrength.value_or_default();
        resolveParams.ResidualBlend = std::isfinite(limiterStrength) ? limiterStrength : 1.0f;
        resolveParams.ResidualMotionBaseX =
            std::min(cfg.DlssNrExperimentStructureTransfer.value_or_default(), 2u);
        const float structureStrength = cfg.DlssNrExperimentStructureTransferStrength.value_or_default();
        resolveParams.ResidualConfidenceSensitivity =
            std::isfinite(structureStrength) ? structureStrength : 1.0f;
    }

    // Report the effective composition settings when they change.

    // Quantised to the precision it is printed at. Comparing raw floats logged 2376 lines in one
    // Enshrouded session, because a measured white point drifts continuously and every drift was a
    // change. A line per meaningful change is the point; a line per frame is a different problem.
    const ComposeReport composeNow { true,
                                     std::round(resolveParams.WhitePoint * 100.0f) / 100.0f,
                                     resolveParams.TransferStrength,
                                     resolveParams.ColourStrength,
                                     resolveParams.MaxRatio,
                                     resolveParams.Passthrough,
                                     resolveParams.DebugView,
                                     resolveParams.CompareMode,
                                     resolveParams.Transfer,
                                     nr.workWidth,
                                     nr.workHeight,
                                     effectivePasses };

    if (loggedCompose != composeNow)
    {
        loggedCompose = composeNow;
        LOG_INFO("DLSS-NR composition: paper white {:.2f}x, detail {:.2f}, colour {:.2f}, guard "
                 "{:.1f}x, colour transform {}, transfer {}, model {}x{}, passes {}, debug view {}, compare {}",
                 composeNow.whitePoint, composeNow.transfer, composeNow.colour, composeNow.maxRatio,
                 composeNow.passthrough != 0 ? "off (frame already tone mapped)" : "on (linear HDR)",
                 composeNow.residual == 3   ? "lighting + colour"
                 : composeNow.residual == 1 ? "matched residual"
                                            : "classic",
                 composeNow.workW, composeNow.workH, composeNow.passes, composeNow.debugView, composeNow.compareMode);
    }

    return resolveParams;
}
