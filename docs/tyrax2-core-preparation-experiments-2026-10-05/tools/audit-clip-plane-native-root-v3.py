"""Read-only completed native audit, invoked only after root successful provenance."""
from pathlib import Path
import argparse,json,hashlib,re,shlex,struct
ap=argparse.ArgumentParser();ap.add_argument('--fixture',type=Path,required=True);ap.add_argument('--abi',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);a=ap.parse_args();F=a.fixture;O=a.out;assert not O.exists();assert (F/'root-native-provenance.json').exists(),'Root completed success provenance required'
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def wp(p):return Path('/mnt/'+p[0].lower()+p[2:].replace('\\','/')) if len(p)>2 and p[1]==':' else Path(p)
pins={}
def pin(p):pins[str(p)]=sha(p);return p
x=read(pin(F/'root-native-command-exit.json'));n=read(pin(F/'root-native-provenance.json'));m=read(pin(F/'target-source-manifest.json'));fr=read(pin(F/'root-source-freeze.json'));assert x['processExitCode']==n['build_exit_code']==0 and m['frozen'] and len(m['files'])==501
assert F.name=='clip-plane-specialize-physical-v3'
base=read(pin(F.parent/'corona24-pricing-physical-v1/target-source-manifest.json'));assert sorted(k for k in m['files']if m['files'][k]!=base['files'][k])==fr['changes']
assert sha(F/'target-source-manifest.json')==fr['sourceManifestSha256']==x['sourceManifestSha256']==n['target_source_manifest_sha256']
log=pin(wp(x['buildLog']));txt=log.read_text();assert '[editor] Native build complete:' in txt and sha(log)==n['build_log_sha256']
def unique(pattern):
 hits=sorted(set(re.findall(pattern,txt)));assert len(hits)==1,(pattern,hits);return Path(hits[0])
E=unique(r'(/home/spider/\.cache/tyrax/native/engines/[a-z0-9]+/tyra)/engine=');G=unique(r'(/home/spider/\.cache/tyrax/native/projects/[a-z0-9]+)='+re.escape(str(F/'game')));T=unique(r'(/home/spider/\.cache/tyrax/native/toolchains/[a-z0-9]+)=')
mapping={};reverse={};ancillary={}
allowed={'tyra/.clang-format','tyra/.dockerignore','tyra/.gitattributes','tyra/.gitignore','tyra/docker-compose.yml','tyra/Dockerfile','tyra/LICENSE','tyra/README.MD','tyra/windows-pcsx2.ps1'}
for rel,h in m['files'].items():
 p=G/rel[5:] if rel.startswith('game/') else E/rel[5:] if rel.startswith('tyra/') else None;assert p and sha(pin(F/rel))==h,rel
 if not p.exists():assert rel in allowed,rel;ancillary[rel]=h;continue
 assert sha(pin(p))==h,rel;mapping[rel]={'actualPath':str(p),'sha256':h};reverse[str(p.resolve())]=rel
assert set(ancillary)==allowed and len(mapping)==492
used=set();deps=[];external={};objects={}
for working in [E/'engine',G]:
 for p in sorted((working/'obj').rglob('*.d')):
  logical=pin(p).read_text().replace('\\\n','').splitlines()[0];target,body=logical.split(':',1);items=[]
  obj=(working/target).resolve();assert obj.exists(),obj;objects[str(obj)]=sha(pin(obj))
  for name in shlex.split(body):
   q=(working/name).resolve() if not name.startswith('/') else Path(name).resolve();h=sha(q);key=reverse.get(str(q));items.append({'path':str(q),'sha256':h,'manifestPath':key})
   if key:used.add(key)
   else:external[str(q)]=h
  deps.append({'path':str(p),'object':str(obj),'dependencies':items})
