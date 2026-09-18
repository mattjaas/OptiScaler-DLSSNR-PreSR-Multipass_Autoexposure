from pathlib import Path

HERE = Path(__file__).resolve()
_candidates = [Path.cwd(), HERE.parent]
if len(HERE.parents) >= 3:
    _candidates.append(HERE.parents[2])
ROOT = next((p for p in _candidates if (p / "OptiScaler").is_dir()), None)
if ROOT is None:
    raise RuntimeError("Run this script from the repository root (the directory containing OptiScaler).")


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8-sig")


def write(rel, text):
    (ROOT / rel).write_text(text, encoding="utf-8", newline="\n")


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, got {count}")
    return text.replace(old, new, 1)


# Runs last, after the D3D12 auto-exposure, anchor, Base White Point and highlight-protection patches.
# It gives Vulkan the same same-frame 64x64 -> 1x1 automatic-exposure path. Runtime exposure stays on
# the GPU; the delayed readback exists only for menu/status and Anchor-point capture.

# -----------------------------------------------------------------------------
# Public Vulkan status functions.
# -----------------------------------------------------------------------------
rel = "OptiScaler/dlssnr/DlssNrFeature_Vk.h"
s = read(rel)
s = replace_once(
    s,
    "namespace DlssNr\n{\n",
    "namespace DlssNr\n{\n\nstruct ExposureStatus;\n",
    "Vulkan ExposureStatus forward declaration",
)
s = replace_once(
    s,
    "bool ExposureOfferedVk();\n\nvoid ShutdownVk(bool deviceAlive = true);\n",
    "bool ExposureOfferedVk();\n\n"
    "// Exposure values read back asynchronously for menu/status only. Runtime Vulkan composition\n"
    "// consumes the same-frame GPU textures directly.\n"
    "ExposureStatus GameExposureStatusVk();\n"
    "ExposureStatus AutoExposureStatusVk();\n\n"
    "void ShutdownVk(bool deviceAlive = true);\n",
    "Vulkan exposure status declarations",
)
write(rel, s)

# -----------------------------------------------------------------------------
# Vulkan feature: resources, same-frame dispatch, anchors, readback and model ExposureTexture.
# -----------------------------------------------------------------------------
rel = "OptiScaler/dlssnr/DlssNrFeature_Vk.cpp"
s = read(rel)
s = replace_once(
    s,
    '#include "DlssNrFeature_Vk.h"\n',
    '#include "DlssNrFeature_Vk.h"\n#include "DlssNrFeature_Dx12.h"\n',
    "Vulkan ExposureStatus definition include",
)
s = replace_once(s, "#include <string>\n", "#include <string>\n#include <vector>\n", "Vulkan vector include")

s = replace_once(
    s,
    "using PFN_VkEvaluate = int(__cdecl*)(void*, void*, void*, void*, void*, void*, void*, unsigned int,",
    "using PFN_VkEvaluate = int(__cdecl*)(void*, void*, void*, void*, void*, void*, void*, void*, unsigned int,",
    "Vulkan evaluate typedef exposure argument",
)
s = replace_once(
    s,
    "    OwnedImage meter;\n"
    "    VkBuffer meterReadback[4] = {};\n",
    "    OwnedImage meter;\n"
    "    OwnedImage autoExposure;\n"
    "    VkBuffer meterReadback[4] = {};\n",
    "Vulkan automatic exposure image",
)
s = replace_once(
    s,
    "    void* meterMapped[4] = {};\n"
    "    unsigned long long meterFrames = 0;\n",
    "    void* meterMapped[4] = {};\n"
    "    unsigned long long meterFrames = 0;\n"
    "    // 0 none, 1 game ExposureTexture, 2 OptiScaler automatic exposure.\n"
    "    uint32_t meterKind[4] = {};\n"
    "    float meterPreExposure[4] = { 1.0f, 1.0f, 1.0f, 1.0f };\n"
    "    uint32_t exposureReadbackSource = 0;\n"
    "    float autoExposureValue = 0.0f;\n"
    "    float autoExposurePreExposure = 1.0f;\n"
    "    unsigned long long autoExposureFrames = 0;\n"
    "    bool autoExposureActive = false;\n",
    "Vulkan automatic exposure CPU status",
)

