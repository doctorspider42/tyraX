#!/usr/bin/env python3
"""Train the deterministic Foot IK landing/stair predictor used by codegen.

The model is deliberately tiny (16 -> 16 ReLU -> 5) and dependency-free.  It
mixes domain-randomized synthetic motion with labelled PCSX2 trajectories from
``tools/data/foot-neural-real.csv``.  Collision queries remain authoritative at
runtime: learned outputs may refine a verified XZ landing point and add a
bounded amount of clearance above a raycast-proven stair lip, but can never
invent a supporting surface.
"""

from __future__ import annotations

import argparse
import csv
import math
import random
from pathlib import Path


INPUTS = 16
HIDDEN = 16
OUTPUTS = 5
TARGET_WEIGHTS = [1.0, 1.0, 0.8, 1.35, 1.35]


def clamp(value: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, value))


def smoothstep(edge0: float, edge1: float, value: float) -> float:
    t = clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def synthetic_sample(
    rng: random.Random,
) -> tuple[list[float], list[float], float]:
    object_vx = rng.uniform(-4.0, 4.0)
    object_vz = rng.uniform(-4.0, 4.0)
    foot_vx = object_vx + rng.uniform(-3.0, 3.0)
    foot_vz = object_vz + rng.uniform(-3.0, 3.0)
    gap = rng.uniform(-0.15, 0.75)
    vertical_v = rng.uniform(-3.0, 3.0)
    previous_v = clamp(vertical_v + rng.uniform(-1.2, 1.2), -3.0, 3.0)

    travel_x, travel_z = object_vx, object_vz
    travel_speed = math.hypot(travel_x, travel_z)
    if travel_speed < 0.08:
        travel_x, travel_z = foot_vx, foot_vz
        travel_speed = math.hypot(travel_x, travel_z)
    if travel_speed > 1e-6:
        dir_x, dir_z = travel_x / travel_speed, travel_z / travel_speed
    else:
        dir_x, dir_z = 0.0, 0.0

    # A continuous slope and a discrete lip deliberately share raw height
    # changes.  The two residual features remove the part explained by the
    # support plane, teaching the model that a hill is not a staircase.
    normal_x = rng.uniform(-0.42, 0.42)
    normal_z = rng.uniform(-0.42, 0.42)
    normal_y = math.sqrt(max(0.25, 1.0 - normal_x**2 - normal_z**2))
    ground_slope = -(normal_x * dir_x + normal_z * dir_z) / normal_y
    near_lead = 0.11
    far_lead = min(0.30, 0.20 + travel_speed * 0.025)
    lead_distance = math.hypot(object_vx, object_vz) * 0.08
    near_excess = rng.uniform(-0.012, 0.012)
    far_excess = rng.uniform(-0.012, 0.012)
    ahead_excess = rng.uniform(-0.012, 0.012)
    if rng.random() < 0.38:
        lip_distance = rng.uniform(0.025, 0.34)
        lip_height = rng.uniform(0.035, 0.34)
        if near_lead >= lip_distance:
            near_excess += lip_height
        if far_lead >= lip_distance:
            far_excess += lip_height
        if lead_distance >= lip_distance:
            ahead_excess += lip_height
    ahead_delta = ground_slope * lead_distance + ahead_excess

    features = [
        clamp(object_vx / 4.0, -1.0, 1.0),
        clamp(object_vz / 4.0, -1.0, 1.0),
        clamp(foot_vx / 6.0, -1.0, 1.0),
        clamp(foot_vz / 6.0, -1.0, 1.0),
        clamp(gap / 0.55, -1.0, 1.0),
        clamp(vertical_v / 3.0, -1.0, 1.0),
        clamp(previous_v / 3.0, -1.0, 1.0),
        clamp(ahead_delta / 0.55, -1.0, 1.0),
        clamp(near_excess / 0.35, -1.0, 1.0),
        clamp(far_excess / 0.35, -1.0, 1.0),
        normal_x,
        normal_z,
    ]

    # Causal short-term state mirrors the runtime EWMAs.  Domain randomization
    # includes both freshly detected and fading obstacles, so a single noisy
    # ray cannot dominate the prediction and a real stair lip survives long
    # enough to guide the rest of the swing.
    filtered_gap = clamp(gap + rng.uniform(-0.08, 0.08), -0.55, 0.55)
    filtered_v = clamp(0.62 * vertical_v + 0.38 * previous_v, -3.0, 3.0)
    lip = max(0.0, near_excess, far_excess)
    lip_memory = max(lip, rng.uniform(0.0, 1.0) * lip +
                     (rng.uniform(0.0, 0.18) if rng.random() < 0.18 else 0.0))
    phase = rng.uniform(0.0, 1.0)
    features.extend([
        clamp(filtered_gap / 0.55, -1.0, 1.0),
        clamp(filtered_v / 3.0, -1.0, 1.0),
        clamp(lip_memory / 0.35, 0.0, 1.0),
        phase * 2.0 - 1.0,
    ])

    lead_seconds = 0.055
    landing_x = (0.65 * object_vx + 0.35 * foot_vx) * lead_seconds
    landing_z = (0.65 * object_vz + 0.35 * foot_vz) * lead_seconds
    predicted_gap = gap + (0.65 * vertical_v + 0.35 * previous_v) * 0.08
    predicted_gap -= ahead_delta
    near_contact = smoothstep(0.46, -0.06, predicted_gap)
    descending = smoothstep(0.18, -1.1, vertical_v)
    contact = near_contact * (0.18 + 0.82 * descending)
    if vertical_v > 0.35:
        contact *= clamp(1.0 - (vertical_v - 0.35) / 0.8, 0.0, 1.0)

    obstacle = smoothstep(0.018, 0.16, lip)
    rising = smoothstep(-0.20, 0.70, vertical_v)
    clearance_intent = obstacle * (0.35 + 0.65 * rising)
    release_intent = obstacle * smoothstep(-0.08, 0.42, vertical_v)
    targets = [
        clamp(landing_x / 0.16, -1.0, 1.0),
        clamp(landing_z / 0.16, -1.0, 1.0),
        contact,
        clearance_intent,
        release_intent,
    ]
    weight = 1.0 + 1.5 * max(clearance_intent, release_intent)
    return features, targets, weight


