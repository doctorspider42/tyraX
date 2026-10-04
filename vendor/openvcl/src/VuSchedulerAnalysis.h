#ifndef __OPENVCL_VUSCHEDULERANALYSIS_H__
#define __OPENVCL_VUSCHEDULERANALYSIS_H__

/*
 * VuSchedulerAnalysis.h
 *
 * Basic-block and dependency scaffolding for the future VU scheduler.
 */

#include "Token.h"
#include "VuInstructionInfo.h"
#include "VuKernelLayout.h"

#include <list>
#include <map>
#include <string>
#include <vector>

namespace vcl
{

class VuLatencyTracker;

enum VuBasicBlockTerminatorKind
{
	VU_BASIC_BLOCK_TERMINATOR_NONE,
	VU_BASIC_BLOCK_TERMINATOR_BRANCH,
	VU_BASIC_BLOCK_TERMINATOR_XGKICK,
	VU_BASIC_BLOCK_TERMINATOR_BOUNDARY,
	VU_BASIC_BLOCK_TERMINATOR_PREORDERED
};

struct VuBasicBlock
{
	VuBasicBlock();

	unsigned int firstTokenIndex;
	std::vector<const Token*> tokens;
	bool terminatedByBarrier;
	VuBasicBlockTerminatorKind terminatorKind;
	const Token* terminator;
};

struct VuLoopInductionUpdate
{
	std::string registerName;
	std::string mnemonic;
	std::string immediate;
	long step;
	bool stepKnown;
	unsigned int tokenIndex;
};

enum VuLoopQSchedulingStrategy
{
	VU_LOOP_Q_SCHEDULE_LOCAL,
	VU_LOOP_Q_SCHEDULE_LOOP_CARRIED,
	VU_LOOP_Q_SCHEDULE_INSUFFICIENT
};

struct VuLoopQStage
{
	VuLoopQStage();

	unsigned int qProducerTokenIndex;
	std::vector<unsigned int> qConsumerTokenIndices;
	unsigned int qProducerLatency;
	unsigned int qProducerConsumerGapCycles;
	unsigned int qProducerConsumerGapDeficitCycles;
	unsigned int loopCarriedQGapCycles;
	unsigned int qProducerInsertionGapCycles;
	unsigned int qProducerInsertionGapDeficitCycles;
	VuLoopQSchedulingStrategy qSchedulingStrategy;
};

enum VuDependencyKind
{
	VU_DEPENDENCY_REGISTER_RAW,
	VU_DEPENDENCY_REGISTER_WAR,
	VU_DEPENDENCY_REGISTER_WAW,
	VU_DEPENDENCY_RESOURCE_RAW,
	VU_DEPENDENCY_RESOURCE_WAR,
	VU_DEPENDENCY_RESOURCE_WAW,
	VU_DEPENDENCY_MEMORY
};

struct VuDependencyEdge
{
	VuDependencyEdge();
	VuDependencyEdge( unsigned int beforeToken, unsigned int afterToken, VuDependencyKind dependencyKind );

	unsigned int before;
	unsigned int after;
	VuDependencyKind kind;
};

enum VuScheduledPaddingKind
{
	VU_SCHEDULED_PADDING_NONE,
	VU_SCHEDULED_PADDING_NOP,
	VU_SCHEDULED_PADDING_WAITQ,
	VU_SCHEDULED_PADDING_WAITP
};

extern const unsigned int VU_SCHEDULED_TOKEN_INDEX_NONE;

struct VuScheduledIssueSlot
{
	VuScheduledIssueSlot();

	const Token* firstToken;
	const Token* secondToken;
	const Token* upperToken;
	const Token* lowerToken;
	unsigned int firstTokenIndex;
	unsigned int secondTokenIndex;
	unsigned int upperTokenIndex;
	unsigned int lowerTokenIndex;
	bool padding;
	VuScheduledPaddingKind paddingKind;
	unsigned int ignoredImplicitWawResources;
	unsigned int issueCycle;
	unsigned int cycleCount;

	// For a padding slot: how many of its cycles have to be spent as emitted
	// instruction words. Cycles waiting on the interlocked FMAC pipeline do not -
	// the hardware stalls on its own - so emitting them only burns micro memory.
	// Equal to cycleCount for every non-padding slot and for waitq/waitp.
	unsigned int emitCycleCount;
};

struct VuScheduledBasicBlock
{
	VuScheduledBasicBlock();

