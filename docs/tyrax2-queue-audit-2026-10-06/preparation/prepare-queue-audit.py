from pathlib import Path
import shutil,json,hashlib
lab=Path(__file__).parent;base=lab/'tyrax2-render-audit-v2';root=lab/'tyrax2-queue-audit-v1'
assert not root.exists();root.mkdir()
ignore=shutil.ignore_patterns('obj','*.elf','*.sym','*.log','closeout-samples.txt','ps2link.run','source-review.json','native-proof.json','coverage-review.json','render-coverage-review.json','v2-correction-review.json')
shutil.copytree(base/'tyra',root/'tyra',ignore=ignore)
shutil.copytree(base/'vehicle/game',root/'vehicle/game',ignore=ignore)
for src in base.glob('*.py'):
 (root/src.name).write_bytes(src.read_text().replace('tyrax2-render-audit-v2','tyrax2-queue-audit-v1').replace('tyrax2-closeout-ps2-render-audit-v1','tyrax2-closeout-ps2-queue-audit-v1').encode())
def edit(rel,fn):
 p=root/rel;s=p.read_text();p.write_bytes(fn(s).encode())
def swap(s,a,b,n=1):
 assert s.count(a)==n,(a,s.count(a));return s.replace(a,b)
shutil.copyfile(lab/'queue-audit-header.hpp',root/'tyra/engine/inc/debug/render_audit.hpp')
q='tyra/engine/src/renderer/core/paths/path1/vif1_queue.cpp'
def queue(s):
 s=swap(s,'  auto closer = openChainCloser;','  RenderAudit::QueueScope auditClose(RenderAudit::CloseChain);\n  RenderAudit::event(RenderAudit::Closers);\n  auto closer = openChainCloser;')
 s=swap(s,'void prepareArena() {','void prepareArena() {\n  RenderAudit::event(RenderAudit::Preparations);')
 s=swap(s,'  if ((TYRA_NATIVE_VIF_RECORD || frameRecording) && nativeWriter && sourceCopied) {','  RenderAudit::QueueScope auditSnapshot(RenderAudit::Snapshot);\n  RenderAudit::event(RenderAudit::Snapshots);\n  if ((TYRA_NATIVE_VIF_RECORD || frameRecording) && nativeWriter && sourceCopied) {')
 s=swap(s,'if (snapshot.chain) { records = snapshot.records; counted = true; }','if (snapshot.chain) { records = snapshot.records; counted = true; RenderAudit::event(RenderAudit::CountedSnapshots); RenderAudit::event(RenderAudit::CopiedBytes,snapshot.bytes); RenderAudit::event(RenderAudit::BorrowedBytes,snapshot.borrowedBytes); } else RenderAudit::event(RenderAudit::SnapshotFailures);')
 s=swap(s,'  return chainArena->copy(chain, qwords, resolveChainRef, nullptr,\n                         reinterpret_cast<u32>(arenaMemory), isImmutableSpan);','  auto snapshot = chainArena->copy(chain, qwords, resolveChainRef, nullptr,\n                         reinterpret_cast<u32>(arenaMemory), isImmutableSpan);\n  if(snapshot.chain){RenderAudit::event(RenderAudit::CopiedBytes,snapshot.bytes);RenderAudit::event(RenderAudit::BorrowedBytes,snapshot.borrowedBytes);}else RenderAudit::event(RenderAudit::SnapshotFailures);\n  return snapshot;')
 s=swap(s,'u32 Vif1Queue::submit(const void* chain, u32 qwords, bool* sourceCopied) {','u32 Vif1Queue::submit(const void* chain, u32 qwords, bool* sourceCopied) {\n  RenderAudit::QueueScope auditSubmit(RenderAudit::SubmitRemainder);\n  RenderAudit::event(RenderAudit::Submits);RenderAudit::event(RenderAudit::SourceQw,qwords);')
 s=swap(s,'    if (!snapshot.chain && chainArena->used()) {','    if (!snapshot.chain && chainArena->used()) {\n      RenderAudit::event(RenderAudit::SnapshotRetries);')
 s=swap(s,'    { HardwareTrace::Scope trace("NativeSizing", HardwareTrace::Kind::Span, true);','    { RenderAudit::QueueScope auditSizing(RenderAudit::Sizing);RenderAudit::event(RenderAudit::SizingLoops);\n      HardwareTrace::Scope trace("NativeSizing", HardwareTrace::Kind::Span, true);')
 s=swap(s,'      HardwareTrace::Scope traceEmit("NativeEmission", HardwareTrace::Kind::Span, true);','      RenderAudit::QueueScope auditEmission(RenderAudit::Emission);RenderAudit::event(RenderAudit::EmissionCalls);RenderAudit::event(RenderAudit::EmissionRecords,records);\n      HardwareTrace::Scope traceEmit("NativeEmission", HardwareTrace::Kind::Span, true);')
 s=swap(s,'if (recorded) { nativePendingSequence = sequence; return sequence; }','if (recorded) { nativePendingSequence = sequence; RenderAudit::event(RenderAudit::RecordedReturns); return sequence; }')
 s=swap(s,'void Vif1Queue::enqueue(const void* chain, u32 sequence) {','void Vif1Queue::enqueue(const void* chain, u32 sequence) {\n  RenderAudit::QueueScope auditEnqueue(RenderAudit::Enqueue);RenderAudit::event(RenderAudit::Enqueues);')
 s=swap(s,'  if (!nativePendingSequence) return;','  if (!nativePendingSequence) return;\n  RenderAudit::QueueScope auditSeal(RenderAudit::Seal);RenderAudit::event(RenderAudit::Flushes);')
 s=swap(s,'bool Vif1Queue::recordGif(const packet2_t* packet) {','bool Vif1Queue::recordGif(const packet2_t* packet) {\n  RenderAudit::event(RenderAudit::GifCalls);')
 s=s.replace('RenderAudit::Work auditCache(RenderAudit::Cache);','RenderAudit::QueueScope auditCache(RenderAudit::Writeback);')
 return s
