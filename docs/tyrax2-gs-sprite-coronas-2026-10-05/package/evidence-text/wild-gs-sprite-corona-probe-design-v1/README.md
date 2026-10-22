# V6 actual corona output diagnostic

Source only. Copy two `source/game` files into a fresh V6 pricing-derived
`wild-gs-sprite-corona-probe-physical-v1`: pricing500 becomes diagnostic501.
The dedicated terrain init/loop shim and header keep qualified ready/halt symbols
and beginFrame/usePipeline/render/endFrame/synchronizeFrame/READY lifecycle.
No pricing source, GPU fence or prior probe fixture is edited.

Three-word cfg is `arm case repeats`, arm0/1, case1..4, repeats1..3. Root intends
all four cases repeat1 in both arms, then cases1/4 repeat3 in both arms: twelve
captures/six pairs. Color source epochs are1..3 (base red31/32/33). Typed owner
and arrays persist; exact final source colors select the current bank. All sparse
counters and cold producer oracle stay disabled every iteration.

- Case1:72 vertices, package72, repeated square triangle pairs[0,1,2,0,2,3].
  Planar square at z0, camera z-20, actual fog19..23 and real nonzero symmetric
  spotlight at(0,0,-1), direction+Z, range10, cutoff80, softness1, RGB4/8/12.
  All four corners have equal spot distances/cone factors. Actual constant final
  RGBA/interior F and positive final SPRITE output are required, not assumed.
- Case2:72 vertices, right corners z1. Actual final F must be nonuniform and the
  candidate must preserve complete triangle output. Z/Q/light can also vary;
  this tests safe rejection, not isolated fog causality.
- Case3:72 vertices, source red offset8 at corner1. Actual final RGBA must remain
  nonuniform and candidate triangle payload must match the full baseline.
- Case4:96 vertices/package75 splits a quad. Actual75/21 packages must have no
  request marker and unchanged complete triangle output in both arms.

The probe does not prove ordinary-night SPRITE activation, pixel equivalence,
hardware raster semantics or performance. Root must freeze/build/review native
501 source bytes and run the actual owned capture workflow independently.
