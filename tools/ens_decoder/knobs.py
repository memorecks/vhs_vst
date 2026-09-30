from prim import *
import struct,re
def lastlabel(b):
    lab=None;info=''
    for mm in re.finditer(rb'[\x01\x04\x05].\x00([\x01-\x40])\x00\x00\x00',b,re.S):
        n=mm.group(1)[0]; s=b[mm.end():mm.end()+n]
        if len(s)==n and all(0x20<=c<0x7f for c in s):
            rest=b[mm.end()+n:]
            if len(rest)>=4:
                n2=struct.unpack_from('<I',rest)[0]
                inf=rest[4:4+n2] if n2<600 else b''
                if n2<600 and all(0x09<=c<0x7f or c>=0xa0 for c in inf):
                    lab=s.decode(); info=inf.decode('latin1')
    return lab,info
def knobinfo(m):
    b=m.body
    step=struct.unpack_from('<d',b,0x3c)[0]
    mn,mx=struct.unpack_from('<ff',b,0x50)
    val=struct.unpack_from('<f',b,0x106)[0]
    v2=struct.unpack_from('<f',b,0x10a)[0]
    lab,info=lastlabel(b[-4000:])
    return mn,mx,step,val,v2,lab,info
if __name__=='__main__':
    top,mods=fulltree()
    for m in mods:
        if m.type in (0x14,):
            mn,mx,st,v,v2,lab,info=knobinfo(m)
            print(f'id={m.id:#x} {m.parent.name[:18]!r:20} {lab!r:12} [{mn:g},{mx:g}] step={st:g} val={mn+v*(mx-mn):.5g} ({v:.4f}) v2={v2:.3f} {info[:70]!r}')
