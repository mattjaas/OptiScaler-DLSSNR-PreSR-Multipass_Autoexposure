
#ifdef VK_MODE
[[vk::binding(0, 0)]]
cbuffer Params : register(b0, space0)
#else
cbuffer Params : register(b0)
#endif
{
    uint  gMode;
    float gWhitePoint;
    uint  gWidth;
    uint  gHeight;
    float gTransferStrength;
    float gColourStrength;
    uint  gDebugView;
    float gMaxRatio;
    uint  gPassthrough;
    float gMvScaleX;     // motion vector units -> pixels of this dispatch
    float gMvScaleY;
    uint  gGuideWidth;   // the motion texture's valid region
    uint  gGuideHeight;
    uint  gCompareMode;  // 0 off, 1 side by side, 2 wipe
    float gCompareSplit; // where the wipe cuts, 0..1
    float gCompareZoom;  // side by side: 1 fits the frame, 2 fills the half
    uint  gCompareSwap;  // put the edited frame on the other side
    uint  gTransfer;     // 0 classic, 1/2 residual spatial/DLSS, 3/4 lighting+colour, 6 direct P100 residual
    float gDebugScale;   // what the debug views are scaled by, held still while the meter moves
    uint  gReversibleMode; // 0 knee, 1 Neutwo+composed, 2 Neutwo+replace, 3 hybrid+composed, 4 hybrid+replace
    uint  gApplyModel;     // 0 output the clean frame (pass still runs), 1 apply the model's edit
    float gMaxDarkening;    // percent: 100 = uncapped, 0 = no darkening
    float gResidualScale;
    uint  gSkinProtection;
    uint  gShowSkinMask;
    float gSkinDetail;
    float gSkinColour;
    float gEnvironmentDetail;
    float gEnvironmentColour;
    float gResidualBlendUnused;
    uint gResidualHistoryValidUnused, gResidualMotionBaseXUnused, gResidualMotionBaseYUnused;
    float gReplaceDetailStrength, gModelWorkScale, gResidualConfidenceUnused;
    uint gExposureMode;
    float gPreExposure, gExposureTrim, gExposureProtection;
    uint gExposureAnchorCount, gExposureSourceWidth, gExposureSourceHeight, gExposurePadding;
    float4 gExposureAnchors[4];
    uint gDirectDetailMode;
    float gDirectDetailMaskStrength;
    uint gDirectResolveUpscaler;
    uint gDirectResolveFlags;
};

// Hue-preserving gamut compression toward the D65 neutral axis.
// Adapted from clshortfuse/RenoDX (https://github.com/clshortfuse/renodx).
// See Licenses/RenoDX_ATTRIBUTION.txt.

float SanitizeFinite(float v, float fallback) { return isfinite(v) ? v : fallback; }

// Approximate skin-colour selection, not a face/skin segmentation network. Warm
// materials may be selected and coloured lighting can hide skin. The preview is
// deliberately exposed so users can check this before relying on protection.
float SkinColourWeight(float3 rgb)
{
    rgb = saturate(rgb);
    float y = dot(rgb, float3(0.299, 0.587, 0.114));
    float cb = (rgb.b - y) * 0.564 + 0.5;
    float cr = (rgb.r - y) * 0.713 + 0.5;
    float2 distance = (float2(cb, cr) - float2(0.405, 0.600)) / float2(0.090, 0.110);
    float chroma = max(rgb.r, max(rgb.g, rgb.b)) - min(rgb.r, min(rgb.g, rgb.b));
    return (1.0 - smoothstep(0.55, 1.35, length(distance))) * smoothstep(0.02, 0.10, chroma);
}

float3 SanitizeFinite3(float3 v, float3 fallback)
{
    return float3(SanitizeFinite(v.x, fallback.x), SanitizeFinite(v.y, fallback.y),
                  SanitizeFinite(v.z, fallback.z));
}

float SafeDivide(float numerator, float denominator, float fallback)
{
    return abs(denominator) > 1e-8 ? numerator / denominator : fallback;
}

// Hunt-Pointer-Estevez LMS over linear BT.709, carrying the fixed D65 adaptation state the
// compression is defined against. The signal itself never leaves BT.709.
float3 LMSToBT709(float3 color)
{
    const float3x3 m = { 5.62059812, -4.57145756, 0.15577924,
                         -1.15555585, 2.25800438, -0.15415806,
                         0.03059913, -0.19018011, 1.06820532 };
    return mul(m, color);
}

float3 BT709ToLMS(float3 color)
{
    const float3x3 m = { 0.30569589, 0.62271286, 0.04528636,
                         0.15776262, 0.76968599, 0.08807030,
                         0.01933082, 0.11919478, 0.95053215 };
    return mul(m, color);
}

// The neutral colour of the same luminance as what is being compressed -- the point everything is
// pulled toward, so that pulling changes saturation and not hue.
float3 D65NeutralBT709(float3 adaptiveStateLms, float luminance)
{
    float3 d65 = LMSToBT709(max(adaptiveStateLms, 1e-8));
    float d65Y = max(dot(d65, float3(0.2126, 0.7152, 0.0722)), 1e-8);
    return d65 * (luminance / d65Y);
}

// The largest scale toward the neutral axis that leaves no channel negative. One for a colour that
// was already representable, which is why this is safe to run on every pixel.
float GamutCompressionScale(float3 color, float3 adaptiveStateLms)
{
    color = SanitizeFinite3(color, float3(0.0, 0.0, 0.0));

    const float y = dot(color, float3(0.2126, 0.7152, 0.0722));

    if (!(y > 1e-8))
        return 1.0;

    const float3 neutral = D65NeutralBT709(adaptiveStateLms, y);
    float scale = 1.0;

    if (color.r < 0.0 && neutral.r > color.r)
        scale = min(scale, SafeDivide(neutral.r, neutral.r - color.r, 1.0));

    if (color.g < 0.0 && neutral.g > color.g)
        scale = min(scale, SafeDivide(neutral.g, neutral.g - color.g, 1.0));

    if (color.b < 0.0 && neutral.b > color.b)
        scale = min(scale, SafeDivide(neutral.b, neutral.b - color.b, 1.0));

    return saturate(SanitizeFinite(scale, 1.0));
}

float3 ClampAp1(float3 color)
{
    const float3 adaptiveStateLms = BT709ToLMS(float3(0.18, 0.18, 0.18));
    const float scale = GamutCompressionScale(color, adaptiveStateLms);

    // Nothing was out of gamut. Leave the colour exactly as it arrived.
    if (scale >= 1.0)
        return color;

    const float y = dot(color, float3(0.2126, 0.7152, 0.0722));
    const float3 neutral = D65NeutralBT709(adaptiveStateLms, y);

    return SanitizeFinite3(neutral + (color - neutral) * scale, max(neutral, 0.0));
}

// ---------------------------------------------------------------------------------------------
// The composition below (UpgradeToneMap's two-branch ratio, the OkLab hue correction, and the blend
// between a luminance-only result and the model's own colour) is taken from RenoDX's DLSS 5 addon by
// clshortfuse -- https://github.com/clshortfuse/renodx. It is their design, not ours; see
// Licenses/RenoDX_LICENSE.txt. The OkLab matrices are Bjorn Ottosson's published constants and the
// AP1, sRGB and PQ transforms are standard colour science.
// ---------------------------------------------------------------------------------------------

// Use OkLab to retain the model's hue while adjusting chroma magnitude.
float3 CbrtSigned(float3 v) { return sign(v) * pow(abs(v), 1.0 / 3.0); }

float3 ToOkLab(float3 color)
{
    const float3x3 rgb_to_lms = { 0.4122214708, 0.5363325363, 0.0514459929,
                                  0.2119034982, 0.6806995451, 0.1073969566,
                                  0.0883024619, 0.2817188376, 0.6299787005 };
    const float3x3 lms_to_lab = { 0.2104542553, 0.7936177850, -0.0040720468,
                                  1.9779984951, -2.4285922050, 0.4505937099,
                                  0.0259040371, 0.7827717662, -0.8086757660 };
    return mul(lms_to_lab, CbrtSigned(mul(rgb_to_lms, color)));
}

float3 FromOkLab(float3 lab)
{
    const float3x3 lab_to_lms = { 1.0, 0.3963377774, 0.2158037573,
                                  1.0, -0.1055613458, -0.0638541728,
                                  1.0, -0.0894841775, -1.2914855480 };
    const float3x3 lms_to_rgb = { 4.0767416621, -3.3077115913, 0.2309699292,
                                  -1.2684380046, 2.6097574011, -0.3413193965,
                                  -0.0041960863, -0.7034186147, 1.7076147010 };
    float3 lms = mul(lab_to_lms, lab);
    return mul(lms_to_rgb, lms * lms * lms);
}

// Take hue direction from correct and chroma magnitude from incorrect.
float3 HueOkLab(float3 incorrect, float3 correct)
{
    float3 incorrectLab = ToOkLab(incorrect);
    const float3 correctLab = ToOkLab(correct);
    const float incorrectChroma = length(incorrectLab.yz);
    const float correctChroma = length(correctLab.yz);

    // Normalize hue direction before scaling; near-grey chroma must not amplify numerical noise.
    const float2 hueDirection = correctChroma > 1e-5 ? correctLab.yz / correctChroma : float2(0.0, 0.0);

    incorrectLab.yz = hueDirection * incorrectChroma;

    return ClampAp1(FromOkLab(incorrectLab));
}

// Bindings are stated for SPIR-V rather than inferred. D3D keeps b, t, u and s in separate register
// files, so b0 and t0 do not collide; Vulkan has one number line per descriptor set, and dxc's default
// mapping would put both at binding 0. The numbers below are the order the pass binds them in, and
// DlssNr_Vk's descriptor set layout has to agree with them entry for entry.
#ifdef VK_MODE
[[vk::binding(1, 0)]]
#endif
Texture2D<float4>   gSource   : register(t0);  // encode: the frame. resolve: the proxy.
#ifdef VK_MODE
[[vk::binding(2, 0)]]
#endif
Texture2D<float4>   gModel    : register(t1);  // resolve: what the model returned.
#ifdef VK_MODE
[[vk::binding(3, 0)]]
#endif
Texture2D<float4>   gOriginal : register(t2);  // resolve: the untouched frame.
#ifdef VK_MODE
[[vk::binding(4, 0)]]
#endif
Texture2D<float4>   gMotion   : register(t3);  // resolve, accumulating: the game's motion vectors.
#ifndef VK_MODE
Texture2D<float4>   gAux      : register(t4);  // DX12 Direct NR: packed P50.rgb + NR retention gate.
Texture2D<float4>   gAux2     : register(t5);  // DX12 Direct NR: selected full-resolution P50 reconstruction.
#endif

#ifdef VK_MODE
[[vk::binding(5, 0)]]
#endif
RWTexture2D<float4> gTarget   : register(u0);  // encode: the proxy. resolve: the frame.
#ifdef VK_MODE
[[vk::binding(6, 0)]]
#endif
RWTexture2D<float4> gKeep     : register(u1);  // encode: the untouched copy. unused by the resolve.
#ifdef VK_MODE
[[vk::binding(7, 0)]]
#endif
SamplerState        gLinear   : register(s0);  // so the edit can be read at a different size

float4 DownsampleLoadClamped(int2 p, uint srcW, uint srcH)
{
    p = clamp(p, int2(0, 0), int2((int) srcW - 1, (int) srcH - 1));
    return gSource.Load(int3(p, 0));
}

