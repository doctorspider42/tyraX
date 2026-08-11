#!/usr/bin/env python3
"""Deterministic Foot IK regression/data runner for generated TyraX games.

Builds with TYRAX_FOOT_IK_TRACE=1, launches PCSX2, drives Remote Pad, captures
an image sequence, and converts PLAYERIK/FOOTIK/FOOTTRAIN log lines to CSV,
labelled neural-training samples, and a compact JSON summary.  The trace flag
is generator-only and compiles out of normal builds.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import os
import platform
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path


PLAYER_HEADER = [
    "frame",
    "player",
    "ground_y",
    "physical_y",
    "visual_y",
    "velocity_y",
    "grounded",
]
FOOT_HEADER = [
    "frame",
    "object",
    "root_y",
    "pelvis_y",
    "left_sole_y",
    "left_ground_y",
    "left_target_y",
    "left_weight",
    "left_velocity_y",
    "left_hit",
    "left_locked",
    "right_sole_y",
    "right_ground_y",
    "right_target_y",
    "right_weight",
    "right_velocity_y",
    "right_hit",
    "right_locked",
    "left_neural_confidence",
    "left_neural_applied",
    "right_neural_confidence",
    "right_neural_applied",
    "neural_vu0_ee_max_delta",
    "left_clearance",
    "left_surface_normal_y",
    "left_align_weight",
    "right_clearance",
    "right_surface_normal_y",
    "right_align_weight",
    "left_sweep_clearance",
    "right_sweep_clearance",
    "left_plan_score",
    "left_plan_applied",
    "right_plan_score",
    "right_plan_applied",
    "pelvis_shift_x",
    "pelvis_shift_z",
    "body_pitch",
    "body_roll",
    "left_down_reach_weight",
    "right_down_reach_weight",
]
TRAIN_HEADER = [
    "frame",
    "object",
    "side",
    "object_velocity_x",
    "object_velocity_z",
    "foot_velocity_x",
    "foot_velocity_y",
    "foot_velocity_z",
    "gap",
    "previous_velocity_y",
    "ahead_height_delta",
    "near_lip_excess",
    "far_lip_excess",
    "surface_normal_x",
    "surface_normal_z",
    "sole_x",
    "sole_z",
    "locked_x",
    "locked_z",
    "hit",
    "locked",
    "releasing",
    "wanted_clearance",
    "release_needed",
    "neural_clearance_intent",
    "neural_release_intent",
    "filtered_gap",
    "filtered_velocity_y",
    "lip_memory",
    "swing_time",
    "sweep_clearance",
    "plan_score",
    "plan_applied",
    "sweep_rise",
    "down_reach_weight",
    "down_reach_gap",
]
SAMPLE_HEADER = [
    *[f"f{i}" for i in range(16)],
    *[f"t{i}" for i in range(5)],
    "weight",
    "frame",
    "object",
    "side",
]


def parse_args() -> argparse.Namespace:
    repo = Path(__file__).resolve().parents[4]
    editor_name = "tyrax-editor.exe" if os.name == "nt" else "tyrax-editor"
    parser = argparse.ArgumentParser(
        description="Build, drive, capture and measure a Foot IK scenario."
    )
    parser.add_argument("project", type=Path, help="Generated TyraX project directory")
    parser.add_argument(
        "--editor",
        type=Path,
        default=repo / "build" / editor_name,
        help="tyrax-editor executable",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="Result directory (default: foot-ik-results/<timestamp>)",
    )
    parser.add_argument(
        "--pad-script",
        default="stick l 127 0; wait 8; neutral; wait 1",
        help="Remote Pad script used for the deterministic drive",
    )
    parser.add_argument("--boot-wait", type=float, default=5.0)
    parser.add_argument("--capture-seconds", type=float, default=10.0)
    parser.add_argument("--capture-every", type=float, default=0.10)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--no-capture", action="store_true")
    parser.add_argument(
        "--neural-ee",
        action="store_true",
        help="force the scalar EE inference twin instead of VU0 macro mode",
    )
    return parser.parse_args()


def run_checked(cmd: list[str], *, env: dict[str, str] | None = None) -> None:
    print("+", subprocess.list2cmdline(cmd), flush=True)
    subprocess.run(cmd, check=True, env=env)


def start_capture(
    scripts: Path, output: Path, seconds: float, every: float
) -> subprocess.Popen[str] | None:
    frames = output / "frames"
    frames.mkdir(parents=True, exist_ok=True)
    if os.name == "nt":
        cmd = [
            "powershell",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(scripts / "screenshot-window.ps1"),
            "-ProcessName",
            "pcsx2-qt",
            "-Watch",
            str(frames),
            "-Auto",
            "-Trim",
            "-Every",
            str(every),
            "-For",
            str(seconds),
            "-Tile",
            "240",
            "-Sheet",
            "contact-sheet.png",
        ]
    elif platform.system() == "Linux":
        cmd = [
            sys.executable,
            str(scripts / "wayland-control.py"),
            "watch",
            str(frames),
            "--auto",
            "--every",
            str(every),
            "--for",
            str(seconds),
            "--tile",
            "240",
            "--sheet",
            "contact-sheet.png",
        ]
    else:
        print("warning: visual capture is unsupported on this OS", file=sys.stderr)
        return None
    print("+", subprocess.list2cmdline(cmd), flush=True)
    return subprocess.Popen(cmd, text=True)


def extract_rows(
    log_path: Path,
) -> tuple[list[list[float]], list[list[float]], list[list[float]]]:
    players: list[list[float]] = []
    feet: list[list[float]] = []
    training: list[list[float]] = []
    if not log_path.exists():
        return players, feet, training
    for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines():
        for marker, header, output in (
            ("PLAYERIK,", PLAYER_HEADER, players),
            ("FOOTIK,", FOOT_HEADER, feet),
            ("FOOTTRAIN,", TRAIN_HEADER, training),
        ):
            pos = line.find(marker)
            if pos < 0:
                continue
            fields = line[pos:].strip().split(",")[1:]
            if len(fields) < len(header):
                continue
            try:
                output.append([float(value) for value in fields[: len(header)]])
            except ValueError:
                pass
    return players, feet, training


def write_csv(path: Path, header: list[str], rows: list[list[float]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(header)
        writer.writerows(rows)


def max_step(values: list[float]) -> float:
    return max((abs(b - a) for a, b in zip(values, values[1:])), default=0.0)


def clamp(value: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, value))


def training_samples(rows: list[list[float]]) -> list[list[float]]:
    """Turn causal runtime frames into short-horizon supervised examples.

    Each unlocked frame looks at most 18 game frames forward for the next real
    lock edge and learns its verified XZ contact point.  Stair labels come from
    the procedural raycast controller's wanted clearance/release decisions;
    those remain the runtime safety gate as well.
    """
    groups: dict[tuple[int, int], list[list[float]]] = {}
    for row in rows:
        groups.setdefault((int(row[1]), int(row[2])), []).append(row)
    output: list[list[float]] = []
    for group in groups.values():
        group.sort(key=lambda row: row[0])
        for i, row in enumerate(group):
            planted = row[20] > 0.5
            # A planted foot no longer consults the landing predictor.  Keeping
            # hundreds of those nearly identical frames taught the confidence
            # head to shout "contact" constantly; retain only the rare planted
            # row that asks the stair controller for an emergency release.
            if planted and row[23] <= 0.5:
                continue
            features = [
                clamp(row[3] / 4.0, -1.0, 1.0),
                clamp(row[4] / 4.0, -1.0, 1.0),
                clamp(row[5] / 6.0, -1.0, 1.0),
                clamp(row[7] / 6.0, -1.0, 1.0),
                clamp(row[8] / 0.55, -1.0, 1.0),
                clamp(row[6] / 3.0, -1.0, 1.0),
                clamp(row[9] / 3.0, -1.0, 1.0),
                clamp(row[10] / 0.55, -1.0, 1.0),
                clamp(row[11] / 0.35, -1.0, 1.0),
                clamp(row[12] / 0.35, -1.0, 1.0),
                clamp(row[13], -1.0, 1.0),
                clamp(row[14], -1.0, 1.0),
                clamp(row[26] / 0.55, -1.0, 1.0),
                clamp(row[27] / 3.0, -1.0, 1.0),
                clamp(row[28] / 0.35, 0.0, 1.0),
                clamp(row[29] / 0.25 - 1.0, -1.0, 1.0),
            ]
            contact = 0.0
            lock_x, lock_z = row[17], row[18]
            if not planted:
                for j in range(i + 1, len(group)):
                    future = group[j]
                    delta = future[0] - row[0]
                    if delta > 18:
                        break
                    previous_locked = group[j - 1][20] > 0.5
                    if future[20] > 0.5 and not previous_locked:
                        t = clamp(1.0 - delta / 14.0, 0.0, 1.0)
                        contact = t * t * (3.0 - 2.0 * t)
                        lock_x, lock_z = future[17], future[18]
                        break
            clearance = clamp(row[22] / 0.20, 0.0, 1.0)
            release = 1.0 if row[23] > 0.5 else 0.0
            targets = [
                clamp((lock_x - row[15]) / 0.16, -1.0, 1.0)
                if contact > 0.0
                else 0.0,
                clamp((lock_z - row[16]) / 0.16, -1.0, 1.0)
                if contact > 0.0
                else 0.0,
                contact,
                clearance,
                release,
            ]
            weight = 0.35 + contact * 1.65 + clearance * 3.0 + release * 3.0
            output.append(
                [*features, *targets, weight, row[0], row[1], row[2]]
            )
    return output


def summarize(
    players: list[list[float]],
    feet: list[list[float]],
    training: list[list[float]],
    samples: list[list[float]],
) -> dict[str, object]:
    physical = [row[3] for row in players]
    visual = [row[4] for row in players]
    physical_step = max_step(physical)
    visual_step = max_step(visual)

    contact_steps: list[float] = []
    correction_acceleration: list[float] = []
    contact_transitions = 0
    for side in (0, 1):
        sole_i = 4 + side * 7
        target_i = sole_i + 2
        weight_i = sole_i + 3
        lock_i = sole_i + 6
        previous_correction: float | None = None
        previous_step: float | None = None
        previous_locked = False
        for row in feet:
            correction = (row[target_i] - row[sole_i]) * row[weight_i]
            if previous_correction is not None:
                step = correction - previous_correction
                contact_steps.append(abs(step))
                if previous_step is not None:
                    correction_acceleration.append(abs(step - previous_step))
                previous_step = step
            locked = row[lock_i] > 0.5
            if locked and not previous_locked:
                contact_transitions += 1
            previous_correction = correction
            previous_locked = locked

    # Contact-policy diagnostics come from FOOTTRAIN rather than the weighted
    # output. They catch two visually loud regressions which a root/ankle jerk
    # metric can miss: planting a fast airborne swing, and asking the stair
    # footprint sweep for clearance when every sample is on the same flat plane.
    fast_contact_transition_frames = 0
    unsupported_sweep_clearance_frames = 0
    rapid_replant_frames = 0
    previous_locked_by_leg: dict[tuple[int, int], bool] = {}
    last_contact_by_leg: dict[tuple[int, int], int] = {}
    last_contact_moving_by_leg: dict[tuple[int, int], bool] = {}
    last_down_reach_by_leg: dict[tuple[int, int], int] = {}
    down_reach_contact_transitions = 0
    for row in training:
        if len(row) < 33:
            continue
        frame, obj, side = int(row[0]), int(row[1]), int(row[2])
        key = (obj, side)
        locked = row[20] > 0.5
        was_locked = previous_locked_by_leg.get(key, False)
        if len(row) >= 36 and row[34] > 0.01:
            last_down_reach_by_leg[key] = frame
        if locked and not was_locked:
            object_speed = math.hypot(row[3], row[4])
            foot_speed = math.hypot(row[5], row[7])
            if foot_speed > max(0.55, object_speed * 0.70):
                fast_contact_transition_frames += 1
            previous_contact = last_contact_by_leg.get(key)
            if (previous_contact is not None
                    and last_contact_moving_by_leg.get(key, False)
                    and frame - previous_contact <= 6):
                rapid_replant_frames += 1
            last_contact_by_leg[key] = frame
            last_contact_moving_by_leg[key] = object_speed > 0.08
            last_down_reach = last_down_reach_by_leg.get(key)
            if last_down_reach is not None and frame - last_down_reach <= 12:
                down_reach_contact_transitions += 1
        previous_locked_by_leg[key] = locked
        if len(row) >= 34 and row[30] > 0.001 and row[33] <= 0.025:
            unsupported_sweep_clearance_frames += 1

    return {
        "player_samples": len(players),
        "foot_samples": len(feet),
        "training_frames": len(training),
        "training_samples": len(samples),
        "contact_transitions": contact_transitions,
        "fast_contact_transition_frames": fast_contact_transition_frames,
        "rapid_replant_frames": rapid_replant_frames,
        "unsupported_sweep_clearance_frames": unsupported_sweep_clearance_frames,
        "down_reach_frames": sum(
            1 for row in training if len(row) >= 36 and row[34] > 0.01
        ),
        "down_reach_contact_transitions": down_reach_contact_transitions,
        "max_down_reach_weight": max(
            (row[34] for row in training if len(row) >= 36), default=0.0
        ),
        "max_down_reach_gap": max(
            (row[35] for row in training
             if len(row) >= 36 and row[34] > 0.01),
            default=0.0,
        ),
        "max_physical_root_step": physical_step,
        "max_visual_root_step": visual_step,
        "root_step_reduction_ratio": (
            visual_step / physical_step if physical_step > 1e-8 else 0.0
        ),
        "max_contact_correction_step": max(contact_steps, default=0.0),
        "max_contact_correction_jerk": max(correction_acceleration, default=0.0),
        "neural_applied_frames": sum(
            1 for row in feet if len(row) >= 22 and (row[19] > 0.5 or row[21] > 0.5)
        ),
        "max_neural_confidence": max(
            (max(row[18], row[20]) for row in feet if len(row) >= 21),
            default=0.0,
        ),
        "neural_vu0_ee_max_delta": max(
            (row[22] for row in feet if len(row) >= 23), default=0.0
        ),
        "max_swing_clearance": max(
            (max(row[23], row[26]) for row in feet if len(row) >= 29),
            default=0.0,
        ),
        "minimum_surface_normal_y": min(
            (min(row[24], row[27]) for row in feet if len(row) >= 29),
            default=1.0,
        ),
        "aligned_foot_frames": sum(
            1 for row in feet if len(row) >= 29 and (row[25] > 0.01 or row[28] > 0.01)
        ),
        "max_neural_clearance_intent": max(
            (row[24] for row in training), default=0.0
        ),
        "max_neural_release_intent": max(
            (row[25] for row in training), default=0.0
        ),
        "max_swept_foot_clearance": max(
            (max(row[29], row[30]) for row in feet if len(row) >= 31),
            default=0.0,
        ),
        "planned_contact_frames": sum(
            1 for row in feet if len(row) >= 35 and (row[32] > 0.5 or row[34] > 0.5)
        ),
        "max_pelvis_support_shift": max(
            (math.hypot(row[35], row[36]) for row in feet if len(row) >= 37),
            default=0.0,
        ),
        "max_body_tilt_radians": max(
            (max(abs(row[37]), abs(row[38])) for row in feet if len(row) >= 39),
            default=0.0,
        ),
        "max_target_penetration": max(
            (max(0.0, (row[5] - row[6]) * row[7],
                 (row[12] - row[13]) * row[14])
             for row in feet if len(row) >= 15),
            default=0.0,
        ),
    }


def main() -> int:
    args = parse_args()
    project = args.project.resolve()
    editor = args.editor.resolve()
    if not project.is_dir():
        raise SystemExit(f"project directory does not exist: {project}")
    if not editor.is_file():
        raise SystemExit(f"editor executable does not exist: {editor}")

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    output = (args.output or Path.cwd() / "foot-ik-results" / stamp).resolve()
    output.mkdir(parents=True, exist_ok=True)
    scripts = Path(__file__).resolve().parent

    env = os.environ.copy()
    env["TYRAX_FOOT_IK_TRACE"] = "1"
    if args.neural_ee:
        env["TYRAX_FOOT_NEURAL_EE"] = "1"
    if not args.skip_build:
        run_checked([str(editor), "--build", str(project), "--run"], env=env)

    print(f"waiting {args.boot_wait:.1f}s for PCSX2 boot", flush=True)
    time.sleep(args.boot_wait)

    capture = None
    if not args.no_capture:
        capture = start_capture(
            scripts, output, args.capture_seconds, args.capture_every
        )
        time.sleep(0.35)

    run_checked([str(editor), "--pad", str(project), args.pad_script])
    if capture is not None:
        capture_code = capture.wait()
        if capture_code:
            print(f"warning: capture exited with {capture_code}", file=sys.stderr)

    time.sleep(0.5)
    players, feet, training = extract_rows(project / "bin" / "log.txt")
    if not players or not feet:
        raise SystemExit(
            "no Foot IK telemetry found in bin/log.txt; ensure the generated "
            "terrain_game.cpp is editor-owned and the test build was not skipped"
        )

    write_csv(output / "player-root.csv", PLAYER_HEADER, players)
    write_csv(output / "feet.csv", FOOT_HEADER, feet)
    write_csv(output / "training-frames.csv", TRAIN_HEADER, training)
    samples = training_samples(training)
    write_csv(output / "training-samples.csv", SAMPLE_HEADER, samples)
    summary = summarize(players, feet, training, samples)
    (output / "summary.json").write_text(
        json.dumps(summary, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(summary, indent=2))
    print(f"results: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
