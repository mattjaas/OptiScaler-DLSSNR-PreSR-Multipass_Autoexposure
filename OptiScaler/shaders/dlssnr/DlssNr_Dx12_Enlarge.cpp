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
    case 7: return Scaler::Magic;
    case 8: return Scaler::FSR1;
    default: return Scaler::Count;
    }
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
                                                           ID3D12Resource* proxy, ID3D12Resource* answer,
                                                           ID3D12Resource* depth, ID3D12Resource* motion,
                                                           const DlssNrFrameInfo& frame, const DlssNrConstants& resolve,
                                                           uint32_t transfer, bool reset,
                                                           ID3D12CommandQueue* timingQueue)
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
    const auto& cfg = *Config::Instance();
    const uint32_t directDetailMode =
        direct ? std::min(cfg.DlssNrDirectDetailRecovery.value_or_default(), 2u) : 0u;
    const uint32_t outputUpscaler =
        direct ? std::min(cfg.DlssNrDirectOutputUpscaler.value_or_default(), 9u) : 9u;
    const uint32_t detailReferenceUpscaler =
        direct ? std::min(cfg.DlssNrDirectDetailReferenceUpscaler.value_or_default(), 9u) : 0u;
    const uint32_t carrierMode = direct ? 2u : structural ? 1u : 0u;
    const int dlssPreset = cfg.DlssNrScalingDlssPreset.value_or_default();

    const auto desc = proxy->GetDesc();
    const unsigned w = unsigned(desc.Width), h = desc.Height;
    if (enlarger && (enlarger->w != w || enlarger->h != h || enlarger->outW != resolve.Width ||
                     enlarger->outH != resolve.Height || (queue && enlarger->queue.Get() != queue) ||
                     enlarger->depthInverted != frame.DepthInverted || enlarger->carrierMode != carrierMode ||
                     enlarger->dlssPreset != dlssPreset || enlarger->outputUpscaler != outputUpscaler ||
                     enlarger->detailReferenceUpscaler != detailReferenceUpscaler))
        ReleaseEnlarger();

    CollectEnlargers();
    if (!enlarger)
    {
        if (retiredEnlargers.size() >= 4)
            return say("Waiting for retired enlargement work.");

        enlarger = std::make_unique<Enlarger>();
        auto& g = *enlarger;
        g.w = w;
        g.h = h;
        g.outW = resolve.Width;
        g.outH = resolve.Height;
        g.depthInverted = frame.DepthInverted;
        g.queue = queue;
        g.carrierMode = carrierMode;
        g.dlssPreset = dlssPreset;
        g.outputUpscaler = outputUpscaler;
        g.detailReferenceUpscaler = detailReferenceUpscaler;

        g.input.Attach(CreateScratch(device, DXGI_FORMAT_R16G16B16A16_FLOAT, w, h));
        g.output.Attach(CreateScratch(device, DXGI_FORMAT_R16G16B16A16_FLOAT, g.outW, g.outH));
        g.depth.Attach(CreateScratch(device, DXGI_FORMAT_R32_FLOAT, w, h));
        g.motion.Attach(CreateScratch(device, DXGI_FORMAT_R32G32_FLOAT, w, h));
        g.exposure.Attach(CreateScratch(device, DXGI_FORMAT_R32_FLOAT, 1, 1));
        g.failed = true;
        if (!g.input || !g.output || !g.depth || !g.motion || !g.exposure)
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
        info.quality = 2;
        info.dlssPreset = dlssPreset;
        info.depthInverted = frame.DepthInverted;
        info.rayReconstruction = false;
        if (!g.dlss->Init(device, cmd, info))
            return say("Private DLSS SR: " + g.dlss->Error());

        // P50->P100 via DLSS owns a completely separate temporal feature/history from NR50->NR100.
        if (direct && detailReferenceUpscaler == 9)
        {
            g.detailDlss = std::make_unique<DlssNr::PrivateUpscalerDx12>(DlssNr::PrivateUpscaler::DLSS);
            if (!g.detailDlss->Init(device, cmd, info))
                return say("Private P50-reference DLSS SR: " + g.detailDlss->Error());
        }

        LOG_INFO("NR enlargement created at {}x{} -> {}x{}, output upscaler {}, detail-reference upscaler {}, "
                 "DLSS preset {}",
                 w, h, g.outW, g.outH, outputUpscaler, detailReferenceUpscaler, dlssPreset);

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

    const auto dd = depth->GetDesc(), md = motion->GetDesc();
    const auto regions = DlssNr::ResolveGuideRegions(
        { unsigned(dd.Width), dd.Height }, { unsigned(md.Width), md.Height },
        { frame.RenderSubrectWidth, frame.RenderSubrectHeight }, { frame.OutputWidth, frame.OutputHeight },
        frame.MotionVectorsLowResolution, frame.DepthSubrectBaseX, frame.DepthSubrectBaseY, frame.MotionSubrectBaseX,
        frame.MotionSubrectBaseY);
    if (!regions.depth.valid() || !regions.motion.valid())
        return say("enlargement needs valid depth and motion.");

    lifetime.Record(cmd);
    g.lifetime.Record(cmd);

    bool detailReady = direct && directDetailMode != 0;
    if (detailReady && (!g.detailInfo || !g.detailReference))
    {
        if (!g.detailInfo)
            g.detailInfo.Attach(CreateScratch(device, DXGI_FORMAT_R16G16B16A16_FLOAT, w, h));
        if (!g.detailReference)
            g.detailReference.Attach(CreateScratch(device, DXGI_FORMAT_R16G16B16A16_FLOAT, g.outW, g.outH));
        if (!g.detailInfo || !g.detailReference)
        {
            static bool warnedDetailAlloc = false;
            if (!warnedDetailAlloc)
            {
                warnedDetailAlloc = true;
                LOG_WARN("NR Direct detail recovery: P50/gate or P50-reference allocation failed; "
                         "falling back to Direct NR without detail recovery.");
            }
            detailReady = false;
        }
    }

    if (detailReady && g.detailReadable)
    {
        Barrier(cmd, g.detailInfo.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        g.detailReadable = false;
    }

    DlssNrConstants encode {};
    encode.Mode = direct ? (detailReady ? DlssNrMode_EncodeDirectDetail : DlssNrMode_Downsample)
                         : structural ? DlssNrMode_EncodeResizeField
                                      : DlssNrMode_EncodeProxyResidual;
    encode.Width = w;
    encode.Height = h;
    encode.Passthrough = resolve.Passthrough;

    bool ok = false;
    if (direct && detailReady)
        ok = shader.DispatchPass(cmd, encode, proxy, answer, nullptr, nullptr, nullptr, g.input.Get(),
                                 g.detailInfo.Get());
    else if (direct)
        ok = shader.DispatchPass(cmd, encode, answer, nullptr, nullptr, nullptr, nullptr, g.input.Get(), nullptr);
    else
        ok = shader.DispatchPass(cmd, encode, proxy, answer, nullptr, nullptr, nullptr, g.input.Get(), nullptr);

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

    if (!ok)
    {
        g.reset = true;
        g.detailReset = true;
        return say("enlargement guide/carrier preparation failed.");
    }

    for (auto* r : { g.input.Get(), g.depth.Get(), g.motion.Get() })
        Barrier(cmd, r, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (detailReady)
    {
        Barrier(cmd, g.detailInfo.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        g.detailReadable = true;
    }

    if (g.readable)
        Barrier(cmd, g.output.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (detailReady && g.detailReferenceReadable)
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

        const Scaler kernel = DirectSpatialScaler(selector);
        if (kernel == Scaler::Count)
            return false;
        if (!scaler)
            scaler = std::make_unique<OS_Dx12>(name, device, false, kernel);
        return scaler && scaler->DispatchResources(cmd, source, target);
    };

    bool outputOk = false;
    if (outputUpscaler == 9)
    {
        auto f = baseFrame;
        f.color.resource = g.input.Get();
        f.output.resource = g.output.Get();
        f.reset = reset || frame.Reset || g.reset || frames < g.lastFrame || frames > g.lastFrame + 1;
        outputOk = g.dlss->Evaluate(cmd, f);
        if (outputOk && g.lastFrame == 0)
            LOG_INFO("NR Direct output: first private DLSS SR evaluation succeeded on producer queue {}",
                     (void*) g.queue.Get());
    }
    else
    {
        outputOk = runSpatial(outputUpscaler, g.input.Get(), g.output.Get(), g.outputSpatialScaler,
                              "DLSS-NR Direct output upscale");
    }

    if (outputOk)
    {
        Barrier(cmd, g.output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        g.readable = true;
    }

    bool detailReferenceOk = !detailReady;
    if (detailReady)
    {
        if (detailReferenceUpscaler == 9)
        {
            if (g.detailDlss)
            {
                auto f = baseFrame;
                f.color.resource = proxy;
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
                runSpatial(detailReferenceUpscaler, proxy, g.detailReference.Get(), g.detailSpatialScaler,
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
                LOG_WARN("NR Direct detail recovery: selected P50 -> P100 reconstruction failed; "
                         "detail recovery is skipped for that frame.");
            }
        }
    }

    for (auto* r : { g.input.Get(), g.depth.Get(), g.motion.Get() })
        Barrier(cmd, r, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    g.lastFrame = frames;
    g.reset = !outputOk;
    if (!outputOk)
    {
        if (outputUpscaler == 9)
        {
            g.failed = true;
            return say("Private DLSS SR: " + g.dlss->Error());
        }
        return say("selected NR50 -> NR100 spatial upscaler failed.");
    }

    enlargementStatus.clear();
    return g.output.Get();
}
