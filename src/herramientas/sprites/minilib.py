import numpy as np
from PIL import Image
from scipy import ndimage
from collections import deque

def load_alpha(path):
    A=np.asarray(Image.open(path).convert('RGB')).astype(int)
    white=(A.min(2)>=215)&((A.max(2)-A.min(2))<30)
    lab,_=ndimage.label(white)
    border=set(np.unique(np.concatenate([lab[0],lab[-1],lab[:,0],lab[:,-1]])))-{0}
    alpha=~np.isin(lab,list(border))
    return A,alpha

def resample(A,mask,x0,y0,w,h,s):
    """Muestrear una region: pixel (i,j) <- caja fuente desde (x0+j/s, y0+i/s), por voto de color."""
    H,W=mask.shape
    key=(A[...,0]>>4)*256+(A[...,1]>>4)*16+(A[...,2]>>4)
    key=np.where(mask,key,-1)
    out=np.zeros((h,w,4),np.uint8)
    for i in range(h):
        sy0=y0+i/s; sy1=sy0+1/s
        for j in range(w):
            sx0=x0+j/s; sx1=sx0+1/s
            votes={}; sums={}
            for yy in range(max(int(sy0),0),min(int(np.ceil(sy1)),H)):
                wy=min(yy+1,sy1)-max(yy,sy0)
                for xx in range(max(int(sx0),0),min(int(np.ceil(sx1)),W)):
                    wgt=wy*(min(xx+1,sx1)-max(xx,sx0)); v=key[yy,xx]
                    votes[v]=votes.get(v,0)+wgt
                    if v>=0: sums[v]=sums.get(v,0)+A[yy,xx]*wgt
            tot=sum(votes.values())
            if tot==0 or votes.get(-1,0)>=tot*0.5: continue
            votes.pop(-1,None); b=max(votes,key=votes.get)
            out[i,j,:3]=np.clip(sums[b]/votes[b],0,255); out[i,j,3]=255
    return Image.fromarray(out)

def cut(A,alpha,box,s,anchor=None):
    """Recortar la region box=(x0,y0,x1,y1) al contenido del sprite."""
    x0,y0,x1,y1=box
    m=np.zeros_like(alpha); m[y0:y1,x0:x1]=alpha[y0:y1,x0:x1]
    w=int(np.ceil((x1-x0)*s)); h=int(np.ceil((y1-y0)*s))
    im=resample(A,m,x0,y0,w,h,s)
    return im

def load_alpha_clean(path, pocket_dist=25, fringe_passes=2):
    """Cargar como load_alpha y ademas:
    - quitar huecos blancos cerca de la orilla (entre ala y cuerpo),
      sin tocar los brillos de los ojos;
    - quitar la franja de pixeles claros/grises pegados al fondo."""
    A,alpha=load_alpha(path)
    mn=A.min(2); mx=A.max(2)
    nearwhite=(mn>=200)&((mx-mn)<35)
    bg=~alpha
    dist=ndimage.distance_transform_edt(~bg)
    lab,k=ndimage.label(nearwhite & alpha)
    if k:
        mind=ndimage.minimum(dist,lab,range(1,k+1))
        for i,md in enumerate(mind):
            if md<=pocket_dist:
                alpha[lab==i+1]=False
    light=(mn>=150)&((mx-mn)<30)
    for _ in range(fringe_passes):
        bg=~alpha
        touch=ndimage.binary_dilation(bg)&alpha
        alpha[touch&light]=False
    return A,alpha