	VuBasicBlock block;
	std::vector<VuScheduledIssueSlot> issueSlots;
	unsigned int firstIssueCycle;
	unsigned int cycleCount;
};

struct VuScheduledProgram
{
	VuScheduledProgram();

	std::vector<VuScheduledBasicBlock> blocks;
	unsigned int cycleCount;
};

struct VuLoopCandidate
{
	VuLoopCandidate();

	std::string label;
	unsigned int labelTokenIndex;
	unsigned int branchTokenIndex;
	unsigned int firstBodyTokenIndex;
	unsigned int lastBodyTokenIndex;
	bool hasLoopDirective;
	unsigned int loopCsClid;
	unsigned int loopCsMlid;
	bool simpleCountedLoop;
	unsigned int memoryLoadCount;
	unsigned int memoryStoreCount;
	bool hasMemoryPreOrPostIncrement;
	bool hasXgkick;
	std::list<std::string> inductionRegisters;
	std::vector<VuLoopInductionUpdate> inductionUpdates;
	std::list<std::string> loopReadWriteRegisters;
	std::vector<const Token*> bodyTokens;
	const Token* branchToken;
};

struct VuSoftwarePipelineRotation
{
	std::string registerBase;
	std::list<std::string> inputFields;
	std::list<std::string> outputFields;
	bool hasScratchRegister;
	std::string scratchRegister;
	// Track 9.E step 3: K-deep rotation bank for multi-stage SWP.
	// bank[0] == scratchRegister when hasScratchRegister == true;
	// bank[k] holds the scratch register for the (k+1)-th extra stage.
	std::vector<std::string> rotationBank;
};

struct VuSoftwarePipelinePrefetch
{
	unsigned int tokenIndex;
	std::string mnemonic;
	VuMemoryKind memoryKind;
	unsigned int memoryFlags;
	bool hasMemoryBase;
	std::string memoryBaseRegister;
	bool hasMemoryOffset;
	long memoryOffset;
	bool readsInductionRegister;
	std::string inductionRegister;
	bool hasNextIterationOffset;
	long nextIterationOffset;
};

struct VuSoftwarePipelineSuffixStore
{
	unsigned int tokenIndex;
	std::string mnemonic;
	bool hasMemoryBase;
	std::string memoryBaseRegister;
	bool hasMemoryOffset;
	long memoryOffset;
	bool usesInductionRegister;
	std::string inductionRegister;
	bool hasNextIterationOffset;
	long nextIterationOffset;
	bool drainCandidate;
	bool hasStoredValueRegister;
	bool storedValueIsFloatRegister;
	std::string storedValueRegister;
	std::list<std::string> storedValueFields;
	bool requiresValueRotation;
	bool hasValueScratchRegister;
	std::string valueScratchRegister;
	bool delayedDrain;
	bool rotateValueBeforePrefetch;
	bool rotateValueAtStore;
};

// Track 9.G-1h step 4b-7a — public descriptor for one node in the
// expanded refit DDG sequence. Mirrors the analysis-private
// VuKernelExpandedNode (anonymous namespace in
// VuSchedulerAnalysis.cpp) one-for-one so that downstream consumers
// (codegen bake-in) can reconstruct the rewritten MAIN body without
// re-deriving the expansion.
//
// Roles:
//   PASSTHROUGH       — emit the source token (indexedTokens[sourceTokenIndex]) verbatim.
//   SPLIT_CLONE       — synthesize one per-field clone of the source token
//                       via splitMultiFieldOpByFieldDecisions(), then select
//                       the cloneOrdinal-th element of the split sequence.
//   MATERIALIZE_MOVE  — synthesize a MOVE that materializes the rename
//                       scratch for decisionIndex; emitted BEFORE the
//                       corresponding cell's PASSTHROUGH op.
//   TAIL_MOVE         — synthesize the post-iteration write-back MOVE for
//                       decisionIndex; emitted AFTER all main-body cells.
//
// sourceCellIndex / sourceTokenIndex are NO_INDEX (-1u) for TAIL_MOVE.
// decisionIndex is NO_INDEX for PASSTHROUGH / SPLIT_CLONE.
struct VuKernelRefitNode
{
	static const unsigned int NO_INDEX = static_cast<unsigned int>( -1 );
	enum Role
	{
		ROLE_PASSTHROUGH      = 0,
		ROLE_SPLIT_CLONE      = 1,
		ROLE_MATERIALIZE_MOVE = 2,
		ROLE_TAIL_MOVE        = 3
	};
	unsigned int role;
	unsigned int sourceCellIndex;
	unsigned int sourceTokenIndex;
	unsigned int decisionIndex;
	unsigned int cloneOrdinal;

