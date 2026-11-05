float3 RejectTemporalGhost(float3 filtered, int2 pixel, out float mask)
{
    mask = 0;
    float depth = gMotion.Load(int3(pixel, 0)).x;
    float nearZ = isfinite(depth) && depth >= 0 && depth <= 1 ? (gPassthrough != 0 ? depth : 1 - depth) : 0;
    if (nearZ <= 0)
        return filtered;
    uint w, h;
    gAux2.GetDimensions(w, h);
    if (w == 0 || h == 0)
        return filtered;
    float2 pos = (float2(pixel) + .5) * float2(w, h) / float2(gWidth, gHeight) - .5;
    int2 base = int2(floor(pos));
    float2 f = frac(pos);
    float age = 0, weight = 0;
    float tolerance = isfinite(gExposureAnchors[2].z) ? clamp(gExposureAnchors[2].z, .001, .5) : .05;
    float precision = isfinite(gExposureAnchors[1].w) ? max(1e-6, gExposureAnchors[1].w * 2) : 2.0 / 65535;
    [unroll] for (int y = 0; y < 2; ++y)[unroll] for (int x = 0; x < 2; ++x)
    {
        int2 q = clamp(base + int2(x, y), int2(0, 0), int2(w, h) - 1);
        float4 state = gAux2.Load(int3(q, 0));
        if (!all(isfinite(state)) || state.x <= 0)
            continue;
        if (abs(state.x - nearZ) > max(state.x, nearZ) * tolerance + precision)
            continue;
        float b = (x ? f.x : 1 - f.x) * (y ? f.y : 1 - f.y);
        age += max(0, state.y) * b;
        weight += b;
    }
    if (weight <= 1e-6)
        return filtered;
    age /= weight;
    if (age <= 0)
        return filtered;
    float fade = isfinite(gExposureAnchors[2].y) ? clamp(gExposureAnchors[2].y, 0, 120) : 3;
    float strength = isfinite(gCompareSplit) ? saturate(gCompareSplit) : 1;
    mask = strength * (fade > 0 ? saturate(age / fade) : 1);
#ifdef DLSSNR_GHOST_CONTRAST
    mask = GhostContrastMask(pixel, mask);
#endif
    float3 raw = GhostRawEdit(pixel);
    return mask >= 1 ? raw : lerp(filtered, raw, mask);
}
