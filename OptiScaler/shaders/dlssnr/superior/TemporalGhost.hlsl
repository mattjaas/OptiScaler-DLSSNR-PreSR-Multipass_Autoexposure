#include "GhostDepth.hlsli"
// Guides only. Reconstruction scatters current camera-space depth through current->previous MV,
// so ordinary camera depth changes are not mistaken for an occluder by comparing two NDC frames directly.
cbuffer Params : register(b0)
{
    uint mode;
    float frames;
    uint width, height;
    float returnFrames, depthThreshold;
    uint initialized;
    float motionGate;
    uint inverted;
    float depthPrecision;
    float unused;
    uint fullWidth, fullHeight;
};
Texture2D<float> Depth : register(t0);
Texture2D<float2> Motion : register(t1);
Texture2D<float4> Previous : register(t2); // near depth, age, matched MV.xy in working pixels
Texture2D<uint> Reconstructed : register(t3);
RWTexture2D<uint> WriteDepth : register(u0);
RWTexture2D<float4> WriteState : register(u1);

float4 CurrentGuide(uint2 p, out float2 previousPos)
{
    float nearZ = -1;
    uint2 chosen = min(p * 2, uint2(fullWidth, fullHeight) - 1);
    [unroll] for (uint y = 0; y < 2; ++y)[unroll] for (uint x = 0; x < 2; ++x)
    {
        uint2 q = min(p * 2 + uint2(x, y), uint2(fullWidth, fullHeight) - 1);
        float z = Depth.Load(int3(q, 0));
        float n = GhostNearDepth(z, inverted);
        if (n > nearZ)
        {
            nearZ = n;
            chosen = q;
        }
    }
    float2 mv = Motion.Load(int3(chosen, 0));
    bool valid = nearZ > 0 && all(isfinite(mv)) && all(abs(mv) < 65504);
    previousPos = (float2(chosen) + .5 + mv) * .5 - .5;
    return valid ? float4(nearZ, 0, mv) : float4(0, 0, 0, 0);
}
#include "GhostSurfaceContinuation.hlsli"
bool Inside(int2 p) { return all(p >= 0) && p.x < int(width) && p.y < int(height); }
[numthreads(8, 8, 1)] void TemporalGhostMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height)
        return;
    int2 p = int2(id.xy);
#if DLSSNR_TEMPORAL_GHOST_STAGE == 0
    WriteDepth[p] = 0; // positive near depth, farther = zero, bitcast preserves order
#else
    float2 previousPos;
    float4 current = CurrentGuide(id.xy, previousPos);
#if DLSSNR_TEMPORAL_GHOST_STAGE == 1
    if (current.x <= 0)
        return;
    int2 base = int2(floor(previousPos));
    float2 f = frac(previousPos);
    [unroll] for (int y = 0; y < 2; ++y)[unroll] for (int x = 0; x < 2; ++x)
    {
        int2 q = base + int2(x, y);
        float weight = (x ? f.x : 1 - f.x) * (y ? f.y : 1 - f.y);
        if (Inside(q) && weight > .05)
            InterlockedMax(WriteDepth[q], asuint(current.x));
    }