	VuKernelRefitNode()
		: role( ROLE_PASSTHROUGH ),
		  sourceCellIndex( NO_INDEX ),
		  sourceTokenIndex( NO_INDEX ),
		  decisionIndex( NO_INDEX ),
		  cloneOrdinal( 0 ) {}
};

struct VuLoopPipelineOpportunity
{
	VuLoopPipelineOpportunity();

	std::string label;
	unsigned int labelTokenIndex;
	unsigned int branchTokenIndex;
	unsigned int qProducerTokenIndex;
	std::vector<unsigned int> qProducerTokenIndices;
	std::vector<VuLoopQStage> qStages;
	unsigned int firstQConsumerTokenIndex;
	unsigned int lastQConsumerTokenIndex;
	unsigned int qProducerLatency;
	unsigned int qProducerConsumerGapCycles;
	unsigned int qProducerConsumerGapDeficitCycles;
	unsigned int loopCarriedQGapCycles;
	unsigned int qProducerInsertionGapCycles;
	unsigned int qProducerInsertionGapDeficitCycles;
	VuLoopQSchedulingStrategy qSchedulingStrategy;
	unsigned int sourcePrefixCycles;
	unsigned int sourceSuffixCycles;
	unsigned int branchDelaySlots;
	unsigned int loopCsClid;
	unsigned int loopCsMlid;
	bool simpleCountedLoop;
	bool hasSingleQProducer;
	bool requiresPrologEpilog;
	bool requiresLoopCarriedRegisters;
	bool qLiveOut;
	bool eligibleSingleQSoftwarePipeline;
	bool hasSoftwarePipelinePlan;
	bool canEmitSoftwarePipeline;
	bool eligibleMultiQSoftwarePipeline;
	bool hasMultiQSoftwarePipelinePlan;
	bool canEmitMultiQSoftwarePipeline;
	bool hasSuffixStoreDrainPlan;
	bool canEmitSuffixStoreDrain;
	std::vector<unsigned int> qConsumerTokenIndices;
	std::vector<unsigned int> prologTokenIndices;
	std::vector<unsigned int> mainTokenIndices;
	std::vector<unsigned int> drainTokenIndices;
	std::vector<unsigned int> multiQPrologTokenIndices;
	std::vector<unsigned int> multiQMainTokenIndices;
	std::vector<unsigned int> multiQCyclicPrefixTokenIndices;
	std::vector<VuSoftwarePipelineRotation> multiQCyclicPrefixRotations;
	unsigned int multiQCyclicPrefixInsertBeforeTokenIndex;
	bool multiQCyclicPrefixNeedsGuard;
	bool multiQCyclicPrefixLastTokenInBranchDelaySlot;
	std::list<std::string> softwarePipelineBlockers;
	std::list<std::string> multiQSoftwarePipelineBlockers;
	std::list<std::string> suffixStoreDrainBlockers;
	std::list<std::string> softwarePipelineRotatedRegisters;
	std::vector<VuSoftwarePipelineRotation> softwarePipelineRotations;
	std::vector<VuSoftwarePipelinePrefetch> softwarePipelinePrefetches;
	std::vector<VuSoftwarePipelineSuffixStore> softwarePipelineSuffixStores;
	std::list<std::string> carriedQInputRegisters;
	std::list<std::string> carriedQOutputRegisters;
	unsigned int memoryLoadCount;
	unsigned int memoryStoreCount;
	bool hasMemoryPreOrPostIncrement;
	bool hasXgkick;
	std::list<std::string> inductionRegisters;
	std::vector<VuLoopInductionUpdate> inductionUpdates;
	std::list<std::string> loopReadWriteRegisters;

	// Track 9.E (multi-stage SWP) — diagnostic-only schema. stageCount=1 for all
	// existing planners; >1 will indicate the loop body has been replicated and
	// software-renamed across multiple in-flight iterations.
	unsigned int stageCount;
	std::vector<unsigned int> kernelTokenIndices;
	std::vector<int> tokenStageOffsets;
	std::map<std::string, unsigned int> stageRotationRegisters;

