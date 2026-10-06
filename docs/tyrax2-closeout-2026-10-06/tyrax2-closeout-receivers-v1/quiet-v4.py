from pathlib import Path
import hashlib,json,shutil
root=Path(__file__).parent;archive=root/'archive-v3';assert not archive.exists();archive.mkdir()
exclude={'bin','obj','native-cache','.res-baked','res','logs','screenshots','preview','authoring','.git','.vscode'}
for name in('game','tyra'):shutil.copytree(root/name,archive/name,ignore=lambda d,n:[x for x in n if x in exclude])
(archive/'game/bin').mkdir()
for name in('vehicle-playground.elf','vehicle-playground.elf.sym','closeout.cfg','closeout-samples.txt','log.txt'):
 f=root/'game/bin'/name
 if f.exists():shutil.copy2(f,archive/'game/bin'/name)
for name in('source-manifest.json','native-proof.json','native-build.log','native-build-v2.log','native-build-v3.log','closeout.hpp'):shutil.copy2(root/name,archive/name)
def edit(p,old,new):
 s=p.read_text(encoding='utf8');assert s.count(old)==1,(p,old,s.count(old));p.write_bytes(s.replace(old,new).encode())
edit(root/'tyra/Makefile.base','-DTYRA_HARDWARE_TRACE=0','-DTYRA_HARDWARE_TRACE=0 -DTYRAX_PRIVATE_QUIET_LOGS=1')
edit(root/'tyra/engine/inc/debug/debug.hpp','#define TYRA_LOG(...) TyraDebug::writeLines("LOG: ", ##__VA_ARGS__, "\\n")','#if TYRAX_PRIVATE_QUIET_LOGS\n// PRIVATE measurement: retain compile-time type checking, no evaluation/emission.\n#define TYRA_LOG(...) do { if (false) TyraDebug::writeLines("LOG: ", ##__VA_ARGS__, "\\n"); } while (0)\n#else\n#define TYRA_LOG(...) TyraDebug::writeLines("LOG: ", ##__VA_ARGS__, "\\n")\n#endif')
edit(root/'tyra/engine/src/renderer/core/paths/path1/vif1_queue.cpp','  if (++frames % 120 == 0) {','  // PRIVATE: keep arena peak/reset/statistics work; suppress only reports/RAM probe.\n  if (++frames % 120 == 0 && !TYRAX_PRIVATE_QUIET_LOGS) {')
edit(root/'game/src/main.cpp','  Tyra::Info::writeLogsToFile = !ps2link;','  Tyra::Info::writeLogsToFile = false;  // PRIVATE: diagnostics stay on stdout, no hot host log file')
edit(root/'game/src/main.cpp','  options.writeLogsToFile = !ps2link;','  options.writeLogsToFile = false;  // PRIVATE same diagnostic destination')
def closing(s,start):
 depth=0
 for i in range(start,len(s)):
  if s[i]=='{':depth+=1
  elif s[i]=='}':
   depth-=1
   if depth==0:return i+1
 raise RuntimeError('unclosed telemetry block')
changes=[]
for name in('game_vehicles.gen.cpp','game_collision.gen.cpp'):
 p=root/'game/src/gen'/name;s=p.read_text(encoding='utf8')
 marker='      static int vehLog = 0;';assert s.count(marker)==1;start=s.index(marker);body=s.index('{',s.index('if (++vehLog >= 25)',start));end=closing(s,body);s=s[:start]+'#if !TYRAX_PRIVATE_QUIET_LOGS\n'+s[start:end]+'\n#endif'+s[end:]
 marker='    // CONTACT TELEMETRY';assert s.count(marker)==1;start=s.index('\n    {',s.index(marker))+1;body=s.index('{',start);end=closing(s,body);s=s[:start]+'#if !TYRAX_PRIVATE_QUIET_LOGS\n'+s[start:end]+'\n#endif'+s[end:];p.write_bytes(s.encode());changes.append(str(p.relative_to(root)))
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();record=dict(status='PRIVATE_QUIET_V4_SOURCE_READY_NATIVE_BUILD',archivedV3=str(archive),priorELFSha256=sha(archive/'game/bin/vehicle-playground.elf'),changes=['tyra/Makefile.base','tyra/engine/inc/debug/debug.hpp','tyra/engine/src/renderer/core/paths/path1/vif1_queue.cpp','game/src/main.cpp']+changes,healthyLogEmissionDisabled=True,vehicleTelemetryAndContactPreparationDisabled=True,arenaReportRamProbeDisabled=True,arenaResetAndMaxStatisticsPreserved=True,assertWarnErrorTrapPreserved=True,publicCommonQuietingCostUnpriced=True,productionEdited=False,devicesExecuted=False)
(root/'quiet-v4-source.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf8');print(record['status'])
