"""Read-only completed native audit, invoked only after root successful provenance."""
from pathlib import Path
import argparse,json,hashlib,re,shlex,struct
ap=argparse.ArgumentParser();ap.add_argument('--fixture',type=Path,required=True);ap.add_argument('--abi',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--pricing-fixture',type=Path,required=True);ap.add_argument('--pricing-proof',type=Path,required=True);a=ap.parse_args();F=a.fixture;O=a.out;assert not O.exists();assert (F/'root-native-provenance.json').exists(),'Root completed success provenance required'
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def wp(p):return Path('/mnt/'+p[0].lower()+p[2:].replace('\\','/')) if len(p)>2 and p[1]==':' else Path(p)
pins={}
def pin(p):pins[str(p)]=sha(p);return p
x=read(pin(F/'root-native-command-exit.json'));n=read(pin(F/'root-native-provenance.json'));m=read(pin(F/'target-source-manifest.json'));fr=read(pin(F/'root-source-freeze.json'));assert x['processExitCode']==n['build_exit_code']==0 and m['frozen'] and len(m['files'])==500
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
assert set(ancillary)==allowed and len(mapping)==491
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
 'game/src/terrain_game.cpp':['pool2_static_probe.hpp'],
 'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer.cpp':['stapip_pool_color_table.hpp'],
 'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp':['night_ablation.hpp','stapip_pool_color_table.hpp'],
 'tyra/engine/src/renderer/3d/pipeline/static/core/programs/cull/stapip_cull_tc_vu1_program.cpp':['stapip_pool_color_packet.hpp'],
 'game/src/gen/game_lighting.gen.cpp':['stapip_pool_color_table.hpp']}.items():
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
# Bind the full-build-expanded source and scheduled VSM to the accepted isolated V3 source.
iso=F.parent/'pool2-v3-isolated-assembly-v1'
assert sha(pin(Path(str(tc)+'.vcl')))==sha(pin(iso/'v3-source.o.vcl'))
assert sha(pin(Path(str(tc)+'.vsm')))==sha(pin(iso/'v3-source.vsm'))
_,_,_,isolatedSymbols=elf(pin(iso/'v3-source.o'))
ib,iss,_,_=elf(iso/'v3-source.o');ist=isolatedSymbols['StaPipVU1Cull_TC_CodeStart'];ien=isolatedSymbols['StaPipVU1Cull_TC_CodeEnd'][0];isec=iss[ist[2]];idata=ib[isec[4]+ist[0]-isec[3]:isec[4]+ien-isec[3]]
assert image==idata,'Full native TC image differs from isolated assembled V3 TC image'
shader=(F/'tyra/engine/src/renderer/3d/pipeline/static/core/programs/cull/stapip_cull_tc_vu1.vclpp').read_text(encoding='utf-8')
assert len(re.findall(r'ibgez\s+colorStride,\s+(poolLegacyLoad\d|poolLegacyAdvance\d)',shader))==6
assert len(re.findall(r'iblez\s+poolColorEnabled',shader))==1
assert 'iaddi   colorStride,        vi00,            -1' in shader
assert 'VU1_STAPIP_POOL2_FLAG' in shader and 'VU1_STAPIP_COUNT_MASK' in shader
# This is a distinct diagnostic identity, with one changed game source and one new header.
pricingManifest=read(pin(a.pricing_fixture/'target-source-manifest.json'));pricingProof=read(pin(a.pricing_proof))
assert len(pricingManifest['files'])==499 and pricingManifest['frozen']
assert pricingProof['status']=='PASS_INDEPENDENT_POOL2_ACTUAL_NATIVE_SOURCE_DEPENDENCIES_LINKED_TC_IMAGE_ABI_ASSETS' and pricingProof['blockers']==[]
assert pricingProof['sourceManifestSha256']==sha(a.pricing_fixture/'target-source-manifest.json')
changed=[]
for rel,h in pricingManifest['files'].items():
 assert sha(pin(a.pricing_fixture/rel))==h,rel
 if m['files'][rel]!=h:changed.append(rel)
