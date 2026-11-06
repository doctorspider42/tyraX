# Imported by make_arm.py for --terrimm: how much of the arena copy is terrain.
# Both arms run with terrain LOD off (no rebuilds, so no release drains);
# arm 1 opts every bag drawn inside renderTerrain into immutableStreams.
# Compare cycle 0 against cycle 1 only: registrations persist into later arms.
def apply(eng, proj, edit, include):
    p = eng / 'inc/debug/cld_probe.hpp'
    t = p.read_text(encoding='utf-8-sig')
    a = 'inline U64 copyChain = 0,'
    assert t.count(a) == 1
    t = t.replace(a, 'inline bool terrainImm = false;\n' + a)
    p.write_bytes(t.encode())
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
    edit(p, '  if (bag->immutableStreams && !bag->immutableRegistered)\n',
         '  if (Cld::terrainImm) bag->immutableStreams = true;\n'
         '  if (bag->immutableStreams && !bag->immutableRegistered)\n')
    p = proj / 'src/gen/game_physics.gen.cpp'
    include(p)
    edit(p, 'void TerrainGame::renderTerrain() {\n',
         'void TerrainGame::renderTerrain() {\n'
         '  struct CldTi { CldTi() { Cld::terrainImm = Cld::arm == 1; } ~CldTi() { Cld::terrainImm = false; } } cldTi;\n')
    edit(p, 'int TerrainGame::terrainLodStep(int cx, int cz) const {\n',
         'int TerrainGame::terrainLodStep(int cx, int cz) const {\n  return 1;  // Claude rig: terrain LOD off in both arms\n')
