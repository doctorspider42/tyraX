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
assert F.name=='wild-gs-sprite-corona-probe-physical-v2', 'dedicated diagnostic fixture required'
P=F.parent/'wild-gs-sprite-corona-physical-v2';D=F.parent/'wild-gs-sprite-corona-probe-design-v2'
reference=read(pin(P/'target-source-manifest.json'));assert reference['frozen'] and len(reference['files'])==500
assert sha(P/'target-source-manifest.json')=='b9582677a88857c64d7c807e8c7acfb170d4aa84b5ef826bdd62734f1e26ebe3'
design=read(pin(D/'preparation-proof.json'));assert design['pricingManifestSha256']==sha(P/'target-source-manifest.json')
delta=['game/src/terrain_game.cpp','game/inc/pool2_static_probe.hpp','game/src/main.cpp'];expected=dict(reference['files'])
for rel in delta:expected[rel]=sha(pin(D/'source'/rel))
assert expected==m['files'], 'exact pricing inventory plus only the three authored Bits16 capability diagnostic deltas required'
assert {k for k in expected if expected[k]!=reference['files'].get(k)}==set(delta)
assert all(m['files'][k]==h for k,h in reference['files'].items() if k.startswith('tyra/'))
probeSource=(F/delta[1]).read_text();gameSource=(F/delta[0]).read_text()
assert 'options.colorDepth = Tyra::ColorDepth::Bits16;' in (F/delta[2]).read_text()
assert 'RendererCoreDepth::bits!=16' in probeSource
assert gameSource.count('#include "pool2_static_probe.hpp"')==1
assert 'Pool2StaticProbe::init(engine, stapip);' in gameSource and 'Pool2StaticProbe::loop(engine, stapip);' in gameSource
assert 'volatile unsigned pool2ProbeReady=0;' in probeSource and 'void pool2ProbeHalt()' in probeSource
assert 'pool2ProbeReady=0x504f4f32' in probeSource and 'synchronizeFrame()' in probeSource
assert 'collectCounters=false' in probeSource and 'memcmp' in probeSource

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
 'game/src/terrain_game.cpp':['pool2_static_probe.hpp','night_ablation.hpp','stapip_corona_sprite.hpp'],
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
# Check all actual images against the immutable qualified pricing ELF, including TC.
pb,ps,psections,psyms=elf(pin(P/'game/bin/vehicle-playground.elf.sym'))
for label,program in programs.items():
 address,size,index=psyms[label];end=psyms[label[:-9]+'CodeEnd'][0];section=ps[index]
 data=pb[section[4]+address-section[3]:section[4]+end-section[3]]
 assert hashlib.sha256(data).hexdigest()==program['sha256'],('pricing image changed',label)
assert len(programs)==16 and programs['StaPipVU1Cull_TC_CodeStart']['roundedMicroInstructions']==506
# Retain ELF symbol binding/type information for the actual diagnostic exports.
def details(path):
 blob,secs,_,_=elf(path);result={}
 for section in secs:
  if section[1]!=2:continue
  stringsSection=secs[section[6]];strings=secdata(blob,stringsSection)
  for offset in range(section[4],section[4]+section[5],section[9]):
   name,value,size,info,other,index=struct.unpack_from('<IIIBBH',blob,offset)
   name=strings[name:].split(b'\0',1)[0].decode('ascii','replace')
   result[name]={'address':value,'size':size,'binding':info>>4,'type':info&15,'sectionIndex':index}
 return blob,secs,result
