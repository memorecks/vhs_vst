import struct,sys
from rk import parse,unz
class M:
    def __init__(s): s.type=None; s.macro=False; s.pos=None; s.props={}; s.groups=[]; s.wires=[]; s.raw={}
    @property
    def name(s):
        return s.props.get((1,2)) or s.props.get((-2147483645,2)) or ''
    def children(s):
        return [m for g in s.groups for m in g]
def val(node):
    # node: list of (tag,v) inside 028b9cb0
    out=[]
    for tag,v in node:
        if isinstance(v,list):
            for t2,v2 in v:
                if isinstance(v2,list): out.append(('struct',tag,v2)); continue
                if t2==0x03a30d33:
                    n=struct.unpack_from('<I',v2)[0]; out.append(v2[5:5+n].decode('latin1'))
                elif t2==0x03a30ba9: out.append(struct.unpack('<i',v2)[0] if len(v2)==4 else v2)
                else: out.append((hex(t2),v2))
        else: out.append((hex(tag),v))
    return out[0] if len(out)==1 else out
def props(node):
    d={}
    for tag,v in node:
        if tag==0x028f3cb0:
            for t,e in v:
                key=None; value=None
                for t2,v2 in e:
                    if t2==0x03a39cb0: key=struct.unpack('<ii',v2) if len(v2)==8 else v2
                    elif t2==0x028b9cb0: value=val(v2)
                d[key]=value
    return d
def module(nodes):
    m=M()
    for tag,v in nodes:
        if tag==0x03a2c92d: m.type=struct.unpack_from('<I',v)[0]; m.macro=bool(v[4])
        elif tag==0x02c2592d: m.pos=struct.unpack('<ii',v[0][1])
        elif tag==0x028f2d33: body(m,v)
        elif tag==0x02cf0cb0: m.props.update(props(v))
        else: m.raw[hex(tag)]=v
    return m
def body(m,nodes):
    for tag,v in nodes:
        if tag==0x02cf0cb0: m.props.update(props(v))
        elif tag==0x02ce4bed:
            g=[]
            for t,vv in v:
                if t==0x028f392d:
                    g=[module(x) for tt,x in vv if tt==0x02b24bed]
            m.groups.append(g)
        elif tag==0x02ceebe3:
            for t,vv in v:
                if t==0x028f3be3:
                    for tt,w in vv:
                        m.wires.append(parsewire(w))
        else: m.raw[hex(tag)]=v
def parsewire(w):
    import re
    o=0; src=None; dst=None
    while o+8<=len(w):
        a,l=struct.unpack_from('<II',w,o); dat=w[o+8:o+8+l]; o+=8+l
        if a==0x05ceebe3:
            k=struct.unpack_from('<I',dat)[0]
            if k==0: src=struct.unpack_from('<ii',dat,4)
            elif k==3: src=('K',struct.unpack_from('<d',dat,16)[0] if l>=24 else struct.unpack_from('<i',dat,12)[0])
            elif k==1 and l==12: src=('QB',struct.unpack_from('<i',dat,8)[0])
            elif k==1:
                # named path: [1, mod, 0-byte, (len,flag,str)...]
                names=[]; q=9
                mod=struct.unpack_from('<i',dat,4)[0]
                while q+5<=len(dat):
                    n=struct.unpack_from('<I',dat,q)[0]
                    if n>64: break
                    names.append(dat[q+5:q+5+n].decode('latin1')); q+=5+n
                    q+=8  # skip 2 u32 (index info)
                src=('BUS',mod,'.'.join(names))
            else: src=('?',dat.hex())
        elif a==0x0392ebe3:
            k=struct.unpack_from('<I',dat)[0]
            dst=struct.unpack_from('<ii',dat,4) if k==0 else ('QB',struct.unpack_from('<i',dat,8)[0])
    return (src,dst)
def load(u):
    tree=parse(u)
    # find first 02b24bed recursively
    def find(n):
        for t,v in n:
            if t==0x02b24bed: return module(v)
            if t==0x02d33ba9:
                m=M(); m.type='CELL'
                for t2,v2 in v:
                    if t2==0x028f2d33: body(m,v2)
                return m
            if isinstance(v,list):
                r=find(v)
                if r: return r
    return find(tree)
def show(m,ind=0,maxd=99,f=sys.stdout):
    p=' '*ind
    extra={k:v for k,v in m.props.items() if k not in ((1,2),(-2147483638,2),(4,6),(-2147483644,1),(-2147483646,1),(-2147483645,2))}
    print(f"{p}[{m.type}{'M' if m.macro else ''}] {m.name!r} {extra if extra else ''}",file=f)
    if ind//2>=maxd: return
    ch=m.children()
    for i,c in enumerate(ch):
        print(f"{p} #{i}",end='',file=f); show(c,ind+2,maxd,f)
    for w in m.wires: print(f"{p}  w {w[0]}.{w[1]} -> {w[2]}.{w[3]}",file=f)
if __name__=='__main__':
    b=open(sys.argv[1],'rb').read()
    show(load(unz(b)))
