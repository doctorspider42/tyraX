# Imported by make_arm.py for --buildsegs: where a terrain chunk build spends
# its time. Lean ticks at fixed points of buildTerrainChunk.
def apply(proj, edit, include):
    p = proj / 'src/gen/game_physics.gen.cpp'
    include(p)
    edit(p, 'void TerrainGame::buildTerrainChunk(int slot, int cx, int cz) {\n',
         'void TerrainGame::buildTerrainChunk(int slot, int cx, int cz) {\n'
         '  const unsigned cldB0 = Cld::ticks();\n')
    edit(p, '  ch.layerPasses.resize(activeN);\n',
         '  const unsigned cldB1 = Cld::ticks(); Cld::scope("B.head", cldB1 - cldB0);\n'
         '  ch.layerPasses.resize(activeN);\n')
    edit(p, '  ch.aoSts.clear();\n',
         '  const unsigned cldB2 = Cld::ticks(); Cld::scope("B.emit", cldB2 - cldB1);\n'
         '  ch.aoSts.clear();\n')
    edit(p, '  // Ground shadow map pass (docs/shadows.md, "Ground shadow maps"): this\n',
         '  const unsigned cldB3 = Cld::ticks(); Cld::scope("B.bags", cldB3 - cldB2);\n'
         '  struct CldBT { unsigned t0; ~CldBT() { Cld::scope("B.tail", Cld::ticks() - t0); } } cldBT{cldB3};\n'
         '  // Ground shadow map pass (docs/shadows.md, "Ground shadow maps"): this\n')
