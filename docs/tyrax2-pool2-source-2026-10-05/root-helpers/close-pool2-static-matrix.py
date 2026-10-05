"""Root offline twelve-capture/six-pair closure; no device or source mutations."""
from pathlib import Path
import hashlib, json
lab=Path('F:/Projects/tyrax2-lab-20261001');out=lab/'wild-pool2-static-matrix-root-closure-v1';assert not out.exists();out.mkdir()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();pins={};captures=[];pairs=[];micro=set()
def read(p):pins[str(p)]=sha(p);return json.loads(p.read_text(encoding='utf-8-sig'))
for n,label in [(96,'a96'),(192,'b192')]:
 for repeat in (1,2,3):
  for arm in (0,1):
   folder=lab/f'pool2-static-{label}-arm{arm}-repeat{repeat}-20261005-launch'
   d=read(folder/'owned-launch.json');c=read(folder/'completed-probe-proof.json');v=read(folder/'verified-capture.json');r=read(folder/'capture-report.json')
   assert c['status']=='PASS_SOURCE_NATIVE_OWNED_FIXED_CASE_PROTOCOL_AND_PAUSED_CAPTURE_BOUND'
   assert v['status']=='PASS_OFFLINE_SINGLE_PAUSED_STATIC_PROBE_CAPTURE_ONLY'
   assert r['status']=='PASS_exact_tag_paused_state_decode_text_identity' and r['pineStatusBefore']==r['pineStatusAfter']==1
   assert v['registers']['pc']==0x1a27f8 and v['registers']['isDelaySlot']==0 and v['readyMarker']=='504f4f32'
   assert (c['vertices'],c['arm'],c['repeats'])==(n,arm,repeat)
   assert c['sourceManifestSha256']=='c15e21ad8e37f3a623dce586854156da2644fdf46811260a7384ff6621abe2bb'
   assert c['elfSha256']=='8773444093a7e46481fa3cf905e3b2a4e8c331cf292c2ac5c0f727b6f67e0159'
   assert c['nativeProofSha256']=='b609ee458adb5fa11074949c0bb44206253717088d8463f6d4a0c5ab938e42b2'
   assert c['stateSha256']==sha(folder/'state.p2s')==r['stateSha256']
   assert c['validatedCaptureSha256']==sha(folder/'verified-capture.json') and c['captureReportSha256']==sha(folder/'capture-report.json')
   assert sha(folder/'PCSX2-after.ini')==v['files'][d['ini']]
   assert sha(folder/'pool2-probe.cfg')==d['cfgSha256'] and (folder/'pool2-probe.cfg').read_text().split()==[str(arm),str(n),str(repeat)]
   stop=read(folder/'owned-stop.json');assert stop['pid']==d['pid'] and stop['completedProofSha256']==sha(folder/'completed-probe-proof.json')
   for name in ('state.p2s','emulator.log','raster.png','PCSX2-after.ini','pool2-probe.cfg'):
    pins[str(folder/name)]=sha(folder/name)
   micro.add(v['actualMicrocodeSha256'])
   repairs=[]
   for name in ('root-ui-repair-record.json','modal-recovery-record.json','owned-stop-second-signal.json','owned-stale-socket-cleanup.json'):
    if (folder/name).exists():read(folder/name);repairs.append(name)
   captures.append(dict(case=label.upper(),arm=arm,repeats=repeat,completedProofSha256=sha(folder/'completed-probe-proof.json'),
    stateSha256=sha(folder/'state.p2s'),actualMicrocodeSha256=v['actualMicrocodeSha256'],finalPackageCounts=v['finalPackageCounts'],repairs=repairs))
  p=lab/f'pool2-static-{label}-repeat{repeat}-pair.json';pair=read(p)
  assert pair['status']=='PASS_TESTED_ACTUAL_PACKED_VU_PAYLOAD_BYTE_EQUAL_ONLY' and pair['case']==label.upper()
  assert sorted(x['count']for x in pair['packages'])==([21,75]if n==96 else [42,75])
  assert pair['first75Overwritten']==(n==192) and len(set(pair['microcodeSha256']))==1
  for arm in (0,1):
   file=lab/f'pool2-static-{label}-arm{arm}-repeat{repeat}-20261005-launch/state.p2s'
   key='/mnt/f/Projects/tyrax2-lab-20261001/'+file.relative_to(lab).as_posix()
   assert pair['inputFiles'][key]==sha(file)
  pairs.append(dict(case=pair['case'],repeats=repeat,proofSha256=sha(p),packages=pair['packages'],
   outputQwords=sum(x['outputQwords']for x in pair['packages']),headerQwords=sum(x['stateQwords']for x in pair['packages']),first75Overwritten=pair['first75Overwritten']))
assert len(captures)==12 and len(pairs)==6 and len(micro)==1
read(lab/'wild-pool2-static-probe-native-review-v1/proof.json');read(lab/'wild-pool2-static-probe-native-review-v1-confirmation/proof.json')
pins[str(Path(__file__))]=sha(Path(__file__))
result=dict(status='ROOT_CLOSED_TWELVE_OWNED_STATIC_CAPTURES_SIX_ACTUAL_PACKED_VU_EQUAL_PAIRS',
 diagnosticSourceFiles=500,captures=captures,pairs=pairs,totalComparedOutputQwords=sum(x['outputQwords']for x in pairs),
 totalComparedHeaderQwords=sum(x['headerQwords']for x in pairs),microcodeSha256=list(micro)[0],inputPins=pins,
 pausedCompletionReadyTextIdentityPassed=True,fixedCasePackedEqualityAccepted=True,
 warmIndependentExecutionEpoch=False,universalGSOrHardwareEqualityAccepted=False,performanceAccepted=False,
 limits=['Static identical final bank payload has no per-iteration epoch. Repeated source calls/protocol and final completion are bound; fresh warm VU execution is not independently timestamped.',
 'B192 final crossing75 and tail42 are retained; the first75 was overwritten. It is not accepted from this snapshot.',
 'Packed STQ/RGBAQ/XYZF2/ADC and GIF header equality applies to these fixed inputs only; GS consumption and all floating boundaries remain outside this proof.',
 'Initial A96 baseline required an observed breakpoint checkbox repair after READY; B192 baseline required dismissing host SDL audio error modal. Later profiles use host Null output, with the same guest ELF.',
 'SaveState drains and host output configuration are separate correctness observers, not physical performance data.'])
(out/'proof.json').write_bytes((json.dumps(result,indent=2)+'\n').encode());print(result['status'],sha(out/'proof.json'));print('outputQwords',result['totalComparedOutputQwords'],'headerQwords',result['totalComparedHeaderQwords'])
