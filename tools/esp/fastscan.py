"""Fast targeted scan: only descends into top-level GRUPs for the requested record types."""
import struct, zlib
def subs(data):
    out=[];o=0;big=None
    while o<len(data):
        t=data[o:o+4].decode('latin1');sz=struct.unpack_from('<H',data,o+4)[0];o+=6
        if t=='XXXX': big=struct.unpack_from('<I',data,o)[0];o+=sz;continue
        if big is not None: sz=big;big=None
        out.append((t,data[o:o+sz]));o+=sz
    return out
_cache={}
def load(path):
    if path not in _cache: _cache[path]=open(path,'rb').read()
    return _cache[path]
def scan(path,types):
    d=load(path)
    hsz=struct.unpack_from('<I',d,4)[0]; o=24+hsz; res={}
    while o<len(d):
        gsz=struct.unpack_from('<I',d,o+4)[0]; lab=d[o+8:o+12].decode('latin1')
        if lab in types:
            q=o+24
            while q<o+gsz:
                if d[q:q+4]==b'GRUP': q+=struct.unpack_from('<I',d,q+4)[0]; continue
                t=d[q:q+4].decode('latin1'); sz,fl,fid=struct.unpack_from('<III',d,q+4)
                raw=d[q+24:q+24+sz]
                if fl&0x40000: raw=zlib.decompress(raw[4:])
                res.setdefault(t,[]).append((fid,fl,subs(raw))); q+=24+sz
        o+=gsz
    return res
def edid(s):
    for t,v in s:
        if t=='EDID': return v.rstrip(b'\0').decode('latin1')
    return ''
def get(s,t):
    for k,v in s:
        if k==t: return v
    return None
