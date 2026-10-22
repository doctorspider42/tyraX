# Private SPR/CALL runtime trials

The October 4 private experiments implement and physically price direct SPR prefix construction and native CALL/RET replay without changing the production renderer.

## Decision

Neither candidate improves this stationary night fixture. Keep the current production path and preserve these implementations as [immutable experimental sources](tyrax2-spr-call-prototype-sources-2026-10-04/README.md). These are measurements of the implemented policies, not a proof that every possible SPR or CALL design must be slower.

| Candidate | Physical order | Candidate minus baseline, own non-pacing windows (ms) |
|---|---|---:|
| V2 direct SPR, byte guard disabled | Off / On / Off | +0.227164 / +0.177647 |
| V2 direct SPR, byte guard disabled | On / Off / On | +0.388999 / +0.272948 |
| V2 initial CALL | Off / On / Off | +19.146800 / +19.218701 |
| V3 corrected CALL | Off / On / Off | +3.532270 / +3.331643 |
| V3 corrected CALL | On / Off / On | +3.470513 / +3.517645 |

Each contrast compares arms of its own ELF and source version. The baseline retains candidate bank/cache footprint, and targets may persist across phases; cold-cache equivalence is not established. The V2 and V3 numbers cannot be subtracted to claim a production optimization. Existing pacing is excluded by the existing whole-loop counter; the remaining elapsed interval includes observation and timing response and is not a pure EE bill. No common observer fee is subtracted. Direct SPR and corrected CALL presentation marker periods remain approximately 33.366 ms; there is no 60 FPS acceptance or television photon-cadence measurement.

## What was implemented

Direct SPR constructs transactional prefix chunks in the private 16 KiB scratchpad and transfers committed chunks into leased frame RAM before exposing VIF1 TADR. Existing cache publication and completion fences remain. A separately enabled byte guard compares the final prefix with the original RAM writer. It passed on PS2 with 320 prefixes, 640 completed chunks, zero refused transfers and zero guard failures. The user confirmed a normal physical image. Guard-enabled elapsed results are diagnostic and are not the guard-disabled SPR price.

CALL caches bounded immutable native recipes, emits CALL/RET targets and retains target/payload leases through completion. Ineligible recipes and unavailable destinations use the original path. V3 first checks whether an unleased destination exists, then skips pending payload spans while preserving the original VIF parser's full state and command handling. This avoids unnecessary decoding in the private cache; the production parser is unchanged. Single-REF diagnostic replay remains enabled, so successful CALL counts are not equivalent to avoided multi-record encoding. The optional multi-record policy bridge from the source proposal was not installed.

The initial actual SDK build caught distinct `u32*` and `uint32_t*` types despite equal word size. V2 corrected two local snapshot-word declarations without changing layout. V3 changes only the cache header relative to V2. Native provenance binds the actual mirror, dependencies, compiled header, ELF and symbol text. Host controls passed at O0/O2 for CALL, direct SPR and joint mechanics; the separate fragmented/randomized bulk parser oracle passed 44,312 checks at each level.

## Runtime qualification

Accepted runs complete 5400 loops with stable 135 Core calls and 49,794 vertices per sampled frame. Each retains 384 cadence rows, 384 scope rows and 15 chunks; all three phases keep the sampler enabled with 518 Count reads per phase. Phase changes occur before pad/game/frame work. Guard selection remains constant for each boot.

The combined V2 guarded emulator trials include active-candidate rasters. The V3 emulator raster was taken after DONE in the final Off phase, so it qualifies that image only. Physical V3 reverse order was left running with CALL active after DONE: the user confirmed that the car, lights, shadows and HUD looked normal without flickering or stretched triangles. The owned client was stopped only afterward. The physical SPR image was also confirmed normal.

No target semantic chain validator was enabled in the timed native builds (`TYRA_VIF1_CHAIN_CHECK=0`); host validator checks, guarded byte comparison and human visual observations are separate evidence. Scratchpad ownership was audited for this private fixture and acquired with idle channels; it is not a global BIOS/library reservation guarantee. The finalized-prefix copy-only mode has no physical result here. These parked trials do not qualify moving routes, scene transitions, other maps, cache-flush removal or a supported new renderer mode.

No-start attempts with empty logs/network failure are preserved and rejected rather than included in timing or described as candidate rendering hangs. Completed raw captures, frozen sources, binaries and native evidence remain in `F:/Projects/tyrax2-lab-20261001`; the repository preserves compact evidence pins and the exact changed source/harness bytes rather than all external assets and SDK inputs.

The [compact runtime record](tyrax2-spr-call-runtime-2026-10-04.json) preserves the accepted capture closures and their own controls. The [V10 batch/texture measurement](tyrax2-batch-texture-scopes-2026-10-04.md) is a separate inclusive interval and cannot be combined with these candidates or earlier overlapping scopes to derive a GPU/EE partition. Cache-flush removal retains the [existing publication gates](tyrax2-spr-staging.md).
