from pathlib import Path
import json,re,hashlib,subprocess
r=Path(__file__).parent;b=r.parent/'tyrax2-render-audit-v2';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
n=json.loads((r/'vehicle/native-proof.json').read_text());assert n['status']=='PASS_PRIVATE_QUEUE_AUDIT_NATIVE_SOURCE_OBJECT_LINK_BINDING'
waitPattern=r'\b(?:dma_channel_wait|draw_wait_finish|waitFor|drain|frameWaitVif|frameWaitGif)\s*\('
changed=[]
for p in (r/'tyra/engine').rglob('*'):
 if not p.is_file()or p.suffix not in ('.cpp','.hpp'):continue
 old=b/'tyra/engine'/p.relative_to(r/'tyra/engine')
 if old.exists()and p.read_bytes()!=old.read_bytes():
  s=p.read_text();t=old.read_text()
  assert len(re.findall(waitPattern,s))==len(re.findall(waitPattern,t)),p
  changed.append(dict(path=p.relative_to(r).as_posix(),sha256=sha(p),parentSha256=sha(old)))
assert {x['path']for x in changed}=={'tyra/engine/inc/debug/render_audit.hpp','tyra/engine/src/renderer/core/paths/path1/vif1_queue.cpp','tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'}
header=(r/'tyra/engine/inc/debug/render_audit.hpp').read_text();assert 'class Stage {public: explicit Stage(StageId){} void finish(){} };'in header and 'class Work {public: explicit Work(WorkId){} void finish(){} };'in header
assert header.count('mfc0')==1 and 'current.reads!=2*current.edges'in header and 'total+wait+pace!=span'in header
assert not re.search(waitPattern,header)
assert n['linkedBuffers']['RenderAudit::samples']['bytes']==17920 and n['linkedBuffers']['RenderAudit::calls']['bytes']==120
assert not any('render_audit' in s for s in n['warningLines'])
# Freeze actual archive-member identity rather than assuming a mirrored source is linked.
em=Path(n['objects']['engine/engine']['actualObject']).parent.parent
ar='/home/spider/.cache/tyrax/native/toolchains/eff2c8918589264d49aa9a0f/ee/bin/mips64r5900el-ps2-elf-ar'
members=subprocess.check_output([ar,'t',str(em/'bin/libtyra.a')],text=True).splitlines();bound={}
for key in ['engine/renderer/core/paths/path1/vif1_queue','engine/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer']:
 obj=n['objects'][key];name=Path(obj['actualObject']).name;assert members.count(name)==1
 payload=subprocess.check_output([ar,'p',str(em/'bin/libtyra.a'),name]);assert hashlib.sha256(payload).hexdigest()==obj['sha256'];bound[key]=obj['sha256']
proof=dict(status='PRIMARY_REVIEWED_SELECTIVE_QUEUE_COVERAGE',nativeProofSha256=sha(r/'vehicle/native-proof.json'),changedEngineSources=changed,linkedArchiveMembers=bound,noAddedWaits=True,denseTags='Stage/Work/count are empty inline APIs; actual queue scope edges are independently counted and parsed.',timedPaths=['sendPacket function including reuse bookkeeping, exclusive of children/waits','Vif1Queue submit remainder, exclusive of children','closeOpenChain callback only after nonnull gate','copyForNativeRecording all counted-copy branches','native sizing fallback only if snapshotCounted is false','native emission loop after capacity admission','flushRecording only after nonzero nativePendingSequence','enqueue including completion callback, excluding covered waits/cache','existing queue FlushCache calls'],counters='Calls at original path gates; copied/borrowed bytes from actual Snapshot result; no extra chain walker.',pacing='Unchanged v2 guard excludes presentation before flip and counts covered GIF calls without leaf clocks.',limits=['Elapsed, not instruction retirement or pure DMA hardware time.','ProducerSend/Submit/Seal values are exclusive remainders. UnobservedRender is all other render work.','Common footprint remains unpriced; calibrate active clocks/counters in both parked orders.'])
(r/'vehicle/queue-coverage-review.json').write_text(json.dumps(proof,indent=2)+'\n');print(proof['status'])
