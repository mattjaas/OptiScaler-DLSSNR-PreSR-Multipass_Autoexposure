#ifndef DLSSNR_GAIN_PREVIEW_GROUPS
#define DLSSNR_GAIN_PREVIEW_GROUPS 8
#endif
// Partial histograms are combined on CPU, avoiding a second GPU dispatch/reduction.
// Experimental diagnostic only. Raw unscaled model-proxy edit; output never enters image composition.
Texture2D<float4> Proxy : register(t0);
Texture2D<float4> Answer : register(t1);
Texture2D<float2> Motion : register(t2);
RWTexture2D<uint4> Result : register(u0);
cbuffer Params : register(b0)
{
    uint Width, Height, PhaseX, PhaseY;
    float MotionThreshold, ChangeThreshold;
    uint RequireChange, PreviousValid;
    uint SerialLow, SerialHigh, GridX, GridY;
    uint MinMoving, MinChanged, Reserved0, Reserved1;
    float4 PreviousMotion[288];
};
groupshared uint HistR[1024];
groupshared uint HistG[1024];
groupshared uint4 Parts[256];
uint HistogramBin(float value)
{
    const uint base = 0x33800000u >> 18;
    uint bits = asuint(value) >> 18;
    return bits < base ? 0 : 1 + min(1022u, bits - base);
}
float2 ReadMotion(uint i)
{
    uint2 cell = uint2(i % 32, i / 32);
    uint2 at = min(((cell * 2 + 1) * uint2(Width, Height)) / uint2(64, 36), uint2(Width - 1, Height - 1));
    return Motion.Load(int3(at, 0));
}
[numthreads(256, 1, 1)]
void GainPreviewMain(uint lane : SV_GroupIndex, uint3 group : SV_GroupID)
{
    if (Width == 0 || Height == 0 || Width > 16384 || Height > 16384 ||
        GridX == 0 || GridY == 0 || GridX > 256 || GridY > 144)
    {
        if (lane == 0 && group.x == 0)
        {
            Result[uint2(256, 0)] = uint4(0, 0, 0, SerialLow);
            Result[uint2(256, 1)] = uint4(0, 0, 0, SerialHigh);
        }
        return;
    }
    float threshold = isfinite(MotionThreshold) && MotionThreshold > 0 ? clamp(MotionThreshold, .001, 10.) : .1;
    float change = isfinite(ChangeThreshold) && ChangeThreshold > 0 ? clamp(ChangeThreshold, .001, 10.) : .0625;
    uint4 part = 0;
    for (uint pair = lane; pair < 288; pair += 256)
    {
        float2 a = ReadMotion(pair * 2), b = ReadMotion(pair * 2 + 1);
        if (group.x == 0) Result[uint2(pair % 257, DLSSNR_GAIN_PREVIEW_GROUPS * 2 + pair / 257)] = uint4(asuint(a), asuint(b));
        for (uint k = 0; k < 2; ++k)
        {
            float2 v = k == 0 ? a : b;
            float2 old = k == 0 ? PreviousMotion[pair].xy : PreviousMotion[pair].zw;
            bool valid = all(isfinite(v)) && all(abs(v) < 1e6);
            part.y += valid;
            part.x += valid && dot(v, v) > threshold * threshold;
            part.z += valid && all(isfinite(old)) && dot(v - old, v - old) > change * change;
        }
    }
    Parts[lane] = part;
    GroupMemoryBarrierWithGroupSync();
    for (uint stride = 128; stride; stride >>= 1)
    {
        if (lane < stride) Parts[lane] += Parts[lane + stride];
        GroupMemoryBarrierWithGroupSync();
    }
    uint4 motion = Parts[0];
    uint gate = motion.y >= 548 && motion.x >= clamp(MinMoving, 1u, 576u) ? 1 : 0;
    if (gate == 1 && RequireChange != 0)
        gate = PreviousValid == 0 ? 3 : motion.z < clamp(MinChanged, 1u, 576u) ? 2 : 1;
    if (lane == 0)
    {
        if (group.x == 0) Result[uint2(256, 0)] = uint4(motion.x, motion.y, GridX * GridY, SerialLow);
        Result[uint2(256, group.x * 2 + 1)] = uint4(0, gate, motion.z, SerialHigh);
    }
    if (gate != 1) return; // no edit reads or histograms on an unchanged/nonmoving field
    // No lane can reuse Parts until every lane has taken the uniform gate above.
    GroupMemoryBarrierWithGroupSync();
    for (uint i = lane; i < 1024; i += 256) { HistR[i] = 0; HistG[i] = 0; }
    GroupMemoryBarrierWithGroupSync();
    uint validEdits = 0;
    for (uint i = group.x * 256 + lane; i < GridX * GridY; i += 256 * DLSSNR_GAIN_PREVIEW_GROUPS)
    {
        uint2 cell = uint2(i % GridX, i / GridX);
        uint2 at = min(((cell * 16 + uint2(PhaseX & 7, PhaseY & 7) * 2 + 1) * uint2(Width, Height)) /
                      (uint2(GridX, GridY) * 16), uint2(Width - 1, Height - 1));
        float2 edit = Answer.Load(int3(at, 0)).rg - Proxy.Load(int3(at, 0)).rg;
        bool valid = all(isfinite(edit)); validEdits += valid;
        if (valid && edit.r > 0) InterlockedAdd(HistR[HistogramBin(edit.r)], 1);
        if (valid && edit.g > 0) InterlockedAdd(HistG[HistogramBin(edit.g)], 1);
    }
    Parts[lane].x = validEdits;
    GroupMemoryBarrierWithGroupSync();
    for (uint stride = 128; stride; stride >>= 1)
    {
        if (lane < stride) Parts[lane].x += Parts[lane + stride].x;
        GroupMemoryBarrierWithGroupSync();
    }
    Result[uint2(lane, group.x * 2)] = uint4(HistR[lane*4], HistR[lane*4+1], HistR[lane*4+2], HistR[lane*4+3]);
    Result[uint2(lane, group.x * 2 + 1)] = uint4(HistG[lane*4], HistG[lane*4+1], HistG[lane*4+2], HistG[lane*4+3]);
    if (lane == 0) Result[uint2(256, group.x * 2 + 1)] = uint4(Parts[0].x, gate, motion.z, SerialHigh);
}