	// Track 9.G step 6e — VuKernelRewritePlan scaffolding (diagnostic-only).
	// Populated by findVuLoopPipelineOpportunities from the modulo placer's
	// VuKernelLayout. No emission path consumes these yet.
	unsigned int kernelRewriteII;
	unsigned int kernelRewriteStageCount;
	unsigned int kernelRewriteConflicts;
	std::vector<unsigned int> kernelRewritePrologTokens;
	std::vector<unsigned int> kernelRewriteMainTokens;
	std::vector<unsigned int> kernelRewriteDrainTokens;
	std::vector<unsigned int> kernelRewriteEntryStages;

	// Track 9.G step 6f — VuKernelRegisterPlan + rename-hint scaffolding
	// (diagnostic-only). Populated alongside the rewrite-plan scaffolding
	// when the modulo placer runs. No emission path consumes these yet.
	unsigned int kernelRewriteRegCount;
	unsigned int kernelRewriteWawCount;
	unsigned int kernelRewriteRawCount;
	unsigned int kernelRewriteWarCount;
	std::vector<VuKernelRegisterHazard> kernelRewriteHazards;
	std::vector<VuKernelRenameHint> kernelRewriteRenameHints;

	// Track 9.G step 8b-1 — per-register rename decisions (scaffolding).
	// Populated alongside kernelRewriteRenameHints. Each unique hint reg
	// gets one decision; assigned=false means the free-VF budget was
	// exhausted and this loop cannot be renamed under the current
	// policy. Diagnostic-only; no emission path consumes this yet.
	std::vector<VuKernelRenameDecision> kernelRewriteRenameDecisions;

	// Track 9.G step 8b-2a — per-decision MOVE slot (scaffolding).
	// For each decision, the modSlot + lane at which the MAIN_LOOP
	// body must insert `reg <- scratch`. assigned=false means no legal
	// slot/lane was found and the plan must not be made eligible.
	std::vector<VuKernelRenameMoveSlot> kernelRewriteRenameMoveSlots;

	// Track 9.G step 6g — VuKernelEnvelope scaffolding (diagnostic-only).
	// High-level prologue/epilogue cycle counts and per-stage token totals
	// derived from the modulo placer's layout. No emission path consumes
	// these yet.
	unsigned int kernelEnvelopeKernelTokens;
	unsigned int kernelEnvelopePrologueCycles;
	unsigned int kernelEnvelopeEpilogueCycles;
	unsigned int kernelEnvelopeConflicts;
	std::vector<unsigned int> kernelEnvelopePrologueTokenCounts;
	std::vector<unsigned int> kernelEnvelopeEpilogueTokenCounts;

	// Track 9.G step 6h — stageCells VLIW grid (diagnostic-only).
	// Mirrors VuKernelRewritePlan::stageCells: a stageCount * II grid of
	// (upper, lower, fdiv, efu) layout-entry indices indexed as
	// (stage * II + modSlot). No emission path consumes this yet.
	std::vector<VuKernelTemplateSlot> kernelRewriteStageCells;

	// Track 9.G-1h step 4b-3b-4 — shadow-opportunity refit metrics.
	// Populated by runExpandedDDGRefitDiagnostic after the modulo placer
	// has run on the original opportunity (always; 4b-6 dropped the env
	// gate around the work — only the stderr line stays gated on
	// OPENVCL_USE_EXPANDED_DDG_PLACER=1). Values are the placer outputs
	// for the SHADOW opportunity whose mainTokenIndices reflect the
	// expanded-DDG sequence (split clones + materialize MOVEs + tail
	// MOVEs, see VuKernelExpandedNode). All zero when the refit path
	// was not eligible. Refit VLIW cells are not exposed here because
	// their layout-entry indices reference a shadow VuKernelLayout that
	// lives only inside the helper; only the scalar verdicts are
	// public. 4b-3b-5 wires CodeGenerator to read these when non-zero.
	unsigned int kernelRewriteRefitII;
	unsigned int kernelRewriteRefitStageCount;
	unsigned int kernelRewriteRefitConflicts;
	unsigned int kernelRewriteRefitMainTokenCount;

	// Track 9.G-1h step 4b-7a — published refit-grid + node descriptors.
	// kernelRewriteRefitNodes is the ordered expanded-node sequence the
	// refit placer was run on. kernelRewriteRefitMainTokens is the
	// refit placer's main grid (II*4 cells; lane order upper, lower,
	// fdiv, efu), where each non-NO_INDEX entry is an index into
	// kernelRewriteRefitNodes. Both vectors are empty when the refit
	// path was not eligible, when no node was produced, or when the
	// refit placer reported conflicts. Codegen consumes these in
	// 4b-7b/c; dormant in 4b-7a.
	std::vector<VuKernelRefitNode> kernelRewriteRefitNodes;
	std::vector<unsigned int>      kernelRewriteRefitMainTokens;
};

struct VuSoftwarePipelineRewritePlan
{
	VuSoftwarePipelineRewritePlan();

