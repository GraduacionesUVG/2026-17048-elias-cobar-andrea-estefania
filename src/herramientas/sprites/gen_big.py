import numpy as np, re, pickle
from PIL import Image
from scipy import ndimage

def fringe_clean(rgba, passes=1):
    a=rgba.copy(); al=a[...,3]>0
    rgb=a[...,:3].astype(int); mn=rgb.min(2); mx=rgb.max(2)
    light=(mn>=170)&((mx-mn)<30)
    for _ in range(passes):
        touch=ndimage.binary_dilation(~al)&al
        kill=touch&light; al[kill]=False
    a[...,3]=np.where(al,255,0); return a

frames={}   # nombre -> lista de (rgba uint8, ax, ay)
d=np.load('result_clean.npy',allow_pickle=True).item(); r=d['r']
AX0,AY0=60,60
for name,key in [('Idle','idle'),('Eat','eat'),('Pet','pet'),('Sleep','sleep')]:
    lst=[]
    for o in r[key]:
        h,w=o.shape; rgba=np.zeros((h,w,4),np.uint8); m=o>=0
        rgba[m,0]=(o[m]>>16)&255; rgba[m,1]=(o[m]>>8)&255; rgba[m,2]=o[m]&255; rgba[m,3]=255
        lst.append((rgba,168-AX0,205-AY0))
    frames[name]=lst
# beber: cargar el dibujo con el cuenco de agua
lst=[(np.asarray(im).copy(),cx,by) for im,cx,by in pickle.load(open('agua.pkl','rb'))]
frames['Drink']=lst
v=pickle.load(open('vida.pkl','rb'))
for name,key in [('Walk','walk'),('Wave','wave'),('Hop','hop'),('Yawn','yawn'),('Up','up')]:
    frames[name]=[(np.asarray(im).copy(),cx,yb+1) for im,cx,yb in v[key]]
# quitar puntos claros de las orillas
from despeckle import despeckle
frames={k:[(despeckle(f[0]),f[1],f[2]) for f in lst] for k,lst in frames.items()}
# igualar colores al idle (hojas con tonos distintos)
from colormatch import ref_colors, match
ref=ref_colors([f[0] for k in ('Idle','Eat','Pet','Sleep') for f in frames[k]])
from colormatch import tone_gain, apply_gain
refF=[f[0] for k in ('Idle','Eat','Pet','Sleep') for f in frames[k]]
gVida=tone_gain([f[0] for k in ('Walk','Wave','Hop','Yawn','Up') for f in frames[k]], refF)
gDrink=tone_gain([f[0] for f in frames['Drink']], refF)
print('ajuste vida',gVida.round(3),'ajuste agua',gDrink.round(3))
for k in ('Drink','Walk','Wave','Hop','Yawn','Up'):
    gk=gDrink if k=='Drink' else gVida
    frames[k]=[(match(apply_gain(f[0],gk),ref),f[1],f[2]) for f in frames[k]]
# recortar y anclar
crop={}
for name,lst in frames.items():
    out=[]
    for rgba,ax,ay in lst:
        al=rgba[...,3]>0; ys,xs=np.nonzero(al)
        y0,y1,x0,x1=ys.min(),ys.max()+1,xs.min(),xs.max()+1
        out.append((rgba[y0:y1,x0:x1],int(round(ax-x0)),int(round(ay-y0))))
    crop[name]=out
# crear paleta comun (255 colores + transparente en el indice 0)
allpx=np.concatenate([f[0][f[0][...,3]>0][:,:3] for lst in crop.values() for f in lst]).astype(np.uint8)
# separar paleta: calidos (naranja, cafe, crema) y frios (agua, gotas)
# para conservar los azules
warm=allpx[allpx[:,0].astype(int)>=allpx[:,2].astype(int)]
cool=allpx[allpx[:,0].astype(int)<allpx[:,2].astype(int)]
NC=40
qw=Image.fromarray(warm.reshape(1,-1,3)).quantize(255-NC,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE)
qc=Image.fromarray(cool.reshape(1,-1,3)).quantize(NC,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE)
pal=np.concatenate([np.array(qw.getpalette()[:(255-NC)*3]).reshape(-1,3),np.array(qc.getpalette()[:NC*3]).reshape(-1,3)])
print('paleta',len(pal),'frios',len(cool))
def to565(c): 
    v=((int(c[0])&0xF8)<<8)|((int(c[1])&0xFC)<<3)|(int(c[2])>>3)
    return 0xF81E if v==0xF81F else v
def index(rgba):
    rgb=rgba[...,:3].reshape(-1,3).astype(int)
    d=((rgb[:,None,:]-pal[None])**2).sum(-1); idx=d.argmin(1)+1
    idx[rgba[...,3].reshape(-1)==0]=0
    return idx
err=0; n=0
h=["#pragma once","#include <Arduino.h>","#include <pgmspace.h>","",
"// Teco grande comprimido: cada pixel es 1 byte (indice de la paleta).",
"// Indice 0 = transparente. Cada frame guarda su ancla (ax, ay): el punto",
"// entre los pies de Teco, para dibujarlo en cualquier lugar de la pantalla.","",
"struct BigFrame {","  const uint8_t* px;","  uint8_t w;","  uint8_t h;","  int16_t ax;","  int16_t ay;","};",""]
h.append("const uint16_t bigPalette[256] PROGMEM = {")
vals=[0xF81F]+[to565(c) for c in pal]
for i in range(0,256,16): h.append("  "+", ".join(f"0x{x:04X}" for x in vals[i:i+16])+",")
h.append("};\n")
total=512
for name,lst in crop.items():
    for i,(rgba,ax,ay) in enumerate(lst):
        idx=index(rgba); hh,ww=rgba.shape[:2]; total+=idx.size
        h.append(f"const uint8_t big{name}{i}[{ww*hh}] PROGMEM = {{")
        for k in range(0,idx.size,24): h.append("  "+", ".join(str(x) for x in idx[k:k+24])+",")
        h.append("};")
    items=", ".join(f"{{ big{name}{i}, {f[0].shape[1]}, {f[0].shape[0]}, {f[1]}, {f[2]} }}" for i,f in enumerate(lst))
    h.append(f"const BigFrame big{name}Frames[{len(lst)}] = {{ {items} }};\n")
open('teco/teco_grande.h','w').write("\n".join(h))
print('teco_grande KB',total/1024)
for name,lst in crop.items(): print(name,[(f[0].shape[1],f[0].shape[0],f[1],f[2]) for f in lst])
# generar vista previa decodificada
prev=Image.new('RGBA',(1200,130),(60,120,170,255)); x=0
for name in ['Idle','Eat','Sleep','Pet','Drink','Walk','Wave','Hop','Yawn','Up']:
    rgba,ax,ay=crop[name][0]; idx=index(rgba).reshape(rgba.shape[:2])
    im=np.zeros(rgba.shape,np.uint8); m=idx>0; im[m,:3]=pal[idx[m]-1]; im[m,3]=255
    prev.alpha_composite(Image.fromarray(im),(x,125-ay)); x+=rgba.shape[1]+4
    if x>1100: break
prev.resize((2400,260),Image.NEAREST).save('/tmp/big_prev.png')
