#pragma once
// Private same-ELF dressing ablation draft; not production or a speed claim.
namespace CloseoutNightGeometry {
constexpr unsigned kCount = 11;
constexpr unsigned kAll = (1u << kCount) - 1u;
constexpr unsigned long long kIds[kCount] = {
  0xebc2b556a5b453c4ULL, 0x65bd7f8ad89b377cULL,
  0x1172b318637275fbULL, 0x217a232b39f12bc6ULL,
  0xa5f7e1bc7c0fa3cdULL, 0x571b6780d8f997e0ULL,
  0x74af3251bec68ad0ULL, 0x3aad4e1affcecf3eULL,
  0x5ba2db2639ebb6c8ULL, 0x995c9d669f16c030ULL,
  0x1f625af6a4c08a22ULL
};
struct State {
  unsigned generation = ~0u;
  int scene = -1;
  int rows[kCount] = {};
  bool saved[kCount] = {};
  bool ready = false;
  bool rejected = false;
  int previousHide = -1;
  unsigned matched = 0;

  // Run after all scripts (especially DistrictMood), before lighting/views.
  // Caller must opt in only for kind2, fixed night and public Players policy.
  // Default privateDisabled path writes no scene state.
  template<class Context>
  bool apply(Context& ctx, const unsigned long long* ids,
             bool privateEnabled, bool fixedNightPlayers, bool hide) {
    if (!privateEnabled) return true;
    if (!fixedNightPlayers || ctx.scene != 0 || !ids || !ctx.objects)
      return false;  // qualification failure, never silently price fallback
    if (scene != ctx.scene || generation != ctx.sceneGeneration) {
      scene = ctx.scene; generation = ctx.sceneGeneration;
      ready = false; rejected = false; previousHide = -1; matched = 0;
    }
    if (rejected) return false;
    if (!ready) {
      for (unsigned k = 0; k < kCount; ++k) {
        int row = -1;
        for (int i = 0; i < ctx.objectCount; ++i)
          if (ids[i] == kIds[k]) {
            if (row >= 0) { rejected = true; return false; }
            row = i;
          }
        if (row < 0) { rejected = true; return false; }
        const auto& o = ctx.objects[row];
        if (!o.active || !o.visible || o.data.type != 0 ||
            o.data.collision != 2) { rejected = true; return false; }
        rows[k] = row; saved[k] = o.visible; ++matched;
      }
      ready = true;
    }
    if (previousHide == (int)hide) return true;
    // Exact selected dressing only. Do not change active/dirty/light requests,
    // geometry buffers, batch ownership, presentation or DMA ownership.
    for (unsigned k = 0; k < kCount; ++k)
      ctx.objects[rows[k]].visible = hide ? false : saved[k];
    previousHide = (int)hide;
    return true;
  }
  template<class Context> unsigned visibleMask(const Context& ctx) const {
    if (!ready) return 0;
    unsigned mask = 0;
    for (unsigned k = 0; k < kCount; ++k)
      if (rows[k] >= 0 && rows[k] < ctx.objectCount &&
          ctx.objects[rows[k]].visible) mask |= 1u << k;
    return mask;
  }
  template<class Context> unsigned activeMask(const Context& ctx) const {
    if (!ready) return 0;
    unsigned mask = 0;
    for (unsigned k = 0; k < kCount; ++k)
      if (rows[k] >= 0 && rows[k] < ctx.objectCount &&
          ctx.objects[rows[k]].active) mask |= 1u << k;
    return mask;
  }
  unsigned savedMask() const {
    if (!ready) return 0;
    unsigned mask = 0;
    for (unsigned k = 0; k < kCount; ++k) if (saved[k]) mask |= 1u << k;
    return mask;
  }
};
}  // namespace CloseoutNightGeometry
