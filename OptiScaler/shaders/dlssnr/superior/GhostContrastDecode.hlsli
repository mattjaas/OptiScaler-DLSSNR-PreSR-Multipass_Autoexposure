// t1 is spare in residual decode. Contrast full-answer PSOs read the proxy from t2 instead.
Texture2D<float4> gGhostContrast : register(t1);
// Output evidence is evaluated BEFORE replacement/fade/feather, using one RGB blend weight.
// A projection onto (old occluder - current raw) estimates occluder carry-over; an
// orthogonal/opposite DLSS detail correction alone is insufficient evidence to mask.
float GhostOutputEvidence(int2 pixel, float3 raw, float3 oldEdit, float3 currentTransport, float3 oldTransport)
{
    const int3 p = int3(pixel, 0);
#ifdef DLSSNR_EDIT_TRANSPORT
    float3 currentInput = AffineEncode(raw), oldInput = AffineEncode(oldEdit);
#else
    float3 base = gDirectDetailMode != 0u ? SanitizeFinite3(gModel.Load(p).rgb, 0) : float3(.5, .5, .5);
    float3 currentInput = clamp(base + currentTransport, 0, 65504);
    float3 oldInput = clamp(base + oldTransport, 0, 65504);
#endif
    // Work directly in the actual carrier domain; no second inverse gain/decode is needed.
    // Input clipping matters: it must not be mistaken for temporal contamination.
    float3 residual = gSource.Load(p).rgb - currentInput;
    float3 direction = oldInput - currentInput;
    if (!all(isfinite(residual)) || !all(isfinite(direction)))
        return 1; // Ambiguous evidence retains the visibility mask.
    // Sub-carrier-quantization changes near 0.5 do not justify replacing DLSS.
    if (all(abs(residual) <= max(1.0 / 1024.0, abs(currentInput) / 1024.0)))
        return 0;
    float d2 = dot(direction, direction), v2 = dot(residual, residual);
    if (d2 <= 1e-8)
        return 1; // Indistinguishable references cannot establish absence of ghosting.
    float projection = dot(residual, direction);
    float threshold = isfinite(gExposureAnchors[0].z) ? clamp(gExposureAnchors[0].z, 0, 1) : .1;
    float softness = isfinite(gExposureAnchors[0].w) ? clamp(gExposureAnchors[0].w, 0, 1) : .05;
    float fraction = max(0, projection) / d2;
    float keep = softness > 0 ? smoothstep(threshold, threshold + softness, fraction) : float(fraction > threshold);
    // Positive, colour-consistent contamination. Squared cosine avoids a sqrt/normalize.
    float alignment = projection > 0 ? projection * projection / max(d2 * v2, 1e-20) : 0;
    float result = keep * smoothstep(.64, .81, alignment);
    return isfinite(result) ? saturate(result) : 1;
}
float GhostContrastMask(int2 pixel, float mask)
{
    if (mask <= 0)
        return 0;
    uint w, h;
    gGhostContrast.GetDimensions(w, h);
    if (w == 0 || h == 0)
        return gExposureAnchors[3].x == 1 ? 0 : mask;
    float2 pos = (float2(pixel) + .5) * float2(w, h) / float2(gWidth, gHeight) - .5;
    int2 nearest = clamp(int2(round(pos)), int2(0, 0), int2(w, h) - 1);
    float4 reference = gGhostContrast.Load(int3(nearest, 0));
    if (reference.a <= 0)
    {
        // Follow the strongest valid footprint contributor at a soft edge.
        // Never average opposite RGB references into an artificial low-contrast colour.
        int2 base = int2(floor(pos));
        float2 f = frac(pos);
        float best = 0;
        [unroll] for (int y = 0; y < 2; ++y)[unroll] for (int x = 0; x < 2; ++x)
        {
            int2 q = clamp(base + int2(x, y), int2(0, 0), int2(w, h) - 1);
            float4 candidate = gGhostContrast.Load(int3(q, 0));
            float weight = (x ? f.x : 1 - f.x) * (y ? f.y : 1 - f.y);
            if (candidate.a > 0 && weight > best)
            {
                reference = candidate;
                best = weight;
            }
        }
    }
    if (!all(isfinite(reference)) || reference.a <= 0)
        return gExposureAnchors[3].x == 1 ? 0 : mask;
    // Transform each edit separately: positive-only gain is nonlinear across zero.
    float3 raw = GhostRawEdit(pixel), oldEdit = reference.rgb / reference.a;
#ifdef DLSSNR_EDIT_TRANSPORT
    // The common affine offset cancels in both input contrast and output evidence.
    float3 currentTransport = raw * AffineGain(), oldTransport = oldEdit * AffineGain();
#else
    float gain = PositiveCarrierGain(gWhitePoint);
    float3 currentTransport = min(raw, 0) + max(raw, 0) * gain;
    float3 oldTransport = min(oldEdit, 0) + max(oldEdit, 0) * gain;
#endif
    if (gExposureAnchors[0].y == 1.0)
        mask *= GhostOutputEvidence(pixel, raw, oldEdit, currentTransport, oldTransport);
    if (mask <= 0 || gExposureAnchors[3].x != 1.0)
        return mask;
    float3 signedDifference = currentTransport - oldTransport;
    float distance;
    // Compare one brightness scalar; take abs AFTER the sum so equal-brightness colours can cancel.
    // Unknown/nonfinite selectors retain the existing RGB criterion.
    [branch] if (gExposureAnchors[3].w == 1.0) distance = abs(dot(signedDifference, float3(.2126, .7152, .0722)));
    else
    {
        float3 difference = abs(signedDifference);
        distance = max(difference.r, max(difference.g, difference.b));
    }
    float threshold = isfinite(gExposureAnchors[3].y) ? clamp(gExposureAnchors[3].y, 0, 1) : .02;
    float softness = isfinite(gExposureAnchors[3].z) ? clamp(gExposureAnchors[3].z, 0, 1) : .005;
    float keep =
        softness > 0 ? 1 - smoothstep(threshold, threshold + softness, distance) : float(distance <= threshold);
    return isfinite(distance) ? mask * keep : 0;
}
