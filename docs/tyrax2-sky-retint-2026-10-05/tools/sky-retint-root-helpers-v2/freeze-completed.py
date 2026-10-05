"""ROOT ONLY evidence mutation; no process/socket/device control or live deletions."""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,struct,subprocess,sys
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
def need(v,msg):
 if not v:raise ValueError(msg)
def path(value):
 s=str(value)
 if os.name=='nt' and re.match(r'^/mnt/[a-z]/',s):return Path(s[5].upper()+':/'+s[7:])
 if os.name!='nt' and re.match(r'^[A-Za-z]:[\\/]',s):return Path('/mnt/'+s[0].lower()+'/'+s[3:].replace('\\','/'))
 return Path(s)
def load(f):return json.loads(f.read_text(encoding='utf-8-sig'))
def text(f):
 b=f.read_bytes();need(b[:6]==b'\x7fELF\x01\x01','ELF32 little endian');off=struct.unpack_from('<I',b,32)[0];size,num,names=struct.unpack_from('<HHH',b,46);ss=[struct.unpack_from('<10I',b,off+i*size)for i in range(num)];s=ss[names];ns=b[s[4]:s[4]+s[5]]
 for s in ss:
  if ns[s[0]:].split(b'\0')[0]==b'.text':return b[s[4]:s[4]+s[5]]
 raise ValueError('ELF .text missing')
