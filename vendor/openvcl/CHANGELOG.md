## Unreleased

- The CMake build is now genuinely out-of-source: the executable stays in the
  build directory and integration tests resolve that target directly. The old
  LoopCS register regression now asserts the actual safety property — a reused
  temporary cannot be overwritten before its final store — instead of requiring
  two registers after the allocator learned to shorten loop-local live ranges.
- `.syntax old` no longer decides whether a name is a field selection by its
  spelling. The trailing `xyzw` of an alias was stripped before anything asked
  whether the argument could carry a field at all, and the whole argument was then
  rejected when it could not - so an integer alias whose name ended in x, y, z or
  w was "Invalid argument", and the spellings that escaped that were a hardcoded
  list of English trigrams (`dex`, `tex`, `lex`, `rex`, `sex`). `next_index`
  compiled and `next_matrix` did not; Sony's `vcl` compiles both, identically, and
  reads no field in either. The same list was a silent divergence in the other
  direction: a FLOAT alias called `vertex` in a `:dest` position is `verte` field
  `x` to Sony's `vcl` and was a whole quadword here, with no diagnostic on either
  side. The modifier is now asked first and the strip is then unconditional, so an
  argument that cannot take a field keeps its name whatever it is spelled and one
  that can is stripped whatever it is spelled. A name that is nothing but field
  letters is not a register reference and is left alone. `.syntax new` never
  enters this code: the 70 real microprograms of a shipped engine and 1360
  generated programs are byte-identical, and so are openvcl's own ps2gl fixtures.
  `test/regress/src/old_alias_int.vcl`, `old_alias_field.vcl` and the control
  `old_alias_int_ok.vcl`.
- A loop-carried live range is no longer abandoned for the whole loop when a count
  goes over. `extendLoopDirectiveRange` refused to extend anything when the
  aliases OVERLAPPING a loop outnumbered the register file - and the set it
  counted is not the set it extends. What gets extended is the read-first names
  the body carries; what got counted also included every short-lived temporary in
  the body, which is never extended and costs nothing. So the guard fired because
  of the temporaries and then dropped the extension for the carried name, whose
  register went to a temporary and whose value was gone by the second iteration.
  Extending nothing is never the safe answer: when the extension genuinely does
  not fit, refusing to allocate is, which is what already happened when the guard
  was not taken. The extended set is now the read-first one unconditionally - the
  narrowing `--loop-liveness-always` used to do alone - and the guard is gone.
  Measured on a `--LoopCS` loop with thirty-four float temporaries and one carried
  name: divergent from its own source under `pb-dag` and `pd-cond` with the guard,
  clean without it, and it still allocates. The same loop with two temporaries
  never reached the guard and was correct all along.
  Reachable only in the default configuration: without a `--LoopCS` directive on
  the branch target a back edge is not treated as a loop at all, and
  `--loop-liveness-always` skips the guard. An instrumented build counted the
  guard firing zero times across 1430 real and generated programs without the flag
  and 387 times on the 70 with it, where it is disabled.
  `test/regress/src/loop_pressure_carry.vcl` and its control.
- `test/regress/cases.tsv` accepts `no:--the-flag` in the `arg` column, which
  compiles that one case with the standard flag list minus that flag. A defect
  that lives in a flag's OFF path cannot be asserted by a suite that only ever
  compiles one configuration, and the two cases above are the first of those.
- `--loop-liveness-always` terminates on every control-flow shape. The live-range
  extension runs once per BACK EDGE, and two back edges need not nest: each call
  picks the first alias of a name in ITS OWN range as the one the readers at the
  top hold, so a loop can tie an alias to one that a neighbouring loop has already
  tied back to it. The same-name chain became a ring with no root, and the
  ancestor walk in `processAliases` - the one walk of that chain in the file
  without a hop cap - never left it. A 216-line generated program with nineteen
  interleaved loops ran 66 minutes and was never seen to finish; it now compiles
  in 0.55 s. The closing edge is refused rather than added, which costs nothing:
  an edge that would close a ring runs between two aliases already on one chain,
  so they reach one root and share one register either way. The cycle test is now
  Floyd rather than a fixed depth of 16, because a fixed depth answers "no cycle"
  for any ring that closes further up and the walks it guards then never end.
  A compile that hangs is worse than one that fails: it stops a project build with
  no diagnosis at all. `test/regress` has a new assertion kind, `TIME`, for it.
