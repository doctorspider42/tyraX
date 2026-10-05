from pathlib import Path
import json,hashlib,xml.etree.ElementTree as ET
P=Path(__file__).parent;L=P.parent;sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
out=P/'resume-source-proof.json';assert not out.exists()
R=L/'pcsx2-control-routes/source-all';M=L/'pcsx2-control-routes/complete-core-source-manifest.json';m=json.loads(M.read_text())
paths=['pcsx2-qt/Debugger/DebuggerWindow.ui','pcsx2-qt/Debugger/DebuggerWindow.cpp','pcsx2/SIO/Pad/Pad.cpp','pcsx2/Hotkeys.cpp','pcsx2-qt/DisplayWidget.cpp']
for rel in paths:assert sha(R/rel)==m['files'][rel]
xml=ET.parse(R/paths[0]);run=xml.find(".//action[@name='actionRun']");assert run is not None and run.find("property[@name='shortcut']")is None
assert run.find("property[@name='text']/string").text=='Run'
cpp=(R/paths[1]).read_text();assert 'm_ui.actionRun, &QAction::triggered, this, &DebuggerWindow::onRunPause'in cpp
assert 'g_emu_thread->setVMPaused(!QtHost::IsVMPaused());'in cpp
pad=(R/paths[2]).read_text();assert '"TogglePause", "Keyboard/Space"'in pad
hot=(R/paths[3]).read_text();assert 'VMManager::SetPaused(VMManager::GetState() != VMState::Paused);'in hot
old=L/'pcsx2-ee-execution-map-v1/profile/PCSX2/inis/PCSX2.ini';assert 'TogglePause = Keyboard/Space'in old.read_text()
r=dict(status='PASS_EXACT_TAG_READ_ONLY_RUN_ACTION_AND_CONFIGURABLE_SPACE_TOGGLE_ROUTE_SOURCE_ONLY',commit=m['commit'],sourcePins={str(R/rel):sha(R/rel)for rel in paths},reportSha256=sha(P/'RESUME.md'),helperSha256=sha(Path(__file__)),historicalProfileSha256=sha(old),
 debuggerRunHasShortcut=False,F5ResumeProven=False,defaultAndHistoricalTogglePauseBinding='Keyboard/Space',
 targetMustBeOwnedRenderSurface=True,oneToggleFromVerifiedInitialPause=True,
 X11DeliveryOrActualResumeOrDeviceAccepted=False)
out.write_text(json.dumps(r,indent=2)+'\n',encoding='utf8');print(sha(out))