p=argparse.ArgumentParser();p.add_argument('--launch',type=Path,required=True);p.add_argument('--stdout',type=Path,required=True);p.add_argument('--artifact',type=Path,required=True);p.add_argument('--environment',choices=['emulator','ps2'],required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--raw-stdout',type=Path);p.add_argument('--normalization-proof',type=Path);a=p.parse_args();need(not a.out.exists(),'unique NEW output directory required')
d=load(a.launch);fixture=path(d['fixture']);archive=path(d['launchArchive']);need(archive.resolve()==a.launch.parent.resolve(),'owned record/archive identity');need(a.launch.name=='owned-launch.json','actual owned launch record');need(fixture.name=='sky-retint-physical-v2','actual qualified V2 fixture required')
for name,digest in d['launchFiles'].items():need(Path(name).name==name,'archive name traversal');need(sha(archive/name)==digest,'launch archive drift '+name)
required={'night-ablation.cfg','quiet-night.cfg','root-source-freeze.json','target-source-manifest.json','root-native-provenance.json','runtime-assets-manifest.json','root-runtime-authority.json','vehicle-playground.elf','vehicle-playground.elf.sym'};need(required<=set(d['launchFiles']),'complete launch authority')
source=load(archive/'target-source-manifest.json');need(source['frozen'] and len(source['files'])==501,'frozen501source inventory');need(sha(fixture/'target-source-manifest.json')==sha(archive/'target-source-manifest.json'),'live source manifest drift')
for name,digest in source['files'].items():need(not Path(name).is_absolute() and '..' not in Path(name).parts,'source traversal');need(sha(fixture/name)==digest,'frozen source drift '+name)
game=fixture/'game/bin'
for name in ('night-ablation.cfg','quiet-night.cfg','vehicle-playground.elf','vehicle-playground.elf.sym'):need(sha(game/name)==sha(archive/name),'live startup/ELF drift '+name)
for name in ('root-source-freeze.json','root-native-provenance.json','runtime-assets-manifest.json','root-runtime-authority.json'):need(sha(fixture/name)==sha(archive/name),'live authority drift '+name)
kind,order,joint,restored=[d[k]for k in ('kind','order','joint','restored')];need(kind==19 and order in (0,1) and joint==restored==0,'kind9 zerojoint/restored required');need(all(type(x)is int for x in (kind,order,joint,restored)),'strict plan integer types');cfg=(archive/'night-ablation.cfg').read_text(encoding='utf8');need(re.fullmatch(r'\s*[0-9]+\s+[0-9]+\s+[0-9]+\s+[0-9]+\s*',cfg)is not None and list(map(int,cfg.split()))==[kind,order,joint,restored],'actual startup plan identity');need(re.fullmatch(r'\s*1\s*',(archive/'quiet-night.cfg').read_text())is not None,'ordinary stationary night')
freeze=load(archive/'root-source-freeze.json');need(freeze['sourceManifestSha256']==sha(archive/'target-source-manifest.json'),'root freeze source binding');authorityPins={}
release=load(archive/'root-runtime-authority.json');need(release['rootSourceFreezeSha256']==sha(archive/'root-source-freeze.json'),'early freeze/release link')
for name,hkey in (('review','reviewSha256'),('host','hostSha256')):
 f=path(release[name]);need(sha(f)==release[hkey],'root source review/host authority drift');authorityPins[str(f)]=sha(f)
native=load(archive/'root-native-provenance.json');need(native['build_backend']=='native' and native['build_exit_code']==0 and native['verified_source_files']==501,'actual native authority');need(native['target_source_manifest_sha256']==sha(archive/'target-source-manifest.json'),'native/source bind');need(native['selected_elf_sha256']==sha(archive/'vehicle-playground.elf')==d['elfSha256'] and native['selected_symbol_sha256']==sha(archive/'vehicle-playground.elf.sym'),'ELF/native bind');need(text(archive/'vehicle-playground.elf')==text(archive/'vehicle-playground.elf.sym'),'actual matching ELF/symbol text')
build=path(native['build_log']);need(sha(build)==native['build_log_sha256'],'actual native build log drift');authorityPins[str(build)]=sha(build)
assets=load(archive/'runtime-assets-manifest.json')['files'];need(len(assets)==native['runtime_assets']==298,'actual298asset inventory')
for name,digest in assets.items():need(not Path(name).is_absolute() and '..' not in Path(name).parts,'asset traversal');need(sha(game/name)==digest,'actual runtime asset drift '+name)
actual={f.relative_to(game).as_posix()for f in game.rglob('*')if f.is_file()and f.suffix not in('.elf','.sym','.cfg','.log','.run')};need(actual==set(assets),'extra/missing runtime assets')
need(a.artifact.resolve()==(game/'night-ablation.log').resolve(),'actual authoritative live artifact path');before={str(f):sha(f)for f in(a.launch,a.stdout,a.artifact)};need('NIGHTDONE order='+str(order)+' valid=1 loops=5400' in a.stdout.read_text(encoding='utf-8-sig'),'actual completed5400 marker')
if a.environment=='ps2':
 need(a.raw_stdout is not None and a.normalization_proof is not None,'physical lossless normalization authority required');normal=load(a.normalization_proof);need(normal['status']=='PASS_LOSSLESS_LATIN1_ASCII_NIGHT_PROTOCOL_NORMALIZATION' and normal['rawSha256']==sha(a.raw_stdout) and normal['normalizedSha256']==sha(a.stdout),'physical raw/normalized log binding')
else:
 need(a.raw_stdout is None and a.normalization_proof is None,'emulator stdout is actual UTF8 source');exe=path(d['exe']);need(sha(exe)==d['exeSha256'],'owned emulator executable identity')
cli=fixture.parent/'sky-retint-controls-v1/analyze-night-cli.py';host=load(path(release['host']))
for name,digest in host['sourcePins'].items():need(sha(path(name))==digest,'current strict host/parser source drift '+name)
need(str(cli)in[str(path(x))for x in host['sourcePins']],'strict CLI host pin')
# Create only after all source/native/cfg/assets prechecks; failed analysis remains explicit, never runtimePASS.
a.out.mkdir();command=[sys.executable,str(cli),'--stdout',str(a.stdout),'--artifact',str(a.artifact),'--environment',a.environment,'--kind',str(kind),'--order',str(order),'--joint',str(joint),'--restored',str(restored),'--report',str(a.out/'strict-analysis.json')];r=subprocess.run(command,capture_output=True,text=True);(a.out/'analysis-command.json').write_text(json.dumps({'command':command,'exitCode':r.returncode,'stdout':r.stdout,'stderr':r.stderr},indent=2)+'\n');need(r.returncode==0,'strict analysis rejected: failed output preserved')
for name,digest in before.items():need(sha(Path(name))==digest,'input changed during analysis/copy')
for f in archive.iterdir():
 if f.is_file():shutil.copy2(f,a.out/f.name)
shutil.copy2(a.stdout,a.out/'stdout.log');shutil.copy2(a.artifact,a.out/'night-ablation.log')
for rel in ('analyze-night-cli.py','analyze-night.py','analyze-loop.py','corona_controls.py','source-controls/night_plan.hpp','source-controls/stapip_vu1_shared_defines.h'):
 f=cli.parent/rel;dest=a.out/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(f,dest)
for name in ('review','host'):shutil.copy2(path(release[name]),a.out/(name+'-authority.json'))
shutil.copy2(build,a.out/'native-build.log')
if a.environment=='ps2':shutil.copy2(a.raw_stdout,a.out/'ps2client-raw.log');shutil.copy2(a.normalization_proof,a.out/'normalization-proof.json')
for name,digest in before.items():need(sha(Path(name))==digest,'input drift after archive copy')
need(sha(a.out/'night-ablation.log')==before[str(a.artifact)]and sha(a.out/'stdout.log')==before[str(a.stdout)],'raw archive equality')
# Source/assets are verified again after copy to reject concurrent fixture changes.
for name,digest in source['files'].items():need(sha(fixture/name)==digest,'post-copy frozen source drift '+name)
for name,digest in assets.items():need(sha(game/name)==digest,'post-copy asset drift '+name)
record=dict(status='PASS_COMPLETED_NIGHT_SOURCE_NATIVE_CFG_ASSET_BOUND_RUNTIME',environment=a.environment,kind=kind,order=order,joint=joint,restored=restored,sourceManifestSha256=sha(archive/'target-source-manifest.json'),selectedElfSha256=d['elfSha256'],frozenSourcesVerified=501,runtimeAssetsVerified=298,matchingELFSymbolText=True,liveArtifactPath=str(a.artifact),liveArtifactSha256=sha(a.artifact),ownedLaunchRecordSha256=sha(a.launch),processStopped=False,liveArtifactDeleted=False,performanceOrPhysicalGainAccepted=False,normalizationApplied=a.environment=='ps2',authorityPins=authorityPins,helperSha256=sha(Path(__file__)),files={f.relative_to(a.out).as_posix():sha(f)for f in a.out.rglob('*')if f.is_file()});(a.out/'machine-evidence.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf8');print(record['status'],sha(a.out/'machine-evidence.json'))
