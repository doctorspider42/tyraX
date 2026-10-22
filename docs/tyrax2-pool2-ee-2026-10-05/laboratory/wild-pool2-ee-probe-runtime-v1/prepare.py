from pathlib import Path
import hashlib,json,py_compile
ROOT=Path(__file__).resolve().parent;LAB=ROOT.parent
OLD=LAB/'wild-pool2-static-probe-root-v1';VERIFY=LAB/'wild-pool2-static-probe-design-v2'
pins={}
def read(p):
 b=p.read_bytes();pins[str(p)]={'sha256':hashlib.sha256(b).hexdigest(),'bytes':len(b)};return b.decode().replace('\r\n','\n')
def write(name,s): (ROOT/name).write_text(s)
def change(s,a,b):
 assert s.count(a)==1,a
 return s.replace(a,b)
for name in ['savestate-reader-pinned.py','decode-vu1-output.py','verify-capture.py']:write(name,read(VERIFY/name))
s=(ROOT/'decode-vu1-output.py').read_text()
s=change(s,'def decode(data,case,arm):','def decode(data,case,arm,repeats=1):')
s=change(s," need(set(banks)==expected,'final banks match case');return banks",''' need(set(banks)==expected,'final banks match case')
 need(repeats in(1,2,3),'explicit final color epoch')
 epoch=repeats-1
 for n,bank in banks.items():
  source=(0 if n==75 else 75) if case=='A96' else (75 if n==75 else 150)
  for i in range(n):
   member=(source+i)//96
   want=(31+epoch,63,95,128) if member==0 else (95-epoch,15,1,128)
   rgba=struct.unpack_from('<4I',bank['outputBytes'],(3*i+1)*16)
   need(rgba==want,'actual final VU epoch RGBA source='+str(source+i)+' epoch='+str(repeats))
  bank['epoch']=repeats
 return banks''')
s=change(s,'def compare(a,b,case):\n x,y=decode(a,case,0),decode(b,case,1);result=[]','def compare(a,b,case,repeats=1):\n x,y=decode(a,case,0,repeats),decode(b,case,1,repeats);result=[]')
s=change(s,"p.add_argument('--savestates',action='store_true');","p.add_argument('--savestates',action='store_true');p.add_argument('--repeats',type=int,choices=(1,2,3),required=True);")
s=change(s,'report=compare(x,y,a.case);','report=compare(x,y,a.case,a.repeats);report[\'finalEpochRGBAValidated\']=True;report[\'repeats\']=a.repeats;')
write('decode-vu1-output.py',s)
s=(ROOT/'verify-capture.py').read_text()
s=change(s,"bank=d.decode(vu,'A96'if n==96 else 'B192',arm)","bank=d.decode(vu,'A96'if n==96 else 'B192',arm,repeats)")
s=change(s,"'Warm printed source counters retain cold values; only actual final VU marker/layout is checked here.'","'Final requested color epoch is independently validated in every saved VU output RGBA; intermediate epochs require their own captures.'")
s=change(s,"actualVuCountAndArmLayoutValidated=True,","actualVuCountAndArmLayoutValidated=True,finalEpochRGBAValidated=True,finalEpoch=repeats,")
s=change(s,"manifest=json.loads(q.source_manifest.read_text(encoding='utf-8-sig'));",'''native=json.loads(q.native_proof.read_text(encoding='utf-8-sig'))
 need(native['status'].startswith('PASS_')and not native.get('blockers'),'accepted independent native review')
 need(native['actualELFSha256']==sha(q.elf)and native['actualSymbolSha256']==sha(q.sym),'native reviewed exact ELF/symbol')
 need(native['sourceManifestSha256']==sha(q.source_manifest)and native['diagnosticOnly'],'native reviewed diagnostic source')
 manifest=json.loads(q.source_manifest.read_text(encoding='utf-8-sig'));''')
