"""Reconstruct A01 work amplification from frozen evidence, without target timers."""
import argparse
import collections
import copy
import csv
import hashlib
import json
import re
from pathlib import Path


def read_csv(path):
    with path.open(newline="") as stream:
        return [{k: float(v) for k, v in row.items()} for row in csv.DictReader(stream)]


def road_ledger(bags, frames, main, names):
    windows = {1320 + cycle * 240 + start + i
               for cycle in range(3) for start in (12, 24, 180) for i in range(8)}
    assert {int(r["offset"]) for r in bags} == windows
    assert len(bags) < 4096
    counter = lambda r, name: r["c" + str(names.index(name))]
    frame_index = {(int(r["phase"]), int(r["offset"])): r for r in frames}
    main_index = {(int(r["phase"]), int(r["offset"])): r for r in main}
    assert len(frame_index) == len(frames) and len(main_index) == len(main)
    grouped = collections.defaultdict(list)
    for r in bags:
        assert all(v >= 0 and v == int(v) for v in r.values())
        assert r["phase"] == 1 and r["id"] > 0 and r["view"] in (0, 1, 2)
        assert r["vertices"] > 0 and counter(r, "Bags") == 1
        assert 0 <= r["wait"] <= r["elapsed"]
        grouped[int(r["offset"])].append(r)

    def totals(rows):
        c = {name: int(sum(counter(r, name) for r in rows)) for name in names}
        vertices = int(sum(r["vertices"] for r in rows))
        submitted = c["CullVerts"] + c["ClipVerts"]
        chunks = c["CullChunks"] + c["ClipChunks"]
        return dict(bagCalls=len(rows), bagInputVertices=vertices,
                    qbuffers=chunks, submittedVertices=submitted,
                    clipQbuffers=c["ClipChunks"], clipVertices=c["ClipVerts"],
                    submittedPerBagInput=submitted / vertices if vertices else 0,
                    verticesPerQbuffer=submitted / chunks if chunks else 0,
                    elapsedMs=sum(r["elapsed"] for r in rows) / 294912,
                    counters=c)

    result = []
    for offset, rows in sorted(grouped.items()):
        frame, whole = frame_index[1, offset], main_index[1, offset]
        assert frame["errors"] == 0 and len(rows) == whole["coreCalls"]
        assert sum(r["elapsed"] for r in rows) <= whole["whole"] - whole["pacing"]
        views = []
        for view, label in enumerate(("main", "reflection", "secondary")):
            selected = [r for r in rows if r["view"] == view]
            if not selected:
                continue
            ids = sorted({int(r["id"]) for r in selected})
            views.append(dict(view=label, **totals(selected), roads=[
                dict(id=i, **totals([r for r in selected if r["id"] == i])) for i in ids]))
        result.append(dict(offset=offset, capture=bool(whole["probeCapture"]),
                           workMs=(whole["whole"] - whole["pacing"]) / 294912,
                           nativeFlushes=int(frame["flushCalls"]),
                           **totals(rows), views=views))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    archive = repo / "docs/tyrax2-road-trace-2026-10-08/accepted-v3"
    producer = repo / "docs/tyrax2-producer-2026-10-08/physical/producer-analysis.json"
    paths = [archive / "physical" / name for name in
             ("road-bags.csv", "road-frames.csv", "drive-probe.csv")]
    header = archive / "tyra/engine/inc/debug/motion_probe.hpp"
    manifest = json.loads((archive / "evidence-sha256.json").read_text())
    for path in paths + [header]:
        assert hashlib.sha256(path.read_bytes()).hexdigest() == manifest[path.relative_to(archive).as_posix()]
    names = [s.strip() for s in re.search(r"enum Counter \{(.*?)\};",
                                         header.read_text(), re.S)[1].split(",")][:-1]
    bags, frames, whole = map(read_csv, paths)
    ledger = road_ledger(bags, frames, whole, names)
    rejected = []
    if args.selftest:
        for label, field, value in (("lost owner", "id", 0), ("invalid view", "view", 4),
                                    ("wrong phase", "phase", 0), ("wrong window", "offset", 1400),
                                    ("negative vertices", "vertices", -1)):
            broken = copy.deepcopy(bags)
            broken[0][field] = value
            try:
                road_ledger(broken, frames, whole, names)
            except AssertionError:
                rejected.append(label)
            else:
                raise RuntimeError("Accepted " + label)
        try:
            road_ledger(bags[1:], frames, whole, names)
        except AssertionError:
            rejected.append("missing bag")
        else:
            raise RuntimeError("Accepted missing bag")
    historical = json.loads(producer.read_text())
    selected = [r for r in historical["movement"] if r["phase"] in (1, 11)]
    stationary = [r for r in historical["attribution"]
                  if r["phase"] in (1, 11) and r["bin"] == 0]
    events = []
    for frame in ledger:
        for view in frame["views"]:
            for road in view["roads"]:
                if road["elapsedMs"] <= .6:
                    continue
                peers = []
                for later in ledger:
                    if later["offset"] not in (frame["offset"] + 240, frame["offset"] + 480):
                        continue
                    for v in later["views"]:
                        if v["view"] == view["view"]:
                            peers += [dict(offset=later["offset"], **r) for r in v["roads"]
                                      if r["id"] == road["id"]]
                events.append(dict(offset=frame["offset"], view=view["view"],
                                   **road, sameAngleLaterCycles=peers))
    result = dict(
        scope="P06 physical parked-car turn road-only exact frames; historical producer census separate",
        limitations=["No per-triangle planes, material/pass IDs or exact raster visibility in P06.",
                     "Bag input counts repeat across views/passes; not unique scene vertices.",
                     "Qbuffers are logical VU chunks, including derived replay counts, NOT EE allocations or DMA launches. Vertex counts are not wire bytes.",
                     "Historical producer census predates terrain refinement; never add its costs to P06.",
                     "No new target run or production fix; full current-scene lineage remains incomplete."],
        sha256={p.relative_to(repo).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in paths + [header, producer]},
        negativeControlsRejected=rejected, roadFrames=ledger, roadEventsAbovePoint6Ms=events,
        historicalProducerMovement=selected, historicalProducerStationary=stationary)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "scene-ledger.json").write_text(json.dumps(result, indent=2) + "\n")
    lines = ["# A01 scene work ledger", "",
             "This offline reconstruction separates recorded work by view and preserves evidence provenance.",
             "", "## P06 Physical Road Frames", "",
             "Parked-car camera turn, original roads, private VU paint. These are actual frames, not window averages.",
             "Qbuffer columns count logical chunks, including whole-bag replay; they do NOT count EE allocations or individual dispatch calls.",
             "", "| Offset | Capture | View | Bags | Input vertices | Qbuffers | Clip qbuffers | Submitted vertices | Vertices/qbuffer |",
             "|---|---|---|---|---|---|---|---|---|"]
    for row in ledger:
        for view in row["views"]:
            lines.append(f'| {row["offset"]} | {int(row["capture"])} | {view["view"]} | '
                         f'{view["bagCalls"]} | {view["bagInputVertices"]} | {view["qbuffers"]} | '
                         f'{view["clipQbuffers"]} | {view["submittedVertices"]} | {view["verticesPerQbuffer"]:.2f} |')
    lines += ["", "## Individual Road Calls Above 0.6 ms", "",
              "Selection is descriptive, not a new performance threshold. Same-angle later cycles are not a controlled cold-build intervention.",
              "", "| Offset | View | ID | Core ms | Clip chunks | New entry | Later-cycle ms |",
              "|---|---|---|---|---|---|---|"]
    for event in events:
        peers = ", ".join(f'{p["elapsedMs"]:.3f}' for p in event["sameAngleLaterCycles"])
        lines.append(f'| {event["offset"]} | {event["view"]} | {event["id"]} | '
                     f'{event["elapsedMs"]:.3f} | {event["clipQbuffers"]} | '
                     f'{event["counters"]["MissNew"]} | {peers} |')
    lines += ["", "## Historical Whole-Scene Census", "",
              "Older build, BEFORE integrated terrain refinement. Context only, not current-road attribution.",
              "", "| Workload | Owner | View | Bags/frame | Cull verts/frame | Clip verts/frame | Clip chunks/frame |",
              "|---|---|---|---|---|---|---|"]
    for label, rows in (("stationary", stationary), ("movement", selected)):
        for row in rows:
            m = row["means"]
            lines.append(f'| {"drive" if row["phase"] == 11 else "rotation"} {label} | '
                         f'{row["producer"]} | {row["view"]} | {m["bags"]:.2f} | '
                         f'{m["cullVerts"]:.2f} | {m["clipVerts"]:.2f} | {m["clipChunks"]:.2f} |')
    lines += ["", "## Interpretation Limits", ""] + ["- " + s for s in result["limitations"]]
    lines += ["", "The JSON includes each road ID, all original counters and SHA-256 hashes of inputs.", ""]
    (args.output / "scene-ledger.md").write_text("\n".join(lines))
    print(json.dumps(dict(frames=len(ledger), bags=len(bags), rejected=rejected,
                         output=str(args.output)), indent=2))


if __name__ == "__main__":
    main()
