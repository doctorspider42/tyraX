# A09 paint generator evidence

This snapshot records private generator and target-assembled paint correctness
qualification, not a physical PS2 performance result or a production default.

## Result

- `buildPaintExperiment` emits separately named images outside the ordinary
  catalogue. All 45 ordinary shader/wrapper files are byte-identical before and
  after. Final editor `--vu-check` and generator ASan/UBSan pass.
- 8,400 host draws match the C16 prototype, including shared TC/TD paths.
  Three specular-zero instruction mutations are rejected. The actual-source
  scalar oracle checks 73,500 components per variant, all 64 empty-input masks,
  and 300 whole-shader draws per variant. Max scalar error 0.000396729, tested
  GIF words exact.
- Accepted V3: 54 nontrivial raw CT32 readbacks, 18 exact EE/VU groups and
  18 exact EE/EE controls, with observable fog in all 27 corresponding pairs.
  First 27 images also equal the earlier A07 outputs exactly.
- Three analyzer controls cover EE/EE pixel corruption, a one-pixel candidate
  change and a fog no-op. Fog near-clip/view-edge outputs were visually checked.
- Each target build binds 473 source files. V3 links Cull TCE ENVN PAINT
  (166 slots) and one shared Clip TC FOLD PAINT (404 slots). All TC/TD/TCE
  wrappers select that same image. Existing FOLD/ENVN bodies are retained;
  only the generator-owned paint prepass is transplanted.

## Attempts

- Initial WSL staging was stopped while copying, before build/launch; the
  generator chain invokes the Windows editor and needs Windows Python here.
- `rejected-v2`: ELF
  `6180110f3cf703a0e7f940fa34ea9a3ffd5065c3b102a119b7fd6bda2a27a0ed`.
  Completes 54 readbacks but strong fog end at 12 near distances reduces two
  view-edge bases to 110/115 RGB colours, failing the >128 nontriviality gate.
  Geometry is visible. No acceptance or shader-equivalence claim from this run.
- `accepted-v3`: ELF
  `4a2bddae33e119ff8f06c4adc93c0c428a8e56c8291429970c6683e928f46f76`.
  Fog end is 24 near distances. All original gates remain unchanged. Fixed
  texture hash was recorded before launch and checked after. No scene assets,
  production defaults or authored maps were edited.

## Layout And Limits

`host` preserves before/after/final generated output and verification logs.
`host-source` contains the generator/parser/simulator sources used by checks;
`tools` contains reproduction/analyzer tools. Both target attempts retain
native-bound sources, ELF, raw pixels, source/launch manifests, logs, settings
and fixed texture. The final SHA256 manifest covers the whole snapshot.

This synthetic harness is not full-game night, lifecycle, arbitrary custom
shader, physical raster or timing qualification. Those integration gates stay
open. The A06 PS2 run remains stuck in freepad DMA Busy; its owned host server
is retained and no new reset/deploy was attempted. Both owned emulator runs
are stopped. Stable physical 60 Hz and the requested shutdown condition have
NOT been achieved.

See the [decision register](../tyrax2-performance-investigation.md) for A10:
engine opt-in capability/uniforms and A08 accepted-basis state in both generated
game headers, with a default-OFF whole-image selection and full fallback tests.
