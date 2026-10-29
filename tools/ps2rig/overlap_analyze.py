"""Validate all first-frame overlap diagnostics; these are not FPS benchmarks."""
from pathlib import Path
import argparse, csv, hashlib, json, math, re, statistics

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('root', type=Path)
    p.add_argument('--log', required=True, type=Path)
    p.add_argument('--output', required=True, type=Path)
    a = p.parse_args()
    log = a.log.read_text(encoding='utf8', errors='replace')
    assert 'CB DONE phases=12' in log
    assert 'VUSATCHECK cases=2048 failures=0 ' in log
    monitor = re.findall(r'DIAG MONITOR div=(\w+) invalid=(\w+) pass=(\d+)', log)
    assert len(monitor) == 1 and monitor[0][2] == '1'
    assert int(monitor[0][0], 16) & 0x20 and int(monitor[0][1], 16) & 0x40
    entries = re.findall(r'DIAG p=(\d+) f=(\d+) us=(\d+) contacts=(\d+) '
                         r'generated=(\d+) depth=(\S+) energy=(\S+) fp=(\w+) vu=(\w+)', log)
    assert len(entries) == 720
    phases, hashes = [], {}
    for phase in range(12):
        rows = [r for r in entries if int(r[0]) == phase]
        assert [int(r[1]) for r in rows] == list(range(60)), phase
        for r in rows:
            assert r[3] == r[4], r
            assert math.isfinite(float(r[5])) and math.isfinite(float(r[6])), r
            # EE FCR31 SI/SD/SO; VU0 STATUS IS/DS/OS. Underflow is recorded,
            # but tiny damped velocities alone are not an invalid computation.
            assert int(r[7], 16) & 0x70 == 0, r
            assert int(r[8], 16) & 0xe00 == 0, r
        for name in [f'collision-phase-{phase:02d}.csv', f'vu0-phase-{phase:02d}.csv']:
            f = a.root / name
            hashes[name] = hashlib.sha256(f.read_bytes()).hexdigest()
        contacts = list(csv.DictReader((a.root / f'collision-phase-{phase:02d}.csv').open()))
        assert len(contacts) == 60
        assert all(int(r['dropped_contacts']) == int(r['push_overflow']) == 0
                   and r['generated_contacts'] == r['contacts'] for r in contacts)
        micro = list(csv.DictReader((a.root / f'vu0-phase-{phase:02d}.csv').open()))
        assert len(micro) == 1
        m = micro[0]
        assert int(m['validation_errors']) == int(m['missed_reference_hits']) == 0
        phases.append(dict(phase=phase, arm='micro-shadow' if phase % 2 else 'scalar',
                           added=128 if (phase // 2) % 2 else 64,
                           layout=['spread', 'spacing_0.4', 'identical_centres'][phase // 4],
                           frames=len(rows), first_physics_ms=int(rows[0][2])/1000,
                           median_physics_ms=statistics.median(int(r[2]) for r in rows)/1000,
                           max_physics_ms=max(int(r[2]) for r in rows)/1000,
                           max_contacts=max(int(r[3]) for r in rows),
                           max_depth=max(float(r[5]) for r in rows),
                           ee_status_values=sorted({r[7] for r in rows}),
                           vu0_status_values=sorted({r[8] for r in rows}),
                           micro=m))
    elf = a.root / 'vehicle-playground.elf'
    out = dict(frames=720, phases=phases, monitor=monitor[0], raw_sha256=hashes,
               log_sha256=hashlib.sha256(a.log.read_bytes()).hexdigest(),
               elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
               ee_exception_mask='0x70', vu0_exception_mask='0xe00',
               conclusion='No division-by-zero, invalid-operation or overflow flags in physics. All cases completed; no capacity omissions.',
               limitations='Per-call printf affects subsequent real dt. Micro arm includes full scalar shadow validation. Do not compare FPS or micro speed from this diagnostic. Maximum contact depth also includes authored scene props.')
    a.output.write_text(json.dumps(out, indent=2), encoding='utf8')
    print('Overlap diagnostics: 720 frames; zero invalid/divide/overflow flags, reference failures or capacity omissions')

if __name__ == '__main__':
    main()