float CatmullRomWeight(float x)
{
    x = abs(x);
    if (x < 1.0)
        return ((1.5 * x - 2.5) * x) * x + 1.0;
    if (x < 2.0)
        return ((-0.5 * x + 2.5) * x - 4.0) * x + 2.0;
    return 0.0;
}

float SincPi(float x)
{
    const float ax = abs(x);
    if (ax < 1e-5)
        return 1.0;
    const float p = 3.14159265358979323846 * x;
    return sin(p) / p;
}

float Lanczos2Weight(float x)
{
    x = abs(x);
    return x < 2.0 ? SincPi(x) * SincPi(0.5 * x) : 0.0;
}

float4 DownsampleKernel4x4(float2 uv, uint srcW, uint srcH, bool lanczos)
{
    const float2 p = uv * float2(srcW, srcH) - 0.5;
    const int2 base = int2(floor(p));
    float4 sum = 0.0;
    float weightSum = 0.0;

    [unroll] for (int y = -1; y <= 2; ++y)
    {
        const float wy = lanczos ? Lanczos2Weight((float) (base.y + y) - p.y)
                                 : CatmullRomWeight((float) (base.y + y) - p.y);
        [unroll] for (int x = -1; x <= 2; ++x)
        {
            const float wx = lanczos ? Lanczos2Weight((float) (base.x + x) - p.x)
                                     : CatmullRomWeight((float) (base.x + x) - p.x);
            const float w = wx * wy;
            sum += DownsampleLoadClamped(base + int2(x, y), srcW, srcH) * w;
            weightSum += w;
        }
    }

    float4 filtered = sum / (abs(weightSum) > 1e-6 ? weightSum : 1.0);
    // Cubic/Lanczos negative lobes can overshoot an encoded proxy. Keep RGB in the model's legal range;
    // alpha follows the nearest source sample, matching the legacy area path's non-filtered alpha.
    const int2 center = int2(clamp(floor(uv * float2(srcW, srcH)), 0.0, float2(srcW - 1, srcH - 1)));
    filtered.rgb = saturate(filtered.rgb);
    filtered.a = DownsampleLoadClamped(center, srcW, srcH).a;
    return filtered;
}


float4 DownsampleSsimSharp(float2 uv, uint srcW, uint srcH, uint dstW, uint dstH)
{
    const float2 invDst = 1.0 / float2(dstW, dstH);
    const float2 srcPos = uv * float2(srcW, srcH) - 0.5;
    const int2 srcCenter = int2(floor(srcPos + 0.5));

    float3 lowMean = 0.0, lowSq = 0.0;
    float lowW = 0.0;
    [unroll] for (int y = -1; y <= 1; ++y)
    {
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            const float w = pow(0.25, abs((float) x) + abs((float) y));
            const float2 nuv = saturate(uv + float2(x, y) * invDst);
            const float3 v = gSource.SampleLevel(gLinear, nuv, 0).rgb;
            lowMean += v * w;
            lowSq += v * v * w;
            lowW += w;
        }
    }
    lowMean /= lowW;
    lowSq /= lowW;

    float3 srcMean = 0.0, srcSq = 0.0;
    float srcWeight = 0.0;
    [unroll] for (int y = -1; y <= 1; ++y)
    {
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            const float w = pow(0.25, abs((float) x) + abs((float) y));
            const float3 v = DownsampleLoadClamped(srcCenter + int2(x, y), srcW, srcH).rgb;
            srcMean += v * w;
            srcSq += v * v * w;
            srcWeight += w;
        }
    }
    srcMean /= srcWeight;
    srcSq /= srcWeight;

    const float3 lowVar = max(lowSq - lowMean * lowMean, 0.0);
    const float3 srcVar = max(srcSq - srcMean * srcMean, 0.0);
    float4 base = gSource.SampleLevel(gLinear, uv, 0);
    const float3 contrastGain = clamp(sqrt((srcVar + 1e-5) / (lowVar + 1e-5)), 0.85, 1.55);

    float3 result = lowMean + (base.rgb - lowMean) * contrastGain;
    result += 0.20 * (base.rgb - lowMean);

    base.rgb = saturate(result);
    base.a = DownsampleLoadClamped(srcCenter, srcW, srcH).a;
    return base;
}

float2 ExposureAnchor(uint i)
{
    return (i & 1u) ? gExposureAnchors[min(i / 2u, 3u)].zw : gExposureAnchors[min(i / 2u, 3u)].xy;
}
float WhitePoint()
{
    float base = max(gWhitePoint, 1e-4);
    if (gExposureMode == 1 || gExposureMode == 3)
    {
        float measured = gMotion.Load(int3(0, 0, 0)).r;
        if (isfinite(measured) && measured > 1e-8)
        {
        base = gExposureMode == 1 ? gPreExposure / measured : measured;
        float trim = gExposureTrim;
        uint count = min(gExposureAnchorCount, 8u);
        if (count > 0)
        {
            trim = ExposureAnchor(0).y;
            [unroll] for (uint i = 1; i < 8; ++i)
            {
                float2 previous = ExposureAnchor(i - 1);
                float2 next = ExposureAnchor(i);
                if (i < count && base > previous.x)
                {
                    float t = saturate(log2(max(base, 1e-8) / previous.x) / log2(next.x / previous.x));
                    trim = exp2(lerp(log2(previous.y), log2(next.y), t));
                }
            }
        }
        base *= trim;
        }
    }
    return max(SanitizeFinite(base, gWhitePoint), 1e-4);
}

static const float3 kLuma = float3(0.2126, 0.7152, 0.0722);

// sRGB rather than a plain 2.2 power: it is what an SDR game buffer actually carries, and the model was
// trained on those.
float3 LinearToSrgb(float3 v)
{
    v = saturate(v);
    return lerp(v * 12.92, 1.055 * pow(max(v, 1e-8), 1.0 / 2.4) - 0.055, step(0.0031308, v));
}

float3 SrgbToLinear(float3 v)
{
    v = saturate(v);
    return lerp(v / 12.92, pow((v + 0.055) / 1.055, 2.4), step(0.04045, v));
}

// The edit at an arbitrary position, exactly as the resolve computes its own.
float3 EditAt(float2 uvq)
{
    float3 p = gSource.SampleLevel(gLinear, uvq, 0).rgb;
    float3 m = gModel.SampleLevel(gLinear, uvq, 0).rgb;

    if (gPassthrough == 0)
    {
        p = SrgbToLinear(p);
        m = SrgbToLinear(m);
    }

    return m - p;
}


// Soft-knee proxy shared by encoding and matched-residual reconstruction.
float3 SoftKnee(float3 display)
{
    if (gPassthrough != 0)
        return display;

    float displayLuma = dot(display, kLuma);

    if (displayLuma > 0.75)
    {
        float rolled = 0.75 + 0.25 * (1.0 - exp(-(displayLuma - 0.75) / 0.25));
        display *= rolled / displayLuma;
    }

    // Per-channel headroom, with the hue kept.
    //
    // The roll-off above is on luminance, and luminance is a weighted sum in which blue counts for
    // seven percent. A saturated blue can therefore sit at B = 2 with a luminance of 0.14, pass the
    // knee untouched, and be clipped per channel by the saturate in LinearToSrgb -- and clipping one
    // channel of a triple is a hue rotation, so blue arrives as cyan. That was the green cast over
    // every blue thing in GTA V at colour strength 1: the sky, the denim, the minimap. The model was
    // shown a cyan proxy, answered in cyan, and at colour strength 1 its hue is the frame's hue.
    //
    // One scalar on the whole triple cannot move hue, so the peak channel is brought to 1 that way.
    // Only pixels that were already being clipped are touched, so everything else is bit-identical
    // to before, and the resolve's reconstruction of this proxy stays exact because it goes through
    // this same function.
    float peak = max(display.r, max(display.g, display.b));

    if (peak > 1.0)
        display /= peak;

    return display;
}

// Unclipped, hue-preserving Neutwo proxy adapted from clshortfuse/RenoDX.
// One scalar maps the peak channel to [0,1), preserving RGB ratios.
// Negative input channels are clamped before encoding. See Licenses/RenoDX_LICENSE.txt.
float Neutwo(float x) { return x * rsqrt(x * x + 1.0); } // [0, inf) -> [0, 1), no clip point

float3 NeutwoEncode(float3 v)
{
    v = max(v, 0.0);
    float m = max(v.r, max(v.g, v.b));

    if (m <= 1e-6)
        return v;

    // One scalar taken from the peak channel keeps the hue; the peak lands at Neutwo(m) < 1, so no
    // channel clips and LinearToSrgb's saturate never fires -- the proxy is fully invertible.
    return v * (Neutwo(m) / m);
}

// Replace-mode inverse of NeutwoEncode. Clamp below its pole at 1 to keep highlights finite.
float3 NeutwoDecode(float3 y)
{
    y = max(y, 0.0);
    float m = max(y.r, max(y.g, y.b));
    m = min(m, 0.999999);

    if (m <= 1e-6)
        return y;

    float x = m * rsqrt(max(1.0 - m * m, 1e-8)); // Neutwo^-1 of the peak
    return y * (x / m);
}

// Hybrid proxy: identity below the knee, a C1-continuous Neutwo rolloff above it.
float HybridCurve(float m)
{
    const float k = 0.75; // knee point: identity below, gentle unclipped roll above

    if (m <= k)
        return m;

    const float e = (m - k) / (1.0 - k);         // excess above the knee, [0, inf)
    return k + (1.0 - k) * (e * rsqrt(e * e + 1.0)); // Neutwo(e) scaled into [k, 1); -> 1, never clips
}

float3 HybridEncode(float3 v)
{
    v = max(v, 0.0);
    float m = max(v.r, max(v.g, v.b));

    if (m <= 1e-6)
        return v;

    // One scalar on the peak channel, hue preserved. Below the knee the scalar is 1 (identity); above
    // it the peak lands at HybridCurve(m) < 1, so no channel clips.
    return v * (HybridCurve(m) / m);
}

// The exact inverse of the hybrid curve, for the hybrid REPLACE decode (mode 4). Because it is IDENTITY
// below the knee, the steep expansion is confined to genuine highlights: midtone model wobble is not
// amplified, so hybrid-replace flashes far less than Neutwo-replace while keeping the raw model detail.
float HybridCurveInv(float y)
{
    const float k = 0.75;

    if (y <= k)
        return y;

    float u = (y - k) / (1.0 - k);                  // Neutwo(e), in [0,1)
    u = min(u, 0.999999);                           // the inverse diverges at 1
    const float e = u * rsqrt(max(1.0 - u * u, 1e-8)); // Neutwo^-1 of the excess
    return k + (1.0 - k) * e;
}

float3 HybridDecode(float3 y)
{
    y = max(y, 0.0);
    float m = max(y.r, max(y.g, y.b));

    if (m <= 1e-6)
        return y;

    return y * (HybridCurveInv(m) / m);
}

// Scale a residual so the result cannot leave the unit cube, without changing its direction.
//
// The model's edit is carried up from a smaller raster and laid on the frame's own proxy, so nothing
// guarantees the sum is still a colour. Clamping per channel would bend the hue -- the channel that
// hits the wall first decides the colour of the rest -- so the whole residual is scaled by the
// largest factor that keeps every channel inside, and the direction survives.
//
// hhkbble's, from the multi-pass PR against this fork.
float3 CubeScaleResidual(float3 P, float3 T)
{
    if (gPassthrough != 0)
        return T;

    float3 d = T - P;
    float alpha = 1.0;

    [unroll] for (int c = 0; c < 3; ++c)
    {
        if (d[c] > 1e-6)
            alpha = min(alpha, (1.0 - P[c]) / d[c]);
        else if (d[c] < -1e-6)
            alpha = min(alpha, (0.0 - P[c]) / d[c]);
    }

    return P + saturate(alpha) * d;
}

