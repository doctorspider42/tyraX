"""Read A44 actual cold reservation gate; not full-scene GPU/FPS proof."""
import argparse,hashlib,json,re,subprocess,sys
from pathlib import Path

def witness(log):
    rows=re.findall(r'A44SLOTS textures=(\d+) words=(\d+) heap=(\d+) budget=(\d+) '
                    r'dynamic=(\d+) controls=(\d+) victimTiers=(\d+) fence=GS released=1',log)
    assert len(rows)==1,('reservation witnesses',len(rows))
    textures,words,heap,budget,dynamic,controls,tiers=map(int,rows[0])
    assert textures==1 and heap>0 and 0<words<=budget==heap//2
    assert dynamic==heap//4 and dynamic>0 and controls==6 and tiers==2
    return dict(textures=textures,words=words,heapWords=heap,budgetWords=budget,
                dynamicAllowanceWords=dynamic,negativeControls=controls,victimTiers=tiers)

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('fixture',type=Path);p.add_argument('--physical',action='store_true')
a=p.parse_args();root=a.fixture.resolve()
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
reader=Path(__file__).with_name('verify-world-cell-sdk-execution.py')
subprocess.run([sys.executable,str(reader),str(root),*(['--physical'] if a.physical else [])],check=True)
binding=json.loads((root/'native-binding.json').read_text())
manifest=json.loads((root/'world-texture-manifest.json').read_text())
assert manifest['experiment']=='A44-world-texture-slots'
logpath=root/('physical/console.log' if a.physical else 'emulator/emulator.log')
log=logpath.read_text(errors='replace');data=witness(log)
line=re.search(r'A44SLOTS[^\n\r]*',log)[0]
controls={'missing':'','duplicate':line+'\n'+line,
          'textures':line.replace('textures=1','textures=2'),
          'budget':re.sub(r'budget=\d+','budget=0',line),
          'controls':line.replace('controls=6','controls=5'),
          'victims':line.replace('victimTiers=2','victimTiers=1'),
          'fence':line.replace('fence=GS','fence=VIF'),
          'release':line.replace('released=1','released=0')}
for name,text in controls.items():
    try:witness(text)
    except AssertionError:continue
    raise AssertionError('Accepted invalid reservation witness: '+name)
report={'valid':True,'experiment':'A44-world-texture-slots',
        'platform':'physical' if a.physical else 'emulator',**data,
        'elfSha256':binding['elfSha256'],'logSha256':sha(logpath),
        'readerNegativeControls':list(controls),'promotionAllowed':False,
        'limits':['One actual texture reservation/rebind/release; not sustained pressure.',
                  'CLUT pointer checked if present, not separate proof for both palette classes.',
                  'Victim-tier bookkeeping was temporarily set then restored on actual manager.',
                  'No full-world budget, compiled material head or new GPU body/raster/FPS gate.']}
(root/('world-texture-physical.json' if a.physical else 'world-texture-emulator.json')).write_text(
    json.dumps(report,indent=2)+'\n')
print(json.dumps(report))
