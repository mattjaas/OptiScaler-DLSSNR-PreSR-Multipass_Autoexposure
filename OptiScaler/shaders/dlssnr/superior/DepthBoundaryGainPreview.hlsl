#ifndef DLSSNR_GAIN_PREVIEW_GROUPS
#define DLSSNR_GAIN_PREVIEW_GROUPS 8
#endif
// Depth-boundary partial moments combined on CPU. One probe, no mask texture/second reduction dispatch.
// Experimental diagnostic only. Raw unscaled model-proxy edit; output never enters image composition.
Texture2D<float4> Proxy : register(t0);
Texture2D<float4> Answer : register(t1);
Texture2D<float2> Motion : register(t2);
Texture2D<float> Depth : register(t3);
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
groupshared float2 Moments[256];
groupshared uint4 Parts[256];
float2 ReadMotion(uint i)
{
    uint2 cell = uint2(i % 32, i / 32);
    uint2 at = min(((cell * 2 + 1) * uint2(Width, Height)) / uint2(64, 36), uint2(Width - 1, Height - 1));
    return Motion.Load(int3(at, 0));
}
[numthreads(256, 1, 1)]
void DepthBoundaryGainPreviewMain(uint lane : SV_GroupIndex, uint3 group : SV_GroupID)
{
    if (Width < 4 || Height < 4 || Width > 16384 || Height > 16384 ||
        GridX == 0 || GridY == 0 || GridX > 256 || GridY > 144)
    {
        if (lane == 0 && group.x == 0)
        {
            Result[uint2(0, 0)] = uint4(0, 0, 0, SerialLow);
            Result[uint2(1, 0)] = uint4(0, 0, 0, SerialHigh);
        }
        return;
    }
    float threshold = isfinite(MotionThreshold) && MotionThreshold > 0 ? clamp(MotionThreshold, .001, 10.) : .1;
    float change = isfinite(ChangeThreshold) && ChangeThreshold > 0 ? clamp(ChangeThreshold, .001, 10.) : .0625;
    uint4 motion = 0;
    if ((Reserved0 & 4u) == 0u)
    {
        uint4 part = 0;
        for (uint pair = lane; pair < 288; pair += 256)
        {
            float2 a = ReadMotion(pair * 2), b = ReadMotion(pair * 2 + 1);
            if (group.x == 0) Result[uint2((pair + 2 + DLSSNR_GAIN_PREVIEW_GROUPS) % 32, (pair + 2 + DLSSNR_GAIN_PREVIEW_GROUPS) / 32)] = uint4(asuint(a), asuint(b));
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
        motion = Parts[0];
    }
    uint gate = (Reserved0 & 4u) != 0u || (motion.y >= 548 && motion.x >= clamp(MinMoving, 1u, 576u)) ? 1 : 0;
    if (gate == 1 && RequireChange != 0 && (Reserved0 & 4u) == 0u)
        gate = PreviousValid == 0 ? 3 : motion.z < clamp(MinChanged, 1u, 576u) ? 2 : 1;
    if (lane == 0)
    {
        if (group.x == 0) Result[uint2(0, 0)] = uint4(motion.x, motion.y, GridX * GridY, SerialLow);
        if (group.x == 0) Result[uint2(1, 0)] = uint4(0, gate, motion.z, SerialHigh);
    }
    if (gate != 1) return; // no edit reads or histograms on an unchanged/nonmoving field

    // The gate is uniform. Keep the same current motion baseline, but emit only A/B/count.
    GroupMemoryBarrierWithGroupSync();
    float2 moments = 0;
    uint validPairs = 0, candidates = 0;
    float precision = asfloat(Reserved1);
    precision = isfinite(precision) && precision >= 0 && precision <= .01 ? max(precision,1e-7) : 1e-7;
    for (uint i = group.x * 256 + lane; i < GridX * GridY; i += 256 * DLSSNR_GAIN_PREVIEW_GROUPS)
    {
        uint2 cell = uint2(i % GridX, i / GridX);
        uint2 at = min(((cell * 16 + uint2(PhaseX & 7, PhaseY & 7) * 2 + 1) * uint2(Width, Height)) /
                      (uint2(GridX, GridY) * 16), uint2(Width - 1, Height - 1));
        float center = Depth.Load(int3(at,0));
        center = (Reserved0 & 1u) != 0 ? center : 1-center;
        if (!isfinite(center) || center <= 0 || center > 1) continue;
        [unroll] for (uint axis=0;axis<2;++axis)
        {
            const uint2 step = axis == 0 ? uint2(0,1) : uint2(1,0);
            const uint coord = axis == 0 ? at.y : at.x, extent = axis == 0 ? Height : Width;
            // Radius 2: guard BEFORE subtracting unsigned coordinates / accessing neighbours.
            if (coord < 1 || coord >= extent-2) continue;
            const uint2 a=at-step, b=at+2*step;
            float edge = Depth.Load(int3(at+step,0));
            edge = (Reserved0 & 1u) != 0 ? edge : 1-edge;
            if (!isfinite(edge) || edge <= 0 || edge > 1 || max(center,edge)<2*min(center,edge) || abs(center-edge)<=precision) continue;
            float da=Depth.Load(int3(a,0)), db=Depth.Load(int3(b,0));
            da=(Reserved0 & 1u) != 0?da:1-da; db=(Reserved0 & 1u) != 0?db:1-db;
            if (!isfinite(da) || !isfinite(db) || min(da,db)<=0 || max(da,db)>1 ||
                abs(da-center)>.25*max(da,center) || abs(db-edge)>.25*max(db,edge) || max(da,db)<2*min(da,db)) continue;
            [branch] if ((Reserved0 & 2u) == 0u)
            {
                float2 ma=Motion.Load(int3(a,0)), mb=Motion.Load(int3(b,0));
                if (!all(isfinite(ma)) || !all(isfinite(mb)) || any(abs(ma)>=1e6) || any(abs(mb)>=1e6) ||
                    max(dot(ma,ma),dot(mb,mb))<=.01 || dot(ma-mb,ma-mb)<=4) continue;
            }
            ++candidates;
            float2 ea=Answer.Load(int3(a,0)).rg-Proxy.Load(int3(a,0)).rg;
            float2 eb=Answer.Load(int3(b,0)).rg-Proxy.Load(int3(b,0)).rg;
            // A+gB assumes no upper FP16 clipping up to 1024x. Reject rather than
            // silently predict an unreachable target when this assumption is false.
            if (!all(isfinite(ea)) || !all(isfinite(eb)) || max(max(ea.x,ea.y),max(eb.x,eb.y))>(65504.-.5)/1024.) continue;
            float2 na=max(min(ea,0.),-.5), nb=max(min(eb,0.),-.5);
            moments.x += dot(abs(na-nb),float2(.5,.5));
            moments.y += dot(abs(max(ea,0.)-max(eb,0.)),float2(.5,.5));
            ++validPairs;
        }
    }
    Parts[lane]=uint4(validPairs,candidates,0,0);
    Moments[lane]=moments;
    GroupMemoryBarrierWithGroupSync();
    for (uint stride=128;stride;stride>>=1)
    {
        if (lane<stride) { Parts[lane]+=Parts[lane+stride]; Moments[lane]+=Moments[lane+stride]; }
        GroupMemoryBarrierWithGroupSync();
    }
    if (lane==0) Result[uint2(2+group.x,0)]=uint4(asuint(Moments[0]),Parts[0].xy);
}
