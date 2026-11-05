#include "GhostDepth.hlsli"
// Optional fast blend of the unchanged compact visibility history.
// FP32 dot products replace sequential FP16 sums: tiny rounding differences are intentional.
// The production history is exactly ceil(work/2); even grids have exact quarter-texel tents.
#include "CompactGhostState.hlsli"
Texture2D<uint2> gCompactGhost : register(t5);
#ifdef DLSSNR_COMPACT_FUSED_DECODE
#define COMPACT_UNMASKED 0
float CompactGhostWeight(int2 pixel)
{
    float mask = 0;
#else
#define COMPACT_UNMASKED filtered
float3 RejectCompactGhost(float3 filtered, int2 pixel, out float mask)
{
    mask = 0;
#endif
    float depth = gMotion.Load(int3(pixel, 0)).x;
    float nearZ = GhostNearDepth(depth, gPassthrough);
    if (nearZ <= 0)
        return COMPACT_UNMASKED;
    uint w = (gWidth + 1) / 2, h = (gHeight + 1) / 2;
    int2 base = (pixel - 1) >> 1;
    float2 f = float2((pixel.x & 1) != 0 ? .25 : .75, (pixel.y & 1) != 0 ? .25 : .75);
    if (((gWidth | gHeight) & 1u) != 0u)
    {
        // ceil(N/2)/N = 1/2 + 1/(2N) for odd N. Keep the small correction
        // separate from the integer coordinate to avoid cancellation on wide odd grids.
        f += (float2(pixel) + .5) * (float2(gWidth & 1u, gHeight & 1u) * .5 / float2(gWidth, gHeight));
        int2 carry = int2(floor(f));
        base += carry;
        f -= float2(carry);
    }
    float maskSum = 0, weight = 0, expanded = 0;
    float tolerance = isfinite(gExposureAnchors[2].z) ? clamp(gExposureAnchors[2].z, .001, .5) : .05;
    float precision = isfinite(gExposureAnchors[1].w) && gExposureAnchors[1].w >= 0 && gExposureAnchors[1].w <= .001
                          ? max(1e-6, gExposureAnchors[1].w * 2)
                          : 2.0 / 65535;
    float fade = isfinite(gExposureAnchors[2].y) ? clamp(gExposureAnchors[2].y, 0, 120) : 3;
    // Gather at the centre of the known footprint, independent of sampler subtexel precision.
    // Gather order is BL, BR, TR, TL; restore the original TL, TR, BL, BR accumulation order.
    uint4 footprint = gCompactGhost.GatherRed(gLinear, (float2(base) + 1.0) / float2(w, h)).wzxy;
    float4 z = asfloat(footprint & 0xffffff80u);
    float4 margin = max(precision, max(z, nearZ) * (1.0 / 65536.0));
    bool4 valid = isfinite(z) & (z > 0) & (abs(z - nearZ) <= max(z, nearZ) * tolerance + margin);
    float4 ages = float4((footprint & 127u) * uint4(valid));
    float4 vx = float4(1 - f.x, f.x, 1 - f.x, f.x);
    float4 vy = float4(1 - f.y, 1 - f.y, f.y, f.y);
    float4 weights = (vx * vy) * float4(valid);
    maskSum = dot(ages, weights);
    weight = dot(weights, float4(1, 1, 1, 1));
    float4 broadened = ages * min(vx, vy);
    expanded = max(max(broadened.x, broadened.y), max(broadened.z, broadened.w));
    if (weight <= 1e-6 || maskSum <= 0)
        return COMPACT_UNMASKED;
    float baseMask = maskSum / weight;
    float expansion = isfinite(gExposureAnchors[2].w) ? saturate(gExposureAnchors[2].w) : .5;
    // The minimum of the two linear tents broadens corners without another texture read.
    // Support ends at the same zero boundary, so no screen-aligned hard dilation edge appears.
    float age = max(baseMask, lerp(baseMask, expanded, expansion));
    float strength = isfinite(gCompareSplit) ? saturate(gCompareSplit) : 1;
    mask = strength * (fade > 0 ? saturate(age / fade) : 1);
#ifdef DLSSNR_GHOST_CONTRAST
    mask = GhostContrastMask(pixel, mask);
#endif
#ifdef DLSSNR_COMPACT_FUSED_DECODE
    return mask;
#else
    float3 raw = GhostRawEdit(pixel);
    return mask >= 1 ? raw : lerp(filtered, raw, mask);
#endif
}
#undef COMPACT_UNMASKED
