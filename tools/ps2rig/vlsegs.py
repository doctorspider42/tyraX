# Imported by make_arm.py for --vlsegs: split renderVehicleGlow into the lamp
# part colour pass, the pool rebuild, the pool submit and the tail glow.
def apply(proj, edit):
    p = proj / 'src/gen/game_vehicles.gen.cpp'
    t = p.read_text(encoding='utf-8-sig')
    def rep(a, b):
        nonlocal t
        assert t.count(a) == 1, a[:60]
        t = t.replace(a, b)
    rep('      renderWorldBag(hp.bag.get());\n',
        '      { Cld::S cldS("VL.submit"); renderWorldBag(hp.bag.get()); }\n')
    rep('      if (rebuild) {\n      // The beam starts',
        '      const unsigned cldRb = Cld::ticks();\n      if (rebuild) {\n      // The beam starts')
    rep('      }  // rebuild\n', '      }  // rebuild\n      Cld::scope("VL.rebuild", Cld::ticks() - cldRb);\n')
    rep('  glowBag_->count = (u32)(glowCount_ * 6);\n',
        '  Cld::S cldG("VL.glow");\n  glowBag_->count = (u32)(glowCount_ * 6);\n')
    if '#include "debug/cld_probe.hpp"' not in t:
        t = '#include "debug/cld_probe.hpp"\n' + t
    p.write_bytes(t.encode())
