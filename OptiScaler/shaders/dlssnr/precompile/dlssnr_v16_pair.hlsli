// Row-local union cache: keep only four source/model taps live, not both 3x3 stencils.
#ifdef DLSSNR_TILED_V16_PAIR
struct V16Guide
{
    float4 native;
    float2 uvq, sourcePos;
    int2 roundedBase;
    float3 weightsX, weightsY;
    float rangeFactor, spatialFactor;
    float3 weighted, bilinear;
    float weightSum;
};
V16Guide V16Begin(int2 p)
{
    uint nw, nh; gOriginal.GetDimensions(nw, nh);
    p = clamp(p, int2(0, 0), int2(nw, nh) - 1);
    V16Guide a;
    a.native = gOriginal.Load(int3(p, 0));
    a.uvq = (float2(p) + 0.5) / float2(nw, nh);
    const float2 geometryScale = float2(asfloat(gResidualMotionBaseXUnused), asfloat(gResidualMotionBaseYUnused));
    a.sourcePos = (float2(p) + 0.5) * geometryScale - 0.5;
    a.roundedBase = int2(floor(a.sourcePos + 0.5));
    const int2 bilinearBase = int2(floor(a.sourcePos));
    const float2 fracPos = frac(a.sourcePos);
    const bool movedX = a.roundedBase.x != bilinearBase.x;
    const bool movedY = a.roundedBase.y != bilinearBase.y;
    a.weightsX = movedX ? float3(1.0 - fracPos.x, fracPos.x, 0.0) : float3(0.0, 1.0 - fracPos.x, fracPos.x);
    a.weightsY = movedY ? float3(1.0 - fracPos.y, fracPos.y, 0.0) : float3(0.0, 1.0 - fracPos.y, fracPos.y);
    const float rangeSigma = max(abs(gResidualBlendUnused), 1e-5);
    const float spatialSigma = max(abs(gResidualScale), 1e-4);
    a.rangeFactor = (0.7213475204444817 / 3.0) / (rangeSigma * rangeSigma);
    a.spatialFactor = 0.7213475204444817 / (spatialSigma * spatialSigma);
    a.weighted = 0.0; a.bilinear = 0.0; a.weightSum = 0.0;
    return a;
}
void V16Accumulate(inout V16Guide a, int ox, int oy, int2 coord, float3 proxy, float3 model)
{
    const float3 residual = model - proxy;
    a.bilinear += residual * (a.weightsX[ox + 1] * a.weightsY[oy + 1]);
    const float3 delta = proxy - a.native.rgb;
    const float2 deltaSpatial = float2(coord) - a.sourcePos;
    const float w = exp2(-(dot(delta, delta) * a.rangeFactor + dot(deltaSpatial, deltaSpatial) * a.spatialFactor));
    a.weighted += residual * w;
    a.weightSum += w;
}
float4 V16Finish(V16Guide a)
{
    const float3 guided = a.weightSum > 1e-8 ? a.weighted / a.weightSum : a.bilinear;
    const float3 editRaw = InterPassGuideBlend(a.bilinear, guided, saturate(gResidualConfidenceUnused));
    const float3 edit = InterPassShapeEditAt(editRaw, a.uvq, a.native.rgb);
    return float4(SanitizeFinite3(a.native.rgb + edit, a.native.rgb), a.native.a);
}
void InterPassV16Pair(int2 p, bool second, out float4 result0, out float4 result1)
{
    if (!second)
    {
        result0 = InterPassCorrectedP100LoadDynamicInternal(p, true, false, int2(0, 0));
        result1 = result0; return;
    }
    V16Guide a = V16Begin(p), b = V16Begin(p + int2(1, 0));
    const int shift = b.roundedBase.x - a.roundedBase.x;
    if (shift < 0 || shift > 1 || a.roundedBase.y != b.roundedBase.y)
    {
        result0 = InterPassCorrectedP100LoadDynamicInternal(p, true, false, int2(0, 0));
        result1 = InterPassCorrectedP100LoadDynamicInternal(p + int2(1, 0), true, false, int2(0, 0));
        return;
    }
    uint sw, sh; gSource.GetDimensions(sw, sh);
    [unroll] for (int oy = -1; oy <= 1; ++oy)
    {
        float3 proxy[4], model[4];
        [unroll] for (int x = 0; x < 4; ++x)
        {
            const int2 coord = clamp(a.roundedBase + int2(min(x, shift + 2) - 1, oy), int2(0, 0), int2(sw, sh) - 1);
            proxy[x] = gSource.Load(int3(coord, 0)).rgb;
            model[x] = gModel.Load(int3(coord, 0)).rgb;
        }
        [unroll] for (int ox = -1; ox <= 1; ++ox)
        {
            const int2 ca = clamp(a.roundedBase + int2(ox, oy), int2(0, 0), int2(sw, sh) - 1);
            const int2 cb = clamp(b.roundedBase + int2(ox, oy), int2(0, 0), int2(sw, sh) - 1);
            V16Accumulate(a, ox, oy, ca, proxy[ox + 1], model[ox + 1]);
            V16Accumulate(b, ox, oy, cb, proxy[ox + 1 + shift], model[ox + 1 + shift]);
        }
    }
    result0 = V16Finish(a); result1 = V16Finish(b);
}
#endif
