"""Summarise physical-PS2 timing arms, and price ONE VU1 PACKAGE end to end.

    summarize_console.py ctl1=<dir> cand=<dir> ctl2=<dir> [--packages 23]

Each <dir> holds a `frame-cost.csv` written by instrument-frame-cost.py on the
console. The control is passed TWICE, from two separate boots, because a delta
means nothing without a repeatability floor and this fixture's is small enough
that a careless reader would quote noise.

WHY THIS SCRIPT EXISTS RATHER THAN A DIFF. The round it belongs to removes a
known number of VU1 PACKAGES and changes nothing else - same triangles as a
surface, same scene, same assets, same engine - so it can do something no
earlier round could: divide a measured millisecond by a counted package and get
the per-package cost of the whole submission path, not of one bracket.

The existing figure, 2.362 us, is packet CONSTRUCTION only (the `packet` bucket
over the frame's packages, docs/ee-submission-rearchitecture.md). If the rest of
`dispatch` scales with packages too, a package is worth far more. That ratio is
what every future packing decision gets priced off, so it is printed
explicitly - per bracket, so a reader can see WHICH terms followed.
"""
import argparse
import csv
from pathlib import Path

POSES = ['garage day', 'garage night', 'outer day', 'outer night']
# The disjoint frame split, then the included telemetry buckets. `work` is the
# repo's usual headline: everything but `present`, i.e. what the game spends
# rather than what the video mode makes it wait for.
COLS = ['update_ms', 'submit_ms', 'finish_ms', 'present_ms', 'total_ms',
        'bounds_included_ms', 'prepare_included_ms', 'dispatch_included_ms',
        'packet_included_ms', 'dma_included_ms', 'vif_wait_included_ms',
        'flushes', 'triangles']
SHOW = [('work', None), ('submit_ms', None), ('dispatch_included_ms', None),
        ('packet_included_ms', None), ('bounds_included_ms', None),
        ('prepare_included_ms', None), ('dma_included_ms', None),
        ('vif_wait_included_ms', None), ('flushes', 0), ('triangles', 0),
        ('total_ms', None)]


def load(path):
    rows = {}
    with open(Path(path) / 'frame-cost.csv', newline='') as f:
        for r in csv.DictReader(f):
            ph = int(r['phase'])
            acc = rows.setdefault(ph, {c: 0.0 for c in COLS})
            acc.setdefault('n', 0)
            for c in COLS:
                acc[c] += float(r[c])
            acc['n'] += 1
    for ph, acc in rows.items():
        n = acc.pop('n')
        for c in COLS:
            acc[c] /= n
        # `work` is total minus present: the part the change can reach.
        acc['work'] = acc['total_ms'] - acc['present_ms']
    return rows


def fmt(v, dp):
    return f'{v:.0f}' if dp == 0 else f'{v:.3f}'


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('arms', nargs='+', help='label=dir (ctl1 first, then cand, then ctl2)')
    p.add_argument('--packages', type=float, default=23.0,
                   help='VU1 packages the candidate removes in the pose named '
                        'by --pose (default 23, the garage-day figure)')
    p.add_argument('--pose', type=int, default=0)
    p.add_argument('--frame-packages', type=float, default=711.0,
                   help="the control's total packages in that pose")
    a = p.parse_args()

    arms, order = {}, []
    for spec in a.arms:
        label, _, path = spec.partition('=')
        arms[label] = load(path)
        order.append(label)
    ctl1, cand = order[0], order[1]
    ctl2 = order[2] if len(order) > 2 else None

    if ctl2:
        print('REPEATABILITY FLOOR - two boots of the SAME ELF')
        print(f'{"bracket":<24}{ctl1:>11}{ctl2:>11}{"drift":>11}')
        print('-' * 57)
        for c, dp in SHOW:
            d = arms[ctl2][a.pose][c] - arms[ctl1][a.pose][c]
            print(f'{c:<24}{fmt(arms[ctl1][a.pose][c], dp):>11}'
                  f'{fmt(arms[ctl2][a.pose][c], dp):>11}{d:+11.3f}')
        print()

    for ph in sorted(arms[ctl1]):
        print(f'=== pose {ph} ({POSES[ph]}) ===')
        print(f'{"bracket":<24}{"control":>11}{"candidate":>11}{"delta":>11}')
        print('-' * 57)
        for c, dp in SHOW:
            base = arms[ctl1][ph][c]
            if ctl2:
                base = 0.5 * (base + arms[ctl2][ph][c])
            d = arms[cand][ph][c] - base
            print(f'{c:<24}{fmt(base, dp):>11}'
                  f'{fmt(arms[cand][ph][c], dp):>11}{d:+11.3f}')
        print()

    # --- the number this round exists to produce ---------------------------
    ph = a.pose
    base = arms[ctl1][ph]
    if ctl2:
        base = {c: 0.5 * (arms[ctl1][ph][c] + arms[ctl2][ph][c])
                for c in list(COLS) + ['work']}
    floor = abs(arms[ctl2][ph]['work'] - arms[ctl1][ph]['work']) if ctl2 else None
    print(f'WHAT ONE VU1 PACKAGE COSTS, pose {ph} ({POSES[ph]})')
    print(f'  the candidate removes {a.packages:.0f} of the control\'s '
          f'{a.frame_packages:.0f} packages')
    if floor is not None:
        print(f'  repeatability floor on `work`: {floor:.3f} ms - read every '
              f'figure below against it')
    print(f'{"bracket":<24}{"delta ms":>11}{"us/package":>13}')
    print('-' * 48)
    for c in ('work', 'submit_ms', 'dispatch_included_ms', 'packet_included_ms',
              'bounds_included_ms', 'prepare_included_ms', 'dma_included_ms'):
        d = arms[cand][ph][c] - base[c]
        print(f'{c:<24}{d:+11.3f}{-d * 1000.0 / a.packages:>13.3f}')
    print()
    print('  For contrast, the figure this round was priced against:')
    print(f'    packet construction alone, control = '
          f'{base["packet_included_ms"] * 1000.0 / a.frame_packages:.3f} us/package')
    print(f'    the whole `dispatch` bracket, control = '
          f'{base["dispatch_included_ms"] * 1000.0 / a.frame_packages:.3f} us/package')


if __name__ == '__main__':
    main()
