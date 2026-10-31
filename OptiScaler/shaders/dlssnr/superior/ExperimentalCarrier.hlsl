// Preserve the frozen v59 source and its independent reference blobs.
#include "precompile/dlssnr.hlsl"
#include "PositiveCarrier.hlsli"
#include "DepthCarrier.hlsli"
#include "DepthBrightness.hlsli"

[numthreads(8, 8, 1)]
void ExperimentalCarrierMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gWidth || id.y >= gHeight) return;
    const int3 p = int3(id.xy, 0);
    float depth = 0.0;
    [branch] if ((gDirectResolveUpscaler & 3u) != 0u || (gMode != 43u && isfinite(gResidualScale) && gResidualScale > 0.0))
        depth = gMotion.Load(p).x;
    const float weight = CarrierDepthWeight(depth, gPassthrough != 0u, gDirectResolveUpscaler, gExposureAnchors[0]);
    const float gain = CarrierDepthGain(gWhitePoint, weight, gDirectResolveUpscaler);
    const float bias = CarrierDepthBias(weight, gDirectResolveUpscaler, gExposureAnchors[0].w);
    if (gMode == 43u)
    {
        const float3 proxy = SanitizeFinite3(gSource.Load(p).rgb, 0.0);
        const float3 model = SanitizeFinite3(gModel.Load(p).rgb, proxy);
        precise float3 edit = model - proxy;
        // One-shot diagnostic: signed edit after positive gain, before colour transforms/clipping.
        gTarget[id.xy] = float4(min(edit, 0.0) + max(edit, 0.0) * PositiveCarrierGain(gain), 1.0);
        return;
    }
    float3 depthColour = 0.0;
    [branch] if (isfinite(gResidualScale) && gResidualScale > 0.0)
        depthColour = CarrierDepthColour(depth, gResidualScale, gResidualBlendUnused,
                                        gSkinProtection, gPassthrough != 0u);
    if (gMode == 37u || gMode == 39u)
    {
        const float3 proxy = SanitizeFinite3(gSource.Load(p).rgb, 0.0);
        const float3 model = SanitizeFinite3(gModel.Load(p).rgb, proxy);
        gTarget[id.xy] = float4(EncodeDepthCarrier(proxy, model, gain, gMaxRatio,
                                                  gDirectDetailMode != 0u, depthColour, bias), 1.0);
        if (gMode == 39u)
        {
            const float2 uv = (float2(id.xy) + 0.5) / float2(gWidth, gHeight);
            const uint2 size = uint2(gTransferStrength, gColourStrength);
            const uint2 mp = min(uint2(uv * size), size - 1) + uint2(gCompareSwap, gTransfer);
            const float2 mv = gOriginal.Load(int3(mp, 0)).xy * float2(gMvScaleX, gMvScaleY);
            gKeep[id.xy] = float4(all(isfinite(mv)) ? mv : float2(0, 0), 0, 1);
        }
    }
    else if (gDirectDetailMode != 0u)
    {
        const float3 proxy = SanitizeFinite3(gModel.Load(p).rgb, 0.0);
        const float3 filtered = SanitizeFinite3(gSource.Load(p).rgb, proxy + depthColour + bias) - depthColour - bias;
        gTarget[id.xy] = float4(DecodeNrAnswer(filtered, proxy, gain, gMaxRatio), 1.0);
    }
    else
        gTarget[id.xy] = float4(DecodePositiveCarrier(SanitizeFinite3(gSource.Load(p).rgb, bias == 0.0 ? float3(0.5, 0.5, 0.5) : 0.5 + depthColour + bias) - depthColour - bias,
                                                     gain, gMaxRatio), 1.0);
}
