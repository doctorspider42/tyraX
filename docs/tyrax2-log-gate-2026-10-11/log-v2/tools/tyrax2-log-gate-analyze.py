"""A27 logging intervention and inherited travel/byte/raster qualification."""
from pathlib import Path
base=Path(__file__).with_name('tyrax2-fixed-header-analyze.py').read_text()
finish="exec(compile(source,str(Path(__file__)),'exec'))"
assert base.count(finish)==1
exec(compile(base.replace(finish,''),str(Path(__file__)),'exec'))
source=source.replace("plan['fixedHeader'] and plan['experiment']=='A26'",
    "plan['ordinaryLogGate'] and not plan['fixedHeader'] and plan['experiment']=='A27'")
source=source.replace("assert 'A26 fixed header differs from SDK auto close' in program",
    "assert 'A26 fixed header differs from SDK auto close' in program and '  if(false){' in program\n"
    "assert 'OrdinaryLog::enabled=variant()!=7 || offset<gasStart || finishPending;' in (root/'tyra/engine/inc/debug/motion_probe.hpp').read_text()\n"
    "assert (root/'tyra/engine/inc/debug/motion_probe.hpp').read_text().index('LOGWINDOWBEGIN')>(root/'tyra/engine/inc/debug/motion_probe.hpp').read_text().index('inline void end(U pacing)')\n"
    "assert 'if (OrdinaryLog::enabled) TyraDebug::writeLines' in (root/'tyra/engine/inc/debug/debug.hpp').read_text()")
source=source.replace("report['consumerInputEqual']=True;report['headerOracle']=checks;report['inputs']=inputs",r'''ordinaryPattern=r'LOG: (VEH |VEHCONTACT |TERRAINSTRIP )' if a.physical else r'open name host:/log.txt flag 302'
def logWindows(log):
 windows=re.findall(r'LOGWINDOWBEGIN phase=(\d+) quiet=(\d+)[^\n]*\n(.*?)LOGWINDOWEND phase=(\d+)',log,re.S)
 assert len(windows)==count
 ordinary=[]
 for phase,(begin,quiet,body,end) in enumerate(windows):
  assert int(begin)==int(end)==phase and int(quiet)==int(plan['modes'][phase]==7)
  messages=re.findall(ordinaryPattern,body)
  assert (not re.search(r'LOG: ',body) and not messages) if int(quiet) else bool(messages)
  ordinary.append(dict(phase=phase,quiet=bool(int(quiet)),ordinaryOutputEvents=len(messages),transport='console' if a.physical else 'host-log-file-opens'))
 return ordinary
report['ordinaryLogWindows']=logWindows(text)
corruptions=[text.replace('LOGWINDOWBEGIN phase=0','MISSINGLOGWINDOW phase=0'),
 text.replace('LOGWINDOWBEGIN phase=1 quiet=1','LOGWINDOWBEGIN phase=1 quiet=1\nLOG: injected'),
 re.sub(ordinaryPattern,'REMOVEDORDINARY ',text),
 text.replace('LOGWINDOWBEGIN phase=1 quiet=1','LOGWINDOWBEGIN phase=1 quiet=0')]
for bad in corruptions:
 try:logWindows(bad)
 except AssertionError:pass
 else:raise AssertionError('Log-gate rejection escaped')
report['negativeControls']+=['missing-log-window','quiet-log-output','inactive-baseline-output','wrong-log-mode']
if not a.physical:
 ordinaryFile=folder/'log.txt'
 assert re.search(r'LOG: VEH ',ordinaryFile.read_text(errors='replace'))
 report['ordinaryLogSha256']=sha(ordinaryFile)
report['consumerInputEqual']=True;report['headerOracle']=checks;report['inputs']=inputs''')
source=source.replace('fixed-header-{platform}.json','log-gate-{platform}.json')
source=source.replace('Same output; fixed header close; elapsed net work, not pure EE cycles',
    'Same output; ordinary logging omitted only during gas; elapsed net work, not pure EE cycles')
exec(compile(source,str(Path(__file__)),'exec'))
