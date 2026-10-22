"""Bind linked SDK VSync semantics and ordered-frame dependencies, not target timing."""
import argparse,hashlib,json,re,struct
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('fixture',type=Path);p.add_argument('--output',type=Path,required=True)
a=p.parse_args();root=a.fixture
sha=lambda b:hashlib.sha256(b).hexdigest()
binding=json.loads((root/'native-binding.json').read_text())
for rel,digest in binding['files'].items():assert sha((root/rel).read_bytes())==digest,rel
address=int(re.search(r'^([0-9a-f]+) T graph_wait_vsync$',(root/'symbols.txt').read_text(),re.M)[1],16)

def text(path):
 raw=path.read_bytes();assert raw[:6]==b'\x7fELF\x01\x01'
 h=struct.unpack_from('<16sHHIIIIIHHHHHH',raw)
 assert h[1:3]==(2,8) and h[11]==40
 sections=[struct.unpack_from('<10I',raw,h[6]+40*i) for i in range(h[12])]
 names=sections[h[13]];strings=raw[names[4]:names[4]+names[5]]
 selected=[s for s in sections if strings[s[0]:strings.index(0,s[0])]==b'.text']
 assert len(selected)==1
 s=selected[0];assert s[2]&4 and s[3]<=address and address+56<=s[3]+s[5]
 return raw,raw[s[4]:s[4]+s[5]],s[3]

elf,code,base=text(root/'game/bin/vehicle-playground.elf')
sym,symcode,symbase=text(root/'game/bin/vehicle-playground.elf.sym')
assert sha(elf)==binding['elfSha256'] and base==symbase and code==symcode
actual=list(struct.unpack_from('<14I',code,address-base))
expected=[0x3c041200,0x3c031200,0xdc821000,0x30420008,0xdc851000,0x00451025,
 0xfc821000,0,0xdc621000,0x30420008,0x1040fffd,0,0x03e00008,0]
def instructionGate(words):
 assert words==expected,'Linked SDK instruction sequence changed; re-audit, do not assume freshness'
instructionGate(actual)
negatives=[]
for index in (3,5,6,10):
 changed=actual.copy();changed[index]^=1
 try:instructionGate(changed)
 except AssertionError:negatives.append(index)
 else:raise AssertionError('Changed SDK operation accepted')

core=(root/'tyra/engine/src/renderer/core/renderer_core.cpp').read_text()
queue=(root/'tyra/engine/src/renderer/core/paths/path1/vif1_queue.cpp').read_text()
gs=(root/'tyra/engine/src/renderer/core/gs/renderer_core_gs.cpp').read_text()
def function(source,start,end):
 at=source.index(start)
 return source[at:source.index(end,at+len(start))]
complete=function(core,'void RendererCore::completePipelineFrame()','void RendererCore::endFrame()')
flush=function(queue,'void Vif1Queue::flushRecording()','void Vif1Queue::')
assert complete.index('Vif1Queue::waitFor(pipelineSequence)')<complete.index('sync.waitAndClear()')
assert complete.index('sync.waitAndClear()')<complete.index('graph_wait_vsync()')<complete.index('gs.flipBuffers(')
assert flush.index('nativePendingSequence = 0')<flush.index('completeBeforeSubmission()')<flush.index('enqueue(nativeWriter->data(), sequence)')
assert 'frameRecording = false;\n  flushRecording();' in queue
assert 'if (frameRecording) completeBeforeSubmission();' in queue
assert 'if (!pipelineFrameActive) completePipelineFrame();' in core
assert 'emitHybridPresent(1);\n      presentFrameBuffer(1);' in gs
assert 'while (pendingBuffer >= 0) graph_wait_vsync();' in gs

# W1C event latch is not a current-blank status: two histories can expose
# the same readable register while only one permits a display-target copy.
cases=[]
for other in range(16):
 bits=((other&7)|((other&8)<<1))
 for latched in (False,True):
  csr=bits|(8 if latched else 0)
  written=(csr&8)|csr
  after=(csr&31)&~(written&31)
  assert after==0
  cases.append({'csrLowEvents':csr,'writtenLowEvents':written&31,'afterWrite':after,
   'pollWaitsBeforeNewVBlank':True})
assert len(cases)==32
histories=[{'vsint':8,'blankActive':True},{'vsint':8,'blankActive':False}]
assert histories[0]['vsint']==histories[1]['vsint'] and histories[0]['blankActive']!=histories[1]['blankActive']
report={'valid':True,'elfSha256':binding['elfSha256'],'symbolsSha256':sha(sym),
 'auditSha256':sha(Path(__file__).read_bytes()),'graphWaitAddress':hex(address),
 'linkedInstructions':[f'{v:08x}' for v in actual],
 'rejectingInstructionControls':negatives,
 'boundSourceFiles':len(binding['files']),'sdkModelCases':cases,'indistinguishableVsintHistories':histories,
 'dependencies':['EE records N+1 in another bank while N runs.',
  'Before submitting N+1: finish N DMA and exclusive GS FINISH; Hybrid2 waits new VBlank then submits display copy.',
  'Current prefix reservation is cleared before callback; recording/HUD/bank protection remains necessary.',
  'Hybrid3 queues completed unscanned copy for ISR publication; this changes schedule, not EE packet speed.'],
 'limits':['Host register-latch model, not measured VBlank or GS copy duration.',
  'Linked freshness behavior is not itself proof of a bug or avoidable physical missed deadline.',
  'Skipping wait on latched VSINT alone is unsafe; active-blank and remaining copy budget are unproved.',
  'No production change, new target clocks, hardware gain or scene ceiling proof.']}
a.output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'valid':True,'address':hex(address),'sources':len(binding['files']),'cases':len(cases)}))
