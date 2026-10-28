// Same point on the same private-DLSS depth raster for encode and decode.
// Normalized device Z, not metres; a power curve lets the user spread perspective depth.
float3 CarrierDepthColour(float depth, float strength, float curve, uint pair, bool inverted)
{
    if (!isfinite(strength) || strength <= 0.0) return 0.0;
    strength = min(strength, 16.0);
    depth = isfinite(depth) ? saturate(depth) : (inverted ? 0.0 : 1.0);
    float z = inverted ? 1.0 - depth : depth;
    curve = isfinite(curve) && curve >= 0.03125 && curve <= 64.0 ? curve : 1.0;
    float t = pow(z, curve);
    t = t * t * (3.0 - 2.0 * t);
    const float3 nearColour = pair == 1u ? float3(0, 1, 0) : pair == 2u ? float3(0, 0, 1) : float3(1, 0, 0);
    return strength * lerp(nearColour, 1.0 - nearColour, t);
}
