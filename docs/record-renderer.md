# Record renderer: retained draw records for StaPip

This page is the plan and the running record of the rewrite of TyraX's
static-pipeline submission into a retained, record-based renderer. It
continues docs/dod-submission.md (frame recording, the stamp pass, the
crossing pass) and the architecture loop (docs/architecture-loop.md), whose
last finding was that the per-route slices had run out: StaPip still spends
57-59 % of its time in cache misses on the headline benchmark, and the next
gain needs the game's per-frame "submit every visible bag" loop and the
general `renderNow` path replaced by contiguous draw records end to end.

Branch `claude/record-renderer-1412cf`, started 2026-11-07 from
`claude/perf-integration` at 2.13.1.

## Target shape

1. **Persistent draw records.** One record per drawable (terrain chunk pass,
   static batch, road and procedural chunk, scene object part, car part,
   wheel, fx), in fixed-size SoA columns: world box, rules, the bag, later the
   resolved state key and the stamp slot. Written on load / spawn / rebuild,
   not per frame.
2. **Change reporting.** The game reports changes (geometry rebuilt,
   transform, visibility, material) instead of submitting every draw. Every
   writer keeps the contract; verify builds police it.
3. **Stage-major frame.** Short passes over contiguous records: visibility,
   classification (inside / crossing / culled), emission (head copy, MVP and
   fog patch, REFs to the baked streams). `renderNow` only for new or
   changed draws.
4. **Multi-view reuse.** Split screen, portals, mirrors and env probes run
   the same record tables through another view.
5. **Parity** with the editor viewport where code is shared.

## Phases

| phase | what | status |
|---|---|---|
| 1 | Record lists + change reporting for terrain chunks, static batches, road and procedural chunks | built (below) |
| 2 | Scene objects and cars | - |
| 3 | Stage-major passes from records (+ scratchpad) | - |
| 4 | Multi-view | - |
| 5 | Delete the old per-draw path | - |

## Phase 1: record lists for the world producers

### The record list

`Tyra::StaPipRecordList` (`stapip_records.hpp`) is a list of draw records
at positions the producer chooses; the position order is the draw order. A
record holds:

| column | contents | read by |
|---|---|---|
| box | world box lo / hi (2 qwords) | every visibility rule |
| meta | rule bits, group bits, LOD tier, grid cell, "same box as previous" | every record |
| aux | an aux box (draw-distance box, portal exit rect), two points (centre, LOD reference), two distances | only the rules that need them |
| bag | the `StaPipBag*` | emission |

`cull(view, out)` is the visibility pass. It walks the list once and writes
the visible positions in order. Every rule reproduces the test the
producer's old loop made, with the same arithmetic:

| rule | old test | used by |
|---|---|---|
| `kRuleFrustum` | `CoreBBox::frustumCheckAABB` on the world box, same planes | all |
| `kRuleSplit` | `outsideSplitBand` (split-screen half) | all |
| `kRuleGround` | `groundBoxDist2` > reflection-probe ground radius | terrain, roads |
| `kRuleDistCentre` | chunk centre farther than its draw distance | roads, procedural |
| `kRuleDistBox` | eye farther than the draw distance from the member-centre box | static batches |
| `kRuleLod` | distant-version switch (near and far tiers) | procedural |
| `kRuleRing` | chunk cell outside the streaming ring rects | terrain |
| `kRuleExit` | chunk rect behind the portal exit plane | terrain |
| `kRuleOcclude` | `occlusionHiddenAabb` (software depth), asked last | static batches without an occluder member, procedural |

A record with group bits is eligible only when the view's group mask has
one of them: the terrain's ground-shadow pass is group 1, masked out while
the day cycle has no sun up. Consecutive records with the same box and
rules (a terrain chunk's base, layer, AO, shadow and emissive passes) share
one box decision.

### The change contract

A list is rewritten from the producer's live state when the producer's
**revision class** epoch moves (`DrawEpochs::g.cls`, docs/dod-submission.md
"The revision contract"):

| list | positions | rewritten when |
|---|---|---|
| terrain | chunk slot x (base, layers..., AO, ground shadow, emissive) | `kClassTerrainChunk` moved, or the slot count / layer stride changed |
| static batches | batch index | `kClassStaticBatch` moved, or the batch count changed |
| roads, procedural | `procChunks` index (roads are owner -3) | `kClassRoadChunk` moved, or the chunk count changed |

Every writer of these bags already bumps its class for the stamp pass
(`DrawEpochs::ClassWriter` around `buildTerrainChunk`, `updateTerrainChunks`,
`rebuildStaticBatch`, `buildStaticBatchList`, `procFinishChunks`,
`procGenerateVolume`, `spawnPrefabAt`, `applyBakedLampsToChunk`,
`loadScene`, ...). So the change report for phase 1 is the existing class
bump, and STAMPVERIFY's revision counters already police it for the bags'
companions. What the class contract did not cover before is the producer's
**draw rules**: boxes, the chunk cell, draw distances, the LOD reference,
the bag pointer and count. RECLISTVERIFY polices those (below).

The static batches' per-frame member scan (a member that became dirty
demotes itself and the batch is rebuilt) stays in `renderStaticBatches`
for this phase; it moves to change reporting with the scene objects in
phase 2.

### Verify

`TYRA_STAPIP_RECORD_LIST_VERIFY` 1 (in `stapip_records.hpp`, default 0).
Every frame, for every list and every view:

- the old loop runs **dry** (`recDry`: it collects the bags it would have
  submitted instead of submitting them), and its sequence must equal the
  records' visible sequence, element by element (`seqBad`);
- the list is rebuilt from the live producer state into a scratch list and
  compared position by position with the retained one (`recBad`). This
  catches a writer that changed a draw rule without a class bump even on
  frames where the culled sequence happens to agree.

`RECLISTVERIFY list=<name> frames= draws= seqBad= recBad=` every 600
frames per list; the first 8 bugs of each kind print a `RECLISTVERIFY BUG`
line. Clean is `seqBad=0 recBad=0`.

Why a sequence check proves the DMA stream: phase 1 changes only which bags
reach `StaPipCore::render` and in what order. Everything downstream of that
call is unchanged code. Equal sequences (same bags, same order, same light
reception, same revision class tags) give StaPip the same input, so the GPU
receives the same stream.

`TYRA_STAPIP_RECORD_LISTS` 0 is the kill switch: the old per-item loops.

### Known difference

The revision class tag (`StaPipBag::revisionClass`) is now written when the
list is rewritten, for every live bag of the producer, not when the bag is
first drawn. A batch bag that an env pass draws before the main pass ever
did is therefore tagged one frame earlier than before. The tag only selects
which facts the stamp pass re-checks, and both answers are verified
(STAMPVERIFY), so the frame's output is the same.