- A cycle that becomes no instruction word no longer counts toward `Q` or `P`
  readiness. Under `--fmac-interlock` a wait on the FMAC pipeline is kept in the
  cycle model and emitted as nothing, because the hardware stalls by itself - but
  the FDIV and EFU pipelines have no interlock, and their results arrive a fixed
  number of cycles after issue with only instruction words carrying the program
  from one cycle to the next. Spending a suppressed stall twice - once as the FMAC
  wait it was, again as part of a division's latency - made a `mulq` three words
  below an `rsqrt` look fourteen cycles below it. The padding was then chosen as
  `nop` rather than `waitq`, and the read returned the previous quotient. The
  outstanding FDIV/EFU results are now pushed back by every cycle that produced no
  word, in both the scheduler's model and the emitter's.
  This also covers the loop-carried direction, which the known-limits list claimed
  came out right by accident and does not: with enough work between a division
  above a loop and the loop itself, `qReadyCycle` elapses and the consumer at the
  top of the body is issued unprotected against a producer two rows below it -
  `test/regress/src/q_backedge_stale.vcl`. Covering it here is still an accident;
  nothing reasons about a back edge, and that remains open.
- Q and P readiness is now measured against the SHORTEST path into a block, not
  the fall-through one. Blocks are scheduled in file order with one latency
  tracker carried along it, so a `div` above a forward branch and a `mulq` at
  the branch's target looked twelve rows apart in the file and were six cycles
  apart on the wire; `Q` has no hardware interlock, so the `mulq` read the
  previous quotient. Each block now takes the largest skew over its incoming
  edges - `s + L - X - 2` for a branch out of a block with skew `s` issuing at
  `X` into a label whose fall-through entry is `L` - and the outstanding FDIV
  and EFU results are pushed back by it before the block is scheduled. Backward
  edges cannot win that maximum, so one pass in file order is exact.
  Only Q and P move: the FMAC pipeline interlocks on VF registers and
  `--branch-interlock` covers the integer results a branch reads.
  Known gap: a producer BELOW its consumer, feeding it through a back edge on
  the next iteration, is a different question and is not answered here.

0.4.0
- Renamed root documentation files to Markdown and consolidated the active
  roadmap into `README.md`, removing the standalone TODO file.
- Added VSM cost analysis modes:
  - `--cost` for human-readable reports;
  - `--cost-json` for machine-readable reports;
  - `--cost-loop <label>=<count>` for loop-weighted block analysis;
  - `--cost-loop-preset ps2gl` for known ps2gl hot-loop weights.
- Cost reports now include static cycles, estimated cycles, FDIV/EFU producer
  issue stalls, explicit `waitq`/`waitp` stalls, slot usage, paired cycles,
  NOP slots, per-label block costs, and weighted hot-block rankings.
- Generic scheduler issue-slot dumps now classify latency padding as `nop`,
  `waitq`, or `waitp`, so tooling can distinguish idle cycles from explicit
  long-latency waits.
- Scheduler issue-slot dumps now include modeled `issue_cycle` and
  `cycle_count`, including multi-cycle `waitq`/`waitp` padding spans.
- Loop-pipeline analysis now reports the complete Q consumer list for multi-Q
  loops instead of exposing only the final Q stage through top-level fields.
- Code emission and scheduler analysis now share the same helper for choosing
  NOP vs `waitq`/`waitp` read-hazard padding.
- Schedule issue-slot dumps now use the same MAC/CLIP flag-liveness
  segmentation as generic code emission.
- Code emission now reuses the shared scheduler-analysis helper for deciding
  when remaining MAC/CLIP WAW dependencies are dead.
- Schedule issue-slot dumps now expose the implicit WAW resources ignored by
  each ready-scheduler segment.
