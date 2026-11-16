"""Rebuild a DR2 .big, replacing named entries. Unchanged entries keep their original compressed bytes."""
import struct,sys,zlib
src,dst=sys.argv[1],sys.argv[2]
repl=dict(a.split('=',1) for a in sys.argv[3:])  # name=path
d=open(src,"rb").read()
magic,unk,size,count,idx,names=struct.unpack('<6I',d[:24])
ents=[list(struct.unpack('<7I',d[idx+i*28:idx+i*28+28])) for i in range(count)]
first=min(e[4] for e in ents)
head=bytearray(d[:first]); body=bytearray(); used=set()
for i,e in enumerate(ents):
    no,h,cs,us,off,al,fl=e
    name=d[no:d.index(b'\0',no)].decode('latin1')
    blob=d[off:off+cs]
    if name in repl:
        raw=open(repl[name],"rb").read(); used.add(name)
        if fl:
            c=zlib.compressobj(9,zlib.DEFLATED,12); blob=struct.pack('>I',len(raw))+c.compress(raw)+c.flush()
        else: blob=raw
        us=len(raw); cs=len(blob)
    pad=(-(first+len(body)))%al if al else 0
    body+=b'\0'*pad; off=first+len(body); body+=blob
    struct.pack_into('<7I',head,idx+i*28,no,h,cs,us,off,al,fl)
assert used==set(repl),set(repl)-used
struct.pack_into('<I',head,8,len(head)+len(body))
open(dst,"wb").write(head+body); print("wrote",dst,len(head)+len(body),"orig",len(d))
