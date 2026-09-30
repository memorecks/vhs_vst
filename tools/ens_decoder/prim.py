import os
ENS_PATH = os.environ.get("VHS_ENS", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "resources", "VHS_1.12.ens"))
import re,struct,sys,collections
from ptok import tok
D=open(ENS_PATH,'rb').read()
class P:
    def __init__(s,off,body):
        s.off=off; s.body=body
        v=struct.unpack_from('<IxHxIIIII',body,0)
        s.type=v[2]; s.id=v[3]; s.nIn=v[5]; s.nOut=v[6]
        s.ins=[]; s.outs=[]; s.children=[]; s.parent=None
        s.nchild=None
        m=re.search(rb"\x5d\x03\x00\x00\x00(....)",body,re.S)
        if m and s.type in (4,9):
            s.nchild=struct.unpack('<I',m.group(1))[0]
        s.name=s.label()
    def label(s):
        b=s.body
        for m in re.finditer(rb'[\x01-\x40]\x00\x00\x00([\x20-\x7e]{1,64})',b[0x30:0x400]):
            t=m.group(1); n=b[0x30+m.start()]
            if len(t)>=n: return t[:n].decode()
        return ''
def outdests(body):
    w=struct.unpack_from('<5I',body)
    n=w[4]; return [struct.unpack_from('<II',body,20+8*i) for i in range(n)]
def build(start=0x99d000):
    T=tok(D,start)
    mods=[]; i=0
    # attach ports to preceding module
    for s,c,b in T:
        if c=='KSModul': mods.append(P(s,b))
        elif mods:
            (mods[-1].ins if c=='KInPort' else mods[-1].outs).append(b)
    return mods
def tree(mods):
    it=iter(mods)
    def take(parent,n):
        for _ in range(n):
            m=next(it); m.parent=parent; parent.children.append(m)
            if m.nchild: take(m,m.nchild)
    return it,take
if __name__=='__main__':
    mods=build()
    print(len(mods))
    c=collections.Counter(m.type for m in mods)
    print(sorted((hex(k),v) for k,v in c.items()))
    for m in mods[:12]: print(hex(m.off),hex(m.type),hex(m.id),m.name,m.nchild,m.nIn,m.nOut,len(m.ins),len(m.outs))
def fulltree():
    mods=build()
    top=P.__new__(P); top.children=[]; top.name='TOP'; top.type=-1; top.nchild=len(mods); top.parent=None
    it=iter(mods)
    def take(parent,n):
        for _ in range(n):
            try: m=next(it)
            except StopIteration: return False
            m.parent=parent; parent.children.append(m)
            if m.nchild: take(m,m.nchild)
        return True
    # top-level: consume until exhausted
    while True:
        try: m=next(it)
        except StopIteration: break
        m.parent=top; top.children.append(m)
        if m.nchild: take(m,m.nchild)
    return top,mods
def fulltree2(overrides={}):
    mods=build()
    for m in mods:
        m.children=[]; m.parent=None
        if m.id in overrides: m.nchild=overrides[m.id]
    top=P.__new__(P); top.children=[]; top.name='TOP'; top.type=-1; top.nchild=None; top.parent=None; top.id=-1
    it=iter(mods)
    def take(parent,n):
        for _ in range(n):
            try: m=next(it)
            except StopIteration: return
            m.parent=parent; parent.children.append(m)
            if m.nchild: take(m,m.nchild)
    while True:
        try: m=next(it)
        except StopIteration: break
        m.parent=top; top.children.append(m)
        if m.nchild: take(m,m.nchild)
    return top,mods
