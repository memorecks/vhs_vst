from prim import *
import struct,sys
def portinfo(b):
    w=struct.unpack_from('<3I',b)
    return w
def dests(b):
    # KOutPort: 1,kind,x,flag, then (count, pairs) - find pattern after 'flag' field where flag in (4,6,0x84,0x86..)
    w=struct.unpack_from('<4I',b)
    off=16
    if w[3] & 0xff == 4:  # named port: len + name + ... (macro/core port)
        n=struct.unpack_from('<I',b,off)[0]; off+=4+n
        # skip optional info
        # then flag byte?  try to find count
        rest=b[off:]
        # heuristic: find u32 count followed by pairs ending with 0x5d
        for k in range(0,12):
            try:
                c=struct.unpack_from('<I',rest,k)[0]
                if 0<=c<40 and rest[k+4+8*c:k+4+8*c+1]==b']':
                    return [struct.unpack_from('<II',rest,k+4+8*i) for i in range(c)]
            except struct.error: pass
        return None
    c=struct.unpack_from('<I',b,off)[0]
    return [struct.unpack_from('<II',b,off+4+8*i) for i in range(c)]
def dump(m,f=sys.stdout,rec=False,ind=''):
    print(f'{ind}MACRO {m.name!r} id={m.id:#x} nch={len(m.children)}',file=f)
    for i,c in enumerate(m.children):
        ins=[portinfo(b)[1:3] for b in c.ins]
        extra=''
        if c.type==0x64: extra='CONST=%g'%struct.unpack_from('<f',c.body,0x38)[0]
        if c.type==0x14:
            from knobs import knobinfo
            mn,mx,st,v,v2,lab,inf=knobinfo(c); extra=f'KNOB {lab!r} [{mn:g},{mx:g}] val={mn+v*(mx-mn):.4g}'
        nm=c.name if c.type in (4,5,6,0x20c) else ''
        print(f'{ind} {i:2d} [{c.type:#x}] {nm!r} id={c.id:#x} in={c.nIn} out={c.nOut} inports={[x[1] if x[1]<100000 else -1 for x in ins]} {extra}',file=f)
        for pi,o in enumerate(c.outs):
            try: ds=dests(o)
            except Exception as e: ds='?'
            if ds:
                print(f'{ind}      out{pi} -> '+', '.join(f'{a}.{b}' for a,b in ds)+'  '+' '.join(f'({m.children[a].name or hex(m.children[a].type)})' if isinstance(a,int) and a<len(m.children) else '(EXT?)' for a,b in ds),file=f)
    if rec:
        for c in m.children:
            if c.children: dump(c,f,True,ind+'    ')
if __name__=='__main__':
    top,mods=fulltree()
    for a in sys.argv[1:]:
        m=[x for x in mods if x.id==int(a,16)][0]
        dump(m,rec=True)