s = replace_once(
    s,
    "// The grid the meter writes, and the size of one readback. 8 * 8 * sizeof(float).\n"
    "constexpr uint32_t kMeterSide = 8;\n",
    "// Shared 64x64 luminance grid. Game-exposure mode only writes texel (0,0); automatic exposure\n"
    "// uses all 4096 tile means, matching the D3D12 path.\n"
    "constexpr uint32_t kMeterSide = 64;\n",
    "Vulkan meter grid 64x64",
)

helper_marker = "// at 1 and must not be encoded a second time; an 8-bit or normalised format cannot be scene-referred\n"
helper_code = r'''struct VkExposureTrimAnchor
{
    float key = 0.0f;
    float trim = 1.0f;
};

std::vector<VkExposureTrimAnchor> ParseExposureTrimAnchorsVk(const std::string& text)
{
    std::vector<VkExposureTrimAnchor> out;
    size_t pos = 0;

    while (pos < text.size() && out.size() < 8)
    {
        const size_t semi = text.find(';', pos);
        const std::string token =
            text.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
        pos = semi == std::string::npos ? text.size() : semi + 1;

        const size_t colon = token.find(':');
        if (colon == std::string::npos)
            continue;

        try
        {
            const float key = std::stof(token.substr(0, colon));
            const float trim = std::stof(token.substr(colon + 1));
            if (std::isfinite(key) && key > 1e-8f && std::isfinite(trim) && trim > 0.0f)
                out.push_back({ key, std::clamp(trim, 0.25f, 50.0f) });
        }
        catch (...)
        {
        }
    }

    std::sort(out.begin(), out.end(),
              [](const VkExposureTrimAnchor& a, const VkExposureTrimAnchor& b) { return a.key < b.key; });
    return out;
}

float ExposureTrimForRuntimeVk(float key, float fallback, const std::vector<VkExposureTrimAnchor>& anchors,
                               bool preview)
{
    fallback = std::clamp(fallback, 0.25f, 50.0f);
    if (preview || anchors.empty() || !(std::isfinite(key) && key > 1e-8f))
        return fallback;
    if (anchors.size() == 1)
        return anchors[0].trim;
    if (key <= anchors.front().key)
        return anchors.front().trim;
    if (key >= anchors.back().key)
        return anchors.back().trim;

    for (size_t i = 0; i + 1 < anchors.size(); ++i)
    {
        const auto& a = anchors[i];
        const auto& b = anchors[i + 1];
        if (key >= a.key && key <= b.key && b.key > a.key * 1.000001f)
        {
            const float t = (std::log(key) - std::log(a.key)) / (std::log(b.key) - std::log(a.key));
            return std::clamp(std::exp(std::log(a.trim) + t * (std::log(b.trim) - std::log(a.trim))),
                              0.25f, 50.0f);
        }
    }

    return anchors.back().trim;
}

void FillAutoExposureTrimConstantsVk(DlssNrConstants& params, const Config& cfg)
{
    params.ExposureTrim = std::clamp(cfg.DlssNrAutoExposureTrim.value_or_default(), 0.25f, 50.0f);
    params.ExposureTrimPreview = cfg.DlssNrAutoExposureTrimPreview.value_or_default() ? 1u : 0u;

    const auto anchors = ParseExposureTrimAnchorsVk(cfg.DlssNrAutoExposureTrimAnchors.value_or_default());
    params.ExposureTrimAnchorCount = (uint32_t) std::min<size_t>(anchors.size(), 8);

    float* pairs = &params.ExposureTrimAnchorExposure0;
    for (size_t i = 0; i < params.ExposureTrimAnchorCount; ++i)
    {
        pairs[i * 2 + 0] = anchors[i].key;
        pairs[i * 2 + 1] = anchors[i].trim;
    }
}

'''
s = replace_once(s, helper_marker, helper_code + helper_marker, "Vulkan trim anchor helpers")

