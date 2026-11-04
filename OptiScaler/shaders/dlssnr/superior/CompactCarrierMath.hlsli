// Combine ghost rejection and the global raw-edit fade into one interpolation.
// Evaluate the mask first: a fully replaced pixel needs no filtered sample/inverse gain.
// The host chooses this specialization for full NR answer, or edit with positive global fade.
void CompactCarrierMath(int2 pixel, float gain)
{
    const int3 p = int3(pixel, 0);
    float rejection = CompactGhostWeight(pixel);
    const float requested = asfloat(gExposurePadding);
    const float globalRaw = isfinite(requested) ? saturate(requested) : 0;
    const float rawWeight = globalRaw + rejection * (1 - globalRaw);
    rejection *= 1 - globalRaw;
    float3 edit;
    if (rawWeight >= 1)
        edit = GhostRawEdit(pixel);
    else
    {
        if (gDirectDetailMode != 0u)
        {
            const float3 proxy = SanitizeFinite3(gModel.Load(p).rgb, 0.0);
            edit = DecodeNrAnswer(SanitizeFinite3(gSource.Load(p).rgb, proxy), proxy, gain);
        }
        else
            edit = DecodePositiveCarrier(SanitizeFinite3(gSource.Load(p).rgb, float3(.5, .5, .5)), gain);
        if (rawWeight > 0)
            edit = lerp(edit, GhostRawEdit(pixel), rawWeight);
    }
    gTarget[pixel] = float4(edit, rejection);
}