#ifndef VK_MODE
float DirectDetailGateAt(int2 p)
{
    const int2 lo = int2(0, 0);
    const int2 hi = int2((int) gWidth - 1, (int) gHeight - 1);
    p = clamp(p, lo, hi);
    const int2 px0 = clamp(p + int2(-1, 0), lo, hi);
    const int2 px1 = clamp(p + int2( 1, 0), lo, hi);
    const int2 py0 = clamp(p + int2(0, -1), lo, hi);
    const int2 py1 = clamp(p + int2(0,  1), lo, hi);

    float3 pc = gSource.Load(int3(p, 0)).rgb;
    float3 nc = gModel.Load(int3(p, 0)).rgb;
    float3 pl = gSource.Load(int3(px0, 0)).rgb;
    float3 pr = gSource.Load(int3(px1, 0)).rgb;
    float3 pu = gSource.Load(int3(py0, 0)).rgb;
    float3 pd = gSource.Load(int3(py1, 0)).rgb;
    float3 nl = gModel.Load(int3(px0, 0)).rgb;
    float3 nr = gModel.Load(int3(px1, 0)).rgb;
    float3 nu = gModel.Load(int3(py0, 0)).rgb;
    float3 nd = gModel.Load(int3(py1, 0)).rgb;

    if (gPassthrough == 0)
    {
        pc = SrgbToLinear(pc); nc = SrgbToLinear(nc);
        pl = SrgbToLinear(pl); pr = SrgbToLinear(pr);
        pu = SrgbToLinear(pu); pd = SrgbToLinear(pd);
        nl = SrgbToLinear(nl); nr = SrgbToLinear(nr);
        nu = SrgbToLinear(nu); nd = SrgbToLinear(nd);
    }

    const float floorY = 1.0 / 512.0;
    const float pY = dot(pc, kLuma);
    const float nY = dot(nc, kLuma);
    const float pBlur = dot(pc + pl + pr + pu + pd, kLuma) / 5.0;
    const float nBlur = dot(nc + nl + nr + nu + nd, kLuma) / 5.0;
    const float pDetail = (pY - pBlur) / (max(pY, pBlur) + floorY);
    const float nDetail = (nY - nBlur) / (max(nY, nBlur) + floorY);

    float gate = 1.0;
    const float pMag = abs(pDetail);
    const float nMag = abs(nDetail);
    if (pMag > 0.010)
    {
        const float retained = nMag / max(pMag, 1e-5);
        gate = smoothstep(0.20, 0.70, retained);
        if (pDetail * nDetail < 0.0 && nMag > 0.20 * pMag)
            gate = 0.0;
    }
    return saturate(gate);
}

float DirectMagicKernel(float x)
{
    const float ax = abs(x);
    if (ax >= 1.5)
        return 0.0;
    if (ax <= 0.5)
        return 0.75 - ax * ax;
    const float t = ax - 1.5;
    return 0.5 * t * t;
}

float3 DirectMagicPixel(int2 outP)
{
    uint srcW, srcH;
    gModel.GetDimensions(srcW, srcH);
    const float2 k = float2(gWidth, gHeight) / float2(max(srcW, 1u), max(srcH, 1u));
    const float2 o = float2(outP) + 0.5;
    const float2 lower = (o - 1.5) / k - 0.5;
    const float2 upper = (o + 1.5) / k - 0.5;

    int2 p0 = int2(ceil(lower));
    int2 p1 = int2(floor(upper));
    p0 = clamp(p0, int2(0, 0), int2((int) srcW - 1, (int) srcH - 1));
    p1 = clamp(p1, int2(0, 0), int2((int) srcW - 1, (int) srcH - 1));

    const int nx = clamp(p1.x - p0.x + 1, 1, 12);
    const int ny = clamp(p1.y - p0.y + 1, 1, 12);
    const float uBase = k.x * ((float) p0.x + 0.5) - o.x;
    const float vBase = k.y * ((float) p0.y + 0.5) - o.y;

    float wx[12], wy[12];
    float sumWx = 0.0, sumWy = 0.0;
    [loop] for (int i = 0; i < nx; ++i)
    {
        wx[i] = DirectMagicKernel(uBase + k.x * (float) i);
        sumWx += wx[i];
    }
    [loop] for (int j = 0; j < ny; ++j)
    {
        wy[j] = DirectMagicKernel(vBase + k.y * (float) j);
        sumWy += wy[j];
    }

    float3 acc = 0.0;
    [loop] for (int j2 = 0; j2 < ny; ++j2)
    {
        [loop] for (int i2 = 0; i2 < nx; ++i2)
            acc += gModel.Load(int3(p0 + int2(i2, j2), 0)).rgb * (wx[i2] * wy[j2]);
    }
    return acc / max(sumWx * sumWy, 1e-12);
}

float3 DirectAreaPixel(int2 outP)
{
    uint srcW, srcH;
    gModel.GetDimensions(srcW, srcH);
    const float x0 = ((float) outP.x * (float) srcW) / (float) gWidth;
    const float x1 = ((float) (outP.x + 1) * (float) srcW) / (float) gWidth;
    const float y0 = ((float) outP.y * (float) srcH) / (float) gHeight;
    const float y1 = ((float) (outP.y + 1) * (float) srcH) / (float) gHeight;
    const float area = (x1 - x0) * (y1 - y0);

    const int i0 = (int) floor(x0);
    const int i1 = (int) ceil(x1) - 1;
    const int j0 = (int) floor(y0);
    const int j1 = (int) ceil(y1) - 1;
    float3 acc = 0.0;

    [loop] for (int j = j0; j <= j1; ++j)
    {
        const int jj = clamp(j, 0, (int) srcH - 1);
        const float aY = max(y0, (float) j);
        const float bY = min(y1, (float) j + 1.0);
        const float wy = max(bY - aY, 0.0);
        [loop] for (int i = i0; i <= i1; ++i)
        {
            const int ii = clamp(i, 0, (int) srcW - 1);
            const float aX = max(x0, (float) i);
            const float bX = min(x1, (float) i + 1.0);
            acc += gModel.Load(int3(ii, jj, 0)).rgb * (max(bX - aX, 0.0) * wy);
        }
    }
    return acc / max(area, 1e-12);
}

float3 DirectSpatialPixel(int2 outP)
{
    outP = clamp(outP, int2(0, 0), int2((int) gWidth - 1, (int) gHeight - 1));
    if (gDirectResolveUpscaler == 8u)
        return DirectMagicPixel(outP);
    if (gDirectResolveUpscaler == 7u)
        return DirectAreaPixel(outP);

    // Selectors 0 and 9 are exactly bilinear on this branch. FSR1's projCentre/squaredRadius
    // constants are never initialized, so its EASU shader takes its bilinear branch everywhere.
    const float2 sampleUv = (float2(outP) + 0.5) / float2(gWidth, gHeight);
    return gModel.SampleLevel(gLinear, sampleUv, 0).rgb;
}

float4 DirectSpatialSample(float2 outUv)
{
    if (gCompareMode != 1u)
    {
        const int2 p = int2(clamp(floor(outUv * float2(gWidth, gHeight)), 0.0,
                                  float2(gWidth - 1u, gHeight - 1u)));
        return float4(DirectSpatialPixel(p), 1.0);
    }

    // Side-by-side normally samples a materialized NR100 linearly. Reproduce that second sampling
    // stage by evaluating the four surrounding output pixels and blending them here.
    const float2 pos = outUv * float2(gWidth, gHeight) - 0.5;
    const int2 base = int2(floor(pos));
    const float2 f = frac(pos);
    const float3 c00 = DirectSpatialPixel(base);
    const float3 c10 = DirectSpatialPixel(base + int2(1, 0));
    const float3 c01 = DirectSpatialPixel(base + int2(0, 1));
    const float3 c11 = DirectSpatialPixel(base + int2(1, 1));
    return float4(lerp(lerp(c00, c10, f.x), lerp(c01, c11, f.x), f.y), 1.0);
}

// Final P100-guided detail experiments. They intentionally live in the existing resolve dispatch.
// Direct NR identifies itself with DirectResolveFlags bit 0 because its internal transfer can be 0 or 7;
// Upscaled NR residual is transfer 6.
float3 ExperimentDecodeProxySample(float3 raw)
{
    return gPassthrough != 0 ? raw : SrgbToLinear(raw);
}

float3 ExperimentFinalModelAt(float2 uvq)
{
    uvq = saturate(uvq);
    const float3 raw = gTransfer == 7u ? DirectSpatialSample(uvq).rgb
                                        : gModel.SampleLevel(gLinear, uvq, 0).rgb;
    return ExperimentDecodeProxySample(raw);
}

float3 ExperimentBaselineReferenceAt(float2 uvq)
{
    uvq = saturate(uvq);
    // Source is the ordinary composition baseline: native P100 proxy for Direct NR and reconstructed
    // P50->P100 for Upscaled NR residual. Using it here makes strength zero algebraically identical
    // to each mode's normal edit.
    return ExperimentDecodeProxySample(gSource.SampleLevel(gLinear, uvq, 0).rgb);
}

float3 ExperimentGainReferenceAt(float2 uvq)
{
    uvq = saturate(uvq);
    // Only Direct structure transfer needs a second reference: Aux2 is the selected reconstruction
    // of the reduced image shown to NR. Upscaled residual already has that reconstruction in Source.
    const bool directStructure =
        (gDirectResolveFlags & 1u) != 0u && gResidualMotionBaseXUnused != 0u;
    const float3 raw = directStructure ? gAux2.SampleLevel(gLinear, uvq, 0).rgb
                                       : gSource.SampleLevel(gLinear, uvq, 0).rgb;
    return ExperimentDecodeProxySample(raw);
}

float3 ExperimentNativeProxyAt(float2 uvq, float normScale)
{
    uvq = saturate(uvq);

    // Direct NR already carries untouched native-resolution P100 in Source.
    if ((gDirectResolveFlags & 1u) != 0u)
        return ExperimentBaselineReferenceAt(uvq);

    // Upscaled NR residual has reconstructed P50 in Source, so derive true P100 geometry from Original
    // using the same model-domain mapping as the ordinary fullProxy path.
    const float3 native = gOriginal.SampleLevel(gLinear, uvq, 0).rgb / max(normScale, 1e-6);
    if (gPassthrough != 0)
        return saturate(native);
    if (gReversibleMode == 0u)
        return saturate(SoftKnee(native));
    return gReversibleMode >= 3u ? HybridEncode(native) : NeutwoEncode(native);
}

