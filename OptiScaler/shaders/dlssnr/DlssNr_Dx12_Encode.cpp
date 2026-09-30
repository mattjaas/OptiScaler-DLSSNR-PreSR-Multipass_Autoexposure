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
        const uint32_t inputFilter = std::min(cfg.DlssNrExperimentInputFilter.value_or_default(), 4u);
        const float inputStrengthRaw = cfg.DlssNrExperimentInputStrength.value_or_default();
        const float inputStrength = std::isfinite(inputStrengthRaw) ? inputStrengthRaw : 1.0f;
        const uint32_t coreMode = std::min(cfg.DlssNrExperimentCoreAttenuation.value_or_default(), 2u);
        const float coreStrengthRaw = cfg.DlssNrExperimentCoreAttenuationStrength.value_or_default();
        const float coreStrength = std::isfinite(coreStrengthRaw) ? coreStrengthRaw : 0.75f;
        const bool legacyInputActive = inputFilter >= 1u && inputFilter <= 3u && inputStrength != 0.0f;
        const bool coreInputActive = inputFilter == 4u && coreMode != 0u && coreStrength != 0.0f;
        const bool inputExperiment =
            workScale < 1.0f && (experimentTransfer == 5u || experimentTransfer == 6u) &&
            (legacyInputActive || coreInputActive);
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
                if (inputFilter == 4u)
                {
                    const float coreThreshold = cfg.DlssNrExperimentCoreDetectionThreshold.value_or_default();
                    const float coreWidth = cfg.DlssNrExperimentCoreWidth.value_or_default();
                    const float haloProtection = cfg.DlssNrExperimentCoreHaloProtection.value_or_default();
                    soften.CompareMode = coreMode;
                    soften.TransferStrength = std::isfinite(coreWidth) ? coreWidth : 1.0f;
                    soften.ColourStrength = coreStrength;
                    soften.DebugScale = std::isfinite(coreThreshold) ? coreThreshold : 0.04f;
                    soften.MaxRatio = std::isfinite(haloProtection) ? haloProtection : 1.0f;
                }
                else
                {
                    const float inputRadius = cfg.DlssNrExperimentInputRadius.value_or_default();
                    const float edgeThreshold = cfg.DlssNrExperimentEdgeThreshold.value_or_default();
                    soften.TransferStrength = std::isfinite(inputRadius) ? inputRadius : 0.75f;
                    soften.ColourStrength = inputStrength;
                    soften.DebugScale = std::isfinite(edgeThreshold) ? edgeThreshold : 0.04f;
                }

                if (shader.DispatchPass(cmdList, soften, nr.colorSmall, nullptr, nullptr, nullptr, nullptr,
                                        nr.colorSoft, nullptr))
                {
                    Barrier(cmdList, nr.colorSoft, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    nr.colorSoftReadable = true;
                    modelInput = nr.colorSoft;
                    context.inputPreparationActive = true;

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
    // residual shader has its own constant buffer. Reuse them here so experimental final-resolve controls
    // add no bytes and keep DlssNrConstants exactly 256 bytes.
    const uint32_t configuredTransfer = cfg.DlssNrTransfer.value_or_default();
    if (context.workScale < 1.0f && configuredTransfer >= 7u && configuredTransfer <= 9u)
    {
        // P100-guided residual family, internal final-resolve Transfer=8/9:
        // ResidualHistoryValid = source-space radius (1..3)
        // ResidualBlend = range sigma in encoded proxy RGB space
        // ResidualScale = spatial sigma in reduced-resolution texels
        // ResidualConfidenceSensitivity = blend: 0 bilinear residual, 1 fully guided.
        // MvScaleX / MvScaleY = high-mid / low-frequency residual gains.
        // GuideWidth = low-frequency shaping active (Aux2 contains 1/8 E50).
        // GuideHeight = experimental shadow confidence active.
        // ExposureSourceWidth/Height/Padding = raw float bits of shadow low/high/floor.
        resolveParams.ResidualHistoryValid =
            std::clamp(cfg.DlssNrGuidedResidualRadius.value_or_default(), 1u, 3u);
        const float rangeSigma = cfg.DlssNrGuidedResidualRangeSigma.value_or_default();
        resolveParams.ResidualBlend = std::isfinite(rangeSigma) ? rangeSigma : 0.015f;
        const float spatialSigma = cfg.DlssNrGuidedResidualSpatialSigma.value_or_default();
        resolveParams.ResidualScale = std::isfinite(spatialSigma) ? spatialSigma : 1.20f;
        const float guideStrength = cfg.DlssNrGuidedResidualGuideStrength.value_or_default();
        resolveParams.ResidualConfidenceSensitivity = std::isfinite(guideStrength) ? guideStrength : 0.75f;

        const uint32_t shapingMode = std::min(cfg.DlssNrGuidedResidualShaping.value_or_default(), 2u);
        const unsigned int finalPass = effectivePasses > 0 ? effectivePasses - 1u : 0u;
        const uint32_t finalStyle = PassSettings(cfg, finalPass).style;
        float highGain = 1.0f;
        float lowGain = 1.0f;
        bool shapingActive = false;
        if (shapingMode == 1u && effectivePasses == 1u && configuredTransfer == 7u)
        {
            if (finalStyle == 0u) // Standard
            {
                highGain = 0.421f;
                lowGain = 0.609f;
                shapingActive = true;
            }
            else if (finalStyle == 1u) // Natural
            {
                highGain = 0.454f;
                lowGain = 0.741f;
                shapingActive = true;
            }
            else if (finalStyle == 2u) // Cinematic
            {
                highGain = 0.370f;
                lowGain = 0.780f;
                shapingActive = true;
            }
        }
        else if (shapingMode == 2u)
        {
            const float configuredHigh = cfg.DlssNrGuidedResidualHighGain.value_or_default();
            const float configuredLow = cfg.DlssNrGuidedResidualLowGain.value_or_default();
            highGain = std::isfinite(configuredHigh) ? configuredHigh : 0.454f;
            lowGain = std::isfinite(configuredLow) ? configuredLow : 0.741f;
            shapingActive = true;
        }
        resolveParams.MvScaleX = highGain;
        resolveParams.MvScaleY = lowGain;
        resolveParams.GuideWidth = shapingActive ? 1u : 0u;
        resolveParams.GuideHeight = cfg.DlssNrGuidedResidualShadowGate.value_or_default() ? 1u : 0u;

        float shadowLow = cfg.DlssNrGuidedResidualShadowLow.value_or_default();
        float shadowHigh = cfg.DlssNrGuidedResidualShadowHigh.value_or_default();
        float shadowFloor = cfg.DlssNrGuidedResidualShadowFloor.value_or_default();
        if (!std::isfinite(shadowLow))
            shadowLow = 0.02f;
        if (!std::isfinite(shadowHigh))
            shadowHigh = 0.08f;
        if (!std::isfinite(shadowFloor))
            shadowFloor = 0.15f;
        static_assert(sizeof(shadowLow) == sizeof(resolveParams.ExposureSourceWidth));
        static_assert(sizeof(shadowHigh) == sizeof(resolveParams.ExposureSourceHeight));
        static_assert(sizeof(shadowFloor) == sizeof(resolveParams.ExposurePadding));
        std::memcpy(&resolveParams.ExposureSourceWidth, &shadowLow, sizeof(shadowLow));
        std::memcpy(&resolveParams.ExposureSourceHeight, &shadowHigh, sizeof(shadowHigh));
        std::memcpy(&resolveParams.ExposurePadding, &shadowFloor, sizeof(shadowFloor));
    }
    else if (context.workScale < 1.0f &&
             (cfg.DlssNrTransfer.value_or_default() == 5u || cfg.DlssNrTransfer.value_or_default() == 6u))
    {
        // Mode-local packing for the final P100-guided resolve:
        // ResidualHistoryValid = limiter mode (0 off, 1 fine, 2 fine+mid)
        // ResidualBlend = limiter strength
        // ResidualMotionBaseY = raw float bits of max edge gain
        // ResidualMotionBaseX / ResidualConfidenceSensitivity = structure-transfer mode / strength
        // DirectResolveFlags bit 1 = limiter debug mask (bit 0 is reserved for the Direct-NR family).
        const float limiterStrengthRaw = cfg.DlssNrExperimentP100EdgeLimiterStrength.value_or_default();
        const float limiterStrength = std::isfinite(limiterStrengthRaw) ? limiterStrengthRaw : 1.0f;
        resolveParams.ResidualHistoryValid =
            limiterStrength != 0.0f
                ? std::min(cfg.DlssNrExperimentP100EdgeLimiter.value_or_default(), 2u)
                : 0u;
        resolveParams.ResidualBlend = limiterStrength;
        float limiterMaxGain = cfg.DlssNrExperimentP100EdgeLimiterMaxGain.value_or_default();
        if (!std::isfinite(limiterMaxGain))
            limiterMaxGain = 1.0f;
        static_assert(sizeof(limiterMaxGain) == sizeof(resolveParams.ResidualMotionBaseY));
        std::memcpy(&resolveParams.ResidualMotionBaseY, &limiterMaxGain, sizeof(limiterMaxGain));
        if (resolveParams.ResidualHistoryValid != 0u &&
            cfg.DlssNrExperimentP100EdgeLimiterDebug.value_or_default())
            resolveParams.DirectResolveFlags |= 2u;
        const float structureStrengthRaw = cfg.DlssNrExperimentStructureTransferStrength.value_or_default();
        const float structureStrength = std::isfinite(structureStrengthRaw) ? structureStrengthRaw : 1.0f;
        resolveParams.ResidualMotionBaseX =
            structureStrength != 0.0f
                ? std::min(cfg.DlssNrExperimentStructureTransfer.value_or_default(), 2u)
                : 0u;
        resolveParams.ResidualConfidenceSensitivity = structureStrength;

        // Additional mode-local packing used only by DlssNrMode_Resolve for Direct/Upscaled residual:
        // ResidualScale = structure max gain
        // MvScaleX = structure confidence threshold
        // MvScaleY = envelope margin in percent
        // GuideWidth = envelope mode (0 off, 1 hard, 2 soft)
        // GuideHeight bits: 0 polarity guard, 1 shadow protection
        // ExposureSourceWidth/Height = raw float bits of shadow threshold / shadow strength.
        float structureMaxGain = cfg.DlssNrExperimentStructureMaxGain.value_or_default();
        if (!std::isfinite(structureMaxGain))
            structureMaxGain = 4.0f;
        resolveParams.ResidualScale = structureMaxGain;

        float structureConfidence = cfg.DlssNrExperimentStructureConfidenceThreshold.value_or_default();
        resolveParams.MvScaleX = std::isfinite(structureConfidence) ? structureConfidence : 0.020f;

        float envelopeMargin = cfg.DlssNrExperimentStructureEnvelopeMargin.value_or_default();
        resolveParams.MvScaleY = std::isfinite(envelopeMargin) ? envelopeMargin : 0.0f;
        resolveParams.GuideWidth = std::min(cfg.DlssNrExperimentStructureEnvelope.value_or_default(), 2u);
        resolveParams.GuideHeight =
            (cfg.DlssNrExperimentStructurePolarityGuard.value_or_default() ? 1u : 0u) |
            (cfg.DlssNrExperimentStructureShadowProtection.value_or_default() ? 2u : 0u);

        float shadowThreshold = cfg.DlssNrExperimentStructureShadowThreshold.value_or_default();
        if (!std::isfinite(shadowThreshold))
            shadowThreshold = 0.08f;
        float shadowStrength = cfg.DlssNrExperimentStructureShadowStrength.value_or_default();
        if (!std::isfinite(shadowStrength))
            shadowStrength = 1.0f;
        static_assert(sizeof(shadowThreshold) == sizeof(resolveParams.ExposureSourceWidth));
        static_assert(sizeof(shadowStrength) == sizeof(resolveParams.ExposureSourceHeight));
        std::memcpy(&resolveParams.ExposureSourceWidth, &shadowThreshold, sizeof(shadowThreshold));
        std::memcpy(&resolveParams.ExposureSourceHeight, &shadowStrength, sizeof(shadowStrength));
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
