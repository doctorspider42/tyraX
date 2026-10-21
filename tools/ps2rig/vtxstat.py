# Imported by make_arm.py for --vtxstat: vertices a frame hands to StaPip,
# split list vs strip, minus bags culled whole. Printed per window as CLDVX
# (per frame averages): list verts, strip verts, list bags, strip bags.
def apply(eng, edit):
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
    t = p.read_text(encoding='utf-8-sig')
    a = '''  if (bag->count <= 0) {
    TYRA_ATTRIB_ADD(renderTicks, attribRenderStart);
    return;
  }

#if TYRA_STAPIP_STAMP
  // Modified by TyraX: a valid stamp IS this bag's submission'''
    assert t.count(a) == 1, t.count(a)
    t = t.replace(a, a.replace('''  }

#if TYRA_STAPIP_STAMP''', '''  }
  Cld::vx[bag->stripped ? 1 : 0] += bag->count; ++Cld::vx[bag->stripped ? 3 : 2];

#if TYRA_STAPIP_STAMP''', 1))
    b = '''    if (frustumCheck == OUTSIDE_FRUSTUM) {
      Cld::span("c.culled", cldT0, Cld::mark());'''
    assert t.count(b) == 1, t.count(b)
    t = t.replace(b, b + '\n      Cld::vx[bag->stripped ? 1 : 0] -= bag->count; --Cld::vx[bag->stripped ? 3 : 2]; ++Cld::vx[4];')
    p.write_bytes(t.encode())
    edit(eng / 'inc/debug/cld_probe.hpp', 'inline U coreCalls = 0;', 'inline U coreCalls = 0;\ninline long long vx[5];')
    edit(eng / 'inc/debug/cld_probe.hpp', '  printf("CLDK slowN=%u fastN=%u\\n", slowN, fastN);',
         '  printf("CLDVX listV=%d stripV=%d listBags=%d stripBags=%d culledBags=%d (per frame)\\n", int(vx[0]/winN), int(vx[1]/winN), int(vx[2]/winN), int(vx[3]/winN), int(vx[4]/winN));\n  for (auto& x : vx) x = 0;\n  printf("CLDK slowN=%u fastN=%u\\n", slowN, fastN);')
    print('vertex census')
