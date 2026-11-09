"""Apply the engine probes, in order, to a FRESH copy of vendor/tyra/engine and
report which ones still apply (the rig engine C:/tx/qp/tyra grew by hand)."""
import os, re, shutil, subprocess, sys, tempfile

SRC = 'F:/Projects/tyra-editor/vendor/tyra/engine'
DST = 'C:/tx/rigtest/engine'
ORDER = ['engine_lateprobe.py', 'engine_mutelog.py', 'engine_gpuwait.py',
         'engine_route.py', 'engine_route2.py', 'engine_route3.py',
         'engine_route3b.py', 'engine_pmu.py', 'engine_arm.py',
         'engine_sendpmu.py', 'engine_stampx.py', 'engine_lean.py',
         'engine_warm.py', 'engine_stampcen.py', 'engine_site.py',
         'engine_stamplap.py']
if os.path.exists(DST):
    shutil.rmtree(DST)
shutil.copytree(SRC, DST)
tmp = tempfile.mkdtemp()
for f in ORDER:
    s = open(os.path.join('C:/tx/qp', f), encoding='utf-8').read()
    s = s.replace('C:/tx/qp/tyra/engine', DST)
    p = os.path.join(tmp, f)
    open(p, 'w', encoding='utf-8').write(s)
    arg = os.path.dirname(DST) if f in ('engine_warm.py', 'engine_stampcen.py', 'engine_site.py', 'engine_stamplap.py') else DST
    r = subprocess.run([sys.executable, p, arg], capture_output=True, text=True)
    last = (r.stdout.strip().splitlines() or [''])[-1]
    err = (r.stderr.strip().splitlines() or [''])[-1]
    print('%-22s %s %s' % (f, 'OK  ' if r.returncode == 0 else 'FAIL', (last if r.returncode == 0 else err)[:110]))
