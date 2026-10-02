#ifndef __OPENVCL_VUSCHEDULINGRULES_H__
#define __OPENVCL_VUSCHEDULINGRULES_H__

/*
 * VuSchedulingRules.h
 *
 * Shared scheduling predicates for VU tokens.  These helpers keep the
 * stateless resource, memory, branch, and pairing rules out of the code
 * emitter so future scheduling passes can reuse the same contract.
 */

#include "Token.h"

#include <list>
#include <string>

namespace vcl
{

bool isVuEmittableInstruction( const Token& token );

std::string lowerVuTokenName( const Token& token );
bool isVuMtir( const Token& token );
bool isVuFtoiConversion( const std::string& name );
bool isVuLoadToFtoiBypassProducer( const std::string& name );
bool isVuMinii( const std::string& name );
bool isVuLoadToMiniiBypassProducer( const std::string& name );

bool isVuMacReader( const std::string& name );
// FSAND/FSEQ/FSOR. Split out of isVuMacReader(), which used to list them while the
// instruction table declared them as reading nothing - two answers to one question.
bool isVuStatusReader( const std::string& name );
bool isVuMacOrStatusReader( const std::string& name );
bool isVuClipReader( const std::string& name );
bool isVuClipw( const std::string& name );

bool vuTokenHasInstructionFlag( const Token& token, unsigned int flag );
unsigned int vuTokenBranchDelaySlots( const Token& token );
bool isVuTerminalUnconditionalBranch( const Token& token );

bool vuTokenReadsQ( const Token& token );
bool vuTokenWritesQ( const Token& token );
bool vuTokenReadsP( const Token& token );
bool vuTokenWritesP( const Token& token );
bool vuTokenReadsRegister( const Token& token, const std::string& key );

void collectVuRegisterReadKeys( const Token& token, std::list<std::string>& reads );
void collectVuRegisterWriteKeys( const Token& token, std::list<std::string>& writes );

bool isVuZeroMoveFromVf00( const Token& token );
bool isVuMoveAsUpperMaxCandidate( const Token& token );
bool vuTokenListReadsMac( const std::list<Token>& tokens );
bool vuTokenListReadsStatus( const std::list<Token>& tokens );
bool vuTokenListReadsClip( const std::list<Token>& tokens );

// The hardware resource an `out_hw_*`/`in_hw_*` directive names: the program's
// contract with whatever runs next, so a resource one of them mentions is live
// even when no instruction in the program touches it.
//
// This lives here, and not in the dead-write pass where it was written, because
// the dead-write pass was the only thing honouring it. The SCHEDULER decided the
// same question from in-program readers alone, so a program declaring
// `out_hw_clip` had its `clipw` correctly kept and then permuted - and the CLIP
// window is positional, so permuting them changes what the successor reads. One
// question, one answer, one place.
unsigned int vuDeclaredHardwareResource( const Token& token );

bool isVuPlainMemoryStore( const Token& token );
bool isVuPlainMemoryLoad( const Token& token );
bool isVuXgkick( const Token& token );
bool isVuMemoryOrderingAccess( const Token& token );
bool isVuBoundaryOperand( const Token& token );
bool isVuSchedulingBarrier( const Token& token );
bool isVuReadyScheduleCandidate( const Token& token );

// --schedule-flag-readers: let instructions that implicitly read the MAC/CLIP
// flags take part in list scheduling instead of ending the scheduling segment.
void setVuScheduleFlagReadersEnabled( bool enabled );
bool vuScheduleFlagReadersEnabled();

// --fmac-interlock: spend a VF-to-VF wait as a hardware stall rather than as
// emitted nop words. Same cycles, smaller program.
void setVuFmacInterlockEnabled( bool enabled );
bool vuFmacInterlockEnabled();

// --pair-best-of-cycles: give --pair-best-of-many a second copy of every ready
// strategy whose PARTNER FILTER asks the cycle question instead of the word one,
// and let a segment take it when it is strictly faster and no larger.
//
// The filter it fixes: under --fmac-interlock the filter asks
// manualReadHazardDelay, which reports 0 for every VF operand still in flight
// because the hardware stalls by itself. So a lower-pipe token four rows behind
// its producer looks free, is paired onto a row that was ready NOW, and holds
// the primary with it. That is why openvcl puts `sq X` on the row after
// `ftoi4 X, X` and SCE drains its stores four rows later for nothing.
//
// Selection is bounded on purpose: the word winner is still chosen over the
// SHIPPED table alone, and the cycle winner only takes the segment when its
// emitted size is <= the word winner's. The flag therefore cannot grow a
// program - VU1 micro memory is a hard ceiling and a frame rate is not.
void setVuPairBestOfCyclesEnabled( bool enabled );
bool vuPairBestOfCyclesEnabled();

// How many cycles after its producer the MAC/CLIP flags may be read. Default 4.
// --sce-latencies sets 1, which is what SCE's vcl emits: over the 25
// microprograms of a real engine its output contains five clipw -> fcand pairs
// exactly one row apart, e.g.
//     clipw.xyz VF15xyz,VF15 | ior   VI09,VI09,VI08
//     addw.x    VF05,VF05,.. | fcand VI01,63
// and 0x3FFFF/0x3F masks mean that fcand is testing the clipw right above it.
void setVuFlagVisibilityLatency( unsigned int cycles );
unsigned int vuFlagVisibilityLatency();
void setVuClipFlagVisibilityLatency( unsigned int cycles );
unsigned int vuClipFlagVisibilityLatency();
void setVuClipFlagSchedulingLatency( unsigned int cycles );
unsigned int vuClipFlagSchedulingLatency();

// Does this CLIP-flag reader test the window as a whole? A reader carries the mask
// it tests as an immediate; the full-window masks - all 24 bits, or the 18 that
// three vertices occupy - ask "is anything outside" and every window position
// answers the same, so such a read does not depend on its own six bits having
// arrived. Anything narrower names particular entries and does.
//
// This lives here rather than in the emitter because BOTH the emitter
// (CodeGenerator::padForClipFlagWindow) and the scheduler (VuLatencyTracker) have
// to agree on it, and they did not: the emitter exempted full-window reads while
// the scheduler kept them four cycles from their CLIP, so the scheduler's caution
// was the binding one. Two copies of this test is how that happens again.
bool vuClipReadIsFullWindow( const Token& token );

// The status register's equivalent question, and the one that decides how much an
// accumulating resource costs. A reader whose mask names only the sticky bits
// (ZS/SS/US/OS/IS/DS) needs every contributor since the last FSSET to be behind it
// and nothing more; a reader that names a non-sticky bit (Z/S/U/O/I/D) is asking
// about the last contributor specifically, and that one has to stay last. See
// addAccumulatingImplicitFlagDependencies() in VuSchedulerAnalysis.cpp.
bool vuStatusReadNeedsLastWriter( const Token& token );

// --exempt-full-clip-masks: let the SCHEDULER make the same exemption. Off by
// default; with it on, a full-window reader no longer waits
// vuClipFlagSchedulingLatency() cycles behind its CLIP. Positional reads are
// untouched - those are the ones a Sutherland-Hodgman edge loop makes, and reading
// one early returns a different vertex's answer under the same mask.
void setVuExemptFullClipMasksEnabled( bool enabled );
bool vuExemptFullClipMasksEnabled();

// --clip-exemption-best-of: do not decide the question above once for the whole
// build. Schedule each PROGRAM twice - exemption off, exemption on - and keep
// whichever emits fewer instruction words, ties going to "off" so a program that
// gains nothing keeps the output it had.
//
// Whole program, not per segment, on purpose. Freeing a reader shortens the segment
// it sits in or leaves it the same length, never lengthens it, so a per-segment
// best-of would look like a guaranteed win; what it would miss is that the schedule
// downstream of the freed reader is different too, and on stapip_cull_d and
// stapip_cull_td that downstream costs a row. Only a whole-program count sees it.
void setVuClipExemptionBestOfEnabled( bool enabled );
bool vuClipExemptionBestOfEnabled();

// Is there anything in this program for the exemption to act on? Without a
// full-window CLIP reader the two arms of the best-of are the same schedule, and the
// second one is pure compile time.
bool vuTokenListHasFullWindowClipReader( const std::list<Token>& tokens );

// Cycles after which a memory load's destination may be read, or 0 to keep the
// instruction table's latency+1. --sce-latencies sets 3 (see VuLatencyTracker).
void setVuIntegerLoadReadyCycles( unsigned int cycles );
unsigned int vuIntegerLoadReadyCycles();

// --emit-delay-fillers: offer the instruction scheduled just before a branch as
// its delay-slot filler, instead of leaving the slot as a nop word.
void setVuEmitDelayFillersEnabled( bool enabled );
bool vuEmitDelayFillersEnabled();

// --branch-interlock: a branch reading a register produced by a LOAD or by a
// flag reader waits in hardware, so that wait must not be paid in instruction
// words. SCE's vcl emits exactly that, and annotates the latency it is leaving
// to the hardware:
//     ilw.x  VI01,8(VI00)
//     iblez  VI01,multiColor      ;  STALL_LATENCY ?3
// Five of the engine's programs do it, and five more put `fcand VI01,8`
// immediately before `ibne ...,VI01,...`. An ordinary integer op is NOT in this
// class - SCE never comes closer than two instructions there (24 cases at
// exactly two), which is what openvcl already emits.
// --branch-bubble-on-dependency: emit the pre-branch bubble only when the row
// above the branch actually produces one of its operands. The emitter's own rule
// is unconditional - one nop in front of every conditional branch - which is
// where openvcl's remaining stall rows are: 80 sites over the resident program
// set against SCE's 36, for the same hazard and the same one-word cost where it
// is real.
void setVuBranchBubbleOnDependencyEnabled( bool enabled );
bool vuBranchBubbleOnDependencyEnabled();

// --loop-liveness-always: never skip extending a live range across a loop's back
// edge. The allocator's guard trades that correctness requirement for a compile
// that succeeds, and the result is a value handed to two names at once.
void setVuLoopLivenessAlwaysEnabled( bool enabled );
void setVuUpperMoveWithWEnabled( bool enabled );
bool vuUpperMoveWithWEnabled();

// --coalesce-float-writes: give a float write the register its own previous
// value already sits in, when that previous value is dead from the write on.
// openvcl spawns a fresh Alias per write, so a two-address self-update chain
// (`mul.x a,a,b` - 3673 of them across the generated programs, against 57 plain
// `move`s) burns a second register for a value that never needed one. The
// integer side has done this since the isubiu loop-counter fix; this is the
// float half. Off by default: it changes which register a program lands in, and
// upstream's fixtures pin those names.
void setVuCoalesceFloatWritesEnabled( bool enabled );
bool vuCoalesceFloatWritesEnabled();

// --trim-uncarried-ranges: a value defined and consumed inside one iteration of
// one loop does not have to hold its register for the whole loop. openvcl's
// branch-state analysis merges the aliases of a name across the back edge and
// stretches the survivor from the loop's entry point to its last line, so a
// per-vertex temporary looks exactly like a loop-carried accumulator. Trim such
// a range back to [first access, last access], but ONLY when every component
// read is written earlier in the same iteration, every access sits inside the
// same loop, and no branch lies between the two ends - i.e. only when the value
// provably dies at its last use. Off by default: it removes liveness, so it is
// the one change here that could hide a real carry if the test were wrong.
// --split-dead-float-ranges: give each INDEPENDENT value of a float name its own
// register, so the scheduler can interleave chains that only ever shared a name.
//
// openvcl keeps one Alias per source name as soon as the name's writes use mixed
// field masks, because BranchState::writeFloat decides whether to reuse the
// existing alias with
//     depend = ((state.fields() & ~argument.fields()) != 0)
// and state.fields() is every field EVER written to the name, not the fields
// still live.  `vuS1`, written .z and then .x, is therefore ONE alias for the
// whole program - and the three per-vertex chains in the generated scripts,
// which reuse the same temporary names, are welded onto one register before the
// scheduler ever runs.  The scheduler keys its hazards on the ALLOCATED register
// (VuTokenResourceAccess::vuRegisterKey), so it sees one serial dependence graph
// where the source has three independent ones, and stalls at every step.  SCE's
// vcl splits the same names - in vu_script3_c it puts the three copies of vuS1
// in VF22, VF27 and VF26 and interleaves them three rows apart, 22 stall cycles
// against openvcl's 347 on the same 258 words.
//
// This pass renames, before anything else looks at the token list, so the rest
// of the compiler is untouched and just sees more names.  A rename is only taken
// where it is provably safe: every access to the name must sit inside ONE
// straight-line region (no label, no branch, no directive inside), which makes a
// linear scan exact and means the name can be neither live-in nor live-out; the
// region's first access must be a write, so nothing is carried in across a back
// edge; and the split point must be a write whose field mask covers every field
// still to be read AND which does not itself read the name, so it is a full kill
// of the old value rather than a two-address self-update.
//
// Splitting costs registers, which is what the six allocator flags fight, so it
// is paired with a register CHOICE change under the same flag: among the free
// registers the allocator would accept, take the one whose nearest already-
// placed neighbour is furthest away instead of the lowest-numbered one.  Without
// that the split webs, whose ranges are disjoint, land back on one register by
// first fit and nothing has changed.  Off by default: it changes which register
// every value lands in.
void setVuSplitDeadFloatRangesEnabled( bool enabled );
bool vuSplitDeadFloatRangesEnabled();

// The register-CHOICE half of --split-dead-float-ranges, on its own switch so the
// allocation ladder can drop it without dropping the rename.
//
// Measured, and it overturns the reason the fallback was written: on all three
// programs that fall back, the peak number of simultaneously live float aliases
// is IDENTICAL with the split and without it - 31, 22 and 23 against 31
// available.  It has to be: splitting a name partitions its live range, so it
// can never raise the number of values live at a line, only the number of names.
// Two of the three fail nine registers below the ceiling.  What runs out is not
// the file, it is this placement rule: `preferSpreadRegister` takes an untouched
// register outright when it finds one, so the first thirty-one values each claim
// a fresh register and a long-lived constant allocated later - `k0` in both
// vu_script2_*_cl - finds every one of them occupied somewhere inside its range.
//
// So the ladder backs the spread off in stages instead of giving up on the
// rename.  See Parser::allocateRegisters.
void setVuSpreadFloatRegistersEnabled( bool enabled );
bool vuSpreadFloatRegistersEnabled();

// --spread-webs-only, and the ladder's second rung: narrow the spread to the
// aliases the split actually created - the ones whose name carries the
// `~generation` suffix - and place everything else by first fit.
//
// The spread exists to put two webs of ONE name on two registers.  Nothing about
// that needs the program's constants scattered as well, and scattering them is
// what leaves a long-lived value with nothing to allocate: in vu0_rt_kernel the
// alias that fails is `bT`, the ray's best hit distance, live over 296 rows and
// never split.  Narrowed, that program allocates on its first attempt and its
// modelled cost goes 1278 -> 1066 cycles, 747 -> 540 FMAC stall cycles, on four
// words LESS.  Applied to all seventy it is worth less than the ladder is (-190
// modelled cycles against -211), so it is a rung and not the default.
void setVuSpreadFloatRegistersWebsOnly( bool enabled );
bool vuSpreadFloatRegistersWebsOnly();

void setVuTrimUncarriedRangesEnabled( bool enabled );
bool vuTrimUncarriedRangesEnabled();

// --sink-loads: move a load down to just before the value it loads is first
// read, which is what SCE's vcl achieves by scheduling and openvcl never could,
// because the allocator only ever saw source order. The generated programs load
// vertex1/2/3 and normal1/2/3 at the top of the loop body and then transform
// them one at a time, so six registers are pinned where two are in use; Sony's
// output for the same source holds one vertex at a time and reloads into the
// register the previous one just freed. The load moves for real - the token is
// spliced in the list and the allocator's timeline re-derived from list
// position - because a range computed from the first read while the load is
// still emitted early is unsound: the register is unreserved between the two,
// and something else can take it. Off by default: it reorders the emitted
// program, so register names and instruction placement change everywhere.
void setVuSinkLoadsEnabled( bool enabled );
bool vuSinkLoadsEnabled();

// --sink-loads-across-stores: let a sinking load pass a store that writes
// through a DIFFERENT base register. Nothing in the source proves the two
// quadwords are distinct, so this is an aliasing assumption and it gets its own
// flag - but it is the assumption SCE's vcl makes. Its own output for
// vu_script3_d puts `lq.xyz VF25,2(VI05)` at row 199, after three stores through
// VI07 at rows 162, 181 and 198, from a source line that sits above all three.
// Without it the generated loop bodies stop dead at the isw that writes the
// previous vertex's ADC bit and every later load piles up behind it. Only has an
// effect together with --sink-loads.
void setVuSinkLoadsAcrossStoresEnabled( bool enabled );
bool vuSinkLoadsAcrossStoresEnabled();

// --sink-loads-into-loops: let a sinking load pass ONE label, so a value loaded
// in the preamble and read inside the batch loop is loaded inside that loop
// instead. The four GIF-tag quadwords are the reason: gifSetTag, lodGifTag,
// testsTag and alphaGifTag are read only by the seven `sq`s that build the
// packet header, but because the load sits above `begin:` their ranges span the
// whole program, and they are 4 of the 32 values live at the vertex loop's
// pressure peak. This is rematerialization, and it is paid for per loop
// iteration, so the motion is only taken when it reaches the value's first
// reader - stopping halfway would pay the instruction and free nothing - and
// only one LOOP HEADER may be crossed, which keeps a preamble load out of the
// inner per-vertex loop (a label nothing branches back to costs nothing and does
// not count). Four conditions make it sound over the back edge: the address
// register is written nowhere in the program, no store can reach the quadword
// (same base and same constant offset - a different offset on the same base is a
// different quadword and is allowed), the load is the only thing that ever
// writes the value, and the reader it lands in front of carries no label of its
// own for a branch to jump straight past it to. Only has an effect together
// with --sink-loads.
void setVuSinkLoadsIntoLoopsEnabled( bool enabled );
bool vuSinkLoadsIntoLoopsEnabled();

// --sink-loads-past-branches: let a sinking load cross a branch, landing at a
// point every path out of the load's old position reaches. vu_script3_tce_cl is
// the program that needs it: five GIF-tag quadwords are loaded in the preamble
// and read only by the `sq`s that build the packet header, but a two-arm branch
// picking the destination address sits in between, and the pass walls off at any
// branch because the allocator has no dominance information. Landing on
// `setDestAddr:` is correct - both arms rejoin there - and it takes the peak from
// 33 simultaneously live float ranges to 28, against 31 registers.
//
// The condition is post-dominance restricted to a forward span, tested during the
// walk itself rather than from a built CFG, which the allocator would otherwise
// need and does not have. Two counts do it. No path may leave the span past the
// landing point: a branch's target label has to have been walked over before the
// load may land after it, so a jump over the current position always blocks it,
// and a branch back to a label already walked over is refused outright. And no
// path may enter the span past the load: a label is only transparent once every
// branch in the WHOLE program that names it has been walked over, so a join of
// arms that all started at the load is free while a way in from anywhere else is
// a wall. One `jr` - a branch whose target is not a label this can read - and the
// program is given up on, since then any label might be entered from outside.
// Counting branches per label rather than positions is what survives the pass
// splicing tokens as it goes: a load is never a branch, so no move it makes can
// change a count. Only has an effect together with --sink-loads.
void setVuSinkLoadsPastBranchesEnabled( bool enabled );
bool vuSinkLoadsPastBranchesEnabled();
// Delete a token whose register destination is dead - no reader anywhere in the
// program, field by field - and whose implicit writes (MAC/CLIP/I/Q/P/R/ACC)
// nothing observes either. Iterated to a fixed point, so the `loi` feeding a
// deleted reader goes with it. Only aliases are candidates: a literal VFxx an
// author named by number may be an interface with the outside world.
//
// CLIP is a 24-bit shift register of four 6-bit judgements, so one CLIP write
// does not kill the previous one - it survives three more pushes, and a
// positional mask still reads it. That is not optional and is not a flag: both
// this pass and the dependency builder model it unconditionally. See
// implicitWriteIsObservable() in RegisterAllocator.cpp and
// addPreciseImplicitFlagDependencies() in VuSchedulerAnalysis.cpp.
void setVuDropDeadWritesEnabled( bool enabled );
bool vuDropDeadWritesEnabled();
void setVuShowPairMissesEnabled( bool enabled );
bool vuShowPairMissesEnabled();
void setVuPairBestOfTwoEnabled( bool enabled );
bool vuPairBestOfTwoEnabled();
void setVuPairBestOfManyEnabled( bool enabled );
bool vuPairBestOfManyEnabled();
bool vuLoopLivenessAlwaysEnabled();

void setVuBranchInterlockEnabled( bool enabled );
bool vuBranchInterlockEnabled();

bool isVuLowerPipe( const Token& token );
bool isVuLongLatencyProducer( const Token& token );
bool isVuLatencyLoad( const Token& token );

bool vuTokensHaveDataDependency( const Token& a, const Token& b );
bool vuTokenCanMoveBefore( const Token& moved,
                           const Token& crossed,
                           unsigned int ignoredImplicitWawResources = 0 );
bool vuTokenRangeCanBeCrossed( const Token& first, const Token& last );
bool vuTokenPairResourcesAreIndependent( const Token& a,
                                         const Token& b,
                                         bool aWritesMac,
                                         bool bWritesMac );

void coalesceAdjacentVuIntegerAdds( std::list<Token>& tokens );

}

#endif
