// Specialized direct Finished Picture HDR10/PQ conversion shader.
// Compiled twice so the hot direct path contains only PQ decode or only PQ encode.
// The equations and matrices are copied verbatim from modes 0/1 in dlssnr_finished_color.hlsl.
cbuffer Params : register(b0)
{
    uint mode; float exposureScale; uint width; uint height;
    float sceneIsLinear; float unusedColour; uint unusedDebug; float maxRatio;
};

Texture2D<float4> source : register(t0);
Texture2D<float4> original : register(t2);
RWTexture2D<float4> target : register(u0);

float3 DecodePQ(float3 code)
{
    const float m1 = 2610.0 / 16384.0, m2 = 2523.0 / 32.0;
    const float c1 = 3424.0 / 4096.0, c2 = 2413.0 / 128.0, c3 = 2392.0 / 128.0;
    float3 p = pow(saturate(code), 1.0 / m2);
    return pow(max(p - c1, 0.0) / max(c2 - c3 * p, 1e-6), 1.0 / m1) * 125.0;
}

float3 EncodePQ(float3 light)
{
    const float m1 = 2610.0 / 16384.0, m2 = 2523.0 / 32.0;
    const float c1 = 3424.0 / 4096.0, c2 = 2413.0 / 128.0, c3 = 2392.0 / 128.0;
    float3 p = pow(saturate(light / 125.0), m1);
    return pow((c1 + c2 * p) / (1.0 + c3 * p), m2);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height) return;

    float4 pixel = source.Load(int3(id.xy, 0));

#if defined(FINISHED_DIRECT_PQ_DECODE)
    const float3x3 to709 = {
         1.6604910, -0.5876411, -0.0728499,
        -0.1245505,  1.1328999, -0.0083494,
        -0.0181508, -0.1005789,  1.1187297 };
    // Negative components carry wide-gamut colours; retain them in FP16.
    target[id.xy] = float4(mul(to709, DecodePQ(pixel.rgb)), pixel.a);
#elif defined(FINISHED_DIRECT_PQ_ENCODE)
    const float3x3 to2020 = {
        0.6274039, 0.3292830, 0.0433131,
        0.0690973, 0.9195404, 0.0113623,
        0.0163914, 0.0880133, 0.8955953 };
    float4 base = original.Load(int3(id.xy, 0));
    float3 result = EncodePQ(mul(to2020, pixel.rgb));
    target[id.xy] = float4(all(isfinite(result)) ? result : base.rgb, base.a);
#else
#error Select exactly one FINISHED_DIRECT_PQ_* variant.
#endif
}
