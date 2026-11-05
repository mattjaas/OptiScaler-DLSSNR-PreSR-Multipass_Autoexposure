#include "GhostDepth.hlsli"
// This intentionally permits a wider mask across depth boundaries after MV/depth visibility detection.
// The quarter-grid history is approximate; expansion cannot guarantee a superset of the fine history.
Texture2D<float> gCoarseGhost : register(t5);
float3 RejectCompactGhost(float3 filtered, int2 pixel, out float mask)
{
    mask = 0;
    float depth = gMotion.Load(int3(pixel, 0)).x;
    float nearZ = GhostNearDepth(depth, gPassthrough);
    if (nearZ <= 0)
        return filtered;
    float field = gCoarseGhost.SampleLevel(gLinear, (float2(pixel) + .5) / float2(gWidth, gHeight), 0);
    float strength = isfinite(gCompareSplit) ? saturate(gCompareSplit) : 1;
    mask = isfinite(field) ? strength * saturate(field) : 0;
    if (mask <= 0)
        return filtered;
#ifdef DLSSNR_GHOST_CONTRAST
    mask = GhostContrastMask(pixel, mask);
#endif
    float3 raw = GhostRawEdit(pixel);
    return mask >= 1 ? raw : lerp(filtered, raw, mask);
}
