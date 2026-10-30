"""Add private baked-arena retirement/request diagnostics to a traffic fixture."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import zipfile

from vehicle_contacts_fixture import replace

HELPERS = r'''
namespace {
struct TrafficRetired { const void* arena=nullptr; u32 bytes=0, frame=0; };
TrafficRetired trafficRetired[1024];
u32 trafficArenaWanted=0,trafficFreed=0;
bool trafficRetiredEnabled(){
  static int enabled=-1;
  if(enabled<0){FILE* f=fopen("traffic-bench-retired.cfg","rb");enabled=f?1:0;if(f)fclose(f);}
  return enabled!=0;
}
u32 trafficCacheBudget(){
  static u32 budget=0;
  if(!budget){
    budget=TYRA_STAPIP_BAKED_BUDGET_QW;
    FILE* f=fopen("traffic-bench-cache-qw.cfg","r");unsigned long wanted=0;
    if(f){if(fscanf(f,"%lu",&wanted)==1 && wanted>=1024 && wanted<=budget)budget=u32(wanted);fclose(f);}
    printf("BKBUDGET qwords=%lu retirement=%d\n",static_cast<unsigned long>(budget),int(trafficRetiredEnabled()));fflush(stdout);
  }
  return budget;
}
void trafficRetire(const void* arena,u32 bytes,u32 frame){
  if(!trafficRetiredEnabled() || !arena || !bytes)return;
  for(auto& r:trafficRetired)if(!r.arena || r.arena==arena){r={arena,bytes,frame};return;}
  printf("BKGARBAGE ERROR ledger_full\n");fflush(stdout);
}
void trafficCollect(const void* arena){
  if(!trafficRetiredEnabled())return;
  for(auto& r:trafficRetired)if(r.arena==arena){trafficFreed+=r.bytes;r={};return;}
}
void trafficRetirementReport(u32 frame,u32 live,size_t entries){
  if(!trafficRetiredEnabled() || frame%8)return;
  u32 bytes=0,blocked=0,count=0,age=0;
  for(const auto& r:trafficRetired)if(r.arena){
    bytes+=r.bytes;++count;age=std::max(age,frame-r.frame);
    if(!Vif1Queue::immutableSpanReady(r.arena))blocked+=r.bytes;
  }
  const struct mallinfo m=mallinfo();
  printf("BKGARBAGE frame=%lu live_qw=%lu entries=%lu retired_bytes=%lu retired_count=%lu blocked_bytes=%lu oldest_age=%lu freed=%lu heap_used=%lu heap_free=%lu\n",
    static_cast<unsigned long>(frame),static_cast<unsigned long>(live),static_cast<unsigned long>(entries),
    static_cast<unsigned long>(bytes),static_cast<unsigned long>(count),static_cast<unsigned long>(blocked),
    static_cast<unsigned long>(age),static_cast<unsigned long>(trafficFreed),
    static_cast<unsigned long>(m.uordblks),static_cast<unsigned long>(m.fordblks));
  trafficFreed=0;fflush(stdout);
}
std::unique_ptr<u8[]> trafficNewArena(u32 bytes){
  trafficArenaWanted=bytes;
  std::unique_ptr<u8[]> result(new u8[bytes]);
  trafficArenaWanted=0;return result;
}
}
extern "C" unsigned tyraTrafficArenaRequest(){return trafficArenaWanted;}
'''


def install(repo, project):
    common = Path(subprocess.check_output([
        'git', '-C', str(repo), 'rev-parse', '--path-format=absolute',
        '--git-common-dir'], text=True).strip())
    assert project.is_relative_to((common.parent/'.tmp').resolve())
    cpp = project.parent/'traffic-engine/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
    vehicles = project/'src/gen/game_vehicles.gen.cpp'
    meta_path = project.parent/'traffic-scaling-metadata.json'
    meta = json.loads(meta_path.read_text(encoding='utf8'))
    assert meta.get('instrumented')
    s = cpp.read_text(encoding='utf8')
    v = vehicles.read_text(encoding='utf8')
    assert 'tyraTrafficArenaRequest' not in s+v
    s = '#include <malloc.h>\n#include <stdio.h>\n'+s
    s = replace(s, 'namespace Tyra {', 'namespace Tyra {\n'+HELPERS)
    before = 'entry->raw = std::unique_ptr<u8[]>(new u8[qw * 16 + 15]);'
    assert s.count(before) == 3
    s = s.replace(before, 'entry->raw = trafficNewArena(qw * 16 + 15);')
    start = s.index('bool StaPipBakedStreams::evictFor(u32 wanted) {')
    end = s.index('\n}\n', start)+2
    part = s[start:end]
    assert part.count('kMaxQwords') == 1
    s = s[:start]+part.replace('kMaxQwords', 'trafficCacheBudget()')+s[end:]
    start = s.index('bool StaPipBakedStreams::reserve(u32 qwords) {')
    end = s.index('\n}\n', start)+2
    part = s[start:end]
    assert part.count('kMaxQwords') == 2
    s = s[:start]+part.replace('kMaxQwords', 'trafficCacheBudget()')+s[end:]
    s = replace(s, '  const u32 qw = item.arenaQw;',
                '  const u32 qw = item.arenaQw;\n  if(item.raw)trafficRetire(item.arena,qw*16,frameNow);')
    start = s.index('void StaPipBakedStreams::onFrameEnd() {')
    end = s.index('\nvoid StaPipBakedStreams::clear()', start)
    part = s[start:end]
    part = replace(part, 'return Vif1Queue::collectRetiredImmutableSpan(item.arena);',
                   'const bool done=Vif1Queue::collectRetiredImmutableSpan(item.arena);\n        if(done)trafficCollect(item.arena);return done;')
    pos = part.rfind('\n}')
    part = part[:pos]+'\n  trafficRetirementReport(frameNow,usedQwords,storage.size());'+part[pos:]
    s = s[:start]+part+s[end:]
    v = 'extern "C" unsigned tyraTrafficArenaRequest();\n'+v
    v = replace(v, r'object=%d\n",phase,frame,allocationStage,allocationOwner);',
                r'object=%d arena_request=%u\n",phase,frame,allocationStage,allocationOwner,tyraTrafficArenaRequest());')
    with zipfile.ZipFile(project.parent/'before-traffic-retirement.zip', 'x', zipfile.ZIP_DEFLATED) as z:
        z.write(cpp, 'stapip_qbuffer_renderer.cpp')
        z.write(vehicles, 'game_vehicles.gen.cpp')
        z.write(meta_path, 'metadata.json')
    cpp.write_bytes(s.encode())
    vehicles.write_bytes(v.encode())
    meta['source_sha256'][str(vehicles.relative_to(project))] = hashlib.sha256(vehicles.read_bytes()).hexdigest()
    engine = project.parent/'traffic-engine'
    meta['engine_source_sha256'][str(cpp.relative_to(engine))] = hashlib.sha256(cpp.read_bytes()).hexdigest()
    meta.update(retirement_probe=True, status='Private retirement probe installed; native build/runtime admission pending.')
    meta_path.write_text(json.dumps(meta, indent=2)+'\n', encoding='utf8')
    print('Installed private retirement ledger/request/budget diagnostics; native build required')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--project', required=True, type=Path)
    p.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
    a = p.parse_args()
    install(a.repo.resolve(), a.project.resolve())