old_readback = r'''    // Take the grid written four frames ago. Retired by now, so this reads mapped memory rather than
    // waiting on the GPU -- which is the whole reason for the ring.
    if (g_vk.meterFrames >= kMeterSlots)
    {
        const void* mapped = g_vk.meterMapped[g_vk.meterFrames % kMeterSlots];

        if (mapped != nullptr)
        {
            float measured = 0.0f;
            std::memcpy(&measured, mapped, sizeof(float));

            // Believed only if it could be an exposure. A texel read through a layout the game did
            // not leave it in, or a slot the game stopped filling, fails here and the last good
            // value stands.
            if (std::isfinite(measured) && measured > 0.0f)
                g_vk.gameExposure = measured;
        }
    }
'''
new_readback = r'''    const uint32_t requestedWhitePointSource = cfg.DlssNrWhitePointSource.value_or_default();
    if (requestedWhitePointSource != g_vk.exposureReadbackSource)
    {
        g_vk.exposureReadbackSource = requestedWhitePointSource;
        g_vk.meterFrames = 0;
        for (uint32_t& kind : g_vk.meterKind)
            kind = 0u;
        g_vk.autoExposureValue = 0.0f;
        g_vk.autoExposurePreExposure = 1.0f;
    }
    g_vk.autoExposureActive = false;

    // Take the value written four meter dispatches ago. Runtime never waits for this; it is only the
    // menu/Anchor-point courier. The composition below reads this frame's GPU texture directly.
    if (g_vk.meterFrames >= kMeterSlots)
    {
        const unsigned long long slot = g_vk.meterFrames % kMeterSlots;
        const void* mapped = g_vk.meterMapped[slot];

        if (mapped != nullptr)
        {
            float measured = 0.0f;
            std::memcpy(&measured, mapped, sizeof(float));
            if (std::isfinite(measured) && measured > 0.0f)
            {
                if (g_vk.meterKind[slot] == 1u)
                    g_vk.gameExposure = measured;
                else if (g_vk.meterKind[slot] == 2u)
                {
                    g_vk.autoExposureValue = measured;
                    g_vk.autoExposurePreExposure = g_vk.meterPreExposure[slot];
                }
            }
        }
    }
'''
s = replace_once(s, old_readback, new_readback, "Vulkan typed exposure readback")

old_meter_alloc = r'''        // The meter is a fixed 8x8 whatever the frame is, so it is only built the once -- but it is
        // built alongside the rest so that a failure here is caught by the same check.
        const bool meterReady = (g_vk.meter.Valid() || CreateImage(g_vk.meter, kMeterSide, kMeterSide,
                                                                   VK_FORMAT_R32_SFLOAT, true)) &&
                                CreateMeterReadback();

        if (!meterReady)
            LOG_WARN("DLSS-NR Vulkan: no exposure meter; the white point stays on the slider");
'''
new_meter_alloc = r'''        // Automatic exposure uses the same 64x64 tile-mean grid as D3D12, then reduces it into
        // a one-pixel R32_FLOAT texture consumed directly by Encode/Resolve and by the NR feature.
        const bool meterReady =
            (g_vk.meter.Valid() ||
             CreateImage(g_vk.meter, kMeterSide, kMeterSide, VK_FORMAT_R32_SFLOAT, true)) &&
            (g_vk.autoExposure.Valid() || CreateImage(g_vk.autoExposure, 1, 1, VK_FORMAT_R32_SFLOAT, true)) &&
            CreateMeterReadback();

        if (!meterReady)
            LOG_WARN("DLSS-NR Vulkan: no exposure meter; automatic exposure is unavailable");
'''
s = replace_once(s, old_meter_alloc, new_meter_alloc, "Vulkan automatic exposure allocation")

s = replace_once(
    s,
    "    if (cfg.DlssNrWhitePointSource.value_or_default() == 1 && g_vk.gameExposure > 1e-6f)\n"
    "    {\n"
    "        const float trim = std::clamp(cfg.DlssNrWhitePointTrim.value_or_default(), 0.25f, 50.0f);\n"
    "        whitePoint = std::clamp(g_vk.gamePreExposure / g_vk.gameExposure * trim, 0.01f, 4096.0f);\n"
    "    }\n",
    "    if (requestedWhitePointSource == 1 && g_vk.gameExposure > 1e-6f)\n"
    "    {\n"
    "        const float baseWhitePoint = g_vk.gamePreExposure / g_vk.gameExposure;\n"
    "        const auto anchors = ParseExposureTrimAnchorsVk(cfg.DlssNrGameExposureTrimAnchors.value_or_default());\n"
    "        const float trim = ExposureTrimForRuntimeVk(\n"
    "            baseWhitePoint, cfg.DlssNrWhitePointTrim.value_or_default(), anchors,\n"
    "            cfg.DlssNrGameExposureTrimPreview.value_or_default());\n"
    "        whitePoint = std::clamp(baseWhitePoint * trim, 0.01f, 4096.0f);\n"
    "    }\n",
    "Vulkan game Base White Point anchors",
)

