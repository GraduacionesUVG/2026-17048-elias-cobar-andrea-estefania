import numpy as np
from PIL import Image
from scipy import ndimage
from collections import deque
files={'idle':'1791352554977_image.png','eat':'1791352588822_image.png','pet':'1791352595881_image.png','sleep':'1791352605982_image.png'}
ANCHOR_X, ANCHOR_Y = 168, 205   # pantalla: centro y pies del buho
TARGET_W = 100

def load(n):
    a=np.asarray(Image.open('/mnt/user-data/uploads/'+files[n]).convert('RGB')).astype(int)
    H,W,_=a.shape
    import minilib
    _,fg=minilib.load_alpha_clean('/mnt/user-data/uploads/'+files[n])
    lab,k=ndimage.label(fg)
    sizes=ndimage.sum(fg,lab,range(1,k+1))
    objs=ndimage.find_objects(lab)
    mains=sorted(np.argsort(sizes)[-3:], key=lambda i: objs[i].start if False else objs[i][1].start)
    frames=[]
    for mi in mains:
        frames.append({'main':mi,'mask':np.zeros((H,W),bool)})
    for i in range(k):
        if sizes[i]<20: continue
        cx=(objs[i][1].start+objs[i][1].stop)/2
        best=min(range(3),key=lambda f: 0 if objs[mains[f]][1].start<=cx<objs[mains[f]][1].stop else min(abs(cx-objs[mains[f]][1].start),abs(cx-objs[mains[f]][1].stop)))
        frames[best]['mask']|=(lab==i+1)
    for f in frames:
        m=(lab==f['main']+1)
        ys,xs=np.nonzero(m)
        y0,y1=ys.min(),ys.max()
        low=ys>=y1-(y1-y0)*0.3
        f['cx']=(xs[low].min()+xs[low].max()+1)/2
        f['by']=y1+1
        f['w']=xs.max()-xs.min()+1
        f['head']=max((xs[ys==y].max()-xs[ys==y].min()+1) for y in range(y0,y0+int((y1-y0)*0.4)) if (ys==y).any())
    return a,frames

data={n:load(n) for n in files}
# Escalar por hoja: normalizar con el ancho de la cabeza del frame 0
# (cada hoja dibuja al buho a distinto tamano).
scales={n:TARGET_W/data[n][1][0]['head'] for n in data}
print('scales',scales)

# crear paleta comun
allpx=[]
for n,(a,frames) in data.items():
    m=np.zeros(a.shape[:2],bool)
    for f in frames: m|=f['mask']
    allpx.append(a[m])
allpx=np.concatenate(allpx).astype(np.uint8)
palimg=Image.fromarray(allpx.reshape(1,-1,3)).quantize(48,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE)
pal=np.array(palimg.getpalette()[:48*3]).reshape(-1,3)

def toidx(a):
    return (a[...,0]>>4)*256+(a[...,1]>>4)*16+(a[...,2]>>4)

AX0,AY0,AX1,AY1=60,60,260,216   # region de trabajo en pantalla
result={}
for n,(a,frames) in data.items():
    idx=toidx(a)
    H,W=idx.shape
    s=scales[n]
    outs=[]
    for f in frames:
        lay=np.where(f['mask'],idx,-1)
        out=np.full((AY1-AY0,AX1-AX0),-1,int)
        for ty in range(AY0,AY1):
            sy0=f['by']+(ty-ANCHOR_Y)/s; sy1=sy0+1/s
            iy0,iy1=int(np.floor(sy0)),int(np.ceil(sy1))
            if iy1<=0 or iy0>=H: continue
            for tx in range(AX0,AX1):
                sx0=f['cx']+(tx-ANCHOR_X)/s; sx1=sx0+1/s
                ix0,ix1=int(np.floor(sx0)),int(np.ceil(sx1))
                if ix1<=0 or ix0>=W: continue
                votes={}; sums={}
                for yy in range(max(iy0,0),min(iy1,H)):
                    wy=min(yy+1,sy1)-max(yy,sy0)
                    for xx in range(max(ix0,0),min(ix1,W)):
                        wx=min(xx+1,sx1)-max(xx,sx0)
                        v=lay[yy,xx]; w=wx*wy; votes[v]=votes.get(v,0)+w
                        if v>=0:
                            sm=sums.setdefault(v,np.zeros(3)); sm+=a[yy,xx]*w
                if votes:
                    tot=sum(votes.values()); tr=votes.get(-1,0)
                    if tr<tot*0.5:
                        votes.pop(-1,None); b=max(votes,key=votes.get)
                        c=sums[b]/votes[b]; out[ty-AY0,tx-AX0]=(int(c[0])<<16)|(int(c[1])<<8)|int(c[2])
        outs.append(out)
    result[n]=outs
np.save('result_clean.npy',{'r':result,'pal':pal},allow_pickle=True)
for n,outs in result.items():
    u=np.zeros_like(outs[0],bool)
    for o in outs: u|=o>=0
    ys,xs=np.nonzero(u)
    print(n,'x',xs.min()+AX0,xs.max()+AX0,'y',ys.min()+AY0,ys.max()+AY0)
