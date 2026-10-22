# Revised bounded whole-Scene PMU timeline proposal

This replaces the long-running PMU v1 design before source freeze or native execution. The existing v1 fixture remains unfrozen and must not be built or released. Root review of this exact timeline is required before implementation.

## Common stopped configuration

At phase799, outside the800..1119 pricing window, read PCCR immediately before any write. Unknown running configuration is invalid0 and receives no write. Establish selector bits with master enable CLEAR: **0x000340D0**. Check readback and mark this locally established stopped state as known. Record original PCCR, setup stopped PCCR and one setup attempt; do not restore unknown values or enable counters at phase setup.

At phase750 and1156, outside pricing, preserve/check configuration control records. The1156 value must be exactly0x000340D0 and no Scene owner may remain active. Counters remain stopped during simulation, presentation, sample exports and all other frames. Cleanup5399 verifies the owned stopped value and performs no enable/reset; if configuration changed, invalid0 and no write. A stopped-selector state can remain without pretending another owner's running state was restored.

## Exact per-Scene timeline, common to Off and On

Only Scene calls whose engine-loop frame lies900..1027 are armed. At every such `TerrainGame::renderScene()` entry:

1. Verify one Scene invocation for the current sample frame, no active/nested owner, unchanged known stopped state, and actual PCCR exactly0x000340D0. A failed ownership/configuration check invalidates the run and performs NO reset, enable or stop write against unknown PCCR.
2. Retain frame and live Scene owner pointer. Zero PMU counters with `mtpc`0/1, `sync.p`, enable locally known selectors through `mtps`0x800340D0 and `sync.p`. This reset/enable serialization runs in BOTH observer arms. Do not touch COP0Count, disable interrupts or introduce DMA/FINISH waits.
3. On only: read ordered I0, D0 using `mfpc` with `sync.p` and compiler memory clobbers. Off performs neither endpoint read.
4. Execute the unchanged Scene body, including its existing waits and any early return.
5. On only: read ordered I1, D1 using the same wrappers.
6. In BOTH arms, before any record export or outside-Scene work, verify live frame/Scene ownership and read actual PCCR. Only the locally known active owner with exact0x800340D0 may write `mtps`**0x000340D0** and `sync.p`. Unknown changed PCCR is invalid0 and receives NO write. Verify stopped readback; propagate instability or ownership failure into NIGHTDONE valid0. The destructor handles all ordinary return paths.
7. After counters are stopped, On stores the bounded raw endpoints. Common state guards and per-frame/phase counters record128 reset/enable/stop sequences,128 entered Scene calls, no nesting, and no residual active owner. On records512 total PMU endpoint reads and128 rows; Off records0 endpoint reads/rows. Three PCCR reads per successful Scene (entry, pre-stop, stopped readback) are COMMON and explicitly counted outside the differential endpoint-read budget.

The counters are armed during one sampled Scene only, rather than continuously through the phase. There are no wrap-tolerant deltas: retain ordered raw endpoints and provisional nonwrapping differences. A decreased/high-bit endpoint remains a rejection, not protection against overflow.

## Cost and safety limits

This same-ELF contrast prices only the four additional On `mfpc` reads/syncs, row writes and associated branches. Per-Scene reset/enable/stop/PCCR checks and serialization are common to both arms and remain **unpriced against production without the observer**. They can substantially perturb the original workload and cache contents. Neither On raw miss differences nor the Off workload becomes an unperturbed production measurement.

Existing whole-loop Count samples can bound accepted completed Scene durations retrospectively. They cannot guarantee an upper bound if the next Scene hangs. Primary counter width, overflow behavior and event maximum increment rate therefore remain separate release gates. No qualified miss totals, overflow protection, per-miss stall cost, miss rates or hardware safety claim follows from this design review alone. No additional Count watchdog is proposed.

## Exact source changes after root review

- Existing `night_ablation.hpp`: replace long-running setup with stopped setup; add explicit ownedStopped/ownedActive state, per-Scene common reset/enable/verified stop, and three common PCCR reads per successful Scene. Keep four endpoint reads On only. No global NDEBUG or assertion changes.
- Existing `night_runtime.hpp`: phase799 stopped setup,1156 stopped-state gate, source-bound phase counts for128 resets/enables/stops and384 common PCCR reads; deferred endpoint export unchanged;5399 stopped ownership verification and no blind restoration.
- Existing generated `game_physics.gen.cpp`: retained RAII boundary; Scene geometry/effects/waits unchanged.
- Plan/source inventory stays kind27/501; all manifests remain unfrozen until review. Production core/qbuffer restore unchanged.
- Strict parser: verify stopped control values, exactly128 common sequences in BOTH arms,384 common Scene PCCR reads,512 On endpoint reads/0 Off, complete raw rows, cleanup stopped state and no residual active owner. Reject unknown entry/exit configuration, owner/frame changes, missing stop, nesting, extra reads or Off data. Retain inherited loop capture plus native/provenance/ELF/symbol/assets closure.
- Host guard controls: exercise the full ordered sequence and all early returns; mutate unknown PCCR at entry and pre-stop and prove reset/enable/stop writes are absent for the unknown owner. Distinguish source behavior controls from primary event semantics and actual target activation.

No revised source has been implemented by this proposal.

## Permanent failure latch revision

The unfrozen draft now permanently latches any PMU/configuration/context/endpoint failure until a new program starts. Later samples and phase799 never configure, reset or enable after failure. An already entered scope still stops only its matching locally owned PCCR; unknown changed ownership receives no write. The pre-latch source/harness and initial executed root host proof are preserved in pmu-scene-failure-latch-before-v1. The updated host source harness is prepared but has not been compiled by this revision. Runtime release remains blocked by unresolved bounded enabled lifetime.
