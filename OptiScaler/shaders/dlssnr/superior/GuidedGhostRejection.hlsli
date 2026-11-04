// One-sided foreground-contamination evidence, not a general temporal ground-truth detector.
// t3: matched NDC depth, t5: matched MV in working pixels. No persistent history or extra dispatch.
float GhostPeak(float3 v)
{
    v = abs(v);
    return max(v.x, max(v.y, v.z));
}
float GhostNearDepth(int2 p)
{
    float z = gMotion.Load(int3(p, 0)).x;
    return isfinite(z) && z >= 0.0 && z <= 1.0 ? (gPassthrough != 0u ? z : 1.0 - z) : -1.0;
}
float3 GhostRestoredTap(int2 pixel)
{
    int3 p = int3(pixel, 0);
    float z = gMotion.Load(p).x;
    float weight = CarrierDepthWeight(z, gPassthrough != 0u, gDirectResolveUpscaler, gExposureAnchors[0]);
    float gain = CarrierDepthGain(gWhitePoint, weight, gDirectResolveUpscaler);
    float bias = CarrierDepthBias(weight, gDirectResolveUpscaler, gExposureAnchors[0].w);
    float3 colour = CarrierDepthColour(z, gResidualScale, gResidualBlendUnused, gSkinProtection, gPassthrough != 0u);
    if (gDirectDetailMode != 0u)
    {
        float3 proxy = SanitizeFinite3(gModel.Load(p).rgb, 0.0);
        float3 filtered = SanitizeFinite3(gSource.Load(p).rgb, proxy + colour + bias) - colour - bias;
        return DecodeNrAnswer(filtered, proxy, gain, gMaxRatio);
    }
    return DecodePositiveCarrier(SanitizeFinite3(gSource.Load(p).rgb, 0.5 + colour + bias) - colour - bias, gain,
                                 gMaxRatio);
}
float3 RejectGuidedGhost(float3 filtered, int2 pixel, out float mask)
{
    mask = 0.0;
    float3 raw = GhostRawEdit(pixel), difference = filtered - raw;
    float threshold =
        isfinite(gColourStrength) && gColourStrength >= 0.0 && gColourStrength <= .25 ? gColourStrength : .004;
    float softness =
        isfinite(gTransferStrength) && gTransferStrength >= .0001 && gTransferStrength <= .25 ? gTransferStrength : .01;
    float strength = isfinite(gCompareSplit) ? saturate(gCompareSplit) : 1.0;
    if (strength == 0.0 || GhostPeak(difference) <= threshold)
        return filtered;
    float nearP = GhostNearDepth(pixel);
    float2 mv = gAux2.Load(int3(pixel, 0)).xy;
    if (nearP <= 0.0 || !all(isfinite(mv)) || any(abs(mv) >= 65504.0))
        return filtered;
    float3 mean = difference;
    float meanLength = length(difference), noise = 0.0;
    const int2 cross[4] = { int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1) };
    [unroll] for (uint n = 0; n < 4; ++n)
    {
        int2 q = clamp(pixel + cross[n], int2(0, 0), int2(gWidth, gHeight) - 1);
        float3 r = GhostRawEdit(q), delta = GhostRestoredTap(q) - r;
        mean += delta;
        meanLength += length(delta);
        noise = max(noise, GhostPeak(r - raw));
    }
    mean *= .2;
    meanLength *= .2;
    float coherence = length(mean) / max(meanLength, 1e-6);
    float amplitude =
        smoothstep(threshold, threshold + softness, GhostPeak(mean) - .25 * noise) * smoothstep(.55, .85, coherence);
    if (amplitude <= .001)
        return filtered;
    float scale = float(gHeight) / 1080.0;
    float radius = isfinite(gExposureAnchors[1].x) ? clamp(gExposureAnchors[1].x, 8.0, 512.0) : 192.0;
    float depthRatio = isfinite(gExposureAnchors[1].y) ? clamp(gExposureAnchors[1].y, 1.02, 4.0) : 1.2;
    float gate = isfinite(gExposureAnchors[1].z) ? clamp(gExposureAnchors[1].z, .1, 16.0) * scale : 2.0 * scale;
    float precision = isfinite(gExposureAnchors[1].w) && gExposureAnchors[1].w >= 0.0 && gExposureAnchors[1].w <= .001
                          ? max(1e-6, gExposureAnchors[1].w * 2.0)
                          : 2.0 / 65535.0;
    float best = 0.0, energy = dot(difference, difference);
    bool moving = dot(mv, mv) >= scale * scale;
    const float2 directions[8] = { float2(1, 0),  float2(.7071, .7071),   float2(0, 1),  float2(-.7071, .7071),
                                   float2(-1, 0), float2(-.7071, -.7071), float2(0, -1), float2(.7071, -.7071) };
    [loop] for (uint tap = 0; tap < 24; ++tap)
    {
        float2 offset;
        if (moving)
        {
            float t, side = 0.0;
            if (tap < 12)
            {
                uint k = tap % 6;
                t = k == 0 ? .5 : k == 1 ? 1.0 : k == 2 ? 2.0 : k == 3 ? 3.0 : k == 4 ? 4.0 : 6.0;
                if (tap < 6)
                    t = -t;
            }
            else
            {
                uint k = (tap - 12) / 2;
                t = k % 3 == 0 ? 1.0 : k % 3 == 1 ? 2.0 : 4.0;
                if (k < 3)
                    t = -t;
                side = (tap & 1u) != 0u ? .35 : -.35;
            }
            offset = mv * t + float2(-mv.y, mv.x) * side;
        }
        else
            offset = directions[tap % 8] * (tap < 8 ? radius / 16.0 : tap < 16 ? radius / 4.0 : radius) * scale;
        offset *= min(1.0, radius * scale / max(length(offset), 1e-6));
        int2 q = pixel + int2(round(offset));
        if (any(q < 0) || q.x >= int(gWidth) || q.y >= int(gHeight))
            continue;
        float nearQ = GhostNearDepth(q);
        if (nearQ <= nearP * depthRatio + precision)
            continue;
        float2 otherMv = gAux2.Load(int3(q, 0)).xy;
        if (!all(isfinite(otherMv)) || any(abs(otherMv) >= 65504.0))
            continue;
        float2 relative = mv - otherMv, delta = float2(q - pixel);
        float v2 = dot(relative, relative), dist2 = dot(delta, delta), alignment = dot(delta, relative);
        if (v2 <= gate * gate || dist2 <= 16.0 * scale * scale || dist2 > 36.0 * v2 + 4.0 * scale * scale ||
            alignment * alignment < .64 * dist2 * v2)
            continue;
        float3 contrast = GhostRawEdit(q) - raw;
        float c2 = dot(contrast, contrast);
        if (c2 <= .0001)
            continue;
        float alpha = dot(difference, contrast) / c2;
        if (alpha <= .015 || alpha >= 1.2)
            continue;
        float relativeError = length(difference - alpha * contrast) * rsqrt(max(energy, 1e-12));
        float fit = 1.0 - smoothstep(.35, .85, relativeError);
        float score =
            fit * smoothstep(.015, .06, alpha) * (1.0 - smoothstep(.8, 1.2, alpha)) * smoothstep(.01, .025, sqrt(c2));
        best = max(best, score);
        if (best >= .999)
            break;
    }
    mask = strength * amplitude * smoothstep(.1, .4, best);
    return mask >= 1.0 ? raw : lerp(filtered, raw, mask);
}