terrain=[d for d in deps if any(v['manifestPath']=='game/src/terrain_game.cpp' for v in d['dependencies'])];assert len(terrain)==1
terrainObject=pin(Path(terrain[0]['object']));tb,ts,td=details(terrainObject);_,_,ld=details(symp)
diagnosticExports={}
for label,typ in [('pool2ProbeReady',1),('pool2ProbeHalt',2)]:
 for entry,secs in [(td[label],ts),(ld[label],ss)]:
  assert entry['binding']==1 and entry['type']==typ and 0<entry['sectionIndex']<len(secs), (label,entry)
  flags=secs[entry['sectionIndex']][2];assert flags&2
  assert (flags&1 if typ==1 else flags&4),(label,flags)
 assert td[label]['size']==ld[label]['size'] and ld[label]['size']>0
 entry=ld[label];section=ss[entry['sectionIndex']];start=section[4]+entry['address']-section[3]
 assert section[3]<=entry['address'] and entry['address']+entry['size']<=section[3]+section[5]
 data=b[start:start+entry['size']] if section[1]!=8 else bytes(entry['size'])
 if typ==1:assert entry['size']==4 and data==bytes(4), 'READY must be initial zero u32'
 diagnosticExports[label]={**entry,'sectionType':section[1],'sectionFlags':section[2],'linkedBytesSha256':hashlib.sha256(data).hexdigest(),'linkedBytesHex':data.hex(),'definingObject':str(terrainObject),'objectSha256':sha(terrainObject)}

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
assert budgets['VU1Clip']['withBillboardsWords']==2040 and budgets['EEClip']['withBillboardsWords']==1840
shader=(F/'tyra/engine/src/renderer/3d/pipeline/static/core/programs/cull/stapip_cull_tc_vu1.vclpp').read_text()
assert 'VU1_STAPIP_CORONA_SPRITE_FLAG' in shader and 'coronaQuadCheck:' in shader and 'coronaCompact:' in shader
post=shader[shader.index('coronaVertexCheck:'):shader.index('; Bound signed-VI differences')]
assert 'iaddiu  colorRunRemaining, vi00, 6' in shader and 'iaddi   colorRunRemaining, colorRunRemaining, -1' in post and 'ibne    colorRunRemaining, vi00, coronaVertexCheck' in post
assert all(t in post for t in ('sub     color3, color2, color1','sub.zw  color3, vertex2, vertex1','sub.z   color3, outputStq2, outputStq1')), 'all six final RGBA/Z/F/Q checks required'
assert 'ilw.x   singleColorEnabled, VU1_OPTIONS_ADDR(vi00)' in shader[shader.index('coronaDone:'):], 'MSCNT persistent option restore required'
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
pricingAssets=read(pin(P/'runtime-assets-manifest.json'))['files'];assert assets==pricingAssets and len(assets)==298
assert len([p for p in assets if p.lower().endswith('.adpcm')])==4
for rel,h in assets.items():assert sha(pin(F/'game/bin'/rel))==h
for row in restored['byte_equal_assets']:assert sha(pin(F/'game/.res-baked'/row['path']))==row['sha256']==sha(F/'game/bin'/row['path'])
for row in restored['converted_audio']:assert sha(F/'game/bin'/row['runtime'])==row['runtime_sha256'] and sha(pin(F/'game/.res-baked'/row['source']))==row['source_wav_sha256']
link=[v for v in txt.splitlines() if 'mips64r5900el-ps2-elf-g++ ' in v and ' -c ' not in v and 'vehicle-playground.elf' in v];assert link
assert any('obj/terrain_game.o' in line for line in link), 'actual diagnostic defining object must be linked'
archives={str(p):sha(pin(p)) for p in (E/'engine').rglob('*.a')};assert archives
pin(Path(__file__));q={'status':'PASS_ROOT_CORONA_DIAGNOSTIC_ACTUAL_NATIVE_501_SOURCE_LINKED_EXPORTS_TC_ABI_ASSETS','blockers':[],'sourceManifestSha256':sha(F/'target-source-manifest.json'),'sourceFiles':501,'mirroredInputs':492,'diagnosticOnly':True,'diagnosticExports':diagnosticExports,'readyAddress':diagnosticExports['pool2ProbeReady']['address'],'haltAddress':diagnosticExports['pool2ProbeHalt']['address'],'sourceDeltasFromPricing':delta,'pricingSourceManifestSha256':sha(P/'target-source-manifest.json'),'allActualVUImagesEqualPricing':True,'ancillaryNonmirrored':ancillary,'actualSourceMirrors':mapping,'dependencies':deps,'externalDependencies':external,'objectPins':objects,'archives':archives,'linkCommands':link,'actualELFSha256':sha(elfp),'actualSymbolSha256':sha(symp),'matchingTextSha256':hashlib.sha256(secdata(b,sections['.text'])).hexdigest(),'actualVUPrograms':programs,'wrapperImages':wrapperImages,'residentBudgets':budgets,'linkedTCMatchesActualAssembledObject':True,'unchangedBaseline2ImageSymbols':unchanged,'baseline2NativeProofSha256':sha(F.parent/'wild-pool2-ee-native-root-review-v2/proof.json'),'coronaSixFinalOutputChecksSourceBound':True,'targetSpriteAcceptanceKnown':False,'actualAssemblerArtifacts':assembled,'actualABI':abi,'runtimeAssets':len(assets),'inputPins':pins,'deviceBuildOrSourceMutations':False,'runtimeWarmActivationOutputOrGainAccepted':False,'limits':['Native assembler acceptance and linked byte identity are not formal per-path VU register/delay semantic proof. Actual output probe/raster remains required.','Resident budgets are source-configured families, not evidence of actual runtime clipping/class selection or program swaps.','Dependency/object observations distinguish actual bytes from fresh compilation; unchanged reused objects are not claimed recompiled.','Actual positive SPRITE output, original triangle fallback, warm cache arm isolation, raster and both-order physical pricing remain separate runtime gates. Sparse EE counters are requests, not accepted sprites.']}
O.mkdir();(O/'proof.json').write_text(json.dumps(q,indent=2)+'\n',encoding='utf-8',newline='\n');print(q['status'],sha(O/'proof.json'));print(json.dumps(budgets,indent=2))
