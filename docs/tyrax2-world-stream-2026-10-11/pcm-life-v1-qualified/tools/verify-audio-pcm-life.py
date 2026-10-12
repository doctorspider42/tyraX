"""Qualify actual PCM feed/lifecycle, never renderer FPS or audible quality."""
import argparse
import hashlib
import json
import re
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('fixture',type=Path);p.add_argument('--log',type=Path,required=True)
a=p.parse_args();root=a.fixture;sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
b=json.loads((root/'native-binding.json').read_text());m=json.loads((root/'pcm-life-manifest.json').read_text())
for rel,digest in b['files'].items():assert sha(root/rel)==digest,rel
assert sha(root/'game/bin/vehicle-playground.elf')==b['elfSha256']
assert sha(root/'game/bin/pcm-event.wav')==m['wavSha256']
log=a.log.read_text(errors='replace')
assert 'PCMLIFEBEGIN rounds=6 wav=pcm-event.wav' in log
assert 'PCMLIFEDONE valid=1 rounds=6 starts=6 waiter=1' in log
assert not any(s in log for s in ('lifecycle timeout','test WAV failed','WAV header not recognized'))
rows=[tuple(map(int,r)) for r in re.findall(r'PCMLIFE round=(\d+) event=(\d+) feeds=(\d+) firstTicks=(\d+) stopped=1 waiting=(\d+) waiters=(\d+) starts=(\d+)',log)]
assert [r[0] for r in rows]==list(range(6))
for r in rows:
    mode=int(r[0]>=3);assert r[1]==mode and r[2]>=3 and 0<r[3]<294912000*3 and r[4]==r[5]==mode and r[6]==r[0]+1
out=a.log.parent/'pcm-life-qualified.json'
out.write_text(json.dumps(dict(valid=True,elfSha256=b['elfSha256'],logSha256=sha(a.log),wavSha256=m['wavSha256'],rows=rows,
    feedAndLifecycle=True,performance=False,audibleQuality=False,promotionAllowed=False),indent=2)+'\n')
print(json.dumps(dict(valid=True,rounds=6,performance=False,audibleQuality=False)))