for rel,headers in {
 'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer.cpp':['stapip_pool_color_table.hpp','stapip_corona_sprite.hpp'],
 'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp':['night_ablation.hpp','stapip_pool_color_table.hpp','stapip_corona_sprite.hpp'],
 'tyra/engine/src/renderer/3d/pipeline/static/core/programs/cull/stapip_cull_tc_vu1_program.cpp':['stapip_pool_color_packet.hpp'],
 'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_vu1_program.cpp':['stapip_corona_sprite.hpp'],
 'game/src/gen/game_lighting.gen.cpp':['stapip_pool_color_table.hpp','stapip_corona_sprite.hpp']}.items():
 hit=[d for d in deps if any(v['manifestPath']==rel for v in d['dependencies'])];assert len(hit)==1,rel
 for h in headers:assert any(v['manifestPath'] and v['manifestPath'].endswith('/'+h) for v in hit[0]['dependencies']),(rel,h)
def elf(p):
 b=p.read_bytes();assert b[:6]==b'\x7fELF\x01\x01';off=struct.unpack_from('<I',b,32)[0];ent,count,idx=struct.unpack_from('<HHH',b,46);ss=[struct.unpack_from('<10I',b,off+i*ent)for i in range(count)];ns=ss[idx];names=b[ns[4]:ns[4]+ns[5]];sections={names[s[0]:].split(b'\0',1)[0].decode():s for s in ss};syms={}
 for sec in ss:
  if sec[1]!=2:continue
  st=ss[sec[6]];strings=b[st[4]:st[4]+st[5]]
  for i in range(sec[4],sec[4]+sec[5],sec[9]):
   name,val,size,info,other,index=struct.unpack_from('<IIIBBH',b,i);label=strings[name:].split(b'\0',1)[0].decode('ascii','replace');syms[label]=(val,size,index)
 return b,ss,sections,syms
