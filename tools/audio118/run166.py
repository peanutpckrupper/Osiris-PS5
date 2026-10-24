import json,os,hashlib,collections
from conv import compile_bank
miss=json.load(open('missing.json')); ok=[]; fails=collections.Counter(); recipes=[]
for pk,bid,size,lang in miss:
    fn='src/X_%d.bnk'%bid
    try:
        raw,out,ed=compile_bank(fn); assert len(raw)==size
        recipes.append(dict(Id=bid,Package=pk,Lang=lang,InputSha256=hashlib.sha256(raw).hexdigest().upper(),OutputSha256=hashlib.sha256(out).hexdigest().upper(),InputBytes=len(raw),OutputBytes=len(out),
            Edits=[dict(Offset=o,Before=b.hex(),After=a.hex(),Reason=r) for o,b,a,r in ed]))
    except Exception as x: fails[repr(x)[:120]]+=1
print(len(recipes),'converted'); [print(v,k) for k,v in fails.most_common()]
json.dump(dict(Format=1,SourceVersion=118,NativeVersion=112,Rules='h5-118-to-112-structural-1',Recipes=recipes),open('extra-recipes.json','w'),separators=(',',':'))