void ExperimentCrossBlur(float2 uvq, float radius, float normScale,
                         float3 modelCenter, float3 baselineCenter, float3 gainCenter, float3 nativeCenter,
                         out float3 modelBlur, out float3 baselineBlur,
                         out float3 gainBlur, out float3 nativeBlur)
{
    const float2 texel = 1.0 / float2(max(gWidth, 1u), max(gHeight, 1u));
    const float2 dx = float2(texel.x * radius, 0.0);
    const float2 dy = float2(0.0, texel.y * radius);

    // Four neighbours plus the already-known centre. This avoids re-reading centre taps in the
    // one-band path and reuses them again for the second band.
    const float3 mL = ExperimentFinalModelAt(uvq - dx);
    const float3 mR = ExperimentFinalModelAt(uvq + dx);
    const float3 mU = ExperimentFinalModelAt(uvq - dy);
    const float3 mD = ExperimentFinalModelAt(uvq + dy);
    modelBlur = (4.0 * modelCenter + mL + mR + mU + mD) * 0.125;

    const float3 bL = ExperimentBaselineReferenceAt(uvq - dx);
    const float3 bR = ExperimentBaselineReferenceAt(uvq + dx);
    const float3 bU = ExperimentBaselineReferenceAt(uvq - dy);
    const float3 bD = ExperimentBaselineReferenceAt(uvq + dy);
    baselineBlur = (4.0 * baselineCenter + bL + bR + bU + bD) * 0.125;

    const bool direct = (gDirectResolveFlags & 1u) != 0u;
    const bool separateGainReference = direct && gResidualMotionBaseXUnused != 0u;
    if (separateGainReference)
    {
        gainBlur = (4.0 * gainCenter +
                    ExperimentGainReferenceAt(uvq - dx) + ExperimentGainReferenceAt(uvq + dx) +
                    ExperimentGainReferenceAt(uvq - dy) + ExperimentGainReferenceAt(uvq + dy)) * 0.125;
    }
    else
    {
        gainBlur = baselineBlur;
    }

    if (direct)
    {
        // Direct Source is already the untouched P100 proxy, so baseline and native geometry are identical.
        nativeBlur = baselineBlur;
    }
    else
    {
        nativeBlur = (4.0 * nativeCenter +
                      ExperimentNativeProxyAt(uvq - dx, normScale) +
                      ExperimentNativeProxyAt(uvq + dx, normScale) +
                      ExperimentNativeProxyAt(uvq - dy, normScale) +
                      ExperimentNativeProxyAt(uvq + dy, normScale)) * 0.125;
    }
}

float ExperimentStructureGain(float3 referenceBand, float3 modelBand)
{
    const float r = dot(referenceBand, kLuma);
    const float m = dot(modelBand, kLuma);
    const float confidence = smoothstep(0.002, 0.020, abs(r));
    if (confidence <= 0.0 || r * m <= 0.0)
        return 1.0;

    const float gain = clamp(abs(m) / max(abs(r), 1e-4), 0.25, 4.0);
    return lerp(1.0, gain, confidence);
}

float3 ExperimentLimitedBand(float3 conventionalEdit, float3 nativeBand, float limiterStrength)
{
    const float editY = abs(dot(conventionalEdit, kLuma));
    const float nativeY = abs(dot(nativeBand, kLuma));
    const float support = saturate((nativeY + 0.002) / (editY + 0.002));
    return conventionalEdit * lerp(1.0, support, limiterStrength);
}

float3 ExperimentResolveBand(float3 conventionalEdit, float3 gainReferenceBand, float3 modelBand,
                             float3 nativeBand, uint structureMode,
                             float limiterStrength, float structureStrength)
{
    float3 limited = conventionalEdit;
    if (gResidualHistoryValidUnused != 0u)
        limited = ExperimentLimitedBand(limited, nativeBand, limiterStrength);

    if (structureMode == 0u)
        return limited;

    const float gain = ExperimentStructureGain(gainReferenceBand, modelBand);
    const float3 nativeGainEdit = nativeBand * (gain - 1.0);
    return lerp(limited, nativeGainEdit, structureStrength);
}

float3 ExperimentP100GuidedEdit(float2 uvq, float3 modelCenter, float3 baselineCenter,
                                float3 gainCenter, float3 nativeCenter, float normScale)
{
    const uint structureMode = min(gResidualMotionBaseXUnused, 2u);
    const float structureStrength = gResidualConfidenceUnused;
    const float limiterStrength = gResidualBlendUnused;

    float3 modelBlur1, baselineBlur1, gainBlur1, nativeBlur1;
    ExperimentCrossBlur(uvq, 1.0, normScale, modelCenter, baselineCenter, gainCenter, nativeCenter,
                        modelBlur1, baselineBlur1, gainBlur1, nativeBlur1);

    if (structureMode < 2u)
    {
        // Low-frequency edit is always the mode's original baseline. Only the high-frequency band
        // is limited/re-expressed through P100 geometry, so structure strength 0 is exactly neutral.
        const float3 lowEdit = modelBlur1 - baselineBlur1;
        const float3 modelBand = modelCenter - modelBlur1;
        const float3 baselineBand = baselineCenter - baselineBlur1;
        const float3 gainBand = gainCenter - gainBlur1;
        const float3 nativeBand = nativeCenter - nativeBlur1;
        const float3 conventionalBandEdit = modelBand - baselineBand;

        return lowEdit +
               ExperimentResolveBand(conventionalBandEdit, gainBand, modelBand, nativeBand, structureMode,
                                     limiterStrength, structureStrength);
    }

    // Two-band version: fine 0..1 px and mid 1..2 px. Low frequencies below radius 2 remain the
    // exact ordinary Direct/Upscaled-residual edit; only the two structure bands change geometry.
    float3 modelBlur2, baselineBlur2, gainBlur2, nativeBlur2;
    ExperimentCrossBlur(uvq, 2.0, normScale, modelCenter, baselineCenter, gainCenter, nativeCenter,
                        modelBlur2, baselineBlur2, gainBlur2, nativeBlur2);

    const float3 lowEdit = modelBlur2 - baselineBlur2;

    const float3 modelFine = modelCenter - modelBlur1;
    const float3 baselineFine = baselineCenter - baselineBlur1;
    const float3 gainFine = gainCenter - gainBlur1;
    const float3 nativeFine = nativeCenter - nativeBlur1;
    const float3 fineEdit =
        ExperimentResolveBand(modelFine - baselineFine, gainFine, modelFine, nativeFine, structureMode,
                              limiterStrength, structureStrength);

    const float3 modelMid = modelBlur1 - modelBlur2;
    const float3 baselineMid = baselineBlur1 - baselineBlur2;
    const float3 gainMid = gainBlur1 - gainBlur2;
    const float3 nativeMid = nativeBlur1 - nativeBlur2;
    const float3 midEdit =
        ExperimentResolveBand(modelMid - baselineMid, gainMid, modelMid, nativeMid, structureMode,
                              limiterStrength, structureStrength);

    return lowEdit + fineEdit + midEdit;
}
#endif

// Detail-quality lab low-resolution filters. These run only at the NR working raster and only when
// explicitly enabled. Slider ranges are UI-only: numeric settings are never clamped to those ranges here.
// Only finite-value sanitisation, texture-coordinate bounds and tiny epsilons needed for valid arithmetic remain.
//
// Mode 16 mapping:
//   Transfer = input preparation selector; TransferStrength = radius/core width; ColourStrength = strength;
//   DebugScale = edge/core detection threshold; CompareMode = core mode (Off/Luma/RGB); MaxRatio = halo protection.
//
// Mode 17 mapping:
//   Transfer/TransferStrength/ColourStrength/DebugScale = NR edge mode/radius/strength/threshold;
//   CompareMode/DebugView/CompareSwap = Ghost Guard mode / band mode / cancel preparation footprint;
//   MaxRatio = Ghost Guard strength;
//   SkinDetail/SkinColour/EnvironmentDetail/EnvironmentColour = ghost detection threshold / sharp-edge threshold /
//       ghost edge-band radius / max suppression;
//   ReplaceDetailStrength/ModelWorkScale/ResidualConfidenceUnused = band strength / low-band radius / mid-band radius.
float ExperimentSmoothThreshold(float threshold, float value)
{
    const float t = max(abs(threshold), 1e-6);
    return smoothstep(t, t * 2.0, max(value, 0.0));
}

float ExperimentThinCoreScore(float yC, float yA, float yB)
{
    const float ySide = 0.5 * (yA + yB);
    // A thin centred structure differs from both sides while the two sides resemble each other.
    // A one-sided step edge has large side asymmetry and is therefore rejected.
    return max(abs(yC - ySide) - 0.5 * abs(yA - yB), 0.0);
}

float4 ExperimentEdgeCoreAttenuation(float2 uv)
{
    uint srcW, srcH;
    gSource.GetDimensions(srcW, srcH);
    const float2 texel = 1.0 / float2(max(srcW, 1u), max(srcH, 1u));
    const float4 center = gSource.SampleLevel(gLinear, uv, 0);
    const uint mode = min(gCompareMode, 2u);
    if (mode == 0u || gColourStrength == 0.0)
        return center;

    // Four cheap axis taps estimate a continuous normal. A strong first derivative is already a good
    // edge normal. Near the centre of a symmetric thin line the gradient vanishes, so second-derivative
    // magnitudes provide the candidate direction; the orthogonal candidate is tested only in that
    // ambiguous case. This handles diagonal wires without a large fixed kernel.
    const float4 left1  = gSource.SampleLevel(gLinear, saturate(uv - float2(texel.x, 0.0)), 0);
    const float4 right1 = gSource.SampleLevel(gLinear, saturate(uv + float2(texel.x, 0.0)), 0);
    const float4 up1    = gSource.SampleLevel(gLinear, saturate(uv - float2(0.0, texel.y)), 0);
    const float4 down1  = gSource.SampleLevel(gLinear, saturate(uv + float2(0.0, texel.y)), 0);
    const float yC = dot(center.rgb, kLuma);
    const float yL = dot(left1.rgb, kLuma);
    const float yR = dot(right1.rgb, kLuma);
    const float yU = dot(up1.rgb, kLuma);
    const float yD = dot(down1.rgb, kLuma);

    const float2 gradient = float2(yR - yL, yD - yU);
    const float gradLen = length(gradient);
    const float curveX = abs(2.0 * yC - yL - yR);
    const float curveY = abs(2.0 * yC - yU - yD);
    const float curvatureLen = sqrt(curveX * curveX + curveY * curveY);

    float2 normal;
    if (gradLen > 1e-6)
    {
        normal = gradient / gradLen;
    }
    else if (curvatureLen > 1e-6)
    {
        // Axis-only second derivatives cannot distinguish the two diagonal signs at an exactly symmetric
        // centre. Pick one continuous candidate here; the orthogonal candidate below resolves that ambiguity.
        normal = normalize(float2(sqrt(curveX + 1e-8), sqrt(curveY + 1e-8)));
    }
    else
    {
        normal = float2(1.0, 0.0);
    }

    const float width = max(abs(gTransferStrength), 1e-4);
    float2 offset = normal * texel * width;
    float3 a = gSource.SampleLevel(gLinear, saturate(uv - offset), 0).rgb;
    float3 b = gSource.SampleLevel(gLinear, saturate(uv + offset), 0).rgb;
    float yA = dot(a, kLuma);
    float yB = dot(b, kLuma);
    float coreScore = ExperimentThinCoreScore(yC, yA, yB);

    // For a perfectly centred diagonal line gradLen can be nearly zero and the curvature candidate has
    // a sign ambiguity. Two extra taps, only on that ambiguous path, decide between the candidate and
    // its perpendicular by selecting the direction with the stronger thin-core signature.
    if (gradLen <= max(abs(gDebugScale), 1e-6))
    {
        const float2 alternate = float2(-normal.y, normal.x);
        const float2 altOffset = alternate * texel * width;
        const float3 altA = gSource.SampleLevel(gLinear, saturate(uv - altOffset), 0).rgb;
        const float3 altB = gSource.SampleLevel(gLinear, saturate(uv + altOffset), 0).rgb;
        const float altYA = dot(altA, kLuma);
        const float altYB = dot(altB, kLuma);
        const float altScore = ExperimentThinCoreScore(yC, altYA, altYB);
        if (altScore > coreScore)
        {
            normal = alternate;
            a = altA;
            b = altB;
            yA = altYA;
            yB = altYB;
            coreScore = altScore;
        }
    }

    const float ySide = 0.5 * (yA + yB);
    const float detected = ExperimentSmoothThreshold(gDebugScale, coreScore);
    const float centralContrast = max(abs(yC - ySide), max(abs(gDebugScale), 1e-6));
    const float sideSimilarity = 1.0 - saturate(abs(yA - yB) / centralContrast);
    // 0 = no halo rejection, 1 = linear side-similarity rejection, >1 = increasingly strict.
    // No slider-range clamp is applied: manually entered values (2, 5, 10, ...) retain their meaning.
    const float haloWeight = pow(max(sideSimilarity, 1e-6), gMaxRatio);
    const float amount = gColourStrength * detected * haloWeight;

    float3 result;
    if (mode == 1u)
    {
        // Luma-only changes the current/core pixel along the neutral axis. It never writes a blur footprint
        // into neighbours; neighbouring pixels are modified only if they independently satisfy thin-core detection.
        result = center.rgb + (ySide - yC) * amount;
    }
    else
    {
        // RGB mode also changes only the current/core pixel, moving it toward the two matching sides.
        result = lerp(center.rgb, 0.5 * (a + b), amount);
    }
    return float4(SanitizeFinite3(result, center.rgb), center.a);
}

