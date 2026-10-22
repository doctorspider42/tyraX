"""Create a component-only real PCM lifecycle fixture from qualified event audio."""
import argparse
import hashlib
import json
import shutil
import wave
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('source',type=Path)
a=p.parse_args();sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
assert not a.out.exists()
b=json.loads((a.source/'native-binding.json').read_text())
for rel,digest in b['files'].items():assert sha(a.source/rel)==digest,rel
assert sha(a.source/'game/bin/vehicle-playground.elf')==b['elfSha256']
assert sha(a.source/'tyra/engine/bin/libtyra.a')==b['engineArchiveSha256']
for report in ('world-cell-price-emulator.json','physical-v1/world-cell-price-ps2.json'):
    r=json.loads((a.source/report).read_text());assert r['valid'] and r['variant']=='audio-event' and r['elfSha256']==b['elfSha256']
def ignore(path,names):
    return [n for n in names if n in ('obj','.git') or (Path(path).name=='bin' and n in ('log.txt','drive-probe.csv')) or
        (n.startswith(('world-price-','triple-present-')) and n.endswith('.bin'))]
for part in ('game','tyra/engine'):shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json'):shutil.copy2(a.source/rel,a.out/rel)
repo=Path(__file__).resolve().parent
shutil.copy2(repo/'tyrax2-audio-pcm-life.hpp',a.out/'game/inc/audio_pcm_life.hpp')
f=a.out/'game/src/terrain_game.cpp';t=f.read_text()
old='void TerrainGame::init() {\n  MotionProbe::init();';assert t.count(old)==1
t=t.replace(old,'''void TerrainGame::init() {
  AudioPcmLife::run(engine->audio.song);
  while(true){const timespec hold={1,0};Threading::sleep(hold);}
  MotionProbe::init();''')
f.write_text('#include "audio_pcm_life.hpp"\n'+t)
wav=a.source/'game/res/sfx/engine_idle.wav'
with wave.open(str(wav),'rb') as w:
    assert w.getcomptype()=='NONE' and w.getsampwidth() in (1,2) and w.getnchannels() in (1,2)
    fmt=dict(rate=w.getframerate(),channels=w.getnchannels(),bits=w.getsampwidth()*8,frames=w.getnframes())
shutil.copy2(wav,a.out/'game/res/pcm-event.wav')
shutil.copy2(wav,a.out/'game/bin/pcm-event.wav')
(a.out/'pcm-life-manifest.json').write_text(json.dumps(dict(baseElfSha256=b['elfSha256'],wavSha256=sha(wav),format=fmt,
    generatorSha256=sha(Path(__file__)),status='UNQUALIFIED',componentOnly=True,
    limits=['No world/FPS/visual qualification; normal game init is intentionally suspended after component.',
            'Actual feed callbacks, play/stop/resume and kernel waiter; audible quality still separate.',
            'Restore bound PCM WAV into game/bin after native build if staging filters WAVs.']),indent=2)+'\n')
print(a.out,'UNQUALIFIED real PCM lifecycle component')