encode_tail = r'''    encode.DebugScale = cfg.DlssNrWhitePointScale.value_or_default();
    encode.GuideWidth = guideWidth;
    encode.GuideHeight = guideHeight;
'''
encode_auto = r'''    encode.DebugScale = cfg.DlssNrWhitePointScale.value_or_default();
    encode.GuideWidth = guideWidth;
    encode.GuideHeight = guideHeight;

    const float framePreExposure =
        (std::isfinite(preExposure) && preExposure > 1e-6f) ? preExposure : 1.0f;
    encode.PreExposure = framePreExposure;
    encode.AutoExposureShadowProtection =
        std::clamp(cfg.DlssNrAutoExposureShadowProtection.value_or_default(), 0.0f, 100.0f);
    FillAutoExposureTrimConstantsVk(encode, cfg);
    encode.UseExposureWhitePoint = 0u;

    bool usingAutoExposure = false;

    // Same-frame automatic exposure: raw linear HDR -> 64x64 exact tile means -> 1x1 exposure.
    // The CPU readback recorded below is deliberately not consulted by Encode/Resolve.
    if (requestedWhitePointSource == 3 && linearHdr && g_vk.meter.Valid() && g_vk.autoExposure.Valid())
    {
        DlssNrConstants meterParams = encode;
        meterParams.Mode = DlssNrMode_Meter;
        meterParams.Width = kMeterSide;
        meterParams.Height = kMeterSide;
        meterParams.MeterCopiesExposure = 0u;

        Transition(cmdBuffer, g_vk.meter, VK_IMAGE_LAYOUT_GENERAL);

        if (g_vk.pass->Dispatch(cmdBuffer, meterParams, kMeterSide, kMeterSide,
                                colour->Resource.ImageViewInfo.ImageView, VK_NULL_HANDLE, VK_NULL_HANDLE,
                                VK_NULL_HANDLE, g_vk.meter.view, VK_NULL_HANDLE,
                                beforeSr ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL))
        {
            Transition(cmdBuffer, g_vk.meter, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            Transition(cmdBuffer, g_vk.autoExposure, VK_IMAGE_LAYOUT_GENERAL);

            DlssNrConstants exposureParams = encode;
            exposureParams.Mode = DlssNrMode_AutoExposure;
            exposureParams.Width = 1;
            exposureParams.Height = 1;
            exposureParams.PreExposure = framePreExposure;
            exposureParams.ExposureSourceWidth = width;
            exposureParams.ExposureSourceHeight = height;

            if (g_vk.pass->Dispatch(cmdBuffer, exposureParams, 1, 1, g_vk.meter.view, VK_NULL_HANDLE,
                                    VK_NULL_HANDLE, VK_NULL_HANDLE, g_vk.autoExposure.view, VK_NULL_HANDLE))
            {
                Transition(cmdBuffer, g_vk.autoExposure, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                usingAutoExposure = true;
                g_vk.autoExposureActive = true;
                encode.UseExposureWhitePoint = 1u;

                const unsigned long long slot = g_vk.meterFrames % kMeterSlots;
                if (g_vk.meterReadback[slot] != VK_NULL_HANDLE)
                {
                    g_vk.meterKind[slot] = 2u;
                    g_vk.meterPreExposure[slot] = framePreExposure;

                    Transition(cmdBuffer, g_vk.autoExposure, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

                    VkBufferImageCopy region {};
                    region.bufferOffset = 0;
                    region.bufferRowLength = 0;
                    region.bufferImageHeight = 0;
                    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                    region.imageOffset = { 0, 0, 0 };
                    region.imageExtent = { 1, 1, 1 };
                    vkCmdCopyImageToBuffer(cmdBuffer, g_vk.autoExposure.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                           g_vk.meterReadback[slot], 1, &region);
                    Transition(cmdBuffer, g_vk.autoExposure, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

                    VkBufferMemoryBarrier toHost {};
                    toHost.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                    toHost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                    toHost.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    toHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    toHost.buffer = g_vk.meterReadback[slot];
                    toHost.offset = 0;
                    toHost.size = kMeterBytes;
                    vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0,
                                         nullptr, 1, &toHost, 0, nullptr);

                    g_vk.meterFrames++;
                    g_vk.autoExposureFrames++;
                }
            }
        }
    }
'''
s = replace_once(s, encode_tail, encode_auto, "Vulkan same-frame automatic exposure dispatch")

