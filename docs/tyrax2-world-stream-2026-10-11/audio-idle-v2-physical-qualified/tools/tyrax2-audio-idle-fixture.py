"""Private empty PCM worker 0.5/16ms A/B; ADPCM and active PCM code retained."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('source',type=Path)
a=p.parse_args();sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
assert not a.out.exists()
binding=json.loads((a.source/'native-binding.json').read_text())
for rel,digest in binding['files'].items():assert sha(a.source/rel)==digest,rel
assert sha(a.source/'game/bin/vehicle-playground.elf')==binding['elfSha256']
assert sha(a.source/'tyra/engine/bin/libtyra.a')==binding['engineArchiveSha256']
gate=json.loads((a.source/'world-cell-price-emulator.json').read_text())
physical=json.loads((a.source/'physical-v1/world-cell-price-ps2.json').read_text())
assert gate['valid'] and physical['valid'] and physical['physicalPrice']
assert gate['variant']=='wheels-vu' and gate['wheelStatic']
assert gate['elfSha256']==physical['elfSha256']==binding['elfSha256']
def ignore(path,names):
    return [n for n in names if n in ('obj','.git') or
        (Path(path).name=='bin' and (n in ('log.txt','drive-probe.csv') or n.startswith('triple-present-'))) or
        (n.startswith(('tape-','command-','cell-','source-','moving-','world-price-')) and n.endswith('.bin'))]
for part in ('game','tyra/engine'):shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json'):shutil.copy2(a.source/rel,a.out/rel)
repo=Path(__file__).resolve().parent
shutil.copy2(repo/'tyrax2-audio-idle.hpp',a.out/'tyra/engine/inc/debug/audio_idle.hpp')
def edit(rel,old,new):
    f=a.out/rel;t=f.read_text();assert t.count(old)==1,(rel,old);f.write_text(t.replace(old,new))
audio='tyra/engine/src/audio/audio.cpp'
edit(audio,'#include "audio/audio.hpp"','#include "audio/audio.hpp"\n#include "debug/audio_idle.hpp"')
edit(audio,'''  Threading::switchThread();

  song.work();''','''  const bool observed=AudioIdle::observe;
  const unsigned generation=observed?AudioIdle::epoch:0;
  const bool playing=(AudioIdle::enabled || observed) && song.isPlaying();
  const bool idle=AudioIdle::enabled && !playing;
  if(idle){
    const timespec delay={0,16000000};
    Threading::sleep(delay);
  }else Threading::switchThread();
  if(observed && AudioIdle::observe && generation==AudioIdle::epoch){
    if(playing || song.isPlaying())++AudioIdle::active;
    else if(idle)++AudioIdle::fast;
    else ++AudioIdle::slow;
  }
  song.work();''')
song='tyra/engine/src/audio/audio_song.cpp'
f=a.out/song;t=f.read_text();f.write_text('#include "debug/audio_idle.hpp"\n'+t)
edit(song,'  songPlaying = true;','  ++AudioIdle::pcmStarts;\n  songPlaying = true;')
probe='tyra/engine/inc/debug/motion_probe.hpp'
edit(probe,'#pragma once','#pragma once\n#include "debug/audio_idle.hpp"')
edit(probe,'  enabled=sample && observing();','  AudioIdle::setObserved(!sample);\n  enabled=sample && observing();')
moving='game/inc/world_moving.hpp'
edit(moving,'#include "wheel_vu.hpp"','#include "wheel_vu.hpp"\n#include "debug/audio_idle.hpp"')
edit(moving,'WheelVu::active=p<6 && modes[p];','WheelVu::active=true;AudioIdle::set(p<6 && modes[p]);')
edit(moving,'  WheelVu::reset();','  WheelVu::reset();AudioIdle::reset();')
edit(moving,'variant=wheels-vu\\n",p,unsigned(WheelVu::active));','variant=audio-idle\\n",p,unsigned(AudioIdle::enabled));')
edit(moving,'  WheelVu::witness(phase);','  WheelVu::witness(phase);AudioIdle::witness(phase);')
game='game/src/terrain_game.cpp'
edit(game,'  const bool movingWheels=WheelVu::active;','  const bool movingWheels=WheelVu::active;\n  const bool movingAudio=AudioIdle::enabled;')
edit(game,'WheelVu::active=arm==1?!movingWheels:movingWheels;','WheelVu::active=true;AudioIdle::set(arm==1?!movingAudio:movingAudio);')
edit(game,'PaintCellPrice::held=true;WheelVu::reset();','PaintCellPrice::held=true;WheelVu::reset();AudioIdle::reset();')
edit(game,'WheelVu::witness(MotionProbe::phase,int(arm));','WheelVu::witness(MotionProbe::phase,int(arm));AudioIdle::witness(MotionProbe::phase,int(arm));')
edit(game,'WheelVu::active=movingWheels;','WheelVu::active=movingWheels;AudioIdle::set(movingAudio);')
manifest=json.loads((a.source/'world-cell-price-manifest.json').read_text())
manifest.update(baseElfSha256=binding['elfSha256'],generatorSha256=sha(Path(__file__)),
    variant='audio-idle',audioIdleNoPriceObserver=True,status='UNQUALIFIED',
    comparison='common direct+paint/Hybrid2/hybrid VU wheels/ordinary roads: empty PCM worker half versus16ms sleep',
    limits=['Only empty PCM polling changes; active PCM and ADPCM implementations retained.',
            'Potential song-start delay increases from0.5 to16ms; private diagnostic, no public audio acceptance.',
            'Require no PCM playback during this vehicle fixture; music runtime/audio quality separate.',
            'Worker witnesses disabled in ALL sampled rows; epoch drops transitions. PCM start event proves absence without per-frame polling.',
            'Full same-ELF image/pose/delivery and physical both-order net/tails required.'])
(a.out/'world-cell-price-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(a.out,'UNQUALIFIED idle PCM worker scheduling')
