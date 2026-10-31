// Research-only shared score. No model-family branch: only the eventual risk
// threshold differs. Existing declarations/root layout and sample coordinates.
#include "edit_signal_probe.hlsl"
groupshared float4 CommonRGBMin[THREADS];
groupshared float2 CommonMidCount[THREADS];

float3 CommonPositive(uint i)
{
    uint2 cell = uint2(i % GridX, i / GridX);
    uint2 pixel = ((cell * 16 + uint2(PhaseX, PhaseY) * 2 + 1) * uint2(Width, Height))
                  / (uint2(GridX, GridY) * 16);
    float3 v = Edit.Load(int3(pixel, 0)).rgb;
    return max(0, float3(isfinite(v.r) ? v.r : 0, isfinite(v.g) ? v.g : 0, isfinite(v.b) ? v.b : 0));
}

float CommonMiddle(float3 v)
{
    // Avoid sum-min-max: cancellation can turn an exact zero into a positive
    // value and corrupt the two-positive-channel counter at HDR magnitudes.
    return max(min(v.r, v.g), min(max(v.r, v.g), v.b));
}

void CommonFinish(float4 rgbMin, float2 midCount)
{
    const uint size = GridX * GridY;
    if (size == 0 || !all(isfinite(rgbMin)) || !all(isfinite(midCount)))
    {
        Result[uint2(0, 0)] = 0; // count=0 means unavailable, never tune toward infinity.
        return;
    }
    const float3 rgb = rgbMin.rgb / size;
    const float lower = min(rgb.r, min(rgb.g, rgb.b));
    const float middle = CommonMiddle(rgb);
    const float upper = max(rgb.r, max(rgb.g, rgb.b));
    const float pixelMin = rgbMin.a / size;
    const float pixelMid = midCount.x / size;
    const float amplitude = max(0, rgb.r + rgb.g + rgb.b - pixelMin - pixelMid);
    const float conditionalMid = midCount.y > 0 ? midCount.x / midCount.y : 0;
    const float score = .125 * amplitude + 2 * max(0, pixelMid - pixelMin)
                      + 1.625 * max(0, upper - lower) + max(0, middle - lower)
                      + max(0, conditionalMid - pixelMid);
    Result[uint2(0, 0)] = float4(score, midCount.y / size, amplitude, size);
}

void CommonSum(uint lane)
{
    GroupMemoryBarrierWithGroupSync();
    for (uint offset = THREADS / 2; offset; offset /= 2)
    {
        if (lane < offset)
        {
            CommonRGBMin[lane] += CommonRGBMin[lane + offset];
            CommonMidCount[lane] += CommonMidCount[lane + offset];
        }
        GroupMemoryBarrierWithGroupSync();
    }
}

[numthreads(THREADS, 1, 1)]
void CommonProbe(uint lane : SV_GroupIndex, uint3 group : SV_GroupID)
{
    const uint size = GridX * GridY;
#if TILED
    const uint first = group.x * THREADS + lane;
    const uint stride = ((size + THREADS - 1) / THREADS) * THREADS;
#else
    const uint first = lane;
    const uint stride = THREADS;
#endif
    float4 rgbMin = 0;
    float2 midCount = 0;
    for (uint i = first; i < size; i += stride)
    {
        const float3 v = CommonPositive(i);
        const float m = CommonMiddle(v);
        rgbMin += float4(v, min(v.r, min(v.g, v.b)));
        midCount += float2(m, m > 0 ? 1 : 0);
    }
    CommonRGBMin[lane] = rgbMin;
    CommonMidCount[lane] = midCount;
    CommonSum(lane);
    if (lane == 0)
    {
#if TILED
        Result[uint2(0, group.x + 1)] = CommonRGBMin[0];
        Result[uint2(1, group.x + 1)] = float4(CommonMidCount[0], 0, 0);
#else
        CommonFinish(CommonRGBMin[0], CommonMidCount[0]);
#endif
    }
}

[numthreads(64, 1, 1)]
void CommonReduce(uint lane : SV_GroupIndex)
{
    float4 rgbMin = 0;
    float2 midCount = 0;
    const uint groups = (GridX * GridY + THREADS - 1) / THREADS;
    for (uint i = lane; i < groups; i += 64)
    {
        rgbMin += Result[uint2(0, i + 1)];
        midCount += Result[uint2(1, i + 1)].xy;
    }
    CommonRGBMin[lane] = rgbMin;
    CommonMidCount[lane] = midCount;
    CommonSum(lane); // This entry is compiled with THREADS=64.
    if (lane == 0) CommonFinish(CommonRGBMin[0], CommonMidCount[0]);
}
