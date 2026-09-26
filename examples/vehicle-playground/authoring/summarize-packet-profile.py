"""Turn physical-console FTPKT lines into a four-pose packet-cost CSV.

The generated frame rig prints one FTPKT row per active producer every 50
frames when TYRA_FRAME_PROFILE is enabled. Feed this script the editor/ps2link
console log captured with Tee-Object. It rejects malformed rows, ignores the
camera-transition margins of benchmark-district.py, and writes per-frame
medians for the four frozen Motor District poses.
"""

import argparse
import csv
import re
import statistics
from collections import defaultdict
from pathlib import Path

WINDOW = 50
POSES = ("garage-day", "garage-night", "outer-road-day", "outer-road-night")
LINE = re.compile(r"FTPKT f=(\d+) p=(\w+) (.*)")
PAIR_FIELDS = {"a128": ("refs_aligned128", "refs_unaligned128")}
TRIPLE_FIELDS = {
    "flush": ("vif_flush", "vif_flushe", "vif_flusha"),
    "msc": ("vif_mscal", "vif_mscalf", "vif_mscnt"),
}
SCALAR_FIELDS = (
    "kick", "gif", "ad", "gqw", "tag", "ref", "cnt", "end", "rq", "iq",
    "vifw", "nop", "unpack", "cyc", "row", "col", "bad",
)


def parse_fields(text: str) -> dict[str, int]:
    raw = dict(token.split("=", 1) for token in text.split())
    out = {name: int(raw[name]) for name in SCALAR_FIELDS}
    for source, names in PAIR_FIELDS.items():
        values = [int(value) for value in raw[source].split("/")]
        if len(values) != len(names):
            raise ValueError(f"bad {source}: {raw[source]}")
        out.update(zip(names, values))
    for source, names in TRIPLE_FIELDS.items():
        values = [int(value) for value in raw[source].split("/")]
        if len(values) != len(names):
            raise ValueError(f"bad {source}: {raw[source]}")
        out.update(zip(names, values))
    return out


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("-o", "--output", required=True, type=Path)
    args = parser.parse_args()

    samples: dict[tuple[int, str], list[dict[str, int]]] = defaultdict(list)
    malformed = 0
    for line in args.log.read_text(encoding="utf-8", errors="replace").splitlines():
        match = LINE.search(line)
        if not match:
            continue
        frame = int(match.group(1))
        phase = frame // 360
        within = frame % 360
        if phase >= len(POSES) or within < 100 or within >= 350:
            continue
        try:
            fields = parse_fields(match.group(3))
        except (KeyError, ValueError):
            malformed += 1
            continue
        samples[(phase, match.group(2))].append(fields)

    if malformed:
        raise SystemExit(f"Rejected {malformed} malformed FTPKT rows")
    if not samples:
        raise SystemExit("No steady-pose FTPKT rows found")

    fieldnames = list(next(iter(samples.values()))[0])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["phase", "producer", "windows"] +
                        [f"{name}_per_frame" for name in fieldnames])
        for (phase, producer), rows in sorted(samples.items()):
            medians = [statistics.median(row[name] for row in rows) / WINDOW
                       for name in fieldnames]
            writer.writerow([POSES[phase], producer, len(rows)] +
                            [f"{value:.3f}" for value in medians])
    print(args.output)


if __name__ == "__main__":
    main()
