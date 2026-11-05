"""game/memmap.py ARM - one MEMMAP line at frame 150: bytes held by the
geometry arrays, by owner (capacity, not size). models = gameModels parts
(verts, strip, lod tiers, AO bytes); objects = objectGeometry parts (base +
env + lit normals + AO + lods + paint); batches = staticBatches; terrain =
terrainChunks (base, emissive, layer passes, AO). free = getAvailableRAM."""
import sys
root = sys.argv[1]
p = root + '/src/terrain_game.cpp'
s = open(p, encoding='utf-8').read()
a = 'void TerrainGame::loop() {\n'
assert s.count(a) == 1
block = r'''
  if (g_frameStamp == 150) {
    auto cap = [](const auto& v) { return (double)v.capacity() * sizeof(v[0]); };
    double mdl = 0, mdlN = 0, obj = 0, objLod = 0, objEnv = 0, bat = 0, ter = 0;
    int objN = 0, objParts = 0;
    for (const auto& m : gameModels)
      for (const auto& pt : m.parts) {
        mdl += cap(pt.verts) + cap(pt.stripVerts) + cap(pt.vertexAo) + cap(pt.stripVertexAo);
        for (const auto& l : pt.lodVerts) mdl += cap(l);
        for (const auto& l : pt.lodStripVerts) mdl += cap(l);
        mdlN += 1;
      }
    for (const auto& g : objectGeometry) {
      if (!g.parts.empty()) ++objN;
      for (const auto& pt : g.parts) {
        ++objParts;
        obj += cap(pt.vertices) + cap(pt.colors) + cap(pt.sts) + cap(pt.litNormals) + cap(pt.aoSts) + cap(pt.aoCols);
        objEnv += cap(pt.envNormals) + cap(pt.envColors) + cap(pt.paintMap) + cap(pt.paintNormals);
        for (const auto& l : pt.lods)
          objLod += cap(l.vertices) + cap(l.colors) + cap(l.sts) + cap(l.envNormals) + cap(l.envColors);
      }
    }
    for (const auto& b : staticBatches) bat += cap(b.vertices) + cap(b.colors) + cap(b.sts);
    for (const auto& ch : terrainChunks) {
      ter += cap(ch.vertices) + cap(ch.colors) + cap(ch.sts) + cap(ch.emisCols) + cap(ch.aoSts);
      for (const auto& lp : ch.layerPasses) ter += cap(lp.colors) + cap(lp.sts);
    }
    double tex = 0; int texN = 0;
    for (auto* t : *engine->renderer.core.texture.repository.getAll()) {
      ++texN;
      if (t && t->core && t->core->data) tex += (double)t->core->width * t->core->height * (int)t->core->bpp / 8.0;
      if (t && t->clut && t->clut->data) tex += (double)t->clut->width * t->clut->height * (int)t->clut->bpp / 8.0;
    }
    const double image = (double)(reinterpret_cast<u32>(tyraxProbeEnd) - 0x100000u);
    char l2_[300];
    snprintf(l2_, sizeof(l2_), "MEMMAP2 elf+bss %.2f MB | textures %.2f MB (%d) | baked %.2f MB retained %.2f MB",
             image / 1048576.0, tex / 1048576.0, texN, stapip.core.probeBakedBytes() / 1048576.0,
             stapip.core.probeRetainedBytes() / 1048576.0);
    TYRA_LOG(l2_);
    char l_[400];
    snprintf(l_, sizeof(l_), "MEMMAP free %.2f MB | models %.2f (%d parts) | objects %.2f (%d objs %d parts) lods %.2f env %.2f | batches %.2f (%d) | terrain %.2f (%d chunks)",
             engine->info.getAvailableRAM(), mdl / 1048576.0, (int)mdlN, obj / 1048576.0, objN, objParts,
             objLod / 1048576.0, objEnv / 1048576.0, bat / 1048576.0, (int)staticBatches.size(),
             ter / 1048576.0, (int)terrainChunks.size());
    TYRA_LOG(l_);
  }
'''
s = s.replace(a, a + block)
s = 'extern "C" char _end[];\nstatic char* const tyraxProbeEnd = _end;\n' + s
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('memmap ok')
