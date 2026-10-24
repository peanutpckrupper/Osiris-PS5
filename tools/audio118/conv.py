"""v118 (Halo 5 Guardians, Durango) -> v112 (Halo 5 Forge PC) soundbank recipe compiler.
Rules, all checked against halo5onpc's 704 verified recipes:
  version    BKHD 118 -> 112
  property   AkPropBundle pID: Wwise 2016 ids -> Forge ids (MakeUpGain 6 -> 0x21, ids 7..0x21 shift down one)
  rtpc       RTPC ParamID: by name, v118 table -> the v113 table the Forge runtime uses
  source     AkMediaInformation gains uFileID (streamed: the sourceID; in memory: this bank with its DIDX offset; prefetched, or held by another bank: the sourceID and 0)
  length     every object size and the HIRC chunk size grow by the bytes inserted inside them"""
import sys,os,struct,logging
sys.path.insert(0,os.path.expanduser('~/work/wwiser'))
from wwiser.parser import wparser, wdefs
logging.disable(logging.CRITICAL)
def walk(n):
    yield n
    for c in (n.get_children() or []): yield from walk(c)
def prop(k):
    if k==6: return 0x21
    if 7<=k<=0x21: return k-1
    if k in (0,1,2,3,4,5) or 0x22<=k<=0x3b: return k
    raise ValueError('property %#x'%k)
_r113={v:k for k,v in wdefs.AkRTPC_ParameterID_113.enum.items()}
def rtpc(k):
    n=wdefs.AkRTPC_ParameterID_118.enum.get(k)
    if n is None or n not in _r113: raise ValueError('rtpc %#x %s'%(k,n))
    return _r113[n]
class Unsupported(Exception): pass
def compile_bank(path):
    raw=open(path,'rb').read()
    p=wparser.Parser(); p.parse_bank(path); bank=p.get_banks()[0]
    edits=[]; inserts=[]  # (offset,len)
    E=lambda o,b,a,r: edits.append((o,b,a,r))
    assert raw[0:4]==b'BKHD' and struct.unpack_from('<I',raw,8)[0]==118
    E(8,raw[8:12],struct.pack('<I',112),'version')
    # DIDX offsets
    didx={}; at=0; chunks={}
    while at<len(raw):
        nm=raw[at:at+4]; sz=struct.unpack_from('<I',raw,at+4)[0]; chunks[nm]=(at,sz); at+=8+sz
    if b'DIDX' in chunks:
        o,s=chunks[b'DIDX']
        for i in range(o+8,o+8+s,12): sid,off,ln=struct.unpack_from('<III',raw,i); didx[sid]=off
    objs=[]
    for n in walk(bank):
        a=n.get_attrs(); nm=n.get_name(); off=a.get('offset')
        if a.get('type')=='skip': raise Unsupported('parser skipped bytes at %#x'%off)
        if nm=='dwSectionSize': objs.append((off,a['value']))
        elif nm=='pID':
            par=n.get_parent().get_name()
            if par!='AkPropBundle': raise Unsupported('pID in '+par)
            if a.get('type') not in('u8',): raise Unsupported('pID type '+str(a.get('type')))
            E(off,raw[off:off+1],bytes([prop(raw[off])]),'property')
        elif nm in('ParamID','PropertyId','TargetProp'):
            par=n.get_parent().get_name()
            if not(nm=='ParamID' and par=='RTPC' and a.get('type') in('u8','var') and raw[off]<0x80): raise Unsupported('%s in %s (%s)'%(nm,par,a.get('type')))
            E(off,raw[off:off+1],bytes([rtpc(raw[off])]),'rtpc')
        elif nm=='AkMediaInformation':
            ch={c.get_name():c for c in n.get_children()}
            st=n.get_parent().find1(name='StreamType').value()
            sid=ch['sourceID'].value(); so=ch['sourceID'].get_attrs()['offset']+4
            if st==2: ins=struct.pack('<I',sid)
            else:
                                ins=struct.pack('<II',struct.unpack_from('<I',raw,12)[0],didx[sid]) if st==0 and sid in didx else struct.pack('<II',sid,0)
            E(so,b'',ins,'source-location'); inserts.append((so,len(ins)))
            sb=ch['uSourceBits']; o=sb.get_attrs()['offset']; v=raw[o]
            if v & ~1: raise Unsupported('source bits %#x'%v)
            E(o,raw[o:o+1],raw[o:o+1],'source-flags')
    # lengths: objects then HIRC chunk
    def grow(lo,hi): return sum(l for o,l in inserts if lo<=o<hi)
    for off,size in objs:
        g=grow(off+4,off+4+size)
        if g: E(off,raw[off:off+4],struct.pack('<I',size+g),'length')
    if b'HIRC' in chunks:
        o,s=chunks[b'HIRC']; g=grow(o+8,o+8+s)
        if g: E(o+4,raw[o+4:o+8],struct.pack('<I',s+g),'length')
    edits.sort(key=lambda e:(e[0],len(e[1])!=0))
    out=bytearray(); cur=0
    for o,b,a,r in edits:
        assert o>=cur and raw[o:o+len(b)]==b,(o,cur)
        out+=raw[cur:o]+a; cur=o+len(b)
    out+=raw[cur:]
    return raw,bytes(out),edits
if __name__=='__main__':
    import json,hashlib,glob
    R=json.load(open(os.path.expanduser('~/mnt/Halo5-Guardians-Dev/halo5onpc/src/h5sololauncher.Core/Conversion/audio-profiles.json')))['Recipes']
    ok=bad=0
    for r in R:
        fn=os.path.expanduser('~/work/conv/src/R_%d.bnk'%r['Id'])
        try:
            raw,out,ed=compile_bank(fn)
            if hashlib.sha256(out).hexdigest().upper()==r['OutputSha256']: ok+=1
            else:
                bad+=1
                if bad<=5:
                    mine={(o,b.hex(),a.hex()) for o,b,a,_ in ed}; ref={(e['Offset'],e['Before'],e['After']) for e in r['Edits']}
                    print('MISMATCH',r['Id'],'mine-only',sorted(mine-ref)[:5],'ref-only',sorted(ref-mine)[:5])
        except Exception as x:
            bad+=1; print('FAIL',r['Id'],repr(x)[:200])
    print('ok',ok,'bad',bad)
