#!/usr/bin/env python3
"""Run a multi-direction Foot IK torture course and merge its training data."""

from __future__ import annotations

import argparse
import csv
import json
import os
import subprocess
import sys
from datetime import datetime
from pathlib import Path


ROUTES = (
    ("forward", "stick l 0 -127; wait 8; neutral; wait 1"),
    ("lateral", "stick l 127 0; wait 8; neutral; wait 1"),
    ("diagonal", "stick l 90 -90; wait 8; neutral; wait 1"),
    ("reverse", "stick l 0 127; wait 8; neutral; wait 1"),
)


def main() -> int:
    repo = Path(__file__).resolve().parents[4]
    editor_name = "tyrax-editor.exe" if os.name == "nt" else "tyrax-editor"
    parser = argparse.ArgumentParser(
        description="Run four deterministic Foot IK routes and merge the results."
    )
    parser.add_argument("project", type=Path)
    parser.add_argument("--editor", type=Path, default=repo / "build" / editor_name)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--capture", action="store_true")
    parser.add_argument("--neural-ee", action="store_true")
    args = parser.parse_args()

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    output = (args.output or Path.cwd() / "foot-ik-gauntlet" / stamp).resolve()
    output.mkdir(parents=True, exist_ok=True)
    runner = Path(__file__).with_name("foot-ik-regression.py")
    summaries: dict[str, object] = {}
    sample_header: list[str] | None = None
    combined_samples: list[list[str]] = []

    for name, pad_script in ROUTES:
        route_output = output / name
        command = [
            sys.executable,
            str(runner),
            str(args.project.resolve()),
            "--editor",
            str(args.editor.resolve()),
            "--output",
            str(route_output),
            "--pad-script",
            pad_script,
        ]
        if not args.capture:
            command.append("--no-capture")
        if args.neural_ee:
            command.append("--neural-ee")
        print("+", subprocess.list2cmdline(command), flush=True)
        subprocess.run(command, check=True)
        summaries[name] = json.loads(
            (route_output / "summary.json").read_text(encoding="utf-8")
        )
        with (route_output / "training-samples.csv").open(
            newline="", encoding="utf-8"
        ) as handle:
            rows = list(csv.reader(handle))
        if rows:
            sample_header = sample_header or rows[0]
            combined_samples.extend(rows[1:])

    maxima: dict[str, float] = {}
    totals: dict[str, int] = {}
    for summary in summaries.values():
        assert isinstance(summary, dict)
        for key, value in summary.items():
            if not isinstance(value, (int, float)):
                continue
            if key.endswith("_samples") or key.endswith("_frames") or key == "contact_transitions":
                totals[key] = totals.get(key, 0) + int(value)
            elif key.startswith("max_") or key.startswith("neural_vu0"):
                maxima[key] = max(maxima.get(key, float("-inf")), float(value))

    if sample_header is not None:
        with (output / "training-samples.csv").open(
            "w", newline="", encoding="utf-8"
        ) as handle:
            writer = csv.writer(handle)
            writer.writerow(sample_header)
            writer.writerows(combined_samples)
    aggregate = {
        "routes": summaries,
        "totals": totals,
        "maxima": maxima,
        "combined_training_samples": len(combined_samples),
    }
    (output / "gauntlet-summary.json").write_text(
        json.dumps(aggregate, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(aggregate, indent=2))
    print(f"gauntlet results: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
