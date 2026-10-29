"""Instrument an existing private --micro-comparison fixture for overlap diagnosis.

Run AFTER collision_stress.py instrument and BEFORE native build. This diagnostic
logs every physics call, including warm-up; do not use its FPS as a benchmark.
"""
from pathlib import Path
import argparse, json, subprocess
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--scratch',required=True)
args=parser.parse_args()
dst=Path(args.scratch).resolve()
repo=Path(__file__).resolve().parents[2]
common=Path(subprocess.check_output(['git','-C',str(repo),'rev-parse','--path-format=absolute','--git-common-dir'],text=True).strip())
allowed=(common.parent/'.tmp').resolve()
if not dst.is_relative_to(allowed) or dst==allowed:raise SystemExit('Use a fixture under primary checkout .tmp/')
meta=json.loads((dst/'metadata.json').read_text())
if not meta.get('micro_comparison'):raise SystemExit('Requires --micro-comparison fixture')
h=dst/'engine/engine/inc/debug/collision_bench.hpp'
if 'DIAG MONITOR' in h.read_text():raise SystemExit('Already instrumented')
s=h.read_text()
s=s.replace('constexpr int warm = 150,','constexpr int warm = 0,').replace('park = TYRA_TEMPORAL_SETTLE?600:120','park = 60').replace('drive = TYRA_TEMPORAL_SETTLE?60:360','drive = 0').replace('settle = TYRA_TEMPORAL_SETTLE?600:120','settle = 0')
s=s.replace('phases = TYRA_TEMPORAL_SETTLE ? TYRA_TEMPORAL_ARMS*2 : TYRA_TEMPORAL_COMPARISON ? TYRA_TEMPORAL_ARMS*14 : TYRA_SOLVER_COMPARISON ? 42 : 28','phases = 12')
s=s.replace('inline void configure() {','inline void configure() {\n solverArm=phase%2;baseline=solverArm==0;added=(phase/2)%2?128:64;layout=phase/4;repeat=0;return;')
s=s.replace('struct PhysicsScope {', '''inline U fpFlags=0,vuFlags=0;
inline U readFp(){U f;asm volatile("cfc1 %0,$31":"=r"(f)::"memory");return f;}
inline void writeFp(U f){asm volatile("ctc1 %0,$31\\n nop\\n nop"::"r"(f):"memory");}
inline void checkFp(){
 U saved=readFp();float one=1,zero=0,out;
 writeFp(saved&~0x3c0078u);
 asm volatile("div.s %0,%1,%2":"=f"(out):"f"(one),"f"(zero):"memory");
 U division=readFp();writeFp(saved&~0x3c0078u);
 asm volatile("div.s %0,%1,%1":"=f"(out):"f"(zero):"memory");
 U invalid=readFp();writeFp(saved);
 printf("DIAG MONITOR div=%08x invalid=%08x pass=%d\\n",division,invalid,!!(division&32)&&!!(invalid&64));fflush(stdout);
}
struct PhysicsScope {''')
s=s.replace('PhysicsScope() : start(ticks()) {}','PhysicsScope() : start(ticks()) {writeFp(readFp()&~0x3c0078u);asm volatile("ctc2 $zero,$16\\n vnop\\n vnop":::"memory");}')
s=s.replace('~PhysicsScope() { phys += ticks() - start; }','''~PhysicsScope() { phys += ticks() - start; U fp=readFp(),vu;asm volatile("cfc2 %0,$16":"=r"(vu)::"memory");fpFlags|=fp;vuFlags|=vu;if(ready&&!finished)printf("DIAG p=%d f=%d us=%u contacts=%u generated=%u depth=%.9g energy=%.9g fp=%08x vu=%08x\\n",phase,frame,unsigned(phys/294.912),contacts,generated,maxDepth,energy,fp,vu); }''')
h.write_text(s)
p=dst/'district/src/gen/collision_baseline.gen.cpp';s=p.read_text();s=s.replace('checked=true;FILE*','checked=true;CB::checkFp();FILE*').replace('if(Tyra::vu0SatVerify && !TYRA_TEMPORAL_SETTLE)CB::phase=12;','').replace('((Tyra::vu0SatVerify && !TYRA_TEMPORAL_SETTLE)?14:CB::phases)','CB::phases').replace('CB::phase==0?-300:0','0')
needle='o.data.position[0]=x;'
s=s.replace(needle,'if(CB::layout==1){x=-1.5F+float(index%4)*.4F;z=-48.0F+float((index/4)%4)*.4F;y=.45F+float(index/16)*.4F;} if(CB::layout==2){x=0;z=-48;y=3;}\n   '+needle)
p.write_text(s)
meta.update(overlap_diagnostic=True,phases=12,frames_per_phase=60,layouts=['spread','spacing_0.4','identical_centres'],counts=[64,128],all_physics_calls_logged=True,fps_comparable=False)
(dst/'metadata.json').write_text(json.dumps(meta,indent=2))
# Resident-IOP marker is deliberately left to the deploy step.
(dst/'district/bin/sat-verify.cfg').write_text('verify')
print('Instrumented overlap diagnostics:', dst)
