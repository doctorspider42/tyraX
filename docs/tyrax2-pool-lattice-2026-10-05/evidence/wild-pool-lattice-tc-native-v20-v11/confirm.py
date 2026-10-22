from pathlib import Path
import hashlib,json,struct,shutil,re
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

L=Path('/mnt/f/Projects/tyrax2-lab-20261001');O=L/'wild-pool-lattice-tc-native-v20-v11';F=L/'wild-pool-lattice-physical-v7';E=Path('/home/spider/.cache/tyrax/native/engines/f8c6050d7adf6fc3145900aa/tyra/engine')
p=read(F/'root-native-provenance.json');m=read(F/'target-source-manifest.json')['files'];assert len(m)==501
for rel,h in m.items():assert sha(F/rel)==h,rel
vrel='tyra/engine/src/renderer/3d/pipeline/static/core/programs/cull/stapip_cull_tc_vu1.vclpp';assert sha(F/vrel)==sha(L/'wild-pool-lattice-source-v11'/vrel)=='b4947038953baa8b7cfc97ceb33296d08f0fb0892c8dcb7c353723530c195248'
assert sha(E/'src/renderer/3d/pipeline/static/core/programs/cull/stapip_cull_tc_vu1.vclpp')==sha(F/vrel)
e=F/'game/bin/vehicle-playground.elf';s=e.with_name(e.name+'.sym');assert sha(e)==p['selected_elf_sha256'];assert sha(s)==p['selected_symbol_sha256'];et,_=elf(e);st,img=elf(s);assert et('.text')==st('.text')
a=read(L/'wild-pool-lattice-native-root-review-v1/proof.json');tcname='StaPipVU1Cull_TC_CodeStart';tc=img(tcname)[0];programs={};unchanged=[];_,baseimg=elf(L/'wild-pool2-ee-physical-v2/game/bin/vehicle-playground.elf.sym')
for name in a['actualVUPrograms']:
 data,start,end=img(name);programs[name]={'bytes':len(data),'sha256':digest(data),'instructions':(len(data)+7)//8,'address':start,'end':end}
 if name!=tcname:assert data==baseimg(name)[0],name;unchanged.append(name)
bb=sum(programs[n]['instructions'] for n in set(v for k,v in a['wrapperImages'].items() if k.startswith('stapip_billboard_')));assert bb==206
budgets={}
for name,row in a['residentBudgets'].items():
 total=sum(programs[n]['instructions'] for n in set(row['uniqueImageSymbols']))+bb;assert total<2042;budgets[name]=total
artifacts={}
for suffix in ['.o','.o.vcl','.o.vsm']:
 ap=E/('obj/renderer/3d/pipeline/static/core/programs/cull/stapip_cull_tc_vu1'+suffix);h=sha(ap)
 if suffix=='.o':assert elf(ap)[1](tcname)[0]==tc
 elif suffix=='.o.vcl':
  text=ap.read_text();tail=text[text.index('latticeEmitLoop:'):text.index('vertexLoopsDone:')];assert tail.count('mul.x')==3 and tail.count('mtir')==3;assert 'fogConstInt' in tail
 else:
  text=ap.read_text();assert 'latticeEmitLoop' in text;assert 'vf' in text.lower() and 'vi' in text.lower()
 shutil.copy2(ap,O/ap.name);assert sha(O/ap.name)==h;artifacts[str(ap)]={'sha256':h,'retained':str(O/ap.name),'bytes':ap.stat().st_size}
result={'status':'PASS_INDEPENDENT_V11_ACTUAL_NATIVE_TC_IMAGE_BUDGET_SOURCE_AND_RETAINED_ARTIFACTS','sourceManifestSha256':sha(F/'target-source-manifest.json'),'sourceFiles':len(m),'elfSha256':sha(e),'symbolSha256':sha(s),'tc':programs[tcname],'residentWithBillboards':budgets,'spareTo2042':{k:2042-v for k,v in budgets.items()},'otherImagesIdenticalQualifiedPool2':unchanged,'actualArtifacts':artifacts,'fogExpandedThreeCalls':True,'compiledObjectEqualsLinkedTC':True,'noNativeBuildOrDeviceOrMutableCacheWrites':True,'limits':'Source/artifact/image identity and size only; actual fog-on decoded output and ordinary activation remain unverified.'}
(O/'proof.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
