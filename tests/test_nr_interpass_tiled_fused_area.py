"""v7 tiled Fused Area regression: exact Area footprint, alpha and edge tiles.

The completed P100 guided value is modeled as an arbitrary float4. Caching
a native guided value must not change its independent Area contributions.
This checks that 8x8 groups cover every source texel needed by every output
pixel, including non-divisible dimensions and the last partial groups.
No CPU test proves GPU synchronization or measured GPU speed.
"""
import math
import random

PITCH = 20

def bounds(native_w, native_h, out_w, out_h, gx, gy):
    x0, y0 = gx*8, gy*8
    x1, y1 = min(x0+8,out_w), min(y0+8,out_h)
    fx0 = math.floor(float(x0)*float(native_w)/float(out_w))
    fy0 = math.floor(float(y0)*float(native_h)/float(out_h))
    fx1 = math.ceil(float(x1)*float(native_w)/float(out_w))
    fy1 = math.ceil(float(y1)*float(native_h)/float(out_h))
    return (fx0, fy0, fx1, fy1)


def area_value(native, x, y, ow, oh, tile=None, origin=None):
    nh, nw = len(native), len(native[0])
    x0, x1 = float(x)*float(nw)/float(ow), float(x+1)*float(nw)/float(ow)
    y0, y1 = float(y)*float(nh)/float(oh), float(y+1)*float(nh)/float(oh)
    denom=max((x1-x0)*(y1-y0),1e-8)
    sx, ex=math.floor(x0),math.ceil(x1)-1
    sy, ey=math.floor(y0),math.ceil(y1)-1
    acc=[0.]*4
    for j in range(sy,ey+1):
        wy=max(min(y1,float(j)+1)-max(y0,float(j)),0.)
        for i in range(sx,ex+1):
            wx=max(min(x1,float(i)+1)-max(x0,float(i)),0.)
            if tile is None:
                source=native[j][i]
            else:
                assert origin[0]<=i<origin[0]+len(tile[0])
                assert origin[1]<=j<origin[1]+len(tile)
                source=tile[j-origin[1]][i-origin[0]]
            for k in range(4):
                acc[k]+=source[k]*(wx*wy)
    acc=[v/denom for v in acc]
    acx=max(0,min(nw-1,math.floor((x+0.5)*nw/ow)))
    acy=max(0,min(nh-1,math.floor((y+0.5)*nh/oh)))
    acc[3]=native[acy][acx][3]
    return [max(0.,min(1., v if math.isfinite(v) else 0.5)) for v in acc[:3]]+[acc[3]]


def check(nw,nh,ow,oh,rng):
    # Force arbitrary signed and >1 native guides: saturate happens AFTER Area.
    native=[[[rng.uniform(-.4,1.4) for _ in range(3)]+[rng.random()]
            for x in range(nw)] for y in range(nh)]
    locations=0
    reused=0
    for gy in range((oh+7)//8):
        for gx in range((ow+7)//8):
            sx,sy,ex,ey=bounds(nw,nh,ow,oh,gx,gy)
            assert sx>=0 and sy>=0 and ex<=nw and ey<=nh
            assert ex>sx and ey>sy
            fits=ex-sx<=PITCH and ey-sy<=PITCH
            tile=[native[y][sx:ex] for y in range(sy,ey)] if fits else None
            nrecon=(ex-sx)*(ey-sy)
            individually=0
            for y in range(gy*8,min((gy+1)*8,oh)):
                for x in range(gx*8,min((gx+1)*8,ow)):
                    r=area_value(native,x,y,ow,oh)
                    t=area_value(native,x,y,ow,oh,tile,(sx,sy))
                    assert all(abs(a-b)<1e-12 for a,b in zip(r,t)),(nw,nh,ow,oh,x,y,r,t)
                    x0,x1=float(x)*nw/ow,float(x+1)*nw/ow
                    y0,y1=float(y)*nh/oh,float(y+1)*nh/oh
                    individually+=(math.ceil(x1)-math.floor(x0))*(math.ceil(y1)-math.floor(y0))
                    locations+=1
            reused+=individually-nrecon if fits else 0
    return locations,reused


def main():
    rng=random.Random(20261010)
    cases=[(39,27),(51,37),(127,71),(128,72),(153,87),(191,107),
           (192,108),(257,143),(384,216)]
    pixels=saved=0
    for nw,nh in cases:
        for percentage in (25,33,40,45,49,51,53,55,60,63,65,66,67,70,73,75,77,80,85,90):
            ow=max(1,int(nw*percentage/100+.5))
            oh=max(1,int(nh*percentage/100+.5))
            n,r=check(nw,nh,ow,oh,rng)
            pixels+=n
            saved+=r
    assert saved > 0
    print(f"v7 Fused Area tiled parity PASS: {pixels} output pixels, "
          f"{saved} duplicate P100 reconstructions avoided in model")

if __name__=="__main__":
    main()
