#!/usr/bin/env python3
"""Train the deterministic Foot IK landing/stair predictor used by codegen.

The model is deliberately tiny (20 -> 16 ReLU -> 6) and dependency-free.  It
mixes domain-randomized synthetic motion with labelled PCSX2 trajectories from
``tools/data/foot-neural-real.csv``.  Collision queries remain authoritative at
runtime: learned outputs may refine a verified XZ landing point, add a bounded
amount of clearance above a raycast-proven stair lip, and firm up a down-reach
envelope the procedural solver already opened - but can never invent a
supporting surface.

Feature order, normalization and output order are EXACT twins of the runtime in
``templates.cpp`` (``applyFootIk``'s ``input[]``) and of the regression runner's
labels.  A divisor changed on one side alone silently feeds the network a
different world than it was trained on, so change all three together.

f0..f3   object vx/vz over 4, ankle vx/vz over 6
f4..f7   sole-to-ground gap over 0.55, ankle vy and previous vy over 3,
         ground delta 80 ms ahead over 0.55
f8..f11  near/far slope-removed lip residuals over 0.35, support normal x/z
f12..f15 filtered gap over 0.55, filtered vy over 3, lip memory over 0.35,
         swing phase (0..0.5 s mapped to -1..1)
f16..f19 THE LOCOMOTION GRADE: descend and ascend confidences (0..1 mapped to
         -1..1), the filtered ground drop under the path ahead over 0.55, and
         the descent reach budget the solver has earned over 0.55

t0..t1   landing residual x/z over 0.16
t2       contact confidence
t3       stair-clearance intent
t4       early-release intent
t5       descent-reach intent (how much of the proven budget to spend)
"""

from __future__ import annotations

import argparse
import csv
import math
import random
from pathlib import Path