- Generic software-pipeline rewrites can now place the next-iteration Q
  producer in the loop branch delay slot when safe, even when the plan has
  cloned prefetch work, scratch-register rotation, or ordinary suffix work
  that can remain before the branch.
- Loop-pipeline text and JSON dumps expose the chosen next-iteration Q
  producer insertion point and whether it occupies the branch delay slot.
- Loop-pipeline rewrite-plan dumps now report suffix dependency blockers when
  a cloned next-iteration Q producer cannot safely move into the branch delay
  slot.
- Generic software-pipeline rewrites can now delay drainable suffix stores:
  the prolog computes and captures the first iteration, the main loop stores
  previous-iteration values before current Q consumers, and the drain stores
  the final captured values on exit.
- Generic multi-Q software-pipeline rewrites can now rotate through the latest
  safe Q stage that still leaves a real loop suffix, instead of being limited
  to the first Q stage.
- Multi-Q cyclic-prefix planning can also split before the first Q consumer
  when the producer-side gap is already latency-safe, avoiding unnecessary
  duplication of consumed Q work in that subset.
- Multi-Q cyclic-prefix planning now evaluates safe split candidates with the
  shared scheduler model and emits the lowest-cycle main-loop shape it finds.
- Multi-Q cyclic-prefix rewrites can now insert the cloned prefix before
  independent loop-tail work, giving the modulo-scheduled Q producer/consumer
  more useful cycles before the branch.
- Generic software-pipeline rewrites now handle labels attached directly to
  `--LoopCS` directives, so ps2gl-style `label: --LoopCS ...` loops actually
  emit the planned prolog/main/drain structure instead of falling back to the
  original loop body.
- Multi-Q cyclic-prefix planning can now use a producer-only first-stage
  prefix when the loop-carried insertion gap is sufficient, priming the first
  Q producer in the prolog without duplicating its consumer.
- Ready-scheduler analysis now has a typed `VuScheduledProgram` wrapper with
  block-level cycle ranges, and schedule dumps expose program-relative issue
  cycles.
- Ready-set token scheduling now flattens that typed `VuScheduledProgram`
  wrapper, keeping codegen token order and schedule dumps on the same
  scheduler plan.
- Scheduled issue slots now carry first/second/upper/lower token indices in
  the scheduler model itself, so text and JSON dumps no longer reconstruct
  those indices from raw token pointers.
- The non-flag ready-set scheduling API now also builds and flattens a typed
  `VuScheduledProgram`, removing another parallel token-only scheduler path.
- Added `--strict-schedule-slots` as an opt-in compiler mode that emits from
  scheduler-selected pairs without the older textual latency/pairing
  lookahead fallbacks.
- Ready-scheduler issue slots now pair safe upper-pipe barrier tails with
  following direct branches that do not own explicit delay-slot fillers, or
  with `xgkick`, moving another legacy codegen pairing case into the generic
  schedule model.
- Strict schedule-slot emission now preserves explicit branch-delay fillers
  after a scheduled upper+branch pair, so the scheduler can model paired loop
  branches without dropping the delay-slot instruction.
- Typed schedule-program dumps now carry Q/P/register latency state across
  label and basic-block boundaries, while legacy token flattening remains
  block-local until direct padding-aware emission is ready.
- `openvcl -o -` now writes compiled VSM output to stdout instead of creating
  a literal file named `-`.
- Added regression fixtures and unit/integration tests for cost analysis.
- Added conservative VU scheduling improvements used by ps2gl:
  - upper/lower pairing lookahead;
  - latency-gap filling;
  - deferred Q/P waits;
  - Q-consuming FMAC pairing with deferred `waitq`;
  - safe plain-store and selected memory movement;
  - branch-padding reuse;
  - conservative branch-delay filling for independent integer instructions;
  - standalone branches omit the old extra pre-branch bubble after normal
    read-hazard padding is satisfied;
  - pre-increment plain stores can fill branch delay slots by adjusting their
    memory offset against the incremented base register;
  - independent plain stores can fill loop branch delay slots after the
    loop-counter increment when they do not read the updated counter;
  - `lq`/`lqi`/`lqd` results can feed `ftoi*` conversions through a narrow
    bypass matching SCE ps2gl ADC setup output;
  - dead VI-only fallthrough integer instructions can fill forward conditional
    branch delay slots when the taken path overwrites the same VI value before
    reading it;
  - deterministic alias allocation;
  - per-field VF readiness tracking;
  - disjoint VF field read/write pairing;
  - terminal-branch auto-exit suppression.
