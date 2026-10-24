# Imported by make_arm.py for --vtxcat (needs --vtxstat): where the frame's
# StaPip vertices come from, by snapshots of the running vertex count at the
# start and end of the G.* scopes (no stack - unmatched costStart calls made
# a stacked attribution leak). Printed as CLDVC lines, per frame averages.
def apply(eng, proj, edit):
    probe = eng / 'inc/debug/cld_probe.hpp'
    edit(probe, 'inline long long vx[5];',
         'inline long long vx[5];\ninline long long vxIn[8], vxBase[8];\n'
         'inline long long vxNow() { return vx[0] + vx[1]; }\n'
         'inline int vxSlot(const char* n) { return n[0] != \'G\' ? -1 : n[2] == \'s\' ? 0 : n[2] == \'e\' ? 1 : n[2] == \'r\' ? 2 : n[2] == \'d\' ? 3 : n[2] == \'u\' ? 4 : n[2] == \'b\' ? 5 : -1; }')
    edit(probe, '  for (auto& x : vx) x = 0;\n',
         '  { static const char* const vn[6] = {"G.scene", "G.endFrame", "G.renderAll", "G.dripHeavy", "G.update", "G.beginFrame"};\n'
         '    for (int i = 0; i < 6; ++i) if (vxIn[i]) printf("CLDVC %s %d\\n", vn[i], int(vxIn[i] / winN)); }\n'
         '  for (auto& x : vxIn) x = 0;\n  for (auto& x : vx) x = 0;\n')
    t = probe.read_text(encoding='utf-8-sig')
    old = ("  explicit S(const char* n_) : n(n_), t(ticks()), i0(pc0()), d0(pc1()) {}\n"
           "  ~S() { scope(n, ticks() - t, pc0() - i0, pc1() - d0); }")
    assert t.count(old) == 1
    t = t.replace(old,
                  "  explicit S(const char* n_) : n(n_), t(ticks()), i0(pc0()), d0(pc1()) { const int s = vxSlot(n); if (s >= 0) vxBase[s] = vxNow(); }\n"
                  "  ~S() { scope(n, ticks() - t, pc0() - i0, pc1() - d0); const int s = vxSlot(n); if (s >= 0) vxIn[s] += vxNow() - vxBase[s]; }", 1)
    probe.write_bytes(t.encode())
