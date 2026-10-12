"""Audit terrain companion eligibility in the qualified skip-A31 fixture.

This is source/log evidence, not a timing measurement or a VU fusion test.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def initializer(text, name):
    match = re.search(r"\b" + re.escape(name) + r"\s*\[[^\]]*\]\s*=\s*\{([^{}]*)\};", text)
    assert match, "Missing generated array: " + name
    return [int(value.strip()) for value in match[1].split(",")]


def audit(root, attempt):
    binding = json.loads((root / "native-binding.json").read_text())
    for relative, digest in binding["files"].items():
        assert sha(root / relative) == digest, "Changed source: " + relative
    assert sha(root / "game/bin/vehicle-playground.elf") == binding["elfSha256"]
    price = json.loads((attempt / "world-cell-price-ps2.json").read_text())
    assert price["valid"] and price["platform"] == "ps2"
    assert price["elfSha256"] == binding["elfSha256"]
    log = attempt / "console.log"
    assert sha(log) == price["logSha256"], "Changed physical log"
    ao = (root / "game/inc/ao_data.gen.hpp").read_text()
    textures = (root / "game/inc/texture_data.gen.hpp").read_text()
    physics = (root / "game/src/gen/game_physics.gen.cpp").read_text()
    occurrence = initializer(ao, "SCENE_AO_MAP_OCCS")
    emission = initializer(ao, "SCENE_AO_MAP_LITS")
    layers = initializer(textures, "TERRAIN_LAYER_COUNTS")
    # Deliberately narrow: reject a changed fixture rather than generalize this finding.
    assert occurrence == emission == [0, 0, 0]
    assert layers == [0, 0, 1]
    assert "if (!ch.aoSts.empty() && terrainMapOcc && layerInfoBag)" in physics
    assert "if (!ch.aoSts.empty() && terrainMapLit && lightAddInfoBag)" in physics
    assert "const float kLift = 0.02F;" in physics
    assert "ch.gsVerts.bind(ch.gsBag)" in physics
    counts = [tuple(map(int, row)) for row in re.findall(
        r"A44TERRAIN compile=PASS cells=(\d+) passes=(\d+)",
        log.read_bytes().decode("latin-1"))]
    assert counts and set(counts) == {(20, 36), (25, 43)}
    return dict(
        valid=True, elfSha256=binding["elfSha256"],
        physicalLogSha256=sha(log), sourceFilesVerified=len(binding["files"]),
        terrainMapOcclusionByScene=occurrence, terrainMapEmissionByScene=emission,
        terrainLayersByScene=layers, observedCellPassCounts=sorted(set(counts)),
        scope="Qualified day driving fixture, scenes0/1 have no same-geometry terrain companions.",
        finding="Remaining terrain companions are separate lifted ground-shadow subsets; do not fuse as identical positions.",
        limits=["Not a measured fusion saving or scene60fps ceiling.",
                "Does not reject future layer/AO maps, vehicle multipass, or all road offload."])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fixture", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("--attempt", type=Path)
    args = parser.parse_args()
    result = audit(args.fixture, args.attempt or args.fixture / "physical-v1")
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