float4 ExperimentLegacyLowResolutionFilter(float2 uv, bool postNr)
{
    if (!postNr && gTransfer == 4u)
        return ExperimentEdgeCoreAttenuation(uv);

    uint srcW, srcH;
    gSource.GetDimensions(srcW, srcH);
    const float2 texel = 1.0 / float2(max(srcW, 1u), max(srcH, 1u));
    const float radius = gTransferStrength;
    const float strength = gColourStrength;
    const float threshold = gDebugScale;

    const float4 center = gSource.SampleLevel(gLinear, uv, 0);
    if (postNr && (gTransfer == 0u || strength == 0.0))
        return center;

    const float2 dx = float2(texel.x * radius, 0.0);
    const float2 dy = float2(0.0, texel.y * radius);
    const float4 left  = gSource.SampleLevel(gLinear, saturate(uv - dx), 0);
    const float4 right = gSource.SampleLevel(gLinear, saturate(uv + dx), 0);
    const float4 up    = gSource.SampleLevel(gLinear, saturate(uv - dy), 0);
    const float4 down  = gSource.SampleLevel(gLinear, saturate(uv + dy), 0);

    const float yC = dot(center.rgb, kLuma);
    const float yL = dot(left.rgb, kLuma);
    const float yR = dot(right.rgb, kLuma);
    const float yU = dot(up.rgb, kLuma);
    const float yD = dot(down.rgb, kLuma);
    const float localMin = min(yC, min(min(yL, yR), min(yU, yD)));
    const float localMax = max(yC, max(max(yL, yR), max(yU, yD)));
    const float edgeRange = localMax - localMin;
    float edge = gTransfer == 1u ? 1.0 : ExperimentSmoothThreshold(threshold, edgeRange);

    float3 softened;
    if (gTransfer == 1u || gTransfer == 2u)
    {
        softened = (4.0 * center.rgb + left.rgb + right.rgb + up.rgb + down.rgb) * 0.125;
    }
    else
    {
        const float2 gradient = float2(yR - yL, yD - yU);
        const float gradLen = length(gradient);
        const float2 normal = gradLen > 1e-6 ? gradient / gradLen : float2(1.0, 0.0);
        const float2 offset = normal * texel * radius;
        const float3 a = gSource.SampleLevel(gLinear, saturate(uv - offset), 0).rgb;
        const float3 b = gSource.SampleLevel(gLinear, saturate(uv + offset), 0).rgb;
        softened = (a + 2.0 * center.rgb + b) * 0.25;

        if (postNr && gTransfer == 4u)
        {
            const float4 baseL = gModel.SampleLevel(gLinear, saturate(uv - dx), 0);
            const float4 baseR = gModel.SampleLevel(gLinear, saturate(uv + dx), 0);
            const float4 baseU = gModel.SampleLevel(gLinear, saturate(uv - dy), 0);
            const float4 baseD = gModel.SampleLevel(gLinear, saturate(uv + dy), 0);
            const float baseGrad =
                length(float2(dot(baseR.rgb - baseL.rgb, kLuma), dot(baseD.rgb - baseU.rgb, kLuma)));
            const float nrGrad = length(float2(yR - yL, yD - yU));
            // This saturate limits a derived confidence mask, not a user control.
            const float excess = saturate((nrGrad - baseGrad) / max(nrGrad + abs(threshold), 1e-5));
            edge *= excess;
        }
    }

    return float4(lerp(center.rgb, softened, strength * edge), center.a);
}

float4 ExperimentProcessedNrAt(float2 uv)
{
    // This is the first post-NR stage. Every later ghost/band helper samples THIS processed field,
    // never raw N, so NR50 edge treatment composes sequentially with band suppression.
    return gTransfer != 0u ? ExperimentLegacyLowResolutionFilter(uv, true)
                           : gSource.SampleLevel(gLinear, saturate(uv), 0);
}

float3 ExperimentProcessedResponseAt(float2 uv)
{
    const float3 processedNr = ExperimentProcessedNrAt(uv).rgb;
    const float3 prepared = gModel.SampleLevel(gLinear, saturate(uv), 0).rgb;
    return processedNr - prepared; // current E after optional NR50 edge treatment
}

float3 ExperimentCrossProcessedResponseBlur(float2 uv, float2 texel, float radius)
{
    const float r = max(abs(radius), 1e-4);
    const float2 dx = float2(texel.x * r, 0.0);
    const float2 dy = float2(0.0, texel.y * r);
    const float3 c = ExperimentProcessedResponseAt(uv);
    return (4.0 * c +
            ExperimentProcessedResponseAt(uv - dx) + ExperimentProcessedResponseAt(uv + dx) +
            ExperimentProcessedResponseAt(uv - dy) + ExperimentProcessedResponseAt(uv + dy)) * 0.125;
}

float ExperimentGhostEdgeMask(float2 uv, float2 texel)
{
    const float r = max(abs(gEnvironmentDetail), 1e-4);
    const float2 dx = float2(texel.x * r, 0.0);
    const float2 dy = float2(0.0, texel.y * r);
    const float yC = dot(gOriginal.SampleLevel(gLinear, uv, 0).rgb, kLuma);
    const float yL = dot(gOriginal.SampleLevel(gLinear, saturate(uv - dx), 0).rgb, kLuma);
    const float yR = dot(gOriginal.SampleLevel(gLinear, saturate(uv + dx), 0).rgb, kLuma);
    const float yU = dot(gOriginal.SampleLevel(gLinear, saturate(uv - dy), 0).rgb, kLuma);
    const float yD = dot(gOriginal.SampleLevel(gLinear, saturate(uv + dy), 0).rgb, kLuma);
    const float localMin = min(yC, min(min(yL, yR), min(yU, yD)));
    const float localMax = max(yC, max(max(yL, yR), max(yU, yD)));
    return ExperimentSmoothThreshold(gSkinColour, localMax - localMin);
}

float ExperimentSameDirectionMask(float3 preparationDelta, float3 response)
{
    const float prepY = dot(preparationDelta, kLuma);
    const float responseY = dot(response, kLuma);
    // Same sign means the model continues the preprocessing change: ghost candidate.
    // Opposite sign means the model is undoing the preparation (for example restoring the dark wire core).
    if (prepY * responseY <= 0.0)
        return 0.0;
    return ExperimentSmoothThreshold(gSkinDetail, abs(prepY));
}

float4 ExperimentNrArtifactControl(float2 uv)
{
    uint srcW, srcH;
    gSource.GetDimensions(srcW, srcH);
    const float2 texel = 1.0 / float2(max(srcW, 1u), max(srcH, 1u));

    const float4 processedNr = ExperimentProcessedNrAt(uv);
    const float3 prepared = gModel.SampleLevel(gLinear, uv, 0).rgb;   // B
    const float3 sharp = gOriginal.SampleLevel(gLinear, uv, 0).rgb;  // S
    const float3 preparationDelta = prepared - sharp;                 // D = B-S
    float3 response = processedNr.rgb - prepared;                     // E after optional NR edge treatment

    const uint bandMode = min(gDebugView, 2u);
    const uint guardMode = min(gCompareMode, 3u);
    float edgeMask = 1.0;
    if (bandMode != 0u || guardMode >= 2u)
        edgeMask = ExperimentGhostEdgeMask(uv, texel);

    // Second stage: suppress low/mid-frequency ghost components from the already edge-treated response.
    // With bandMode=0 there are no neighbourhood response taps at all.
    if (bandMode != 0u && gReplaceDetailStrength != 0.0)
    {
        const float3 lowBand = ExperimentCrossProcessedResponseBlur(uv, texel, gModelWorkScale);
        const float lowMask = ExperimentSameDirectionMask(preparationDelta, lowBand) * edgeMask;
        response -= lowBand * (gReplaceDetailStrength * lowMask);

        if (bandMode >= 2u)
        {
            const float3 midBlur = ExperimentCrossProcessedResponseBlur(uv, texel, gResidualConfidenceUnused);
            const float3 midBand = midBlur - lowBand;
            const float midMask = ExperimentSameDirectionMask(preparationDelta, midBand) * edgeMask;
            response -= midBand * (gReplaceDetailStrength * midMask);
        }
    }

    // Third stage: Ghost Guard evaluates the response left by all previous stages.
    // Basic mode has no low/mid taps and no sharp-edge-neighbourhood taps.
    if (guardMode != 0u && gMaxRatio != 0.0 && gEnvironmentColour != 0.0)
    {
        float guardMask = ExperimentSameDirectionMask(preparationDelta, response);
        if (guardMode >= 2u)
            guardMask *= edgeMask;

        // Max suppression is semantically a cap, but neither it nor Guard strength is clamped to 0..1.
        const float amount = min(gMaxRatio * guardMask, gEnvironmentColour);
        if (guardMode < 3u)
        {
            response *= 1.0 - amount;
        }
        else
        {
            // Directional mode suppresses only RGB components whose E continues D in the same direction.
            const float channelThreshold = max(abs(gSkinDetail) * 0.25, 1e-6);
            const float3 threshold3 = float3(channelThreshold, channelThreshold, channelThreshold);
            const float3 zero3 = float3(0.0, 0.0, 0.0);
            const float3 sameChannel =
                step(threshold3, abs(preparationDelta)) *
                step(zero3, preparationDelta * response);
            response *= 1.0 - amount * sameChannel;
        }
    }

    // Final stage. Off preserves the previous B + processed(E) behaviour exactly.
    // On removes the preparation footprint algebraically: S + processed(E) = N_processed - (B-S).
    const float3 base = gCompareSwap != 0u ? sharp : prepared;
    return float4(SanitizeFinite3(base + response, processedNr.rgb), processedNr.a);
}