assert changed==['game/src/terrain_game.cpp'] and set(m['files'])-set(pricingManifest['files'])=={'game/inc/pool2_static_probe.hpp'}
assert all(m['files'][rel]==h for rel,h in pricingManifest['files'].items() if rel.startswith('tyra/'))
pricingELF=pin(a.pricing_fixture/'game/bin/vehicle-playground.elf');pricingSym=pin(a.pricing_fixture/'game/bin/vehicle-playground.elf.sym')
assert sha(pricingELF)==pricingProof['actualELFSha256'] and sha(pricingSym)==pricingProof['actualSymbolSha256']
assert programs['StaPipVU1Cull_TC_CodeStart']['sha256']==pricingProof['actualVUPrograms']['StaPipVU1Cull_TC_CodeStart']['sha256']
header=(F/'game/inc/pool2_static_probe.hpp').read_text(encoding='utf-8');game=(F/'game/src/terrain_game.cpp').read_text(encoding='utf-8')
assert header.count('engine->renderer.renderer3D.usePipeline(pipeline);')==1
assert header.index('usePipeline(pipeline)')<header.index('pipeline.core.render(&bag);')<header.index('engine->renderer.endFrame();')<header.index('engine->renderer.core.synchronizeFrame();')
assert 'pool2ProbeReady=0x504f4f32u;pool2ProbeHalt();' in header
assert 'volatile unsigned pool2ProbeReady = 0;' in header and '__attribute__((noinline,noreturn)) void pool2ProbeHalt()' in header
assert '#include "pool2_static_probe.hpp"' in game and 'Pool2StaticProbe::init(engine, stapip);' in game and 'Pool2StaticProbe::loop(engine, stapip);' in game
assert 'return;  // dedicated correctness ELF: ordinary init is excluded' in game and 'return;  // no new ordinary game/VU job after probe completion' in game
exports={}
for name in ['pool2ProbeReady','pool2ProbeHalt']:
 assert name in syms,name
 address,size,index=syms[name];assert index>0 and size>0
 sec=ss[index];data=b[sec[4]+address-sec[3]:sec[4]+address-sec[3]+size] if sec[1]!=8 else bytes(size)
 exports[name]={'address':address,'bytes':size,'section':index,'codeOrZeroInitializationSha256':hashlib.sha256(data).hexdigest()}
