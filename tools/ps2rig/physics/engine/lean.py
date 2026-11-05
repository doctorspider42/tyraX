"""engine/lean.py - RIG ENGINE ONLY (engine/setup.py). Two arms behind StaPipCore::probeArm:
 bit 4 : partial replay classifies straight into routes/masks
         (StaPipBagPackager::classifyRoutes), no StaPipBagPackage records;
 bit 16: verify - also runs create() + the routes loop and counts mismatches
         (StaPipCore::probeLeanBad / probeLeanChecked);
 bit 8 : ImmutableSpanTable::lowerBound searches a packed u32 base array
         (Tyra::g_spanBases) instead of the 16-byte spans.
Idempotent."""
import sys
root = sys.argv[1].rstrip('/') + '/'


def edit(rel, pairs):
    p = root + rel
    s = open(p, encoding='utf-8', newline='').read()
    assert '\r' not in s
    for a, b in pairs:
        if b in s:
            continue
        assert s.count(a) == 1, (rel, a[:70], s.count(a))
        s = s.replace(a, b)
    open(p, 'w', encoding='utf-8', newline='').write(s)


PH = 'inc/renderer/3d/pipeline/static/core/bag/packaging/stapip_bag_packager.hpp'
edit(PH, [
    ('  StaPipBagPackage* create(u16* o_size, StaPipBag* data, u16 size);\n',
     '  StaPipBagPackage* create(u16* o_size, StaPipBag* data, u16 size);\n'
     '  /** Rig (lean partial classify): create()\'s verdicts as replay routes\n'
     '   * (0 drop, 1 cull, 2 clip) and clip masks, no package records. Returns\n'
     '   * the package count; writes nothing when it exceeds capacity. */\n'
     '  u16 classifyRoutes(const StaPipBag* data, u16 size, u8* routes, u8* masks,\n'
     '                     u32 capacity);\n'),
])
PC = 'src/renderer/3d/pipeline/static/core/bag/packaging/stapip_bag_packager.cpp'
edit(PC, [
    ('void StaPipBagPackager::setMaxVertCount(const u32& count) {\n',
     'u16 StaPipBagPackager::classifyRoutes(const StaPipBag* data, u16 size,\n'
     '                                      u8* routes, u8* masks, u32 capacity) {\n'
     '  const u16 n = static_cast<u16>((data->count + size - 1) / size);\n'
     '  if (n > capacity) return n;\n'
     '  const u32 third = maxVertCount / 3;\n'
     '  const bool coarse = size == maxVertCount && renderBBox && objectSpacePlanes;\n'
     '  const bool wantMask = capturePlaneMasks;\n'
     '  CoreBBoxFrustum coarseRoute = PARTIALLY_IN_FRUSTUM;\n'
     '  for (u16 i = 0; i < n; ++i) {\n'
     '    if (coarse && (i & 7) == 0)\n'
     '      coarseRoute = CoreBBox::frustumCheckAABB(objectSpacePlanes,\n'
     '          renderBBox->coarseMin(i / 8), renderBBox->coarseMax(i / 8), nullptr);\n'
     '    const u32 start = static_cast<u32>(i) * size;\n'
     '    const u32 pkgSize = i == n - 1 ? data->count - start : size;\n'
     '    CoreBBoxFrustum v;\n'
     '    u8 mask = 0;\n'
     '    bool gb = false;\n'
     '    if (coarse && coarseRoute != PARTIALLY_IN_FRUSTUM) {\n'
     '      v = coarseRoute;\n'
     '    } else if (!renderBBox || !objectSpacePlanes) {\n'
     '      v = OUTSIDE_FRUSTUM;\n'
     '    } else {\n'
     '      Vec4 mn, mx;\n'
     '      const u32 first = start / third;\n'
     '      const u32 parts = pkgSize <= third ? (start + pkgSize - 1) / third - first + 1\n'
     '                                         : (pkgSize + third - 1) / third;\n'
     '      renderBBox->getMergedMinMax(first, static_cast<u16>(parts), &mn, &mx);\n'
     '      u8* frustumMask = (wantMask && clipObjectSpacePlanes == nullptr) ? &mask : nullptr;\n'
     '      v = CoreBBox::frustumCheckAABB(objectSpacePlanes, mn, mx, frustumMask);\n'
     '      if (wantMask && clipObjectSpacePlanes != nullptr) {\n'
     '        const u8 m = v == PARTIALLY_IN_FRUSTUM\n'
     '            ? CoreBBox::activePlaneMaskAABB(clipObjectSpacePlanes, mn, mx, 8) : 0;\n'
     '        mask = static_cast<u8>(m & 0x3F);\n'
     '        gb = v == PARTIALLY_IN_FRUSTUM && m == 0;\n'
     '      }\n'
     '    }\n'
     '    routes[i] = (v == IN_FRUSTUM || gb) ? 1\n'
     '              : (v == PARTIALLY_IN_FRUSTUM && (!data->stripped || pkgSize >= 3)) ? 2 : 0;\n'
     '    masks[i] = mask;\n'
     '  }\n'
     '  return n;\n'
     '}\n\n'
     'void StaPipBagPackager::setMaxVertCount(const u32& count) {\n'),
])
H = 'inc/renderer/3d/pipeline/static/core/stapip_core.hpp'
edit(H, [
    ('  static inline u32 probeArm = 0;\n',
     '  static inline u32 probeArm = 0;\n  static inline u32 probeLeanBad = 0, probeLeanChecked = 0;\n')])