	std::string label;
	std::string prologLabel;
	std::string mainLabel;
	std::string drainLabel;
	unsigned int labelTokenIndex;
	unsigned int branchTokenIndex;
	unsigned int qProducerTokenIndex;
	unsigned int prefetchInsertAfterTokenIndex;
	unsigned int qProducerInsertAfterTokenIndex;
	bool qProducerInBranchDelaySlot;
	bool qProducerBranchDelayBlockedBySuffixDependency;
	unsigned int qProducerBranchDelaySuffixBlockerTokenIndex;
	bool cyclicPrefixBeforeBranch;
	unsigned int cyclicPrefixInsertBeforeTokenIndex;
	bool cyclicPrefixNeedsGuard;
	bool cyclicPrefixLastTokenInBranchDelaySlot;
	bool drainsSuffixStores;
	bool emitsDrain;
	std::vector<unsigned int> prefetchTokenIndices;
	std::vector<unsigned int> cyclicPrefixTokenIndices;
	std::vector<VuSoftwarePipelineRotation> cyclicPrefixRotations;
	std::vector<VuLoopInductionUpdate> inductionUpdates;
	std::vector<VuSoftwarePipelinePrefetch> prefetches;
	std::vector<VuSoftwarePipelineRotation> rotations;
	std::vector<VuSoftwarePipelineSuffixStore> suffixStores;
	std::vector<unsigned int> prologTokenIndices;
	std::vector<unsigned int> mainTokenIndices;
	std::vector<unsigned int> drainTokenIndices;
	std::vector<unsigned int> qConsumerTokenIndices;

	// Track 9.E (multi-stage SWP) — diagnostic-only schema. stageCount=1 for all
	// existing planners; >1 will indicate prolog/main/drain encode replicated
	// kernel iterations.
	unsigned int stageCount;
	std::vector<unsigned int> kernelTokenIndices;
	std::vector<int> tokenStageOffsets;
	std::map<std::string, unsigned int> stageRotationRegisters;

	// Track 9.G step 6e — VuKernelRewritePlan scaffolding (diagnostic-only).
	// Mirrors the like-named fields on VuLoopPipelineOpportunity. Populated by
	// buildVuSoftwarePipelineRewritePlans by copy from the source opportunity.
	// No emission path consumes these yet.
	unsigned int kernelRewriteII;
	unsigned int kernelRewriteStageCount;
	unsigned int kernelRewriteConflicts;
	std::vector<unsigned int> kernelRewritePrologTokens;
	std::vector<unsigned int> kernelRewriteMainTokens;
	std::vector<unsigned int> kernelRewriteDrainTokens;
	std::vector<unsigned int> kernelRewriteEntryStages;

	// Track 9.G step 6f — register-plan + rename-hint scaffolding
	// (diagnostic-only). Mirrors the opportunity-side fields.
	unsigned int kernelRewriteRegCount;
	unsigned int kernelRewriteWawCount;
	unsigned int kernelRewriteRawCount;
	unsigned int kernelRewriteWarCount;
	std::vector<VuKernelRegisterHazard> kernelRewriteHazards;
	std::vector<VuKernelRenameHint> kernelRewriteRenameHints;

	// Track 9.G step 8b-1 — rename decisions (scaffolding).
	// Mirrors the opportunity-side field.
	std::vector<VuKernelRenameDecision> kernelRewriteRenameDecisions;

	// Track 9.G step 8b-2a — per-decision MOVE slot (scaffolding).
	// Mirrors the opportunity-side field.
	std::vector<VuKernelRenameMoveSlot> kernelRewriteRenameMoveSlots;

	// Track 9.G step 6g — VuKernelEnvelope scaffolding (diagnostic-only).
	// Mirrors the opportunity-side envelope fields.
	unsigned int kernelEnvelopeKernelTokens;
	unsigned int kernelEnvelopePrologueCycles;
	unsigned int kernelEnvelopeEpilogueCycles;
	unsigned int kernelEnvelopeConflicts;
	std::vector<unsigned int> kernelEnvelopePrologueTokenCounts;
	std::vector<unsigned int> kernelEnvelopeEpilogueTokenCounts;

	// Track 9.G step 6h — stageCells VLIW grid (diagnostic-only).
	// Mirrors the opportunity-side stageCells field.
	std::vector<VuKernelTemplateSlot> kernelRewriteStageCells;

