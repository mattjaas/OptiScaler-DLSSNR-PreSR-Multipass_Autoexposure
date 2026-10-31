// Private carrier transport only. Relative distance 0 near / 1 far, NOT metres.
float CarrierDepthWeight(float depth, bool inverted, uint flags, float4 parameters)
{
    [branch] if ((flags & 3u) == 0u || !isfinite(depth) || depth < 0.0 || depth > 1.0) return 0.0;
    float curve = isfinite(parameters.z) && parameters.z >= .03125 && parameters.z <= 64.0 ? parameters.z : .25;
    float p = pow(saturate(inverted ? depth : 1.0-depth), curve);
    if ((flags & 4u) != 0u) return p;
    float nearLimit = isfinite(parameters.x) ? clamp(parameters.x, 0.0, .9999) : 0.0;
    float farLimit = isfinite(parameters.y) ? clamp(parameters.y, nearLimit+.0001, 1.0) : 1.0;
    float t = saturate((1.0-p-nearLimit)/(farLimit-nearLimit));
    return 1.0-t*t*(3.0-2.0*t);
}
float CarrierDepthGain(float peak, float weight, uint flags)
{
    if ((flags & 1u) == 0u) return peak; // preserve the existing disabled path
    return 1.0+weight*(max(1.0,PositiveCarrierGain(peak))-1.0);
}
float CarrierDepthBias(float weight, uint flags, float strength)
{
    if ((flags & 2u) == 0u) return 0.0;
    strength = isfinite(strength) ? clamp(strength,0.0,64.0) : .1;
    return weight*strength;
}
float3 EncodeDepthCarrier(float3 proxy, float3 model, float gain, float saturation,
                         bool fullAnswer, float3 colour, float bias)
{
    // With no added brightness, retain the old operation order bit for bit.
    if (bias == 0.0)
    {
        float3 encoded = fullAnswer ? EncodeNrAnswer(proxy,model,gain,saturation) :
                                     EncodePositiveCarrier(proxy,model,gain,saturation);
        return min(encoded+colour,65504.0);
    }
    precise float3 edit = model-proxy;
    precise float3 scaled = min(edit,0.0)+max(edit,0.0)*PositiveCarrierGain(gain);
    precise float3 base = fullAnswer ? proxy : 0.5;
    precise float3 encoded = base+ScaleCarrierChroma(scaled,CarrierSaturation(saturation));
    // Lift BEFORE clipping: negative edit can be carried when the offset makes it nonnegative.
    return clamp(encoded+colour+bias,0.0,65504.0);
}
