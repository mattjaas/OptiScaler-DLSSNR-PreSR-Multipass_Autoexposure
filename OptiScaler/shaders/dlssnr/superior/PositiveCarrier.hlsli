// Shared by the ordinary and fused private-carrier preparation paths.
float PositiveCarrierGain(float gain)
{
    return isfinite(gain) && gain >= 1.0 && gain <= 1024.0 ? gain : 1.0;
}
float CarrierSaturation(float saturation)
{
    return isfinite(saturation) && saturation >= 1.0 && saturation <= 64.0 ? saturation : 1.0;
}
// A linear chroma transform of the SIGNED edit, before adding the neutral 0.5.
// Binary-exact weights sum to one; the inverse uses the reciprocal saturation.
float3 ScaleCarrierChroma(float3 edit, float factor)
{
    [branch] if (factor == 1.0) return edit;
    precise float grey = dot(edit, float3(0.25, 0.5, 0.25));
    precise float3 chroma = edit - grey;
    return grey + factor * chroma;
}
float3 EncodePositiveCarrier(float3 proxy, float3 model, float gain, float saturation)
{
    precise float3 edit = model - proxy;
    // Keep the subtraction and neutral addition separate: zero edit is exactly 0.5.
    precise float3 scaled = min(edit, 0.0) + max(edit, 0.0) * PositiveCarrierGain(gain);
    precise float3 coloured = ScaleCarrierChroma(scaled, CarrierSaturation(saturation));
    precise float3 encoded = 0.5 + coloured;
    // DLSS cannot carry negatives. The upper bound is only the FP16 storage limit,
    // never the previous arbitrary 2.0 limit. Darkening is independent of gain.
    return clamp(encoded, 0.0, 65504.0);
}
float3 DecodePositiveCarrier(float3 filtered, float gain, float saturation)
{
    precise float3 edit = filtered - 0.5;
    // Reverse transforms in reverse order: chroma first, positive amplification second.
    precise float3 restored = ScaleCarrierChroma(edit, 1.0 / CarrierSaturation(saturation));
    return min(restored, 0.0) + max(restored, 0.0) / PositiveCarrierGain(gain);
}
