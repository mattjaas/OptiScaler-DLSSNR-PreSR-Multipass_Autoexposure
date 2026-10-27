// Shared by the ordinary and fused private-carrier preparation paths.
float PositiveCarrierGain(float gain)
{
    return isfinite(gain) && gain >= 1.0 && gain <= 1024.0 ? gain : 1.0;
}
float3 EncodePositiveCarrier(float3 proxy, float3 model, float gain)
{
    precise float3 edit = model - proxy;
    // Keep the subtraction and neutral addition separate: zero edit is exactly 0.5.
    precise float3 scaled = min(edit, 0.0) + max(edit, 0.0) * PositiveCarrierGain(gain);
    precise float3 encoded = 0.5 + scaled;
    // DLSS cannot carry negatives. The upper bound is only the FP16 storage limit,
    // never the previous arbitrary 2.0 limit. Darkening is independent of gain.
    return clamp(encoded, 0.0, 65504.0);
}
float3 DecodePositiveCarrier(float3 filtered, float gain)
{
    precise float3 edit = filtered - 0.5;
    return min(edit, 0.0) + max(edit, 0.0) / PositiveCarrierGain(gain);
}
