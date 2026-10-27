// Algorithm / reduction order from the user-supplied d4r/NGX report.
// Existing 256-byte Superior ABI, six SRVs and two UAVs; no extra root signature.
#include "precompile/dlssnr.hlsl"

// Mode 40: Width/Height = measurement grid. SourceWidth/Height = input rect size.
// MotionBaseX/Y = input origin; ExposureMode = explicit method (3 CNN, 4 K, 5 T2).
// Mode 41: Width/Height = block grid; HistoryValid = old state is initialized;
// DebugView = measure, ApplyModel = reset. t0 = block sums, t1 = previous state.
// Mode 42: WhitePoint = manual value. All outputs are FP32; final exposure is FP16 RTZ.
float TruncatedPositiveHalf(float v)
{
    if (!(v > 0.0) || !isfinite(v)) return 1.0;
    uint bits = f32tof16(v) & 65535u;
    if (f16tof32(bits) > v) --bits;
    const float result = f16tof32(bits);
    return result > 0.0 && isfinite(result) ? result : 1.0;
}
float CalculateLegacyDLSSAutoExposure(float logMean)
{
    return TruncatedPositiveHalf((0.18 / 0.82) * exp2(asfloat(0xBFB8AA3Bu) * logMean));
}
float CalculateTransformer2DLSSAutoExposure(float mean)
{
    return TruncatedPositiveHalf(0.23547087609767914 / max(mean, 0.005));
}
groupshared float privateParts[64];
[numthreads(64, 1, 1)]
void PrivateExposureMain(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    if (gMode == 42u)
    {
        if (lane == 0u)
            gTarget[uint2(0, 0)] = float4(isfinite(gWhitePoint) && gWhitePoint > 0.0 ? gWhitePoint : 1.0, 0, 0, 0);
        return;
    }
    float sum = 0.0;
    if (gMode == 40u)
    {
        const uint2 grid = uint2(gWidth, gHeight);
        const uint2 base = group.xy * 32u + uint2(lane & 7u, lane >> 3u) * 4u;
        const float invCount = 1.0 / (float(grid.x) * float(grid.y));
        for (uint y = 0; y < 4; ++y)
            for (uint x = 0; x < 4; ++x)
            {
                const uint2 at = base + uint2(x, y);
                if (any(at >= grid)) continue;
                const uint2 sourceAt = uint2(floor(mad(float2(at) / float2(grid - 1),
                    float2(gExposureSourceWidth - 1u, gExposureSourceHeight - 1u), 0.5)));
                // The actual carrier is R16G16B16A16_FLOAT: K's canonical half conversion
                // is already exact here. Never re-quantize this input with another rounding mode.
                const float3 c = gSource.Load(int3(uint2(gResidualMotionBaseXUnused, gResidualMotionBaseYUnused) + sourceAt, 0)).rgb;
                if (gExposureMode != 5u)
                {
                    const float invPre = 1.0 / gPreExposure;
                    const float scale = isfinite(invPre) && invPre > 0.0 ? invPre : 1.0;
                    precise float luma = scale * mad(c.b, 0.25, mad(c.r, 0.25, 0.5 * c.g));
                    if (luma > 0.0 && isfinite(luma))
                        sum += (log2(luma + 1e-6) * asfloat(0x3F317218u)) * invCount;
                }
                else
                {
                    precise float luma = 0.25 * c.r + 0.5 * c.g + 0.25 * c.b;
                    // Explicit invalid policy; finite values use the report's clamp exactly.
                    if (isnan(luma)) luma = 0.0;
                    sum += invCount * min(max(luma, 0.0), 10000.0);
                }
            }
    }
    else if (gDebugView != 0u)
        for (uint i = lane; i < gWidth * gHeight; i += 64u)
            sum += gSource.Load(int3(i % gWidth, i / gWidth, 0)).x;
    privateParts[lane] = sum;
    GroupMemoryBarrierWithGroupSync();
    if (lane != 0u) return;
    float total = 0.0;
    for (uint i = 0u; i < 64u; ++i) total += privateParts[i];
    if (gMode == 40u)
        gTarget[group.xy] = float4(total, 0, 0, 0);
    else
    {
        float current = gResidualHistoryValidUnused != 0u ? gModel.Load(int3(0, 0, 0)).x : 1.0;
        float previous = current;
        if (gDebugView != 0u)
        {
            current = gExposureMode == 5u ? CalculateTransformer2DLSSAutoExposure(total) : CalculateLegacyDLSSAutoExposure(total);
            if (gApplyModel != 0u) previous = gExposureMode == 5u ? 0.0 : current;
        }
        gTarget[uint2(0, 0)] = float4(current, previous, 0, 0);
    }
}