def load_real(path: Path) -> list[tuple[list[float], list[float], float]]:
    rows: list[tuple[list[float], list[float], float]] = []
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            features = [float(row.get(f"f{i}", "0")) for i in range(INPUTS)]
            targets = [float(row[f"t{i}"]) for i in range(OUTPUTS)]
            weight = float(row.get("weight", "1"))
            rows.append((features, targets, weight))
    return rows


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--seed", type=int, default=2002)
    parser.add_argument("--samples", type=int, default=4500)
    parser.add_argument("--epochs", type=int, default=70)
    parser.add_argument(
        "--real",
        type=Path,
        action="append",
        help="labelled PCSX2 training-samples.csv (repeatable)",
    )
    parser.add_argument(
        "--real-repeat",
        type=int,
        default=3,
        help="number of deterministic copies of real samples mixed per epoch",
    )
    args = parser.parse_args()
    rng = random.Random(args.seed)
    data = [synthetic_sample(rng) for _ in range(args.samples)]
    default_real = Path(__file__).resolve().parent / "data" / "foot-neural-real.csv"
    real_paths = args.real if args.real is not None else (
        [default_real] if default_real.is_file() else []
    )
    real_count = 0
    for path in real_paths:
        real = load_real(path)
        real_count += len(real)
        for _ in range(max(1, args.real_repeat)):
            data.extend(real)

    scale = math.sqrt(2.0 / INPUTS)
    w1 = [[rng.uniform(-scale, scale) for _ in range(INPUTS)] for _ in range(HIDDEN)]
    b1 = [0.05 for _ in range(HIDDEN)]
    w2 = [[rng.uniform(-0.25, 0.25) for _ in range(HIDDEN)] for _ in range(OUTPUTS)]
    b2 = [0.0 for _ in range(OUTPUTS)]
    params = [w1, [b1], w2, [b2]]
    first = [[[0.0 for _ in row] for row in matrix] for matrix in params]
    second = [[[0.0 for _ in row] for row in matrix] for matrix in params]
    step = 0
    target_weight_sum = sum(TARGET_WEIGHTS)

    for epoch in range(args.epochs):
        rng.shuffle(data)
        rate = 0.005 * (0.18 + 0.82 * (1.0 - epoch / args.epochs))
        total = 0.0
        total_weight = 0.0
        for features, target, sample_weight in data:
            hidden_raw = [
                b1[h] + sum(w1[h][i] * features[i] for i in range(INPUTS))
                for h in range(HIDDEN)
            ]
            hidden = [max(0.0, value) for value in hidden_raw]
            output = [
                b2[o] + sum(w2[o][h] * hidden[h] for h in range(HIDDEN))
                for o in range(OUTPUTS)
            ]
            delta2 = [
                2.0
                * sample_weight
                * TARGET_WEIGHTS[o]
                * (output[o] - target[o])
                / target_weight_sum
                for o in range(OUTPUTS)
            ]
            total += sample_weight * sum(
                TARGET_WEIGHTS[o] * (output[o] - target[o]) ** 2
                for o in range(OUTPUTS)
            ) / target_weight_sum
            total_weight += sample_weight
            grad_w2 = [
                [delta2[o] * hidden[h] for h in range(HIDDEN)]
                for o in range(OUTPUTS)
            ]
            grad_b2 = delta2
            delta1 = [
                (
                    sum(delta2[o] * w2[o][h] for o in range(OUTPUTS))
                    if hidden_raw[h] > 0.0
                    else 0.0
                )
                for h in range(HIDDEN)
            ]
            grad_w1 = [
                [delta1[h] * features[i] for i in range(INPUTS)]
                for h in range(HIDDEN)
            ]
            grad_b1 = delta1
            grads = [grad_w1, [grad_b1], grad_w2, [grad_b2]]
            step += 1
            for matrix, grad, m1, m2 in zip(params, grads, first, second):
                for row, grow, r1, r2 in zip(matrix, grad, m1, m2):
                    for j in range(len(row)):
                        r1[j] = 0.9 * r1[j] + 0.1 * grow[j]
                        r2[j] = 0.999 * r2[j] + 0.001 * grow[j] * grow[j]
                        mh = r1[j] / (1.0 - 0.9**step)
                        vh = r2[j] / (1.0 - 0.999**step)
                        row[j] -= rate * mh / (math.sqrt(vh) + 1e-8)
        if epoch in (0, args.epochs - 1):
            print(f"epoch {epoch + 1:3d}: mse={total / total_weight:.7f}")

    def cpp_row(values: list[float]) -> str:
        return "{" + ", ".join(f"{value:.9g}F" for value in values) + "}"

    print(
        "\n// seed",
        args.seed,
        "synthetic",
        args.samples,
        "real",
        real_count,
        "repeat",
        args.real_repeat,
        "epochs",
        args.epochs,
    )
    print(f"static const float FOOT_NEURAL_W1[{HIDDEN}][{INPUTS}] = {{")
    for values in w1:
        print("  " + cpp_row(values) + ",")
    print("};")
    print(f"static const float FOOT_NEURAL_B1[{HIDDEN}] = " + cpp_row(b1) + ";")
    print(f"static const float FOOT_NEURAL_W2[{OUTPUTS}][{HIDDEN}] = {{")
    for values in w2:
        print("  " + cpp_row(values) + ",")
    print("};")
    print(f"static const float FOOT_NEURAL_B2[{OUTPUTS}] = " + cpp_row(b2) + ";")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