- Fixed several scheduler correctness bugs found through ps2gl/PCSX2 testing:
  - unsafe `loi` pairing with FMAC instructions that read `I`;
  - implicit broadcast reads, such as `mulw.xyz ..., VFw`, now depend on the
    broadcast component instead of the destination `.xyz` mask;
  - Q/P producer and consumer accounting now treats `mfp` as a P consumer, not
    a new P producer.
  - `madd*` and `msub*` instruction metadata now models ACC reads, preventing
    scheduler movement across multiply-add/subtract accumulator chains.
- Added focused regression tests for the new scheduling hazards.
- Established the next architecture direction: one canonical VU instruction
  metadata table shared by the scheduler and cost analyzer.
- Introduced `VuInstructionInfo` as the first shared VU instruction metadata
  table and moved cost-analyzer opcode classification, latency, throughput,
  and Q/P producer checks onto it.
- Expanded `VuInstructionInfo` into the canonical parser/cost metadata table,
  moved hardware operand construction onto it, and added
  `--dump-instruction-info` / `--dump-instruction-info-json`.
- Added `VuTokenResourceAccess` as the table-driven descriptor layer for VF/VI
  register fields, implicit resources, memory flags, branch delay slots, and
  bypass notes.
- Moved scheduler register-key, field-mask, Q/P, and implicit-resource checks
  onto `VuTokenResourceAccess`.
- Moved CodeGenerator pairing and movement register-conflict checks onto
  `VuTokenResourceAccess` read/write descriptors.
- Moved scheduler memory/control classification for loads, stores, `xgkick`,
  pre/post-increment, and branch barriers onto `VuTokenResourceAccess`.
- Added memory base-register and constant-offset descriptors and moved the
  scheduler's plain memory alias checks onto them.
- Added branch behavior flags for unconditional, link, and register branches,
  and moved branch-delay emission/pairing checks onto descriptor metadata.
- Added `VuSchedulingRules` as the shared stateless rule layer for emittable
  token checks, token movement, pair resource conflicts, Q/P and MAC/CLIP flag
  predicates, branch-delay queries, and adjacent integer-add coalescing.
- Moved `VuSchedulerAnalysis` ready-candidate, barrier, memory-ordering, pipe,
  and latency-priority checks onto `VuSchedulingRules`.
- Let the ready-set scheduler include dependency-safe plain stores so
  long-latency Q/P producers can move ahead of them when descriptors prove the
  movement safe.
- Let the ready-set dependency graph distinguish plain memory accesses by base
  register and constant offset, allowing distinct loads/stores to reorder.
- Added dependency-chain priority to the ready-set scheduler so critical
  producer chains are chosen ahead of unrelated short work.
- Weighted ready-set dependency-chain priority by instruction latency, improving
  the ps2gl pure-OpenVCL aggregate by 2 static and 2 estimated cycles.
- Added deferred-wait pairing for independent upper-pipe work above lower-pipe
  Q/P consumers, allowing a movable upper instruction to share the `waitq` or
  `waitp` row.
- Ignored MAC flag WAW edges in ready-set scheduling when the full shader never
  reads MAC flags, improving the ps2gl pure-OpenVCL aggregate by another 84
  static and 83 estimated cycles.
- Added the same dead-reader detection for CLIP flags, keeping CLIP WAW
  ordering only when a shader can read CLIP state.
- Reused the same dead MAC/CLIP WAW mask in CodeGenerator latency-gap filling
  and pairing lookahead, improving the ps2gl pure-OpenVCL aggregate by another
  28 static and 32 estimated cycles.
- Let ready-set scheduling ignore MAC/CLIP WAW edges after the final matching
  flag reader in the token stream, improving the ps2gl pure-OpenVCL aggregate
  by another 480 static and 510 estimated cycles while preserving conservative
  ordering before readers.
