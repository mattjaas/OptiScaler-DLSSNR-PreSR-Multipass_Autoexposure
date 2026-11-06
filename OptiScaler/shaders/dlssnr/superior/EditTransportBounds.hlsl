// Exact all-pixel/all-RGB bounds on the working edit. Never sampled, read back or synchronized to the CPU.
cbuffer Params : register(b0)
{
    uint mode;
    float requestedGain;
    uint width, height;
    float unused0, unused1;
    uint selection;
};
#if DLSSNR_TRANSPORT_BOUNDS_STAGE == 0
Texture2D<float4> Proxy : register(t0);
Texture2D<float4> Answer : register(t1);
RWTexture2D<float2> Output : register(u0);
#else
Texture2D<float2> Parts : register(t0);
RWByteAddressBuffer Output : register(u0); // 256-byte default-heap buffer, subsequently bound as b1
#endif
groupshared float2 Bounds[64];
float3 Finite(float3 x, float3 fallback)
{
    return float3(isfinite(x.r) ? x.r : fallback.r, isfinite(x.g) ? x.g : fallback.g, isfinite(x.b) ? x.b : fallback.b);
}
[numthreads(64, 1, 1)] void EditTransportBounds(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    float lo = 3.402823466e38, hi = -3.402823466e38;
#if DLSSNR_TRANSPORT_BOUNDS_STAGE == 0
    uint2 start = group.xy * 32 + uint2(lane & 7, lane >> 3) * 4;
    [unroll] for (uint y = 0; y < 4; ++y)[unroll] for (uint x = 0; x < 4; ++x)
    {
        uint2 p = start + uint2(x, y);
        if (p.x >= width || p.y >= height)
            continue;
        float3 proxy = Finite(Proxy.Load(int3(p, 0)).rgb, 0), answer = Finite(Answer.Load(int3(p, 0)).rgb, proxy);
        float3 edit = Finite(answer - proxy, 0);
        lo = min(lo, min(edit.r, min(edit.g, edit.b)));
        hi = max(hi, max(edit.r, max(edit.g, edit.b)));
    }
#else
    for (uint k = lane; k < width * height; k += 64)
    {
        float2 b = Parts.Load(int3(k % width, k / width, 0));
        lo = min(lo, b.x);
        hi = max(hi, b.y);
    }
#endif
    Bounds[lane] = float2(lo, hi);
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint stride = 32; stride > 0; stride >>= 1)
    {
        if (lane < stride)
            Bounds[lane] =
                float2(min(Bounds[lane].x, Bounds[lane + stride].x), max(Bounds[lane].y, Bounds[lane + stride].y));
        GroupMemoryBarrierWithGroupSync();
    }
    if (lane != 0)
        return;
#if DLSSNR_TRANSPORT_BOUNDS_STAGE == 0
    Output[group.xy] = Bounds[0];
#else
    lo = Bounds[0].x;
    hi = Bounds[0].y;
    if (!isfinite(lo) || !isfinite(hi) || lo > hi)
    {
        lo = 0;
        hi = 0;
    }
    float gain = isfinite(requestedGain) ? clamp(requestedGain, .01, 1024) : 1;
    if (selection == 1)
    {
        gain = lo < 0 ? min(3.402823466e38, .5 / (-lo)) : 1;
        if (hi > 0)
            gain = min(gain, (65504 - .5) / hi);
        if (!isfinite(gain) || gain <= 0)
            gain = 1;
    }
    float bias = selection == 2 ? -lo * gain : .5;
    Output.Store4(0, asuint(float4(gain, bias, lo, hi)));
#endif
}
