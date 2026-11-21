import numpy as np
from scipy import ndimage
def despeckle(rgba, passes=2, diff=45, minlum=120):
    a=np.asarray(rgba).copy()
    for _ in range(passes):
        al=a[...,3]>0
        lum=(a[...,0].astype(int)*30+a[...,1].astype(int)*59+a[...,2].astype(int)*11)//100
        edge=ndimage.binary_dilation(~al)&al
        k=np.ones((3,3)); k[1,1]=0
        nsum=ndimage.convolve(np.where(al,lum,0),k,mode='constant')
        ncnt=ndimage.convolve(al.astype(int),k,mode='constant')
        mean=np.where(ncnt>0,nsum/np.maximum(ncnt,1),255)
        kill=edge&(lum>=minlum)&(lum>mean+diff)&(ncnt>=2)
        a[...,3]=np.where(kill,0,a[...,3])
    return a
