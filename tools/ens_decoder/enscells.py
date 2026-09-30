import os
ENS_PATH = os.environ.get("VHS_ENS", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "resources", "VHS_1.12.ens"))
import pickle,struct
from rk import chunks,parse
from core import load,module
d=open(ENS_PATH,'rb').read()
H=b'#NI#CS#Document##NI#Reaktor#Core#TaggedFile#'
cells=[]
i=0
while True:
    i=d.find(H,i)
    if i<0: break
    j=d.find(b'atad',i,i+200)
    u,end=chunks(d,j)
    try: m=load(u)
    except Exception as e: m=None; print('ERR',hex(i),e)
    cells.append((i,end,m))
    i=end
pickle.dump([(a,b) for a,b,_ in cells],open('cellpos.pkl','wb'))
pickle.dump(cells,open('cells.pkl','wb'))
for a,b,m in cells:
    if m is None: print(hex(a),'None'); continue
    ins=[c.name for c in m.groups[0]] if m.groups else []
    outs=[c.name for c in m.groups[1]] if len(m.groups)>1 else []
    print(hex(a),b-a,repr(m.name),m.type,'in',ins,'out',outs)
