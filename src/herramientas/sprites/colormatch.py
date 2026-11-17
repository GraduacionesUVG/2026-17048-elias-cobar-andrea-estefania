import numpy as np
def ref_colors(frames_rgba):
    px=np.concatenate([f[f[...,3]>0][:,:3] for f in frames_rgba]).astype(int)
    cols,counts=np.unique(px,axis=0,return_counts=True)
    return cols[counts>=3]          # colores realmente usados
def match(rgba, ref, maxdist=60):
    a=np.asarray(rgba).copy(); m=a[...,3]>0
    px=a[m][:,:3].astype(int)
    # calcular distancia ponderada (mas peso a verde y rojo)
    w=np.array([2,4,3])
    out=px.copy()
    for i in range(0,len(px),4000):
        chunk=px[i:i+4000]
        d=(((chunk[:,None,:]-ref[None])**2)*w).sum(-1)
        j=d.argmin(1); dist=np.sqrt(d[np.arange(len(chunk)),j]/3)
        warm=chunk[:,0]>=chunk[:,2]          # no modificar azules (botella, gota)
        ok=(dist<maxdist)&warm
        out[i:i+4000][ok]=ref[j[ok]]
    a[m,:3]=out
    return a

def warm_mask(px):
    return px[:,0]>=px[:,2]

def tone_gain(frames_src, frames_ref):
    """Calcular la ganancia por canal para igualar el naranja de src al de ref."""
    import colorsys
    def orange_mean(frames):
        px=np.concatenate([f[f[...,3]>0][:,:3] for f in frames]).astype(float)
        mx=px.max(1); mn=px.min(1); sat=(mx-mn)/np.maximum(mx,1)
        sel=(px[:,0]>px[:,1])&(px[:,1]>px[:,2])&(sat>0.6)&(mx>150)
        return px[sel].mean(0)
    return orange_mean(frames_ref)/orange_mean(frames_src)

def apply_gain(rgba, gain):
    a=np.asarray(rgba).copy(); m=a[...,3]>0
    px=a[m][:,:3].astype(float)
    w=warm_mask(px)
    px[w]=np.clip(px[w]*gain,0,255)
    a[m,:3]=px.astype(np.uint8)
    return a
