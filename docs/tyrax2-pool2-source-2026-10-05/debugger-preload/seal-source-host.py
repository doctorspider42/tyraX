from pathlib import Path
import json,hashlib,importlib.util,struct
P=Path(__file__).parent;L=P.parent;sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
out=P/'proof.json';assert not out.exists()
root=L/'pcsx2-control-routes/source-all';manifest=L/'pcsx2-control-routes/complete-core-source-manifest.json';m=json.loads(manifest.read_text())
assert m['commit']=='94d86c891b1621c0b252e4fc2e155bf90274dcc0'
paths=['pcsx2-qt/Debugger/DebuggerSettingsManager.cpp','pcsx2-qt/Debugger/Breakpoints/BreakpointModel.cpp','pcsx2-qt/Debugger/Breakpoints/BreakpointModel.h','pcsx2/DebugTools/Breakpoints.h','pcsx2/VMManager.cpp','pcsx2/Elfheader.cpp','pcsx2/CDVD/CDVD.cpp','pcsx2/Pcsx2Config.cpp','pcsx2-qt/QtHost.cpp']
for rel in paths:assert sha(root/rel)==m['files'][rel]
sp=importlib.util.spec_from_file_location('prepare',P/'prepare-preload.py');h=importlib.util.module_from_spec(sp);sp.loader.exec_module(h)
checks=[];rejects=[]
assert h.crc(struct.pack('<III',1,2,3)+b'XYZ')==0;checks.append('XOR_full_words_ignore_trailing_bytes')
assert h.crc(struct.pack('<II',0x12345678,0x87654321))==0x95511559;checks.append('XOR_not_crc32')
for pc in(0x110000,0x1ad758):
 row=h.row(pc);assert len(row)==8 and all(isinstance(x,str)for x in row.values())and row['TYPE']=='8'and row['X']=='1'and row['CONDITION']=='';checks.append('schema_'+hex(pc))
for pc in(0,3,-4,0x100000000):
 try:h.row(pc)
 except ValueError:rejects.append(pc)
 else:raise AssertionError(pc)
old=L/'pcsx2-ee-execution-map-v1/bin/vehicle-playground.elf';historical=h.crc(old.read_bytes());assert historical==0x5a2013f2;checks.append('historical_actual_elf_crc_matches_owned_profile_filename')
r=dict(status='PASS_EXACT_TAG_SOURCE_PRELOAD_FORMAT_AND_OFFLINE_CRC_SCHEMA_CONTROLS_NOT_RUNTIME',sourceManifestSha256=sha(manifest),sourceCommit=m['commit'],sourcePins={str(root/rel):sha(root/rel)for rel in paths},
 hostPositiveControls=checks,hostNegativeControls=rejects,historicalElfCRC=dict(path=str(old),sha256=sha(old),computed='5A2013F2',newProbeRuntimeEvidence=False),
 files={str(p):sha(p)for p in [P/'prepare-preload.py',P/'REPORT.md',Path(__file__)]},profileWritten=False,UIOrPineOrDeviceInvoked=False,
 actualDiagnosticPayloadOrBreakpointLoadOrPauseAccepted=False)
out.write_text(json.dumps(r,indent=2)+'\n',encoding='utf8');print(sha(out))