if "colour->Resource.ImageViewInfo.ImageView,\n                             VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, g_vk.proxy.view" in s:
    s = replace_once(
        s,
        "    if (!g_vk.pass->Dispatch(cmdBuffer, encode, width, height, colour->Resource.ImageViewInfo.ImageView,\n"
        "                             VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, g_vk.proxy.view, g_vk.keep.view,\n"
        "                             beforeSr ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL))\n",
        "    if (!g_vk.pass->Dispatch(cmdBuffer, encode, width, height, colour->Resource.ImageViewInfo.ImageView,\n"
        "                             VK_NULL_HANDLE, VK_NULL_HANDLE,\n"
        "                             usingAutoExposure ? g_vk.autoExposure.view : VK_NULL_HANDLE, g_vk.proxy.view,\n"
        "                             g_vk.keep.view,\n"
        "                             beforeSr ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL))\n",
        "Vulkan encode automatic exposure binding multipass",
    )
else:
    s = replace_once(
        s,
        "    if (!g_vk.pass->Dispatch(cmdBuffer, encode, width, height, sourceView, VK_NULL_HANDLE, VK_NULL_HANDLE,\n"
        "                             VK_NULL_HANDLE, g_vk.proxy.view, g_vk.keep.view, encodeLayout))\n",
        "    if (!g_vk.pass->Dispatch(cmdBuffer, encode, width, height, sourceView, VK_NULL_HANDLE, VK_NULL_HANDLE,\n"
        "                             usingAutoExposure ? g_vk.autoExposure.view : VK_NULL_HANDLE, g_vk.proxy.view,\n"
        "                             g_vk.keep.view, encodeLayout))\n",
        "Vulkan encode automatic exposure binding",
    )
s = replace_once(
    s,
    "            meter.Width = kMeterSide;\n"
    "            meter.Height = kMeterSide;\n\n"
    "            Transition(cmdBuffer, g_vk.meter, VK_IMAGE_LAYOUT_GENERAL);\n",
    "            meter.Width = kMeterSide;\n"
    "            meter.Height = kMeterSide;\n"
    "            meter.MeterCopiesExposure = 1u;\n\n"
    "            g_vk.meterKind[slot] = 1u;\n"
    "            g_vk.meterPreExposure[slot] = g_vk.gamePreExposure;\n\n"
    "            Transition(cmdBuffer, g_vk.meter, VK_IMAGE_LAYOUT_GENERAL);\n",
    "Vulkan game exposure meter kind",
)

if "    int evaluated = 1;\n    for (unsigned int pass = 0; pass < passes; ++pass)\n" in s:
    s = replace_once(
        s,
        "    int evaluated = 1;\n"
        "    for (unsigned int pass = 0; pass < passes; ++pass)\n",
        "    NVSDK_NGX_Resource_VK* modelExposure = usingAutoExposure ? &g_vk.autoExposure.ngx : nullptr;\n"
        "    int evaluated = 1;\n"
        "    for (unsigned int pass = 0; pass < passes; ++pass)\n",
        "Vulkan model exposure declaration multipass",
    )
    s = replace_once(
        s,
        "            &input->ngx, depth, motion, &answer->ngx, workWidth, workHeight, guideWidth, guideHeight,\n",
        "            &input->ngx, depth, motion, modelExposure, &answer->ngx, workWidth, workHeight, guideWidth, guideHeight,\n",
        "Vulkan model ExposureTexture argument multipass",
    )
