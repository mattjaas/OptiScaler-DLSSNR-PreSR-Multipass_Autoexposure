"""Regression for classical P100 radius-one guided/bilinear stencil."""
import math
import random

def sample(a,x,y):
    return a[min(max(y,0),len(a)-1)][min(max(x,0),len(a[0])-1)]

def run(model,proxy,x,y,guide,optimized):
    b=(math.floor(x+.5),math.floor(y+.5))
    f=(math.floor(x),math.floor(y))
    fx,fy=x-f[0],y-f[1]
    wx=([1-fx,fx,0] if b[0]!=f[0] else [0,1-fx,fx])
    wy=([1-fy,fy,0] if b[1]!=f[1] else [0,1-fy,fy])
    bn=[0.]*3
    bp=[0.]*3
    weighted=[0.]*3
    total=0.
    for oy in range(-1,2):
        for ox in range(-1,2):
            cx=min(max(b[0]+ox,0),len(proxy[0])-1)
            cy=min(max(b[1]+oy,0),len(proxy)-1)
            a=sample(proxy,cx,cy)
            n=sample(model,cx,cy)
            color=sum((a[k]-guide[k])**2 for k in range(3))/3.
            space=(cx-x)**2+(cy-y)**2
            w=2**(-0.7213475204444817*(color/.08**2+space/1.2**2))
            total+=w
            for k in range(3):
                weighted[k]+=(n[k]-a[k])*w
            if optimized:
                wbi=wx[ox+1]*wy[oy+1]
                for k in range(3):
                    bn[k]+=(n[k]-a[k])*wbi
    if not optimized:
        for dy in range(2):
            for dx in range(2):
                bw=(fx if dx else 1-fx)*(fy if dy else 1-fy)
                n=sample(model,f[0]+dx,f[1]+dy)
                a=sample(proxy,f[0]+dx,f[1]+dy)
                for k in range(3):
                    bn[k]+=n[k]*bw
                    bp[k]+=a[k]*bw
        bn=[bn[k]-bp[k] for k in range(3)]
    guided=[z/total for z in weighted] if total>1e-8 else bn
    return [bn[k]*.25+guided[k]*.75 for k in range(3)]

def main():
    rng=random.Random(6)
    cases=0
    for ww,wh,nw,nh in [(19,13,39,27),(25,17,39,27),(26,18,39,27),(33,23,39,27),(1,1,3,3)]:
        p=[[[rng.random() for c in range(3)] for x in range(ww)] for y in range(wh)]
        m=[[[rng.random() for c in range(3)] for x in range(ww)] for y in range(wh)]
        for _ in range(300):
            x=rng.randrange(nw);y=rng.randrange(nh)
            sx=(x+.5)/nw*ww-.5;sy=(y+.5)/nh*wh-.5
            guide=sample(p,math.floor(sx+.5),math.floor(sy+.5))
            a=run(m,p,sx,sy,guide,True)
            b=run(m,p,sx,sy,guide,False)
            assert all(abs(a[i]-b[i])<1e-12 for i in range(3)),(ww,wh,x,y,a,b)
            cases+=1
    print("Classical v6 radius-one bilinear/guided parity PASS:",cases)

if __name__=="__main__":
    main()
