# A29 Bounded VU Clip Output

Before this lead, read the investigation register and A28's architecture audit.
A24's package grouping/caps, A25's consumer bypass and C06's cached expansion
are not this proposal: change the VU output contract to remove whole-package
fan-out sizing, then consume immutable native strips rather than EE lists.

The private actual-generator prototype supports list and native-strip inputs,
dispatching complete clipped polygons from alternating bounded output rings.
Strip input advances by one and swaps the first two input corners on odd
primitives, including all matching attributes. Header prim-tag word3 carries
initial package-local parity and is cleared in the emitted GS descriptor.
This is an intermediate correctness gate, NOT a runtime fix or speed claim.
Production emitters/defaults are untouched. No extra hot renderer counters.
`tools/tyrax2-clip-ring-host.py` copies and pins the current generator/simulator,
derives a private clip body and records RAM at every simulated XGKICK so ring
reuse cannot hide earlier output corruption. `verify-clip-ring.cpp` compares
ordered vertex words AND state tags against individually clipped original
triangles, verifies unchanged uploaded data and both double-buffer halves.

For TC: two rings of 9+21*3=72 qwords, 72 list vertices with three streams,
and two header qwords occupy 362/460 qwords. Clip-only metadata at 1016 does
not overlap the scratch polygons ending at 1015. Cull's single-colour copies
also use this tail but are not concurrently active; runtime integration must
preserve that separation and the existing final PATH1/VIF drain contract.

