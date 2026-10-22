# A10 paint engine evidence

This snapshot qualifies the default-OFF public engine paint API and whole-image
selection, not generated-game lifecycle or physical PS2 performance.

## Verified

- `TYRA_VU1_EXP_PAINT=0` retains original bag layout, uniforms and VCL images;
  paint fields/checks are conditional and no PAINT image is linked in the ELF.
- Enabled engine uses accepted-basis uniforms, the generated VU paint prepass,
  and one shared Clip TC FOLD PAINT image for all TC/TD/TCE wrappers.
- Each target build passes 17 runtime capability checks plus null-bag rejection:
  real legacy clipping, resident-class removal and program override changes,
  missing streams, non-MVP, non-HIGHLIGHT2, lighting and billboard exclusion.
- Both builds complete 54 nontrivial CT32 readbacks across fog off/on. Enabled
  EE/VU/EE pairs and disabled all-EE groups are exact RGBA. All 54 images also
  equal A09 V3, and fog has a visible raster effect in every matching pair.
- Host ASan/UBSan compiles the actual eligibility method and render guard with
  both flags. A removed return under NDEBUG-style assertion removal is caught.
  This is a source-fragment test, not a whole release-game execution.
- All 32 wrapper preprocessor combinations select the expected symbols and
  shared ownership. The original 15 cull/clip VCL files are unchanged. Four
  generated-image numerical/GIF oracles and rebuilt editor --vu-check pass.
- Each native source/ELF binding covers 471 files. The verifier's new explicit
  `--symbol paintRasterCull` preserves its old MotionProbe::rows default; a
  deliberately missing witness symbol is rejected.

## Builds

Enabled: `e40a3cfa06aff16b5ae7c28b14fa5e4ea2ae74ff7748893d44e132358bfa7989`.
Disabled: `923b7c1f4f3d8f61d340bc3b3bfbd8a05f14a0d8427218bdb9f71cc158517bdd`.
The enabled symbols contain Clip TC FOLD PAINT (404 slots) and Cull TCE ENVN
PAINT (166 slots). The disabled symbols contain neither PAINT image.

The first enabled compile omitted the private motion header needed by unused
TerrainGame translation units. It failed before launch; the corrected fixture
imports the header only, not engine motion hooks. The initial binding attempt
required MotionProbe::rows, which this isolated engine no longer references;
that failed before launch too. The explicit raster-witness check replaces that
inapplicable condition without relaxing source/ELF equality. Early staging-only
files were not deployed. Both completed emulator runs were stopped.

## Contents And Limits

`enabled` and `disabled` preserve bound sources, ELF, logs, settings, raw outputs
and texture. `host` preserves baseline sources and host verification; `tools`
preserves the emitter, staging and checking tools. All files are covered by the
SHA256 manifest. The full experimental flag must agree in engine and game:
conditional texture-bag fields make per-TU mismatches an ABI error.

No authored maps were changed. Engine defaults remain OFF. Generated-game
accepted-basis state, normal-stamp invalidation, night/roads, scene reload and
LOD/fallback integration remain A11. The physical console still repeats A06's
freepad DMA Busy; its owned host server is retained. No reset, physical launch,
60-Hz claim or shutdown occurred.
