# Imported by make_arm.py for --vtxcat2 (needs --vtxstat and --vtxcat):
# per cost-scope vertex attribution that survives unmatched costStart calls.
# costStart records (its tick value, the running vertex count) in a ring;
# costEnd looks its own start tick up and charges the difference to its label.
def apply(eng, proj, edit):
    probe = eng / 'inc/debug/cld_probe.hpp'
    edit(probe, 'inline long long vxIn[8], vxBase[8];',
         'inline long long vxIn[8], vxBase[8];\n'
         'inline unsigned vrT[64]; inline long long vrV[64]; inline unsigned vrN = 0;\n'
         'inline long long vxLab[96];\n'
         'inline void vrStart(unsigned t) { vrT[vrN & 63] = t; vrV[vrN & 63] = vx[0] + vx[1]; ++vrN; }\n'
         'inline void vrEnd(const char* label, unsigned t);')
    t = probe.read_text(encoding='utf-8-sig')
    t = t.replace('}  // namespace Cld',
                  'inline void vrEnd(const char* label, unsigned t) {\n'
                  '  for (unsigned k = 1; k <= 64 && k <= vrN; ++k) { const unsigned j = (vrN - k) & 63;\n'
                  '    if (vrT[j] == t) { const long long d = vx[0] + vx[1] - vrV[j];\n'
                  '      for (int i = 0; i < used; ++i) if (names[i] == label) { vxLab[i] += d; return; } return; } }\n'
                  '}\n}  // namespace Cld', 1)
    old = '  for (auto& x : vxIn) x = 0;\n'
    assert t.count(old) == 1
    t = t.replace(old, '  for (int i = 0; i < used; ++i) if (vxLab[i]) printf("CLDVL %s %d\\n", names[i], int(vxLab[i] / winN));\n'
                       '  for (auto& x : vxLab) x = 0;\n' + old)
    probe.write_bytes(t.encode())
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, '  auto costStart = [&]() -> u32 { return profTicks(); };',
         '  auto costStart = [&]() -> u32 { const u32 cldT = profTicks(); Cld::vrStart(cldT); return cldT; };')
    edit(p, '    Cld::scope(label, profTicks() - start);\n',
         '    Cld::scope(label, profTicks() - start);\n    Cld::vrEnd(label, start);\n')
