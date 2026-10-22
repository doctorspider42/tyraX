from pathlib import Path
import hashlib,json,subprocess,re
r=Path('/mnt/f/Projects/tyrax2-lab-20261001/tyrax2-render-audit-v1');p=r/'vehicle';engine=r/'tyra/engine';sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
n=json.loads((p/'native-proof.json').read_text());tool=Path('/home/spider/.cache/tyrax/native/toolchains/eff2c8918589264d49aa9a0f/ee/bin');archive=Path(n['actualSourceMirrors']['tyra/engine/Makefile']['actualPath']).parent/'bin/libtyra.a'
members={}
for name,v in n['objects'].items():
 if not name.startswith('engine/'):continue
 obj=Path(v['actualObject']);data=subprocess.check_output([str(tool/'mips64r5900el-ps2-elf-ar'),'p',str(archive),obj.name]);assert hashlib.sha256(data).hexdigest()==v['sha256'];members[name]=v['sha256']
files=['src/renderer/core/paths/path1/vif1_queue.cpp','inc/renderer/core/paths/path1/frame_submission.hpp','src/renderer/core/renderer_core_sync.cpp','src/renderer/core/renderer_core.cpp','src/renderer/core/2d/renderer_core_2d.cpp']
for f in files:
 t=(engine/f).read_text();assert 'debug/loop_balance.hpp' in t
 if 'vif1_queue' in f:assert 'while (running) advanceIfIdle(); }' in t and 'flushRecording();' in t
 if 'renderer_core_sync' in f:assert '  } }\n  clear();' in t
 t0=(r.parent/'tyrax2-city-day-drive-v1/tyra/engine'/f).read_text()
 for call in ['dma_channel_wait(','draw_wait_finish(','graph_wait_vsync(','while (*','while (running)','while (static_cast<s32>(completed']:
  assert t.count(call)==t0.count(call),(f,call)
rc=(engine/files[3]).read_text();assert rc.count('LoopBalance::pace(t0,t1)')==3
defines=(p/'native-build-v1.log').read_text();assert not re.search(r'loop_balance.hpp:\d+:\d+: warning:',defines)
proof=dict(status='ROOT_LEAF_WAIT_AND_ARCHIVE_COVERAGE_REVIEW',archiveMembersMatchNativeObjects=members,sourceFiles={f:sha(engine/f)for f in files},extraCountReads='3 phase markers plus 2 per outer leaf wait in enabled raw loops only; existing entry/end/pacing stamps reused.',waitClosure='Queue waitFor spins, drain spin plus SDK VIF wait, ring backpressure, direct VIF waits, all three VIF FIFO polls, central GIF/VIF DMA adapters, central draw_wait_finish and GS check loop. GS clear/queue flush/arena reset excluded from wait spans. flipBuffers/vsync remain existing pacing stamps.',nesting='Outer-only intervals; mixed kinds or phase crossing invalidate. Strict runtime parser additionally requires zero nesting.',jobOwnership='Pipeline submission records current EE loop, sequence and context. Completion records actual pending owner; never inferred by subtracting one.',omissions='Raytracer VU0 wait is outside this fixture (no raytraced renderer). SDK-internal work/preemption inside wait interval retained. HardwareTrace and FrameProfile compiled out. No added wait, poll, FINISH, DMA drain, controller or physics change.',bufferBytes=15872,commonDisabledFootprintUnpriced=True)
(p/'coverage-review.json').write_text(json.dumps(proof,indent=2)+'\n');print(proof['status'])
