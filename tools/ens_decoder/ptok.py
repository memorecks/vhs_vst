import re,struct,sys
def tok(d,start=0,end=None):
    pat=re.compile(rb'\[(\x07|\x08)\x00\x00\x00(KSModul|KInPort|KOutPort)')
    ms=[m for m in pat.finditer(d,start,end or len(d))]
    out=[]
    for a,b in zip(ms,ms[1:]+[None]):
        s=a.start(); e=b.start() if b else min(len(d),s+600)
        out.append((s,a.group(2).decode(),d[s+5+len(a.group(2)):e]))
    return out
if __name__=='__main__':
    d=open(sys.argv[1],'rb').read()
    for s,c,b in tok(d):
        strs=[x.decode() for x in re.findall(rb'[\x20-\x7e]{3,}',b[:3000])][:4]
        if c=='KSModul':
            v=struct.unpack_from('<IxHxIIIII',b,0)
            print(f'{s:06x} MOD type=0x{v[2]:x} id=0x{v[3]:x} f={v[4]} nIn={v[5]} nOut={v[6]} {strs}')
        else:
            n=(len(b))//4
            print(f'{s:06x}   {c} {b[:120].hex(" ")} {strs}')
