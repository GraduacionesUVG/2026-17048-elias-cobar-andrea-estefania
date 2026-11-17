# Convertir MiniSprite (RGB565, 2 bytes por pixel) a indices de 1 byte
# con paleta propia (sin perdida si usa <= 255 colores).
import re, sys
from collections import Counter
T=0xF81F
def compress_header(path):
    t=open(path).read()
    arrays={m.group(1):(m.start(),m.end(),[int(x,16) for x in re.findall(r'0x([0-9A-Fa-f]{4})',m.group(2))])
            for m in re.finditer(r'const uint16_t (\w+)\[[^\]]*\] PROGMEM = \{(.*?)\};\n?',t,re.S)}
    used=set(re.findall(r'\{ (\w+), \d+, \d+ \}',t))
    out=t; before=after=0; lossy=0
    for name in sorted(used, key=lambda n:-arrays[n][0]):   # recorrer de atras hacia adelante
        s,e,vals=arrays[name]
        cols=[c for c,_ in Counter(v for v in vals if v!=T).most_common()]
        if len(cols)>255:
            lossy+=1
            keep=cols[:255]
            def near(c):
                r,g,b=c>>11,(c>>5)&63,c&31
                return min(keep,key=lambda k:(r-(k>>11))**2*4+(g-((k>>5)&63))**2+(b-(k&31))**2*4)
            remap={c:(c if c in keep else near(c)) for c in cols}; cols=keep
        else: remap={c:c for c in cols}
        idx={c:i+1 for i,c in enumerate(cols)}
        px=[0 if v==T else idx[remap[v]] for v in vals]
        pal=[T]+cols
        L=[f"const uint16_t {name}_pal[{len(pal)}] PROGMEM = {{"]
        for i in range(0,len(pal),16): L.append("  "+", ".join(f"0x{v:04X}" for v in pal[i:i+16])+",")
        L.append("};")
        L.append(f"const uint8_t {name}_px[{len(px)}] PROGMEM = {{")
        for i in range(0,len(px),32): L.append("  "+",".join(str(v) for v in px[i:i+32])+",")
        L.append("};\n")
        out=out[:s]+"\n".join(L)+out[e:]
        before+=len(vals)*2; after+=len(px)+len(pal)*2
    out=re.sub(r'\{ (\w+), (\d+), (\d+) \}',lambda m:f'{{ {m.group(1)}_px, {m.group(1)}_pal, {m.group(2)}, {m.group(3)} }}' if m.group(1) in used else m.group(0),out)
    open(path,'w').write(out)
    print(f"{path}: {before/1024:.0f} KB -> {after/1024:.0f} KB  (sprites con >255 colores: {lossy})")
for p in sys.argv[1:]: compress_header(p)
