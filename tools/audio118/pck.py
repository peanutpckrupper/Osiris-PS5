import struct
def lut(p, which=0):
    with open(p,'rb') as f:
        h=f.read(28); L=struct.unpack_from('<I',h,4)[0]+8; f.seek(0); t=f.read(L)
    lm,bl=struct.unpack_from('<II',t,12); at=28+lm if which==0 else 28+lm+bl
    c=struct.unpack_from('<I',t,at)[0]
    return [struct.unpack_from('<5I',t,at+4+i*20) for i in range(c)]
