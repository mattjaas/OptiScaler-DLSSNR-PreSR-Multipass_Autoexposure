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
float3 GhostOutputCarrier(float3 raw, float3 base, float gain)
{
#ifdef DLSSNR_EDIT_TRANSPORT
    return AffineEncode(raw);
#else
    return clamp(base + min(raw, 0) + max(raw, 0) * gain, 0, 65504);
#endif
}

float GhostMaxChannel(float3 value) { return max(value.r, max(value.g, value.b)); }

// A trail may contain several past poses and colours. Protect changes outside the
// current neighbourhood's range even when the single stored reference disagrees.
float GhostOutputRangeEvidence(int2 pixel, float3 raw)
{
    const int3 p = int3(pixel, 0);
    float3 base = gDirectDetailMode != 0u ? SanitizeFinite3(gModel.Load(p).rgb, 0) : float3(.5, .5, .5);
    float gain = PositiveCarrierGain(gWhitePoint);
    float3 neutral = GhostOutputCarrier(0, base, gain);
    float3 current = GhostOutputCarrier(raw, base, gain);
    float3 output = gSource.Load(p).rgb;
    if (!all(isfinite(output)) || !all(isfinite(current)))
        return 1;

    // Local evidence alone needs a stronger deviation than a match to saved history.
    float threshold = isfinite(gExposureAnchors[0].z) ? clamp(gExposureAnchors[0].z, .18, 1) : .18;
    float softness = isfinite(gExposureAnchors[0].w) ? saturate(gExposureAnchors[0].w) : .05;
    float3 precision = max(1.0 / 1024.0, abs(current) / 1024.0);
    float scale = .02 + GhostMaxChannel(abs(current - neutral));
    // Adding neighbours can only lower the score. Skip their reads when the centre already rules it out.
    [branch] if (GhostMaxChannel(abs(output - current)) <= threshold * scale ||
                 all(abs(output - current) <= precision)) return 0;

    float3 low = current, high = current;
    // Six work-resolution pixels at the capture resolution. Keep a similar footprint under resolution changes.
    int radius = int(clamp(round(6.0 * gHeight / 1404.0), 2.0, 12.0));
    [unroll] for (int y = -1; y <= 1; y += 2)[unroll] for (int x = -1; x <= 1; x += 2)
    {
        float3 tap = GhostOutputCarrier(GhostRawEdit(pixel + int2(x, y) * radius), base, gain);
        low = min(low, tap);
        high = max(high, tap);
    }

    float3 outside = max(output - high, low - output);
    if (all(outside <= precision))
        return 0;
    scale = .02 + GhostMaxChannel(max(abs(low - neutral), abs(high - neutral)));
    float score = GhostMaxChannel(outside) / scale;
    return softness > 0 ? smoothstep(threshold, threshold + softness, score) : float(score > threshold);
}

float4 LoadGhostContrastReference(int2 pixel)
{
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
    return reference;
}

float GhostContrastMask(int2 pixel, float mask)
{
    if (mask <= 0)
        return 0;
    bool outputGate = gExposureAnchors[0].y == 1.0;
    bool contrastGate = gExposureAnchors[3].x == 1.0;
    float4 reference = LoadGhostContrastReference(pixel);
    bool valid = all(isfinite(reference)) && reference.a > 0;
    if (!valid && (!contrastGate || !outputGate))
        return contrastGate ? 0 : mask;

    float3 raw = GhostRawEdit(pixel);
    float keep = 0;
    [branch] if (valid)
    {
        // Transform each edit separately: positive-only gain is nonlinear across zero.
        float3 oldEdit = reference.rgb / reference.a;
#ifdef DLSSNR_EDIT_TRANSPORT
        // The common affine offset cancels in both input contrast and output evidence.
        float3 currentTransport = raw * AffineGain(), oldTransport = oldEdit * AffineGain();
#else
        float gain = PositiveCarrierGain(gWhitePoint);
        float3 currentTransport = min(raw, 0) + max(raw, 0) * gain;
        float3 oldTransport = min(oldEdit, 0) + max(oldEdit, 0) * gain;
#endif
        keep = outputGate ? GhostOutputEvidence(pixel, raw, oldEdit, currentTransport, oldTransport) : 1;
        [branch] if (contrastGate && keep > 0)
        {
            float3 signedDifference = currentTransport - oldTransport;
            float distance;
            // Compare one brightness scalar; take abs AFTER the sum so equal-brightness colours can cancel.
            // Unknown/nonfinite selectors retain the existing RGB criterion.
            [branch] if (gExposureAnchors[3].w == 1.0) distance =
                abs(dot(signedDifference, float3(.2126, .7152, .0722)));
            else
            {
                float3 difference = abs(signedDifference);
                distance = max(difference.r, max(difference.g, difference.b));
            }
            float threshold = isfinite(gExposureAnchors[3].y) ? clamp(gExposureAnchors[3].y, 0, 1) : .02;
            float softness = isfinite(gExposureAnchors[3].z) ? clamp(gExposureAnchors[3].z, 0, 1) : .005;
            float contrastKeep =
                softness > 0 ? 1 - smoothstep(threshold, threshold + softness, distance) : float(distance <= threshold);
            keep *= isfinite(distance) ? contrastKeep : 0;
        }
    }
    [branch] if (outputGate && keep < 1) keep = max(keep, GhostOutputRangeEvidence(pixel, raw));
    return mask * keep;
}
