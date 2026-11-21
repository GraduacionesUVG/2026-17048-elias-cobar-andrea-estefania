import pickle, numpy as np
from PIL import Image
spr=pickle.load(open('mini2.pkl','rb'))
T=0xF81F
def rgb565(im):
    a=np.asarray(im.convert('RGBA')).astype(int)
    out=[]
    for p in a.reshape(-1,4):
        if p[3]<128: out.append(T); continue
        c=((p[0]&0xF8)<<8)|((p[1]&0xFC)<<3)|(p[2]>>3)
        out.append(0xF81E if c==T else c)
    return out
def arr(name,im):
    v=rgb565(im); L=[f"const uint16_t {name}[{im.width} * {im.height}] PROGMEM = {{"]
    for i in range(0,len(v),16): L.append("  "+", ".join(f"0x{x:04X}" for x in v[i:i+16])+",")
    L.append("};\n"); return "\n".join(L), len(v)*2
total=0
def frames(prefix,lst,tablename,comment):
    global total
    s=[f"// {comment} ({lst[0].width}x{lst[0].height}, {len(lst)} frames)"]
    for i,im in enumerate(lst):
        a,b=arr(f"{prefix}{i}",im); s.append(a); total+=b
    items=", ".join(f"{{ {prefix}{i}, {im.width}, {im.height} }}" for i,im in enumerate(lst))
    s.append(f"const MiniSprite {tablename}[{len(lst)}] = {{ {items} }};\n")
    return "\n".join(s)

# ---------- teco_mini.h
h="""#pragma once
#include <Arduino.h>
#include <pgmspace.h>

// Teco pequeno para los minijuegos. Fondo transparente = MINI_TRANSPARENT.
// Todos los frames de un mismo grupo tienen el mismo tamano y estan
// alineados por los pies, asi que se dibujan en la misma posicion.

#define MINI_TRANSPARENT 0xF81F

#ifndef MINI_SPRITE_DEFINED
#define MINI_SPRITE_DEFINED
struct MiniSprite {
  const uint16_t* data;
  uint8_t w;
  uint8_t h;
};
#endif

"""
h+=frames("tecoMiniNormal",spr['normal'],"tecoMiniNormalFrames","Normal: 0 y 1 ojos abiertos, 2 ojos cerrados")+"\n"
h+=frames("tecoMiniBien",spr['bien'],"tecoMiniBienFrames","Bien: celebra (el frame 1 es un saltito)")+"\n"
h+=frames("tecoMiniUy",spr['uy'],"tecoMiniUyFrames","Uy: cuando el nino se equivoca")+"\n"
h+=frames("tecoMiniGranjero",spr['granjero'],"tecoMiniGranjeroFrames","Granjero: 0 ojos abiertos, 1 ojos cerrados")+"\n"
open('teco/teco_mini.h','w').write(h)
print('teco_mini KB',total/1024); total=0

# ---------- milpa.h
tile=spr['tierra'][0].crop((0,0,85,77))
m="""#pragma once
#include <Arduino.h>
#include <pgmspace.h>
#include "teco_mini.h"

// Sprites del juego LA MILPA. Fondo transparente = MINI_TRANSPARENT.
// Las plantas estan alineadas por la base (todas se dibujan con los pies
// en la misma linea del suelo).

"""
m+=frames("milpaPlanta",spr['planta'],"milpaPlantaFrames","Etapas: 0 semilla, 1 brote, 2 joven, 3 mediana, 4 alta, 5 con mazorca")+"\n"
m+=frames("milpaMarchita",spr['marchita'],"milpaMarchitaFrames","Planta marchita (sed)")+"\n"
m+=frames("milpaSurco",spr['surco'],"milpaSurcoFrames","Surco vacio")+"\n"
m+=frames("milpaMazorca",spr['mazorca'],"milpaMazorcaFrames","Mazorca")+"\n"
m+=frames("milpaGota",spr['gota'],"milpaGotaFrames","Gota: 0 tengo sed, 1 cayendo, 2 salpica")+"\n"
a,b=arr("milpaTierra",tile); total+=b
m+=f"""// Franja de tierra repetible: arriba pasto (con huecos transparentes),
// abajo tierra. Las filas TIERRA_DIRT_TOP..TIERRA_H-1 se repiten hacia
// abajo para llenar el resto de la pantalla.
#define TIERRA_W {tile.width}
#define TIERRA_H {tile.height}
#define TIERRA_DIRT_TOP 22

{a}"""
open('teco/milpa.h','w').write(m)
print('milpa KB',total/1024)