INPUTS = 20
HIDDEN = 16
OUTPUTS = 6
# The clearance/release/reach heads are the rare, useful events; the landing
# residual is present on every sample and would otherwise dominate the loss.
TARGET_WEIGHTS = [1.0, 1.0, 0.8, 1.35, 1.35, 1.35]


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
    # The gap range reaches a whole staircase step below the sole now: that is
    # the case the level-ground bands cannot express and the reach head exists
    # for. Sampling it only to 0.75 taught the old net that a 0.5 gap was the
    # edge of the world.
    gap = rng.uniform(-0.15, 1.05)
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
    filtered_gap = clamp(gap + rng.uniform(-0.08, 0.08), -0.55, 1.05)
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

    # --- the locomotion grade ------------------------------------------------
    # Synthesized the way the runtime derives it: a slope from the root's own
    # vertical speed against its horizontal travel, a probed drop under the path
    # ahead, and the reach that combination has earned. A third of the samples
    # are explicit descents (a staircase is the case that matters), a sixth are
    # climbs, and the rest are level - including level ground that happens to
    # carry a noisy velocity, which is what stops the net from reading every
    # jitter as a step down.
    roll = rng.random()
    root_vy = 0.0
    drop_ahead = 0.0
    if roll < 0.34:  # descending
        step = rng.uniform(0.06, 0.55)
        drop_ahead = step if rng.random() < 0.85 else 0.0
        root_vy = -rng.uniform(0.1, 1.6)
    elif roll < 0.50:  # ascending
        root_vy = rng.uniform(0.1, 1.6)
        drop_ahead = 0.0
    else:  # level, sometimes noisy
        root_vy = rng.uniform(-0.12, 0.12)
        drop_ahead = rng.uniform(0.0, 0.02) if rng.random() < 0.2 else 0.0
    horizontal = max(travel_speed, 0.20)
    grade = root_vy / horizontal if travel_speed > 0.08 else 0.0
    descend = max(smoothstep(-0.06, -0.32, grade),
                  smoothstep(0.02, 0.14, drop_ahead))
    ascend = smoothstep(0.06, 0.32, grade)
    if ascend > descend:
        descend *= 1.0 - ascend
    else:
        ascend *= 1.0 - descend
    reach_cap = rng.uniform(0.25, 0.6)
    reach_budget = min(drop_ahead * descend, reach_cap)
    features.extend([
        clamp(descend * 2.0 - 1.0, -1.0, 1.0),
        clamp(ascend * 2.0 - 1.0, -1.0, 1.0),
        clamp(drop_ahead / 0.55, -1.0, 1.0),
        clamp(reach_budget / 0.55, -1.0, 1.0),
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
    # A descending BODY is as good a reason to expect contact as a descending
    # ankle: in a flat walk clip played by a falling root the shoe drops toward
    # the tread while the ankle rises in model space.
    contact = max(contact, near_contact * 0.85 * descend)
    contact = clamp(contact, 0.0, 1.0)

    obstacle = smoothstep(0.018, 0.16, lip)
    rising = smoothstep(-0.20, 0.70, vertical_v)
    clearance_intent = obstacle * (0.35 + 0.65 * rising)
    release_intent = obstacle * smoothstep(-0.08, 0.42, vertical_v)
    # Spend the reach when there is a budget, the body is going down, and the
    # foot is far enough above the surface for the level-ground bands to have
    # missed it. A tiny gap needs no envelope - the ordinary plant handles it -
    # and a budget of zero means no raycast proved anything to reach for.
    reach_intent = (smoothstep(0.01, 0.10, reach_budget) * descend *
                    smoothstep(0.04, 0.22, gap))
    reach_intent *= 1.0 - smoothstep(0.10, 0.55, max(0.0, root_vy))
    targets = [
        clamp(landing_x / 0.16, -1.0, 1.0),
        clamp(landing_z / 0.16, -1.0, 1.0),
        contact,
        clearance_intent,
        release_intent,
        clamp(reach_intent, 0.0, 1.0),
    ]
    weight = 1.0 + 1.5 * max(clearance_intent, release_intent, reach_intent)
    return features, targets, weight


def load_real(path: Path) -> list[tuple[list[float], list[float], float]]:
    """Labelled PCSX2 rows.

    Missing columns default to 0, which is what makes a recording made before a
    feature or a head existed still usable: the grade lanes read as level ground
    and the reach target as "no envelope wanted", both of which are true of the
    routes those rows were walked on.
    """
    rows: list[tuple[list[float], list[float], float]] = []
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            features = [float(row.get(f"f{i}", "0") or 0) for i in range(INPUTS)]
            targets = [float(row.get(f"t{i}", "0") or 0) for i in range(OUTPUTS)]
            weight = float(row.get("weight", "1") or 1)
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
    parser.add_argument(
        "--val-fraction",
        type=float,
        default=0.15,
        help="held-out share: a separately seeded synthetic batch plus the TAIL "
             "of the real rows (0 disables, and then no val figure is printed)",
    )
    args = parser.parse_args()
    rng = random.Random(args.seed)
    data = [synthetic_sample(rng) for _ in range(args.samples)]
    # Validation synthetic rows come from their OWN generator seed, not from a
    # split: synthetic_sample draws i.i.d., so a fresh batch is a cleaner test
    # than partitioning one - these are draws training never saw.
    val: list[tuple[list[float], list[float], float]] = []
    if args.val_fraction > 0.0:
        val_rng = random.Random(args.seed + 104729)  # a different stream
        val_count = max(1, int(args.samples * args.val_fraction))
        val = [synthetic_sample(val_rng) for _ in range(val_count)]
    default_real = Path(__file__).resolve().parent / "data" / "foot-neural-real.csv"
    real_paths = args.real if args.real is not None else (
        [default_real] if default_real.is_file() else []
    )
    real_count = 0
    for path in real_paths:
        real = load_real(path)
        real_count += len(real)
        # The rows are CONSECUTIVE PCSX2 frames, so the held-out part is the
        # TAIL - a contiguous block - and it is taken BEFORE the repeat below.
        # A random split would put frame N in training and N+1 in validation,
        # which are nearly the same sample; repeating first would put the very
        # same row in both. Either one reports a loss that means nothing.
        cut = len(real)
        if args.val_fraction > 0.0 and len(real) > 4:
            cut = len(real) - max(1, int(len(real) * args.val_fraction))
            val.extend(real[cut:])
        for _ in range(max(1, args.real_repeat)):
            data.extend(real[:cut])

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
            line = f"epoch {epoch + 1:3d}: train={total / total_weight:.7f}"
            if val:
                vs = 0.0
                vw = 0.0
                for features, target, sample_weight in val:
                    hidden_raw = [
                        b1[h] + sum(w1[h][i] * features[i] for i in range(INPUTS))
                        for h in range(HIDDEN)
                    ]
                    hid = [max(0.0, value) for value in hidden_raw]
                    out = [
                        b2[o] + sum(w2[o][h] * hid[h] for h in range(HIDDEN))
                        for o in range(OUTPUTS)
                    ]
                    for o in range(OUTPUTS):
                        vs += (sample_weight * TARGET_WEIGHTS[o]
                               * (out[o] - target[o]) ** 2 / target_weight_sum)
                    vw += sample_weight
                line += f"  val={vs / vw:.7f}  (n={len(val)})"
            print(line)

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
