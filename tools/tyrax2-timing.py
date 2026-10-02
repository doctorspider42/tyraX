"""Summarize one exact contiguous FTRAW window from a physical PS2 stdout log."""
import argparse
import json
import math
import re
import statistics
from pathlib import Path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('log', type=Path)
    p.add_argument('--first', type=int, required=True)
    p.add_argument('--frames', type=int, default=512)
    p.add_argument('--hz', type=int, choices=[50, 60], required=True)
    args = p.parse_args()
    if args.first < 0 or args.frames < 1:
        p.error('first must be nonnegative and frames positive')
    text = args.log.read_text(encoding='utf-8', errors='replace')
    # ps2client inserts newlines at tty packet boundaries, including within a
    # hex word. Join ONLY those newlines; preserve the original token spaces.
    samples = {}
    for block in text.split('LOG: FTRAW ')[1:]:
        block = block.split('LOG:')[0].replace('\r', '').replace('\n', '').strip()
        tokens = block.split()
        if not tokens or not re.fullmatch(r'\d+', tokens[0]):
            continue
        first = int(tokens[0])
        # The producer writes exactly 64 values per record (kRaw=512).
        if len(tokens) != 65 or any(not re.fullmatch(r'[0-9a-fA-F]{1,8}', x)
                                    for x in tokens[1:]):
            continue
        for i, token in enumerate(tokens[1:]):
            frame = first + i
            if frame in samples:
                p.error(f'Duplicate frame {frame}; do not concatenate boots')
            samples[frame] = int(token, 16) / 294912
    wanted = range(args.first, args.first + args.frames)
    missing = [frame for frame in wanted if frame not in samples]
    if missing:
        p.error(f'Incomplete window: {len(missing)} missing, first {missing[0]}')
    values = [samples[frame] for frame in wanted]
    ordered = sorted(values)
    result = {'log': str(args.log.resolve()), 'first': args.first,
              'frames': args.frames, 'hz': args.hz, 'budget_ms': 1000 / args.hz,
              'mean_ms': statistics.mean(values),
              'median_ms': statistics.median(values),
              'p95_ms': ordered[math.ceil(len(values)*0.95)-1],
              'max_ms': max(values),
              'over_budget': sum(v > 1000 / args.hz for v in values),
              'note': 'Renderer critical-path work excluding presentation pacing; not full period or delivered FPS.'}
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
