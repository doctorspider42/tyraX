"""apply.py PROJECT [probe...] - instrument a regenerated project.

Always: kf (the KF timing ring + the scripted drive), kf2n, kf3, physlaps,
phys2. Then the named optional probes in the order below, whatever order they
were given in; a probe's prerequisites are added for it. Several optional
probes share KFL slots 36..43 - pick one family per arm."""
import os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
BASE = ['kf', 'kf2n', 'kf3', 'physlaps', 'phys2']
OPTIONAL = ['late', 'mute', 'batchlap', 'bakeev2', 'settlelap', 'reblap',
            'gpuwait', 'terrlap', 'chunklap', 'chunkid', 'emitlap', 'objlap',
            'memmap', 'bplap', 'awakevis', 'route', 'arm', 'warm', 'stampcen',
            'site', 'stamplap', 'cell']
NEEDS = {'reblap': ['settlelap'], 'arm': ['route'], 'warm': ['route', 'arm'],
         'stampcen': ['route', 'arm', 'warm'],
         'site': ['route', 'arm', 'warm', 'stampcen'],
         'stamplap': ['route', 'arm', 'warm', 'stampcen', 'site']}

proj, want = sys.argv[1], set(sys.argv[2:])
bad = want - set(OPTIONAL)
if bad:
    sys.exit('unknown probe(s): %s\nknown: %s' % (' '.join(sorted(bad)), ' '.join(OPTIONAL)))
for w in list(want):
    want.update(NEEDS.get(w, []))
for name in BASE + [o for o in OPTIONAL if o in want]:
    arg = proj + '/src/gen/game_physics.gen.cpp' if name == 'physlaps' else proj
    r = subprocess.run([sys.executable, os.path.join(HERE, name + '.py'), arg],
                       capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit('%s: %s' % (name, (r.stdout + r.stderr).strip()))
    print('%-10s ok' % name)
