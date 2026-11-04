// Separate optimized method. The original TemporalGhost.hlsl remains byte-for-byte unchanged.
#include "CompactGhostState.hlsli"
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
Texture2D<uint2> Previous : register(t2);
Texture2D<uint> Reconstructed : register(t3);
RWTexture2D<uint> WriteDepth : register(u0);
RWTexture2D<uint2> WriteState : register(u1);
uint2 PreviousState(int2 p) { return Previous.Load(int3(p, 0)); }
uint2 CurrentState(int2 p) { return WriteState[p]; }
void StoreState(int2 p, uint2 bits) { WriteState[p] = bits; }
bool Inside(int2 p) { return all(p >= 0) && p.x < int(width) && p.y < int(height); }
[numthreads(8, 8, 1)] void CompactGhostMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height)
        return;
    int2 p = int2(id.xy);
#if DLSSNR_COMPACT_GHOST_STAGE == 1
    float nearZ = -1;
    uint2 chosen = min(id.xy * 2, uint2(fullWidth, fullHeight) - 1);
    [unroll] for (uint y = 0; y < 2; ++y)[unroll] for (uint x = 0; x < 2; ++x)
    {
        uint2 q = min(id.xy * 2 + uint2(x, y), uint2(fullWidth, fullHeight) - 1);
        float z = Depth.Load(int3(q, 0));
        float n = isfinite(z) && z >= 0 && z <= 1 ? (inverted != 0 ? z : 1 - z) : -1;
        if (n > nearZ)
        {
            nearZ = n;
            chosen = q;
        }
    }
    float2 mv = Motion.Load(int3(chosen, 0));
    bool valid = nearZ > 0 && all(isfinite(mv)) && all(abs(mv) < 65504);
    uint phase = (chosen.x & 1u) | ((chosen.y & 1u) << 1);
    StoreState(p, valid ? PackGhost(float4(nearZ, float(phase), mv)) : uint2(0, 0));
    if (initialized == 0 || !valid)
        return;
    // Scatter uses unquantized current MV/depth; the stored state's age slot temporarily holds phase.
    float2 previousPos = (float2(chosen) + .5 + mv) * .5 - .5;
    int2 base = int2(floor(previousPos));
    float2 f = frac(previousPos);
    [unroll] for (int y = 0; y < 2; ++y)[unroll] for (int x = 0; x < 2; ++x)
    {
        int2 q = base + int2(x, y);
        float weight = (x ? f.x : 1 - f.x) * (y ? f.y : 1 - f.y);
        if (Inside(q) && weight > .05)
            InterlockedMax(WriteDepth[q], asuint(nearZ));
    }
#else
    uint2 packed = CurrentState(p);
    float4 current = UnpackGhost(packed);
    uint phase = packed.x & 3u;
    current.y = 0;
    if (initialized == 0 || current.x <= 0)
    {
        StoreState(p, PackGhost(current));
        return;
    }
    float2 chosen = float2(id.xy * 2 + uint2(phase & 1u, phase >> 1));
    float2 previousPos = (chosen + .5 + current.zw) * .5 - .5;
    int2 oldPixel = int2(round(previousPos));
    if (!Inside(oldPixel))
    {
        StoreState(p, PackGhost(current));
        return;
    }
    float4 old = UnpackGhost(PreviousState(oldPixel));
    if (!all(isfinite(old)) || old.x <= 0)
    {
        StoreState(p, PackGhost(current));
        return;
    }
    float nFrames = isfinite(frames) ? clamp(frames, 1, 120) : 16;
    float tolerance = isfinite(depthThreshold) ? clamp(depthThreshold, .001, .5) : .05;
    float precision = isfinite(depthPrecision) && depthPrecision >= 0 && depthPrecision <= .001
                          ? max(depthPrecision * 2, 1e-6)
                          : 2.0 / 65535;
    // Bound the depth packing error for the current pair; no half-precision depth comparison.
    precision = max(precision, max(old.x, current.x) * (1.0 / 65536.0));
    if (current.x > old.x * (1 + tolerance) + precision)
    {
        StoreState(p, PackGhost(current));
        return;
    }
    float gate =
        isfinite(motionGate) ? clamp(motionGate, .1, 16) * float(fullHeight) / 1080 : 2.0 * float(fullHeight) / 1080;
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
    bool exposed = reconstructed > current.x * (1 + tolerance) + precision;
    if (exposed)
    {
        StoreState(p, PackGhost(float4(current.x, nFrames, current.zw)));
        return;
    }
    float2 delta = current.zw - old.zw;
    float errorSquared = dot(delta, delta), bestSquared = errorSquared;
    bool depthStep = old.x > current.x * (1 + tolerance) + precision;
    if (errorSquared <= gate * gate * 4 && !depthStep)
    {
        // No depth boundary or contact-layer test can fire; ordinary timer continuation is certain.
        StoreState(p, PackGhost(float4(current.x, max(0, min(old.y, nFrames) - 1), current.zw)));
        return;
    }
    bool oldNearBoundary = false;
    if (errorSquared > gate * gate * 4 || depthStep)
    {
        const int2 axis[4] = { int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1) };
        [unroll] for (uint ring = 0; ring < 2; ++ring)[unroll] for (uint k = 0; k < 4; ++k)
        {
            int distance = max(1, int(round((ring == 0 ? 2.0 : 8.0) * float(height) / 1080)));
            int2 q = oldPixel + axis[k] * distance;
            if (!Inside(q))
                continue;
            float4 neighbour = UnpackGhost(PreviousState(q));
            if (!all(isfinite(neighbour)) || neighbour.x <= 0)
                continue;
            if (neighbour.x <= current.x * 1.25 + precision &&
                abs(neighbour.x - current.x) + precision < abs(old.x - current.x) * .5)
            {
                float2 neighbourDelta = current.zw - neighbour.zw;
                bestSquared = min(bestSquared, dot(neighbourDelta, neighbourDelta));
                oldNearBoundary = oldNearBoundary || old.x > neighbour.x * (1 + tolerance) + precision;
            }
        }
    }
    float error = sqrt(errorSquared), bestError = sqrt(bestSquared);
    bool contact = error > gate * 2 && bestError + gate < error * .5 && old.x > current.x + precision;
    bool disappeared = oldNearBoundary && old.x > current.x * (1 + tolerance) + precision;
    float age = error <= bestError + gate ? max(0, min(old.y, nFrames) - 1) : 0;
    if (exposed || contact || disappeared)
        age = nFrames;
    StoreState(p, PackGhost(float4(current.x, age, current.zw)));
#endif
}
