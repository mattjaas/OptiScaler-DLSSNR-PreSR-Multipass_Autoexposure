// Approximate outward feather: max-reduce the post-contrast alpha, then depth-protected bilinear reconstruction.
// Full-resolution original alpha is always retained. No full-resolution neighbour search or weighted dilation.
#include "GhostDepth.hlsli"
cbuffer Params : register(b0)
{
    uint mode;
    float radius1080;
    uint width, height;
    float globalRawWeight, depthTolerance;
    uint inverted;
    float depthPrecision;
    uint3 reserved;
    uint step, fullHeight;
};
Texture2D<float4> Base : register(t0);
Texture2D<float2> Field : register(t1); // alpha and depth at the original maximum, before spatial enlargement
Texture2D<float4> Proxy : register(t2);
Texture2D<float> Depth : register(t3);
Texture2D<float4> Answer : register(t4);
#if DLSSNR_GHOST_FEATHER_STAGE == 0
RWTexture2D<float2> Output : register(u0);
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
    uint blockSize = clamp(step, 1u, 128u);
#if DLSSNR_GHOST_FEATHER_STAGE == 0
    uint fw, fh;
    Base.GetDimensions(fw, fh);
    float maximum = 0;
    uint2 chosen = uint2(p) * blockSize;
    [loop] for (uint y = 0; y < blockSize; ++y)[loop] for (uint x = 0; x < blockSize; ++x)
    {
        uint2 q = id.xy * blockSize + uint2(x, y);
        if (q.x >= fw || q.y >= fh)
            continue;
        float a = Mask(Base.Load(int3(q, 0)).a);
        if (a > maximum)
        {
            maximum = a;
            chosen = q;
        }
    }
    // One depth read for the winning raw sample; carry that depth all the way to final composition.
    float z = maximum > 0 ? GhostNearDepth(Depth.Load(int3(chosen, 0)), inverted) : -1;
    Output[p] = z > 0 ? float2(maximum, z) : 0;
#else
#ifdef DLSSNR_GHOST_FEATHER_INPLACE
    // Every thread owns one output pixel; neighbours are read only from the completed immutable small field.
    float2 posEarly = (float2(p) + .5) / float(blockSize) - .5;
    int2 cornerEarly = int2(floor(posEarly));
    uint ew, eh;
    Field.GetDimensions(ew, eh);
    float maximum = 0;
    [unroll] for (int y = 0; y < 2; ++y)[unroll] for (int x = 0; x < 2; ++x) maximum =
        max(maximum, Mask(Field.Load(int3(clamp(cornerEarly + int2(x, y), int2(0, 0), int2(ew, eh) - 1), 0)).x));
    if (maximum <= 0)
        return;
    float4 base = Output[p];
#else
    float4 base = Base.Load(int3(p, 0));
#endif
    float original = Mask(base.a), expanded = original;
    float z = GhostNearDepth(Depth.Load(int3(p, 0)), inverted);
    float tolerance = isfinite(depthTolerance) ? clamp(depthTolerance, .001, .5) : .05;
    float precision = isfinite(depthPrecision) && depthPrecision >= 0 && depthPrecision <= .001
                          ? max(depthPrecision * 2, 1e-6)
                          : 2.0 / 65535;
    [branch] if (isfinite(radius1080) && radius1080 > 0 && blockSize > 1 && z > 0 && original < 1)
    {
        float2 pos = (float2(p) + .5) / float(blockSize) - .5;
        int2 corner = int2(floor(pos));
        float2 f = frac(pos);
        uint fw, fh;
        Field.GetDimensions(fw, fh);
        float sum = 0;
        [unroll] for (int y = 0; y < 2; ++y)[unroll] for (int x = 0; x < 2; ++x)
        {
            int2 q = clamp(corner + int2(x, y), int2(0, 0), int2(fw, fh) - 1);
            float2 sample = Field.Load(int3(q, 0));
            if (sample.y > 0 && z <= sample.y * (1 + tolerance) + precision)
                sum += Mask(sample.x) * (x ? f.x : 1 - f.x) * (y ? f.y : 1 - f.y);
        }
        expanded = max(original, UpwardHalf(saturate(sum)));
    }
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
#ifdef DLSSNR_GHOST_FEATHER_INPLACE
    if (expanded <= original)
        return;
#endif
    Output[p] = float4(base.rgb, expanded);
#endif
}
