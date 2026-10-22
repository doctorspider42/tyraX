"""Independent read-only confirmation under root's held native cache."""
from pathlib import Path
import hashlib,json,struct
L=Path('/mnt/f/Projects/tyrax2-lab-20261001');F=L/'wild-pool-lattice-physical-v5';O=L/'wild-pool-lattice-native-independent-confirmation-v1';A=L/'wild-pool-lattice-native-root-review-v1/proof.json'
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def digest(b):return hashlib.sha256(b).hexdigest()
def wp(s):return Path('/mnt/'+s[0].lower()+s[2:].replace('\\','/')) if len(s)>2 and s[1]==':' else Path(s)
def elf(p):
 b=p.read_bytes();assert b[:6]==b'\x7fELF\x01\x01';off=struct.unpack_from('<I',b,32)[0];ent,n,idx=struct.unpack_from('<HHH',b,46);ss=[struct.unpack_from('<10I',b,off+i*ent) for i in range(n)];ns=b[ss[idx][4]:ss[idx][4]+ss[idx][5]]
 named={ns[s[0]:].split(b'\0',1)[0].decode():s for s in ss};syms={}
 for s in ss:
  if s[1]!=2:continue
  st=ss[s[6]];strings=b[st[4]:st[4]+st[5]]
  for i in range(0,s[5],s[9]):
   no,v,sz,info,other,si=struct.unpack_from('<IIIBBH',b,s[4]+i);name=strings[no:].split(b'\0',1)[0].decode();syms[name]=(v,si)
 def section(name):s=named[name];return b[s[4]:s[4]+s[5]]
 def image(name):
  v,si=syms[name];ev,esi=syms[name.replace('_CodeStart','_CodeEnd')];assert si==esi and ev>=v;s=ss[si];offset=s[4]+v-s[3];return b[offset:offset+ev-v],v,ev
 return section,image
assert not (O/'proof.json').exists()
assert sha(A)=='dc00e55415e41d07f49d848ebf9e85ffff6fc9b7a244eea3c6ee07d5f498199c'
a=read(A);p=read(F/'root-native-provenance.json');ex=read(F/'root-native-command-exit.json');m=read(F/'target-source-manifest.json')['files'];assert len(m)==501
assert p['build_exit_code']==ex['processExitCode']==0 and not a['blockers'] and a['status'].startswith('PASS_')
ms=sha(F/'target-source-manifest.json');assert ms==p['target_source_manifest_sha256']==a['sourceManifestSha256']=='08260273c9caebf7de11ae06cc5642403df23ef8328ac42399bf5714c95e29d6'
for rel,h in m.items():assert sha(F/rel)==h,rel
for field in ['inputPins','objectPins','archives','externalDependencies']:
 for path,h in a[field].items():assert sha(wp(path))==h,(field,path)
for rel,row in a['actualSourceMirrors'].items():assert sha(wp(row['actualPath']))==row['sha256']==m[rel],rel
for dep in a['dependencies']:
 assert wp(dep['object']).exists()
 for row in dep['dependencies']:assert sha(wp(row['path']))==row['sha256'],row['path']
log=wp(p['build_log']);assert sha(log)==p['build_log_sha256'];logtext=log.read_text(errors='replace');commands=a['linkCommands'];commands=[commands] if isinstance(commands,str) else commands
for c in commands:assert c in logtext,'link command missing'
e=F/'game/bin/vehicle-playground.elf';s=e.with_name(e.name+'.sym');assert sha(e)==p['selected_elf_sha256']==a['actualELFSha256']=='7bf09a918d7742abf57b4afa83fca3c8593edf8f75bfdab351bf2cab57114c4e';assert sha(s)==p['selected_symbol_sha256']==a['actualSymbolSha256']=='ec3d9b474ab626ae903f68210ba5d431d1f89dbf866b1117111c2f011651f673'
et,_=elf(e);st,img=elf(s);assert et('.text')==st('.text');th=digest(et('.text'));assert th==a['matchingTextSha256']
programs={}
for name,row in a['actualVUPrograms'].items():
 data,start,end=img(name);assert (start,end,len(data),digest(data),(len(data)+7)//8)==(row['address'],row['end'],row['bytes'],row['sha256'],row['roundedMicroInstructions']),name;programs[name]=row
TC='StaPipVU1Cull_TC_CodeStart';tc=img(TC)[0];assert len(tc)==3712
for row in a['actualAssemblerArtifacts']:
 ap=wp(row['path']);assert sha(ap)==row['sha256'];rp=L/'wild-pool-lattice-tc-native-v17-v9'/ap.name;assert sha(rp)==row['sha256']
 if ap.suffix=='.o':
  for op in [ap,rp]:assert elf(op)[1](TC)[0]==tc
_,baseimg=elf(L/'wild-pool2-ee-physical-v2/game/bin/vehicle-playground.elf.sym')
unchanged=[]
for name in programs:
 if name!=TC:assert img(name)[0]==baseimg(name)[0],name;unchanged.append(name)
bb=sum(programs[n]['roundedMicroInstructions'] for n in set(v for k,v in a['wrapperImages'].items() if k.startswith('stapip_billboard_')));assert bb==206
budgets={}
for name,row in a['residentBudgets'].items():
 main=sum(programs[n]['roundedMicroInstructions'] for n in set(row['uniqueImageSymbols']));total=main+bb;assert main==row['mainWords'] and total==row['withBillboardsWords'] and total<row['drawFinishAddress']==2042;budgets[name]=total
assert budgets=={'VU1Clip':1998,'EEClip':1798}
abi=a['actualABI'];assert abi['status'].startswith('PASS_')
for path,h in abi['inputHashes'].items():assert sha(wp(path))==h,path
words=struct.unpack('<10I',(L/'wild-pool-lattice-target-abi-v1/layout.bin').read_bytes());assert words==tuple(abi['sizes'].values())==(48,6144,28,420,16,112,48,44,44,28)
assets=read(F/'runtime-assets-manifest.json')['files'];base=read(L/'wild-pool-table-physical-v3/runtime-assets-manifest.json')['files'];assert assets==base and len(assets)==298
adpcm=[r for r in assets if r.endswith('.adpcm')];assert len(adpcm)==4
for rel,h in assets.items():assert sha(F/'game/bin'/rel)==h,rel
result={'status':'PASS_INDEPENDENT_LATTICE_POST_NATIVE_FILE_CONFIRMATION','rootAuditSha256':sha(A),'checkerSha256':sha(Path(__file__)),'sourceManifestSha256':ms,'sourceFiles':len(m),'rehashInputPins':len(a['inputPins']),'sourceMirrors':len(a['actualSourceMirrors']),'dependencies':len(a['dependencies']),'objects':len(a['objectPins']),'elfSha256':sha(e),'symbolSha256':sha(s),'matchingTextSha256':th,'tcImageSha256':digest(tc),'tcMicroInstructions':464,'tcMatchesActualAndRetainedSuccessfulV17Objects':True,'otherVUImagesIdenticalToQualifiedPool2':unchanged,'residentWithBillboardWords':budgets,'r5900ABIWords':words,'assetsExactBaseline':298,'adpcm':adpcm,'noBuildDeviceSourceOrMirrorMutations':True,'runtimeActivationPixelOutputOrPhysicalGainAccepted':False,'limits':'Independent read-only source, dependency, object, linked-image, ABI and runtime-asset identity. No target execution or semantic acceptance.'}
(O/'proof.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