- Applied the same remaining-reader MAC/CLIP WAW mask to CodeGenerator
  latency-gap filling and pairing lookahead, improving the ps2gl pure-OpenVCL
  aggregate by another 54 static and 12 estimated cycles.
- Allowed `move.xyz <dst>, vf00` to use the upper-pipe zeroing form after the
  final MAC reader, matching the same MAC-liveness rule used by scheduling.
- Added behavior-preserving scheduler analysis scaffolding for basic-block
  construction and descriptor-derived dependency edges.
- Wired the scheduler analysis layer into code generation in preserve-order
  mode, establishing the production hook for ready-set scheduling.
- Added `--cost-compare` and `--cost-compare-json` for side-by-side VSM cost
  comparisons between a baseline and candidate, including signed deltas.
- Added `--cost-compare-markdown` for direct side-by-side Markdown cost tables
  suitable for renderer comparison reports.
- Added `--cost-compare-list-markdown` to read baseline/candidate VSM pairs
  from a manifest and emit a single multi-row Markdown comparison table.
- Added `--cost-compare-list-check <metric>` to fail a VSM pair manifest when
  any individual candidate exceeds its matching baseline for a selected metric.
- Markdown cost comparisons include loop-weighted static and estimated totals.
- `--cost-loop-preset ps2gl` now recognizes SCE optimized
  `EXPL_...__MAIN_LOOP` labels when weighting and comparing reference VSMs.
- The ps2gl loop preset now also maps SCE fast-family
  `adcLoop_done_lid__MAIN_LOOP` labels onto `xform_loop_lid`, so hot-loop
  comparisons weight those reference shaders correctly.
- Extended cost comparison reports with top weighted block deltas for estimated
  cycles, idle slots, and wait stalls.
- Added the first ready-set scheduler pass for straight-line arithmetic runs,
  using descriptor-derived dependencies while keeping labels, memory, waits,
  branches, and explicit barriers fixed.
- Extended the ready-set scheduler to pull plain loads earlier in straight-line
  blocks so their latency can overlap independent arithmetic.
- Added conservative adjacent upper/direct-branch pairing, preserving the
  branch delay slot and avoiding branch hoists across control flow.
- Moved the CLI version string into a shared source constant and added
  `--version` regression coverage.
- Kept alias live ranges sorted as they are built, making range intersection
  checks cheaper while preserving adjacent-range merging.
- Removed stale token-parser TODO comments now covered by instruction and
  memory descriptor metadata.
- Extended VF lifetimes inside `--LoopCS`-marked loops when register pressure
  allows, so loop temporaries that need scheduler overlap are not prematurely
  coalesced onto one physical register.
- Enabled the safe generic software-pipeline rewrite pass by default while
  keeping `--disable-generic-software-pipelining` for comparison and debugging.
- Delayed dependency-chain priority for latency-blocked ready-set candidates,
  so independent work fills Q/P latency before consumers are selected.
- Extended loop-pipeline diagnostics so every Q stage reports its loop-carried
  gap, next-producer insertion gap, deficit, and scheduling strategy.
- Added explicit padding slots to generic schedule dumps so latency stalls are
  visible in the scheduler plan before code emission.

## 0.3.3

	- Unified error-reporting into a separate class, and changed error
    display to a more standard appearance.
  - Added support for using 'cpp' as preprocessor in addition to gasp.
    If both are used, cpp will run before gasp, to stay compatible.
  - Added commandline-argument to specify alternative for cpp.
  - Added input-parsers for gasp and cpp that reflects the original
    filename and line in errorcodes to assist debugging.
  - Wrote a RPN expression-evaluator to assist pseudo-instruction LOI.
  - Added initial CLIP operands, but without proper validation.
  - Fixed MR32 so that destination-fields are rotated properly.
  - Fixed templates for RGET and RNEXT so that they generated proper
    destination fields.
  - Added a threshold for the number of times a dynamic branch can
    execute in the register allocator. This value has been initially set
    to 16, but can be changed by the commandline-parameter '--bthres'.
    This allows programs with a lot of dynamic branches that intersect
    to resolve.
  - Fixed multi-argument operands so that they can leave a trailing
    comma at the end without issuing any error.
  - If the final code-block is not terminated by a --exit/--endexit pair,
    the code-generator will now fail as it should.
  - Added commandline-argument '--version' to show current version.
  - Fixed a inconsistency in the argument-extraction.
  - Added a library of math-routines for use with LOI.

