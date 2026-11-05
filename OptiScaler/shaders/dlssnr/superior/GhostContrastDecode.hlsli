// t1 is spare in residual decode. Contrast full-answer PSOs read the proxy from t2 instead.
Texture2D<float4> gGhostContrast : register(t1);
float GhostContrastMask(int2 pixel, float mask)
{
    if (mask <= 0)
        return 0;
    uint w, h;
    gGhostContrast.GetDimensions(w, h);
    if (w == 0 || h == 0)
        return 0;
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
        return 0;
    // Apply the CURRENT effective transport gain independently to both signed edits.
    // Multiplying their difference is wrong when only one edit component is positive.
    float gain = PositiveCarrierGain(gWhitePoint);
    float3 currentEdit = GhostRawEdit(pixel), oldEdit = reference.rgb / reference.a;
    float3 signedDifference =
        min(currentEdit, 0) + max(currentEdit, 0) * gain - (min(oldEdit, 0) + max(oldEdit, 0) * gain);
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