	// Track 9.G-1h step 4b-3b-5 — shadow-opportunity refit metrics,
	// mirrored from VuLoopPipelineOpportunity by
	// buildVuSoftwarePipelineRewritePlans so emission-side consumers
	// (CodeGenerator) can see the placer's shadow verdict without
	// reaching back into analysis state. After 4b-6 these are populated
	// unconditionally (whenever the refit path was eligible); the
	// OPENVCL_USE_EXPANDED_DDG_PLACER env var now only toggles the
	// `[refit-codegen]` stderr line in CodeGenerator. Dormant: no
	// emission path branches on these yet.
	unsigned int kernelRewriteRefitII;
	unsigned int kernelRewriteRefitStageCount;
	unsigned int kernelRewriteRefitConflicts;
	unsigned int kernelRewriteRefitMainTokenCount;

	// Track 9.G-1h step 4b-7a — mirror of opportunity-side refit-grid
	// + node descriptors, copied through by
	// buildVuSoftwarePipelineRewritePlans. See the opportunity field
	// comment for semantics. Dormant in 4b-7a; consumed by 4b-7b/c.
	std::vector<VuKernelRefitNode> kernelRewriteRefitNodes;
	std::vector<unsigned int>      kernelRewriteRefitMainTokens;
};

std::vector<VuBasicBlock> buildVuBasicBlocks( const std::list<Token>& tokens );
std::vector<VuDependencyEdge> buildVuDependencyGraph( const VuBasicBlock& block,
                                                      unsigned int ignoredImplicitWawResources = 0 );
std::vector<VuScheduledIssueSlot> scheduleVuBasicBlockReadyIssueSlots( const VuBasicBlock& block,
                                                                       unsigned int ignoredImplicitWawResources = 0 );
VuScheduledProgram scheduleVuProgramReadyIssueSlots( const std::list<Token>& tokens,
                                                     unsigned int ignoredImplicitWawResources = 0 );
std::vector< std::vector<VuScheduledIssueSlot> > scheduleVuBasicBlocksReadyIssueSlotsWithFlagLiveness(
    const std::list<Token>& tokens );
VuScheduledProgram scheduleVuProgramReadyIssueSlotsWithFlagLiveness( const std::list<Token>& tokens );
std::list<Token> flattenVuScheduledProgramTokens( const VuScheduledProgram& program );

// Instruction words this schedule will cost in micro memory: one per issue slot,
// except a multi-cycle padding slot, which costs only the nops the emitter actually
// writes for it (the FMAC interlock resolves the rest on the hardware). NOT the same
// number as VuScheduledProgram::cycleCount, and the only one a size comparison
// between two schedules of the same program may use.
unsigned int vuScheduledProgramEmittedWordCount( const VuScheduledProgram& program );
unsigned int vuIgnoredFlagWawResourcesForRemaining( std::list<Token>::const_iterator begin,
                                                    std::list<Token>::const_iterator end );
// The three-iterator form knows where the PROGRAM starts, not just where the
// remaining range does, so a back edge out of the remaining range can be
// followed to the readers ahead of it. The two-iterator form is the old
// behaviour and is what a caller with no list start can still ask for.
unsigned int vuIgnoredFlagWawResourcesForRemaining( std::list<Token>::const_iterator programBegin,
                                                    std::list<Token>::const_iterator begin,
                                                    std::list<Token>::const_iterator end );
VuScheduledPaddingKind vuScheduledPaddingKindForReadHazard( const Token& token,
                                                            const Token* partner,
                                                            const VuLatencyTracker& latencyTracker,
                                                            int currentCycle );
std::vector<VuLoopCandidate> findVuLoopCandidates( const std::list<Token>& tokens );
std::vector<VuLoopPipelineOpportunity> findVuLoopPipelineOpportunities( const std::list<Token>& tokens );
std::vector<VuSoftwarePipelineRewritePlan> buildVuSoftwarePipelineRewritePlans( const std::list<Token>& tokens );
std::list<Token> applyVuSoftwarePipelinePlans( const std::list<Token>& tokens );
bool advanceVuStoreBaseUpdates( std::list<Token>& tokens );
bool vuScheduledProgramHasNoCycleRegression( const VuScheduledProgram& original,
                                             const VuScheduledProgram& candidate );
std::list<Token> applyVuSoftwarePipelinePlansWithSafeStoreBaseAdvance( const std::list<Token>& tokens );

// Track 9.G step 7a — eligibility for the generic kernel-rewrite emitter.
// Returns true when the modulo-placer scaffolding (steps 6a-6h) on a
// VuSoftwarePipelineRewritePlan is complete enough that a multi-stage
// emitter could consume it without further rename support:
//   - kernelRewriteII > 0
//   - kernelRewriteStageCount >= 2
//   - kernelRewriteConflicts == 0
//   - kernelRewriteRenameHints empty (no cross-stage register reuse to resolve)
//   - kernelRewriteMainTokens non-empty (the MAIN_LOOP body exists)
// Diagnostic-only; no consumer yet.
bool isVuPlanEligibleForGenericKernelRewrite( const VuSoftwarePipelineRewritePlan& plan );

// Track 9.G step 7b: SCE-style multi-stage emitter.
// For each VuSoftwarePipelineRewritePlan that passes
// isVuPlanEligibleForGenericKernelRewrite, splice the loop body into
// the SCE-convention 4-label form:
//
//   <label>__PRO1:
//       <kernelRewritePrologTokens>
//       <branch>          ; retargeted to <label>__EPI1
//   <label>__MAIN_LOOP:
//       <kernelRewriteMainTokens>
//       <branch>          ; retargeted to <label>__MAIN_LOOP
//   <label>__EPI0:
//       <kernelRewriteDrainTokens>
//   <label>__EPI1:        ; tail entry for the skip-main path
//
// Plans that are NOT eligible are passed through unchanged (they remain
// the responsibility of applyVuSoftwarePipelinePlans).
//
// This function does NOT consult any environment variable; the
// OPENVCL_USE_GENERIC_KERNEL_REWRITE gate is enforced at the call site
// in CodeGenerator.cpp.
std::list<Token> applyVuGenericKernelRewritePlans( const std::list<Token>& tokens );

// Test seam: same behaviour as the single-argument overload but uses
// the explicitly provided plan vector instead of calling
// buildVuSoftwarePipelineRewritePlans internally. Lets unit tests
// inject hand-crafted eligible plans against synthetic token streams.
std::list<Token> applyVuGenericKernelRewritePlans( const std::list<Token>& tokens,
                                                   const std::vector<VuSoftwarePipelineRewritePlan>& plans );

// 9.G-1h-4a-2: out-of-band descriptor for the MAIN steady-state body
// of one rewritten loop. The body lives in the half-open token range
// [mainLabel, endLabel) of the rewritten stream:
//   - mainLabel: the "<plan.label>__MAIN_LOOP" label token marking the
//     first body cycle.
//   - endLabel:  the "<plan.label>__EPI0" label token, which directly
//     follows the closing branch of the MAIN body.
// Populated by the 3-argument overload of applyVuGenericKernelRewritePlans
// below; consumed by the 4a-3 scheduler-bypass pass.
//
// 9.G-1h-4a-3a: also carry the placer grid (II * 4 cells, lane order:
// upper, lower, fdiv, efu) and the II. Each grid entry is an index
// into the INPUT token list passed to applyVuGenericKernelRewritePlans,
// or VuKernelRewritePlan::NO_TOKEN for an empty (NOP) cell. The
// indices are only valid during the same rewriting pass; consumers
// (4a-3b) are responsible for resolving them to live tokens before
// any subsequent token-list mutation.
struct VuKernelBlockRange
{
	std::string mainLabel;
	std::string endLabel;
	unsigned int II;
	std::vector<unsigned int> placerGridMainTokens;

