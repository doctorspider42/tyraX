"""Run in WSL: bind current source/mirror/dependencies/objects and final ELF. No build/device."""
from pathlib import Path
import hashlib,json,re,subprocess
root=Path('/mnt/f/Projects/tyrax2-lab-20261001/tyrax2-closeout-receivers-v1');game=root/'game';engine=root/'tyra/engine'
gm=Path('/home/spider/.cache/tyrax/native/projects/a7d97586b2c6d84f1a8a7ea3');em=Path('/home/spider/.cache/tyrax/native/engines/6b2836217bebd236c5d83bf1/tyra/engine');sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
log=root/'native-build-v4.log';assert log.is_file();text=log.read_bytes().decode('utf-16' if log.read_bytes().startswith(b'\xff\xfe') else 'utf8',errors='replace');assert not re.search(r'error:',text); warnings=[line for line in text.splitlines() if 'warning:' in line]; assert len(warnings)==12 and all('stapip_qbuffer.cpp:' in line and '[-Wclass-memaccess]' in line for line in warnings),warnings;assert 'obj/terrain_game.o' in text
files={}
for folder in ('game/src','game/inc','tyra/engine/src','tyra/engine/inc'):
 for f in(root/folder).rglob('*'):
  if f.is_file():files[f.relative_to(root).as_posix()]=sha(f)
for name in('game/vehicle-playground.tyra','game/Makefile','tyra/engine/Makefile','tyra/Makefile.base'):files[name]=sha(root/name)
manifest=dict(status='PRIVATE_CURRENT_HEAD_CLOSEOUT_SOURCE_READY_ROOT_REVIEW',head='5b6cd898b05fbbdcc11466bfc8a1911a9c285d4b',frozen=False,files=files,trialKinds={'0':'All/Players/All or reverse','1':'All fixed, sampler Off/On/Off or reverse','2':'Players fixed, eleven dressing visible/hidden/visible or reverse'},privateQuietTelemetryRemovalUnpriced=True,healthyLoggingMuted=True,publicCommonCostUnpriced=True,compiledOutTraceFootprintUnpriced=True)
(root/'source-manifest-v4.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf8')
mirrors={}
for n,d in files.items():
 mirror=gm/n[5:]if n.startswith('game/')else em/n[len('tyra/engine/'):]if n.startswith('tyra/engine/')else em.parent/'Makefile.base'
 assert mirror.is_file()and sha(mirror)==d,n;mirrors[n]=dict(actualPath=str(mirror),sha256=d)
objects={}
for name,base,required in [('game/terrain_game',gm,['inc/scene_data.hpp','inc/closeout_geometry.hpp',str(em/'inc/debug/closeout.hpp'),str(em/'inc/debug/night_sampler.hpp')]),('engine/engine',em,['inc/debug/closeout.hpp','inc/debug/night_sampler.hpp']),('engine/renderer/core/renderer_core',em,['inc/debug/night_sampler.hpp']),('engine/renderer/core/paths/path1/vif1_queue',em,['src/renderer/core/paths/path1/vif1_queue.cpp','inc/info/info.hpp']),('game/gen/game_vehicles.gen',gm,['inc/game_runtime.gen.hpp',str(em/'inc/debug/debug.hpp')])]:
 rel=name.split('/',1)[1];obj=base/('obj/'+rel+'.o');dep=base/('obj/'+rel+'.d');d=dep.read_text(encoding='utf8');assert obj.is_file()and all(x in d for x in required),(name,required,d)
 objects[name]=dict(actualObject=str(obj),objectSha256=sha(obj),actualDependency=str(dep),dependencySha256=sha(dep),requiredDependencies=required)
elf=game/'bin/vehicle-playground.elf';sym=Path(str(elf)+'.sym');assert elf.is_file()and sym.is_file();assert sha(elf)==sha(gm/'bin/vehicle-playground.elf')and sha(sym)==sha(gm/'bin/vehicle-playground.elf.sym')
tool='/home/spider/.cache/tyrax/native/toolchains/eff2c8918589264d49aa9a0f/ee/bin';nm=subprocess.check_output([tool+'/mips64r5900el-ps2-elf-nm','-S','-C',str(sym)],text=True);(root/'actual-symbols-v4.txt').write_text(nm,encoding='utf8');buffers={}
for name,size in [('NightSampler::samples',6144),('NightSampler::chunks',420)]:
 rows=[r for r in nm.splitlines()if r.endswith(' '+name)];assert len(rows)==1,rows;tokens=rows[0].split();assert int(tokens[1],16)==size;buffers[name]=dict(address=int(tokens[0],16),bytes=size,type=tokens[2])
proof=dict(status='PASS_PRIVATE_CURRENT_HEAD_CLOSEOUT_ACTUAL_SOURCE_OBJECT_LINK_BINDING',sourceManifestSha256=sha(root/'source-manifest-v4.json'),nativeBuildLogSha256=sha(log),actualELF=str(elf),actualELFSha256=sha(elf),actualSymbol=str(sym),actualSymbolSha256=sha(sym),actualSourceMirrors=mirrors,objects=objects,engineArchiveSha256=sha(em/'bin/libtyra.a'),linkedBuffers=buffers,compiler=tool+'/mips64r5900el-ps2-elf-g++',samplerSourceSha256=sha(engine/'inc/debug/night_sampler.hpp'),noNewBuildWarnings=True,inheritedWarnings=warnings,rootIndependentReviewPending=True,runtimeReleased=False,deviceExecuted=False,hardwarePerformanceQualified=False)
(root/'native-proof-v4.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf8');print(proof['status']);print(proof['actualELFSha256'])
