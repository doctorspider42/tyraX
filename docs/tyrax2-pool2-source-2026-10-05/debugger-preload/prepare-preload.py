"""Prepare one source-derived debugger settings JSON outside the profile; never launch/control UI."""
from pathlib import Path
import argparse,struct,json,hashlib,importlib.util
L=Path(__file__).parents[1];W=L/'wild-pool2-static-probe-design-v2';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def need(x,s):
 if not x:raise ValueError(s)
def load(name,p):
 s=importlib.util.spec_from_file_location(name,p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
def crc(data):
 result=0
 for (word,) in struct.iter_unpack('<I',data[:len(data)//4*4]):result^=word
 return result
def row(pc):
 need(0<pc<=0xffffffff and pc%4==0,'aligned nonzero EE PC')
 return {'X':'1','TYPE':'8','OFFSET':f'{pc:08x}','DESCRIPTION':'POOL2_STATIC_PROBE_HALT','SIZE / LABEL':'','INSTRUCTION':'','CONDITION':'','HITS':'0'}
def main():
 a=argparse.ArgumentParser();a.add_argument('--elf',type=Path,required=True);a.add_argument('--sym',type=Path,required=True);a.add_argument('--debugger-settings-dir',type=Path,required=True);a.add_argument('--out',type=Path,required=True);q=a.parse_args()
 need(not q.out.exists(),'NEW external output only');need(q.out.resolve()!=q.debugger_settings_dir.resolve(),'prepare outside actual profile')
 v=load('verify',W/'verify-capture.py');r=load('reader',W/'savestate-reader-pinned.py');sy=v.symbols(q.sym);e,t=r.text_from_elf(q.elf);se,st=r.text_from_elf(q.sym);need(t==st and e['textAddress']==se['textAddress'],'exact ELF/sym text identity')
 pc=sy['pool2ProbeHalt']['address'];need(e['textAddress']<=pc<e['textAddress']+len(t),'halt entry inside boot text')
 c=crc(q.elf.read_bytes());need(c!=0,'PCSX2 game CRC nonzero');serial=q.elf.stem;need(serial and all(x not in serial for x in '/\\'),'ELF title')
 filename=f'{serial}_{c:08X}.json';payload={'Version':'0.01','Breakpoints':[row(pc)]}
 q.out.mkdir();p=q.out/filename;p.write_text(json.dumps(payload,indent=2)+'\n',encoding='utf8')
 proof=dict(status='PREPARED_SOURCE_DERIVED_SINGLE_EE_EXECUTE_BREAKPOINT_NOT_INSTALLED_OR_RUNTIME_ACCEPTED',pcsx2Tag='v2.9.93',pcsx2Commit='94d86c891b1621c0b252e4fc2e155bf90274dcc0',
  files={str(x):sha(x)for x in(q.elf,q.sym,p,Path(__file__),W/'verify-capture.py',W/'savestate-reader-pinned.py')},haltPC=f'{pc:08x}',readyAddress=f'{sy["pool2ProbeReady"]["address"]:08x}',
  pcsx2CRC=f'{c:08X}',serial=serial,destination=str(q.debugger_settings_dir/filename),payloadSha256=sha(p),
  noDiscElfOverrideRequired=True,startWithDebugger=True,preloadedBeforeBootRequired=True,
  sourceDerivedSettingsFormat='Version0.01, eight string fields, TYPE8->BreakPoint rather than memcheck, enabled1, empty condition, default continueOnHitfalse/instrumentationfalse/maxHits0',
  runtimeLoadedBreakpointAndExactHaltPinePausedStateStillRequired=True,deviceOrUIOrProfileMutationInvoked=False)
 (q.out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf8');print(json.dumps(proof,indent=2))
if __name__=='__main__':main()
