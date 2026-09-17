from pathlib import Path
import sys


def replace_labeled_call(script: str, label: str, replacement: str) -> str:
    marker = f'"{label}",'
    idx = script.find(marker)
    if idx < 0:
        raise RuntimeError(f"{label}: label not found in patcher")
    start = script.rfind("s = replace_once(", 0, idx)
    if start < 0:
        raise RuntimeError(f"{label}: replace_once start not found")
    end = script.find("\n)", idx)
    if end < 0:
        raise RuntimeError(f"{label}: replace_once end not found")
    end += 2
    return script[:start] + replacement.rstrip() + script[end:]


def replace_needle_block(script: str, label: str, replacement: str) -> str:
    marker = f'"{label}")'
    idx = script.find(marker)
    if idx < 0:
        raise RuntimeError(f"{label}: call not found in patcher")
    call_start = script.rfind("s = replace_once(", 0, idx)
    start = script.rfind("needle = '''", 0, call_start)
    if start < 0:
        raise RuntimeError(f"{label}: needle start not found")
    end = script.find("\n", idx)
    if end < 0:
        end = len(script)
    else:
        end += 1
    return script[:start] + replacement.rstrip() + "\n" + script[end:]


def insert_after_labeled_call(script: str, label: str, addition: str) -> str:
    marker = f'"{label}",'
    idx = script.find(marker)
    if idx < 0:
        raise RuntimeError(f"{label}: label not found for insertion")
    end = script.find("\n)", idx)
    if end < 0:
        raise RuntimeError(f"{label}: call end not found for insertion")
    end += 2
    return script[:end] + "\n" + addition.rstrip() + script[end:]


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: adapt_autoexposure_patchers_for_presr_exposure_path.py <patch-tools-dir>")

    path = Path(sys.argv[1]).resolve() / "apply_dlssnr_autoexposure.py"
    script = path.read_text(encoding="utf-8")

    # The target's meter block has no source-fork comment after Width/Height. The
    # flag itself is still required and can be anchored on those two assignments.
    script = replace_labeled_call(
        script,
        "meter copy flag",
        '''s = replace_once(
    s,
    "        meterParams.Width = 1;\\n"
    "        meterParams.Height = 1;\\n",
    "        meterParams.Width = 1;\\n"
    "        meterParams.Height = 1;\\n"
    "        meterParams.MeterCopiesExposure = 1;\\n",
    "meter copy flag",
)''',
    )

    # PreSR/Multipass already tracks the active colour resource and its exact state.
    # Generate the automatic exposure from that resource, then preserve the source
    # patcher's variable spellings so its later source-selection rewrite still turns
    # game/automatic exposure into mutually exclusive choices.
    script = replace_needle_block(
        script,
        "auto exposure dispatch",
        '''needle = ''' + "'''" + '''    g_nr.gamePreExposure = frame.PreExposure;

    float whitePoint = frame.WhitePointOverride > 0.0f ? frame.WhitePointOverride : ResolveWhitePoint(cfg, isHdrBuffer);
''' + "'''" + '''
replacement = ''' + "'''" + '''    g_nr.gamePreExposure = frame.PreExposure;

    ID3D12Resource* nrExposure = (ID3D12Resource*) frame.ExposureTexture;
    const bool nrExposureIsGame = nrExposure != nullptr;
    const D3D12_RESOURCE_STATES exposureArrival =
        cfg.ExposureResourceBarrier.has_value()
            ? (D3D12_RESOURCE_STATES) cfg.ExposureResourceBarrier.value()
            : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    if (g_nr.setExposure != nullptr && nrExposure == nullptr && isHdrBuffer &&
        g_nr.meter != nullptr && g_nr.autoExposure != nullptr)
    {
        DlssNrConstants meterParams {};
        meterParams.Mode = DlssNrMode_Meter;
        meterParams.Width = kDlssNrMeterGrid;
        meterParams.Height = kDlssNrMeterGrid;
        meterParams.MeterCopiesExposure = 0;

        const D3D12_RESOURCE_STATES priorTargetState = targetState;
        TransitionTarget(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        DispatchPass(cmdList, meterParams, target, nullptr, nullptr, nullptr, nullptr, g_nr.meter, nullptr);
        TransitionTarget(priorTargetState);

        Barrier(cmdList, g_nr.meter, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        if (g_nr.autoExposureReadable)
            Barrier(cmdList, g_nr.autoExposure, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        DlssNrConstants exposureParams {};
        exposureParams.Mode = DlssNrMode_AutoExposure;
        exposureParams.Width = 1;
        exposureParams.Height = 1;
        exposureParams.PreExposure = frame.PreExposure;
        const D3D12_RESOURCE_DESC exposureSourceDesc = target->GetDesc();
        exposureParams.ExposureSourceWidth = (uint32_t) exposureSourceDesc.Width;
        exposureParams.ExposureSourceHeight = exposureSourceDesc.Height;

        DispatchPass(cmdList, exposureParams, g_nr.meter, nullptr, nullptr, nullptr, nullptr,
                     g_nr.autoExposure, nullptr);

        Barrier(cmdList, g_nr.meter, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier(cmdList, g_nr.autoExposure, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        g_nr.autoExposureReadable = true;
        nrExposure = g_nr.autoExposure;
    }

    if (g_nr.setExposure != nullptr)
        g_nr.setExposure(g_nr.capabilityParams, nrExposure);

    float whitePoint = frame.WhitePointOverride > 0.0f ? frame.WhitePointOverride : ResolveWhitePoint(cfg, isHdrBuffer);
''' + "'''" + '''
s = replace_once(s, needle, replacement, "auto exposure dispatch")''',
    )

    # The target already binds t4 for its zero-latency game exposure. Once the
    # source patch has decided that automatic exposure is active, initialise that
    # same binding to the generated 1x1 texture; source 1's existing block then
    # overrides it with the game's texture.
    script = insert_after_labeled_call(
        script,
        "v5 auto exposure active flag",
        '''s = replace_once(
    s,
    "    ID3D12Resource* exposureTex = nullptr;\\n",
    "    ID3D12Resource* exposureTex = usingAutoExposure ? g_nr.autoExposure : nullptr;\\n",
    "PreSR live exposure binding for automatic source",
)''',
    )

    # t4 and WhitePoint() already exist in this fork for the live game-exposure
    # path. Reuse them instead of declaring a second t4 or a competing white-point
    # function. Game exposure keeps its existing zero-latency formula; automatic
    # exposure uses the new source patch's PreExposure/Trim constants.
    script = replace_labeled_call(
        script,
        "v5 exposure SRV",
        '''s = replace_once(
    s,
    "Texture2D<float4>   gExposure : register(t4);\\n",
    "Texture2D<float4>   gExposure : register(t4);\\n",
    "v5 exposure SRV",
)''',
    )

    target_whitepoint = r'''float WhitePoint()
{
#ifndef VK_MODE
    if (gUseGameExposure != 0)
    {
        float e = gExposure.Load(int3(0, 0, 0)).r;
        if (e > 1e-6 && e < 1e6)
            return clamp(gExposurePreMul / e, 0.01, 4096.0);
        // A missing or absurd sample falls through to the CPU value the meter path still maintains.
    }
#endif
    return max(gWhitePoint, 1e-4);
}
'''
    merged_whitepoint = r'''float WhitePoint()
{
#ifndef VK_MODE
    const float e = gExposure.Load(int3(0, 0, 0)).r;
    if (e > 1e-6 && e < 1e6)
    {
        if (gUseGameExposure != 0)
            return clamp(gExposurePreMul / e, 0.01, 4096.0);

        if (gUseExposureWhitePoint != 0)
        {
            const float preExposure =
                (isfinite(gPreExposure) && gPreExposure > 1e-6) ? gPreExposure : 1.0;
            const float trim = clamp(gExposureTrim, 0.25, 10.0);
            return clamp(preExposure / e * trim, 0.01, 4096.0);
        }
    }
#endif
    return max(gWhitePoint, 1e-4);
}
'''
    script = replace_labeled_call(
        script,
        "v5 effective white point",
        f'''s = replace_once(\n    s,\n    {target_whitepoint!r},\n    {merged_whitepoint!r},\n    "v5 effective white point",\n)''',
    )

    script = replace_labeled_call(
        script,
        "v5 encode white point",
        '''s = replace_once(
    s,
    "        float3 normalized = frame / WhitePoint();\\n",
    "        float3 normalized = frame / WhitePoint();\\n",
    "v5 encode white point",
)''',
    )
    script = replace_labeled_call(
        script,
        "v5 resolve white point",
        '''s = replace_once(
    s,
    "    const float normScale = gPassthrough != 0 ? 1.0 : WhitePoint();\\n",
    "    const float normScale = gPassthrough != 0 ? 1.0 : WhitePoint();\\n",
    "v5 resolve white point",
)''',
    )
    script = replace_labeled_call(
        script,
        "v5 comparison divider white point",
        '''s = replace_once(
    s,
    "        result = float3(WhitePoint(), WhitePoint(), WhitePoint());\\n",
    "        result = float3(WhitePoint(), WhitePoint(), WhitePoint());\\n",
    "v5 comparison divider white point",
)''',
    )

    # PreSR/Multipass already passes exposureTex as DispatchPass's fifth SRV for
    # both encode and resolve. With exposureTex initialised above for source 2,
    # changing these calls again would regress the fork's existing source-1 path.
    script = replace_labeled_call(
        script,
        "v5 encode exposure SRV",
        '''s = replace_once(
    s,
    "    DispatchPass(cmdList, encodeParams, target, nullptr, nullptr, nullptr, exposureTex,\\n"
    "                        g_nr.colorCopy, g_nr.hdrCopy);\\n",
    "    DispatchPass(cmdList, encodeParams, target, nullptr, nullptr, nullptr, exposureTex,\\n"
    "                        g_nr.colorCopy, g_nr.hdrCopy);\\n",
    "v5 encode exposure SRV",
)''',
    )
    script = replace_labeled_call(
        script,
        "v5 resolve exposure SRV",
        '''s = replace_once(
    s,
    "        const bool resolved = DispatchPass(cmdList, resolveParams, resolveProxy, resolveAnswer,\\n"
    "                                           resolveOriginal, motionIn, exposureTex, resolveTarget, nullptr);\\n",
    "        const bool resolved = DispatchPass(cmdList, resolveParams, resolveProxy, resolveAnswer,\\n"
    "                                           resolveOriginal, motionIn, exposureTex, resolveTarget, nullptr);\\n",
    "v5 resolve exposure SRV",
)''',
    )

    path.write_text(script, encoding="utf-8", newline="\n")
    print(f"Merged source AutoExposure patch with PreSR live exposure path: {path}")


if __name__ == "__main__":
    main()
