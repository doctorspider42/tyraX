"""Turn the route stations into a POPPING verdict.

    python analyze_route.py ctl=<dir> cand=<dir> [--stations 20] [--approach 12]

Each directory holds `sN-k.png`, one per station, from probe-visibility.ps1
driving the route sampler. Stations 0..11 walk the garage approach from 117
units out to 45 (crossing the 100 switch and the 90 hysteresis edge); 12..19
orbit the object at a fixed 110 units, crossing view-sector boundaries of the
8-view card.

TWO QUESTIONS, and the second is the gate.

1. WHAT DOES THE CARD COST AT THIS DISTANCE?  candidate vs control at the SAME
   station. Both arms are parked at an identical camera position, so the
   difference is the representation and nothing else. Zero means the swap has
   not happened yet at that station.

2. DOES IT POP?  A pop is not "the picture changed" - crossing the threshold is
   SUPPOSED to change the picture. A pop is the change being much larger than
   what moving the camera that far does anyway. So the script prints, for each
   step along the route:

     move(ctl)   pixels that change between adjacent stations in the CONTROL.
                 This is the honest yardstick: it is what the player's eye
                 already accepts as "the camera moved".
     move(cand)  the same step in the candidate, which includes any swap.
     ratio       move(cand) / move(ctl).

   A swap step whose ratio is near 1 is invisible against ordinary camera
   motion. A ratio far above 1 is a pop, and its size is the finding.

   Judging the swap by the cand-vs-ctl column alone would be wrong: that column
   is large wherever the card is showing AT ALL, including at stations where
   nothing is popping because both neighbours are cards.
"""
import argparse
from pathlib import Path

import numpy as np
from PIL import Image


def load(p):
    return np.asarray(Image.open(p).convert('RGB'), dtype=np.int16)


def npx(a, b):
    d = np.abs(a - b).max(axis=2)
    return int((d > 0).sum()), int(d.max())


ap = argparse.ArgumentParser(description=__doc__,
                             formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument('arms', nargs=2)
ap.add_argument('--stations', type=int, default=20)
ap.add_argument('--approach', type=int, default=12)
a = ap.parse_args()
names, dirs = zip(*[s.split('=', 1) for s in a.arms])
ctl_d, cand_d = Path(dirs[0]), Path(dirs[1])

# eye z per approach station, and the distance to Tower block 06 at (-33,0,80)
ZS = [-32, -20, -12, -8, -6, -4, -2, 0, 4, 12, 24, 40]
def dist(z):
    return ((-33.0) ** 2 + 4.0 ** 2 + (80.0 - z) ** 2) ** 0.5


def shot(d, s, k=1):
    p = d / f's{s}-{k}.png'
    return load(p) if p.exists() else None


# Repeatability is checked with ONE re-visit rather than a repeat of every
# station: this fixture's determinism is already established (the 2026-09-17
# visibility round had all 19 probes bit-identical and zero control drift over
# a boot), and 20 stations x 2 arms is long enough without doubling it. The
# re-visit is taken LAST, so it is a drift check over the whole run as well.
print('REPEATABILITY - station 0, revisited at the END of the run, per arm')
for nm, d in zip(names, (ctl_d, cand_d)):
    first, again = shot(d, 0, 1), None
    p = d / 's0rep-1.png'
    if p.exists():
        again = load(p)
    if first is not None and again is not None:
        px, worst = npx(first, again)
        note = ''
        if px:
            # A small drift is a NOISE FLOOR, not a dead run: it says how big a
            # between-arm difference has to be before it means anything. Only a
            # drift comparable to the differences being read invalidates them.
            note = f'   <- noise floor: ignore between-arm numbers below ~{px} px'
        print(f'  {nm}: {px} px, worst {worst}{note}')
    else:
        print(f'  {nm}: no re-visit capture - this run has no drift check')

print('\n1. WHAT THE CARD COSTS, per station')
print(f'{"st":>3}{"where":>26}{"dist40":>8}{"cand vs ctl px":>16}{"%screen":>9}{"worst":>7}')
print('-' * 69)
cvc = {}
for s in range(a.stations):
    c, d = shot(ctl_d, s), shot(cand_d, s)
    if c is None or d is None:
        continue
    px, worst = npx(c, d)
    cvc[s] = px
    if s < a.approach:
        where, dd = f'approach eye z={ZS[s]}', f'{dist(ZS[s]):.0f}'
    else:
        where, dd = f'orbit {120 + 13*(s-a.approach)} deg', '110'
    print(f'{s:>3}{where:>26}{dd:>8}{px:>16}{100.0*px/(c.shape[0]*c.shape[1]):>8.2f}%{worst:>7}')

print('\n2a. THE POP ITSELF - the DISCONTINUITY in the column above')
print('The swap replaces one representation with the other between two adjacent')
print('stations. Everything the card was getting wrong at the last station before')
print('the swap changes back in one frame, so THAT number is the pop:')
seq = [s for s in range(a.stations) if s in cvc]
worst_pop, worst_at = 0, None
for i in range(len(seq) - 1):
    s, t = seq[i], seq[i + 1]
    if t == a.approach:      # approach -> orbit is a teleport, not a swap
        continue
    jump = abs(cvc[t] - cvc[s])
    if jump > worst_pop:
        worst_pop, worst_at = jump, (s, t)
    if (cvc[s] == 0) != (cvc[t] == 0):
        print(f'  stations {s}->{t}: representation SWITCHES, '
              f'{cvc[s]} -> {cvc[t]} px of difference')
if worst_at:
    print(f'  largest single-step change in the card\'s error: {worst_pop} px '
          f'at stations {worst_at[0]}->{worst_at[1]}')

print('\n2b. DOES IT POP? step between ADJACENT stations, both arms')
print(f'{"step":>9}{"move(ctl)":>11}{"move(cand)":>12}{"ratio":>8}   verdict')
print('-' * 64)
for s in range(a.stations - 1):
    if s + 1 == a.approach:
        print('  --- approach ends, orbit begins (not a continuous move) ---')
        continue
    c0, c1 = shot(ctl_d, s), shot(ctl_d, s + 1)
    d0, d1 = shot(cand_d, s), shot(cand_d, s + 1)
    if any(x is None for x in (c0, c1, d0, d1)):
        continue
    mc = npx(c0, c1)[0]
    md = npx(d0, d1)[0]
    r = (md / mc) if mc else float('inf')
    swap = (cvc.get(s, 0) == 0) != (cvc.get(s + 1, 0) == 0)
    verdict = ('SWAP HERE' if swap else '')
    if r > 2.0:
        verdict += '  <- candidate step is more than twice the control step'
    print(f'{f"{s}->{s+1}":>9}{mc:>11}{md:>12}{r:>8.2f}   {verdict}')

print('\nRead the ratio, not the raw counts. A swap step near 1.0 is hidden by '
      'the camera motion\nthat is happening anyway; one far above 1.0 is a pop '
      'and the number is how bad.')
