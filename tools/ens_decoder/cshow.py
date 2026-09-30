import struct,sys,pickle
from core import M
NAMES={1:'Const',2:'Add',3:'Mul',4:'Sub',5:'Div',10:'Router',11:'Merge',13:'Compare',16:'CmpSign?',17:'Neg?',18:'Abs',
 22:'Read',23:'Write',24:'?24',25:'SHR',26:'?26',27:'AND',28:'OR',29:'XOR',30:'R/W Order',41:'?41',42:'ES Ctl',44:'Array',45:'Read[]',46:'Write[]',47:'?47',48:'Log',49:'Exp',50:'Table',
 10000:'IN',10001:'IN(OBC)',10002:'IN(bool)',10007:'IN(bus)',11000:'OUT',11001:'OUT(OBC)',11002:'OUT(bool)',11007:'OUT(bus)',12002:'CELLIN',13002:'CELLOUT',
 14000:'?14000',14003:'?14003',15000:'?15000',15003:'?15003',20000:'?20000',20003:'?20003',30003:'?30003',30004:'?30004'}
def cval(c):
    p=c.props
    if (1,4) in p:
        v=p[(1,4)]
        if isinstance(v,tuple): return struct.unpack('<d',v[1])[0]
    if (1,1) in p: return p[(1,1)]
    return None
def desc(c):
    n=NAMES.get(c.type,str(c.type))
    if c.macro: return f'M:{c.name!r}'
    s=n
    if c.type==1: s+=f'({cval(c)})'
    elif c.type in (13,): s+=f'[mode={c.props.get((1,1))}]'
    elif c.type in (48,49):
        v=c.props.get((2,4)); s+=f'[base={struct.unpack("<d",v[1])[0]:.6g}]' if v else ''
    elif c.type==44: s+=f'[size={c.props.get((1,1))}]'
    elif c.type==11: s+=f'[n={c.props.get((1,1))}]'
    if c.name: s+=f' {c.name!r}'
    return s
def show(m,ind=0,maxd=99,f=sys.stdout,skip=()):
    p=' '*ind
    print(f"{p}{desc(m)}",file=f)
    ch=m.children()
    for i,c in enumerate(ch):
        if c.macro and ind//2<maxd and c.name not in skip:
            print(f"{p} #{i} ",end='',file=f); show(c,ind+2,maxd,f,skip)
        else:
            print(f"{p} #{i} {desc(c)}",file=f)
    def pn(c,port,out):
        # map port index to terminal name for macros
        if c.macro:
            grp=c.groups[1] if out else c.groups[0]
            for t in grp:
                if t.props.get((-2147483639,1))==port: return t.name or str(port)
        return str(port)
    def ref(r,out):
        if r is None: return '?'
        if r[0]=='K': return f'K({r[1]:.10g})'
        if r[0]=='QB': return f'QB[{r[1]}]'
        if r[0]=='BUS': return f'BUS[{r[2]}]'
        if r[0]=='?': return '?'+r[1]
        a,b=r
        return f'#{a}.{pn(ch[a],b,out) if a<len(ch) else b}'
    for src,dst in m.wires:
        print(f"{p}  w {ref(src,True)} -> {ref(dst,False)}",file=f)
