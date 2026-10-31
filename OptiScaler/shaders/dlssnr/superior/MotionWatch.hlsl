// Small independent motion-only probe, used only when an existing preview cannot supply motion.
Texture2D<float2> Motion : register(t2);
RWTexture2D<uint4> Result : register(u0);
cbuffer Params : register(b0)
{
    uint Width, Height, OriginX, OriginY;
    float ScaleX, ScaleY;
    uint Unused0, Unused1;
    uint SerialLow, SerialHigh, RegionWidth, RegionHeight;
    uint4 Unused2;
    float4 UnusedMotion[288];
};
groupshared float Peak[256];
groupshared uint Valid[256];
[numthreads(256,1,1)]
void MotionWatchMain(uint lane:SV_GroupIndex)
{
    uint w,h; Motion.GetDimensions(w,h);
    bool region = Width>0 && Height>0 && Width<=16384 && Height<=16384 && RegionWidth>0 && RegionHeight>0 &&
        RegionWidth<=16384 && RegionHeight<=16384 && OriginX<=w && OriginY<=h;
    if(region) region = RegionWidth<=w-OriginX && RegionHeight<=h-OriginY && isfinite(ScaleX) && isfinite(ScaleY);
    if(!region)
    {
        if(lane==0) {Result[uint2(0,0)]=uint4(0,0,576,SerialLow);Result[uint2(1,0)]=uint4(0,0,0,SerialHigh);}
        return;
    }
    float peak=0;uint count=0;
    for(uint i=lane;i<576;i+=256)
    {
        uint2 cell=uint2(i%32,i/32);
        uint2 at=((cell*2+1)*uint2(RegionWidth,RegionHeight))/uint2(64,36)+uint2(OriginX,OriginY);
        float2 value=Motion.Load(int3(at,0))*float2(ScaleX,ScaleY);
        bool valid=all(isfinite(value)) && all(abs(value)<1e6);
        if(valid) {++count;peak=max(peak,dot(value,value));}
    }
    Peak[lane]=peak;Valid[lane]=count;
    GroupMemoryBarrierWithGroupSync();
    for(uint stride=128;stride;stride>>=1)
    {
        if(lane<stride) {Peak[lane]=max(Peak[lane],Peak[lane+stride]);Valid[lane]+=Valid[lane+stride];}
        GroupMemoryBarrierWithGroupSync();
    }
    if(lane==0)
    {
        Result[uint2(0,0)]=uint4(asuint(Peak[0]),Valid[0],576,SerialLow);
        Result[uint2(1,0)]=uint4(0,Valid[0]==576?1:0,0,SerialHigh);
    }
}
