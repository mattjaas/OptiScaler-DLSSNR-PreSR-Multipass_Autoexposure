// Research fixture only, not part of the rendering pipeline or generated blobs.
// t0 is a signed FP32 capture. Live integration must use model-proxy BEFORE gain,
// saturation/depth/clipping and preserve that same coordinate system.
Texture2D<float4> Edit : register(t0);
RWTexture2D<float4> Result : register(u0);
cbuffer Parameters : register(b0)
{
    uint Width, Height, PhaseX, PhaseY;
    uint GridX, GridY;
    float AmplitudeThreshold;
    uint Unused;
};
#ifndef THREADS
#define THREADS 256
#endif
#ifndef HISTOGRAM
#define HISTOGRAM 0
#endif
#ifndef TILED
#define TILED 0
#endif
groupshared uint Counts[THREADS];
#if HISTOGRAM
groupshared uint Histogram[256];
#endif

float Amplitude(uint i)
{
    uint2 cell = uint2(i % GridX, i / GridX);
    uint2 pixel = ((cell * 16 + uint2(PhaseX, PhaseY) * 2 + 1) * uint2(Width, Height))
                  / (uint2(GridX, GridY) * 16);
    float3 value = Edit.Load(int3(pixel, 0)).rgb;
    value = float3(isfinite(value.x) ? value.x : 0,
                   isfinite(value.y) ? value.y : 0,
                   isfinite(value.z) ? value.z : 0);
    return max(0, max(value.r, max(value.g, value.b)));
}

float2 QuantileBracket(uint size)
{
#if HISTOGRAM
    uint cumulative = 0;
    const uint rank = (size * 9 + 9) / 10;
    for (uint bin = 0; bin < 256; ++bin)
    {
        cumulative += Histogram[bin];
        if (cumulative >= rank)
        {
            const uint base = 0x39800000u >> 19;
            float lo = bin == 0 ? 0 : asfloat((base + bin - 1) << 19);
            float hi = bin == 255 ? 3.402823466e+38 : asfloat((base + bin) << 19);
            return float2(lo, hi);
        }
    }
#endif
    return 0;
}

[numthreads(THREADS, 1, 1)]
void Probe(uint lane : SV_GroupIndex, uint3 group : SV_GroupID)
{
#if HISTOGRAM
    for (uint bin = lane; bin < 256; bin += THREADS) Histogram[bin] = 0;
    GroupMemoryBarrierWithGroupSync();
#endif
    uint count = 0;
    const uint size = GridX * GridY;
#if TILED
    const uint first = group.x * THREADS + lane;
    const uint stride = ((size + THREADS - 1) / THREADS) * THREADS;
#else
    const uint first = lane;
    const uint stride = THREADS;
#endif
    for (uint i = first; i < size; i += stride)
    {
        float a = Amplitude(i);
        count += a > AmplitudeThreshold;
#if HISTOGRAM
        // 16 bins per exponent: <=6.25% bracket width, no log/pow/sort.
        // Bin 0 includes zero and sub-2^-12; bin 255 is overflow.
        uint bits = asuint(a) >> 19;
        const uint base = 0x39800000u >> 19;
        uint bin = bits < base ? 0 : 1 + min(254u, bits - base);
        InterlockedAdd(Histogram[bin], 1);
#endif
    }
    Counts[lane] = count;
    GroupMemoryBarrierWithGroupSync();
    for (uint offset = THREADS / 2; offset; offset /= 2)
    {
        if (lane < offset) Counts[lane] += Counts[lane + offset];
        GroupMemoryBarrierWithGroupSync();
    }
#if TILED && HISTOGRAM
    // This variant is compiled with 64 threads: four adjacent bins per lane.
    Result[uint2(lane, group.x + 1)] = float4(Histogram[lane * 4], Histogram[lane * 4 + 1],
                                            Histogram[lane * 4 + 2], Histogram[lane * 4 + 3]);
#endif
    if (lane == 0)
    {
#if TILED
#if HISTOGRAM
        Result[uint2(64, group.x + 1)] = float4(Counts[0], 0, 0, 0);
#else
        Result[uint2(group.x + 1, 0)] = float4(Counts[0], 0, 0, 0);
#endif
#else
        Result[uint2(0, 0)] = float4(Counts[0], QuantileBracket(size), size);
#endif
    }
}

[numthreads(64, 1, 1)]
void Reduce(uint lane : SV_GroupIndex)
{
    const uint groups = (GridX * GridY + THREADS - 1) / THREADS;
    uint count = 0;
#if HISTOGRAM
    uint4 bins = 0;
    for (uint i = 0; i < groups; ++i) bins += (uint4)Result[uint2(lane, i + 1)];
    Histogram[lane * 4] = bins.x;
    Histogram[lane * 4 + 1] = bins.y;
    Histogram[lane * 4 + 2] = bins.z;
    Histogram[lane * 4 + 3] = bins.w;
    for (uint i = lane; i < groups; i += 64) count += (uint)Result[uint2(64, i + 1)].x;
#else
    for (uint i = lane; i < groups; i += 64) count += (uint)Result[uint2(i + 1, 0)].x;
#endif
    Counts[lane] = count;
    GroupMemoryBarrierWithGroupSync();
    for (uint offset = 32; offset; offset /= 2)
    {
        if (lane < offset) Counts[lane] += Counts[lane + offset];
        GroupMemoryBarrierWithGroupSync();
    }
    if (lane == 0) Result[uint2(0, 0)] = float4(Counts[0], QuantileBracket(GridX * GridY), GridX * GridY);
}
