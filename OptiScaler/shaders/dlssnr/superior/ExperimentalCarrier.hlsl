// Preserve the frozen v59 source and its independent reference blobs.
#ifdef DLSSNR_COMPACT_GHOST
#define gAux2 gUnusedFloatAux2
#endif
#include "precompile/dlssnr.hlsl"
#ifdef DLSSNR_COMPACT_GHOST
#undef gAux2
// The host selects this PSO only for Mode 38; keep the general carrier unchanged.
#define gMode 38u
#ifdef DLSSNR_COMPACT_NR_ANSWER
#define gDirectDetailMode DLSSNR_COMPACT_NR_ANSWER
#endif
#endif
#include "PositiveCarrier.hlsli"
#ifdef DLSSNR_GHOST_REJECTION
#include "GhostRejection.hlsli"
#ifdef DLSSNR_COMPACT_GHOST
#include "CompactGhostDecode.hlsli"
#endif
#ifdef DLSSNR_GUIDED_GHOST
#include "GuidedGhostRejection.hlsli"
#endif
#ifdef DLSSNR_TEMPORAL_GHOST
#include "TemporalGhostDecode.hlsli"
#endif
#endif

[numthreads(8, 8, 1)]
void ExperimentalCarrierMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gWidth || id.y >= gHeight) return;
    const int3 p = int3(id.xy, 0);
    const float gain = PositiveCarrierGain(gWhitePoint);
    if (gMode == 43u)
    {
        const float3 proxy = SanitizeFinite3(gSource.Load(p).rgb, 0.0);
        const float3 model = SanitizeFinite3(gModel.Load(p).rgb, proxy);
        precise float3 edit = model - proxy;
        // One-shot diagnostic: signed edit after positive gain, before carrier offset/clipping.
        gTarget[id.xy] = float4(min(edit, 0.0) + max(edit, 0.0) * gain, 1.0);
        return;
    }
    if (gMode == 37u || gMode == 39u)
    {
        const float3 proxy = SanitizeFinite3(gSource.Load(p).rgb, 0.0);
        const float3 model = SanitizeFinite3(gModel.Load(p).rgb, proxy);
        gTarget[id.xy] = float4(min((gDirectDetailMode != 0u ? EncodeNrAnswer(proxy, model, gain)
                                                             : EncodePositiveCarrier(proxy, model, gain)) +
                                        0.0,
                                    65504.0),
                                1.0);
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
    {
        float3 edit;
        if (gDirectDetailMode != 0u)
        {
            const float3 proxy = SanitizeFinite3(gModel.Load(p).rgb, 0.0);
            const float3 filtered = SanitizeFinite3(gSource.Load(p).rgb, proxy);
            edit = DecodeNrAnswer(filtered, proxy, gain);
        }
        else
        {
            const float3 filtered = SanitizeFinite3(gSource.Load(p).rgb, float3(.5, .5, .5));
            edit = DecodePositiveCarrier(filtered, gain);
        }
#ifdef DLSSNR_GHOST_REJECTION
        float rejectionMask;
#ifdef DLSSNR_COMPACT_GHOST
        edit = RejectCompactGhost(edit, int2(id.xy), rejectionMask);
#elif defined(DLSSNR_TEMPORAL_GHOST)
        edit = RejectTemporalGhost(edit, int2(id.xy), rejectionMask);
#elif defined(DLSSNR_GUIDED_GHOST)
        edit = RejectGuidedGhost(edit, int2(id.xy), rejectionMask);
#else
        edit = RejectUnsupportedEdit(edit, int2(id.xy), rejectionMask);
#endif
#endif
        const float weightRaw = asfloat(gExposurePadding);
        [branch] if (isfinite(weightRaw) && weightRaw > 0.0)
        {
            const float3 proxy = SanitizeFinite3(gOriginal.Load(p).rgb, 0.0);
            const float3 rawEdit = SanitizeFinite3(gAux.Load(p).rgb, proxy) - proxy;
            edit = weightRaw >= 1.0 ? rawEdit : lerp(edit, rawEdit, weightRaw);
#ifdef DLSSNR_GHOST_REJECTION
            rejectionMask *= 1.0 - saturate(weightRaw);
#endif
        }
#ifdef DLSSNR_GHOST_REJECTION
        gTarget[id.xy] = float4(edit, rejectionMask);
#else
        gTarget[id.xy] = float4(edit, 1.0);
#endif
    }
}
