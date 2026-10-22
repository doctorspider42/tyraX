"""Build a private static-shadow material composition trial, optionally with offline terrain."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("out", type=Path)
    p.add_argument("source", type=Path)
    p.add_argument("materials", type=Path)
    p.add_argument("--terrain", action="store_true")
    p.add_argument("--terrain-with-material", action="store_true", help="Compare full reference against both changes together")
    a = p.parse_args()
    assert not (a.terrain and a.terrain_with_material)
    binding = json.loads((a.source / "native-binding.json").read_text())
    for rel, digest in binding["files"].items():
        assert sha(a.source / rel) == digest, rel
    assert sha(a.source / "game/bin/vehicle-playground.elf") == binding["elfSha256"]
    material = json.loads((a.materials / "manifest.json").read_text())
    assert material["resolution"] == 128 and not material["qualityEquivalent"]
    assert not a.out.exists()

    def ignore(path, names):
        return [n for n in names if n in ("obj", ".git") or
                (Path(path).name == "bin" and (n.endswith((".csv", ".tga", ".sym")) or n == "log.txt" or
                 n.startswith(("tape-", "command-", "cell-", "source-", "moving-", "world-price-"))))]

    for rel in ("game", "tyra/engine"):
        shutil.copytree(a.source / rel, a.out / rel, ignore=ignore)
    for rel in ("tyra/Makefile.base", "authored-manifest.json"):
        shutil.copy2(a.source / rel, a.out / rel)
    if (a.source / "offline-terrain-ps2.bin").exists():
        shutil.copy2(a.source / "offline-terrain-ps2.bin", a.out / "offline-terrain-ps2.bin")
    target = a.out / "game/bin/composed"
    target.mkdir()
    for page in material["pages"]:
        assert sha(a.materials / page["file"]) == page["sha256"]
        shutil.copy2(a.materials / page["file"], target / page["file"])
    header = '''#pragma once
namespace Tyra { class Texture; }
namespace MaterialCompose {
inline bool enabled=false;
inline unsigned generation=~0U,applied=0,removedVertices=0;
inline Tyra::Texture* textures[25]={};
inline const char* paths[25]={
'''
    paths = {(page["cx"], page["cz"]): "composed/" + page["file"] for page in material["pages"]}
    header += ",\n".join('"'+paths[(x,z)]+'"' if (x,z) in paths else "nullptr" for z in range(5) for x in range(5))
    header += "\n};\n}\n"
    (a.out / "game/inc/material_compose.hpp").write_text(header)

    def edit(rel, old, new):
        f = a.out / rel
        text = f.read_text()
        assert text.count(old) == 1, (rel, old, text.count(old))
        f.write_text(text.replace(old, new))

    edit("game/inc/terrain_game.hpp", "  void auditOfflineTerrain();", "  void auditOfflineTerrain();\n  void prepareComposedTerrain();\n  void applyComposedTerrain(TerrainChunk& ch);")
    edit("game/inc/world_moving.hpp", '#include "offline_terrain.hpp"', '#include "offline_terrain.hpp"\n#include "material_compose.hpp"')
    terrain_mode = "p<6 && modes[p]" if a.terrain_with_material else str(a.terrain).lower()
    edit("game/inc/world_moving.hpp", "OfflineTerrain::enabled=p<6 && modes[p];", "OfflineTerrain::enabled="+terrain_mode+";MaterialCompose::enabled=p<6 && modes[p];")
    edit("game/inc/world_moving.hpp", "OfflineTerrain::restores=OfflineTerrain::fallbacks=0;", "OfflineTerrain::restores=OfflineTerrain::fallbacks=0;MaterialCompose::applied=MaterialCompose::removedVertices=0;")
    edit("game/inc/world_moving.hpp", 'variant=offline-terrain\\n",p,unsigned(OfflineTerrain::enabled));', 'variant=material-compose\\n",p,unsigned(MaterialCompose::enabled));')
    edit("game/inc/world_moving.hpp", "  WheelVu::witness(phase);AudioIdle::witness(phase);", '  WheelVu::witness(phase);AudioIdle::witness(phase);\n  printf("MATERIALPHASE phase=%u enabled=%u terrain=%u applied=%u removedVertices=%u\\n",phase,unsigned(MaterialCompose::enabled),unsigned(OfflineTerrain::enabled),MaterialCompose::applied,MaterialCompose::removedVertices);')
    game = "game/src/terrain_game.cpp"
    edit(game, "{OfflineTerrain::load();auditOfflineTerrain();}", "{OfflineTerrain::load();prepareComposedTerrain();auditOfflineTerrain();MaterialCompose::applied=MaterialCompose::removedVertices=0;}")
    edit(game, "  const bool movingOffline=OfflineTerrain::enabled;", "  const bool movingOffline=OfflineTerrain::enabled;\n  const bool movingMaterial=MaterialCompose::enabled;")
    held_terrain = "arm==1?!movingOffline:movingOffline" if a.terrain_with_material else "movingOffline"
    edit(game, "OfflineTerrain::enabled=arm==1?!movingOffline:movingOffline;", "OfflineTerrain::enabled="+held_terrain+";MaterialCompose::enabled=arm==1?!movingMaterial:movingMaterial;")
    edit(game, "OfflineTerrain::enabled=movingOffline;\n", "OfflineTerrain::enabled=movingOffline;MaterialCompose::enabled=movingMaterial;\n")
    edit(game, "  if(textures.empty())return false;", "  if(MaterialCompose::generation==sceneGeneration)for(auto* texture:MaterialCompose::textures)addTexture(texture);\n  if(textures.empty())return false;")
    # Both producers apply the same authored quality mode before compiling cells.
    physics = a.out / "game/src/gen/game_physics.gen.cpp"
    text = physics.read_text()
    old = "  if(!completeTerrainSlots || !WorldMoving::skipA31){"
    assert text.count(old) == 2
    text = text.replace(old, "  applyComposedTerrain(ch);\n" + old)
    physics.write_text(text + "\n" + Path(__file__).with_name("tyrax2-terrain-material-methods.inc").read_text())
    manifest = json.loads((a.source / "world-cell-price-manifest.json").read_text())
    manifest.update(variant="material-compose", status="UNQUALIFIED", materialManifest=material,
                    offlineTerrainCommon=a.terrain, offlineTerrainWithMaterial=a.terrain_with_material, qualityEquivalent=False,
                    limits=manifest["limits"] + material["limits"])
    (a.out / "world-cell-price-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(a.out)


if __name__ == "__main__":
    main()
