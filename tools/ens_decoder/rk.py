import struct,zlib,sys
def chunks(b,i=0,end=None):
    """yield (offset, bytes) for consecutive atad chunks starting at i"""
    end=len(b) if end is None else end
    out=bytearray()
    while i<end and b[i:i+4]==b'atad':
        meth=b[i+8:i+16]; cl,ul=struct.unpack_from('<II',b,i+16)
        raw=b[i+24:i+24+cl]
        out+= zlib.decompress(raw) if meth==b'crngbilz' else raw
        i+=24+cl
    return bytes(out),i
def unz(b):
    i=b.find(b'atad')
    return chunks(b,i)[0]
def parse(u,off=0,end=None):
    end=len(u) if end is None else end
    nodes=[]
    while off+8<=end:
        tag,ln=struct.unpack_from('<II',u,off)
        if off+8+ln>end: raise ValueError('overrun')
        data=u[off+8:off+8+ln]
        kind=tag>>24
        if kind==2:
            nodes.append((tag,parse(u,off+8,off+8+ln)))
        else:
            nodes.append((tag,data))
        off+=8+ln
    return nodes
def leafstr(d):
    if len(d)>=5:
        n=struct.unpack_from('<I',d)[0]
        if len(d)==5+n and d[4]==1:
            try: return repr(d[5:].decode('latin1'))
            except: pass
        if len(d)==4+2*n:
            try: return 'u16:'+repr(d[4:].decode('utf-16le'))
            except: pass
    if len(d)==4:
        i=struct.unpack('<i',d)[0]; f=struct.unpack('<f',d)[0]
        return f'i={i} f={f:g}'
    if len(d)==8:
        return 'ii=%s d=%g'%(struct.unpack('<ii',d),struct.unpack('<d',d)[0])
    return d.hex(' ')
def dump(nodes,ind=0,out=sys.stdout,maxd=99):
    for tag,v in nodes:
        if isinstance(v,list):
            print('  '*ind+f'{tag:08x} {{',file=out)
            if ind<maxd: dump(v,ind+1,out,maxd)
            print('  '*ind+'}',file=out)
        else:
            print('  '*ind+f'{tag:08x} = {leafstr(v)}',file=out)
if __name__=='__main__':
    b=open(sys.argv[1],'rb').read()
    dump(parse(unz(b)))
