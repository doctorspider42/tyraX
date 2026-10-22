"""Prepare same-ELF capture OFF/ON runs; memory reservation is identical."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('source',type=Path)
p.add_argument('--mode',choices=('OFF','ON'),required=True)
p.add_argument('--reuse-built',action='store_true')
p.add_argument('--recover-crlf-control',action='store_true')
a=p.parse_args();sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
assert not a.out.exists()
binding=json.loads((a.source/'native-binding.json').read_text())
for rel,digest in binding['files'].items():assert sha(a.source/rel)==digest,rel
assert sha(a.source/'game/bin/vehicle-playground.elf')==binding['elfSha256']
if a.reuse_built:
    assert json.loads((a.source/'world-source-switch-manifest.json').read_text())['runtimeSwitch']
    log=(a.source/'emulator/emulator.log').read_text(errors='replace')
    if a.recover_crlf_control:
        assert a.mode=='OFF' and (a.source/'game/bin/source-control.txt').read_bytes()==b'0\r\n'
        assert 'A44SOURCE stage=0 valid=0 bytes=0' in log
    else:assert 'DRIVEDONE valid=1' in log
else:
    assert not a.recover_crlf_control
    report=json.loads((a.source/'world-source-emulator.json').read_text())
    assert not report['valid'] and report['elfSha256']==binding['elfSha256']
    assert report['logSha256']==sha(a.source/'emulator/emulator.log')
    for rel,digest in report['files'].items():assert sha(a.source/rel)==digest,rel
def ignore(path,names):
    return [n for n in names if n in ('obj','.git') or
            (n.startswith(('tape-','command-','cell-','source-')) and n.endswith('.bin'))]
for part in ('game','tyra/engine'):shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json','world-cell-terrain-manifest.json',
            'world-cell-view-manifest.json','world-source-manifest.json'):
    shutil.copy2(a.source/rel,a.out/rel)
header=a.out/'game/inc/world_source_capture.hpp'
if not a.reuse_built:
    t=header.read_text()
    def once(old,new):
        global t
        assert t.count(old)==1,old
        t=t.replace(old,new)
    once('inline bool active=false,valid=true;',
         'inline bool active=false,valid=true,configured=false,enabled=false;')
    once('  active=true;', '''  if(!configured){
    auto* file=fopen("host:source-control.txt","rb");
    const int mode=file?fgetc(file):-1;
    const int extra=file?fgetc(file):-1;
    if(file)fclose(file);
    if((mode!='0' && mode!='1') || (extra!=EOF && extra!='\\n')){valid=false;return;}
    enabled=mode=='1';configured=true;
    printf("A44SOURCECONFIG enabled=%u reserve=%u\\n",unsigned(enabled),limit);
  }
  active=enabled;''')
    once('  char path[80];snprintf(path,sizeof(path),"host:source-%u.bin",stage);',
         '''  if(!enabled){
    printf("A44SOURCE stage=%u valid=1 records=0 bytes=0\\n",stage);return;
  }
  char path[80];snprintf(path,sizeof(path),"host:source-%u.bin",stage);''')
    header.write_text(t)
else:
    for rel in ('native-binding.json','symbols.txt','build.log'):
        shutil.copy2(a.source/rel,a.out/rel)
    if (a.source/'source-switch-host.json').exists():
        shutil.copy2(a.source/'source-switch-host.json',a.out/'source-switch-host.json')
    assert sha(a.out/'game/bin/vehicle-playground.elf')==binding['elfSha256']
(a.out/'game/bin/source-control.txt').write_bytes(b'1' if a.mode=='ON' else b'0')
manifest=json.loads((a.out/'world-source-manifest.json').read_text())
manifest['headerSha256']=sha(header)
manifest['baseElfSha256']=binding['elfSha256']
manifest['runtimeSwitch']=True
(a.out/'world-source-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(a.out/'world-source-switch-manifest.json').write_text(json.dumps({
    'runtimeSwitch':True,'mode':a.mode,'generatorSha256':sha(Path(__file__)),
    'controlSha256':sha(a.out/'game/bin/source-control.txt'),
    'sourceElfSha256':binding['elfSha256'],'reuseBuilt':a.reuse_built,
    'recoveredInvalidControl':a.recover_crlf_control,
    'reserveBytes':2*1024*1024,'status':'UNQUALIFIED'},indent=2)+'\n')
print(a.out,a.mode,'same-ELF copy' if a.reuse_built else 'requires native build')
