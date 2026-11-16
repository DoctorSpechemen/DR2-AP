import struct,sys,zlib,os
src,out=sys.argv[1],sys.argv[2]
d=open(src,"rb").read()
magic,_,size,count,idx,_=struct.unpack('<6I',d[:24])
bad=0
for i in range(count):
    no,h,cs,us,off,al,fl=struct.unpack('<7I',d[idx+i*28:idx+i*28+28])
    name=d[no:d.index(b'\0',no)].decode('latin1')
    raw=d[off:off+cs]
    if fl: raw=raw[4:]
    if fl:
        try: raw=zlib.decompress(raw)
        except Exception as e:
            try: raw=zlib.decompress(raw,-15)
            except Exception as e2: bad+=1; continue
    name=os.path.basename(name.replace('\\','/'))
    open(os.path.join(out,name),"wb").write(raw)
print(count,"entries",bad,"failed")