#include "dlssnr_resize.hlsli"

groupshared float4 gExposureReduce[64];

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID, uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
    const uint lane = groupThreadId.y * 8u + groupThreadId.x;

    if (gMode == 3)
    {
        if (groupId.x >= gWidth || groupId.y >= gHeight)
            return;

        uint fullW, fullH;
        gSource.GetDimensions(fullW, fullH);

        const uint tx0 = (groupId.x * fullW) / gWidth;
        const uint tx1 = ((groupId.x + 1u) * fullW) / gWidth;
        const uint ty0 = (groupId.y * fullH) / gHeight;
        const uint ty1 = ((groupId.y + 1u) * fullH) / gHeight;
        const uint endX = max(tx1, tx0 + 1u);
        const uint endY = max(ty1, ty0 + 1u);

        float localSum = 0.0;
        [loop] for (uint ty = ty0 + groupThreadId.y; ty < endY; ty += 8u)
        {
            [loop] for (uint tx = tx0 + groupThreadId.x; tx < endX; tx += 8u)
            {
                const float3 c = max(gSource.Load(int3(min(tx, fullW - 1u), min(ty, fullH - 1u), 0)).rgb, 0.0);
                const float luma = dot(c, kLuma);
                localSum += isfinite(luma) ? max(luma, 0.0) : 0.0;
            }
        }

        gExposureReduce[lane] = float4(localSum, 0.0, 0.0, 0.0);
        GroupMemoryBarrierWithGroupSync();
        [unroll] for (uint stride = 32u; stride > 0u; stride >>= 1u)
        {
            if (lane < stride)
                gExposureReduce[lane].x += gExposureReduce[lane + stride].x;
            GroupMemoryBarrierWithGroupSync();
        }

        if (lane == 0u)
        {
            const uint taken = (endX - tx0) * (endY - ty0);
            gTarget[groupId.xy] = float4(taken > 0u ? gExposureReduce[0].x / (float) taken : 0.0,
                                         0.0, 0.0, 1.0);
        }
        return;
    }

    if (gMode == 11)
    {
        const uint srcW = max(gExposureSourceWidth, 1u);
        const uint srcH = max(gExposureSourceHeight, 1u);
        const float preExposure =
            (isfinite(gPreExposure) && gPreExposure > 1e-6) ? gPreExposure : 1.0;
        const float protection = saturate(gExposureProtection * 0.01);

        float weightedBufferLuma = 0.0;
        float weightedSceneLogLuma = 0.0;
        float totalPixels = 0.0;

        [loop] for (uint index = lane; index < 4096u; index += 64u)
        {
            const uint tx = index & 63u;
            const uint ty = index >> 6u;
            const uint x0 = (tx * srcW) / 64u;
            const uint x1 = ((tx + 1u) * srcW) / 64u;
            const uint y0 = (ty * srcH) / 64u;
            const uint y1 = ((ty + 1u) * srcH) / 64u;
            const uint tileW = max(x1 - x0, 1u);
            const uint tileH = max(y1 - y0, 1u);
            const float pixels = (float) tileW * (float) tileH;
            const float tileMean = max(SanitizeFinite(gSource.Load(int3(tx, ty, 0)).r, 0.0), 0.0);

            weightedBufferLuma += tileMean * pixels;
            totalPixels += pixels;
            if (protection > 0.0)
            {
                const float sceneLuma = max(tileMean / preExposure, 1e-8);
                weightedSceneLogLuma += clamp(log2(sceneLuma), -24.0, 24.0) * pixels;
            }
        }

        gExposureReduce[lane] = float4(weightedBufferLuma, weightedSceneLogLuma, totalPixels, 0.0);
        GroupMemoryBarrierWithGroupSync();
        [unroll] for (uint stride = 32u; stride > 0u; stride >>= 1u)
        {
            if (lane < stride)
                gExposureReduce[lane].xyz += gExposureReduce[lane + stride].xyz;
            GroupMemoryBarrierWithGroupSync();
        }

        const float allPixels = gExposureReduce[0].z;
        const float averageBufferLuma =
            allPixels > 0.0 ? gExposureReduce[0].x / allPixels : 0.0;
        float meteredSceneLuma = averageBufferLuma / preExposure;

        if (protection > 0.0 && allPixels > 0.0)
        {
            const float referenceLogLuma = gExposureReduce[0].y / allPixels;
            const float highlightKneeEv = lerp(3.0, 1.0, protection);
            const float highlightCompressionSlope = lerp(1.0, 0.35, protection);
            float protectedLinearSum = 0.0;

            [loop] for (uint index2 = lane; index2 < 4096u; index2 += 64u)
            {
                const uint tx2 = index2 & 63u;
                const uint ty2 = index2 >> 6u;
                const uint x0 = (tx2 * srcW) / 64u;
                const uint x1 = ((tx2 + 1u) * srcW) / 64u;
                const uint y0 = (ty2 * srcH) / 64u;
                const uint y1 = ((ty2 + 1u) * srcH) / 64u;
                const uint tileW = max(x1 - x0, 1u);
                const uint tileH = max(y1 - y0, 1u);
                const float pixels = (float) tileW * (float) tileH;
                const float tileMean = max(SanitizeFinite(gSource.Load(int3(tx2, ty2, 0)).r, 0.0), 0.0);
                const float sceneLuma = max(tileMean / preExposure, 1e-8);
                const float logLuma = clamp(log2(sceneLuma), -24.0, 24.0);
                const float deltaEv = logLuma - referenceLogLuma;
                float compressedLogLuma = logLuma;
                if (deltaEv > highlightKneeEv)
                    compressedLogLuma = referenceLogLuma + highlightKneeEv +
                                        (deltaEv - highlightKneeEv) * highlightCompressionSlope;
                protectedLinearSum += exp2(clamp(compressedLogLuma, -24.0, 24.0)) * pixels;
            }

            gExposureReduce[lane].w = protectedLinearSum;
            GroupMemoryBarrierWithGroupSync();
            [unroll] for (uint stride2 = 32u; stride2 > 0u; stride2 >>= 1u)
            {
                if (lane < stride2)
                    gExposureReduce[lane].w += gExposureReduce[lane + stride2].w;
                GroupMemoryBarrierWithGroupSync();
            }

            const float protectedAverage = gExposureReduce[0].w / allPixels;
            if (isfinite(protectedAverage) && protectedAverage > 1e-8)
                meteredSceneLuma = protectedAverage;
        }

        if (lane == 0u)
        {
            float white = preExposure * meteredSceneLuma * (0.82 / 0.18);
            gTarget[uint2(0, 0)] = float4(isfinite(white) && white > 1e-8 ? white : 1.0, 0, 0, 1);
        }
        return;
    }

    if (id.x >= gWidth || id.y >= gHeight)
        return;

    // Normalised, so the source may be any size relative to this dispatch.
    float2 uv = (float2(id.xy) + 0.5) / float2(gWidth, gHeight);

    if (gMode == 14)
    {
        gTarget[id.xy] = gSource.SampleLevel(gLinear, uv, 0);
        return;
    }

    if (gMode == 16)
    {
        gTarget[id.xy] = ExperimentLegacyLowResolutionFilter(uv, false);
        return;
    }
    if (gMode == 17)
    {
        gTarget[id.xy] = ExperimentNrArtifactControl(uv);
        return;
    }

    // Experimental private-DLSS carrier, not an ordinary colour image. Neutral 0.5 encodes zero;
    // values below it carry darkening. A reversible signed compression avoids clipping negative
    // edits at the DLSS input. Scale small linear-light edits up before storing them in FP16;
    // at unit scale, a dark scene's edits round to neutral before DLSS even sees them.
    if (gMode == 12)
    {
        gTarget[id.xy] = float4(NrEncodeResizeField(NrPairedResizeField(int2(id.xy), int2(gWidth, gHeight))), 1);
        return;
    }
#ifndef VK_MODE
    if (gMode == 13)
    {
        // Compatibility fallback for non-FP16 model outputs: preserve the old combined carrier+gate pass.
        const int2 p = int2(id.xy);
        const float4 proxyRaw = gSource.Load(int3(p, 0));
        const float4 answerRaw = gModel.Load(int3(p, 0));
        gTarget[id.xy] = answerRaw;
        gKeep[id.xy] = float4(proxyRaw.rgb, DirectDetailGateAt(p));
        return;
    }
    if (gMode == 15)
    {
        // Normal Direct NR gated-detail path: alpha-only gate, with no redundant NR50 copy.
        gTarget[id.xy] = float4(0.0, 0.0, 0.0, DirectDetailGateAt(int2(id.xy)));
        return;
    }
