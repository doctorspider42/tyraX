from pathlib import Path
import json,hashlib,re
r=Path('/mnt/f/Projects/tyrax2-lab-20261001/tyrax2-render-audit-v2');base=r.parent/'tyrax2-loop-balance-v1';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
d=json.loads((r/'vehicle/source-review.json').read_text());changed=[]
for name,h in d['files'].items():
 if not name.endswith(('.cpp','.hpp')) or name.endswith('render_audit.hpp'):continue
 f=r/'vehicle/game'/name[5:]if name.startswith('game/')else r/name;old=base/'vehicle/game'/name[5:]if name.startswith('game/')else base/name
 if sha(f)==sha(old):continue
 t=f.read_text();o=old.read_text();changed.append(name)
 for call in ['dma_channel_wait(','draw_wait_finish(','graph_wait_vsync(','sync.align3D(','while (*','while (running)']:
  assert t.count(call)==o.count(call),(name,call)
rc=(r/'tyra/engine/src/renderer/core/renderer_core.cpp').read_text();assert rc.count('LoopBalance::pacingBegin(t0)')==3
head=(r/'tyra/engine/inc/debug/render_audit.hpp').read_text();assert all(x not in head for x in ['dma_channel','draw_wait','graph_wait','FlushCache'])
assert 'current.reads!=2*current.edges' in head and 'total+wait+pacing!=span' in head
log=(r/'vehicle/native-build-v1.log').read_text();assert not re.search(r'render_audit.hpp:\d+:\d+: warning:',log)
proof=dict(status='ROOT_TWO_AXIS_SOURCE_COVERAGE_REVIEW',changedSources=changed,phaseAnchor='Existing LoopBalance renderStart/gameEnd stamps; independent work and stage sums each reconcile to exclusive render span.',clockCost='2 Count reads for each change of stage/work tag; equal nested tags use zero extra clocks. Wait/pacing clocks shared with existing sampler. Counter writes remain active observer work.',pacingExclusion='Begin before actual flip at original t0, end at original t1; nested instrumented functions cannot backdate accounting.',stageLabels='20 explicit bins, including root gaps and scene preparation. Labels locate execution; interleaved submissions may be charged to later stages.',workLabels='11 explicit bins including render-other, Core prep, bounds, dispatch, geometry copies, EE clipping, texture, object state, packet build, submission and cache flush.',counterLimits='Input/event counts only. Texture-chain QWs omit REF payload and submit-source QWs are not bus bytes. Effect stage calls include inactive gate checks.',legacyRenderCostCapture='Rejected when armed in measured raw loops; ordinary render-cost request remains zero. No per-object fairness drains enabled.',newBufferBytes=24064,commonFootprintUnpriced=True)
(r/'vehicle/render-coverage-review.json').write_text(json.dumps(proof,indent=2)+'\n');print(proof['status'])
