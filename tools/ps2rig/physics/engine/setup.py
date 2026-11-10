"""setup.py DST - the rig engine: a copy of vendor/tyra at DST with every
engine probe applied, in the order they were written against each other, plus
rig-base.patch (the counters the game probes read: late events, mute switch,
GS-tail wait, StaPip refresh/churn counters, baked/retained byte totals).

The probes are harmless counters, but this engine never ships: arms build
against it with `-Engine DST`, vendor/tyra stays untouched. Re-run after
vendor/tyra changed; a probe whose anchor moved stops with its anchor."""
import os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..', '..', '..'))
ORDER = ['lateprobe', 'mutelog', 'gpuwait', 'route', 'route2', 'route3',
         'route3b', 'pmu', 'arm', 'sendpmu', 'stampx', 'lean', 'warm',
         'stampcen', 'site', 'stamplap']

dst = sys.argv[1]
if os.path.exists(dst):
    shutil.rmtree(dst)
shutil.copytree(os.path.join(REPO, 'vendor', 'tyra'), dst,
                ignore=shutil.ignore_patterns('.git', 'obj', 'bin'))
eng = os.path.join(dst, 'engine')
for name in ORDER:
    r = subprocess.run([sys.executable, os.path.join(HERE, name + '.py'), eng],
                       capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit('%s: %s' % (name, (r.stdout + r.stderr).strip()))
    print('%-10s ok' % name)
r = subprocess.run(['git', 'apply', '--recount', '-p1', '--directory=engine',
                    os.path.join(HERE, 'rig-base.patch')], cwd=dst,
                   capture_output=True, text=True)
if r.returncode != 0:
    sys.exit('rig-base.patch: ' + r.stderr.strip())
print('rig-base   ok\nrig engine ready:', dst)