#endif
    if (gMode == 9)
    {
        float3 source = gSource.Load(int3(id.xy, 0)).rgb;
        float3 answer = gModel.Load(int3(id.xy, 0)).rgb;
        if (gPassthrough == 0) { source = SrgbToLinear(source); answer = SrgbToLinear(answer); }
        float3 d = SanitizeFinite3(answer - source, 0.0);
        gTarget[id.xy] = float4(0.5 + 0.5 * d / (1.0 / 64.0 + abs(d)), 1.0);
        return;
    }
    if (gMode == 10)
    {
        // Mode-local fields: guide active sizes/origins, and motion-to-working-pixel scale.
        uint2 dp = min(uint2(uv * uint2(gGuideWidth, gGuideHeight)),
                       uint2(gGuideWidth, gGuideHeight) - 1) + uint2(gDebugView, gCompareMode);
        uint2 size = uint2(gTransferStrength, gColourStrength);
        uint2 mp = min(uint2(uv * size), size - 1) + uint2(gCompareSwap, gTransfer);
        float z = gSource.Load(int3(dp, 0)).r;
        float2 mv = gModel.Load(int3(mp, 0)).xy * float2(gMvScaleX, gMvScaleY);
        gTarget[id.xy] = float4(isfinite(z) ? z : 0.0, 0, 0, 1);
        gKeep[id.xy] = float4(all(isfinite(mv)) ? mv : float2(0, 0), 0, 1);
        return;
    }
    if (gMode == 5)
    {
        float3 difference = SanitizeFinite3(gModel.Load(int3(id.xy, 0)).rgb -
                                            gSource.Load(int3(id.xy, 0)).rgb, 0.0);
        float3 d = difference / max(gResidualScale, 1e-4);
        gTarget[id.xy] = float4(0.5 + 0.5 * d / (1.0 + abs(d)), 1.0);
        return;
    }
    if (gMode == 6)
    {
        float4 base = gSource.Load(int3(id.xy, 0));
        float3 encoded = SanitizeFinite3(gModel.Load(int3(id.xy, 0)).rgb, 0.5);
        // Limit the inverse near its poles: DLSS can ring outside the carrier's [0,1] range.
        float3 signedEdit = clamp(2.0 * encoded - 1.0, -0.999, 0.999);
        float3 edit = signedEdit / (1.0 - abs(signedEdit)) * max(gResidualScale, 1e-4);
        gTarget[id.xy] = float4(max(SanitizeFinite3(base.rgb + edit, base.rgb), 0.0), base.a);
        return;
    }
    if (gMode == 7)
    {
        gTarget[id.xy] = 1.0;
        return;
    }

    if (gMode == 8)
    {
        // Already encoded: restore the input range without applying the tone curve again.
        float4 raw = gSource.Load(int3(id.xy, 0));
        gTarget[id.xy] = float4(saturate(SanitizeFinite3(raw.rgb, 0.5)), raw.a);
        return;
    }

    if (gMode == 2)
    {
        uint srcW, srcH;
        gSource.GetDimensions(srcW, srcH);

        // Nothing to do when the sizes already agree.
        if (srcW == gWidth && srcH == gHeight)
        {
            gTarget[id.xy] = gSource.Load(int3(id.xy, 0));
            return;
        }

        // Exact Output Scaling filters are dispatched outside this shader. This local path handles
        // Area, Bilinear, Point and SSIM Sharp, plus Area fallback when an external filter fails.
        const uint filter = min(gTransfer, 11u);
        const float2 sampleUv = (float2(id.xy) + 0.5) / float2(gWidth, gHeight);
        if (filter == 1u)
        {
            float4 sampled = gSource.SampleLevel(gLinear, sampleUv, 0);
            const int2 center = int2(clamp(floor(sampleUv * float2(srcW, srcH)), 0.0,
                                           float2(srcW - 1, srcH - 1)));
            sampled.a = DownsampleLoadClamped(center, srcW, srcH).a;
            gTarget[id.xy] = sampled;
            return;
        }
        if (filter == 4u)
        {
            const int2 center = int2(clamp(floor(sampleUv * float2(srcW, srcH)), 0.0,
                                           float2(srcW - 1, srcH - 1)));
            gTarget[id.xy] = DownsampleLoadClamped(center, srcW, srcH);
            return;
        }
        if (filter == 11u)
        {
            gTarget[id.xy] = DownsampleSsimSharp(sampleUv, srcW, srcH, gWidth, gHeight);
            return;
        }

        // Exact area-weighted downsampling avoids the aliasing of a single bilinear tap.
        // Adapted from hhkbble's multi-pass contribution.
        const float x0 = ((float) id.x * (float) srcW) / (float) gWidth;
        const float x1 = ((float) (id.x + 1) * (float) srcW) / (float) gWidth;
        const float y0 = ((float) id.y * (float) srcH) / (float) gHeight;
        const float y1 = ((float) (id.y + 1) * (float) srcH) / (float) gHeight;
        const float area = (x1 - x0) * (y1 - y0);

        const int i0 = (int) floor(x0);
        const int i1 = (int) ceil(x1) - 1;
        const int j0 = (int) floor(y0);
        const int j1 = (int) ceil(y1) - 1;

        float3 acc = 0.0;

        for (int j = j0; j <= j1; ++j)
        {
            const int jj = clamp(j, 0, (int) srcH - 1);
            const float aY = max(y0, (float) j);
            const float bY = min(y1, (float) j + 1.0);
            const float wy = max(bY - aY, 0.0);

            for (int i = i0; i <= i1; ++i)
            {
                const int ii = clamp(i, 0, (int) srcW - 1);
                const float aX = max(x0, (float) i);
                const float bX = min(x1, (float) i + 1.0);
                acc += gSource.Load(int3(ii, jj, 0)).rgb * (max(bX - aX, 0.0) * wy);
            }
        }

        const int acx = clamp((int) floor(((float) id.x + 0.5) * (float) srcW / (float) gWidth), 0, (int) srcW - 1);
        const int acy = clamp((int) floor(((float) id.y + 0.5) * (float) srcH / (float) gHeight), 0, (int) srcH - 1);

        gTarget[id.xy] = float4(acc / area, gSource.Load(int3(acx, acy, 0)).a);
        return;
    }

    if (gMode == 0)
    {
        float4 source = gSource.Load(int3(id.xy, 0));
        float3 frame = max(source.rgb, float3(0.0, 0.0, 0.0));

        // Kept so the resolve has the frame as it was, rather than having to reconstruct it.
        gKeep[id.xy] = float4(frame, source.a);

        // Some games hand DLSS a frame that has already been through their tonemapper. The game says
        // which in its own DLSS creation flags, and converting one that needs no conversion is pure
        // damage, so it goes through untouched.
        if (gPassthrough != 0)
        {
            gTarget[id.xy] = float4(frame, source.a);
            return;
        }

        // What the model is shown. Mode 2 -- the default -- scales the frame and encodes it, and that
        // is all: the game is going to tone map this picture later, so tone mapping it here as well
        // shows the model a doubly compressed image. Measured against Cyberpunk's own numbers, the
        // Reinhard proxy handed the model a scene value of 1.0 as 0.55 and 1.5 as 0.64 -- flat, dark,
        // and nothing like the finished frame it was trained on. The model then synthesised weakly,
        // judged tone on a picture that does not exist, and its answer had to be un-crushed on the way
        // back. Mode 0 keeps that old curve, mode 1 the fitted one.
        // A soft knee instead of a hard ceiling. Anything above 0.75 is rolled off rather than
        // clipped, so the model is never shown a field of flat white whose blown pixels flip between
        // frames -- unstable input is unstable output, and this is where a bright scene would produce
        // it. The resolve reproduces this exactly, so the two agree on what the frame's own proxy is.
        // The classic soft knee, or -- when the reversible proxy is on -- the unclipped Neutwo encode
        // that shows the model highlight gradation the knee throws away. Reached only when the frame
        // is not passthrough (handled and returned above), so NeutwoEncode never sees a tone-mapped
        // frame. Both are undone by the resolve: the knee approximately, Neutwo exactly.
        float3 normalized = frame / WhitePoint();
        float3 display;
        if (gReversibleMode == 0)
            display = SoftKnee(normalized);        // soft knee
        else if (gReversibleMode >= 3)
            display = HybridEncode(normalized);    // 3 hybrid composed, 4 hybrid replace -- same curve
        else
            display = NeutwoEncode(normalized);    // 1 composed, 2 replace -- both the full Neutwo proxy

        // The reversible proxy forces opaque alpha -- feature 18 expects an opaque colour input, and
        // the frame's own alpha is not part of what the model reads. The knee path keeps the frame's
        // alpha, so the default stays byte-identical.
        float alpha = gReversibleMode != 0 ? 1.0 : source.a;

        gTarget[id.xy] = float4(LinearToSrgb(display), alpha);
        return;
    }

    // Comparison, decided before anything is read, because side by side changes which part of the
    // frame this pixel is showing rather than just which version of it.
    //
    //   1  side by side  each half carries the whole frame, so both are squeezed horizontally
    //   2  wipe          one frame cut at the split, nothing resampled
    //
    // Neither needs the menu open to stay up. The wipe's split is a setting like any other; the menu
    // is only how you drag it.
    float2 cmpUv = uv;
    bool showOriginal = false;
    bool onDivider = false;
    bool outsideFrame = false;

    if (gCompareMode == 1)
    {
        showOriginal = (uv.x < 0.5) != (gCompareSwap != 0);

        // Each half is half as wide as the frame and just as tall, so the frame cannot fill it and
        // keep its shape. Stretching it to fit is what made both sides look squashed. Fitting it
        // properly leaves the halves letterboxed, which is the honest way round: a comparison that
        // changes the shape of what it is comparing is not showing you the picture.
        //
        // Zoom decides which is given up. At 1 the whole frame is there at its right proportions
        // with bars above and below; at 2 the half is filled and the sides are cropped away.
        float2 half2 = float2(uv.x < 0.5 ? uv.x * 2.0 : (uv.x - 0.5) * 2.0, uv.y) - 0.5;
        cmpUv = float2(0.5 + half2.x / gCompareZoom, 0.5 + half2.y * 2.0 / gCompareZoom);

        outsideFrame = cmpUv.x < 0.0 || cmpUv.x > 1.0 || cmpUv.y < 0.0 || cmpUv.y > 1.0;
        onDivider = abs(uv.x - 0.5) < (1.0 / max(gWidth, 1u));
    }
    else if (gCompareMode == 2)
    {
        showOriginal = (uv.x < gCompareSplit) != (gCompareSwap != 0);
        onDivider = abs(uv.x - gCompareSplit) < (1.0 / max(gWidth, 1u));
    }

    // Sampled rather than loaded: when the model ran at a reduced resolution these are smaller than the
    // frame, and its edit is enlarged here while the frame underneath stays untouched.
    float4 proxySample = gSource.SampleLevel(gLinear, cmpUv, 0);
    float4 modelSample;
#ifndef VK_MODE
    if (gTransfer == 7u)
        modelSample = DirectSpatialSample(cmpUv);
    else
#endif
        modelSample = gModel.SampleLevel(gLinear, cmpUv, 0);

    // Nothing was encoded on the way in, so nothing is decoded here either.
    float3 proxy = gPassthrough != 0 ? proxySample.rgb : SrgbToLinear(proxySample.rgb);
    float3 model = gPassthrough != 0 ? modelSample.rgb : SrgbToLinear(modelSample.rgb);

    // The model's own answer, kept before the matched-residual block below can rewrite `model`, so the
    // replace decode uses what the model returned rather than the residual reconstruction.
    float3 modelDirect = model;
    float4 originalSample = gCompareMode == 1 ? gOriginal.SampleLevel(gLinear, cmpUv, 0)
                                              : gOriginal.Load(int3(id.xy, 0));

    // All three pictures have to share a scale before their luminances can be compared. The proxy and
    // the model come back from an sRGB decode, so they sit in 0..1 where 1 is the white point; the
    // frame is raw linear and runs well past that. Comparing them unnormalised is a real bug and it
    // reads exactly like the model has stopped adding detail: with the frame several times larger,
    // the shadow branch never fires, every pixel takes the highlight branch, and the clamp flattens
    // the result to a near-constant scale. Colour still moves, because that comes from the model's
    // own hue, which is what makes the failure so confusing to look at.
    const float normScale = gPassthrough != 0 ? 1.0 : WhitePoint();
    float3 original = originalSample.rgb / normScale;

    float originalLuma = dot(original, kLuma);
    float proxyLuma = dot(proxy, kLuma);

    // Apply the model. Off outputs the frame as the upscaler produced it (clean) while the pass keeps
    // running -- so with Hold frame you can freeze a frame and toggle this to A/B the same frozen frame
    // with and without Neural Rendering. In passthrough the frame is already display-referred.
    if (gApplyModel == 0)
    {
        gTarget[id.xy] = float4(max(originalSample.rgb, 0.0), originalSample.a);
        return;
    }

    if (gDebugView == 1)
    {
        gTarget[id.xy] = float4(proxy * gDebugScale, originalSample.a);
        return;
    }

    if (gDebugView == 2)
    {
        gTarget[id.xy] = float4(model * gDebugScale, originalSample.a);
        return;
    }

    uint proxyW, proxyH;
    gSource.GetDimensions(proxyW, proxyH);
    const bool modelRanSmall = proxyW != gWidth || proxyH != gHeight;
    if ((gTransfer == 3 || gTransfer == 4) && (proxyW < gWidth || proxyH < gHeight))
    {
        proxy = gPassthrough != 0 ? saturate(original)
                : (gReversibleMode == 0 ? saturate(SoftKnee(original))
                   : gReversibleMode >= 3 ? HybridEncode(original) : NeutwoEncode(original));
        model = NrReconstructModel(proxy, cmpUv, modelSample.rgb);
        modelDirect = model;
        proxyLuma = dot(proxy, kLuma);
    }