assert exports['pool2ProbeReady']['bytes']==4
halt=exports['pool2ProbeHalt'];assert halt['bytes']%4==0 and halt['bytes']<=128
hs=ss[halt['section']];hc=b[hs[4]+halt['address']-hs[3]:hs[4]+halt['address']-hs[3]+halt['bytes']]
exports['pool2ProbeHalt']['actualInstructionWords']=[f'{word:08x}' for word in struct.unpack('<'+'I'*(len(hc)//4),hc)]
# Fresh diagnostic terrain object, deps and recorded compile command must all bind the probe header.
terrain=next(d for d in deps if any(v['manifestPath']=='game/src/terrain_game.cpp' for v in d['dependencies']))
terrainObject=Path(terrain['object']);_,_,_,terrainSymbols=elf(pin(terrainObject))
assert 'pool2ProbeReady' in terrainSymbols and 'pool2ProbeHalt' in terrainSymbols
compileLines=[line for line in txt.splitlines() if 'mips64r5900el-ps2-elf-g++ ' in line and ' -c ' in line and 'src/terrain_game.cpp' in line];assert len(compileLines)==1
probeSourceProof=read(pin(F/'probe-source-proof.json'))
assert probeSourceProof.get('all500ReconstructedExact') is True
assert probeSourceProof['newGameSha256']==m['files']['game/src/terrain_game.cpp']
assert probeSourceProof['newSourceSha256']==m['files']['game/inc/pool2_static_probe.hpp']
assert probeSourceProof['oldGameSha256']==pricingManifest['files']['game/src/terrain_game.cpp']
assert probeSourceProof['baseManifestSha256']==sha(a.pricing_fixture/'target-source-manifest.json')
assembled=[]
for p in sorted(tc.parent.glob(tc.name+'*')):
 pin(p);assembled.append({'path':str(p),'sha256':sha(p)})
# The completed SDK dependency is authoritative, not the earlier host shim.
sdk=[Path(p) for p in external if p.endswith('/packet2_utils.h')];assert sdk
for p in sdk:pin(p);assert 'packet2_utils_vu_add_unpack_data' in p.read_text()
abi=read(pin(a.abi/'proof.json'));assert abi['sizes']=={'Sample':48,'samples':6144,'Chunk':28,'chunks':420,'Counter':16,'commonCounter':112,'extraCounter':48,'WildCounter':44,'PoolTableCounter':44}
for p,h in abi['inputHashes'].items():assert sha(pin(wp(p)))==h
assert struct.unpack('<9I',(a.abi/'layout.bin').read_bytes())==tuple(abi['sizes'].values())
assets=read(pin(F/'runtime-assets-manifest.json'))['files'];restored=read(pin(F/'runtime-assets-restored.json'));assert len(assets)==n['runtime_assets']==len(restored['byte_equal_assets'])+len(restored['converted_audio'])
assert len(assets)==298 and assets==read(pin(a.pricing_fixture/'runtime-assets-manifest.json'))['files'],'Diagnostic resource bytes differ from pricing fixture'
for rel,h in assets.items():assert sha(pin(F/'game/bin'/rel))==h
for row in restored['byte_equal_assets']:assert sha(pin(F/'game/.res-baked'/row['path']))==row['sha256']==sha(F/'game/bin'/row['path'])
for row in restored['converted_audio']:assert sha(F/'game/bin'/row['runtime'])==row['runtime_sha256'] and sha(pin(F/'game/.res-baked'/row['source']))==row['source_wav_sha256']
link=[v for v in txt.splitlines() if 'mips64r5900el-ps2-elf-g++ ' in v and ' -c ' not in v and 'vehicle-playground.elf' in v];assert link
archives={str(p):sha(pin(p)) for p in (E/'engine').rglob('*.a')};assert archives
pin(Path(__file__));q={'status':'PASS_INDEPENDENT_POOL2_STATIC_DIAGNOSTIC_ACTUAL_NATIVE_500_SOURCE_TC_IDENTITY_EXPORTS_ASSETS','blockers':[],'sourceManifestSha256':sha(F/'target-source-manifest.json'),'sourceFiles':500,'mirroredInputs':491,'ancillaryNonmirrored':ancillary,'actualSourceMirrors':mapping,'dependencies':deps,'externalDependencies':external,'objectPins':objects,'archives':archives,'linkCommands':link,'actualELFSha256':sha(elfp),'actualSymbolSha256':sha(symp),'matchingTextSha256':hashlib.sha256(secdata(b,sections['.text'])).hexdigest(),'actualVUPrograms':programs,'wrapperImages':wrapperImages,'residentBudgets':budgets,'linkedTCMatchesActualAssembledObject':True,'linkedTCEqualsAcceptedIsolatedV3Image':True,'nativeExpandedAndScheduledInputsEqualIsolatedV3':True,'actualAssemblerArtifacts':assembled,'actualABI':abi,'runtimeAssets':len(assets),'inputPins':pins,'deviceBuildOrSourceMutations':False,'runtimeWarmActivationOutputOrGainAccepted':False,'diagnosticOnly':True,'performanceAccepted':False,'diagnosticExports':exports,'freshProbeTerrainCompileCommand':compileLines[0],'probeTerrainObject':str(terrainObject),'pricingTCByteIdentityAccepted':True,'pricingProofSha256':sha(a.pricing_proof),'sourceDeltaFromPricing':changed+['game/inc/pool2_static_probe.hpp'],'limits':['Native assembler acceptance and linked byte identity are not formal per-path VU register/delay semantic proof. Actual output probe/raster remains required.','Resident budgets are source-configured families, not evidence of actual runtime clipping/class selection or program swaps.','Dependency/object observations distinguish actual bytes from fresh compilation; unchanged reused objects are not claimed recompiled.','Positive warm candidate marker/table replay, ownership/retirement and both-order physical pricing remain separate runtime gates.']}
O.mkdir();(O/'proof.json').write_text(json.dumps(q,indent=2)+'\n',encoding='utf-8',newline='\n');print(q['status'],sha(O/'proof.json'));print(json.dumps(budgets,indent=2))
