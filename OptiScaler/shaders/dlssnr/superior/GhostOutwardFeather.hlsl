// Optional post-decode operation: contrast cancellation has already written the mask to alpha.
// Two separable weighted maxima preserve every existing mask sample and add only a soft fringe.
#include "GhostDepth.hlsli"
cbuffer Params : register(b0)
{
    uint mode;
    float radius1080;
    uint width, height;
    float globalRawWeight, depthTolerance;
    uint inverted;
    float depthPrecision;
};
Texture2D<float4> Base : register(t0);
Texture2D<float> Horizontal : register(t1);
Texture2D<float4> Proxy : register(t2);
Texture2D<float> Depth : register(t3);
Texture2D<float4> Answer : register(t4);
#if DLSSNR_GHOST_FEATHER_STAGE == 0
RWTexture2D<float> Output : register(u0);
#else
RWTexture2D<float4> Output : register(u0);
#endif
float Mask(float v) { return isfinite(v) ? saturate(v) : 0; }
float UpwardHalf(float v)
{
    uint bits = f32tof16(v);
    return f16tof32(bits + uint(f16tof32(bits) < v));
}
[numthreads(8, 8, 1)] void GhostOutwardFeather(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height)
        return;
    int2 p = int2(id.xy);
    float4 base = Base.Load(int3(p, 0));
    float original = Mask(base.a), expanded = original;
    float radius = isfinite(radius1080) && radius1080 > 0 ? min(radius1080 * float(height) / 1080, 128) : 0;
    float z = GhostNearDepth(Depth.Load(int3(p, 0)), inverted);
    float tolerance = isfinite(depthTolerance) ? clamp(depthTolerance, .001, .5) : .05;
    float precision = isfinite(depthPrecision) && depthPrecision >= 0 && depthPrecision <= .001
                          ? max(depthPrecision * 2, 1e-6)
                          : 2.0 / 65535;
    // No new fringe on invalid depth. A closer target never inherits a farther surface's mask.
    [branch] if (radius > 0 && z > 0 && original < 1)
    {
        int extent = int(ceil(radius));
        [loop] for (int offset = -extent; offset <= extent; ++offset)
        {
#if DLSSNR_GHOST_FEATHER_STAGE == 0
            int2 q = p + int2(offset, 0);
#else
            int2 q = p + int2(0, offset);
#endif
            if (any(q < 0) || q.x >= int(width) || q.y >= int(height))
                continue;
#if DLSSNR_GHOST_FEATHER_STAGE == 0
            float mask = Mask(Base.Load(int3(q, 0)).a);
#else
            float mask = Mask(Horizontal.Load(int3(q, 0)));
#endif
            float candidate = mask * max(0, 1 - abs(float(offset)) / (radius + 1));
            [branch] if (candidate > expanded)
            {
                float sourceZ = GhostNearDepth(Depth.Load(int3(q, 0)), inverted);
                if (sourceZ > 0 && z <= sourceZ * (1 + tolerance) + precision)
                    expanded = candidate;
            }
        }
    }
    expanded = max(original, UpwardHalf(expanded));
#if DLSSNR_GHOST_FEATHER_STAGE == 0
    Output[p] = expanded;
#else
    float g = isfinite(globalRawWeight) ? saturate(globalRawWeight) : 0;
    expanded = max(original, min(expanded, 1 - g));
    // Base RGB already contains g + original of raw edit. Add only the extra rejection fraction.
    [branch] if (expanded > original)
    {
        float3 proxy = Proxy.Load(int3(p, 0)).rgb, answer = Answer.Load(int3(p, 0)).rgb;
        proxy =
            float3(isfinite(proxy.r) ? proxy.r : 0, isfinite(proxy.g) ? proxy.g : 0, isfinite(proxy.b) ? proxy.b : 0);
        answer = float3(isfinite(answer.r) ? answer.r : proxy.r, isfinite(answer.g) ? answer.g : proxy.g,
                        isfinite(answer.b) ? answer.b : proxy.b);
        float3 raw = clamp(answer - proxy, -65504, 65504);
        float weight = saturate((expanded - original) / max(1e-6, 1 - g - original));
        base.rgb = lerp(base.rgb, raw, weight);
    }
    Output[p] = float4(base.rgb, expanded);
#endif
}