## 0.3.2

  - It's now possible to branch between code-blocks with branch-tracking
    intact. This will allow sharing subroutines between blocks.
  - Added initial support for output parameters from code-blocks.
    Currently it will only apply output parameters if the initiating
    branch for the block reaches the exit-point.
  - Ending a code-block now properly generates a termination with nop[E].
  - All currently unsupported preprocessor directives that are VCL
    specific have been filtered from code-output. (They were previously
    passed along unprocessed)
  - Fixed issue in operand-templates where an instruction that wanted a
    indirect read with an immediate offset would generate invalid code.
  - Fixed issue with destination-fields where specifying a field on the
    second (or third) argument (e.g. 'ilw VI01,0(VI00)x') argument with
    no field specified in the operand would generate an error.
  - Fixed issue involving indirect register accesses with immediates
    where GAS would complain when the immediate was omitted.
  - Added minimal extraction of memory-groups.

## 0.3.1

  - Fixed issue with branching to a subroutine from separate locations,
    which caused the branch to be aborted and ignoring any code that
    followed the BAL.

## 0.3

  - Refactored register-allocator, the new version now supports
    independent ranges for register-aliases and a more flexible
    handling of branches.
  - Reverted BAL to static branching as the register-allocator now
    can handle this case.
  - Added return-address tracking for integer-registers when using
    branches (to allow tracking branches into subroutines).
  - JR and JALR no longer aborts the current branch, but attempts
    to read the return-address and jump to it. If it isn't a valid
    address, the branch is aborted.
  - Added proper register inputs for both float & integer. You can
    now connect more than one alias to a register.
  - Added support for using I,P,Q,R and ACC as register inputs.
  - Added support for .name directive.
  - Added removal of dead code.
  - Added support for --cont tag.

## 0.2.1

  - Unified a lot of operand templates to reduce chance of error.
  - Added simplifications for SUBA, MULA, MADDA and MSUBA.
  - Fixed register allocation issue, now encloses registers that
    are reused due to branching (loops).
  - Fix for JR and JALR, they now abort the current branch when
    reached, to avoid issues with returning from subroutines.
  - Flagged BAL as dynamic to support early aborts from JR and JALR.
  - BAL and JALR lacked the write-modifier on the register that
    contains the return address, fixed.

## 0.2

  - Implemented first iteration of the dynamic branch-tracking
    register allocator.
  - Implemented .init_v?_* operands, except the range-version.
	- Added input parameter handling for entry points.
	- Added value verification for all float & integer-aliases.
	- Added value verification for direct register writes.
  - Added proper write-dependency from pseudo-instruction LOI.
  - Rewrote input-parser to remove dependencies on strtok() and
    static char buffers.
  - Fixed issues with the templates for a lot of operands.
  - Improved argument parsing in the tokenizer.
  - Corrected so that immediates do not affect the destination field.
  - Register numbers are now allowed to be variable in size.
  - Added support for C++-style comments.

## 0.1.1

  - Fixed issues with OPMULA and OPMSUB, dvp-as required that the
    generated arguments contained valid(xyz) fields.
  - Had named the '.rem_v*'-declarations incorrectly, fixed
  - Fixed win32 code to generate proper temporary filenames
  - Changed error-output to use std::cerr instead of cout, to allow
    proper use of pipes.
  - Added sourcecode-comments output support (-c).
  - Renamed 'isblank()' to 'isBlank()' to avoid issues with defines
    and gcc.
  - Made the register extraction code stricter against syntax issues
		(e.g. 'iaddiu temp1, vi00dontcare, 1' compiled without errors)

## 0.1

  - Initial release
