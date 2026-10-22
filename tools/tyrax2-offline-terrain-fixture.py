"""Bootstrap a private offline-terrain experiment from the qualified drive.

The dump is a target arithmetic oracle, not the production exporter. It covers
every effective LOD/edge combination, independently of the driven route. A host
compiler can then factor the immutable streams before a separate runtime test.
"""
import argparse
import hashlib
import json
import shutil
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    parser.add_argument("source", type=Path)
    args = parser.parse_args()
    binding = json.loads((args.source / "native-binding.json").read_text())
    for relative, digest in binding["files"].items():
        assert sha(args.source / relative) == digest, relative
    assert sha(args.source / "game/bin/vehicle-playground.elf") == binding["elfSha256"]
    assert not args.out.exists()

    def ignore(path, names):
        return [name for name in names if name in ("obj", ".git") or
                (Path(path).name == "bin" and (name.endswith((".csv", ".tga", ".sym")) or
                 name == "log.txt" or name.startswith(("tape-", "command-", "cell-", "source-", "moving-", "world-price-"))))]

    for relative in ("game", "tyra/engine"):
        shutil.copytree(args.source / relative, args.out / relative, ignore=ignore)
    shutil.copy2(args.source / "tyra/Makefile.base", args.out / "tyra/Makefile.base")
    shutil.copy2(args.source / "authored-manifest.json", args.out / "authored-manifest.json")

    def edit(relative, old, new):
        path = args.out / relative
        text = path.read_text()
        assert text.count(old) == 1, (relative, old, text.count(old))
        path.write_text(text.replace(old, new))

    header = """#pragma once
namespace OfflineTerrain {
inline bool forcing=false;
inline int own=1, edges[4]={1,1,1,1};
}
"""
    (args.out / "game/inc/offline_terrain.hpp").write_text(header)
    physics = "game/src/gen/game_physics.gen.cpp"
    edit(physics, '#include "game_runtime.gen.hpp"', '#include "game_runtime.gen.hpp"\n#include "offline_terrain.hpp"')
    edit(physics, "  const int lod = terrainLodStep(cx, cz);", "  const int lod = OfflineTerrain::forcing ? OfflineTerrain::own : terrainLodStep(cx, cz);")
    old = "  ch.lodS = (unsigned char)terrainLodStep(cx, cz + 1);"
    edit(physics, old, old + """
  if(OfflineTerrain::forcing){
    ch.lodW=OfflineTerrain::edges[0];ch.lodE=OfflineTerrain::edges[1];
    ch.lodN=OfflineTerrain::edges[2];ch.lodS=OfflineTerrain::edges[3];
  }""")
    edit("game/inc/terrain_game.hpp", "  bool compileCompleteTerrainChunk(TerrainChunk& ch);",
         "  void dumpOfflineTerrain();\n  bool compileCompleteTerrainChunk(TerrainChunk& ch);")
    edit("game/src/terrain_game.cpp", "  WorldCellComplete::directAudit=MotionProbe::phase<6 && MotionProbe::offset==1100;",
         "  if(MotionProbe::phase==0 && MotionProbe::offset==1099)dumpOfflineTerrain();\n  WorldCellComplete::directAudit=MotionProbe::phase<6 && MotionProbe::offset==1100;")
    method = Path(__file__).with_name("tyrax2-offline-terrain-dump.inc").read_text()
    path = args.out / physics
    path.write_text(path.read_text() + "\n" + method)
    record = dict(status="UNQUALIFIED", parentElf=binding["elfSha256"],
                  generatorSha256=sha(Path(__file__)), methodSha256=sha(Path(__file__).with_name("tyrax2-offline-terrain-dump.inc")),
                  scope="All effective terrain LOD/edge combinations in initial static daylight scene; native arithmetic oracle only.",
                  limits=["No runtime fast path, offline exporter integration or performance result.",
                          "Source data is produced by the original EE generator in an emulator before host compilation."])
    (args.out / "offline-terrain-manifest.json").write_text(json.dumps(record, indent=2) + "\n")
    print(args.out)


if __name__ == "__main__":
    main()