write('verify-capture.py',s)
s=read(OLD/'launch-probe-v3.py')
s=change(s,"p.add_argument('--stem',required=True)","p.add_argument('--stem',required=True)\np.add_argument('--native-proof',type=Path,required=True)")
s=change(s,"re.fullmatch('pool2-static-[a-z0-9-]+',a.stem)","re.fullmatch('pool2-ee-probe-[a-z0-9-]+',a.stem)")
s=change(s,"fixture=lab/'wild-pool2-static-probe-physical-v1'","fixture=lab/'wild-pool2-ee-probe-physical-v1'")
s=change(s,"native=lab/'wild-pool2-static-probe-native-review-v1/proof.json'","native=a.native_proof")
s=change(s,"nativeProof=str(native),cfgSha256=sha(cfg),preload=b,profileBeforeSha256=sha(ini),","nativeProof=str(native),runtimeAssetsManifestSha256=sha(fixture/'runtime-assets-manifest.json'),cfgSha256=sha(cfg),preload=b,profileBeforeSha256=sha(ini),")
write('launch-probe.py',s)
s=read(OLD/'finish-probe-v2.py')
start=s.index('config=re.findall(');end=s.index("crc=d['preload']",start)
s=s[:start]+'''from protocol import verify
protocol=verify(text,d['arm'],d['vertices'],d['repeats'],clip=0)
assert v['sourceManifestSha256']==d['sourceManifestSha256'] and v['nativeProofSha256']==d['nativeProofSha256']
assert v['files'][str(archive/'pool2-probe.cfg')]==d['cfgSha256']
assert v['finalEpochRGBAValidated'] and v['finalEpoch']==d['repeats']
assert sha(Path(d['fixture'])/'runtime-assets-manifest.json')==d['runtimeAssetsManifestSha256']
'''+s[end:]
s=change(s,'warmFreshnessIndependentEpoch=False,','warmFreshnessIndependentEpoch=True,finalColorEpoch=d[\'repeats\'],protocol=protocol,runtimeAssetsManifestSha256=d[\'runtimeAssetsManifestSha256\'],')
s=change(s,"'Fixed static final bank output lacks an iteration epoch; warm source calls/protocol are bound, not independently timestamped VU execution.'","'Final requested color epoch is proven in saved VU output; earlier iterations are protocol-bound without individual snapshots.'")
s=change(s,"'Printed warm counters retain cold values. Snapshot drains are correctness observer operations; no target timing result.'","'All counters and cold expansion oracle disabled. Snapshot drains are correctness observer operations; no target timing result.'")
write('finish-probe.py',s)
s=read(OLD/'capture-ready.py')
s=change(s,"work=lab/'wild-pool2-static-probe-design-v2'","work=root")
s=change(s,"root/'finish-probe-v2.py'","root/'finish-probe.py'")
write('capture-ready.py',s)
s=read(OLD/'run-case-v2.py')
s=change(s,"p.add_argument('--slot',type=int,required=True)","p.add_argument('--slot',type=int,required=True)\np.add_argument('--native-proof',type=Path,required=True)\np.add_argument('--tag',default='20261005-v1')")
s=change(s,'a=p.parse_args();assert 162<=a.slot<=171',"a=p.parse_args();assert 172<=a.slot<=183;assert re.fullmatch('[a-z0-9-]+',a.tag)")
s=change(s,"work=lab/'wild-pool2-static-probe-design-v2'","work=root")
s=change(s,"stem=f'pool2-static-{\"a\" if a.vertices==96 else \"b\"}{a.vertices}-arm{a.arm}-repeat{a.repeats}-20261005'","stem=f'pool2-ee-probe-{\"a\" if a.vertices==96 else \"b\"}{a.vertices}-arm{a.arm}-repeat{a.repeats}-{a.tag}'")
s=change(s,"root/'launch-probe-v3.py'","root/'launch-probe.py'")
s=change(s,"'--repeats',str(a.repeats),'--stem',stem]","'--repeats',str(a.repeats),'--stem',stem,'--native-proof',str(a.native_proof)]")
s=change(s,"root/'finish-probe-v2.py'","root/'finish-probe.py'")
write('run-case.py',s)
for f in ROOT.glob('*.py'):py_compile.compile(str(f),doraise=True)
read(LAB/'wild-pool2-ee-probe-physical-v1/game/inc/pool2_static_probe.hpp')
read(LAB/'wild-pool2-ee-probe-physical-v1/tyra/engine/src/renderer/3d/pipeline/shared/vcl_sml.i')
for p in pins:assert hashlib.sha256(Path(p).read_bytes()).hexdigest()==pins[p]['sha256']
outputs={p.name:hashlib.sha256(p.read_bytes()).hexdigest()for p in ROOT.iterdir()if p.is_file()and p.name!='preparation-proof.json'}
(ROOT/'preparation-proof.json').write_text(json.dumps({'status':'PREPARED_SYNTAX_CHECKED_NO_RUNTIME_DEVICE_ACTIVITY','inputs':pins,'outputs':outputs,'limitations':['No native build or device execution.','Clip capture decoder not supplied: inside TC decoder rejects alternate clip topology/marker.','Independent native review must expose old accepted field schema.']},indent=2))
print('Prepared source-only runtime overlay',ROOT)
