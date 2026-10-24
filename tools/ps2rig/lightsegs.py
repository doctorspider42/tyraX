# Imported by make_arm.py for --lightsegs: what the dynamic-light step costs,
# split into the pick loop and the spot build + zero-influence test. Lean ticks.
def apply(eng, edit, include):
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
    include(p)
    edit(p, '''    light = rendererCore->pickDynLight(
        worldCenter,
        0.5F * Math::sqrtNonNegative(ex * ex + ey * ey + ez * ez) * scale,
        bag->info->dynLightSkipSlot);
''', '''    const unsigned cldL0 = Cld::ticks();
    light = rendererCore->pickDynLight(
        worldCenter,
        0.5F * Math::sqrtNonNegative(ex * ex + ey * ey + ez * ez) * scale,
        bag->info->dynLightSkipSlot);
    Cld::scope("L.pickS", Cld::ticks() - cldL0);
''')
    edit(p, '''  auto meshSpot =
      buildSpotForBag(light ? *light : rendererCore->spot, bag->info->model);
  if (meshSpot.enabled && stapipSpotHasNoInfluence(meshSpot, s->lo, s->hi))
    meshSpot.enabled = false;
''', '''  const unsigned cldL1 = Cld::ticks();
  auto meshSpot =
      buildSpotForBag(light ? *light : rendererCore->spot, bag->info->model);
  if (meshSpot.enabled && stapipSpotHasNoInfluence(meshSpot, s->lo, s->hi))
    meshSpot.enabled = false;
  Cld::scope("L.spotS", Cld::ticks() - cldL1);
''')
    p = eng / 'src/renderer/core/renderer_core.cpp'
    include(p)
    edit(p, '''  for (u32 i = 0; i < count; i++) {
    const auto* l = candidates[i];''', '''  Cld::lightN += count;
  for (u32 i = 0; i < count; i++) {
    const auto* l = candidates[i];''')
    p = eng / 'inc/debug/cld_probe.hpp'
    t = p.read_text(encoding='utf-8-sig')
    a = 'inline U64 copyChain = 0,'
    assert t.count(a) == 1
    t = t.replace(a, 'inline U64 lightN = 0;\n' + a)
    a = '  printf("CLDP frame im=%u'
    assert t.count(a) == 1
    t = t.replace(a, '  printf("CLDL candidates10=%u\\n", U(lightN * 10 / winN)); lightN = 0;\n' + a)
    p.write_bytes(t.encode())
