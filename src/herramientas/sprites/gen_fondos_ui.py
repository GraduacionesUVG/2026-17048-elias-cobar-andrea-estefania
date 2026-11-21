import numpy as np, pickle
from PIL import Image
from scipy import ndimage
import minilib
exec(open('gen_mini.py').read().split("# ---------- teco_mini.h")[0])   # rgb565, arr, frames
def to565(c):
    return ((int(c[0])&0xF8)<<8)|((int(c[1])&0xFC)<<3)|(int(c[2])>>3)
# ---------- comprimir fondos con paleta
out=["#pragma once","#include <Arduino.h>","#include <pgmspace.h>","",
"// Fondos comprimidos: 1 byte por pixel + paleta de 256 colores.","#define FONDO_W 320","#define FONDO_H 240",""]
for name,png in [('fondoDia','fondoDia.png'),('fondoNoche','fondoNoche.png')]:
    im=Image.open(png).convert('RGB')
    q=im.quantize(256,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE)
    pal=np.array(q.getpalette()[:256*3]).reshape(-1,3); idx=np.asarray(q).reshape(-1)
    err=np.abs(np.asarray(im).reshape(-1,3).astype(int)-pal[idx]).mean(); print(name,'error medio',round(err,2))
    out.append(f"const uint16_t {name}Pal[256] PROGMEM = {{")
    for i in range(0,256,16): out.append("  "+", ".join(f"0x{to565(c):04X}" for c in pal[i:i+16])+",")
    out.append("};")
    out.append(f"const uint8_t {name}Px[FONDO_W * FONDO_H] PROGMEM = {{")
    for i in range(0,idx.size,32): out.append("  "+", ".join(str(x) for x in idx[i:i+32])+",")
    out.append("};\n")
# borde frontal del nido (dibujar encima de Teco cuando esta adentro)
d=np.asarray(Image.open('fondoDia.png').convert('RGB')).astype(int)
r,g,b=d[...,0],d[...,1],d[...,2]
nest=(r>150)&(g>90)&(g<190)&(b<90)&(r-b>90)
lab,k=ndimage.label(nest); sz=ndimage.sum(nest,lab,range(1,k+1)); nm=lab==(np.argmax(sz)+1)
nm=ndimage.binary_dilation(nm,iterations=2)
runs=[]
for y in range(186,214):
    row=nm[y]; x=0
    while x<320:
        if row[x]:
            x0=x
            while x<320 and row[x]: x+=1
            runs.append((y,x0,x))
        else: x+=1
out.append("// Borde frontal del nido: tramos (fila, x inicial, x final) que se")
out.append("// vuelven a pintar con el fondo cuando Teco esta sentado adentro.")
out.append(f"#define NEST_RUNS {len(runs)}")
out.append("const int16_t nestRuns[NEST_RUNS][3] PROGMEM = {")
for i in range(0,len(runs),6): out.append("  "+", ".join(f"{{{a},{b},{c}}}" for a,b,c in runs[i:i+6])+",")
out.append("};")
open('teco/fondos.h','w').write("\n".join(out)); print('runs',len(runs))
# ---------- generar UI de la pantalla principal
A,al=minilib.load_alpha_clean('/mnt/user-data/uploads/1791405561392_image.png')
def c(box,s):
    im=minilib.cut(A,al,box,s); return im.crop(im.getbbox())
icons=[c((98,276,209,396),0.2),c((565,276,696,397),0.2),c((328,276,460,382),0.2),c((800,281,939,390),0.2)]  # comer, dormir, jugar, libro
from despeckle import despeckle
bub=Image.open('globo.png').convert('RGBA')   # globo con contorno y colita
icons=[Image.fromarray(despeckle(i)) for i in icons]
v=pickle.load(open('vida.pkl','rb'))
bf=[Image.fromarray(despeckle(i.crop(i.getbbox()))) for i in v['bfly']]; bd=[Image.fromarray(despeckle(i.crop(i.getbbox()))) for i in v['bird']]
global total; total=0
u="""#pragma once
#include <Arduino.h>
#include <pgmspace.h>
#include "teco_mini.h"

// Iconos del menu (0 comer, 1 dormir, 2 jugar, 3 lecciones), globo de
// pensamiento y visitantes. Fondo transparente = MINI_TRANSPARENT.

"""
for k,im in enumerate(icons):
    u+=frames(f"uiIcon{k}_",[im],f"uiIcon{k}","Icono "+["comer","dormir","jugar","lecciones"][k])+"\n"
u+="const MiniSprite* const uiIcons[4] = { &uiIcon0[0], &uiIcon1[0], &uiIcon2[0], &uiIcon3[0] };\n\n"
u+=frames("uiBubble",[bub],"uiBubbleFrames","Globo de pensamiento")+"\n"
u+=frames("visButterfly",bf,"visButterflyFrames","Mariposa")+"\n"
u+=frames("visBird",bd,"visBirdFrames","Pajarito: 0 parado, 1 volando")+"\n"
open('teco/home_ui.h','w').write(u); print('ui KB',total/1024, [i.size for i in icons], bub.size, [i.size for i in bf],[i.size for i in bd])