else:
    s = replace_once(
        s,
        "    Transition(cmdBuffer, g_vk.output, VK_IMAGE_LAYOUT_GENERAL);\n\n"
        "    const int evaluated = g_vk.evaluate(\n"
        "        (void*) cmdBuffer, g_vk.feature, g_vk.capabilityParams, &modelInput->ngx, depth, motion, &g_vk.output.ngx,\n",
        "    Transition(cmdBuffer, g_vk.output, VK_IMAGE_LAYOUT_GENERAL);\n\n"
        "    NVSDK_NGX_Resource_VK* modelExposure = usingAutoExposure ? &g_vk.autoExposure.ngx : nullptr;\n\n"
        "    const int evaluated = g_vk.evaluate(\n"
        "        (void*) cmdBuffer, g_vk.feature, g_vk.capabilityParams, &modelInput->ngx, depth, motion, modelExposure,\n"
        "        &g_vk.output.ngx,\n",
        "Vulkan model ExposureTexture argument",
    )
if "resolveProxy->view, resolveAnswer->view" in s:
    s = replace_once(
        s,
        "    if (!g_vk.pass->Dispatch(cmdBuffer, resolve, width, height, resolveProxy->view, resolveAnswer->view,\n"
        "                             g_vk.keep.view, VK_NULL_HANDLE,\n"
        "                             beforeSr ? g_vk.preColor.view : colour->Resource.ImageViewInfo.ImageView,\n"
        "                             VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))\n",
        "    if (!g_vk.pass->Dispatch(cmdBuffer, resolve, width, height, resolveProxy->view, resolveAnswer->view,\n"
        "                             g_vk.keep.view, usingAutoExposure ? g_vk.autoExposure.view : VK_NULL_HANDLE,\n"
        "                             beforeSr ? g_vk.preColor.view : colour->Resource.ImageViewInfo.ImageView,\n"
        "                             VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))\n",
        "Vulkan resolve automatic exposure binding multipass",
    )
else:
    s = replace_once(
        s,
        "    if (!g_vk.pass->Dispatch(cmdBuffer, resolve, width, height, modelInput->view, g_vk.output.view, g_vk.keep.view,\n"
        "                             VK_NULL_HANDLE, destView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))\n",
        "    if (!g_vk.pass->Dispatch(cmdBuffer, resolve, width, height, modelInput->view, g_vk.output.view, g_vk.keep.view,\n"
        "                             usingAutoExposure ? g_vk.autoExposure.view : VK_NULL_HANDLE, destView, VK_NULL_HANDLE,\n"
        "                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))\n",
        "Vulkan resolve automatic exposure binding",
    )
status_impl = (
    "ExposureStatus GameExposureStatusVk()\n"
    "{\n"
    "    std::lock_guard<std::mutex> lock(g_vkMutex);\n"
    "    ExposureStatus s {};\n"
    "    s.seenFrames = g_vk.meterFrames;\n"
    "    s.offeredNow = g_vk.exposureOffered;\n"
    "    s.everOffered = g_vk.exposureOffered || g_vk.gameExposure > 0.0f;\n"
    "    s.exposure = g_vk.gameExposure;\n"
    "    s.preExposure = g_vk.gamePreExposure;\n"
    "    return s;\n"
    "}\n\n"
    "ExposureStatus AutoExposureStatusVk()\n"
    "{\n"
    "    std::lock_guard<std::mutex> lock(g_vkMutex);\n"
    "    ExposureStatus s {};\n"
    "    s.seenFrames = g_vk.autoExposureFrames;\n"
    "    s.offeredNow = g_vk.autoExposureActive;\n"
    "    s.everOffered = g_vk.autoExposureFrames != 0;\n"
    "    s.exposure = g_vk.autoExposureValue;\n"
    "    s.preExposure = g_vk.autoExposurePreExposure;\n"
    "    return s;\n"
    "}\n\n"
)
if "bool StageCarriesTheModelVk()" in s:
    marker = "void ShutdownVk(bool deviceAlive)\n"
    s = replace_once(s, marker, status_impl + marker, "Vulkan exposure status implementations")
