#include "CompactGhostState.hlsli"
Texture2D<uint2> gCompactGhost : register(t5);
typedef float16_t MaskScalar;
typedef vector<float16_t, 2> MaskPair;
float3 RejectCompactGhost(float3 filtered, int2 pixel, out float mask)
{
    mask = 0;
    float depth = gMotion.Load(int3(pixel, 0)).x;
    float nearZ = isfinite(depth) && depth >= 0 && depth <= 1 ? (gPassthrough != 0 ? depth : 1 - depth) : 0;
    if (nearZ <= 0)
        return filtered;
    uint w, h;
    gCompactGhost.GetDimensions(w, h);
    if (w == 0 || h == 0)
        return filtered;
    float2 pos = (float2(pixel) + .5) * float2(w, h) / float2(gWidth, gHeight) - .5;
    int2 base = int2(floor(pos));
    float2 f = frac(pos);
    MaskScalar maskSum = 0, weight = 0, expanded = 0;
    float tolerance = isfinite(gExposureAnchors[2].z) ? clamp(gExposureAnchors[2].z, .001, .5) : .05;
    float precision = isfinite(gExposureAnchors[1].w) && gExposureAnchors[1].w >= 0 && gExposureAnchors[1].w <= .001
                          ? max(1e-6, gExposureAnchors[1].w * 2)
                          : 2.0 / 65535;
    float fade = isfinite(gExposureAnchors[2].y) ? clamp(gExposureAnchors[2].y, 0, 120) : 3;
    // Gather at the centre of the known footprint, independent of sampler subtexel precision.
    // Gather order is BL, BR, TR, TL; restore the original TL, TR, BL, BR accumulation order.
    uint4 footprint = gCompactGhost.GatherRed(gLinear, (float2(base) + 1.0) / float2(w, h)).wzxy;
    [unroll] for (int y = 0; y < 2; ++y)[unroll] for (int x = 0; x < 2; ++x)
    {
        uint packed = footprint[y * 2 + x];
        float z = asfloat(packed & 0xffffff80u), age = float(packed & 127u);
        if (!isfinite(z) || z <= 0)
            continue;
        float margin = max(precision, max(z, nearZ) * (1.0 / 65536.0));
        if (abs(z - nearZ) > max(z, nearZ) * tolerance + margin)
            continue;
        MaskPair tent = MaskPair(x ? f.x : 1 - f.x, y ? f.y : 1 - f.y);
        MaskScalar b = tent.x * tent.y;
        MaskScalar a = MaskScalar(age);
        maskSum += a * b;
        weight += b;
        expanded = max(expanded, a * min(tent.x, tent.y));
    }
    if (weight <= 1e-6 || maskSum <= 0)
        return filtered;
    float baseMask = maskSum / weight;
    float expansion = isfinite(gExposureAnchors[2].w) ? saturate(gExposureAnchors[2].w) : .5;
    // The minimum of the two linear tents broadens corners without another texture read.
    // Support ends at the same zero boundary, so no screen-aligned hard dilation edge appears.
    float age = max(baseMask, lerp(baseMask, expanded, expansion));
    float strength = isfinite(gCompareSplit) ? saturate(gCompareSplit) : 1;
    mask = strength * (fade > 0 ? saturate(age / fade) : 1);
    float3 raw = GhostRawEdit(pixel);
    return mask >= 1 ? raw : lerp(filtered, raw, mask);
}
