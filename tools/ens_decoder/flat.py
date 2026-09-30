import struct,sys,pickle,collections
from cshow import NAMES,cval
BLACK={'z^-1','z^-1 ndc','z^-1 fbk','z^-1 ffd','Latch','ILatch','x mul a','x div a','x + a','x - a','a - x','Ix + a','Ix mul a',
       'Router GT','Router GE','Router LT','Router LE','Router EQ','Router NE','Clip Max','Clip Min','Clip MinMax','Max','Min','sqrt','sqrt (>0)','tanh','P2F','F2P',
       'dB2AF','AF2dB','1/x','x^2','round','sign +/-','Dup Flt',"Dup Flt'",'Event[-1]','No Event','Round','Latch[-1]','x AND a','x OR a','x SHR a','Quantize','legacy R5 SR for AudioCell','SR,CR,pCR,DR init','Lin Smoother [A]','Lin Smoothers [A]','Lin Smoother [S]','Stop?','Gate','Start',' Freeze '}
def tport(t): return t.props.get((-2147483639,1))
def isnum(x):
    try: float(x); return True
    except: return False
class Flat:
    def __init__(s,black=BLACK,out=sys.stdout):
        s.n=0; s.lines=[]; s.black=black; s.out=out
    def new(s,expr,comment=''):
        s.n+=1; v=f'v{s.n}'; s.lines.append(f'{v} = {expr}'+(f'   # {comment}' if comment else '')); return v
    def run(s,m,inputs,path):
        """inputs: dict port->varname. returns dict outport->var"""
        ch=m.children()
        incoming=collections.defaultdict(list)
        qbw={}
        for src,dst in m.wires:
            if dst is None: continue
            if dst[0]=='QB': qbw[dst[1]]=src
            else: incoming[tuple(dst)].append(src)
        memo={}
        ins_by_port={tport(t):t for t in (m.groups[0] if m.groups else [])}
        def srcval(src):
            if src is None: return '?'
            if src[0]=='K': return repr(src[1])
            if src[0]=='QB':
                w=qbw.get(src[1]); return srcval(w) if w is not None else f'QB?{src[1]}'
            if src[0]=='BUS': return f'<{src[2]}>'
            if src[0]=='?': return '?'
            mi,port=src
            return outval(mi,port)
        def inval(mi,port):
            ss=incoming.get((mi,port),[])
            if not ss: return None
            vals=[srcval(x) for x in ss]
            return vals[0] if len(vals)==1 else 'MERGEWIRE('+','.join(vals)+')'
        def outval(mi,port):
            key=(mi,port)
            if key in memo:
                if memo[key] is None: return f'CYCLE#{mi}'
                return memo[key]
            memo[key]=None
            c=ch[mi]
            if c.type in (10000,10001,10002,10007,12002):
                v=inputs.get(tport(c),f'IN{tport(c)}:{c.name}')
            elif c.macro:
                name=c.name
                args={}
                for t in c.groups[0] if c.groups else []:
                    p=tport(t); a=inval(mi,p)
                    args[p]=a if a is not None else 'nc'
                if name in s.black:
                    outs=[tport(t) for t in (c.groups[1] if len(c.groups)>1 else [])]
                    argstr=', '.join(f'{(t.name or p)}={args[p]}' for t in c.groups[0] for p in [tport(t)])
                    base=s.new(f"{name}({argstr})", path+'/'+name)
                    res={o:(base if len(outs)==1 else f'{base}.{o}') for o in outs}
                else:
                    s.lines.append(f'# >>> macro {path}/{name}')
                    res=s.run(c,args,path+'/'+name)
                    s.lines.append(f'# <<< {path}/{name}')
                for o,v in res.items(): memo[(mi,o)]=v
                v=memo.get(key) or f'NOOUT{port}'
                memo[key]=v; return v
            else:
                t=c.type; nm=NAMES.get(t,str(t))
                def a(p):
                    r=inval(mi,p); return r if r is not None else '0'
                if t==1: v=repr(cval(c))
                elif t in (2,3,11):
                    ps=sorted(p for (x,p) in incoming if x==mi)
                    args=[inval(mi,p) for p in ps]
                    if t in (2,3) and args and all(isnum(x) for x in args):
                        import math
                        vals=[float(x) for x in args]; r=sum(vals) if t==2 else math.prod(vals)
                        memo[key]=repr(r); return repr(r)
                    op={2:' + ',3:' * '}.get(t)
                    v=s.new(op.join(args) if op else f'Merge({", ".join(args)})')
                elif t in (4,5) and isnum(a(1)) and isnum(a(2)) and not (t==5 and float(a(2))==0):
                    v=repr(float(a(1))-float(a(2)) if t==4 else float(a(1))/float(a(2)))
                elif t in (4,5): v=s.new(f'{a(1)} {"-" if t==4 else "/"} {a(2)}')
                elif t==13:
                    mode={0:'==',1:'!=',2:'<=',3:'<',4:'>=',5:'>'}[c.props.get((1,1),0)]
                    v=s.new(f'({a(1)} {mode} {a(2)})')
                elif t==10:
                    base=s.new(f'Router(ctl={a(2)}, x={a(1)})')
                    v=f'{base}.{"T" if port==1 else "F"}'
                elif t==17: v=s.new(f'-({a(1)})')
                elif t==18: v=s.new(f'abs({a(1)})')
                elif t in (48,49):
                    b=c.props.get((2,4)); base=struct.unpack('<d',b[1])[0] if b else None
                    v=s.new(f'{"log" if t==48 else "exp"}_b{base:.6g}({a(1)})')
                else:
                    ps=sorted(p for (x,p) in incoming if x==mi)
                    v=s.new(f'{nm}('+', '.join(f'{p}={inval(mi,p)}' for p in ps)+f')'+(f'.{port}' if port not in (0,1) else ''), f'{c.name} props={ {k:v for k,v in c.props.items() if k not in ((-2147483644,1),(-2147483646,1),(4,6))} }' if c.props else '')
            memo[key]=v; return v
        outs={}
        for t in (m.groups[1] if len(m.groups)>1 else []):
            mi=ch.index(t); outs[tport(t)]=inval(mi,0) or 'nc'
        # also evaluate everything for side effects (writes)
        for mi,c in enumerate(ch):
            if not c.macro and c.type in (23,46):  # writes
                outval(mi,0)
        return outs
def flatten_cell(m,black=BLACK):
    f=Flat(black)
    ins={}
    res=f.run(m,ins,m.name)
    return f.lines,res
if __name__=='__main__':
    cells=pickle.load(open('cells.pkl','rb'))
    idx=int(sys.argv[1])
    lines,res=flatten_cell(cells[idx][2])
    print('\n'.join(lines)); print('OUTPUTS',res)