else:
    marker = "std::optional<double> LastGpuTimeVk() { return g_vk.lastGpuTime; }\n\n"
    s = replace_once(s, marker, marker + status_impl, "Vulkan exposure status implementations multipass")
s = replace_once(
    s,
    "        g_vk.meter = OwnedImage {};\n\n"
    "        for (int i = 0; i < 4; ++i)\n",
    "        g_vk.meter = OwnedImage {};\n"
    "        g_vk.autoExposure = OwnedImage {};\n\n"
    "        for (int i = 0; i < 4; ++i)\n",
    "Vulkan device-loss auto exposure image",
)
s = replace_once(
    s,
    "            g_vk.meterMapped[i] = nullptr;\n"
    "        }\n\n"
    "        g_vk.device = VK_NULL_HANDLE;\n",
    "            g_vk.meterMapped[i] = nullptr;\n"
    "            g_vk.meterKind[i] = 0u;\n"
    "            g_vk.meterPreExposure[i] = 1.0f;\n"
    "        }\n\n"
    "        g_vk.autoExposureValue = 0.0f;\n"
    "        g_vk.autoExposurePreExposure = 1.0f;\n"
    "        g_vk.autoExposureFrames = 0;\n"
    "        g_vk.autoExposureActive = false;\n"
    "        g_vk.exposureReadbackSource = 0;\n\n"
    "        g_vk.device = VK_NULL_HANDLE;\n",
    "Vulkan device-loss automatic exposure state",
)
s = replace_once(
    s,
    "    DestroyImage(g_vk.meter);\n"
    "    DestroyMeterReadback();\n",
    "    DestroyImage(g_vk.meter);\n"
    "    DestroyImage(g_vk.autoExposure);\n"
    "    DestroyMeterReadback();\n",
    "Vulkan normal teardown automatic exposure image",
)
s = replace_once(
    s,
    "    g_vk.timedFrames = 0;\n"
    "    g_vk.lastGpuTime.reset();\n\n"
    "    g_vk.device = VK_NULL_HANDLE;\n",
    "    g_vk.timedFrames = 0;\n"
    "    g_vk.lastGpuTime.reset();\n"
    "    g_vk.autoExposureValue = 0.0f;\n"
    "    g_vk.autoExposurePreExposure = 1.0f;\n"
    "    g_vk.autoExposureFrames = 0;\n"
    "    g_vk.autoExposureActive = false;\n"
    "    g_vk.exposureReadbackSource = 0;\n"
    "    for (uint32_t& kind : g_vk.meterKind)\n"
    "        kind = 0u;\n\n"
    "    g_vk.device = VK_NULL_HANDLE;\n",
    "Vulkan normal teardown automatic exposure state",
)
write(rel, s)

# -----------------------------------------------------------------------------
# Shared HLSL: on Vulkan binding 4 (gMotion) is the generated 1x1 exposure in Encode/Resolve.
# Keep the existing Multipass WhitePoint() untouched; only AutomaticExposureWhitePoint()
# changes its texture source between D3D12 and Vulkan.
# -----------------------------------------------------------------------------
rel = "OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl"
s = read(rel)
if "float AutomaticExposureWhitePoint()\n" in s:
    s = replace_once(
        s,
        "float AutomaticExposureWhitePoint()\n"
        "{\n"
        "#ifndef VK_MODE\n"
        "    const float exposure = gExposure.Load(int3(0, 0, 0)).r;\n",
        "float AutomaticExposureWhitePoint()\n"
        "{\n"
        "#ifdef VK_MODE\n"
        "    const float exposure = gMotion.Load(int3(0, 0, 0)).r;\n"
        "#else\n"
        "    const float exposure = gExposure.Load(int3(0, 0, 0)).r;\n"
        "#endif\n",
        "Vulkan AutomaticExposureWhitePoint exposure source",
    )
    s = replace_once(
        s,
        "    }\n"
        "#endif\n"
        "    return max(gWhitePoint, 1e-4);\n"
        "}\n\n"
        "float EffectiveWhitePoint()\n",
        "    }\n"
        "    return max(gWhitePoint, 1e-4);\n"
        "}\n\n"
        "float EffectiveWhitePoint()\n",
        "Vulkan AutomaticExposureWhitePoint guard removal",
    )