C = 'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
edit(C, [
    ('        auto packages = packager.create(&packagesCount, bag, maxVertCount);\n'
     '        probeRl_.m[4] = probeTicks();\n'
     '        if (packagesCount <= 512) {\n'
     '          for (u16 i = 0; i < packagesCount; ++i) {\n',
     '        StaPipBagPackage* packages = nullptr;\n'
     '        const bool lean_ = (probeArm & 4) && !telemetryEnabled;\n'
     '        if (lean_) packagesCount = packager.classifyRoutes(bag, maxVertCount, routes, masks, 512);\n'
     '        else packages = packager.create(&packagesCount, bag, maxVertCount);\n'
     '        if (lean_ && (probeArm & 16) && packagesCount <= 512) {\n'
     '          static u8 vr_[512], vm_[512];\n'
     '          u16 vn_ = 0;\n'
     '          auto vp_ = packager.create(&vn_, bag, maxVertCount);\n'
     '          for (u16 i = 0; i < vn_; ++i) {\n'
     '            const StaPipBagPackage& p = vp_[i];\n'
     '            u8 route = 0;\n'
     '            if (p.isInFrustum == IN_FRUSTUM || isGuardBandOnly(p)) route = 1;\n'
     '            else if (p.isInFrustum == PARTIALLY_IN_FRUSTUM && (!bag->stripped || p.size >= 3)) route = 2;\n'
     '            vr_[i] = route; vm_[i] = p.clipPlaneMask;\n'
     '            ++probeLeanChecked;\n'
     '            if (vr_[i] != routes[i] || (route == 2 && vm_[i] != masks[i])) ++probeLeanBad;\n'
     '          }\n'
     '          if (vn_ != packagesCount) ++probeLeanBad;\n'
     '        }\n'
     '        probeRl_.m[4] = probeTicks();\n'
     '        if (packagesCount <= 512) {\n'
     '          for (u16 i = 0; !lean_ && i < packagesCount; ++i) {\n'),
    ('        if (!replayedPartial) {\n'
     '          // The runs did not fit the packet: the ordinary loop, same packages.\n',
     '        if (!replayedPartial) {\n'
     '          if (packages == nullptr) packages = packager.create(&packagesCount, bag, maxVertCount);\n'
     '          // The runs did not fit the packet: the ordinary loop, same packages.\n'),
])
A = 'inc/renderer/core/paths/path1/frame_chain_arena.hpp'
edit(A, [
    ('namespace Tyra {\n// Disjoint engine-owned ranges',
     'namespace Tyra {\ninline bool g_spanBases = false;  // rig arm: packed base search\n// Disjoint engine-owned ranges'),
    ('    for (uint32_t i = count; i > at; --i) spans[i] = spans[i-1];\n'
     '    spans[at] = {base,qwords,0,false}; ++count; return true;\n',
     '    for (uint32_t i = count; i > at; --i) { spans[i] = spans[i-1]; bases[i] = bases[i-1]; }\n'
     '    spans[at] = {base,qwords,0,false}; bases[at] = base; ++count; return true;\n'),
    ('    for (uint32_t i = at; i + 1 < count; ++i) spans[i] = spans[i+1];\n    --count;\n',
     '    for (uint32_t i = at; i + 1 < count; ++i) { spans[i] = spans[i+1]; bases[i] = bases[i+1]; }\n    --count;\n'),
    ('  Span spans[kCapacity]; uint32_t count = 0;\n'
     '  uint32_t lowerBound(uint32_t address) const {\n'
     '    uint32_t lo = 0, hi = count;\n',
     '  Span spans[kCapacity]; uint32_t count = 0;\n'
     '  uint32_t bases[kCapacity];\n'
     '  uint32_t lowerBound(uint32_t address) const {\n'
     '    uint32_t lo = 0, hi = count;\n'
     '    if (g_spanBases) {\n'
     '      while (lo < hi) {\n'
     '        const uint32_t mid = lo + (hi - lo) / 2;\n'
     '        if (bases[mid] < address) lo = mid + 1; else hi = mid;\n'
     '      }\n'
     '      return lo;\n'
     '    }\n'),
])
print('lean engine ok')
