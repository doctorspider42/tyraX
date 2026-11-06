# Imported by make_arm.py for --objcensus: at rig frames 3000 (parked) and
# 3880 (driving) print every object the object pass draws: index, type, model,
# position, and how many part / AO / emissive bags it submitted.
def apply(proj, edit):
    p = proj / 'src/gen/game_physics.gen.cpp'
    t = p.read_text(encoding='utf-8-sig')
    def rep(a, b):
        nonlocal t
        assert t.count(a) == 1, a[:70]
        t = t.replace(a, b)
    rep('    u32 lpMain = 0, lpCompanion = 0;\n',
        '    u32 lpMain = 0, lpCompanion = 0;\n'
        '    const bool cldCen = Cld::frame == 3000 || Cld::frame == 3880;\n'
        '    int cldParts = 0, cldAo = 0, cldEmis = 0;\n')
    rep('        renderObjectBag(i, part.bag.get());\n        const u32 lpB',
        '        renderObjectBag(i, part.bag.get());\n        ++cldParts;\n'
        '        if (part.aoBag) ++cldAo;\n        if (part.emisBag && optionalMaterialDetail(i)) ++cldEmis;\n'
        '        const u32 lpB')
    rep('    costEnd("Object",i,costObjectStart);\n',
        '    costEnd("Object",i,costObjectStart);\n'
        '    if (cldCen && cldParts > 0) { const auto& cd = runtimeObjects[i].data; printf("CENSUS f=%u obj=%d type=%d model=%d pos=%d,%d,%d parts=%d ao=%d emis=%d batch=%d\\n", Cld::frame, i, cd.type, cd.model, (int)cd.position[0], (int)cd.position[1], (int)cd.position[2], cldParts, cldAo, cldEmis, (int)(i < (int)objectBatchOf.size() ? objectBatchOf[i] : -2)); }\n')
    p.write_bytes(t.encode())
