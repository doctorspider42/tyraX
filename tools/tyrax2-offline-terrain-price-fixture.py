"""Integrate the factored offline terrain into the qualified moving game."""
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
    parser.add_argument("compiled", type=Path)
    parser.add_argument("--ps2-compiled", type=Path, help="Separately pinned PS2-arithmetic bootstrap data; emulator and PS2 inputs remain distinct")
    parser.add_argument("--current-producer", action="store_true", help="Use the checkout's current terrain builder in the common fixture, retaining the qualified fixed drive/residency protocol")
    args = parser.parse_args()
    binding = json.loads((args.source / "native-binding.json").read_text())
    for relative, digest in binding["files"].items():
        assert sha(args.source / relative) == digest, relative
    assert sha(args.source / "game/bin/vehicle-playground.elf") == binding["elfSha256"]
    report = json.loads((args.compiled / "report.json").read_text())
    assert report["valid"] and sha(args.compiled / "offline-terrain.bin") == report["outputSha256"]
    assert not args.out.exists()

    def ignore(path, names):
        return [name for name in names if name in ("obj", ".git") or
                (Path(path).name == "bin" and (name.endswith((".csv", ".tga", ".sym")) or
                 name == "log.txt" or name.startswith(("tape-", "command-", "cell-", "source-", "moving-", "world-price-"))))]

    for relative in ("game", "tyra/engine"):
        shutil.copytree(args.source / relative, args.out / relative, ignore=ignore)
    for relative in ("tyra/Makefile.base", "authored-manifest.json"):
        shutil.copy2(args.source / relative, args.out / relative)
    repo = Path(__file__).resolve().parent
    shutil.copy2(repo / "tyrax2-offline-terrain-runtime.hpp", args.out / "game/inc/offline_terrain.hpp")
    shutil.copy2(args.compiled / "offline-terrain.bin", args.out / "game/bin/offline-terrain.bin")

    def edit(relative, old, new):
        path = args.out / relative
        text = path.read_text()
        assert text.count(old) == 1, (relative, old, text.count(old))
        path.write_text(text.replace(old, new))

    physics = "game/src/gen/game_physics.gen.cpp"
    original = (args.out / physics).read_text()
    start = original.index("void TerrainGame::buildTerrainChunk(int slot, int cx, int cz) {")
    end = original.index("// Unbuilt chunks in the current view rect", start)
    method = original[start:end].rstrip()
    assert method.endswith("}")
    current_hash = None
    if args.current_producer:
        template_path = repo.parent / "src/game_templates.inc"
        template = template_path.read_text()
        current_start = template.index("void TerrainGame::buildTerrainChunk(int slot, int cx, int cz) {")
        current_end = template.index("// True when the slot pool holds every chunk", current_start)
        helper_end = template.index("// Unbuilt chunks in the current view rect", current_end)
        helper = template[current_end:helper_end].rstrip()
        current = template[current_start:current_end].rstrip()
        assert current.endswith("}") and "shadeAtC" in current and "if (!ch.stripLogged)" in current
        lifetime = method[method.index("  if(ch.completeCell.owner){"):method.index("  if (ch.cx >= 0)")]
        publication = method[method.index("  if(!completeTerrainSlots || !WorldMoving::skipA31){"):]
        current = current.replace("  TerrainChunk& ch = terrainChunks[slot];", "  TerrainChunk& ch = terrainChunks[slot];\n" + lifetime, 1)
        current = current[:-1] + publication
        path = args.out / physics
        path.write_text(original[:start] + current + "\n\n" + helper + "\n\n" + original[end:])
        original = path.read_text()
        method = current
        header_path = args.out / "game/inc/terrain_game.hpp"
        header = header_path.read_text()
        header = header.replace("  struct TerrainChunk {", "  struct TerrainChunk {\n    bool stripLogged=false;", 1)
        needle = "  void updateTerrainChunks("
        assert header.count(needle) == 1
        header = header.replace(needle, "  std::vector<float> terrainShadeCache;\n  int terrainBuiltThisFrame=0;\n  bool terrainWholeMapResident() const;\n" + needle, 1)
        header_path.write_text(header)
        text = path.read_text()
        text = text.replace("void TerrainGame::resetTerrainChunks() {", "void TerrainGame::resetTerrainChunks() {\n  terrainShadeCache.clear();", 1)
        path.write_text(text)
        current_hash = sha(template_path)
    prefix = method[method.index("  TerrainChunk& ch"):method.index("  const int cellsX")]
    bags = method[method.index("  if (!ch.bag) {"):method.index("  // Ground shadow map pass")]
    shadow_path = method[method.index("    std::string want;", method.index("  // Ground shadow map pass")):method.index("    ch.gsVerts.clear();", method.index("  // Ground shadow map pass"))]
    shadow_bind = method[method.index("    if (!ch.gsVerts.empty()) {", method.index("  // Ground shadow map pass")):method.index("  // Every pass above")]
    shadow_bind = shadow_bind.replace('      ch.gsCols.assign(ch.gsVerts.size(), Color(128.0F, 128.0F, 128.0F, 128.0F));\n', '')
    # Strip/run metadata and exact bounds are already in the offline artifact.
    # Do not rescan every vertex for diagnostic triangles or derive bounds again.
    tail = (method[method.index("  // Every pass above"):method.index("  // The chunk's SURFACE")] +
            method[method.index("  if(!completeTerrainSlots || !WorldMoving::skipA31){"):])
    setup = """
  const int lod=terrainLodStep(cx,cz);
  const auto* entry=OfflineTerrain::find(g_activeScene,cx,cz,lod);
  TYRA_ASSERT(entry,"Offline terrain fixture admission");
  ch.lod=lod;ch.lodW=terrainLodStep(cx-1,cz);ch.lodE=terrainLodStep(cx+1,cz);
  ch.lodN=terrainLodStep(cx,cz-1);ch.lodS=terrainLodStep(cx,cz+1);
  const unsigned edge[]={cx?unsigned(ch.lodW>lod?ch.lodW:lod):unsigned(lod),
    cx+1<terrainChunksX?unsigned(ch.lodE>lod?ch.lodE:lod):unsigned(lod),
    cz?unsigned(ch.lodN>lod?ch.lodN:lod):unsigned(lod),
    cz+1<terrainChunksZ?unsigned(ch.lodS>lod?ch.lodS:lod):unsigned(lod)};
  const float* bounds=OfflineTerrain::bounds(*entry,edge);
  ch.minY=bounds[0];ch.maxY=bounds[1];memcpy(ch.aabbMin,bounds+2,12);memcpy(ch.aabbMax,bounds+5,12);
  OfflineTerrain::restore(ch.vertices,*entry,0,edge);OfflineTerrain::restore(ch.colors,*entry,1,edge);
  OfflineTerrain::restore(ch.sts,*entry,2,edge);OfflineTerrain::restore(ch.gsVerts,*entry,3,edge);
  OfflineTerrain::restore(ch.gsCols,*entry,4,edge);OfflineTerrain::restore(ch.gsSts,*entry,5,edge);
  ch.layerPasses.clear();ch.aoSts.clear();ch.aoCols.clear();ch.emisCols.clear();
  const bool textured=TERRAIN_TEXTURE>=0 && loadedTextures[TERRAIN_TEXTURE];
  TYRA_ASSERT(textured && TERRAIN_LAYER_COUNT==0 && !terrainMapLit && !terrainMapOcc,"Offline terrain material contract");
  const bool strips=true;const unsigned stripRun=75;
  ++OfflineTerrain::restores;
"""
    offline = "namespace Vehicle_playground {\nvoid TerrainGame::buildOfflineTerrainChunk(int slot,int cx,int cz){\n" + prefix + setup + bags + "  {\n" + shadow_path + shadow_bind + tail + "\n}\n"
    assert offline.count("acquireTexture(want)") == 1
    edit(physics, '#include "game_runtime.gen.hpp"', '#include "game_runtime.gen.hpp"\n#include "offline_terrain.hpp"')
    edit(physics, "void TerrainGame::buildTerrainChunk(int slot, int cx, int cz) {", """void TerrainGame::buildTerrainChunk(int slot, int cx, int cz) {
  if(OfflineTerrain::enabled && !OfflineTerrain::entries.empty() && g_activeScene==0){buildOfflineTerrainChunk(slot,cx,cz);return;}""")
    path = args.out / physics
    path.write_text(path.read_text() + "\n" + offline + "\n" + (repo / "tyrax2-offline-terrain-audit.inc").read_text())
    edit("game/inc/terrain_game.hpp", "  bool compileCompleteTerrainChunk(TerrainChunk& ch);", "  void buildOfflineTerrainChunk(int slot,int cx,int cz);\n  void auditOfflineTerrain();\n  bool compileCompleteTerrainChunk(TerrainChunk& ch);")
    edit("game/inc/world_moving.hpp", "#pragma once", '#pragma once\n#include "offline_terrain.hpp"')
    edit("game/inc/world_moving.hpp", "AudioIdle::set(p<6 && modes[p]);", "AudioIdle::set(true);OfflineTerrain::enabled=p<6 && modes[p];")
    edit("game/inc/world_moving.hpp", "WheelVu::reset();AudioIdle::reset();", "WheelVu::reset();AudioIdle::reset();OfflineTerrain::restores=OfflineTerrain::fallbacks=0;")
    edit("game/inc/world_moving.hpp", 'variant=audio-event\\n",p,unsigned(AudioIdle::enabled));', 'variant=offline-terrain\\n",p,unsigned(OfflineTerrain::enabled));')
    edit("game/inc/world_moving.hpp", "  WheelVu::witness(phase);AudioIdle::witness(phase);", '  WheelVu::witness(phase);AudioIdle::witness(phase);\n  printf("OFFLINEPHASE phase=%u enabled=%u restores=%u fallbacks=%u\\n",phase,unsigned(OfflineTerrain::enabled),OfflineTerrain::restores,OfflineTerrain::fallbacks);')
    game = "game/src/terrain_game.cpp"
    edit(game, "  WorldCellComplete::directAudit=MotionProbe::phase<6 && MotionProbe::offset==1100;", "  if(MotionProbe::phase<6 && MotionProbe::offset==1099){OfflineTerrain::load();auditOfflineTerrain();}\n  WorldCellComplete::directAudit=MotionProbe::phase<6 && MotionProbe::offset==1100;")
    edit(game, "  const bool movingAudio=AudioIdle::enabled;", "  const bool movingAudio=AudioIdle::enabled;\n  const bool movingOffline=OfflineTerrain::enabled;")
    edit(game, "AudioIdle::set(arm==1?!movingAudio:movingAudio);", "AudioIdle::set(true);OfflineTerrain::enabled=arm==1?!movingOffline:movingOffline;")
    # Actual producer replacement in each held arm, after the old GPU owner drains.
    edit(game, "  if(candidate)TYRA_ASSERT(prepareCompleteTerrain(),\"A44 price checkpoint preparation\");", "  for(int slot=0;slot<int(terrainChunks.size());++slot){auto& ch=terrainChunks[slot];if(ch.cx>=0)buildTerrainChunk(slot,ch.cx,ch.cz);}\n  if(candidate)TYRA_ASSERT(prepareCompleteTerrain(),\"A44 price checkpoint preparation\");")
    edit(game, "AudioIdle::set(movingAudio);", "AudioIdle::set(movingAudio);OfflineTerrain::enabled=movingOffline;")
    manifest = json.loads((args.source / "world-cell-price-manifest.json").read_text())
    manifest.update(variant="offline-terrain", status="UNQUALIFIED", baseElfSha256=binding["elfSha256"],
                    offlineBinarySha256=report["outputSha256"], offlineReport=report,
                    offlineRuntimeSha256=sha(repo / "tyrax2-offline-terrain-runtime.hpp"),
                    offlineAuditSha256=sha(repo / "tyrax2-offline-terrain-audit.inc"),
                    limits=report["limits"] + ["Common resident tables and loading footprint are present in both roles.", "Same-ELF net routing comparison only; no production host export or public adoption."])
    if current_hash:
        manifest.update(currentTerrainTemplateSha256=current_hash,currentProducerCommon=True)
        manifest["limits"].append("Current builder includes shade reuse, layer constants and once-per-slot diagnostics in BOTH roles. The inherited controlled drive/residency/engine fixture is not the full current production checkout.")
    if args.ps2_compiled:
        physical = json.loads((args.ps2_compiled / "report.json").read_text())
        assert physical["valid"] and physical["oraclePlatform"] == "ps2"
        assert physical["exactNativeVariants"] == report["exactNativeVariants"]
        assert sha(args.ps2_compiled / "offline-terrain.bin") == physical["outputSha256"]
        shutil.copy2(args.ps2_compiled / "offline-terrain.bin", args.out / "offline-terrain-ps2.bin")
        manifest.update(offlineBinaryByPlatform={"emulator": report["outputSha256"], "ps2": physical["outputSha256"]}, offlinePs2Report=physical)
        manifest["limits"].append("Emulator and PS2 use different explicitly pinned bootstrap asset bytes; same ELF does not mean same cross-platform input artifact.")
    (args.out / "world-cell-price-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(args.out)


if __name__ == "__main__":
    main()