else:
    s = replace_once(
        s,
        "#ifndef VK_MODE\n"
        "    if (gUseExposureWhitePoint != 0)\n"
        "    {\n"
        "        const float exposure = gExposure.Load(int3(0, 0, 0)).r;\n",
        "    if (gUseExposureWhitePoint != 0)\n"
        "    {\n"
        "#ifdef VK_MODE\n"
        "        const float exposure = gMotion.Load(int3(0, 0, 0)).r;\n"
        "#else\n"
        "        const float exposure = gExposure.Load(int3(0, 0, 0)).r;\n"
        "#endif\n",
        "Vulkan EffectiveWhitePoint exposure source",
    )
    s = replace_once(
        s,
        "    }\n"
        "#endif\n\n"
        "    return fallback;\n"
        "}\n",
        "    }\n\n"
        "    return fallback;\n"
        "}\n",
        "Vulkan EffectiveWhitePoint guard removal",
    )
write(rel, s)

# -----------------------------------------------------------------------------
# Forwarder: selected exposure is a first-class Vulkan NGX input.
# -----------------------------------------------------------------------------
rel = "OptiScaler/dlssnr/forwarder/dlssnr_forwarder.cpp"
s = read(rel)
s = replace_once(
    s,
    "                                             void *color, void *depth, void *motion, void *output,\n",
    "                                             void *color, void *depth, void *motion, void *exposure, void *output,\n",
    "Vulkan forwarder evaluate exposure argument",
)
s = replace_once(
    s,
    "    setResourcePtr(capabilityParams, \"DLSSNR.MVec\", motion);\n"
    "    setResourcePtr(capabilityParams, \"DLSSNR.Output\", output);\n",
    "    setResourcePtr(capabilityParams, \"DLSSNR.MVec\", motion);\n"
    "    setResourcePtr(capabilityParams, \"ExposureTexture\", exposure);\n"
    "    setResourcePtr(capabilityParams, \"DLSSNR.Output\", output);\n",
    "Vulkan forwarder ExposureTexture",
)
write(rel, s)

# -----------------------------------------------------------------------------
# Menu: use Vulkan's own delayed status and remove the D3D12-only warning.
# -----------------------------------------------------------------------------
rel = "OptiScaler/dlssnr/DlssNr_Menu.cpp"
s = read(rel)
s = replace_once(
    s,
    "            const auto ex = DlssNr::GameExposureStatus();\n"
    "            const auto autoEx = DlssNr::AutoExposureStatus();\n"
    "            const bool vk = DlssNr::IsRunningVk();\n",
    "            const bool vk = DlssNr::IsRunningVk();\n"
    "            const auto ex = vk ? DlssNr::GameExposureStatusVk() : DlssNr::GameExposureStatus();\n"
    "            const auto autoEx = vk ? DlssNr::AutoExposureStatusVk() : DlssNr::AutoExposureStatus();\n",
    "menu Vulkan exposure status selection",
)
s = replace_once(
    s,
    "                if (vk)\n"
    "                    ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.25f, 1.0f),\n"
    "                                       \"Automatic exposure is currently available on D3D12 only.\");\n"
    "                else if (autoEx.exposure > 1e-8f)\n",
    "                if (autoEx.exposure > 1e-8f)\n",
    "menu remove Vulkan automatic-exposure warning",
)
s = replace_once(
    s,
    "            const auto gameStatus = DlssNr::GameExposureStatus();\n",
    "            const auto gameStatus = vk ? DlssNr::GameExposureStatusVk() : DlssNr::GameExposureStatus();\n",
    "menu game anchor Vulkan status",
)
s = replace_once(
    s,
    "            const auto autoStatus = DlssNr::AutoExposureStatus();\n",
    "            const auto autoStatus = vk ? DlssNr::AutoExposureStatusVk() : DlssNr::AutoExposureStatus();\n",
    "menu auto anchor Vulkan status",
)
s = s.replace("Automatic exposure is currently D3D12 only.",
              "Automatic exposure is available on D3D12 and Vulkan.")
write(rel, s)

print("DLSS-NR Vulkan Automatic exposure patch applied")
