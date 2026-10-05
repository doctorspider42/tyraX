from pathlib import Path
import json,hashlib,subprocess
lab=Path('F:/Projects/tyrax2-lab-20261001');sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
out=lab/'wild-pool2-ee-static-matrix-root-closure-v1';assert not out.exists();out.mkdir();captures=[];pairs=[];micro=set();pins={}
def read(p):pins[str(p)]=sha(p);return json.loads(p.read_text(encoding='utf-8-sig'))
native=read(lab/'wild-pool2-ee-probe-native-root-review-v1/proof.json');mf=lab/'wild-pool2-ee-probe-physical-v1/target-source-manifest.json';halt=native['diagnosticExports']['pool2ProbeHalt']['address'];helper=lab/'wild-pool2-ee-probe-runtime-v1/decode-vu1-output.py'
for n,label in [(96,'a96'),(192,'b192')]:
 for repeat in(1,2,3):
  folders=[]
  for arm in(0,1):
   folder=lab/f'pool2-ee-probe-{label}-arm{arm}-repeat{repeat}-20261005-v1-launch';folders.append(folder)
   d=read(folder/'owned-launch.json');c=read(folder/'completed-probe-proof.json');v=read(folder/'verified-capture.json');r=read(folder/'capture-report.json');stop=read(folder/'owned-stop.json')
   assert c['status']=='PASS_SOURCE_NATIVE_OWNED_FIXED_CASE_PROTOCOL_AND_PAUSED_CAPTURE_BOUND' and v['status']=='PASS_OFFLINE_SINGLE_PAUSED_STATIC_PROBE_CAPTURE_ONLY'
   assert r['pineStatusBefore']==r['pineStatusAfter']==1 and v['registers']['pc']==halt and v['registers']['isDelaySlot']==0 and v['readyMarker']=='504f4f32'
   assert(c['vertices'],c['arm'],c['repeats'])==(n,arm,repeat)
   assert c['sourceManifestSha256']==sha(mf) and c['elfSha256']==native['actualELFSha256'] and c['nativeProofSha256']==sha(lab/'wild-pool2-ee-probe-native-root-review-v1/proof.json')
   assert c['stateSha256']==sha(folder/'state.p2s')==r['stateSha256'] and c['validatedCaptureSha256']==sha(folder/'verified-capture.json')
   assert stop['pid']==d['pid'] and stop['completedProofSha256']==sha(folder/'completed-probe-proof.json')
   assert c['warmFreshnessIndependentEpoch'] and c['finalColorEpoch']==repeat and c['protocol']['countersDisabled'] and c['protocol']['coldOracleDisabled']
   for name in('state.p2s','emulator.log','raster.png','PCSX2-after.ini','pool2-probe.cfg'):pins[str(folder/name)]=sha(folder/name)
   micro.add(v['actualMicrocodeSha256']);captures.append({'vertices':n,'arm':arm,'repeats':repeat,'completedProofSha256':sha(folder/'completed-probe-proof.json'),'stateSha256':sha(folder/'state.p2s'),'actualMicrocodeSha256':v['actualMicrocodeSha256'],'protocol':c['protocol']})
  ws=lambda p:'/mnt/f/Projects/tyrax2-lab-20261001/'+p.relative_to(lab).as_posix()
  pair=out/f'{label}-repeat{repeat}-pair.json';cmd=['wsl','-d','Ubuntu','--','python3',ws(helper),'--baseline',ws(folders[0]/'state.p2s'),'--table',ws(folders[1]/'state.p2s'),'--case',label.upper(),'--repeats',str(repeat),'--savestates','--out',ws(pair)]
  result=subprocess.run(cmd,capture_output=True,text=True);assert result.returncode==0,(result.stdout,result.stderr)
  p=read(pair);assert p['status']=='PASS_TESTED_ACTUAL_PACKED_VU_PAYLOAD_BYTE_EQUAL_ONLY' and p['finalEpochRGBAValidated'] and p['repeats']==repeat
  assert sorted(x['count']for x in p['packages'])==([21,75]if n==96 else[42,75]);pairs.append(p)
assert len(captures)==12 and len(pairs)==6 and len(micro)==1
pins[str(Path(__file__))]=sha(Path(__file__))
r={'status':'ROOT_CLOSED_TWELVE_EE_LAZY_EPOCH_CAPTURES_SIX_PACKED_OUTPUT_PAIRS','captures':captures,'pairs':pairs,'inputPins':pins,'actualMicrocodeSha256':next(iter(micro)),'finalEpochRGBAValidated':True,'insideLazyBackingNotMaterializedTargetSelfChecks':True,'nativeSixtyFPSAccepted':False,'productionPromotionAccepted':False,'limits':['Fixed inside96/192 cases only; clipped output not accepted.','B192 first75 overwritten; finalcrossing75/tail42 only.','Final requestedepoch1/2/3 actuallydecoded; earlieriterations protocolbound only; changingepochforcesreplaymiss, stablewarmhitnotproven.','SaveState/MTVUoff correctness observer distinctfromphysicalpricing; no hardware rounding orGSraster universal equality.']}
(out/'proof.json').write_bytes((json.dumps(r,indent=2)+'\n').encode());print(r['status'],sha(out/'proof.json'))