edit(q,queue)
edit('tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp',lambda s:swap(s,'void StaPipQBufferRenderer::sendPacket() {','void StaPipQBufferRenderer::sendPacket() {\n  RenderAudit::QueueScope auditSend(RenderAudit::ProducerSend);RenderAudit::event(RenderAudit::Sends);'))
# The parent proofs bind the same original input universe plus these modified files.
pin=json.loads((base/'vehicle/source-review.json').read_text())
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for rel in pin['files']:
 p=root/'vehicle/game'/rel[5:]if rel.startswith('game/')else root/rel
 pin['files'][rel]=sha(p)
pin['status']='PRIVATE_SELECTIVE_QUEUE_AUDIT_SOURCE_FROZEN';pin['parentRenderELFSha256']='d686eda2e78d488fd3c7dada64821a6663825c2f28aa566776150de245c16b00'
(root/'vehicle/source-review.json').write_bytes((json.dumps(pin,indent=2)+'\n').encode())
edit('prove-native.py',lambda s:swap(s,"('RenderAudit::samples',24064),('RenderAudit::current',188),('RenderAudit::stageCalls',240),('RenderAudit::workCalls',132)","('RenderAudit::samples',18944),('RenderAudit::current',148),('RenderAudit::calls',120)").replace('PASS_PRIVATE_RENDER_AUDIT_NATIVE','PASS_PRIVATE_QUEUE_AUDIT_NATIVE'))
edit('root-release.py',lambda s:s.replace('PASS_PRIVATE_RENDER_AUDIT_NATIVE','PASS_PRIVATE_QUEUE_AUDIT_NATIVE'))
edit('analyze-day-drive.py',lambda s:s.replace('RENDER|RENDERCALLS','RENDER|RENDERCALLS|QUEUE|QUEUECALLS'))
print('PREPARED_SELECTIVE_QUEUE_AUDIT',root)
