# Imported by make_arm.py for --roadchunkab: road chunking A/B, chosen at the
# scene (re)load that precedes each cycle (Cld::cycle % CLD_ARMS = the arm the
# cycle will run). Arm 0: as shipped (close at 36 bands / V 16 / 1800 verts).
# Arm 1: V rebased IN PLACE at a strip restart, chunk closed at 72 dense
# stations / 36 bands / 1800 verts. Arm 2: the same with 144 dense stations.
def apply(proj, edit):
    p = proj / 'src/gen/game_vehicles.gen.cpp'
    edit(p, '      if (!c || stationsInChunk >= 36 || rb[0].v - chunkVBase >= 16.0F ||\n'
            '          c->vertices.size() + spanVertices > 1800) {\n',
         '      const int cldRcArm = (int)(Cld::cycle % CLD_ARMS);\n'
         '      const int cldDenseCap = cldRcArm == 1 ? 72 : 144;\n'
         '      if (!c || stationsInChunk >= 36 ||\n'
         '          (cldRcArm == 0 ? rb[0].v - chunkVBase >= 16.0F : cldDense + (b - a) > cldDenseCap) ||\n'
         '          c->vertices.size() + spanVertices > 1800) {\n'
         '        cldDense = 0;\n')
    edit(p, '      auto P = [&](const RoadRowV& q) { return Tyra::Vec4(q.x, q.y, q.z, 1.0F); };\n'
            '      auto S = [&](const RoadRowV& q) {\n'
            '        return Tyra::Vec4(q.u, q.v - chunkVBase, 1.0F, 0.0F);\n',
         '      if (cldRcArm != 0 && rb[0].v - chunkVBase >= 16.0F) {\n'
         '        chunkVBase = floorf(ra[0].v);  // rebase in place: next band restarts its strip\n'
         '        alongOpen = false;\n'
         '      }\n'
         '      cldDense += b - a;\n'
         '      auto P = [&](const RoadRowV& q) { return Tyra::Vec4(q.x, q.y, q.z, 1.0F); };\n'
         '      auto S = [&](const RoadRowV& q) {\n'
         '        return Tyra::Vec4(q.u, q.v - chunkVBase, 1.0F, 0.0F);\n')
    edit(p, '    int stationsInChunk = 0;\n', '    int stationsInChunk = 0;\n    int cldDense = 0;\n')
    edit(p, '  TYRA_LOG("ROADS scene ", scene, " chunks ", roadChunks,',
         '  printf("ROADCHUNKAB arm=%d chunks=%d verts=%d\\n", (int)(Cld::cycle % CLD_ARMS), roadChunks, roadVertices);\n'
         '  TYRA_LOG("ROADS scene ", scene, " chunks ", roadChunks,')