Ring reuse reasoning: ring A is kicked, then B is produced/kicked, then A is
written. The second kick's synchronization makes the preceding A transfer
complete before those writes. Store latency and scheduled machine instructions
still need inspection; an abstract interpreter is NOT asynchronous proof.
The [Sony VU manual, sections 3.4.9 and XGKICK](https://manuals.plus/m/a858ef55cd2638accdfca612d5359eb6e48a48c7fd6792b18080d866e610fde5.pdf)
describes PATH1 synchronization and warns against rewriting ongoing transfers.
Every triangle emits state even when clipping leaves zero geometry.

Host V5 passes 1280 cases/29392 triangles, with 27050 nonempty and 9937
fan-expanded source triangles. Covers both C/TC images, list/strip, both halves,
all64 plane masks, both initial strip parities, single/many colour, shared
directional/env/spot variants and wide side/far crossings. Actual VCL/DVP
assembly succeeds: C list346/strip376 and TC list378/strip404 instruction words.
These individual image sizes are NOT a qualified whole resident-set budget.
V2 initially ran out of C VF registers; private ring-only late tag reloads
fix lifetime without changing the ordinary generator. V4's rejected harness
gave C's positive lighting variant single-colour layout without its required
normal stream; corrected V5 stages the mandatory stream. No physical run was
made from either failed gate. Fixtures remain outside the repo at
`F:/Projects/tyrax2-clip-ring-20261011-host-v1` through `host-v5`.

V6 repeats V5, passes three rejection controls (vertex corruption, parity
corruption and wrong ring address), and verifies the scheduled XGKICK pair's
upper instruction is NOP in all four assembled images. `host-v6/` preserves
the actual derived sources, per-source pins, oracle output, VCL/VSM/objects
and assembled sizes. It is host/assembly evidence, not a PS2 A/B archive.
Read-only recheck recompiles the archived sources into an external directory,
reproduces the oracle output and all four emitted images byte-for-byte, and
verifies the 30-file inventory before/after. No archived file is modified.

## Runtime Integration Progress

Host V8 adds a HYBRID input ABI: primitive-header word3 is zero for legacy
lists, or 2|parity for native strips. One image handles both without changing
the six-plane packed-count mask. Folded lighting and compiled paint prepass
are retained. V8 passes 7680 cases/176352 source triangles, 162392 nonempty
and 59561 fan-expanded, plus three rejection controls. With active paint,
the original prepass intentionally overwrites the uploaded VU colour block;
all other input words remain unchanged. This is not an EE source-RAM write.
Actual images: C hybrid-fold392, TC hybrid-fold-paint464 instruction words.
`host-v8/` preserves 74 source/oracle/assembly files with an inventory.

`tools/tyrax2-clip-ring-fixture.py` derives a PRIVATE runtime fixture from bound
A28 V2. It keeps stable terrain focus and quiet ordinary logs in BOTH arms.
Refined subrange classifications/omissions remain unchanged; only adjacent
true-clip ranges merge and borrow native source streams, capped at72 inputs.
Cull ranges keep the old expansion route. Unrefined/custom cases retain list
input; custom overrides are rejected by this fixture's switch. Image switches
occur at warm phase/checkpoint boundaries through the existing VU1-clipping
drain/cache invalidation path, never by swapping images during priced travel.
The normal queue snapshot/data-lifetime mechanism remains in use.

Runtime V1 native build/binding PASS: 481 source/mirror files, linked witness,
ELF `b1be0792fde17a762de08c7e731a3390b97b9fb20e3ee410eb73e2d8b2d5528c`.
Linked nm confirms the candidate sizes. Unique main resident-set arithmetic
is 1920-338-404+392+464=2034 words, including both billboards, against2042
before the six-word finish helper. Emulator logs retain12 resident programs
and resident billboards throughout the switches; the candidate assertion passes.

V1 hidden emulator completes six trips, DRIVEDONE valid=1 and all60 newly
exported cold main/env rasters are exact across all five arms. SDK automatic/
manual header oracles and25-chunk own/neighbor contracts pass. Owned emulator
PID37/Xvfb26 stopped after completed exports; console remains powered on.
This is PRELIMINARY, not full A29 qualification: V1 retains the old
packet-shaped input hash and no separate native activation gate. Its different
counts/hashes are expected from topology/grouping, but cannot be waived.
Do not price V1 or apply the A28 reader as an A29 qualification.

The revised fixture generator uses `tyrax2-clip-ring-input.hpp`, a cold-only
ordered-triangle/attribute/mask oracle independent of packet grouping. Host
`verify-clip-ring-input.cpp` passes4480 cases, all stream combinations, odd/even
starts and different list/native group boundaries; parity/mask/attribute
corruption and disabled observation controls pass. Candidate cold native-call
activation must be positive; every control must be zero. Header byte/state
checks remain per actual emitted header, but header COUNTS must no longer be
required identical when dispatches are deliberately removed.

Pending: rebuild V2 with the revised canonical/activation gates, add the A29
reader with rejection controls, requalify hidden emulator, then physical
same-ELF A/B with normal roads/reflections
and stable terrain focus in BOTH arms. Do not promote this intermediate shape
or claim stable 60 fps without those gates.

The A29 reader (`tools/tyrax2-clip-ring-analyze.py`) preserves the inherited
travel, live-road/reflection, quiet-window, terrain and all-five-arm raster gates.
It compares canonical ordered input across every arm, requires fewer positive
SDK-verified headers in the candidate, positive native activation only there,
and the linked 2034-word residency including billboards. Source/shader pins are
checked against the bound fixture. Missing/inactive native witnesses, native
controls, empty native triangle totals and altered canonical hashes are rejected.

V2's first native compile rejected a missing diagnostic include: the inherited
header writer had received ClipTrial transitively from the old consumer header.
The canonical host-independent header intentionally lacks that dependency.
The private runtime generator now includes clip_capacity.hpp explicitly in
the writer; preserve the failed build log before rebuilding. No runtime result
or physical price follows from the failed compile.

V2 rebuild and native mirror binding PASS:481 files, ELF
`2a1a2dc892e16f8263ab256052d43d38c2f6e978bb176b6d036a4619bf5a4cd9`.
The74-file V8 inventory is unchanged; both runtime shader source hashes match
its qualified entries. First emulator cold point compares752 ordered triangles
and2256 canonical vertices with identical hashes in all arms; candidate
native40 calls/204 triangles, controls zero. This partial point is not full
qualification or a price. The fresh six-trip run is in progress.

V2 hidden emulator full qualification PASS: six trips, DRIVEDONE valid=1,
all60 exact main/env rasters,30 equal canonical-input contracts,30 positive
SDK header oracles (candidate removes11..12 headers),30 terrain contracts,
positive native36..40 calls/192..208 triangles only in candidates, and all22
reader rejection controls. Linked residency2034 and481-file binding pass.
Owned emulator351/Xvfb340 command lines were verified and stopped after exports.
The same ELF is now on real PS2 via editor-network-reset; six physical trips
are in progress. No physical price/default promotion yet.

V2 deliberately leaves unrefined/cache-unavailable partial strips on the legacy
expanded-list path. Source review confirms this excludes the ordinary road
amplification documented in A24 V3/P06:66 inputs still become16 four-triangle
jobs. The next same-mechanism revision uses `--native-fallback` to borrow these
original strips too, capped at min(72,maxVertCount)-2 triangles. Original
package/mask/order, refined-route decisions and lifetime snapshots remain.
It must prove positive cold RINGFALLBACK only in the candidate, canonical input
equality, SDK headers and exact rasters before pricing. This is not A24's raised
worst-case fan-out cap: bounded output rings remove that original constraint.
Do not attribute a small V2-only result to the full native-input proposal.

## Refined-Only Physical Result

V2 completes all six physical trips, canonical/native/header/terrain contracts,
all60 exact raster exports and22 reader controls. Day Off/On/Off mean net work:
15.805348 /15.834176 /15.934273 ms; night18.060121 /18.035058 /18.095193 ms.
Candidate42.130 day and37.131 night Hz: NOT60. Bracketed mean savings only
0.035635 /0.042599 ms; spatial0.046004 /0.038044 ms. Night candidate p95
26.111640 exceeds max control25.884786 by0.226854 ms, failing the0.15-ms
first-order gate. No promotion/reverse micro-tuning. This refined-only variant
does not materially solve EE preparation. Archive both qualified platforms
before the broader fallback revision; physical server30276 stopped only after
DRIVEDONE, console remains on. No scene ceiling proof follows from this result.

Root A29 scripts and their reader/archive/helper dependency chain are explicitly
unignored in .gitignore so future source commits retain the runnable tools;
downloaded PS2 utilities and build artifacts remain excluded.

V2 archive contains646 files; both archived-tool reanalyses reproduce the
original platform reports byte-for-byte and its inventory remains unchanged.

## Native Fallback Revision

V3 native build/binding PASS:481 files, ELF
`7d585e09662f2a83fbf9c34d198d2dd1159882d4e55db5b764b5ad36a9ec4ab4`.
Host canonical4480/four controls still pass, now also checking cold fallback
marker/reset/disabled observation. The broader fixture notes fallback admission
only under the existing cold observer; there are no new hot clocks/censuses.
Its first hidden-emulator checkpoint admits6 fallback jobs/226 triangles and
compares all752 canonical triangles/2256 vertices exactly. Candidate fallback
is mandatory in all six cold checkpoints; controls must remain zero and four
new reader negatives reject missing/inactive/contaminated/empty fallback.
This partial emulator point is not full qualification or physical pricing.

V3 full hidden-emulator qualification PASS: all six trips,60 exact main/env
rasters, ordered canonical inputs,30 native plus30 fallback admission contracts,
SDK headers/terrain/quiet guards and26 reader rejection controls. At the first
day checkpoint, actual headers drop242 to179. Resident set stays2034/2042;
no shader/default change beyond the private A29 image. Owned emulator34/Xvfb23
verified and stopped after completion. Same ELF physical deployment is underway
via the editor procedure; no physical V3 result/promotion yet.

## Broader Physical Result

V3 completes all six PS2 trips and all correctness gates:60 exact main/env
rasters, canonical ordered triangles, positive native/fallback only in candidate,
SDK headers, terrain/quiet contracts and26 reader negatives. Day Off/On/Off
mean net work16.005037 /15.307210 /15.907616 ms; night18.157235 /17.470447
/18.117075 ms. Bracketed mean savings0.649116 /0.666708 ms, spatial0.786443
/0.613253 ms. Candidate p95 improves to21.333157 /24.849060 ms versus max
controls22.596558 /25.559892 ms. First-order performance gate PASS, but actual
entry cadence43.303896 /38.433009 Hz is NOT60. No production promotion yet:
this architectural prototype needs stronger benefit/order validation before
its broader ABI/residency change is justified. Owned server33296 stopped after
DRIVEDONE; devices remain on. Do not subtract V2/V3 across ELFs to price a
branch; only each series' internal same-ELF contrasts are causal evidence.

V3 archive contains645 files. Both archived-tool reanalyses reproduce original
platform reports byte-for-byte; the full inventory is unchanged afterward.
V2's646-file archive remains unchanged too. Last Codex allowance check90%
remaining; no goal-completion/allowance/failure shutdown condition applies.

## Next Source Questions

Before another revision, check A24/A25/C06/C11/C18/H02 and the register again.
Two concrete contract issues remain, not generic cache tuning:

- The PRIVATE ring emitter now repeats the full7/9-qword GS state block for
  every source triangle, including empty output. The legacy shader emitted it
  once per multi-triangle job. This is introduced prototype work, not evidence
  of an original EE bug. Investigate retaining first state even for an empty
  first polygon, then kicking geometry-only tags. Require cumulative effective
  GS-state/ordered-vertex host comparison, actual scheduling/resident2042-word
  bound and exact runtime rasters; do not waive state correctness or displace
  billboards to fit.
- EE still classifies every refined four-triangle block against screen/guard
  planes and repacks safe Cull ranges into lists. The bounded ring contract may
  make coarse native submission cheaper than this fine "optimization". This
  is NOT simply disabling C11 under its old fan-out contract. A candidate must
  preserve macro packages/masks/geometry and explicitly qualify all pre-route
  source triangles plus output, since post-route clip input counts legitimately
  differ. Do not waive the current canonical gate or use destructive omission
  as speed evidence. These questions are source-backed, NOT measured fixes.

The next private host revision adds a hybrid state-once body: the first polygon
kicks the full state even when empty; later kicks start at the geometry tag.
Metadata1017.y stores the subsequent-kick bias without changing input ABI.
Ring alternation still uses full ring bases, not biased kick addresses. The
host oracle compares cumulative effective state and each primitive descriptor
against original per-triangle output, including both input topologies/fold/paint.
Missing first state and corrupted first state join the existing vertex/parity/
ring controls. Actual assembly and full residency remain pending; do not deploy
or infer FPS from an interpreter-only result.

Host V9 passes11520 cases/264528 source triangles,243381 nonempty/89481
fan-expanded, including3840 state-once cases and291 empty-first cases. All
five controls pass. Actual C once-fold396 /TC once-fold-paint470 words make
the whole set1178+396+470=2044>2042: REJECTED for runtime, no deployment.
V10 reuses the already-loaded input step across adjacent optional stream
pointer increments; no new register or synchronization removal. Repeat all
host/assembly checks and full residency before runtime integration.

V10 repeats all11520/264528 cases, exact ordered output/cumulative effective
state, five controls and291 empty-first cases. Actual selected images now
C394 /TC466 words: whole main set2038/2042 including billboards, before the
six-word finish helper. Existing uniform FLUSHE, wrap FLUSH/DIRECT and texture
mutation align3D barriers remain; no external state mutation may interleave
with a running job. The --state-once runtime fixture rejects other host sizes
before building, and the reader checks actual linked ranges/full residency.
Native/emulator/physical qualification remains required; no FPS claim yet.

The first state-once derivation had an overly broad symbol-count assertion:
the source has four C fold and four TC fold-paint spellings (two declarations
and two uses each), not six identical fold references. A second mistaken
three-per-spelling assertion is rejected too. Actual partial-source inspection
confirms four; the helper now checks/replaces each spelling independently.
Preserve rejected partial V4/V5 fixtures; use a fresh V6 derivative.

Runtime V6 native481-file binding PASS, ELF
`43f380e847cdaf4c73af08351f26df4125f886c787632662d08a1a7514657748`.
Linked C394/TC466 and full2038/2042 residency pass; six hidden-emulator trips
qualify all60 exact main/env rasters, canonical/native/fallback/SDK/terrain/quiet
contracts and26 reader controls. Owned emulator31/Xvfb20 verified and stopped.
The same ELF is deploying to PS2 through the editor procedure; physical price
pending. No public renderer/default change or goal completion claim.
Host V10's94-file archive recompiled read-only reproduces its oracle result and
all20 emitted image sources identically; inventory unchanged. Rejected V9's93
generated/oracle/assembly files are also preserved with an inventory.

The first V6 physical attempt stops producing output in night candidate phase4,
after four travel windows and20 main/env pairs. No DRIVEDONE: reject it as
incomplete, do not price its partial day results or assert a renderer root
cause. Preserve physical-interrupted in the external V6 fixture. Owned server
2608 was stopped; fresh303-input staging and the editor-equivalent reset launch
the identical ELF with server24416. Startup rendering succeeds. Retry physical
qualification is pending;89% Codex allowance remains and devices stay on.

V6's retry also stops output in night candidate phase4, last frame9120 instead
of9000. Both attempts lack DRIVEDONE and export only20 main/env pairs, not30:
physical qualification REJECTED AS INCOMPLETE. No assertion/root cause is
established. The live owned server24416 was stopped after unchanged output was
rechecked; preserve both directories and physical-status.md in the external
fixture. No partial-day price or state-once promotion. A30 coarse EE routing
will hold the already hardware-qualified V3 shader active in both arms instead.
Host/emulator state-once successes do not substitute for hardware correctness.

`tools/tyrax2-clip-ring-archive.py` preserves both completed platform reports,
bound runtime sources/ELF, shader pins, rejected initial build output, raster
exports and the reader's dependency chain. It refuses missing canonical,
native-activation, quiet-transport or residency qualification. Preserve failed
or regressed physical candidates too; a qualified archive is not promotion.
