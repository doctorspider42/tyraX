# Texture filtering (Smooth / Pixel / Pixel close-up) — implementation plan

Design: https://claude.ai/artifact/76NzCheEZfKDhRAgFMHbod (4 screens).
Variant B: the filter is a property of the **draw** (material/part), not of the
VRAM image, so atlas pages are never split and cost nothing extra.

## Modes → GS TEX1

| UI name        | stored      | MMAG    | MMIN    |
|----------------|-------------|---------|---------|
| Smooth         | `smooth`    | LINEAR  | LINEAR  |
| Pixel          | `pixel`     | NEAREST | NEAREST |
| Pixel close-up | `closeup`   | NEAREST | LINEAR  |

No mipmaps exist (`lod.max_level = 0`), so "close-up" is the only knob against
far-distance shimmer.

## Data flow

```
project (default + per-texture-path map)
  → texbake: "# tyra-filter <mode>" after map_Kd in the BAKED .mtl
      (per member, so an atlas page can carry mixed filters)
  → tmdl bake: part flags bits 1..2
  → LeanObjLoader / TmdlLoader → LeanObjMaterial::filter
  → generated game: part.filter / GameMaterial::filter
  → StaPipTextureBag::filter (DynPip: same on its texture bag)
  → qbuffer renderer: TEX1 qword per bag (already emitted per bag)
```

Scope v1: static `.obj`/`.tmdl` models, primitives, animated (DynPip) models,
static batches. Out of scope (stay smooth): terrain layers, sky, particles,
decals/mirrors/portals, HUD/2D, lightmaps/AO/probes/post-fx. Terrain is the
natural phase 2 (its textures are known at codegen; no hint needed).

---

## 1. Model — `src/project.hpp`

- `ProjectSettings::textureFilter` — `std::string`, default `"smooth"`
  (next to `textureQuant`, ~l.1830). Smooth default = zero visual change for
  every existing project.
- `Project::textureFilterOverride` — `std::map<std::string, std::string>`
  keyed by the res-relative texture path, exactly like `atlasControl`
  (~l.4138). Absent = follow the project default.
- Helper `Project::effectiveTextureFilter(const std::string& resPath)` —
  the one place that resolves default vs override; texbake, the viewport and
  the UI all call it.
- Put the map in the **same section** as `atlasControl` — no new `Section`, so
  `kSectionCount` / its `static_assert` (l.4680) stay untouched. (See the
  section-count trap memory: a section added without bumping it deletes data
  on save.)

## 2. Serialization — `src/project.cpp`, `src/version.hpp`

- Write/read `textureFilter` in settings and a `"textureFilter"` object
  (`path → mode`) beside `"atlasControl"` (~l.6962). Omit default-valued
  entries; drop unknown mode strings on read.
- `kFormatVersion` 122 → 123 with the usual changelog comment block in
  `version.hpp`. Re-check the number right before merging — parallel branches
  steal format numbers.
- App version minor bump (new feature).

## 3. Bake — `src/texbake.cpp`, `src/tmdl.cpp`, `src/templates.cpp`

- `texbake.cpp` (~l.69–131): when rewriting a baked `.mtl`, emit
  `# tyra-filter pixel|closeup` right after each `map_Kd` whose **source**
  texture resolves to non-smooth. Do it for atlased AND non-atlased
  materials (one code path). Extend the "is this a hint line" check at
  ~l.131 so a re-bake strips the old hint.
- Bake cache signature: include the resolved filter of each referenced
  texture, otherwise changing a filter does not re-bake the `.mtl`
  (cf. the CRLF/LF signature memory — hash the setting, not the file).
- `tmdl.cpp` / the model bake in `templates.cpp` (~l.35655): write the mode
  into the existing per-part `flags` word (bit 0 = reflRounded today):
  bits 1–2 = 0 smooth / 1 pixel / 2 closeup. Old loaders ignore the bits, so
  no `.tmdl` version bump is needed — but the model bake signature must
  include it.

## 4. Engine — `vendor/tyra/engine`

- `inc/loaders/3d/obj_loader/lean_obj_loader.hpp`: `LeanObjMaterial::filter`
  (`PipelineTextureMappingType`-like enum extended with close-up, or a small
  `u8`), default smooth.
- `src/loaders/3d/obj_loader/lean_obj_loader.cpp` (~l.111): parse
  `tyra-filter` next to `tyra-uvrect`.
- `src/loaders/3d/tmdl_loader/tmdl_loader.cpp` (~l.168): decode flags bits 1–2.
- `inc/.../static/core/bag/stapip_texture_bag.hpp`: `u8 filter` (default
  smooth). Same field on the DynPip texture bag.
- `stapip_qbuffer_renderer.cpp`: `setInfo()` (~l.1731) currently derives
  MMAG/MMIN from `info->textureMappingType`; make it take the bag and let a
  **non-smooth texture-bag filter win** over the info bag. TEX1 is already
  emitted per bag (~l.1590), so no new GS traffic.
  - Stamp/retained routes (`TYRA_STAPIP_STAMP`): the TEX1 qword is part of the
    captured stream, so the filter must be in the stamp's "inputs it cannot
    re-derive" check — or simply be fixed per bag (it is) and verified.
