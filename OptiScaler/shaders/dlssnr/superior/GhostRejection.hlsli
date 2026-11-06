// Included only in the optional carrier-decode PSO. All values are restored, signed edit units.
float3 GhostRawEdit(int2 pixel)
{
    const int3 p = int3(clamp(pixel, int2(0, 0), int2(gWidth, gHeight) - 1), 0);
    const float3 proxy = SanitizeFinite3(gOriginal.Load(p).rgb, 0.0);
#ifdef DLSSNR_EDIT_TRANSPORT
    return AffineRawEdit(proxy, SanitizeFinite3(gAux.Load(p).rgb, proxy));
#else
    return SanitizeFinite3(gAux.Load(p).rgb, proxy) - proxy;
#endif
}
float3 RejectUnsupportedEdit(float3 filtered, int2 pixel, out float mask)
{
    const float3 raw = GhostRawEdit(pixel);
    float3 low = raw, high = raw;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
            if (x != 0 || y != 0)
            {
                const float3 edit = GhostRawEdit(pixel + int2(x, y));
                low = min(low, edit);
                high = max(high, edit);
            }
    const float3 outside = max(max(low - filtered, filtered - high), 0.0);
    const float distance = max(outside.x, max(outside.y, outside.z));
    const float threshold = isfinite(gColourStrength) && gColourStrength >= 0.0 && gColourStrength <= 0.25
        ? gColourStrength : 0.01;
    const float softness = isfinite(gTransferStrength) && gTransferStrength >= 0.0001 && gTransferStrength <= 0.25
        ? gTransferStrength : 0.02;
    const float strength = isfinite(gCompareSplit) ? saturate(gCompareSplit) : 1.0;
    mask = strength * smoothstep(threshold, threshold + softness, distance);
    // One RGB weight avoids independently replacing channels and introducing colour fringes.
    return mask >= 1.0 ? raw : lerp(filtered, raw, mask);
}