#ifndef VK_MODE
    // Optional native-P100-guided detail remapping. The entire operation is folded into this resolve:
    // no NR100/P100-detail intermediate is written. Direct uses Aux2 as its selected reconstructed
    // P50 reference; Upscaled NR residual uses Source, which already is reconstructed P100.
    const bool finalDetailExperiment =
        ((gDirectResolveFlags & 1u) != 0u || gTransfer == 6u) &&
        (gResidualHistoryValidUnused != 0u || gResidualMotionBaseXUnused != 0u);
    if (finalDetailExperiment)
    {
        const bool direct = (gDirectResolveFlags & 1u) != 0u;
        const uint structureMode = min(gResidualMotionBaseXUnused, 2u);
        const float3 baselineCenter = proxy;
        const float3 gainCenter =
            direct && structureMode != 0u ? ExperimentGainReferenceAt(cmpUv) : baselineCenter;
        const float3 nativeProxyCenter =
            direct ? baselineCenter : ExperimentNativeProxyAt(cmpUv, normScale);
        const float3 guidedEdit =
            ExperimentP100GuidedEdit(cmpUv, model, baselineCenter, gainCenter, nativeProxyCenter, normScale);
        model = proxy + guidedEdit;
        modelDirect = model;
    }
#endif

    float3 edit = model - proxy;
    if (gTransfer == 2)
    {
        float3 carrier = clamp(2.0 * SanitizeFinite3(modelSample.rgb, 0.5) - 1.0, -0.999, 0.999);
        edit = (1.0 / 64.0) * carrier / (1.0 - abs(carrier));
    }

    // Coring was tried here and removed: the per-frame churn's amplitude overlaps the real detail's,
    // so an amplitude threshold cannot separate them -- it only relocated the noise to the threshold.

    if (gDebugView == 3)
    {
        // Amplified and centred on grey, so both directions of the edit are visible at once.
        float3 shown = saturate(0.5 + edit * 20.0);
        gTarget[id.xy] = float4(SrgbToLinear(shown) * gDebugScale, originalSample.a);
        return;
    }

    // Composition uses the current model answer; temporal residual accumulation is a separate pass.

    // Rebuild the full-resolution proxy and add only the upsampled model difference.
    // Skip ordinary matched residual at native resolution to preserve Classic's exact arithmetic.
    // Residual transfer and cube scaling are adapted from hhkbble's multi-pass contribution.
    if ((gTransfer == 1 && modelRanSmall) || gTransfer == 2 || gTransfer == 6)
    {
        // Match the encode's curve, passthrough and saturation before cube-scaling the residual.
        // An out-of-range reconstructed proxy would collapse the residual scale to zero.
        float3 fullProxy = gPassthrough != 0
                               ? saturate(original)
                               : (gReversibleMode == 0   ? saturate(SoftKnee(original))
                                  : gReversibleMode >= 3 ? HybridEncode(original)
                                                         : NeutwoEncode(original));
        proxy = fullProxy;
        proxyLuma = dot(proxy, kLuma);

        // At the same rate there is no residual to carry: the model's own picture is already at the
        // frame's resolution, and P + (m - p) collapses to m exactly.
        model = CubeScaleResidual(fullProxy, fullProxy + edit);
        if (gTransfer == 2 || gTransfer == 6) modelDirect = model;
    }

    // Rescale the model answer to the original luminance and restore headroom lost by the proxy.
    float modelLuma = dot(model, kLuma);
    float3 upgraded;

    if (modelLuma <= 1e-5)
    {
        // The model can return an empty frame for an input it cannot read. Rescaling that collapses
        // the picture to black, so the frame is handed back untouched instead.
        upgraded = original;
    }
    else
    {
        float ratio;

        if (originalLuma < proxyLuma)
        {
            // Below what the proxy showed: the frame's own luminance is the target.
            ratio = originalLuma / max(proxyLuma, 1e-6);
        }
        else
        {
            // Above it, the difference is headroom the proxy could not represent -- brightness the
            // frame really has and the model never saw. It is handed back on top of the model's own
            // answer rather than scaled away, which is what kept highlights from being muted.
            ratio = (modelLuma + max(0.0, originalLuma - proxyLuma)) / modelLuma;
        }

        // Keep the RGB blend within [0,1]; strength above 1 amplifies the bounded luminance ratio below.
        upgraded = lerp(original, HueOkLab(model * ratio, model), saturate(gTransferStrength));
    }

    // Detail strength decides how much of the model's picture is reached at all; colour strength
    // decides whether its colour comes with it. At 0 the frame keeps the game's own hue exactly and
    // only its light carries the model's verdict; at 1 the model's colour arrives as well.
    float upgradedLuma = dot(upgraded, kLuma);

    // A common luminance floor suppresses unstable ratios in near-black pixels.
    const float kRatioFloor = 1.0 / 512.0;
    float lumaRatio = (upgradedLuma + kRatioFloor) / (originalLuma + kRatioFloor);

    // Amplify detail through a luminance-ratio power while preserving neutral edits.
    const float amplified = pow(max(lumaRatio, 1e-6), 1.0 + max(gTransferStrength - 1.0, 0.0));

    // Independent one-sided guards. Highlight guard only limits brightening. Darkening guard only
    // raises the lower luminance-ratio bound; at 100% its floor is zero, so darkening is uncapped.
    const float highlightGuard = max(gMaxRatio, 1.0);
    const float darkeningFloor = 1.0 - saturate(gMaxDarkening / 100.0);
    float boundedRatio = clamp(amplified, darkeningFloor, highlightGuard);

    // Exactly one while the ratio is already inside the guard, so a frame that never needed bounding
    // is untouched rather than rounded, and strength zero stays bit-identical.
    upgraded *= boundedRatio / max(lumaRatio, 1e-6);

    // Both blend endpoints obey the luminance guard. Above colour strength 1, boost OkLab chroma
    // while preserving lightness/hue, then compress out-of-gamut colours toward neutral.
    float3 result = lerp(original * boundedRatio, upgraded, min(gColourStrength, 1.0));

    if (gColourStrength > 1.0)
        result = ClampAp1(FromOkLab(float3(1.0, gColourStrength, gColourStrength) * ToOkLab(max(result, 0.0))));

    // Replace modes decode the model answer directly; passthrough colour needs no inverse transform.
    if (gReversibleMode == 2)
        result = gPassthrough != 0 ? modelDirect : NeutwoDecode(modelDirect);
    else if (gReversibleMode == 4)
        result = gPassthrough != 0 ? modelDirect : HybridDecode(modelDirect);

    // Restore native luminance detail with a bounded, positive ratio (including near-black edges).
    if ((gReversibleMode == 2 || gReversibleMode == 4) && gModelWorkScale > 0.0 &&
        gModelWorkScale < 0.999 && gReplaceDetailStrength > 0.0)
    {
        float2 tap = clamp(round(1.0 / gModelWorkScale), 1.0, 4.0) / float2(gWidth, gHeight);
        float3 neighbours = gOriginal.SampleLevel(gLinear, cmpUv + float2(tap.x, 0), 0).rgb +
                            gOriginal.SampleLevel(gLinear, cmpUv - float2(tap.x, 0), 0).rgb +
                            gOriginal.SampleLevel(gLinear, cmpUv + float2(0, tap.y), 0).rgb +
                            gOriginal.SampleLevel(gLinear, cmpUv - float2(0, tap.y), 0).rgb;
        float blur = dot(original + neighbours / normScale, kLuma) / 5.0;
        float contrast = (originalLuma - blur) / (max(originalLuma, blur) + kRatioFloor);
        result *= exp2(clamp(gReplaceDetailStrength, 0.0, 2.0) * clamp(contrast, -1.0, 1.0));
    }

#ifndef VK_MODE
    // Direct NR detail recovery. gAux2 is the selected full-resolution reconstruction of P50.
    // Full-lost-detail needs no gate texture at all. NR-gated mode samples only gAux.a.
    if (gDirectDetailMode != 0)
    {
        const float4 p50Reference = gAux2.SampleLevel(gLinear, cmpUv, 0);
        const float3 p50 = gPassthrough != 0 ? p50Reference.rgb : SrgbToLinear(p50Reference.rgb);
        const float p100Y = max(dot(proxy, kLuma), 0.0);
        const float p50Y = max(dot(p50, kLuma), 0.0);
        float lostStops = log2((p100Y + kRatioFloor) / (p50Y + kRatioFloor));
        if (!isfinite(lostStops))
            lostStops = 0.0;

        float gate = 1.0;
        if (gDirectDetailMode == 2u)
        {
            const float4 detailInfo = gAux.SampleLevel(gLinear, cmpUv, 0);
            gate = lerp(1.0, saturate(detailInfo.a), saturate(gDirectDetailMaskStrength));
        }
        const float detailRatio = exp2(lostStops * gate);
        if (isfinite(detailRatio) && detailRatio > 0.0)
            result *= detailRatio;
    }
#endif

    // Back out of the normalised space the composition worked in.
    result *= normScale;

    if (gSkinProtection != 0)
    {
        // Classify the untouched frame, never NR's recoloured output. Controls
        // attenuate the final edit, including replace mode and all model passes.
        float3 displayRgb = gPassthrough != 0 ? original : LinearToSrgb(saturate(original));
        float mask = SkinColourWeight(displayRgb);
        float detail = lerp(gEnvironmentDetail, gSkinDetail, mask);
        float colour = lerp(gEnvironmentColour, gSkinColour, mask);
        float baseY = dot(max(originalSample.rgb, 0.0), kLuma);
        float editedY = dot(max(result, 0.0), kLuma);
        float wantedY = lerp(baseY, editedY, detail);
        float3 baseChroma = originalSample.rgb / max(baseY, 1e-6);
        float3 editedChroma = result / max(editedY, 1e-6);
        // Exact endpoints avoid changing the default image or fully protected pixels.
        if (detail == 0.0 && colour == 0.0)
            result = originalSample.rgb;
        else if (detail != 1.0 || colour != 1.0)
            result = ClampAp1(lerp(baseChroma, editedChroma, colour) * wantedY);
        if (gShowSkinMask != 0)
            result = mask.xxx * normScale;
    }

    // The stabilized ratio above deliberately adds kRatioFloor, which is useful for model composition
    // but can hide large real percentage losses in very dark pixels. Enforce the darkening control
    // once more on the final edited luminance without that stabilization term. This makes 0% mean
    // exactly "no luminance darkening" and N% cap the final reduction to N%, including deep shadows,
    // replace modes and colour/skin composition. 100% remains a no-op.
    const float finalDarkeningFloor = 1.0 - saturate(gMaxDarkening / 100.0);
    if (finalDarkeningFloor > 0.0 && gShowSkinMask == 0)
    {
        const float baseY = dot(max(originalSample.rgb, 0.0), kLuma);
        const float editedY = dot(max(result, 0.0), kLuma);
        const float minimumY = baseY * finalDarkeningFloor;
        if (editedY < minimumY)
        {
            if (editedY > 1e-6)
                result *= minimumY / editedY;
            else if (baseY > 1e-6)
                result = max(originalSample.rgb, 0.0) * finalDarkeningFloor;
        }
    }

    // The side being shown untouched takes the frame as it arrived, past every step above.
    if (showOriginal)
        result = originalSample.rgb;

    // The letterbox. The sampler clamps rather than wrapping, so without this the bars would be the
    // frame's edge row smeared down the screen.
    if (outsideFrame)
        result = float3(0.0, 0.0, 0.0);

    // A hairline so the two sides are never mistaken for one picture.
    if (onDivider)
        result = float3(WhitePoint(), WhitePoint(), WhitePoint());

    gTarget[id.xy] = float4(max(result, float3(0.0, 0.0, 0.0)), originalSample.a);
}
