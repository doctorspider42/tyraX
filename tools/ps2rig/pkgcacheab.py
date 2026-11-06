# Imported by make_arm.py for --pkgcacheab: arm 1 = crossing packages REF the
# bag's own arrays (no immutable package copy); other arms = fillCachedPackage.
def apply(eng, edit, include):
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
    include(p)
    edit(p, '                                              const StaPipBagPackage& pkg) {\n  auto* bag = pkg.bag;\n',
         '                                              const StaPipBagPackage& pkg) {\n  if (Cld::arm == 1) return false;\n  auto* bag = pkg.bag;\n')