	// Track 9.G-1h step 4b-7a — refit-grid + node descriptors, copied
	// from VuSoftwarePipelineRewritePlan by
	// applyVuGenericKernelRewritePlans. refitMainTokens is the refit
	// placer's main grid (II*4 cells; lane order upper, lower, fdiv,
	// efu) with each non-NO_INDEX entry indexing into refitNodes.
	// Both are empty when the rewrite did not go through the refit
	// path (e.g. when the original placer already produced a non-
	// rename-eligible layout). Dormant in 4b-7a; codegen wires up to
	// these in 4b-7b/c.
	std::vector<VuKernelRefitNode> refitNodes;
	std::vector<unsigned int>      refitMainTokens;
};

// 9.G-1h-4a-2: same behaviour as the 2-argument overload, but also
// fills outRanges with one entry per actually-rewritten plan. The
// vector is cleared first. MD5-invariant on the token output relative
// to the 2-argument overload.
std::list<Token> applyVuGenericKernelRewritePlans( const std::list<Token>& tokens,
                                                   const std::vector<VuSoftwarePipelineRewritePlan>& plans,
                                                   std::vector<VuKernelBlockRange>& outRanges );
std::list<Token> scheduleVuTokensPreservingOrder( const std::list<Token>& tokens );
std::list<Token> scheduleVuTokensReadySet( const std::list<Token>& tokens,
                                           unsigned int ignoredImplicitWawResources = 0 );
std::list<Token> scheduleVuTokensReadySetWithFlagLiveness( const std::list<Token>& tokens );

// Track 9.G step 8b-2c-1 — per-field op-splitter for kernel rename.
//
// vuOpIsSplittableForKernelRename returns true when `token`'s
// instruction is on the conservative FMAC allowlist (add/sub/mul/madd/
// msub/max/mini and their broadcast variants). Such an op writes a
// single FLOAT_REGISTER destination whose field mask matches the op's
// field set, and each output field depends only on the matching input
// field — making it safe to split into single-field clones for the
// purpose of honouring per-field rename decisions.
//
// splitMultiFieldOpByFieldDecisions clones `token` into one
// destination-retargeted single-field op per decision whose `reg` base
// matches the token's destination and whose field is present in the
// token's `fields()` mask. Fields not covered by any decision are
// emitted as a single residual clone with the destination unchanged.
// When `token` is unsplittable, or no decision matches its
// destination, the token is appended verbatim (single clone). The
// helper never mutates `token`.
bool vuOpIsSplittableForKernelRename( const Token& token );
void splitMultiFieldOpByFieldDecisions( const Token& token,
                                        const std::vector<VuKernelRenameDecision>& decisions,
                                        std::list<Token>& out );

// Track 9.G step 8b-2c-2 — eligibility gate for the per-field
// kernel-rename emission path. Returns true iff:
//   - the plan satisfies the 8b-2b base scaffolding (II>0,
//     stageCount>=2, conflicts==0, mainTokens non-empty);
//   - kernelRewriteRenameDecisions and kernelRewriteRenameMoveSlots
//     are non-empty, equal in length, and every entry is assigned;
//   - every mainTokens op that reads or writes any decision base is
//     on the splittable allowlist (vuOpIsSplittableForKernelRename).
// kernelRewriteRenameHints is intentionally NOT required to be empty
// here: rename resolution is the whole point of this path.
//
// countKernelRenameEmissionBlockers returns the number of mainTokens
// ops that touch a decision base but are NOT splittable — i.e. the
// reasons isVuPlanEligibleForKernelRenameEmission returned false.
// Returns 0 when the scaffolding itself is incomplete (the predicate
// already rejected such plans).
//
// Diagnostic-only at this step; no emission path consumes them yet.
bool isVuPlanEligibleForKernelRenameEmission( const VuSoftwarePipelineRewritePlan& plan,
                                              const std::vector<const Token*>& indexedTokens );
unsigned int countKernelRenameEmissionBlockers( const VuSoftwarePipelineRewritePlan& plan,
                                                const std::vector<const Token*>& indexedTokens );

// describeKernelRenameEmissionBlockers returns the op names of the
// mainTokens that hard-block kernel-rename emission (unsplittable AND
// write a decision base). Useful for the diagnostic env var when
// surgically widening the allowlist.
std::vector<std::string> describeKernelRenameEmissionBlockers(
	const VuSoftwarePipelineRewritePlan& plan,
	const std::vector<const Token*>& indexedTokens );

// Track 9.G step 8b-2d-3: "soft blockers" are unsplittable mainTokens
// that touch a decision base but only READ it (no write to the
// renamed reg). They are not hard blockers: the emitter handles them
// by materializing the renamed reg from its scratches immediately
// before the op, then emitting the op unchanged.
unsigned int countKernelRenameEmissionSoftBlockers( const VuSoftwarePipelineRewritePlan& plan,
                                                    const std::vector<const Token*>& indexedTokens );
std::vector<std::string> describeKernelRenameEmissionSoftBlockers(
	const VuSoftwarePipelineRewritePlan& plan,
	const std::vector<const Token*>& indexedTokens );

// tokenIsKernelRenameMaterializeCandidate returns true iff `token`
// is a soft blocker against `plan`: it touches a decision base, is
// not splittable, and does not write any decision base. The emitter
// must prepend per-decision materialize MOVEs before such a token.
bool tokenIsKernelRenameMaterializeCandidate( const Token& token,
                                              const VuSoftwarePipelineRewritePlan& plan );

}

#endif
