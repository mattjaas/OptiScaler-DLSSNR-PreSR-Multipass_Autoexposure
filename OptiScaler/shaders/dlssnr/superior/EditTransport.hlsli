// Optional affine transport PSOs only. The positive-only shaders and their root signature stay unchanged.
#ifdef DLSSNR_EDIT_TRANSPORT
cbuffer EditTransportCoefficients : register(b1) { float4 gEditTransport; }; // gain, bias, raw min, raw max
float AffineGain() { return isfinite(gEditTransport.x) && gEditTransport.x > 0 ? gEditTransport.x : 1; }
float AffineMinimum() { return isfinite(gEditTransport.z) ? gEditTransport.z : 0; }
float3 AffineRawEdit(float3 proxy, float3 answer) { return SanitizeFinite3(answer - proxy, 0); }
float3 AffineEncode(float3 edit)
{
    float gain = AffineGain();
    return clamp(gExposureAnchors[0].x == 2 ? (edit - AffineMinimum()) * gain : .5 + edit * gain, 0, 65504);
}
float3 AffineDecode(float3 filtered)
{
    float3 finite = SanitizeFinite3(filtered, 0);
    float3 edit = gExposureAnchors[0].x == 2 ? finite / AffineGain() + AffineMinimum() : (finite - .5) / AffineGain();
    return float3(isfinite(filtered.r) ? edit.r : 0, isfinite(filtered.g) ? edit.g : 0,
                  isfinite(filtered.b) ? edit.b : 0);
}
#endif
