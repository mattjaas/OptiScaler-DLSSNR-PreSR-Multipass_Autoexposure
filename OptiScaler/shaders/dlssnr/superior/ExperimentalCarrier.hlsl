// Preserve the frozen v59 source and its independent reference blobs.
#include "precompile/dlssnr.hlsl"
#include "PositiveCarrier.hlsli"

[numthreads(8, 8, 1)]
void ExperimentalCarrierMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gWidth || id.y >= gHeight) return;
    const int3 p = int3(id.xy, 0);
    if (gMode == 37u || gMode == 39u)
    {
        const float3 proxy = SanitizeFinite3(gSource.Load(p).rgb, 0.0);
        const float3 model = SanitizeFinite3(gModel.Load(p).rgb, proxy);
        gTarget[id.xy] = float4(EncodePositiveCarrier(proxy, model, gWhitePoint), 1.0);
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
        gTarget[id.xy] = float4(DecodePositiveCarrier(SanitizeFinite3(gSource.Load(p).rgb, 0.5), gWhitePoint), 1.0);
}
