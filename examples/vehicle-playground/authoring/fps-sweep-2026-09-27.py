"""Move the Motor District benchmark camera through two roads.

Run after benchmark-district.py and before --build/--refresh-gen. The four
360-frame phases keep the stock day/night switch and CSV sampler, but move
the camera on every frame. The 120-frame warmup per phase means the recorded
window covers the latter two thirds of each route.
"""
import argparse
import json
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("project", type=Path)
a = p.parse_args()
assert (a.project / "BENCHMARK.json").exists(), "Use a benchmark-district.py fixture"
source = a.project / "src/scripts/zz_district_benchmark.cpp"
text = source.read_text(encoding="utf-8")
old = (
    "    ctx.cameraEye = phase < 2 ? Tyra::Vec4(0,4,-32,1) : Tyra::Vec4(4,9,102,1);\n"
    "    ctx.cameraAt = phase < 2 ? Tyra::Vec4(0,1,-12,1) : Tyra::Vec4(65,3,106,1);"
)
new = (
    "    const float travel = (float)(frame % 360) / 359.0F;\n"
    "    ctx.cameraEye = phase < 2 ? Tyra::Vec4(0,4,-74 + 162 * travel,1) : Tyra::Vec4(4 + 95 * travel,9,102,1);\n"
    "    ctx.cameraAt = phase < 2 ? Tyra::Vec4(0,1,-54 + 162 * travel,1) : Tyra::Vec4(25 + 95 * travel,3,106,1);"
)
assert text.count(old) == 1, "Expected the unmodified Motor District sampler"
source.write_text(text.replace(old, new), encoding="utf-8")
manifest = a.project / "BENCHMARK.json"
settings = json.loads(manifest.read_text(encoding="utf-8"))
settings["cameraSweep"] = "garage +Z -74..88; outer +X 4..99; day/night each; samples after frame 120"
manifest.write_text(json.dumps(settings, indent=2) + "\n", encoding="utf-8")
print(source)