elfp=pin(F/'game/bin/vehicle-playground.elf');symp=pin(F/'game/bin/vehicle-playground.elf.sym');assert sha(elfp)==n['selected_elf_sha256']==sha(pin(G/'bin/vehicle-playground.elf'));assert sha(symp)==n['selected_symbol_sha256']==sha(pin(G/'bin/vehicle-playground.elf.sym'))
b,ss,sections,syms=elf(symp);eb,es,esections,_=elf(elfp)
def secdata(blob,s):return blob[s[4]:s[4]+s[5]]
assert secdata(b,sections['.text'])==secdata(eb,esections['.text']) and sections['.text'][3]==esections['.text'][3]
programs={}
for label,(address,size,index) in syms.items():
 if not label.endswith('_CodeStart'):continue
 endlabel=label[:-9]+'CodeEnd'
 if endlabel not in syms:continue
 end=syms[endlabel][0];length=end-address;assert length>=0 and length%8==0,(label,length)
 rounded=((length//8)+1)&~1
 sec=ss[index];data=b[sec[4]+address-sec[3]:sec[4]+end-sec[3]]
 programs[label]={'address':address,'end':end,'bytes':length,'roundedMicroInstructions':rounded,'sha256':hashlib.sha256(data).hexdigest()}
assert 'StaPipVU1Cull_TC_CodeStart' in programs
# Source wrapper aliases identify resident image choices, including C/D and TC/TCE sharing.
wrapperImages={}
for p in sorted((E/'engine/src/renderer/3d/pipeline/static/core/programs').rglob('*_program.cpp')):
 pin(p);obj=E/'engine/obj'/p.relative_to(E/'engine/src').with_suffix('.o')
 _,_,_,symbols=elf(pin(obj));choices=[name for name in symbols if name.endswith('_CodeStart')]
 assert len(choices)==1,(str(obj),choices)
 wrapperImages[p.stem]=choices[0]
assert len(wrapperImages)==17,len(wrapperImages)
def family(kind):return sorted(set(v for k,v in wrapperImages.items() if k.startswith('stapip_'+kind+'_')))
def words(names):return sum(programs[name]['roundedMicroInstructions'] for name in names)
cull=family('cull');clip=family('clip');asis=family('as_is');bb=family('billboard');assert len(cull)==5 and len(clip) in (2,3) and len(asis)==5 and len(bb)==2
finish=programs['VU1DrawFinish_CodeStart']['roundedMicroInstructions'];limit=2048-finish;assert limit==2042,limit
budgets={}
for mode,second in [('VU1Clip',clip),('EEClip',asis)]:
 names=sorted(set(cull+second));main=words(names);assert main<=limit,(mode,main,limit)
 budgets[mode]={'uniqueImageSymbols':names,'mainWords':main,'billboardWords':words(bb),'withBillboardsWords':main+words(bb),'billboardsFit':main+words(bb)<=limit,'drawFinishAddress':limit,'measuredRuntimeSelectionProven':False}
# Actual assembled TC object must provide exactly the linked image, not an old image with the same wrapper class.
tc=E/'engine/obj/renderer/3d/pipeline/static/core/programs/cull/stapip_cull_tc_vu1.o';ob,oss,osec,osym=elf(pin(tc));st,_,index=osym['StaPipVU1Cull_TC_CodeStart'];en=osym['StaPipVU1Cull_TC_CodeEnd'][0];sec=oss[index];image=ob[sec[4]+st-sec[3]:sec[4]+en-sec[3]];assert hashlib.sha256(image).hexdigest()==programs['StaPipVU1Cull_TC_CodeStart']['sha256']
# V11 compiler artifacts are bound directly to the actual assembled object and linked ELF above.
baseline=read(pin(F.parent/'wild-pool2-ee-native-root-review-v2/proof.json'));assert baseline['status'].startswith('PASS_') and not baseline.get('blockers');unchanged=[]
for label,program in programs.items():
 if label=='StaPipVU1Cull_TC_CodeStart':continue
 assert program['sha256']==baseline['actualVUPrograms'][label]['sha256'],label
 unchanged.append(label)
assert len(unchanged)==15, ('exact fifteen unchanged non-TC images required',unchanged)
assert budgets['VU1Clip']['withBillboardsWords']<=2042 and budgets['EEClip']['withBillboardsWords']<=2042
assert all(b['billboardsFit'] for b in budgets.values())
shader=(F/'tyra/engine/src/renderer/3d/pipeline/static/core/programs/cull/stapip_cull_tc_vu1.vclpp').read_text()
assert 'VU1_STAPIP_CORONA_SPRITE_FLAG' in shader and 'coronaQuadCheck:' in shader and 'coronaCompact:' in shader
post=shader[shader.index('coronaVertexCheck:'):shader.index('; Bound signed-VI differences')]
assert 'iaddiu  colorRunRemaining, vi00, 6' in shader and 'iaddi   colorRunRemaining, colorRunRemaining, -1' in post and 'ibne    colorRunRemaining, vi00, coronaVertexCheck' in post
assert all(t in post for t in ('sub     color3, color2, color1','sub.zw  color3, vertex2, vertex1','sub.z   color3, outputStq2, outputStq1')), 'all six final RGBA/Z/F/Q checks required'
assert 'ilw.x   singleColorEnabled, VU1_OPTIONS_ADDR(vi00)' in shader[shader.index('coronaDone:'):], 'MSCNT persistent option restore required'
vsm=pin(Path(str(tc)+'.vsm'));assert len(re.findall(r'abs\.[xyzw]+ VF00, VF18\s+fmand',vsm.read_text()))==6
qualified=read(pin(F.parent/'corona24-probe-native-root-review-v1/proof.json'));assert all(programs[k]['sha256']==qualified['actualVUPrograms'][k]['sha256'] for k in programs), 'all qualified diagnostic microprogram bytes retained'
assembled=[]
for p in sorted(tc.parent.glob(tc.name+'*')):
 pin(p);assembled.append({'path':str(p),'sha256':sha(p)})
# The completed SDK dependency is authoritative, not the earlier host shim.
sdk=[Path(p) for p in external if p.endswith('/packet2_utils.h')];assert sdk
for p in sdk:pin(p);assert 'packet2_utils_vu_add_unpack_data' in p.read_text()
abi=read(pin(a.abi/'proof.json'));assert abi['sizes']=={'Sample':48,'samples':6144,'Chunk':28,'chunks':420,'Counter':16,'commonCounter':112,'extraCounter':48,'WildCounter':44,'PoolTableCounter':44,'CoronaCounter':36}
for p,h in abi['inputHashes'].items():assert sha(pin(wp(p)))==h
assert struct.unpack('<10I',(a.abi/'layout.bin').read_bytes())==tuple(abi['sizes'].values())
assets=read(pin(F/'runtime-assets-manifest.json'))['files'];restored=read(pin(F/'runtime-assets-restored.json'));assert len(assets)==n['runtime_assets']==len(restored['byte_equal_assets'])+len(restored['converted_audio'])
for rel,h in assets.items():assert sha(pin(F/'game/bin'/rel))==h
for row in restored['byte_equal_assets']:assert sha(pin(F/'game/.res-baked'/row['path']))==row['sha256']==sha(F/'game/bin'/row['path'])
for row in restored['converted_audio']:assert sha(F/'game/bin'/row['runtime'])==row['runtime_sha256'] and sha(pin(F/'game/.res-baked'/row['source']))==row['source_wav_sha256']
link=[v for v in txt.splitlines() if 'mips64r5900el-ps2-elf-g++ ' in v and ' -c ' not in v and 'vehicle-playground.elf' in v];assert link
archives={str(p):sha(pin(p)) for p in (E/'engine').rglob('*.a')};assert archives
pin(Path(__file__));q={'status':'PASS_ROOT_CLIP_PLANE_SPECIALIZE_ACTUAL_NATIVE_501_SOURCE_LINKED_ABI_ASSETS','blockers':[],'sourceManifestSha256':sha(F/'target-source-manifest.json'),'sourceFiles':501,'mirroredInputs':492,'ancillaryNonmirrored':ancillary,'actualSourceMirrors':mapping,'dependencies':deps,'externalDependencies':external,'objectPins':objects,'archives':archives,'linkCommands':link,'actualELFSha256':sha(elfp),'actualSymbolSha256':sha(symp),'matchingTextSha256':hashlib.sha256(secdata(b,sections['.text'])).hexdigest(),'actualVUPrograms':programs,'wrapperImages':wrapperImages,'residentBudgets':budgets,'linkedTCMatchesActualAssembledObject':True,'unchangedBaseline2ImageSymbols':unchanged,'baseline2NativeProofSha256':sha(F.parent/'wild-pool2-ee-native-root-review-v2/proof.json'),'coronaSixFinalOutputChecksSourceBound':True,'targetSpriteAcceptanceKnown':False,'actualAssemblerArtifacts':assembled,'actualABI':abi,'runtimeAssets':len(assets),'inputPins':pins,'deviceBuildOrSourceMutations':False,'runtimeWarmActivationOutputOrGainAccepted':False,'limits':['Native assembler acceptance and linked byte identity are not formal per-path VU register/delay semantic proof. Actual output probe/raster remains required.','Resident budgets are source-configured families, not evidence of actual runtime clipping/class selection or program swaps.','Dependency/object observations distinguish actual bytes from fresh compilation; unchanged reused objects are not claimed recompiled.','Actual positive SPRITE output, original triangle fallback, warm cache arm isolation, raster and both-order physical pricing remain separate runtime gates. Sparse EE counters are requests, not accepted sprites.']}
O.mkdir();(O/'proof.json').write_text(json.dumps(q,indent=2)+'\n',encoding='utf-8',newline='\n');print(q['status'],sha(O/'proof.json'));print(json.dumps(budgets,indent=2))
