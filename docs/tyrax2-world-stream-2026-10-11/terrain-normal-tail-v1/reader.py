"""Reconcile existing Hybrid2 entry intervals with actual present records.

Offline evidence only: never infer exclusive EE cycles or a capacity ceiling.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("fixture", type=Path)
p.add_argument("attempt", type=Path)
p.add_argument("output", type=Path)
a = p.parse_args()
sha = lambda f: hashlib.sha256(f.read_bytes()).hexdigest()
root, attempt = a.fixture.resolve(), a.attempt.resolve()
sealed = json.loads((root / "sha256.json").read_text())
def require_sealed(path):
    rel = str(path.relative_to(root))
    assert rel in sealed and sha(path) == sealed[rel], rel

for name in ("native-binding.json", "game/bin/vehicle-playground.elf"):
    require_sealed(root / name)
for name in ("editor-deploy.json", "world-cell-price-ps2.json"):
    require_sealed(attempt / name)
binding = json.loads((root / "native-binding.json").read_text())
for rel, digest in binding["files"].items():
    require_sealed(root / rel)
    assert sha(root / rel) == digest, rel
deploy = json.loads((attempt / "editor-deploy.json").read_text())
assert deploy["elfSha256"] == binding["elfSha256"]
assert sha(attempt / "bin/vehicle-playground.elf") == binding["elfSha256"]
qualified = json.loads((attempt / "world-cell-price-ps2.json").read_text())
assert qualified["valid"] and qualified["elfSha256"] == binding["elfSha256"]
assert qualified["variant"] in ("audio-event", "terrain-normals")
if qualified["variant"] == "terrain-normals":
    assert len(qualified["normalSources"]) == 6
    assert all(item["crossPhasePixelsExact"]
               for item in qualified["normalMovingImages"])
engine = (root / "tyra/engine/src/engine.cpp").read_text()
assert "if(MotionProbe::offset==MotionProbe::gasStart)TripleTrial::start(MotionProbe::phase);" in engine
assert "renderer.core.synchronizeFrame();renderer.core.gs.selectDiagnosticTarget(0);" in engine
assert "TripleTrial::stopAndExport();" in engine
pins = {}
def read_csv(path):
    require_sealed(path)
    pins[str(path.relative_to(attempt))] = sha(path)
    with path.open(newline="") as f:
        return list(csv.DictReader(f))

rows = read_csv(attempt / "bin/drive-probe.csv")
tick_ms = 294912
def ms(ticks):
    return ticks / tick_ms

phases = []
for phase in range(6):
    cohort = [r for r in rows if int(r["phase"]) == phase]
    gas = [r for r in cohort if int(r["gas"]) == 1]
    assert gas and all(int(r["flips"]) == 1 for r in gas)
    assert [int(r["offset"]) for r in gas] == list(range(
        int(gas[0]["offset"]), int(gas[-1]["offset"]) + 1))
    presents = read_csv(attempt / f"bin/triple-present-{phase}.csv")
    # Capture starts before the first gas loop; pending final job is drained
    # once after the last priced loop. This is specific to this fixed harness.
    assert len(presents) == len(gas) + 1
    assert all(r["slot"] == "1" and r["isr"] == "0" for r in presents)
    intervals = [ms((int(presents[i]["tick"]) -
                     int(presents[i-1]["tick"])) & 0xffffffff)
                 for i in range(1, len(presents))]
    assert all(10 < v < 100 for v in intervals)
    context = []
    for i, interval in enumerate(intervals, start=1):
        if interval <= 25:
            continue
        neighborhood = []
        for j in range(max(0, i-2), min(len(gas), i+3)):
            r = gas[j]
            prior = gas[j-1] if j else cohort[cohort.index(r)-1]
            neighborhood.append(dict(
                offset=int(r["offset"]),
                entryMs=ms(int(r["period"])),
                wholeMs=ms(int(r["whole"])),
                workMs=ms(int(r["whole"])-int(r["pacing"])),
                pacingMs=ms(int(r["pacing"])),
                updateMs=ms(int(r["update"])),
                renderMinusPacingMs=ms(int(r["render"])-int(r["pacing"])),
                z=float(r["z"]), speed=float(r["speed"]),
                candidate=int(qualified["phases"][phase]["candidate"]),
                probeCapture=int(r["probeCapture"]),
                paint=int(r["paint"]), rebuild=int(r["rebuild"]),
                entryGapAfterPreviousWholeMs=ms(int(r["period"])-int(prior["whole"])),
                presentIntervalMs=None if j == 0 else intervals[j-1]))
        context.append(dict(presentIndex=i,
                            loopOffset=None if i == len(gas) else int(gas[i]["offset"]),
                            presentIntervalMs=interval, neighborhood=neighborhood))
    phases.append(dict(phase=phase, gasRows=len(gas), presentRecords=len(presents),
                       latePresentIntervals=len(context),
                       lateEntryIntervals=sum(int(r["period"]) > tick_ms*25 for r in gas),
                       presentHz=1000*len(intervals)/sum(intervals), tails=context))

report = dict(valid=True, variant=qualified["variant"],
              readerSha256=sha(Path(__file__)),
              elfSha256=binding["elfSha256"], inputHashes=pins,
              phases=phases, exclusiveEeAttribution=False, capacityCeiling=False,
              boundaries=[
                  "Entry period in row N covers loop N-1, including its pacing.",
                  "Whole minus pacing includes consumer waits and preemption.",
                  "Pending N-1 presentation occurs during N before new DMA submission.",
                  "Hybrid2 present records are framebuffer programming after copy, not optical scanout.",
                  "Source-matched one-present-per-loop and one final drain establish capture indexing."])
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps([dict(phase=r["phase"], late=r["latePresentIntervals"],
                       hz=r["presentHz"]) for r in phases]))