#else
    if (initialized == 0 || current.x <= 0)
    {
        WriteState[p] = current;
        return;
    }
    float nFrames = isfinite(frames) ? clamp(frames, 1, 120) : 16;
    float tolerance = isfinite(depthThreshold) ? clamp(depthThreshold, .001, .5) : .05;
    float precision = isfinite(depthPrecision) && depthPrecision >= 0 && depthPrecision <= .001
                          ? max(depthPrecision * 2, 1e-6)
                          : 2.0 / 65535;
    int2 oldPixel = int2(round(previousPos));
    if (!Inside(oldPixel))
    {
        WriteState[p] = current;
        return;
    } // new frame edge is not proven previously occluded
    float4 old = Previous.Load(int3(oldPixel, 0));
    if (!all(isfinite(old)) || old.x <= 0)
    {
        WriteState[p] = current;
        return;
    }
    float gate =
        isfinite(motionGate) ? clamp(motionGate, .1, 16) * float(fullHeight) / 1080 : 2.0 * float(fullHeight) / 1080;
    float gateSquared = gate * gate;
    float2 delta = current.zw - old.zw;
    float errorSquared = dot(delta, delta), bestSquared = errorSquared;
    // Keep the extra texture reads inside a real branch: HLSL 2018 && does not short-circuit calls.
    [branch] if (old.y > 1 && (current.x > old.x * (1 + tolerance) + precision || errorSquared > gateSquared * 4))
    {
        if (ContinueGhostSurface(current, old, (previousPos + .5) * 2 - .5 - current.zw, tolerance, precision, gate))
        {
            WriteState[p] = float4(current.x, max(0, min(old.y, nFrames) - 1), current.zw);
            return;
        }
    }
    bool occluding = current.x > old.x * (1 + tolerance) + precision;
    // A closer current object must not inherit a background's timer.
    if (occluding)
    {
        WriteState[p] = current;
        return;
    }
    bool previousOccluder = old.x > current.x * (1 + tolerance) + precision;
    if (errorSquared <= gateSquared * 4 && !previousOccluder)
    {
        WriteState[p] = float4(current.x, max(0, min(old.y, nFrames) - 1), current.zw);
        return;
    }
    bool relativeMotion = false;
    // A fast background can reproject into the MIDDLE of a wide old occluder.
    // Follow relative displacement to a previous background witness instead of requiring a nearby edge.
    if (old.x > current.x + precision)
    {
        int2 q = int2(round(previousPos + (old.zw - current.zw) / 2));
        if (Inside(q))
        {
            float4 neighbour = Previous.Load(int3(q, 0));
            if (all(isfinite(neighbour)) && neighbour.x > 0 && neighbour.x <= current.x * 1.25 + precision &&
                abs(neighbour.x - current.x) + precision < abs(old.x - current.x) * .5)
            {
                float2 neighbourDelta = current.zw - neighbour.zw;
                bestSquared = min(bestSquared, dot(neighbourDelta, neighbourDelta));
                float2 layerDelta = old.zw - neighbour.zw;
                relativeMotion = dot(layerDelta, layerDelta) >= gateSquared;
                if (relativeMotion && previousOccluder && old.x > neighbour.x * (1 + tolerance) + precision)
                {
                    WriteState[p] = float4(current.x, nFrames, current.zw);
                    return;
                }
            }
        }
    }
    const int2 axis[4] = { int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1) };
    [unroll] for (uint ring = 0; ring < 2; ++ring)[unroll] for (uint k = 0; k < 4; ++k)
    {
        int distance = max(1, int(round((ring == 0 ? 2.0 : 8.0) * float(height) / 1080)));
        int2 q = oldPixel + axis[k] * distance;
        if (!Inside(q))
            continue;
        float4 neighbour = Previous.Load(int3(q, 0));
        if (!all(isfinite(neighbour)) || neighbour.x <= 0)
            continue;
        if (neighbour.x <= current.x * 1.25 + precision &&
            abs(neighbour.x - current.x) + precision < abs(old.x - current.x) * .5)
        {
            float2 neighbourDelta = current.zw - neighbour.zw;
            bestSquared = min(bestSquared, dot(neighbourDelta, neighbourDelta));
            // Foreground and matching background from the SAME previous frame:
            // a common camera acceleration is not relative motion between layers.
            float2 layerDelta = old.zw - neighbour.zw;
            bool differentMotion = dot(layerDelta, layerDelta) >= gateSquared;
            relativeMotion = relativeMotion || differentMotion;
            if (differentMotion && previousOccluder && old.x > neighbour.x * (1 + tolerance) + precision)
            {
                // A matching visible background layer and a different foreground motion prove disappearance.
                WriteState[p] = float4(current.x, nFrames, current.zw);
                return;
            }
        }
    }
    // For motion starting this frame, look where the old occluder would be now.
    // Compare two CURRENT-frame MVs; this also handles a fast background around a still foreground.
    if (!relativeMotion && old.x > current.x + precision)
    {
        int2 source = int2(round((previousPos + .5) * 2 - .5 - old.zw));
        if (all(source >= 0) && source.x < int(fullWidth) && source.y < int(fullHeight))
        {
            float z = Depth.Load(int3(source, 0));
            float nearZ = GhostNearDepth(z, inverted);
            if (nearZ > current.x + precision && abs(nearZ - old.x) <= old.x * tolerance + precision)
            {
                float2 foreground = Motion.Load(int3(source, 0));
                float2 layerDelta = foreground - current.zw;
                relativeMotion = all(isfinite(foreground)) && all(abs(foreground) < 65504) &&
                                 dot(layerDelta, layerDelta) >= gateSquared;
            }
        }
    }
    // A newly moving foreground may have had zero MV in the previous frame.
    // Only on a depth change without old-frame evidence, inspect the current boundary.
    if (!relativeMotion && old.x > current.x + precision)
    {
        [unroll] for (uint ring = 0; ring < 2; ++ring)[unroll] for (uint k = 0; k < 4; ++k)
        {
            int distance = max(ring == 0 ? 1 : 2, int(round((ring == 0 ? 2.0 : 8.0) * float(height) / 1080)));
            int2 q = p + axis[k] * distance;
            if (!Inside(q))
                continue;
            uint2 source = min(uint2(q) * 2 + 2 / 2, uint2(fullWidth, fullHeight) - 1);
            float z = Depth.Load(int3(source, 0));
            float nearZ = GhostNearDepth(z, inverted);
            if (nearZ <= current.x + precision || abs(nearZ - old.x) > old.x * tolerance + precision)
                continue;
            float2 foreground = Motion.Load(int3(source, 0));
            float2 layerDelta = foreground - current.zw;
            if (all(isfinite(foreground)) && all(abs(foreground) < 65504) && dot(layerDelta, layerDelta) >= gateSquared)
                relativeMotion = true;
        }
    }
    bool exposed = false;
    if (relativeMotion && previousOccluder)
    {
        // Current-frame reconstructed depth corroborates the newly moving foreground.
        int2 base = int2(floor(previousPos));
        float2 f = frac(previousPos);
        float reconstructed = 0;
        [unroll] for (int y = 0; y < 2; ++y)[unroll] for (int x = 0; x < 2; ++x)
        {
            int2 q = base + int2(x, y);
            float weight = (x ? f.x : 1 - f.x) * (y ? f.y : 1 - f.y);
            if (Inside(q) && weight > .1)
                reconstructed = max(reconstructed, asfloat(Reconstructed.Load(int3(q, 0))));
        }
        exposed = reconstructed > current.x * (1 + tolerance) + precision;
    }
    float error = sqrt(errorSquared), bestError = sqrt(bestSquared);
    bool contact = relativeMotion && error > gate * 2 && bestError + gate < error * .5 && old.x > current.x + precision;
    float age = error <= bestError + gate ? max(0, min(old.y, nFrames) - 1) : 0;
    if (exposed || contact)
        age = nFrames;
    WriteState[p] = float4(current.x, age, current.zw);
#endif
#endif
}
