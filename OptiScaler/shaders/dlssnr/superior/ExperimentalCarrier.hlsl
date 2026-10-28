// Preserve the frozen v59 source and its independent reference blobs.
#include "precompile/dlssnr.hlsl"
#include "PositiveCarrier.hlsli"
#include "DepthCarrier.hlsli"

[numthreads(8, 8, 1)]
void ExperimentalCarrierMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gWidth || id.y >= gHeight) return;
    const int3 p = int3(id.xy, 0);
    float3 depthColour = 0.0;
    [branch] if (isfinite(gResidualScale) && gResidualScale > 0.0)
        depthColour = CarrierDepthColour(gMotion.Load(p).x, gResidualScale, gResidualBlendUnused,
                                        gSkinProtection, gPassthrough != 0u);
    if (gMode == 37u || gMode == 39u)
    {
        const float3 proxy = SanitizeFinite3(gSource.Load(p).rgb, 0.0);
        const float3 model = SanitizeFinite3(gModel.Load(p).rgb, proxy);
        // Add after the edit transforms. A zero strength takes the exact saturation-only path.
        precise float3 encoded = EncodePositiveCarrier(proxy, model, gWhitePoint, gMaxRatio);
        gTarget[id.xy] = float4(min(encoded + depthColour, 65504.0), 1.0);
        if (gMode == 39u)
        {
            const float2 uv = (float2(id.xy) + 0.5) / float2(gWidth, gHeight);
            const uint2 size = uint2(gTransferStrength, gColourStrength);
            const uint2 mp = min(uint2(uv * size), size - 1) + uint2(gCompareSwap, gTransfer);
            const float2 mv = gOriginal.Load(int3(mp, 0)).xy * float2(gMvScaleX, gMvScaleY);
            gKeep[id.xy] = float4(all(isfinite(mv)) ? mv : float2(0, 0), 0, 1);
        }
    }
    else
        gTarget[id.xy] = float4(DecodePositiveCarrier(SanitizeFinite3(gSource.Load(p).rgb, 0.5) - depthColour,
                                                     gWhitePoint, gMaxRatio), 1.0);
}
