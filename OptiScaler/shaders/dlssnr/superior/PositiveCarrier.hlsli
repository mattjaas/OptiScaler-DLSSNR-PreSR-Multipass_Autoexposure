// Uniform positive-only carrier. Rejected saturation/depth-colour/depth-brightness controls
// are not part of this shader, including when obsolete constant slots contain non-default values.
float PositiveCarrierGain(float gain) { return isfinite(gain) && gain >= .01 && gain <= 1024.0 ? gain : 1.0; }
// Callers validate gain once with PositiveCarrierGain, including direct/invalid-parameter tests.
float3 EncodePositiveCarrier(float3 proxy, float3 model, float gain)
{
    precise float3 edit = model - proxy;
    precise float3 scaled = min(edit, 0.0) + max(edit, 0.0) * gain;
    precise float3 encoded = .5 + scaled;
    return clamp(encoded, 0.0, 65504.0);
}
float3 EncodeNrAnswer(float3 proxy, float3 model, float gain)
{
    [branch] if (gain == 1.0) return clamp(model, 0.0, 65504.0);
    precise float3 edit = model - proxy;
    precise float3 scaled = min(edit, 0.0) + max(edit, 0.0) * gain;
    precise float3 encoded = proxy + scaled;
    return clamp(encoded, 0.0, 65504.0);
}
float3 DecodePositiveCarrier(float3 filtered, float gain)
{
    precise float3 edit = filtered - .5;
    return min(edit, 0.0) + max(edit, 0.0) / gain;
}
float3 DecodeNrAnswer(float3 filtered, float3 proxy, float gain)
{
    precise float3 edit = filtered - proxy;
    return min(edit, 0.0) + max(edit, 0.0) / gain;
}
