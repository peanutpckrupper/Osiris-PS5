import os,glob,sys,json
sys.path.insert(0,os.path.expanduser('~/work/conv')); from pck import lut
M=os.path.expanduser('~/mnt')
have=set()
for d in ['SFX','English(US)']:
    for p in glob.glob(M+'/H5Forge/sound/win/'+d+'/*.pck'): have|={e[0] for e in lut(p)}
camp=glob.glob(M+'/Halo5-Guardians-Dev/h5cache/game/audio/*/campaign.pck')[0]
have|={e[0] for e in lut(camp)}
miss=[]
for lang,pk in [('SFX','soundbank'),('SFX','soundstream'),('English(US)','soundvoice')]:
    for e in lut(M+'/Halo5-Guardians-Vanilla/RootFolder/sound/Durango/%s/%s.pck'%(lang,pk)):
        if e[0] not in have: miss.append((pk,e[0],e[2],e[4]))
print(len(miss), sum(m[2] for m in miss)); json.dump(miss,open(os.path.expanduser('~/work/conv/missing.json'),'w'))
import collections; print(collections.Counter(m[0] for m in miss))