- `dynpip_core.cpp`: `begin()` sets `lod` once per render call; move the
  MMAG/MMIN choice to per-bag where `packet2_utils_gs_add_lod`
  (`dynpip_renderer.cpp` ~l.162) is called.
- Mark every edit `Modified by TyraX`. Remember the stale-libtyra trap: a
  failed engine build leaves the old `.a` and the game hangs silently.

## 5. Generated game — `src/game_templates.inc` (+ `templates.cpp` structs)

- `GameMaterial::filter` (templates.cpp ~l.1727 next to `uvRect`) and the
  model part struct: copy from `mat.filter` in `loadModelAsset`
  (~l.6641 / its twin ~l.9071) and `loadMaterialAsset` (~l.6709 / ~l.9139).
  Both class copies are edited as a pair.
- Every `StaPipTextureBag` built from a part/material sets
  `texBag->filter = part.filter` (~l.9875 anim, ~l.14300, ~l.14645, …).
  Bags that sample engine targets (env map, corona, shadow, lightmap ADD
  pass, volumes) keep the default.
- Live Link: the filter is a bake-time property → a change needs a rebuild,
  like an atlas member repaint. Say so in the docs; no recipe-hash change.

## 6. Static batching — `src/staticbatch.hpp/.cpp`

- `MaterialInfo` (hpp l.141) gains `filter`; the `MaterialInfoFn` the editor
  supplies resolves it via `effectiveTextureFilter`.
- Batch key (cpp ~l.349: `texture == … && keyX … && drawDistance …`) adds
  `&& filter == …`. **This is the bug to avoid:** with atlasing on, a pixel
  wall and a smooth crate on the same page share `texture` and would merge
  into one bag with one TEX1.
- The emitted batch bag sets `texBag->filter`.

## 7. Editor viewport — `src/viewport.cpp/.hpp`

- `texCache_` (hpp l.1476) is keyed by res path — set
  `GL_TEXTURE_MIN/MAG_FILTER` from `effectiveTextureFilter` at upload
  (close-up = MAG `GL_NEAREST`, MIN `GL_LINEAR`).
- On a filter change, re-apply `glTexParameteri` to the cached GL name; no
  re-upload.
- Viewport toolbar menu: Match PS2 / All smooth / All pixel — a viewport-only
  preference (editor prefs, not the project).
- Context menu on a selected object → "Texture filtering ▸" for its
  texture(s); it edits the per-texture override (the submenu header names the
  texture and the use count, as in the design).

## 8. UI

- **Project Settings → Textures** (`src/app.cpp` preferences, wherever
  `textureQuant` is drawn): three cards with live previews, the "N textures
  override" row + *Manage overrides…*, the "Always smooth" info block.
- **Asset Browser inspector** (`src/assetbrowser.cpp`): for PNGs, a
  4-way control Default / Smooth / Pixel / Close-up, *Override* badge,
  *Reset to default*, magnified preview with the effective filter.
- **Tools → Texture Filtering** (new window, model it on `src/atlas_ui.cpp`):
  table of every texture referenced by materials, filters
  All/Overrides/Pixel/Locked, multi-select + bulk set, lighting textures shown
  locked.
- ImGui ID trap: four identical option labels in two lists inside one popup
  collide — suffix `##filter` ids.
- Tests via `--ui-script` need the widgets to be reachable by name; give the
  cards and the segmented control distinct visible labels.

## 9. Docs (same commit — `tyra-docs` checklist)

- New `docs/texture-filtering.md`: modes, where to set them, what is always
  smooth, atlas = no cost, rebuild needed after a change, close-up rationale
  (no mipmaps). Before/after screenshot pair from PCSX2.
- `docs/texture-atlasing.md`: one line under "How it works" — `tyra-filter`
  hint rides next to `tyra-uvrect`; gutter unchanged.
- `docs/model-pipeline.md`: `.tmdl` part flags bits 1–2.
- README feature list + CHANGELOG.
- Skills: `tyra-editor-dev` (project field, bake hint, batching key),
  `tyra-engine-dev` (texture-bag filter wins over info bag). Regenerate the
  `.agents/skills` twin.

## 10. Verification (`tyra-testing`)

1. Codegen-only check: a scratch project (short path under
   `%TEMP%\tyra-editor-test\`) with one pixel texture → baked `.mtl` has the
   hint, `.tmdl` part flags have the bit, `project.json` round-trips.
2. Atlas on, pixel + smooth members on one page → `--atlas-report` shows one
   page; static batching keeps two batches.
3. PCSX2: frozen FPP camera A/B screenshots (pixel vs smooth wall, close-up
   near vs far). Animated model with a pixel texture.
4. Old project loads unchanged (format 122 → no overrides, smooth).
5. Real PS2 pass optional — TEX1 per bag already ran on hardware.
