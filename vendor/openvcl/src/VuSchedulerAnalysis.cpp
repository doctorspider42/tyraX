#include "VuSchedulerAnalysis.h"

#include "VuLatencyTracker.h"
#include "VuModuloReservationTable.h"
#include "VuKernelLayout.h"
#include "VuSchedulingRules.h"
#include "VuTokenResourceAccess.h"

#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <algorithm>

namespace vcl
{

extern const unsigned int VU_SCHEDULED_TOKEN_INDEX_NONE = ~0u;

// Track 9.G-1h step 4b-7a: out-of-class definition for the
// VuKernelRefitNode::NO_INDEX in-class static const (declared in
// VuSchedulerAnalysis.h). Required because the value is ODR-used
// (passed to vector::push_back by const reference) at the refit-
// publish site below.
const unsigned int VuKernelRefitNode::NO_INDEX;

namespace
{
	// Defined further down, next to the padding it belongs with.
	int pairingHazardDelay( const VuLatencyTracker& latencyTracker,
	                        const Token& token,
	                        const Token* partner,
	                        int currentCycle,
	                        bool stallAware = false );

	bool containsKey( const std::list<std::string>& keys, const std::string& key )
	{
		for( std::list<std::string>::const_iterator i = keys.begin(); i != keys.end(); ++i )
		{
			if( *i == key )
				return true;
		}
		return false;
	}

	bool intersects( const std::list<std::string>& a, const std::list<std::string>& b )
	{
		for( std::list<std::string>::const_iterator i = a.begin(); i != a.end(); ++i )
		{
			if( containsKey( b, *i ) )
				return true;
		}
		return false;
	}

	void addEdge( std::vector<VuDependencyEdge>& edges, unsigned int before, unsigned int after, VuDependencyKind kind )
	{
		edges.push_back( VuDependencyEdge( before, after, kind ) );
	}

	// `chainWriters` is the difference between a flag register and a flag SHIFT
	// register. MAC is a register: only the newest write is visible to a reader,
	// so the writers before it may run in any order as long as they all precede
	// it - which is what the writer->finalWriter fan-in expresses. CLIP is a
	// 24-bit window of the last FOUR judgements, and a positional reader
	// (`fcand VI01,0x3CA3CA` names all four entries) sees which push landed in
	// which entry, so the writers must keep their program order among themselves
	// as well. Chaining is what says that.
	// `alreadyBarriered` is how many of `writers` a previous call already covered.
	// The list is no longer emptied at a reader (see below), so this runs once per
	// reader over a list that only grows, and without it the fan-in would be
	// re-emitted in full every time - correct, because duplicate edges cancel
	// between `incoming` and `outgoing`, but quadratic for nothing.
	void addImplicitFlagWriterBarrierEdges( std::vector<VuDependencyEdge>& edges,
	                                        const std::vector<unsigned int>& writers,
	                                        bool preserveFinalWriter,
	                                        bool chainWriters,
	                                        unsigned int alreadyBarriered = 0 )
	{
		if( writers.size() < 2 )
			return;

		if( chainWriters )
		{
			unsigned int first = alreadyBarriered < 1 ? 1 : alreadyBarriered;
			for( unsigned int i = first; i < writers.size(); ++i )
				addEdge( edges, writers[i - 1], writers[i], VU_DEPENDENCY_RESOURCE_WAW );
			return;
		}

		if( !preserveFinalWriter )
			return;

		// Nothing has been added since the last call, so the final writer is the
		// one that was already fanned into.
		if( alreadyBarriered == writers.size() )
			return;

		const unsigned int finalWriter = writers.back();
		for( std::vector<unsigned int>::const_iterator writer = writers.begin();
		     writer != writers.end(); ++writer )
		{
			if( *writer != finalWriter )
				addEdge( edges, *writer, finalWriter, VU_DEPENDENCY_RESOURCE_WAW );
		}
	}

	// An ACCUMULATING resource: writes merge instead of replacing, so there is no
	// write-after-write hazard between two of them and nothing for a later write to
	// kill. The status register is the one - every FMAC and every DIV/SQRT/RSQRT
	// OR-s into its sticky half, and only FSSET clears it.
	//
	// This is deliberately NOT addPreciseImplicitFlagDependencies with a flag. That
	// one models a register: it edges the writers into the LAST writer and only the
	// last writer into the reader, which is right for a value that gets replaced
	// and wrong for one that gets merged. (It used also to empty its writer list at
	// a reader. That was not a difference between the two, it was a bug in that
	// one, and this pass is what it was fixed against.) Here:
	//
	//   * every contributor since the last clear is edged into every reader, because
	//     every one of them is part of the answer the reader gets;
	//   * a reader does not retire anything, so a second reader is still ordered
	//     behind the first reader's contributors;
	//   * there is no writer-to-writer edge at all, which is the whole point - the
	//     alternative under the pairwise builder is a WAW edge between every pair of
	//     FMACs, which would serialise every program containing one `fsand`;
	//   * except that the register's non-sticky half (Z/S/U/O/I/D) IS
	//     last-writer-wins, so a reader whose mask names one of those bits pins the
	//     last contributor as the last one. `fsand VI01,0x0C00` - only sticky bits -
	//     pays nothing for that; `fsand VI01,0x0002` pays exactly what a MAC reader
	//     pays today. The mask is in the source, so the cost is the program's own.
	//
	// FSSET is a hard barrier and needs no case of its own: it is declared reading
	// AND writing the resource, so the reader branch below edges every contributor
	// into it, the WAR test in the pairwise builder edges every earlier READER into
	// it, and the explicit clear->contributor edges here keep later contributors
	// below it.
	void addAccumulatingImplicitFlagDependencies( std::vector<VuDependencyEdge>& edges,
	                                              const std::vector<VuTokenResourceAccess>& accesses,
	                                              const std::vector<const Token*>& tokens,
	                                              unsigned int resource )
	{
		std::vector<unsigned int> contributors;
		bool hasClear = false;
		unsigned int clearIndex = 0;

		for( unsigned int index = 0; index < accesses.size(); ++index )
		{
			const bool readsResource = (accesses[index].implicitReads & resource) != 0;
			const bool writesResource = (accesses[index].implicitWrites & resource) != 0;
			const bool clearsResource = readsResource && writesResource;

			if( readsResource )
			{
				for( std::vector<unsigned int>::const_iterator c = contributors.begin();
				     c != contributors.end(); ++c )
					addEdge( edges, *c, index, VU_DEPENDENCY_RESOURCE_RAW );

				if( !clearsResource
				    && contributors.size() > 1
				    && resource == VU_RESOURCE_STATUS
				    && vuStatusReadNeedsLastWriter( *tokens[index] ) )
					addImplicitFlagWriterBarrierEdges( edges, contributors, true, false );
			}

			if( clearsResource )
			{
				contributors.clear();
				hasClear = true;
				clearIndex = index;
				continue;
			}

			if( writesResource )
			{
				if( hasClear )
					addEdge( edges, clearIndex, index, VU_DEPENDENCY_RESOURCE_WAW );
				contributors.push_back( index );
			}
		}

		// And there is deliberately NO trailing live-out barrier, which is where an
		// accumulating resource stops resembling a register. For MAC or CLIP the last
		// writer in the block produces the value that leaves it, so when the resource
		// is live out that writer has to stay last. An accumulating resource has no
		// such writer: the value leaving the block is every contributor since the last
		// clear, OR-ed, and OR does not care what order it ran in. The sticky bits are
		// the half of the status register that exists to be read later - that is what
		// makes them sticky - and any order of contributors hands the successor the
		// same ones.
		//
		// The non-sticky half is not covered by that argument and is not modelled
		// across the block boundary. It cannot usefully be: `out_hw_status` names one
		// register with two halves, and "the flags of whichever instruction happens to
		// be last" is not a contract any scheduler can keep. Inside the block, where
		// the question IS answerable - the reader's own mask says which half it wants -
		// it is answered, above.
	}

	void addPreciseImplicitFlagDependencies( std::vector<VuDependencyEdge>& edges,
	                                         const std::vector<VuTokenResourceAccess>& accesses,
	                                         unsigned int resource,
	                                         unsigned int ignoredImplicitWawResources )
	{
		std::vector<unsigned int> pendingWriters;
		unsigned int barrieredWriters = 0;
		const bool preserveLiveOutWriter = (ignoredImplicitWawResources & resource) == 0;
		// Only CLIP shifts; every other implicit resource here is a plain register.
		// Not behind a flag: which push landed in which entry is a property of the
		// hardware, not of a command-line option.
		const bool chainWriters = resource == VU_RESOURCE_CLIP;

		for( unsigned int index = 0; index < accesses.size(); ++index )
		{
			const bool readsResource = (accesses[index].implicitReads & resource) != 0;
			const bool writesResource = (accesses[index].implicitWrites & resource) != 0;

			if( readsResource )
			{
				if( !pendingWriters.empty() )
				{
					addImplicitFlagWriterBarrierEdges( edges, pendingWriters, true, chainWriters,
					                                   barrieredWriters );
					barrieredWriters = pendingWriters.size();
					addEdge( edges, pendingWriters.back(), index, VU_DEPENDENCY_RESOURCE_RAW );
				}

				// AND THE LIST IS NOT EMPTIED HERE. A reader consumes nothing: the
				// value it read is still the value in the register, so the NEXT
				// reader of the same resource observes the same writers and needs
				// the same edges. Clearing left a second reader with no edge at
				// all - free to be scheduled above the writers it reads, and above
				// the first reader - and that is a miscompile, not a relaxation.
				//
				// It survived because something else usually orders the pair by
				// accident: two `fcand`s writing the same VI register have a
				// register WAW between them, and the RAW into the first one then
				// holds the second down transitively. Give them different
				// destinations, or make the second reader's fields disjoint from
				// the first's - `madd.x`/`madd.y`/`madd.z` draining one ACC seed -
				// and nothing holds it at all. Both shapes are in this engine.
				//
				// Only an actual KILL retires a writer, and for these resources
				// there is no kill to model: a later write is edged into the
				// readers after it by the same fan-in, so keeping the earlier ones
				// in the list costs redundant WAW edges and nothing else. This is
				// what addAccumulatingImplicitFlagDependencies has always done.
			}

			if( writesResource )
				pendingWriters.push_back( index );
		}

		// The trailing group has no reader after it, so its order is observable
		// only if the resource is live out - and when it is not, a shift register
		// is as unordered as a plain one. `preserveLiveOutWriter` gates the chain
		// for the same reason it gates the fan-in.
		addImplicitFlagWriterBarrierEdges( edges, pendingWriters, preserveLiveOutWriter,
		                                   chainWriters && preserveLiveOutWriter,
		                                   barrieredWriters );
	}

	bool memoryOrderRequiresDependency( const VuTokenResourceAccess& beforeAccess,
	                                    const VuTokenResourceAccess& afterAccess,
	                                    const Token& before,
	                                    const Token& after )
	{
		if( beforeAccess.memoryKind == VU_MEMORY_NONE || afterAccess.memoryKind == VU_MEMORY_NONE )
			return false;
		if( !isVuMemoryOrderingAccess( before ) && !isVuMemoryOrderingAccess( after ) )
			return false;
		return !vuTokenCanMoveBefore( after, before );
	}

	VuBasicBlockTerminatorKind vuBlockTerminatorKind( const Token& token )
	{
		if( isVuBoundaryOperand( token ) )
			return VU_BASIC_BLOCK_TERMINATOR_BOUNDARY;
		if( isVuXgkick( token ) )
			return VU_BASIC_BLOCK_TERMINATOR_XGKICK;
		if( vuTokenBranchDelaySlots( token ) > 0 )
			return VU_BASIC_BLOCK_TERMINATOR_BRANCH;
		if( token.flags() & Token::PREORDERED )
			return VU_BASIC_BLOCK_TERMINATOR_PREORDERED;
		return VU_BASIC_BLOCK_TERMINATOR_NONE;
	}

	// How many modelled cycles one extra instruction word has to buy before
	// --pair-best-of-cycles will spend it.
	//
	// A cycle-cheaper schedule has ALREADY paid for its own extra rows - the trial
	// cycle count includes them - so at rate 1 the flag takes every net win. It is
	// not 1 because VU1 micro memory is a hard ceiling and a frame rate is not.
	// Measured over the ten resident programs, which share one 2042-word upload:
	//
	//   rate  1: 2086 words / 2750 cycles   - over the ceiling, unusable
	//   rate  2: 2016 words / 2847 cycles   - 26 words of headroom left
	//   rate  4: 1984 words / 2956 cycles
	//   off:     1968 words / 3010 cycles      (SCE: 2028 / 2433)
	//
	// 4 rather than 2, for whoever enables this flag - it is NOT enabled by default,
	// and the measurement below is why.
	//
	// Rate 2 fits the 2042-word ceiling but puts TyraX's 45 generated programs at
	// 9392 words against SCE's 9264 - handing back a won result to buy cycles. Rate 4
	// is 9248 (sixteen under SCE) and 1984 resident (58 under the ceiling, 44 under
	// SCE), for -2.76% modelled cycles against rate 2's -5.91%: 47% of the cycles for
	// 29% of the words. Rate 1 moves blocks neither reaches, and costs 2086 - over the
	// hardware ceiling.
	//
	// The modelled figure looked like an UNDERSTATEMENT of rate 4, and that argument
	// was wrong. The model counts each block once, and rate 4 spends its words almost
	// entirely in the clipper's per-edge and per-emitted-vertex loops - the deepest
	// code in the corpus - putting `edgeLoop` at exact SCE cycle parity (45) in all
	// five resident clip programs. Weighted by trip counts that should be worth more
	// than 2.76%. Measured on the console it is worth nothing: alternated A/B against
	// the flag-off build on a VU1-bound scene, 84.01/84.61 FPS against 84.46/85.05 -
	// consistently about half a percent SLOWER, in both pairs. Modelled cycles and
	// frames have now disagreed in DIRECTION three times on that scene, and only one
	// change in this whole migration turned modelled cycles into frames
	// (--split-dead-float-ranges, -16.8% modelled -> +7.7% measured).
	//
const unsigned int VU_PAIR_CYCLES_PER_WORD = 4;

	// One point in the space of ready-list heuristics. The default values are the
	// only behaviour openvcl had before --pair-best-of-many; the alternatives exist
	// so that several complete schedules can be tried per segment and the shortest
	// kept, which is why a variant can never make the output longer.
	//
	// Only the knobs some shipped strategy actually varies live here. Three more were
	// swept and never won a single segment - the pipe-alternation bonus, the
	// latency-load bonus, and a reward for unblocking an opposite-pipe successor - and
	// their values are back to being the constants they effectively were. See
	// "--pair-best-of-many" in the engine's docs/toolchain-image.md before adding one
	// of them back.
	struct VuReadySegmentStrategy
	{
		VuReadySegmentStrategy()
		{
			preferUnblockingWhenUnpaired = false;
			priorityWeight = 20;
			longLatencyProducerBonus = 500;
			partnerPrefersWorst = false;
			stallAwarePairing = false;
		}

		bool preferUnblockingWhenUnpaired;
		int priorityWeight;
		int longLatencyProducerBonus;
		// Fill the row with the least valuable legal partner rather than the best one,
		// leaving the valuable one to be a primary on a row of its own.
		bool partnerPrefersWorst;
		// Judge a partner by the cycles it costs the row rather than by the nop words
		// it saves - see pairingHazardDelay. Only --pair-best-of-cycles sets it, and
		// only as an EXTRA arm to compare against, never as the sole behaviour.
		bool stallAwarePairing;
	};

	int readyCandidateScore( unsigned int candidate,
	                         bool haveLastPipe,
	                         bool lastWasLower,
	                         const VuBasicBlock& block,
	                         const std::vector<unsigned int>& priority,
	                         const VuLatencyTracker& latencyTracker,
	                         unsigned int currentCycle,
	                         const VuReadySegmentStrategy& strategy )
	{
		int score = static_cast<int>( candidate );
		const int delay =
		    latencyTracker.readHazardDelay( *block.tokens[candidate], NULL, static_cast<int>( currentCycle ) );
		score += delay * 1000;

		if( isVuLongLatencyProducer( *block.tokens[candidate] ) )
			score -= strategy.longLatencyProducerBonus;
		else if( isVuLatencyLoad( *block.tokens[candidate] ) )
			score -= 300;

		if( haveLastPipe && isVuLowerPipe( *block.tokens[candidate] ) != lastWasLower )
			score -= 100;

		if( delay == 0 && candidate < priority.size() )
			score -= static_cast<int>( priority[candidate] ) * strategy.priorityWeight;

		return score;
	}

	std::vector<unsigned int> buildDependencyPriorities( const VuBasicBlock& block,
	                                                    const std::vector< std::vector<unsigned int> >& outgoing )
	{
		std::vector<unsigned int> priority( block.tokens.size(), 1 );

		for( unsigned int reverse = block.tokens.size(); reverse > 0; --reverse )
		{
			const unsigned int i = reverse - 1;
			unsigned int cost = 1;
			if( block.tokens[i]->operand() && block.tokens[i]->operand()->latency() > cost )
				cost = block.tokens[i]->operand()->latency();
			unsigned int best = cost;
			for( std::vector<unsigned int>::const_iterator edge = outgoing[i].begin(); edge != outgoing[i].end(); ++edge )
			{
				if( *edge < priority.size() && priority[*edge] + cost > best )
					best = priority[*edge] + cost;
			}
			priority[i] = best;
		}

		return priority;
	}

	bool tokenWritesMacForPair( const Token& token )
	{
		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess( token, access ) )
			return false;
		return (access.implicitWrites & VU_RESOURCE_MAC) != 0;
	}

	bool tokenSchedulesAsUpper( const Token& token, unsigned int ignoredImplicitWawResources )
	{
		if( (ignoredImplicitWawResources & VU_RESOURCE_MAC) && isVuMoveAsUpperMaxCandidate( token ) )
			return true;
		return !isVuLowerPipe( token );
	}

	bool tokenSchedulesAsLower( const Token& token, unsigned int ignoredImplicitWawResources )
	{
		return !tokenSchedulesAsUpper( token, ignoredImplicitWawResources ) && isVuLowerPipe( token );
	}

	bool tokenWritesMacForPair( const Token& token, unsigned int ignoredImplicitWawResources )
	{
		if( tokenSchedulesAsUpper( token, ignoredImplicitWawResources ) && isVuMoveAsUpperMaxCandidate( token ) )
			return true;
		return tokenWritesMacForPair( token );
	}

	unsigned int chooseReadyPairPartner( unsigned int primary,
	                                     const VuBasicBlock& block,
	                                     const std::vector<unsigned int>& incoming,
	                                     const std::vector<bool>& emitted,
	                                     const std::vector<unsigned int>& priority,
	                                     const VuLatencyTracker& latencyTracker,
	                                     unsigned int ignoredImplicitWawResources,
	                                     unsigned int currentCycle,
	                                     const VuReadySegmentStrategy& strategy )
	{
		unsigned int best = static_cast<unsigned int>( block.tokens.size() );
		int bestScore = 0;
		const bool primaryIsLower = tokenSchedulesAsLower( *block.tokens[primary],
		                                                   ignoredImplicitWawResources );
		const bool primaryWritesMac = tokenWritesMacForPair( *block.tokens[primary],
		                                                     ignoredImplicitWawResources );
		const int primaryDelay =
		    pairingHazardDelay( latencyTracker, *block.tokens[primary], NULL,
		                        static_cast<int>( currentCycle ), strategy.stallAwarePairing );

		for( unsigned int i = 0; i < block.tokens.size(); ++i )
		{
			if( i == primary || emitted[i] || incoming[i] != 0 )
				continue;
			if( tokenSchedulesAsLower( *block.tokens[i],
			                           ignoredImplicitWawResources ) == primaryIsLower )
				continue;
			if( !vuTokenPairResourcesAreIndependent( *block.tokens[primary],
			                                         *block.tokens[i],
			                                         primaryWritesMac,
			                                         tokenWritesMacForPair( *block.tokens[i],
			                                                               ignoredImplicitWawResources ) ) )
				continue;
			if( pairingHazardDelay( latencyTracker,
			                        *block.tokens[primary],
			                        block.tokens[i],
			                        static_cast<int>( currentCycle ),
			                        strategy.stallAwarePairing ) > primaryDelay )
				continue;

			const int score = readyCandidateScore( i,
			                                       false,
			                                       false,
			                                       block,
			                                       priority,
			                                       latencyTracker,
			                                       currentCycle,
			                                       strategy );
			const bool better = strategy.partnerPrefersWorst ? score > bestScore : score < bestScore;
			if( best == block.tokens.size() || better )
			{
				best = i;
				bestScore = score;
			}
		}

		return best;
	}

	// Why did this primary find nobody to share its row with? chooseReadyPairPartner
	// rejects for four reasons and returns one number, so the reasons are re-derived here
	// and tallied. Six blind attempts at the remaining words all measured byte-identical
	// output; this says which reason to attack.
	void reportPairMiss( unsigned int primary,
	                     const VuBasicBlock& block,
	                     const std::vector<unsigned int>& incoming,
	                     const std::vector<bool>& emitted,
	                     const VuLatencyTracker& latencyTracker,
	                     unsigned int ignoredImplicitWawResources,
	                     unsigned int currentCycle )
	{
		unsigned int notReady = 0, samePipe = 0, resource = 0, latency = 0;
		const bool primaryIsLower = tokenSchedulesAsLower( *block.tokens[primary],
		                                                  ignoredImplicitWawResources );
		const bool primaryWritesMac = tokenWritesMacForPair( *block.tokens[primary],
		                                                     ignoredImplicitWawResources );
		const int primaryDelay =
		    pairingHazardDelay( latencyTracker, *block.tokens[primary], NULL,
		                        static_cast<int>( currentCycle ) );

		for( unsigned int i = 0; i < block.tokens.size(); ++i )
		{
			if( i == primary || emitted[i] )
				continue;
			if( incoming[i] != 0 )
			{
				++notReady;
				continue;
			}
			if( tokenSchedulesAsLower( *block.tokens[i],
			                           ignoredImplicitWawResources ) == primaryIsLower )
			{
				++samePipe;
				continue;
			}
			if( !vuTokenPairResourcesAreIndependent( *block.tokens[primary],
			                                         *block.tokens[i],
			                                         primaryWritesMac,
			                                         tokenWritesMacForPair( *block.tokens[i],
			                                                               ignoredImplicitWawResources ) ) )
			{
				++resource;
				continue;
			}
			if( pairingHazardDelay( latencyTracker, *block.tokens[primary], block.tokens[i],
			                        static_cast<int>( currentCycle ) ) > primaryDelay )
				++latency;
		}

		// `starved` is the pipe that had nothing to offer: the opposite of the primary's
		// own. Which one it is decides where to look - a samePipe miss with an upper
		// primary wants more lower-pipe work ready, and the other way round.
		std::cerr << "[pairmiss] blk" << block.firstTokenIndex << " "
		          << lowerVuTokenName( *block.tokens[primary] )
		          << " primary=" << ( primaryIsLower ? "lower" : "upper" )
		          << " starved=" << ( primaryIsLower ? "upper" : "lower" )
		          << " notReady=" << notReady
		          << " samePipe=" << samePipe
		          << " resource=" << resource
		          << " latency=" << latency << std::endl;
	}

	void markReadyTokenScheduled( unsigned int token,
	                              std::vector<unsigned int>& incoming,
	                              const std::vector< std::vector<unsigned int> >& outgoing,
	                              std::vector<bool>& emitted,
	                              unsigned int& emittedCount )
	{
		emitted[token] = true;
		++emittedCount;

		for( std::vector<unsigned int>::const_iterator i = outgoing[token].begin(); i != outgoing[token].end(); ++i )
		{
			if( incoming[*i] > 0 )
				--incoming[*i];
		}
	}

	VuScheduledIssueSlot makeIssueSlot( const Token* first,
	                                    const Token* second,
	                                    unsigned int issueCycle = 0,
	                                    unsigned int cycleCount = 1,
	                                    unsigned int ignoredImplicitWawResources = VU_RESOURCE_NONE )
	{
		VuScheduledIssueSlot slot;
		slot.firstToken = first;
		slot.secondToken = second;
		slot.padding = first == NULL && second == NULL;
		slot.paddingKind = slot.padding ? VU_SCHEDULED_PADDING_NOP : VU_SCHEDULED_PADDING_NONE;
		slot.issueCycle = issueCycle;
		slot.cycleCount = cycleCount;

		if( first )
		{
			if( tokenSchedulesAsLower( *first, ignoredImplicitWawResources ) )
				slot.lowerToken = first;
			else
				slot.upperToken = first;
		}

		if( second )
		{
			if( tokenSchedulesAsLower( *second, ignoredImplicitWawResources ) )
				slot.lowerToken = second;
			else
				slot.upperToken = second;
		}

		return slot;
	}

	VuScheduledIssueSlot makePaddingIssueSlot( VuScheduledPaddingKind paddingKind,
	                                           unsigned int issueCycle,
	                                           unsigned int cycleCount )
	{
		VuScheduledIssueSlot slot = makeIssueSlot( NULL, NULL, issueCycle, cycleCount );
		slot.paddingKind = paddingKind;
		return slot;
	}

	VuScheduledIssueSlot makeUpperWaitIssueSlot( const Token* upper,
	                                             VuScheduledPaddingKind paddingKind,
	                                             unsigned int issueCycle,
	                                             unsigned int cycleCount,
	                                             unsigned int ignoredImplicitWawResources )
	{
		VuScheduledIssueSlot slot =
			makeIssueSlot( upper, NULL, issueCycle, cycleCount, ignoredImplicitWawResources );
		slot.paddingKind = paddingKind;
		return slot;
	}

	unsigned int waitPaddingCycleCount( VuScheduledPaddingKind paddingKind,
	                                    const VuLatencyTracker& latencyTracker,
	                                    unsigned int currentCycle )
	{
		if( paddingKind == VU_SCHEDULED_PADDING_WAITQ )
		{
			const int readyCycle = latencyTracker.qReadyCycle();
			const unsigned int nextCycle = static_cast<unsigned int>(
				readyCycle > static_cast<int>( currentCycle + 1 )
				? readyCycle
				: static_cast<int>( currentCycle + 1 ) );
			return nextCycle - currentCycle;
		}
		if( paddingKind == VU_SCHEDULED_PADDING_WAITP )
		{
			const int readyCycle = latencyTracker.pReadyCycle();
			const unsigned int nextCycle = static_cast<unsigned int>(
				readyCycle > static_cast<int>( currentCycle + 1 )
				? readyCycle
				: static_cast<int>( currentCycle + 1 ) );
			return nextCycle - currentCycle;
		}
		return 1;
	}

	// Whether a token may share an instruction word with another is decided by
	// vuTokenPairResourcesAreIndependent - a write and a read of the same thing in
	// ONE word is a genuine hazard no interlock can fix. The LATENCY question next
	// to it is different: it asks whether an earlier producer is still in flight,
	// and that is exactly what the FMAC interlock stalls for. Under
	// --fmac-interlock, refusing to pair over it only costs instruction words.
	// Two different questions, and which one the partner filter should ask is the
	// whole of --pair-best-of-cycles.
	//
	//   stallAware = false (what openvcl has always asked): how many nop WORDS
	//     would this pairing cost me? Under --fmac-interlock that is 0 for every
	//     VF operand still in flight, because the hardware stalls by itself.
	//   stallAware = true: how many CYCLES would it cost? A lower-pipe token whose
	//     producer is four rows back holds the WHOLE row when it is paired, so a
	//     primary that was ready this cycle waits with it. Declining costs one
	//     word and never a cycle.
	//
	// The first answer is why openvcl pairs a store onto the row right after the
	// ftoi that feeds it; SCE drains its stores four rows later and pays nothing.
	int pairingHazardDelay( const VuLatencyTracker& latencyTracker,
	                        const Token& token,
	                        const Token* partner,
	                        int currentCycle,
	                        bool stallAware )
	{
		return ( vuFmacInterlockEnabled() && !stallAware )
		           ? latencyTracker.manualReadHazardDelay( token, partner, currentCycle )
		           : latencyTracker.readHazardDelay( token, partner, currentCycle );
	}

	void appendReadHazardPaddingSlots( std::vector<VuScheduledIssueSlot>& slots,
	                                   const Token& token,
	                                   const Token* partner,
	                                   VuLatencyTracker& latencyTracker,
	                                   unsigned int blockStartCycle,
	                                   unsigned int& currentCycle )
	{
		int issueDelay = latencyTracker.readHazardDelay( token,
		                                                 partner,
		                                                 static_cast<int>( currentCycle ) );
		while( issueDelay > 0 )
		{
			const VuScheduledPaddingKind paddingKind =
				vuScheduledPaddingKindForReadHazard( token,
				                                     partner,
				                                     latencyTracker,
				                                     static_cast<int>( currentCycle ) );
			const unsigned int paddingCycleCount =
				waitPaddingCycleCount( paddingKind, latencyTracker, currentCycle );
			// How much of this wait has to become instruction words: the cycles
			// spent on the interlocked FMAC pipeline do not, the hardware stalls
			// for them by itself. The cycle model keeps the full count, so the
			// schedule below is unchanged - only the emitted size shrinks.
			const int manualDelay =
				latencyTracker.manualReadHazardDelay( token,
				                                      partner,
				                                      static_cast<int>( currentCycle ) );
			unsigned int emitCycleCount = paddingCycleCount;
			if( vuFmacInterlockEnabled() && paddingKind == VU_SCHEDULED_PADDING_NOP )
			{
				const unsigned int manual =
					manualDelay > 0 ? static_cast<unsigned int>( manualDelay ) : 0u;
				emitCycleCount = manual < paddingCycleCount ? manual : paddingCycleCount;
			}
			VuScheduledIssueSlot paddingSlot =
				makePaddingIssueSlot( paddingKind,
				                      currentCycle - blockStartCycle,
				                      paddingCycleCount );
			paddingSlot.emitCycleCount = emitCycleCount;
			if( emitCycleCount > 0 )
			slots.push_back( paddingSlot );
			// A cycle that becomes no word is a cycle the FMAC interlock is
			// trusted to supply, and the FDIV pipeline is NOT interlocked: Q and P
			// arrive a fixed number of cycles after the division issues, and the
			// only thing that carries a program from one of those cycles to the
			// next is an instruction word. Letting a suppressed cycle count
			// against qReadyCycle spends a stall twice - once as the FMAC wait it
			// was, and again as part of a division's latency - and the Q reader
			// below then issues with no waitq on a gap the hardware never opened.
			// Pushing the pipelined results out by the suppressed count measures
			// that gap in words, which is what the hardware guarantees.
			latencyTracker.delayPipelinedResultsBy(
				static_cast<int>( paddingCycleCount ) - static_cast<int>( emitCycleCount ) );
			currentCycle += paddingCycleCount;
			issueDelay = latencyTracker.readHazardDelay( token,
			                                             partner,
			                                             static_cast<int>( currentCycle ) );
		}
	}

	void assignIgnoredImplicitWawResources( std::vector<VuScheduledIssueSlot>& slots,
	                                        unsigned int ignoredImplicitWawResources )
	{
		for( std::vector<VuScheduledIssueSlot>::iterator i = slots.begin(); i != slots.end(); ++i )
			i->ignoredImplicitWawResources = ignoredImplicitWawResources;
	}

	unsigned int tokenIndexInScheduledBlock( const VuBasicBlock& block, const Token* token )
	{
		if( !token )
			return VU_SCHEDULED_TOKEN_INDEX_NONE;

		for( unsigned int i = 0; i < block.tokens.size(); ++i )
		{
			if( block.tokens[i] == token )
				return block.firstTokenIndex + i;
		}

		return VU_SCHEDULED_TOKEN_INDEX_NONE;
	}

	void assignScheduledIssueSlotTokenIndices( const VuBasicBlock& block,
	                                           std::vector<VuScheduledIssueSlot>& slots )
	{
		for( std::vector<VuScheduledIssueSlot>::iterator slot = slots.begin(); slot != slots.end(); ++slot )
		{
			slot->firstTokenIndex = tokenIndexInScheduledBlock( block, slot->firstToken );
			slot->secondTokenIndex = tokenIndexInScheduledBlock( block, slot->secondToken );
			slot->upperTokenIndex = tokenIndexInScheduledBlock( block, slot->upperToken );
			slot->lowerTokenIndex = tokenIndexInScheduledBlock( block, slot->lowerToken );
		}
	}

	bool tokenCanEnterBarrierTailPair( const Token& token )
	{
		if( !token.operand() )
			return false;
		if( token.flags() & Token::IGNORED )
			return false;
		if( token.flags() & Token::BRANCH_DELAY_FILLER )
			return false;
		if( token.flags() & Token::PREORDERED )
			return false;
		if( token.operand()->flags() & Operand::PREPROCESSOR )
			return false;
		if( token.operand()->flags() & Operand::FILTERED )
			return false;
		if( token.operand()->unit() == Operand::ENTER || token.operand()->unit() == Operand::EXIT )
			return false;
		if( !token.operand()->isUpperExecutionPath() && !token.operand()->isLowerExecutionPath() )
			return false;
		return true;
	}

	bool tokenCanPairWithBarrierTail( const Token& previous,
	                                  const Token& barrier,
	                                  unsigned int ignoredImplicitWawResources )
	{
		if( !tokenCanEnterBarrierTailPair( previous ) || !tokenCanEnterBarrierTailPair( barrier ) )
			return false;
		if( barrier.label().length() != 0 )
			return false;
		if( !isVuXgkick( barrier ) && vuTokenBranchDelaySlots( barrier ) == 0 )
			return false;
		if( tokenSchedulesAsLower( previous, ignoredImplicitWawResources )
		    == tokenSchedulesAsLower( barrier, ignoredImplicitWawResources ) )
			return false;

		VuTokenResourceAccess previousAccess;
		VuTokenResourceAccess barrierAccess;
		if( !buildVuTokenResourceAccess( previous, previousAccess )
		    || !buildVuTokenResourceAccess( barrier, barrierAccess ) )
			return false;
		if( previousAccess.branchDelaySlots > 0 )
			return false;
		if( barrierAccess.branchDelaySlots > 0
		    && (barrierAccess.instructionFlags & (VU_INSTR_LINK_BRANCH | VU_INSTR_REGISTER_BRANCH)) )
			return false;
		if( (previousAccess.instructionFlags & (VU_INSTR_WAIT_Q | VU_INSTR_WAIT_P))
		    || (barrierAccess.instructionFlags & (VU_INSTR_WAIT_Q | VU_INSTR_WAIT_P)) )
			return false;

		const unsigned int previousReadsImplicit = previousAccess.implicitReads;
		const unsigned int previousWritesImplicit = previousAccess.implicitWrites;
		const unsigned int barrierReadsImplicit = barrierAccess.implicitReads;
		const unsigned int barrierWritesImplicit = barrierAccess.implicitWrites;
		if( previousWritesImplicit & (barrierReadsImplicit | barrierWritesImplicit) )
			return false;
		if( barrierWritesImplicit & (previousReadsImplicit | previousWritesImplicit) )
			return false;

		if( vuTokensHaveDataDependency( previous, barrier )
		    || vuTokensHaveDataDependency( barrier, previous ) )
			return false;

		return true;
	}

	bool tokenCanPairWithFlagReaderTail( const Token& previous,
	                                     const Token& tail,
	                                     unsigned int ignoredImplicitWawResources )
	{
		if( !tokenCanEnterBarrierTailPair( previous ) || !tokenCanEnterBarrierTailPair( tail ) )
			return false;
		if( tail.label().length() != 0 )
			return false;
		if( tokenSchedulesAsLower( previous, ignoredImplicitWawResources )
		    == tokenSchedulesAsLower( tail, ignoredImplicitWawResources ) )
			return false;

		VuTokenResourceAccess previousAccess;
		VuTokenResourceAccess tailAccess;
		if( !buildVuTokenResourceAccess( previous, previousAccess )
		    || !buildVuTokenResourceAccess( tail, tailAccess ) )
			return false;
		if( (tailAccess.implicitReads & (VU_RESOURCE_MAC | VU_RESOURCE_CLIP)) == 0 )
			return false;
		if( previousAccess.memoryKind != VU_MEMORY_NONE || tailAccess.memoryKind != VU_MEMORY_NONE )
			return false;
		if( previousAccess.branchDelaySlots > 0 || tailAccess.branchDelaySlots > 0 )
			return false;
		if( (previousAccess.instructionFlags & (VU_INSTR_WAIT_Q | VU_INSTR_WAIT_P))
		    || (tailAccess.instructionFlags & (VU_INSTR_WAIT_Q | VU_INSTR_WAIT_P)) )
			return false;

		const unsigned int previousReadsImplicit = previousAccess.implicitReads;
		const unsigned int previousWritesImplicit = previousAccess.implicitWrites;
		const unsigned int tailReadsImplicit = tailAccess.implicitReads;
		const unsigned int tailWritesImplicit = tailAccess.implicitWrites;
		if( previousWritesImplicit & (tailReadsImplicit | tailWritesImplicit) )
			return false;
		if( tailWritesImplicit & (previousReadsImplicit | previousWritesImplicit) )
			return false;

		if( vuTokensHaveDataDependency( previous, tail )
		    || vuTokensHaveDataDependency( tail, previous ) )
			return false;

		return true;
	}

	bool tokenCanPairWithPreviousTail( const Token& previous,
	                                   const Token& tail,
	                                   unsigned int ignoredImplicitWawResources )
	{
		return tokenCanPairWithBarrierTail( previous, tail, ignoredImplicitWawResources )
		    || tokenCanPairWithFlagReaderTail( previous, tail, ignoredImplicitWawResources );
	}

	bool tryPairTailWithPreviousSlot( std::vector<VuScheduledIssueSlot>& slots,
	                                  const Token& tail,
	                                  unsigned int ignoredImplicitWawResources )
	{
		if( slots.empty() )
			return false;

		VuScheduledIssueSlot& previousSlot = slots.back();
		if( previousSlot.padding || previousSlot.cycleCount != 1 )
			return false;
		if( !previousSlot.firstToken || previousSlot.secondToken )
			return false;
		if( !tokenCanPairWithPreviousTail( *previousSlot.firstToken,
		                                   tail,
		                                   ignoredImplicitWawResources ) )
			return false;

		previousSlot.secondToken = &tail;
		if( tokenSchedulesAsLower( tail, ignoredImplicitWawResources ) )
			previousSlot.lowerToken = &tail;
		else
			previousSlot.upperToken = &tail;
		return true;
	}

	bool selectUpperWaitFiller( const VuBasicBlock& block,
	                            const std::vector<unsigned int>& incoming,
	                            const std::vector<bool>& emitted,
	                            const std::vector<unsigned int>& priority,
	                            const VuLatencyTracker& latencyTracker,
	                            unsigned int ignoredImplicitWawResources,
	                            unsigned int currentCycle,
	                            const VuReadySegmentStrategy& strategy,
	                            unsigned int& waitToken,
	                            unsigned int& upperToken,
	                            VuScheduledPaddingKind& paddingKind )
	{
		waitToken = static_cast<unsigned int>( block.tokens.size() );
		upperToken = static_cast<unsigned int>( block.tokens.size() );
		paddingKind = VU_SCHEDULED_PADDING_NONE;
		int bestScore = 0;

		for( unsigned int wait = 0; wait < block.tokens.size(); ++wait )
		{
			if( emitted[wait] || incoming[wait] != 0 )
				continue;
			if( !tokenSchedulesAsLower( *block.tokens[wait], ignoredImplicitWawResources ) )
				continue;
			if( latencyTracker.readHazardDelay( *block.tokens[wait],
			                                    NULL,
			                                    static_cast<int>( currentCycle ) ) <= 0 )
				continue;

			const VuScheduledPaddingKind waitKind =
				vuScheduledPaddingKindForReadHazard( *block.tokens[wait],
				                                     NULL,
				                                     latencyTracker,
				                                     static_cast<int>( currentCycle ) );
			if( waitKind != VU_SCHEDULED_PADDING_WAITQ
			    && waitKind != VU_SCHEDULED_PADDING_WAITP )
				continue;

			for( unsigned int upper = 0; upper < block.tokens.size(); ++upper )
			{
				if( upper == wait || emitted[upper] || incoming[upper] != 0 )
					continue;
				if( !tokenSchedulesAsUpper( *block.tokens[upper], ignoredImplicitWawResources ) )
					continue;
				if( latencyTracker.readHazardDelay( *block.tokens[upper],
				                                    NULL,
				                                    static_cast<int>( currentCycle ) ) > 0 )
					continue;

				const int score = readyCandidateScore( upper,
				                                       false,
				                                       false,
				                                       block,
				                                       priority,
				                                       latencyTracker,
				                                       currentCycle,
				                                       strategy );
				if( upperToken == block.tokens.size() || score < bestScore )
				{
					waitToken = wait;
					upperToken = upper;
					paddingKind = waitKind;
					bestScore = score;
				}
			}
		}

		return upperToken < block.tokens.size();
	}

	std::vector<VuScheduledIssueSlot> scheduleReadySegmentIssueSlots( const std::vector<const Token*>& segment,
	                                                                  unsigned int ignoredImplicitWawResources,
	                                                                  VuLatencyTracker& latencyTracker,
	                                                                  unsigned int blockStartCycle,
	                                                                  unsigned int& currentCycle,
	                                                                  const VuReadySegmentStrategy& strategy =
	                                                                      VuReadySegmentStrategy() )
	{
		std::vector<VuScheduledIssueSlot> slots;
		if( segment.size() < 2 )
		{
			for( std::vector<const Token*>::const_iterator i = segment.begin(); i != segment.end(); ++i )
			{
				appendReadHazardPaddingSlots( slots,
				                              **i,
				                              NULL,
				                              latencyTracker,
				                              blockStartCycle,
				                              currentCycle );
				slots.push_back( makeIssueSlot( *i,
				                                NULL,
				                                currentCycle - blockStartCycle,
				                                1,
				                                ignoredImplicitWawResources ) );
				latencyTracker.recordWrites( **i, static_cast<int>( currentCycle ) );
				++currentCycle;
			}
			assignIgnoredImplicitWawResources( slots, ignoredImplicitWawResources );
			return slots;
		}

		VuBasicBlock block;
		block.tokens = segment;

		std::vector<VuDependencyEdge> edges = buildVuDependencyGraph( block, ignoredImplicitWawResources );
		std::vector<unsigned int> incoming( block.tokens.size(), 0 );
		std::vector< std::vector<unsigned int> > outgoing( block.tokens.size() );

		for( std::vector<VuDependencyEdge>::const_iterator i = edges.begin(); i != edges.end(); ++i )
		{
			if( i->before >= block.tokens.size() || i->after >= block.tokens.size() )
				continue;
			++incoming[i->after];
			outgoing[i->before].push_back( i->after );
		}

		const std::vector<unsigned int> priority = buildDependencyPriorities( block, outgoing );
		std::vector<bool> emitted( block.tokens.size(), false );
		unsigned int emittedCount = 0;
		bool haveLastPipe = false;
		bool lastWasLower = false;

		while( emittedCount < block.tokens.size() )
		{
			unsigned int waitToken = static_cast<unsigned int>( block.tokens.size() );
			unsigned int waitUpper = static_cast<unsigned int>( block.tokens.size() );
			VuScheduledPaddingKind waitPaddingKind = VU_SCHEDULED_PADDING_NONE;
			if( selectUpperWaitFiller( block,
			                           incoming,
			                           emitted,
			                           priority,
			                           latencyTracker,
			                           ignoredImplicitWawResources,
			                           currentCycle,
			                           strategy,
			                           waitToken,
			                           waitUpper,
			                           waitPaddingKind ) )
			{
				(void)waitToken;
				const unsigned int waitCycles =
					waitPaddingCycleCount( waitPaddingKind, latencyTracker, currentCycle );
				slots.push_back( makeUpperWaitIssueSlot( block.tokens[waitUpper],
				                                         waitPaddingKind,
				                                         currentCycle - blockStartCycle,
				                                         waitCycles + 1,
				                                         ignoredImplicitWawResources ) );
				const unsigned int issueCycle = currentCycle + waitCycles;
				markReadyTokenScheduled( waitUpper, incoming, outgoing, emitted, emittedCount );
				latencyTracker.recordWrites( *block.tokens[waitUpper], static_cast<int>( issueCycle ) );
				currentCycle = issueCycle + 1;
				haveLastPipe = false;
				continue;
			}

			unsigned int best = static_cast<unsigned int>( block.tokens.size() );
			int bestScore = 0;

			for( unsigned int i = 0; i < block.tokens.size(); ++i )
			{
				if( emitted[i] || incoming[i] != 0 )
					continue;

				const int score = readyCandidateScore( i,
				                                       haveLastPipe,
				                                       lastWasLower,
				                                       block,
				                                       priority,
				                                       latencyTracker,
				                                       currentCycle,
				                                       strategy );
				if( best == block.tokens.size() || score < bestScore )
				{
					best = i;
					bestScore = score;
				}
			}

			if( best == block.tokens.size() )
			{
				for( unsigned int i = 0; i < block.tokens.size(); ++i )
				{
					if( !emitted[i] )
						slots.push_back( makeIssueSlot( block.tokens[i],
						                                NULL,
						                                0,
						                                1,
						                                ignoredImplicitWawResources ) );
				}
				return slots;
			}

			const unsigned int partner = chooseReadyPairPartner( best,
			                                                     block,
			                                                     incoming,
			                                                     emitted,
			                                                     priority,
			                                                     latencyTracker,
			                                                     ignoredImplicitWawResources,
			                                                     currentCycle,
			                                                     strategy );
			if( partner >= block.tokens.size() && vuShowPairMissesEnabled() )
				reportPairMiss( best, block, incoming, emitted, latencyTracker,
				                ignoredImplicitWawResources, currentCycle );

			// This row is going out half empty whatever fills it, so which instruction
			// fills it is free - and can be spent on the NEXT row: prefer one that
			// unblocks an instruction of the other pipe, since that is what makes the
			// following row a pair. --show-pair-misses is what pointed here: `notReady`
			// is the dominant reason a partner was unavailable (132 of 170 rows in
			// stapip_clip_c) and resource conflicts are 0.
			//
			// Whether it pays depends on the segment, so the caller tries both and keeps
			// the shorter schedule rather than anyone guessing a scope.
			if( strategy.preferUnblockingWhenUnpaired && partner >= block.tokens.size() )
			{
				unsigned int rechosen = best;
				int rechosenScore = 0;
				bool have = false;
				for( unsigned int i = 0; i < block.tokens.size(); ++i )
				{
					if( emitted[i] || incoming[i] != 0 )
						continue;
					if( isVuLowerPipe( *block.tokens[i] ) != isVuLowerPipe( *block.tokens[best] ) )
						continue;
					bool unblocks = false;
					for( std::vector<unsigned int>::const_iterator edge = outgoing[i].begin();
					     !unblocks && edge != outgoing[i].end(); ++edge )
					{
						if( *edge >= block.tokens.size() || emitted[*edge] )
							continue;
						if( incoming[*edge] != 1 )
							continue;      // not its last blocker
						if( isVuLowerPipe( *block.tokens[i] ) != isVuLowerPipe( *block.tokens[*edge] ) )
							unblocks = true;
					}
					if( !unblocks )
						continue;
					const int score = readyCandidateScore( i, haveLastPipe, lastWasLower,
					                                       block, priority, latencyTracker,
					                                       currentCycle, strategy );
					if( !have || score < rechosenScore )
					{
						have = true;
						rechosen = i;
						rechosenScore = score;
					}
				}
				best = rechosen;
			}

			const Token* partnerToken = partner < block.tokens.size() ? block.tokens[partner] : NULL;
			const int bestDelay =
				latencyTracker.readHazardDelay( *block.tokens[best],
				                                NULL,
				                                static_cast<int>( currentCycle ) );
			const VuScheduledPaddingKind bestPaddingKind =
				vuScheduledPaddingKindForReadHazard( *block.tokens[best],
				                                     NULL,
				                                     latencyTracker,
				                                     static_cast<int>( currentCycle ) );
			if( bestDelay > 0
			    && tokenSchedulesAsUpper( *block.tokens[best], ignoredImplicitWawResources )
			    && (bestPaddingKind == VU_SCHEDULED_PADDING_WAITQ
			        || bestPaddingKind == VU_SCHEDULED_PADDING_WAITP) )
			{
				const unsigned int waitCycles =
					waitPaddingCycleCount( bestPaddingKind, latencyTracker, currentCycle );
				slots.push_back( makeUpperWaitIssueSlot( block.tokens[best],
				                                         bestPaddingKind,
				                                         currentCycle - blockStartCycle,
				                                         waitCycles + 1,
				                                         ignoredImplicitWawResources ) );
				const unsigned int issueCycle = currentCycle + waitCycles;
				markReadyTokenScheduled( best, incoming, outgoing, emitted, emittedCount );
				latencyTracker.recordWrites( *block.tokens[best], static_cast<int>( issueCycle ) );
				currentCycle = issueCycle + 1;
				haveLastPipe = false;
				continue;
			}

			appendReadHazardPaddingSlots( slots,
			                              *block.tokens[best],
			                              partnerToken,
			                              latencyTracker,
			                              blockStartCycle,
			                              currentCycle );
			slots.push_back( makeIssueSlot( block.tokens[best],
			                                partnerToken,
			                                currentCycle - blockStartCycle,
			                                1,
			                                ignoredImplicitWawResources ) );

			markReadyTokenScheduled( best, incoming, outgoing, emitted, emittedCount );
			latencyTracker.recordWrites( *block.tokens[best], static_cast<int>( currentCycle ) );
			if( partner < block.tokens.size() )
			{
				markReadyTokenScheduled( partner, incoming, outgoing, emitted, emittedCount );
				latencyTracker.recordWrites( *block.tokens[partner], static_cast<int>( currentCycle ) );
				haveLastPipe = false;
			}
			else
			{
				haveLastPipe = true;
				lastWasLower = tokenSchedulesAsLower( *block.tokens[best],
				                                      ignoredImplicitWawResources );
			}
			++currentCycle;
		}

		assignIgnoredImplicitWawResources( slots, ignoredImplicitWawResources );
		return slots;
	}

	void appendIssueSlotsFlat( const std::vector<VuScheduledIssueSlot>& slots,
	                           std::list<Token>& scheduled )
	{
		for( std::vector<VuScheduledIssueSlot>::const_iterator i = slots.begin(); i != slots.end(); ++i )
		{
			if( i->firstToken )
			{
				Token token( *i->firstToken );
				token.setFlags( token.flags() & ~(Token::SCHEDULED_PAIR_FIRST | Token::SCHEDULED_PAIR_SECOND) );
				if( i->secondToken )
					token.setFlags( token.flags() | Token::SCHEDULED_PAIR_FIRST );
				scheduled.push_back( token );
			}
			if( i->secondToken )
			{
				Token token( *i->secondToken );
				token.setFlags( token.flags() & ~(Token::SCHEDULED_PAIR_FIRST | Token::SCHEDULED_PAIR_SECOND) );
				token.setFlags( token.flags() | Token::SCHEDULED_PAIR_SECOND );
				scheduled.push_back( token );
			}
		}
	}

	struct VuFlagLiveness
	{
		VuFlagLiveness()
		{
			lastMacReader = -1;
			lastClipReader = -1;
			lastStatusReader = -1;
			declaredLiveResources = VU_RESOURCE_NONE;
		}

		int lastMacReader;
		int lastClipReader;
		int lastStatusReader;
		unsigned int declaredLiveResources;
	};

	bool tokenReadsMac( const Token& token )
	{
		return token.operand() && isVuMacReader( token.operand()->name() );
	}

	bool tokenReadsClip( const Token& token )
	{
		return token.operand() && isVuClipReader( token.operand()->name() );
	}

	bool tokenReadsStatus( const Token& token )
	{
		return token.operand() && isVuStatusReader( token.operand()->name() );
	}

	unsigned int ignoredFlagWawMaskForIndex( unsigned int index,
	                                         int lastMacReader,
	                                         int lastClipReader,
	                                         int lastStatusReader,
	                                         unsigned int declaredLiveResources )
	{
		unsigned int mask = VU_RESOURCE_NONE;
		if( lastMacReader < 0 || index > static_cast<unsigned int>( lastMacReader ) )
			mask |= VU_RESOURCE_MAC;
		if( lastClipReader < 0 || index > static_cast<unsigned int>( lastClipReader ) )
			mask |= VU_RESOURCE_CLIP;
		if( lastStatusReader < 0 || index > static_cast<unsigned int>( lastStatusReader ) )
			mask |= VU_RESOURCE_STATUS;
		// A resource the program DECLARES as its contract with whatever runs next is
		// never dead, whatever the readers say. The dead-write pass has honoured
		// out_hw_clip since it was written; this is the half that did not, which is
		// how a `clipw` the dead-write pass correctly kept could still be permuted
		// out of the window position its successor reads it in.
		return mask & ~declaredLiveResources;
	}

	// A writer past the last reader is dead ONLY IF CONTROL NEVER COMES BACK.
	//
	// "Last reader" is a position in the token list, and the token list is
	// linear while the program is not. A `clipw` in a loop's last block sits at
	// a higher index than the `fceq` at the loop's head, so it read as dead -
	// and CLIP is a SHIFT REGISTER, so dropping its writer chain let the
	// scheduler exchange two `clipw` and hand the next iteration's `fceq` a
	// window with two entries transposed. The dead-write pass never removed
	// them; nothing was missing from the output except the order, which is why
	// four rounds of value oracles walked past it and only a branch-condition
	// comparison caught it.
	//
	// So each back edge extends its resources' liveness to the end of its own
	// loop: a reader anywhere inside [target, branch] keeps every writer in
	// that range live, because the next iteration reaches it. Run to a fixpoint
	// for nested and overlapping loops - an outer back edge can be pulled past
	// an inner one's end. A program with no backward branch is unaffected, and
	// so is a loop whose reader already sits last.
	bool branchTargetLabel( const Token& token, std::string& label );

	void extendFlagLivenessAcrossBackEdges( const std::list<Token>& tokens,
	                                        VuFlagLiveness& liveness )
	{
		std::vector<const Token*> indexed;
		std::map<std::string, unsigned int> labels;
		unsigned int index = 0;
		for( std::list<Token>::const_iterator i = tokens.begin(); i != tokens.end(); ++i, ++index )
		{
			indexed.push_back( &*i );
			if( (*i).label().length() != 0 )
				labels[(*i).label()] = index;
		}

		std::vector<std::pair<unsigned int, unsigned int> > backEdges;   // (target, branch)
		for( unsigned int b = 0; b < indexed.size(); ++b )
		{
			std::string label;
			if( !branchTargetLabel( *indexed[b], label ) )
				continue;
			std::map<std::string, unsigned int>::const_iterator t = labels.find( label );
			if( t == labels.end() || t->second > b )
				continue;
			backEdges.push_back( std::make_pair( t->second, b ) );
		}
		if( backEdges.empty() )
			return;

		bool changed = true;
		for( int guard = 0; changed && guard < 64; ++guard )
		{
			changed = false;
			for( unsigned int e = 0; e < backEdges.size(); ++e )
			{
				const int lo = static_cast<int>( backEdges[e].first );
				const int hi = static_cast<int>( backEdges[e].second );
				if( liveness.lastMacReader >= lo && liveness.lastMacReader < hi )
				{
					liveness.lastMacReader = hi;
					changed = true;
				}
				if( liveness.lastClipReader >= lo && liveness.lastClipReader < hi )
				{
					liveness.lastClipReader = hi;
					changed = true;
				}
				if( liveness.lastStatusReader >= lo && liveness.lastStatusReader < hi )
				{
					liveness.lastStatusReader = hi;
					changed = true;
				}
			}
		}
	}

	VuFlagLiveness analyzeFlagLiveness( const std::list<Token>& tokens )
	{
		VuFlagLiveness liveness;
		unsigned int index = 0;
		for( std::list<Token>::const_iterator i = tokens.begin(); i != tokens.end(); ++i, ++index )
		{
			if( tokenReadsMac( *i ) )
				liveness.lastMacReader = static_cast<int>( index );
			if( tokenReadsClip( *i ) )
				liveness.lastClipReader = static_cast<int>( index );
			if( tokenReadsStatus( *i ) )
				liveness.lastStatusReader = static_cast<int>( index );
			liveness.declaredLiveResources |= vuDeclaredHardwareResource( *i );
		}
		extendFlagLivenessAcrossBackEdges( tokens, liveness );
		return liveness;
	}

	// What this schedule will actually cost in micro memory. --pair-best-of-two
	// compared std::vector::size(), which counts a multi-cycle NOP padding slot as
	// one; the code generator writes min(emitCycleCount,cycleCount) words for it and
	// one word for every other slot, waitq/waitp included - emitUpperWithWait folds
	// the wait into the same row.
	unsigned int issueSlotEmittedWordCount( const std::vector<VuScheduledIssueSlot>& slots )
	{
		unsigned int words = 0;
		for( std::vector<VuScheduledIssueSlot>::const_iterator i = slots.begin(); i != slots.end(); ++i )
		{
			if( !i->padding )
			{
				++words;
				continue;
			}
			const unsigned int cycles = i->cycleCount > 0 ? i->cycleCount : 1;
			words += i->emitCycleCount < cycles ? i->emitCycleCount : cycles;
		}
		return words;
	}

	// The strategies --pair-best-of-many tries. Order is not significant except that
	// ties keep the earliest, so index 0 must be the plain heuristic and index 1 the
	// one --pair-best-of-two already had: with an empty win the output then matches
	// --pair-best-of-two rather than drifting for no gain.
	const std::vector<VuReadySegmentStrategy>& readySegmentStrategies()
	{
		static std::vector<VuReadySegmentStrategy> strategies;
		if( !strategies.empty() )
			return strategies;

		// These five are what survived a 26-entry sweep of the four knobs over the ten
		// resident programs: every other point in it lost or tied on every segment, so
		// carrying it would only cost compile time. Trimming to the winners is exact,
		// not an approximation - the minimum over a subset that still contains every
		// segment's argmin is the same minimum.
		VuReadySegmentStrategy plain;
		strategies.push_back( plain );

		VuReadySegmentStrategy unblocking;
		unblocking.preferUnblockingWhenUnpaired = true;
		strategies.push_back( unblocking );

		// Critical-path height at 1.5x and 3x source order. Both directions were swept
		// (6, 10, 40, 100, 200, 400); only these two ever come out ahead.
		VuReadySegmentStrategy weighted;
		weighted.priorityWeight = 30;
		strategies.push_back( weighted );

		VuReadySegmentStrategy weightedUnblocking;
		weightedUnblocking.priorityWeight = 60;
		weightedUnblocking.preferUnblockingWhenUnpaired = true;
		strategies.push_back( weightedUnblocking );

		// Stop hoisting divides and rsqrts ahead of everything else. Worth two words on
		// stapip_cull_td, where starting the divide early strands the rest of the row.
		VuReadySegmentStrategy noLongLatency;
		noLongLatency.longLatencyProducerBonus = 0;
		strategies.push_back( noLongLatency );

		// The other direction was swept too, and is NOT here: the bonus at 1500, alone
		// and with priorityWeight 30, never wins. Both were added to this list, built,
		// and measured - the 70 real microprograms came out BYTE-IDENTICAL
		// (e78d466912af036dfea8d0a9f3b8385c) and the five programs where openvcl still
		// pays a `waitq` did not move a word. A best-of option that never wins is pure
		// compile time, so they were removed and the result recorded here instead. What
		// remains of that gap is a latency question, not a scheduling one.

		// Spend the row's free slot on the LEAST valuable legal partner and leave the
		// valuable one to be a primary. Worth three words on stapip_cull_tce and one
		// each on cull_c and cull_tc - the programs where the ready set is a long run
		// of one pipe and a partner, once spent, is not there for the next row.
		VuReadySegmentStrategy cheapPartner;
		cheapPartner.partnerPrefersWorst = true;
		strategies.push_back( cheapPartner );

		VuReadySegmentStrategy cheapPartnerUnblocking = cheapPartner;
		cheapPartnerUnblocking.preferUnblockingWhenUnpaired = true;
		strategies.push_back( cheapPartnerUnblocking );

		return strategies;
	}

	// Every strategy scheduled on a copy of the latency tracker, then the shortest
	// re-run for real. A variant can only ever be discarded, so adding one cannot
	// lengthen the output - it costs compile time and nothing else.
	void appendBestOfManyReadySegmentSlots( const std::vector<const Token*>& segment,
	                                       std::vector<VuScheduledIssueSlot>& slots,
	                                       unsigned int ignoredImplicitWawResources,
	                                       VuLatencyTracker& latencyTracker,
	                                       unsigned int blockStartCycle,
	                                       unsigned int& currentCycle )
	{
		const std::vector<VuReadySegmentStrategy>& strategies = readySegmentStrategies();
		// --pair-best-of-cycles mirrors every entry of the table with its partner
		// filter asking the cycle question instead of the word one. The arms are
		// EXTRA, never a replacement: the word winner below is still whatever the
		// shipped table produced, so the flag can only ever trade a word it is
		// allowed to spend for a cycle it measures.
		std::vector<VuReadySegmentStrategy> mirrored;
		if( vuPairBestOfCyclesEnabled() )
		{
			for( unsigned int i = 0; i < strategies.size(); ++i )
			{
				VuReadySegmentStrategy stallAware = strategies[i];
				stallAware.stallAwarePairing = true;
				mirrored.push_back( stallAware );
			}
		}
		const unsigned int trialCount =
			static_cast<unsigned int>( strategies.size() + mirrored.size() );

		unsigned int bestIndex = 0;
		unsigned int bestWords = 0;
		unsigned int plainWords = 0;
		// The cycle winner is tracked separately: only a schedule that is no LONGER
		// than the word winner may take the segment on cycles, so the emitted size
		// is bounded by what ships today whatever the cycle model says.
		unsigned int cycleIndex = 0;
		unsigned int cycleCycles = 0;
		unsigned int cycleWords = 0;
		unsigned int bestIndexCycles = 0;

		for( unsigned int i = 0; i < trialCount; ++i )
		{
			const VuReadySegmentStrategy& strategy =
				i < strategies.size() ? strategies[i] : mirrored[i - strategies.size()];
			VuLatencyTracker trialTracker = latencyTracker;
			unsigned int trialCycle = currentCycle;
			const std::vector<VuScheduledIssueSlot> trial =
				scheduleReadySegmentIssueSlots( segment,
				                                ignoredImplicitWawResources,
				                                trialTracker,
				                                blockStartCycle,
				                                trialCycle,
				                                strategy );
			const unsigned int words = issueSlotEmittedWordCount( trial );
			const unsigned int cycles = trialCycle - currentCycle;
			if( i == 0 )
				plainWords = words;
			// The word winner is decided over the SHIPPED table only, so with the
			// flag on the fallback is bit-for-bit the schedule chosen without it.
			if( i < strategies.size() && ( i == 0 || words < bestWords ) )
			{
				bestWords = words;
				bestIndex = i;
				bestIndexCycles = cycles;
			}
			if( i == 0 || cycles < cycleCycles
			    || ( cycles == cycleCycles && words < cycleWords ) )
			{
				cycleCycles = cycles;
				cycleWords = words;
				cycleIndex = i;
			}
		}

		if( vuPairBestOfCyclesEnabled() && cycleCycles < bestIndexCycles )
		{
			const unsigned int saved = bestIndexCycles - cycleCycles;
			const unsigned int extra = cycleWords > bestWords ? cycleWords - bestWords : 0;
			if( saved >= extra * VU_PAIR_CYCLES_PER_WORD )
			{
				bestIndex = cycleIndex;
				bestWords = cycleWords;
			}
		}

		// Attribution: which entry in the table earned this segment, and by how much
		// against the plain heuristic. A table entry that never appears here is dead
		// weight - it only costs compile time.
		if( vuShowPairMissesEnabled() && bestIndex != 0 )
			std::cerr << "[pairwin] strategy=" << bestIndex
			          << " words=" << bestWords
			          << " plain=" << plainWords
			          << " saved=" << ( plainWords - bestWords ) << std::endl;

		const VuReadySegmentStrategy& winner =
			bestIndex < strategies.size() ? strategies[bestIndex]
			                             : mirrored[bestIndex - strategies.size()];
		std::vector<VuScheduledIssueSlot> segmentSlots =
			scheduleReadySegmentIssueSlots( segment,
			                                ignoredImplicitWawResources,
			                                latencyTracker,
			                                blockStartCycle,
			                                currentCycle,
			                                winner );
		slots.insert( slots.end(), segmentSlots.begin(), segmentSlots.end() );
	}

	void appendReadyScheduledSegmentSlots( const std::vector<const Token*>& segment,
	                                       std::vector<VuScheduledIssueSlot>& slots,
	                                       unsigned int ignoredImplicitWawResources,
	                                       VuLatencyTracker& latencyTracker,
	                                       unsigned int blockStartCycle,
	                                       unsigned int& currentCycle )
	{
		if( vuPairBestOfManyEnabled() )
		{
			appendBestOfManyReadySegmentSlots( segment,
			                                  slots,
			                                  ignoredImplicitWawResources,
			                                  latencyTracker,
			                                  blockStartCycle,
			                                  currentCycle );
			return;
		}

		if( !vuPairBestOfTwoEnabled() )
		{
			std::vector<VuScheduledIssueSlot> only =
				scheduleReadySegmentIssueSlots( segment,
				                                ignoredImplicitWawResources,
				                                latencyTracker,
				                                blockStartCycle,
				                                currentCycle );
			slots.insert( slots.end(), only.begin(), only.end() );
			return;
		}

		// Two trial schedules on copies, then the winner for real. Fewer issue slots is
		// fewer emitted rows, and the two strategies differ only in what fills a row that
		// was going out half empty either way.
		VuReadySegmentStrategy plainStrategy;
		VuLatencyTracker plainTracker = latencyTracker;
		unsigned int plainCycle = currentCycle;
		const std::vector<VuScheduledIssueSlot> plain =
			scheduleReadySegmentIssueSlots( segment,
			                                ignoredImplicitWawResources,
			                                plainTracker,
			                                blockStartCycle,
			                                plainCycle,
			                                plainStrategy );

		VuReadySegmentStrategy unblockStrategy;
		unblockStrategy.preferUnblockingWhenUnpaired = true;
		VuLatencyTracker unblockTracker = latencyTracker;
		unsigned int unblockCycle = currentCycle;
		const std::vector<VuScheduledIssueSlot> unblocking =
			scheduleReadySegmentIssueSlots( segment,
			                                ignoredImplicitWawResources,
			                                unblockTracker,
			                                blockStartCycle,
			                                unblockCycle,
			                                unblockStrategy );

		VuReadySegmentStrategy winner;
		winner.preferUnblockingWhenUnpaired = unblocking.size() < plain.size();
		std::vector<VuScheduledIssueSlot> segmentSlots =
			scheduleReadySegmentIssueSlots( segment,
			                                ignoredImplicitWawResources,
			                                latencyTracker,
			                                blockStartCycle,
			                                currentCycle,
			                                winner );
		slots.insert( slots.end(), segmentSlots.begin(), segmentSlots.end() );
	}

	std::vector<VuScheduledIssueSlot> scheduleVuBasicBlockReadyIssueSlotsWithFlagLiveness(
	    const VuBasicBlock& block,
	    const VuFlagLiveness& liveness,
	    VuLatencyTracker& latencyTracker,
	    unsigned int blockStartCycle )
	{
		std::vector<VuScheduledIssueSlot> slots;
		std::vector<const Token*> segment;
		unsigned int segmentMask = VU_RESOURCE_NONE;
		unsigned int segmentLastIndex = block.firstTokenIndex;
		bool haveSegment = false;
		unsigned int currentCycle = blockStartCycle;

		for( unsigned int offset = 0; offset < block.tokens.size(); ++offset )
		{
			const unsigned int tokenIndex = block.firstTokenIndex + offset;
			const Token& token = *block.tokens[offset];
			if( isVuReadyScheduleCandidate( token ) )
			{
				// The ignorable-flag-WAW mask answers ONE question:
				// is this flag read after the token range the dependency graph is
				// about to be built over? Inside that range
				// addPreciseImplicitFlagDependencies already finds every reader
				// itself and orders the writers against it; the mask only decides
				// whether the writers TRAILING the last reader have to keep their
				// order (a live-out flag) or may be reordered freely.
				//
				// So it belongs to the end of the segment, not to each token in
				// it. Deriving it per token forced a segment break at every change
				// and again at the last MAC/CLIP reader - several breaks per
				// vertex - and the scheduler can only hide a latency with work
				// that is inside its current segment, which is where the nop
				// padding came from. Taking the mask from the segment's LAST token
				// keeps exactly the same answer for the trailing writers (an
				// index equal to the last reader stays conservative) while letting
				// one segment span everything between two real barriers.
				segmentLastIndex = tokenIndex;
				haveSegment = true;
				segment.push_back( &token );
				continue;
			}

			segmentMask = ignoredFlagWawMaskForIndex( segmentLastIndex,
			                                          liveness.lastMacReader,
			                                          liveness.lastClipReader,
			                                          liveness.lastStatusReader,
			                                          liveness.declaredLiveResources );
			appendReadyScheduledSegmentSlots( segment,
			                                 slots,
			                                 segmentMask,
			                                 latencyTracker,
			                                 blockStartCycle,
			                                 currentCycle );
			segment.clear();
			haveSegment = false;
			bool pairedWithPreviousSlot = false;
			if( !slots.empty() )
			{
				const unsigned int pairedCycle = blockStartCycle + slots.back().issueCycle;
				if( pairingHazardDelay( latencyTracker, token, NULL, static_cast<int>( pairedCycle ) ) <= 0 )
					pairedWithPreviousSlot =
						tryPairTailWithPreviousSlot( slots,
						                             token,
						                             slots.back().ignoredImplicitWawResources );
			}
			if( !pairedWithPreviousSlot )
			{
				appendReadHazardPaddingSlots( slots,
				                              token,
				                              NULL,
				                              latencyTracker,
				                              blockStartCycle,
				                              currentCycle );
				slots.push_back( makeIssueSlot( &token,
				                                NULL,
				                                currentCycle - blockStartCycle,
				                                1,
				                                VU_RESOURCE_NONE ) );
				latencyTracker.recordWrites( token, static_cast<int>( currentCycle ) );
				++currentCycle;
			}
			else
			{
				const unsigned int pairedCycle = blockStartCycle + slots.back().issueCycle;
				latencyTracker.recordWrites( token, static_cast<int>( pairedCycle ) );
			}
		}

		segmentMask = ignoredFlagWawMaskForIndex( segmentLastIndex,
		                                          liveness.lastMacReader,
		                                          liveness.lastClipReader,
		                                          liveness.lastStatusReader,
		                                          liveness.declaredLiveResources );
		appendReadyScheduledSegmentSlots( segment,
		                                 slots,
		                                 segmentMask,
		                                 latencyTracker,
		                                 blockStartCycle,
		                                 currentCycle );

		assignScheduledIssueSlotTokenIndices( block, slots );
		return slots;
	}

	bool branchTargetLabel( const Token& token, std::string& label )
	{
		if( !token.operand() || token.operand()->unit() != Operand::BRU )
			return false;

		for( std::list<Token::Argument>::const_iterator i = token.arguments().begin(); i != token.arguments().end(); ++i )
		{
			if( ((*i).flags() & Token::Argument::BRANCH)
			    && (*i).type() == Token::Argument::IMMEDIATE
			    && (*i).immediate().length() != 0 )
			{
				label = (*i).immediate();
				return true;
			}
		}

		return false;
	}

	bool tokenIsLoopDirective( const Token& token )
	{
		return token.operand() && token.operand()->name() == "--LoopCS";
	}

	bool tokenIsLoopExtraDirective( const Token& token )
	{
		return token.operand() && token.operand()->name() == "--LoopExtra";
	}

	bool tokenCanCarrySoftwarePipelineLabel( const Token& token )
	{
		return !token.operand() || tokenIsLoopDirective( token );
	}

	bool parseImmediateLong( const std::string& text, long& value )
	{
		char* end = NULL;
		value = std::strtol( text.c_str(), &end, 0 );
		return end && *end == '\0';
	}

	bool parseImmediateUnsigned( const std::string& text, unsigned int& value )
	{
		long parsed = 0;
		if( !parseImmediateLong( text, parsed ) || parsed < 0 )
			return false;
		value = static_cast<unsigned int>( parsed );
		return true;
	}

	bool describeLoopCsDirective( const Token& token,
	                              unsigned int& clid,
	                              unsigned int& mlid )
	{
		if( !tokenIsLoopDirective( token ) )
			return false;

		const std::list<Token::Argument>& args = token.arguments();
		if( args.size() != 2 )
			return false;

		std::list<Token::Argument>::const_iterator first = args.begin();
		std::list<Token::Argument>::const_iterator second = first;
		++second;
		if( first->type() != Token::Argument::IMMEDIATE
		    || second->type() != Token::Argument::IMMEDIATE )
			return false;

		return parseImmediateUnsigned( first->immediate(), clid )
		    && parseImmediateUnsigned( second->immediate(), mlid );
	}

	bool describeLoopCsDirectiveAtLabel( const std::vector<const Token*>& tokens,
	                                     unsigned int labelIndex,
	                                     unsigned int& clid,
	                                     unsigned int& mlid )
	{
		if( labelIndex < tokens.size()
		    && describeLoopCsDirective( *tokens[labelIndex], clid, mlid ) )
			return true;
		const unsigned int next = labelIndex + 1;
		return next < tokens.size()
		    && describeLoopCsDirective( *tokens[next], clid, mlid );
	}

	bool describeSelfIntegerImmediateUpdate( const Token& token,
	                                         unsigned int tokenIndex,
	                                         VuLoopInductionUpdate& update )
	{
		if( !token.operand() )
			return false;

		const std::string name = lowerVuTokenName( token );
		if( name != "iaddiu" && name != "isubiu" )
			return false;

		const std::list<Token::Argument>& args = token.arguments();
		if( args.size() != 3 )
			return false;

		std::list<Token::Argument>::const_iterator dst = args.begin();
		std::list<Token::Argument>::const_iterator src = dst;
		++src;
		std::list<Token::Argument>::const_iterator imm = src;
		++imm;

		if( (*dst).type() != Token::Argument::INTEGER_REGISTER
		    || (*src).type() != Token::Argument::INTEGER_REGISTER
		    || (*imm).type() != Token::Argument::IMMEDIATE )
			return false;
		if( !((*dst).flags() & Token::Argument::WRITE) )
			return false;

		std::string dstReg;
		std::string srcReg;
		if( !vuRegisterKey( *dst, dstReg ) || !vuRegisterKey( *src, srcReg ) )
			return false;
		if( dstReg != srcReg )
			return false;

		long step = 0;
		const bool stepKnown = parseImmediateLong( (*imm).immediate(), step );
		if( name == "isubiu" )
			step = -step;

		update.registerName = dstReg;
		update.mnemonic = name;
		update.immediate = (*imm).immediate();
		update.step = step;
		update.stepKnown = stepKnown;
		update.tokenIndex = tokenIndex;
		return true;
	}

	bool loopTargetHasDirective( const std::vector<const Token*>& tokens, unsigned int labelIndex )
	{
		if( labelIndex < tokens.size() && tokenIsLoopDirective( *tokens[labelIndex] ) )
			return true;
		const unsigned int next = labelIndex + 1;
		return next < tokens.size() && tokenIsLoopDirective( *tokens[next] );
	}

	void addUniqueString( std::list<std::string>& values, const std::string& value )
	{
		if( !containsKey( values, value ) )
			values.push_back( value );
	}

	void collectRegisterIntersection( const std::list<std::string>& reads,
	                                  const std::list<std::string>& writes,
	                                  std::list<std::string>& intersection )
	{
		for( std::list<std::string>::const_iterator i = reads.begin(); i != reads.end(); ++i )
		{
			if( containsKey( writes, *i ) )
				addUniqueString( intersection, *i );
		}
	}

	void analyzeLoopCandidateResources( VuLoopCandidate& candidate )
	{
		std::list<std::string> reads;
		std::list<std::string> writes;

		for( unsigned int offset = 0; offset < candidate.bodyTokens.size(); ++offset )
		{
			const Token* token = candidate.bodyTokens[offset];
			VuTokenResourceAccess access;
			if( buildVuTokenResourceAccess( *token, access ) )
			{
				if( access.memoryKind == VU_MEMORY_LOAD )
					++candidate.memoryLoadCount;
				else if( access.memoryKind == VU_MEMORY_STORE )
					++candidate.memoryStoreCount;
				else if( access.memoryKind == VU_MEMORY_XGKICK )
					candidate.hasXgkick = true;

				if( access.memoryFlags & (VU_MEMORY_FLAG_PREDEC | VU_MEMORY_FLAG_POSTINC) )
					candidate.hasMemoryPreOrPostIncrement = true;
			}

			VuLoopInductionUpdate inductionUpdate;
			if( describeSelfIntegerImmediateUpdate( *token,
			                                        candidate.firstBodyTokenIndex + offset,
			                                        inductionUpdate ) )
			{
				addUniqueString( candidate.inductionRegisters, inductionUpdate.registerName );
				candidate.inductionUpdates.push_back( inductionUpdate );
			}

			std::list<std::string> tokenReads;
			std::list<std::string> tokenWrites;
			collectVuRegisterReadKeys( *token, tokenReads );
			collectVuRegisterWriteKeys( *token, tokenWrites );
			for( std::list<std::string>::const_iterator read = tokenReads.begin(); read != tokenReads.end(); ++read )
				addUniqueString( reads, *read );
			for( std::list<std::string>::const_iterator write = tokenWrites.begin(); write != tokenWrites.end(); ++write )
				addUniqueString( writes, *write );
		}

		collectRegisterIntersection( reads, writes, candidate.loopReadWriteRegisters );
	}

	void collectLoopCarriedQInputs( const VuLoopCandidate& loop,
	                                unsigned int consumerOffset,
	                                std::list<std::string>& carriedInputs )
	{
		std::list<std::string> reads;
		collectVuRegisterReadKeys( *loop.bodyTokens[consumerOffset], reads );

		for( unsigned int before = 0; before < consumerOffset; ++before )
		{
			std::list<std::string> writes;
			collectVuRegisterWriteKeys( *loop.bodyTokens[before], writes );
			for( std::list<std::string>::const_iterator read = reads.begin(); read != reads.end(); ++read )
			{
				if( containsKey( writes, *read ) )
					addUniqueString( carriedInputs, *read );
			}
		}
	}

	void collectLoopCarriedQOutputs( const VuLoopCandidate& loop,
	                                 unsigned int consumerOffset,
	                                 std::list<std::string>& carriedOutputs )
	{
		std::list<std::string> writes;
		collectVuRegisterWriteKeys( *loop.bodyTokens[consumerOffset], writes );

		for( unsigned int after = consumerOffset + 1; after < loop.bodyTokens.size(); ++after )
		{
			std::list<std::string> reads;
			collectVuRegisterReadKeys( *loop.bodyTokens[after], reads );
			for( std::list<std::string>::const_iterator write = writes.begin(); write != writes.end(); ++write )
			{
				if( containsKey( reads, *write ) )
					addUniqueString( carriedOutputs, *write );
			}
		}
	}

	unsigned int countEmittableTokens( const VuLoopCandidate& loop,
	                                   unsigned int beginOffset,
	                                   unsigned int endOffset )
	{
		unsigned int count = 0;
		if( beginOffset > loop.bodyTokens.size() )
			return 0;
		if( endOffset > loop.bodyTokens.size() )
			endOffset = static_cast<unsigned int>( loop.bodyTokens.size() );
		for( unsigned int i = beginOffset; i < endOffset; ++i )
		{
			const Token& token = *loop.bodyTokens[i];
			if( token.operand()
			    && !token.operand()->isPreprocessor()
			    && !(token.flags() & Token::IGNORED)
			    && (token.operand()->isUpperExecutionPath() || token.operand()->isLowerExecutionPath()) )
				++count;
		}
		return count;
	}

	bool tokenIsPipelineInstruction( const Token& token )
	{
		return token.operand()
		    && !token.operand()->isPreprocessor()
		    && !(token.flags() & Token::IGNORED)
		    && (token.operand()->isUpperExecutionPath() || token.operand()->isLowerExecutionPath());
	}

	void appendPipelineInstructionIndices( const VuLoopCandidate& loop,
	                                       unsigned int beginOffset,
	                                       unsigned int endOffset,
	                                       std::vector<unsigned int>& indices )
	{
		if( beginOffset > loop.bodyTokens.size() )
			return;
		if( endOffset > loop.bodyTokens.size() )
			endOffset = static_cast<unsigned int>( loop.bodyTokens.size() );
		for( unsigned int i = beginOffset; i < endOffset; ++i )
		{
			if( tokenIsPipelineInstruction( *loop.bodyTokens[i] ) )
				indices.push_back( loop.firstBodyTokenIndex + i );
		}
	}

	void addPipelineBlocker( VuLoopPipelineOpportunity& opportunity, const std::string& blocker )
	{
		if( !containsKey( opportunity.softwarePipelineBlockers, blocker ) )
			opportunity.softwarePipelineBlockers.push_back( blocker );
	}

	void addMultiQPipelineBlocker( VuLoopPipelineOpportunity& opportunity, const std::string& blocker )
	{
		if( !containsKey( opportunity.multiQSoftwarePipelineBlockers, blocker ) )
			opportunity.multiQSoftwarePipelineBlockers.push_back( blocker );
	}

	void addSuffixStoreDrainBlocker( VuLoopPipelineOpportunity& opportunity, const std::string& blocker )
	{
		if( !containsKey( opportunity.suffixStoreDrainBlockers, blocker ) )
			opportunity.suffixStoreDrainBlockers.push_back( blocker );
	}

	std::string registerBaseKey( const std::string& key )
	{
		std::string::size_type field = key.find( '.' );
		if( field == std::string::npos )
			return key;
		return key.substr( 0, field );
	}

	// Forward declaration: the rename-aware RecMII below consults this
	// predicate, which is defined later in the same anonymous namespace
	// (see vuOpIsSplittableForKernelRenameByName around the kernel-rename
	// splitter section). The forward decl keeps both helpers in the same
	// translation unit without reshuffling the existing layout.
	bool vuOpIsSplittableForKernelRenameByName( const std::string& opNameRaw );

	// Track 9.G step 4a: shared MII helpers. Compute the recurrence-bound MII
	// (RecMII) and resource-bound MII (ResMII) for a simple-counted loop body.
	// Used by the OPENVCL_DUMP_LOOP_MII diagnostic and intended for reuse by
	// subsequent step-4 sub-tracks (priority list, modulo reservation table,
	// iterative scheduler). Diagnostic / analysis only.
	unsigned int computeLoopRecMII( const std::vector<unsigned int>& mt,
	                                const std::vector<const Token*>& indexedTokens )
	{
		const unsigned int n = static_cast<unsigned int>( mt.size() );
		if( n == 0 ) return 1;
		std::vector< std::vector<std::string> > nodeWrites( n ), nodeReads( n );
		for( unsigned int k = 0; k < n; ++k )
		{
			if( mt[k] >= indexedTokens.size() ) continue;
			VuTokenResourceAccess acc;
			if( !buildVuTokenResourceAccess( *indexedTokens[mt[k]], acc ) ) continue;
			for( std::list<std::string>::const_iterator it = acc.registerWrites.begin();
			     it != acc.registerWrites.end(); ++it )
				nodeWrites[k].push_back( registerBaseKey( *it ) );
			for( std::list<std::string>::const_iterator it = acc.registerReads.begin();
			     it != acc.registerReads.end(); ++it )
				nodeReads[k].push_back( registerBaseKey( *it ) );
			const unsigned int iw = acc.implicitWrites;
			const unsigned int ir = acc.implicitReads;
			if( iw & VU_RESOURCE_ACC )  nodeWrites[k].push_back( "@ACC" );
			if( iw & VU_RESOURCE_Q )    nodeWrites[k].push_back( "@Q" );
			if( iw & VU_RESOURCE_P )    nodeWrites[k].push_back( "@P" );
			if( iw & VU_RESOURCE_R )    nodeWrites[k].push_back( "@R" );
			if( iw & VU_RESOURCE_I )    nodeWrites[k].push_back( "@I" );
			if( iw & VU_RESOURCE_MAC )  nodeWrites[k].push_back( "@MAC" );
			if( iw & VU_RESOURCE_CLIP ) nodeWrites[k].push_back( "@CLIP" );
			if( ir & VU_RESOURCE_ACC )  nodeReads[k].push_back( "@ACC" );
			if( ir & VU_RESOURCE_Q )    nodeReads[k].push_back( "@Q" );
			if( ir & VU_RESOURCE_P )    nodeReads[k].push_back( "@P" );
			if( ir & VU_RESOURCE_R )    nodeReads[k].push_back( "@R" );
			if( ir & VU_RESOURCE_I )    nodeReads[k].push_back( "@I" );
			if( ir & VU_RESOURCE_MAC )  nodeReads[k].push_back( "@MAC" );
			if( ir & VU_RESOURCE_CLIP ) nodeReads[k].push_back( "@CLIP" );
		}
		std::vector<unsigned int> eFrom, eTo, eDist;
		std::vector<int> eLat;
		unsigned int carried = 0;
		for( unsigned int i = 0; i < n; ++i )
		{
			for( unsigned int j = 0; j < n; ++j )
			{
				if( i == j ) continue;
				const unsigned int dist = ( i < j ) ? 0u : 1u;
				std::string sharedRaw;
				for( unsigned int a = 0; a < nodeWrites[i].size() && sharedRaw.empty(); ++a )
					for( unsigned int b = 0; b < nodeReads[j].size() && sharedRaw.empty(); ++b )
						if( nodeWrites[i][a] == nodeReads[j][b] )
							sharedRaw = nodeWrites[i][a];
				if( !sharedRaw.empty()
				    && mt[i] < indexedTokens.size()
				    && mt[j] < indexedTokens.size() )
				{
					VuLatencyTracker tr;
					tr.reset();
					tr.recordWrites( *indexedTokens[mt[i]], 0 );
					const int d = tr.readHazardDelay( *indexedTokens[mt[j]], NULL, 0 );
					const unsigned int lat = static_cast<unsigned int>( d > 0 ? d : 1 );
					eFrom.push_back( i ); eTo.push_back( j ); eDist.push_back( dist );
					eLat.push_back( static_cast<int>( lat ) );
					if( dist == 1 ) ++carried;
				}
				std::string sharedWaw;
				for( unsigned int a = 0; a < nodeWrites[i].size() && sharedWaw.empty(); ++a )
					for( unsigned int b = 0; b < nodeWrites[j].size() && sharedWaw.empty(); ++b )
						if( nodeWrites[i][a] == nodeWrites[j][b] )
							sharedWaw = nodeWrites[i][a];
				if( !sharedWaw.empty() )
				{
					eFrom.push_back( i ); eTo.push_back( j ); eDist.push_back( dist );
					eLat.push_back( 1 );
					if( dist == 1 ) ++carried;
				}
				std::string sharedWar;
				for( unsigned int a = 0; a < nodeReads[i].size() && sharedWar.empty(); ++a )
					for( unsigned int b = 0; b < nodeWrites[j].size() && sharedWar.empty(); ++b )
						if( nodeReads[i][a] == nodeWrites[j][b] )
							sharedWar = nodeReads[i][a];
				if( !sharedWar.empty() )
				{
					eFrom.push_back( i ); eTo.push_back( j ); eDist.push_back( dist );
					eLat.push_back( 1 );
					if( dist == 1 ) ++carried;
				}
			}
		}
		if( carried == 0 || eFrom.empty() ) return 1;
		const unsigned int E = static_cast<unsigned int>( eFrom.size() );
		double hi = 0.0;
		for( unsigned int e = 0; e < eLat.size(); ++e ) hi += (double)eLat[e];
		if( hi < 1.0 ) hi = 1.0;
		double lo = 0.0;
		std::vector<double> d( n, 0.0 );
		for( int iter = 0; iter < 60; ++iter )
		{
			const double mid = 0.5 * ( lo + hi );
			for( unsigned int v = 0; v < n; ++v ) d[v] = 0.0;
			for( unsigned int pass = 0; pass < n; ++pass )
			{
				bool changed = false;
				for( unsigned int e = 0; e < E; ++e )
				{
					const double w = (double)eLat[e] - mid * (double)eDist[e];
					const double nd = d[ eFrom[e] ] + w;
					if( nd > d[ eTo[e] ] + 1e-12 )
					{
						d[ eTo[e] ] = nd;
						changed = true;
					}
				}
				if( !changed ) break;
			}
			bool positive = false;
			for( unsigned int e = 0; e < E && !positive; ++e )
			{
				const double w = (double)eLat[e] - mid * (double)eDist[e];
				if( d[ eFrom[e] ] + w > d[ eTo[e] ] + 1e-9 )
					positive = true;
			}
			if( positive ) lo = mid;
			else           hi = mid;
		}
		const double recmiiFract = lo;
		unsigned int recmiiInt = static_cast<unsigned int>( recmiiFract );
		if( (double)recmiiInt + 1e-6 < recmiiFract ) ++recmiiInt;
		if( recmiiInt < 1 ) recmiiInt = 1;
		return recmiiInt;
	}

	// Track 9.G step 1c-1 (diagnostic): rename-aware RecMII.
	//
	// The baseline `computeLoopRecMII` above counts every loop-carried RAW
	// edge against the recurrence bound. On xform_loop_lid this yields
	// RecMII=36 even though ResMII=20, because the VF13.{x,y,z,w} chain
	// (split FMACs writing VF13 and a later ftoi4 reading the same base)
	// shows up as four self-recurrences with distance 1.
	//
	// Track 9.G step 8b-1 / 8b-2x already plans renames that break exactly
	// these chains: it routes successive iterations' writes of VF13 to
	// scratch registers VF30/29/28/31 and reads them back via per-field
	// MOVEs in the next iteration. With those renames in place the
	// dist=1 edge is severed, so the corresponding recurrence vanishes.
	//
	// This helper recomputes RecMII under the assumption that any
	// loop-carried RAW edge whose shared base is a VF register AND whose
	// producer is in the kernel-rename splittable allowlist will be
	// broken by the rename machinery. WAW / WAR edges and edges keyed on
	// implicit resources (@ACC, @Q, etc.) are never dropped. The returned
	// value is purely diagnostic in 1c-1 — the live MII computation in
	// steps 4a/4b/4c/4d still uses the baseline `computeLoopRecMII`.
	//
	// A subsequent sub-step (1c-2) will wire this value into the MII used
	// by the placer when OPENVCL_USE_GENERIC_KERNEL_REWRITE is set,
	// preserving env-OFF MD5s while opening the door to multi-stage
	// schedules env-ON.
	unsigned int computeLoopRecMIIRenamed( const std::vector<unsigned int>& mt,
	                                       const std::vector<const Token*>& indexedTokens,
	                                       unsigned int* droppedCarriedEdgesOut )
	{
		if( droppedCarriedEdgesOut != NULL ) *droppedCarriedEdgesOut = 0;
		const unsigned int n = static_cast<unsigned int>( mt.size() );
		if( n == 0 ) return 1;
		std::vector< std::vector<std::string> > nodeWrites( n ), nodeReads( n );
		// rotatedBases = set of VF register bases (e.g., "VF13") that have
		// at least one splittable producer in the loop. Per the kernel-
		// rename model (8b-1 / 8b-2x), the writer at each iteration is
		// redirected to a scratch register, so successive iterations'
		// values of the base live in DIFFERENT physical registers. Hence
		// all loop-carried (dist=1) RAW / WAW / WAR edges keyed on a
		// rotated VF base disappear: the next iteration reads / writes a
		// different physical register from the previous iteration's.
		std::vector<std::string> rotatedBases;
		for( unsigned int k = 0; k < n; ++k )
		{
			if( mt[k] >= indexedTokens.size() ) continue;
			VuTokenResourceAccess acc;
			if( !buildVuTokenResourceAccess( *indexedTokens[mt[k]], acc ) ) continue;
			for( std::list<std::string>::const_iterator it = acc.registerWrites.begin();
			     it != acc.registerWrites.end(); ++it )
				nodeWrites[k].push_back( registerBaseKey( *it ) );
			for( std::list<std::string>::const_iterator it = acc.registerReads.begin();
			     it != acc.registerReads.end(); ++it )
				nodeReads[k].push_back( registerBaseKey( *it ) );
			const unsigned int iw = acc.implicitWrites;
			const unsigned int ir = acc.implicitReads;
			if( iw & VU_RESOURCE_ACC )  nodeWrites[k].push_back( "@ACC" );
			if( iw & VU_RESOURCE_Q )    nodeWrites[k].push_back( "@Q" );
			if( iw & VU_RESOURCE_P )    nodeWrites[k].push_back( "@P" );
			if( iw & VU_RESOURCE_R )    nodeWrites[k].push_back( "@R" );
			if( iw & VU_RESOURCE_I )    nodeWrites[k].push_back( "@I" );
			if( iw & VU_RESOURCE_MAC )  nodeWrites[k].push_back( "@MAC" );
			if( iw & VU_RESOURCE_CLIP ) nodeWrites[k].push_back( "@CLIP" );
			if( ir & VU_RESOURCE_ACC )  nodeReads[k].push_back( "@ACC" );
			if( ir & VU_RESOURCE_Q )    nodeReads[k].push_back( "@Q" );
			if( ir & VU_RESOURCE_P )    nodeReads[k].push_back( "@P" );
			if( ir & VU_RESOURCE_R )    nodeReads[k].push_back( "@R" );
			if( ir & VU_RESOURCE_I )    nodeReads[k].push_back( "@I" );
			if( ir & VU_RESOURCE_MAC )  nodeReads[k].push_back( "@MAC" );
			if( ir & VU_RESOURCE_CLIP ) nodeReads[k].push_back( "@CLIP" );
			if( indexedTokens[mt[k]]->operand() != NULL
			    && vuOpIsSplittableForKernelRenameByName(
			           indexedTokens[mt[k]]->operand()->name() ) )
			{
				for( unsigned int w = 0; w < nodeWrites[k].size(); ++w )
				{
					const std::string& b = nodeWrites[k][w];
					if( b.empty() || b[0] == '@' ) continue;
					const char c0 = b[0];
					if( c0 != 'v' && c0 != 'V' ) continue;
					bool already = false;
					for( unsigned int r = 0; r < rotatedBases.size() && !already; ++r )
						if( rotatedBases[r] == b ) already = true;
					if( !already ) rotatedBases.push_back( b );
				}
			}
		}
		// isRotated(b) helper inlined: small set, linear scan acceptable.
		unsigned int droppedCarried = 0;
		std::vector<unsigned int> eFrom, eTo, eDist;
		std::vector<int> eLat;
		unsigned int carried = 0;
		for( unsigned int i = 0; i < n; ++i )
		{
			for( unsigned int j = 0; j < n; ++j )
			{
				if( i == j ) continue;
				const unsigned int dist = ( i < j ) ? 0u : 1u;
				std::string sharedRaw;
				for( unsigned int a = 0; a < nodeWrites[i].size() && sharedRaw.empty(); ++a )
					for( unsigned int b = 0; b < nodeReads[j].size() && sharedRaw.empty(); ++b )
						if( nodeWrites[i][a] == nodeReads[j][b] )
							sharedRaw = nodeWrites[i][a];
				if( !sharedRaw.empty()
				    && mt[i] < indexedTokens.size()
				    && mt[j] < indexedTokens.size() )
				{
					bool drop = false;
					if( dist == 1 )
					{
						for( unsigned int r = 0; r < rotatedBases.size() && !drop; ++r )
							if( rotatedBases[r] == sharedRaw ) drop = true;
					}
					if( drop ) { ++droppedCarried; }
					else
					{
						VuLatencyTracker tr;
						tr.reset();
						tr.recordWrites( *indexedTokens[mt[i]], 0 );
						const int d = tr.readHazardDelay( *indexedTokens[mt[j]], NULL, 0 );
						const unsigned int lat = static_cast<unsigned int>( d > 0 ? d : 1 );
						eFrom.push_back( i ); eTo.push_back( j ); eDist.push_back( dist );
						eLat.push_back( static_cast<int>( lat ) );
						if( dist == 1 ) ++carried;
					}
				}
				std::string sharedWaw;
				for( unsigned int a = 0; a < nodeWrites[i].size() && sharedWaw.empty(); ++a )
					for( unsigned int b = 0; b < nodeWrites[j].size() && sharedWaw.empty(); ++b )
						if( nodeWrites[i][a] == nodeWrites[j][b] )
							sharedWaw = nodeWrites[i][a];
				if( !sharedWaw.empty() )
				{
					bool drop = false;
					if( dist == 1 )
					{
						for( unsigned int r = 0; r < rotatedBases.size() && !drop; ++r )
							if( rotatedBases[r] == sharedWaw ) drop = true;
					}
					if( drop ) { ++droppedCarried; }
					else
					{
						eFrom.push_back( i ); eTo.push_back( j ); eDist.push_back( dist );
						eLat.push_back( 1 );
						if( dist == 1 ) ++carried;
					}
				}
				std::string sharedWar;
				for( unsigned int a = 0; a < nodeReads[i].size() && sharedWar.empty(); ++a )
					for( unsigned int b = 0; b < nodeWrites[j].size() && sharedWar.empty(); ++b )
						if( nodeReads[i][a] == nodeWrites[j][b] )
							sharedWar = nodeReads[i][a];
				if( !sharedWar.empty() )
				{
					bool drop = false;
					if( dist == 1 )
					{
						for( unsigned int r = 0; r < rotatedBases.size() && !drop; ++r )
							if( rotatedBases[r] == sharedWar ) drop = true;
					}
					if( drop ) { ++droppedCarried; }
					else
					{
						eFrom.push_back( i ); eTo.push_back( j ); eDist.push_back( dist );
						eLat.push_back( 1 );
						if( dist == 1 ) ++carried;
					}
				}
			}
		}
		if( droppedCarriedEdgesOut != NULL ) *droppedCarriedEdgesOut = droppedCarried;
		if( carried == 0 || eFrom.empty() ) return 1;
		const unsigned int E = static_cast<unsigned int>( eFrom.size() );
		double hi = 0.0;
		for( unsigned int e = 0; e < eLat.size(); ++e ) hi += (double)eLat[e];
		if( hi < 1.0 ) hi = 1.0;
		double lo = 0.0;
		std::vector<double> d( n, 0.0 );
		for( int iter = 0; iter < 60; ++iter )
		{
			const double mid = 0.5 * ( lo + hi );
			for( unsigned int v = 0; v < n; ++v ) d[v] = 0.0;
			for( unsigned int pass = 0; pass < n; ++pass )
			{
				bool changed = false;
				for( unsigned int e = 0; e < E; ++e )
				{
					const double w = (double)eLat[e] - mid * (double)eDist[e];
					const double nd = d[ eFrom[e] ] + w;
					if( nd > d[ eTo[e] ] + 1e-12 )
					{
						d[ eTo[e] ] = nd;
						changed = true;
					}
				}
				if( !changed ) break;
			}
			bool positive = false;
			for( unsigned int e = 0; e < E && !positive; ++e )
			{
				const double w = (double)eLat[e] - mid * (double)eDist[e];
				if( d[ eFrom[e] ] + w > d[ eTo[e] ] + 1e-9 )
					positive = true;
			}
			if( positive ) lo = mid;
			else           hi = mid;
		}
		const double recmiiFract = lo;
		unsigned int recmiiInt = static_cast<unsigned int>( recmiiFract );
		if( (double)recmiiInt + 1e-6 < recmiiFract ) ++recmiiInt;
		if( recmiiInt < 1 ) recmiiInt = 1;
		return recmiiInt;
	}

	unsigned int computeLoopResMII( const std::vector<unsigned int>& mt,
	                                const std::vector<const Token*>& indexedTokens )
	{
		unsigned int nUpper = 0, nLower = 0;
		unsigned int fdivBusy = 0, efuBusy = 0;
		for( unsigned int k = 0; k < mt.size(); ++k )
		{
			if( mt[k] >= indexedTokens.size() ) continue;
			const Token& tk = *indexedTokens[mt[k]];
			if( !tk.operand() ) continue;
			const VuInstructionInfo* info =
			    findVuInstructionInfo( normalizeVuMnemonic( tk.operand()->name() ) );
			if( !info ) continue;
			if( info->pipe == VU_PIPE_NOP ) continue;
			if( info->pipe == VU_PIPE_UPPER ) ++nUpper;
			else if( info->pipe == VU_PIPE_LOWER ) ++nLower;
			if( info->unit == VU_EXEC_FDIV ) fdivBusy += info->throughput;
			else if( info->unit == VU_EXEC_EFU ) efuBusy += info->throughput;
		}
		unsigned int resmii = nUpper;
		if( nLower    > resmii ) resmii = nLower;
		if( fdivBusy  > resmii ) resmii = fdivBusy;
		if( efuBusy   > resmii ) resmii = efuBusy;
		if( resmii < 1 ) resmii = 1;
		return resmii;
	}

	// Track 9.G step 4c: per-node priority for iterative modulo scheduling.
	// Compute ASAP / ALAP / height / mobility over the intra-iteration DDG
	// (RAW via VuLatencyTracker, WAW/WAR with lat=1). The intra DDG is a DAG
	// because intra edges are emitted only for i<j; topo order = ascending
	// index. Loop-carried edges are intentionally excluded here: they are
	// already captured by RecMII (which bounds II), and ASAP/ALAP within a
	// single iteration are the inputs Lam's iterative modulo scheduler
	// consumes to choose insertion order. Diagnostic / analysis only.
	struct LoopPriorityResult
	{
		std::vector<unsigned int> asap;
		std::vector<unsigned int> alap;
		std::vector<unsigned int> height;
		std::vector<unsigned int> mobility;
		std::vector<unsigned int> order;     // descending priority
		unsigned int              scheduleLength;
	};

	void computeLoopPriority( const std::vector<unsigned int>& mt,
	                          const std::vector<const Token*>& indexedTokens,
	                          unsigned int II,
	                          LoopPriorityResult& out )
	{
		const unsigned int n = static_cast<unsigned int>( mt.size() );
		out.asap.assign( n, 0 );
		out.alap.assign( n, 0 );
		out.height.assign( n, 0 );
		out.mobility.assign( n, 0 );
		out.order.clear();
		out.scheduleLength = 0;
		if( n == 0 ) return;

		std::vector< std::vector<std::string> > nodeWrites( n ), nodeReads( n );
		for( unsigned int k = 0; k < n; ++k )
		{
			if( mt[k] >= indexedTokens.size() ) continue;
			VuTokenResourceAccess acc;
			if( !buildVuTokenResourceAccess( *indexedTokens[mt[k]], acc ) ) continue;
			for( std::list<std::string>::const_iterator it = acc.registerWrites.begin();
			     it != acc.registerWrites.end(); ++it )
				nodeWrites[k].push_back( registerBaseKey( *it ) );
			for( std::list<std::string>::const_iterator it = acc.registerReads.begin();
			     it != acc.registerReads.end(); ++it )
				nodeReads[k].push_back( registerBaseKey( *it ) );
			const unsigned int iw = acc.implicitWrites;
			const unsigned int ir = acc.implicitReads;
			if( iw & VU_RESOURCE_ACC )  nodeWrites[k].push_back( "@ACC" );
			if( iw & VU_RESOURCE_Q )    nodeWrites[k].push_back( "@Q" );
			if( iw & VU_RESOURCE_P )    nodeWrites[k].push_back( "@P" );
			if( iw & VU_RESOURCE_R )    nodeWrites[k].push_back( "@R" );
			if( iw & VU_RESOURCE_I )    nodeWrites[k].push_back( "@I" );
			if( iw & VU_RESOURCE_MAC )  nodeWrites[k].push_back( "@MAC" );
			if( iw & VU_RESOURCE_CLIP ) nodeWrites[k].push_back( "@CLIP" );
			if( ir & VU_RESOURCE_ACC )  nodeReads[k].push_back( "@ACC" );
			if( ir & VU_RESOURCE_Q )    nodeReads[k].push_back( "@Q" );
			if( ir & VU_RESOURCE_P )    nodeReads[k].push_back( "@P" );
			if( ir & VU_RESOURCE_R )    nodeReads[k].push_back( "@R" );
			if( ir & VU_RESOURCE_I )    nodeReads[k].push_back( "@I" );
			if( ir & VU_RESOURCE_MAC )  nodeReads[k].push_back( "@MAC" );
			if( ir & VU_RESOURCE_CLIP ) nodeReads[k].push_back( "@CLIP" );
		}

		std::vector<unsigned int> eFrom, eTo, eLat;
		for( unsigned int i = 0; i < n; ++i )
		{
			for( unsigned int j = i + 1; j < n; ++j )
			{
				std::string sharedRaw;
				for( unsigned int a = 0; a < nodeWrites[i].size() && sharedRaw.empty(); ++a )
					for( unsigned int b = 0; b < nodeReads[j].size() && sharedRaw.empty(); ++b )
						if( nodeWrites[i][a] == nodeReads[j][b] )
							sharedRaw = nodeWrites[i][a];
				if( !sharedRaw.empty()
				    && mt[i] < indexedTokens.size()
				    && mt[j] < indexedTokens.size() )
				{
					VuLatencyTracker tr;
					tr.reset();
					tr.recordWrites( *indexedTokens[mt[i]], 0 );
					const int d = tr.readHazardDelay( *indexedTokens[mt[j]], NULL, 0 );
					const unsigned int lat = static_cast<unsigned int>( d > 0 ? d : 1 );
					eFrom.push_back( i ); eTo.push_back( j ); eLat.push_back( lat );
				}
				std::string sharedWaw;
				for( unsigned int a = 0; a < nodeWrites[i].size() && sharedWaw.empty(); ++a )
					for( unsigned int b = 0; b < nodeWrites[j].size() && sharedWaw.empty(); ++b )
						if( nodeWrites[i][a] == nodeWrites[j][b] )
							sharedWaw = nodeWrites[i][a];
				if( !sharedWaw.empty() )
				{
					eFrom.push_back( i ); eTo.push_back( j ); eLat.push_back( 1 );
				}
				std::string sharedWar;
				for( unsigned int a = 0; a < nodeReads[i].size() && sharedWar.empty(); ++a )
					for( unsigned int b = 0; b < nodeWrites[j].size() && sharedWar.empty(); ++b )
						if( nodeReads[i][a] == nodeWrites[j][b] )
							sharedWar = nodeReads[i][a];
				if( !sharedWar.empty() )
				{
					eFrom.push_back( i ); eTo.push_back( j ); eLat.push_back( 1 );
				}
			}
		}

		const unsigned int E = static_cast<unsigned int>( eFrom.size() );

		// ASAP via topological relaxation (i<j is a valid topo order).
		for( unsigned int e = 0; e < E; ++e )
		{
			const unsigned int cand = out.asap[ eFrom[e] ] + eLat[e];
			if( cand > out.asap[ eTo[e] ] )
				out.asap[ eTo[e] ] = cand;
		}

		// Height: longest distance from node to any sink. Reverse topo (i: n-1..0).
		for( unsigned int idx = n; idx-- > 0; )
		{
			unsigned int h = 0;
			for( unsigned int e = 0; e < E; ++e )
			{
				if( eFrom[e] == idx )
				{
					const unsigned int cand = eLat[e] + out.height[ eTo[e] ];
					if( cand > h ) h = cand;
				}
			}
			out.height[idx] = h;
		}

		// Schedule length L = max(asap[i] + height[i]) over all nodes; never
		// shorter than II-1 (a one-iteration window must hold at least II
		// slots). ALAP[i] = L - height[i].
		unsigned int L = 0;
		for( unsigned int i = 0; i < n; ++i )
		{
			const unsigned int reach = out.asap[i] + out.height[i];
			if( reach > L ) L = reach;
		}
		if( II > 0 && L + 1 < II ) L = II - 1;
		out.scheduleLength = L;

		for( unsigned int i = 0; i < n; ++i )
			out.alap[i] = ( out.height[i] > L ) ? 0u : ( L - out.height[i] );

		for( unsigned int i = 0; i < n; ++i )
			out.mobility[i] = ( out.alap[i] > out.asap[i] )
			                ? ( out.alap[i] - out.asap[i] ) : 0u;

		// Order by descending height; tie-break by ascending mobility, then
		// ascending node index. Selection sort (n is small, < 50 in practice).
		out.order.resize( n );
		for( unsigned int i = 0; i < n; ++i ) out.order[i] = i;
		for( unsigned int a = 0; a < n; ++a )
		{
			unsigned int best = a;
			for( unsigned int b = a + 1; b < n; ++b )
			{
				const unsigned int ib = out.order[b];
				const unsigned int ia = out.order[best];
				const bool hbBigger     = out.height[ib]   >  out.height[ia];
				const bool hEqual       = out.height[ib]   == out.height[ia];
				const bool mbSmaller    = out.mobility[ib] <  out.mobility[ia];
				const bool mEqual       = out.mobility[ib] == out.mobility[ia];
				const bool idxSmaller   = ib < ia;
				if( hbBigger
				    || ( hEqual && mbSmaller )
				    || ( hEqual && mEqual && idxSmaller ) )
					best = b;
			}
			if( best != a )
			{
				const unsigned int tmp = out.order[a];
				out.order[a] = out.order[best];
				out.order[best] = tmp;
			}
		}
	}

	std::string registerFieldKey( const std::string& key )
	{
		std::string::size_type field = key.find( '.' );
		if( field == std::string::npos || field + 1 >= key.size() )
			return "";
		return key.substr( field + 1 );
	}

	void appendFieldNames( unsigned int fieldMask, std::list<std::string>& fields )
	{
		if( fieldMask == 0 )
			fieldMask = Token::X | Token::Y | Token::Z | Token::W;
		if( fieldMask & Token::X ) addUniqueString( fields, "x" );
		if( fieldMask & Token::Y ) addUniqueString( fields, "y" );
		if( fieldMask & Token::Z ) addUniqueString( fields, "z" );
		if( fieldMask & Token::W ) addUniqueString( fields, "w" );
	}

	bool describeStoreValueRegister( const Token& token,
	                                 std::string& registerName,
	                                 std::list<std::string>& fields,
	                                 bool& isFloatRegister )
	{
		for( std::list<Token::Argument>::const_iterator i = token.arguments().begin();
		     i != token.arguments().end(); ++i )
		{
			if( (*i).flags() & (Token::Argument::INDIRECT | Token::Argument::WRITE) )
				continue;
			if( (*i).type() != Token::Argument::FLOAT_REGISTER
			    && (*i).type() != Token::Argument::INTEGER_REGISTER )
				continue;
			if( !vuRegisterKey( *i, registerName ) )
				return false;
			isFloatRegister = (*i).type() == Token::Argument::FLOAT_REGISTER;
			if( (*i).type() == Token::Argument::FLOAT_REGISTER )
				appendFieldNames( vuReadFieldMask( token, *i ), fields );
			return true;
		}
		return false;
	}

	void collectRotatedRegisterBaseKeys( const std::list<std::string>& keys,
	                                     std::list<std::string>& rotatedRegisters )
	{
		for( std::list<std::string>::const_iterator i = keys.begin(); i != keys.end(); ++i )
			addUniqueString( rotatedRegisters, registerBaseKey( *i ) );
	}

	void addRotationField( std::vector<VuSoftwarePipelineRotation>& rotations,
	                       const std::string& key,
	                       bool input )
	{
		const std::string base = registerBaseKey( key );
		unsigned int index = static_cast<unsigned int>( rotations.size() );
		for( unsigned int i = 0; i < rotations.size(); ++i )
		{
			if( rotations[i].registerBase == base )
			{
				index = i;
				break;
			}
		}

		if( index == rotations.size() )
		{
			VuSoftwarePipelineRotation rotation;
			rotation.registerBase = base;
			rotation.hasScratchRegister = false;
			rotation.scratchRegister = "";
			rotations.push_back( rotation );
		}

		if( input )
			addUniqueString( rotations[index].inputFields, registerFieldKey( key ) );
		else
			addUniqueString( rotations[index].outputFields, registerFieldKey( key ) );
	}

	void collectRotationDescriptors( const std::list<std::string>& keys,
	                                 bool input,
	                                 std::vector<VuSoftwarePipelineRotation>& rotations )
	{
		for( std::list<std::string>::const_iterator i = keys.begin(); i != keys.end(); ++i )
			addRotationField( rotations, *i, input );
	}

	void collectVfBaseKeys( const std::list<std::string>& keys, std::list<std::string>& bases )
	{
		for( std::list<std::string>::const_iterator i = keys.begin(); i != keys.end(); ++i )
		{
			const std::string base = registerBaseKey( *i );
			if( base.size() >= 2 && base[0] == 'V' && base[1] == 'F' )
				addUniqueString( bases, base );
		}
	}

	void collectLoopVfBaseKeys( const VuLoopCandidate& loop, std::list<std::string>& bases )
	{
		for( std::vector<const Token*>::const_iterator i = loop.bodyTokens.begin(); i != loop.bodyTokens.end(); ++i )
		{
			std::list<std::string> reads;
			std::list<std::string> writes;
			collectVuRegisterReadKeys( **i, reads );
			collectVuRegisterWriteKeys( **i, writes );
			collectVfBaseKeys( reads, bases );
			collectVfBaseKeys( writes, bases );
		}
	}

	std::string vfRegisterName( unsigned int index )
	{
		std::stringstream s;
		s << "VF";
		if( index < 10 )
			s << "0";
		s << index;
		return s.str();
	}

	// Track 9.E step 3: K-deep rotation bank allocation.
	// depth==1 behaves identically to the original single-scratch implementation;
	// depth>1 fills rotationBank with up to `depth` scratch registers per carried
	// register drawn from the free VF pool.  hasScratchRegister / scratchRegister
	// are kept for backward compat (populated from bank[0] when available).
	void assignRotationScratchRegisters( const VuLoopCandidate& loop,
	                                     std::vector<VuSoftwarePipelineRotation>& rotations,
	                                     unsigned int depth = 1 )
	{
		std::list<std::string> used;
		collectLoopVfBaseKeys( loop, used );
		for( std::vector<VuSoftwarePipelineRotation>::const_iterator i = rotations.begin(); i != rotations.end(); ++i )
			addUniqueString( used, i->registerBase );

		for( std::vector<VuSoftwarePipelineRotation>::iterator rotation = rotations.begin();
		     rotation != rotations.end(); ++rotation )
		{
			rotation->rotationBank.clear();
			for( unsigned int k = 0; k < depth; ++k )
			{
				for( unsigned int reverse = 32; reverse > 1; --reverse )
				{
					const std::string scratch = vfRegisterName( reverse - 1 );
					if( containsKey( used, scratch ) )
						continue;
					rotation->rotationBank.push_back( scratch );
					addUniqueString( used, scratch );
					break;
				}
				if( rotation->rotationBank.size() <= k )
					break;  // free VF pool exhausted for this depth
			}
			if( !rotation->rotationBank.empty() )
			{
				rotation->hasScratchRegister = true;
				rotation->scratchRegister = rotation->rotationBank[0];
			}
		}
	}

	void collectSuffixStoreValueKeys( const VuSoftwarePipelineSuffixStore& store,
	                                  std::list<std::string>& keys )
	{
		if( !store.hasStoredValueRegister )
			return;
		if( !store.storedValueIsFloatRegister )
		{
			addUniqueString( keys, store.storedValueRegister );
			return;
		}
		if( store.storedValueFields.empty() )
		{
			addUniqueString( keys, store.storedValueRegister + ".x" );
			addUniqueString( keys, store.storedValueRegister + ".y" );
			addUniqueString( keys, store.storedValueRegister + ".z" );
			addUniqueString( keys, store.storedValueRegister + ".w" );
			return;
		}
		for( std::list<std::string>::const_iterator field = store.storedValueFields.begin();
		     field != store.storedValueFields.end(); ++field )
			addUniqueString( keys, store.storedValueRegister + "." + *field );
	}

	bool suffixStoreNeedsValueRotation( const VuSoftwarePipelineSuffixStore& store,
	                                    const std::list<std::string>& clobberedKeys )
	{
		std::list<std::string> valueKeys;
		collectSuffixStoreValueKeys( store, valueKeys );
		return intersects( valueKeys, clobberedKeys );
	}

	void collectSoftwarePipelinePrefetchWrites( const VuLoopPipelineOpportunity& opportunity,
	                                            const std::vector<const Token*>& indexedTokens,
	                                            bool rotationsWillUseScratch,
	                                            std::list<std::string>& prefetchWrites );
	bool branchCanInvertToDrain( const Token& token );
	bool tokenRangeHasMemoryDependencyWithStore( const VuSoftwarePipelineSuffixStore& store,
	                                             unsigned int beginIndex,
	                                             unsigned int endIndex,
	                                             const std::vector<const Token*>& indexedTokens,
	                                             const std::vector<VuSoftwarePipelineSuffixStore>& stores );
	Token adjustedMultiQCyclicPrefixToken( const Token& token,
	                                       const std::vector<VuSoftwarePipelineRotation>& rotations,
	                                       const std::vector<VuLoopInductionUpdate>& inductionUpdates,
	                                       unsigned int insertionTokenIndex );

	bool tokenRangeWritesAny( unsigned int beginIndex,
	                          unsigned int endIndex,
	                          const std::vector<const Token*>& indexedTokens,
	                          const std::list<std::string>& keys )
	{
		for( unsigned int i = beginIndex; i <= endIndex && i < indexedTokens.size(); ++i )
		{
			std::list<std::string> writes;
			collectVuRegisterWriteKeys( *indexedTokens[i], writes );
			if( intersects( writes, keys ) )
				return true;
		}
		return false;
	}

	void assignSuffixStoreValueScratchRegisters( const VuLoopCandidate& loop,
	                                             const std::list<std::string>& clobberedKeys,
	                                             const std::vector<VuSoftwarePipelineRotation>& rotations,
	                                             const std::vector<const Token*>& indexedTokens,
	                                             unsigned int insertionTokenIndex,
	                                             std::vector<VuSoftwarePipelineSuffixStore>& stores )
	{
		std::list<std::string> used;
		collectLoopVfBaseKeys( loop, used );
		for( std::vector<VuSoftwarePipelineRotation>::const_iterator i = rotations.begin();
		     i != rotations.end(); ++i )
		{
			addUniqueString( used, i->registerBase );
			if( i->hasScratchRegister )
				addUniqueString( used, i->scratchRegister );
		}

		for( std::vector<VuSoftwarePipelineSuffixStore>::iterator store = stores.begin();
		     store != stores.end(); ++store )
		{
			store->requiresValueRotation = false;
			store->hasValueScratchRegister = false;
			store->valueScratchRegister = "";

			if( !store->hasStoredValueRegister
			    || !store->storedValueIsFloatRegister
			    || !suffixStoreNeedsValueRotation( *store, clobberedKeys ) )
				continue;

			std::list<std::string> valueKeys;
			collectSuffixStoreValueKeys( *store, valueKeys );
			if( insertionTokenIndex + 1 < store->tokenIndex
			    && tokenRangeWritesAny( insertionTokenIndex + 1,
			                            store->tokenIndex - 1,
			                            indexedTokens,
			                            valueKeys ) )
				continue;

			store->requiresValueRotation = true;
			store->rotateValueBeforePrefetch = true;
			for( unsigned int reverse = 32; reverse > 1; --reverse )
			{
				const std::string scratch = vfRegisterName( reverse - 1 );
				if( containsKey( used, scratch ) )
					continue;
				store->hasValueScratchRegister = true;
				store->valueScratchRegister = scratch;
				addUniqueString( used, scratch );
				break;
			}
		}
	}

	void collectUsedScratchVfRegisters( const VuLoopCandidate& loop,
	                                    const std::vector<VuSoftwarePipelineRotation>& rotations,
	                                    const std::vector<VuSoftwarePipelineSuffixStore>& stores,
	                                    std::list<std::string>& used )
	{
		collectLoopVfBaseKeys( loop, used );
		for( std::vector<VuSoftwarePipelineRotation>::const_iterator i = rotations.begin();
		     i != rotations.end(); ++i )
		{
			addUniqueString( used, i->registerBase );
			if( i->hasScratchRegister )
				addUniqueString( used, i->scratchRegister );
		}
		for( std::vector<VuSoftwarePipelineSuffixStore>::const_iterator i = stores.begin();
		     i != stores.end(); ++i )
		{
			if( i->hasValueScratchRegister )
				addUniqueString( used, i->valueScratchRegister );
		}
	}

	bool assignOneSuffixStoreValueScratchRegister( VuSoftwarePipelineSuffixStore& store,
	                                               std::list<std::string>& used )
	{
		if( store.hasValueScratchRegister )
			return true;

		for( unsigned int reverse = 32; reverse > 1; --reverse )
		{
			const std::string scratch = vfRegisterName( reverse - 1 );
			if( containsKey( used, scratch ) )
				continue;
			store.hasValueScratchRegister = true;
			store.valueScratchRegister = scratch;
			addUniqueString( used, scratch );
			return true;
		}

		return false;
	}

	void classifySuffixStoreDrainOpportunity( VuLoopPipelineOpportunity& opportunity,
	                                          const VuLoopCandidate& loop,
	                                          bool rotationsWillUseScratch,
	                                          const std::vector<const Token*>& indexedTokens )
	{
		if( opportunity.softwarePipelineSuffixStores.empty() )
			return;

		opportunity.hasSuffixStoreDrainPlan = false;
		opportunity.canEmitSuffixStoreDrain = false;

		if( opportunity.branchTokenIndex >= indexedTokens.size()
		    || !branchCanInvertToDrain( *indexedTokens[opportunity.branchTokenIndex] ) )
			addSuffixStoreDrainBlocker( opportunity, "non_invertible_loop_branch" );
		if( opportunity.qLiveOut )
			addSuffixStoreDrainBlocker( opportunity, "q_live_out" );

		bool hasDrainCandidate = false;
		for( std::vector<VuSoftwarePipelineSuffixStore>::const_iterator i = opportunity.softwarePipelineSuffixStores.begin();
		     i != opportunity.softwarePipelineSuffixStores.end(); ++i )
		{
			if( i->drainCandidate )
				hasDrainCandidate = true;
		}
		if( !hasDrainCandidate )
			return;

		opportunity.hasSuffixStoreDrainPlan = true;

		std::list<std::string> prefetchWrites;
		collectSoftwarePipelinePrefetchWrites( opportunity,
		                                       indexedTokens,
		                                       rotationsWillUseScratch,
		                                       prefetchWrites );
		std::list<std::string> used;
		collectUsedScratchVfRegisters( loop,
		                               opportunity.softwarePipelineRotations,
		                               opportunity.softwarePipelineSuffixStores,
		                               used );

		for( std::vector<VuSoftwarePipelineSuffixStore>::iterator store =
		         opportunity.softwarePipelineSuffixStores.begin();
		     store != opportunity.softwarePipelineSuffixStores.end(); ++store )
		{
			if( !store->drainCandidate )
				continue;
			if( !store->hasNextIterationOffset )
			{
				addSuffixStoreDrainBlocker( opportunity, "store_without_next_offset" );
				continue;
			}
			if( !store->hasStoredValueRegister )
			{
				addSuffixStoreDrainBlocker( opportunity, "store_without_value_register" );
				continue;
			}
			if( store->tokenIndex < indexedTokens.size()
			    && (indexedTokens[store->tokenIndex]->flags() & (Token::PREORDERED | Token::E | Token::D
			                                                   | Token::T | Token::BRANCH_DELAY_FILLER)) )
			{
				addSuffixStoreDrainBlocker( opportunity, "store_has_control_flag" );
				continue;
			}

			std::list<std::string> valueKeys;
			collectSuffixStoreValueKeys( *store, valueKeys );
			const bool valueWrittenAfterStore =
			    store->tokenIndex + 1 <= opportunity.branchTokenIndex
			    && tokenRangeWritesAny( store->tokenIndex + 1,
			                            opportunity.branchTokenIndex,
			                            indexedTokens,
			                            valueKeys );
			const bool prefetchClobbersValue =
			    suffixStoreNeedsValueRotation( *store, prefetchWrites );
			if( valueWrittenAfterStore || prefetchClobbersValue )
			{
				if( !store->storedValueIsFloatRegister )
				{
					addSuffixStoreDrainBlocker( opportunity, "non_vf_store_value_rotation" );
					continue;
				}
				store->requiresValueRotation = true;
				if( prefetchClobbersValue && store->tokenIndex > opportunity.qConsumerTokenIndices.back() )
					store->rotateValueBeforePrefetch = true;
				else
					store->rotateValueAtStore = true;
				if( !assignOneSuffixStoreValueScratchRegister( *store, used ) )
					addSuffixStoreDrainBlocker( opportunity, "missing_suffix_store_value_scratch" );
			}
			store->delayedDrain = true;
		}

		for( std::vector<VuSoftwarePipelineSuffixStore>::const_iterator store =
		         opportunity.softwarePipelineSuffixStores.begin();
		     store != opportunity.softwarePipelineSuffixStores.end(); ++store )
		{
			if( !store->delayedDrain )
				continue;
			if( tokenRangeHasMemoryDependencyWithStore( *store,
			                                           store->tokenIndex + 1,
			                                           opportunity.branchTokenIndex,
			                                           indexedTokens,
			                                           opportunity.softwarePipelineSuffixStores ) )
				addSuffixStoreDrainBlocker( opportunity, "delayed_store_memory_dependency" );
		}

		if( !opportunity.suffixStoreDrainBlockers.empty() )
		{
			for( std::vector<VuSoftwarePipelineSuffixStore>::iterator store =
			         opportunity.softwarePipelineSuffixStores.begin();
			     store != opportunity.softwarePipelineSuffixStores.end(); ++store )
			{
				store->delayedDrain = false;
				store->rotateValueBeforePrefetch = false;
				store->rotateValueAtStore = false;
			}
			return;
		}

		opportunity.canEmitSuffixStoreDrain = true;
	}

	const VuSoftwarePipelineRotation* findRotationForBase( const std::vector<VuSoftwarePipelineRotation>& rotations,
	                                                       const std::string& base )
	{
		for( std::vector<VuSoftwarePipelineRotation>::const_iterator i = rotations.begin(); i != rotations.end(); ++i )
		{
			if( i->registerBase == base )
				return &*i;
		}
		return NULL;
	}

	const VuLoopInductionUpdate* findInductionUpdate( const std::vector<VuLoopInductionUpdate>& updates,
	                                                  const std::string& registerName )
	{
		for( std::vector<VuLoopInductionUpdate>::const_iterator i = updates.begin(); i != updates.end(); ++i )
		{
			if( i->registerName == registerName )
				return &*i;
		}
		return NULL;
	}

	bool aggregateInductionStepAfterToken( const std::vector<VuLoopInductionUpdate>& updates,
	                                       const std::string& registerName,
	                                       unsigned int tokenIndex,
	                                       long& step )
	{
		bool found = false;
		step = 0;
		for( std::vector<VuLoopInductionUpdate>::const_iterator i = updates.begin();
		     i != updates.end(); ++i )
		{
			if( i->registerName != registerName || i->tokenIndex <= tokenIndex )
				continue;
			if( !i->stepKnown )
				return false;
			step += i->step;
			found = true;
		}
		return found;
	}

	void appendSoftwarePipelinePrefetchDescriptor( const Token& token,
	                                               unsigned int tokenIndex,
	                                               const VuLoopPipelineOpportunity& opportunity,
	                                               std::vector<VuSoftwarePipelinePrefetch>& prefetches )
	{
		VuSoftwarePipelinePrefetch prefetch;
		prefetch.tokenIndex = tokenIndex;
		prefetch.mnemonic = lowerVuTokenName( token );
		prefetch.memoryKind = VU_MEMORY_NONE;
		prefetch.memoryFlags = VU_MEMORY_FLAG_NONE;
		prefetch.hasMemoryBase = false;
		prefetch.memoryBaseRegister = "";
		prefetch.hasMemoryOffset = false;
		prefetch.memoryOffset = 0;
		prefetch.readsInductionRegister = false;
		prefetch.inductionRegister = "";
		prefetch.hasNextIterationOffset = false;
		prefetch.nextIterationOffset = 0;

		VuTokenResourceAccess access;
		if( buildVuTokenResourceAccess( token, access ) )
		{
			prefetch.memoryKind = access.memoryKind;
			prefetch.memoryFlags = access.memoryFlags;
			prefetch.hasMemoryBase = access.hasMemoryBase;
			prefetch.memoryBaseRegister = access.memoryBaseRegister;
			prefetch.hasMemoryOffset = access.hasMemoryOffset;
			prefetch.memoryOffset = access.memoryOffset;

			for( std::list<std::string>::const_iterator i = opportunity.inductionRegisters.begin();
			     i != opportunity.inductionRegisters.end(); ++i )
			{
				if( containsKey( access.registerReads, *i )
				    || (access.hasMemoryBase && access.memoryBaseRegister == *i) )
				{
					prefetch.readsInductionRegister = true;
					prefetch.inductionRegister = *i;
					break;
				}
			}

			const VuLoopInductionUpdate* update =
			    access.hasMemoryBase
			    ? findInductionUpdate( opportunity.inductionUpdates, access.memoryBaseRegister )
			    : NULL;
			if( update && update->stepKnown && access.hasMemoryOffset )
			{
				prefetch.hasNextIterationOffset = true;
				prefetch.nextIterationOffset = access.memoryOffset + update->step;
			}
		}

		prefetches.push_back( prefetch );
	}

	void appendSoftwarePipelineSuffixStoreDescriptor( const Token& token,
	                                                  unsigned int tokenIndex,
	                                                  const VuLoopPipelineOpportunity& opportunity,
	                                                  std::vector<VuSoftwarePipelineSuffixStore>& suffixStores )
	{
		VuSoftwarePipelineSuffixStore suffixStore;
		suffixStore.tokenIndex = tokenIndex;
		suffixStore.mnemonic = lowerVuTokenName( token );
		suffixStore.hasMemoryBase = false;
		suffixStore.memoryBaseRegister = "";
		suffixStore.hasMemoryOffset = false;
		suffixStore.memoryOffset = 0;
		suffixStore.usesInductionRegister = false;
		suffixStore.inductionRegister = "";
		suffixStore.hasNextIterationOffset = false;
		suffixStore.nextIterationOffset = 0;
		suffixStore.drainCandidate = false;
		suffixStore.hasStoredValueRegister = false;
		suffixStore.storedValueIsFloatRegister = false;
		suffixStore.storedValueRegister = "";
		suffixStore.requiresValueRotation = false;
		suffixStore.hasValueScratchRegister = false;
		suffixStore.valueScratchRegister = "";
		suffixStore.delayedDrain = false;
		suffixStore.rotateValueBeforePrefetch = false;
		suffixStore.rotateValueAtStore = false;

		VuTokenResourceAccess access;
		if( buildVuTokenResourceAccess( token, access )
		    && access.memoryKind == VU_MEMORY_STORE
		    && access.memoryFlags == VU_MEMORY_FLAG_NONE )
		{
			suffixStore.hasMemoryBase = access.hasMemoryBase;
			suffixStore.memoryBaseRegister = access.memoryBaseRegister;
			suffixStore.hasMemoryOffset = access.hasMemoryOffset;
			suffixStore.memoryOffset = access.memoryOffset;

			for( std::list<std::string>::const_iterator i = opportunity.inductionRegisters.begin();
			     i != opportunity.inductionRegisters.end(); ++i )
			{
				if( access.hasMemoryBase && access.memoryBaseRegister == *i )
				{
					suffixStore.usesInductionRegister = true;
					suffixStore.inductionRegister = *i;
					break;
				}
			}

			long stepAfterStore = 0;
			if( access.hasMemoryBase
			    && access.hasMemoryOffset
			    && aggregateInductionStepAfterToken( opportunity.inductionUpdates,
			                                         access.memoryBaseRegister,
			                                         tokenIndex,
			                                         stepAfterStore ) )
			{
				suffixStore.hasNextIterationOffset = true;
				suffixStore.nextIterationOffset = access.memoryOffset - stepAfterStore;
				suffixStore.drainCandidate = suffixStore.usesInductionRegister;
			}

			suffixStore.hasStoredValueRegister =
			    describeStoreValueRegister( token,
			                                suffixStore.storedValueRegister,
			                                suffixStore.storedValueFields,
			                                suffixStore.storedValueIsFloatRegister );
		}

		suffixStores.push_back( suffixStore );
	}

	bool isIbeqOrIbne( const Token& token )
	{
		const std::string name = lowerVuTokenName( token );
		return name == "ibeq" || name == "ibne";
	}

	bool branchCanInvertToDrain( const Token& token )
	{
		if( !isIbeqOrIbne( token ) )
			return false;

		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess( token, access ) )
			return false;
		if( (access.instructionFlags & VU_INSTR_BRANCH) == 0 )
			return false;
		if( access.instructionFlags & (VU_INSTR_UNCONDITIONAL_BRANCH
		                             | VU_INSTR_LINK_BRANCH
		                             | VU_INSTR_REGISTER_BRANCH) )
			return false;

		std::string target;
		return branchTargetLabel( token, target );
	}

	const Operand* syntheticIbeqOperand()
	{
		static const Operand ibeqOperand( "IBEQ",
		                                  3,
		                                  Operand::LOWER | Operand::DYNAMIC,
		                                  "vi,vi,imm:branch",
		                                  Operand::BRU,
		                                  2,
		                                  2 );
		return &ibeqOperand;
	}

	const Operand* syntheticIbneOperand()
	{
		static const Operand ibneOperand( "IBNE",
		                                  3,
		                                  Operand::LOWER | Operand::DYNAMIC,
		                                  "vi,vi,imm:branch",
		                                  Operand::BRU,
		                                  2,
		                                  2 );
		return &ibneOperand;
	}

	Token invertedBranchToDrainToken( const Token& token, const std::string& drainLabel )
	{
		Token branch( token );
		branch.setLabel( "" );
		const std::string name = lowerVuTokenName( branch );
		if( name == "ibne" )
		{
			branch.setName( "ibeq" );
			branch.setOperand( syntheticIbeqOperand() );
		}
		else if( name == "ibeq" )
		{
			branch.setName( "ibne" );
			branch.setOperand( syntheticIbneOperand() );
		}

		for( std::list<Token::Argument>::iterator i = branch.arguments().begin();
		     i != branch.arguments().end(); ++i )
		{
			if( ((*i).flags() & Token::Argument::BRANCH)
			    && (*i).type() == Token::Argument::IMMEDIATE )
				i->setImmediate( drainLabel );
		}

		return branch;
	}

	bool delayedSuffixStoreForTokenIndex( const std::vector<VuSoftwarePipelineSuffixStore>& stores,
	                                      unsigned int tokenIndex )
	{
		for( std::vector<VuSoftwarePipelineSuffixStore>::const_iterator i = stores.begin();
		     i != stores.end(); ++i )
		{
			if( i->tokenIndex == tokenIndex )
				return i->delayedDrain;
		}
		return false;
	}

	bool tokenRangeHasMemoryDependencyWithStore( const VuSoftwarePipelineSuffixStore& store,
	                                             unsigned int beginIndex,
	                                             unsigned int endIndex,
	                                             const std::vector<const Token*>& indexedTokens,
	                                             const std::vector<VuSoftwarePipelineSuffixStore>& stores )
	{
		if( store.tokenIndex >= indexedTokens.size() )
			return true;

		VuTokenResourceAccess storeAccess;
		if( !buildVuTokenResourceAccess( *indexedTokens[store.tokenIndex], storeAccess ) )
			return true;

		for( unsigned int tokenIndex = beginIndex; tokenIndex <= endIndex && tokenIndex < indexedTokens.size(); ++tokenIndex )
		{
			if( tokenIndex == store.tokenIndex || delayedSuffixStoreForTokenIndex( stores, tokenIndex ) )
				continue;

			VuTokenResourceAccess access;
			if( !buildVuTokenResourceAccess( *indexedTokens[tokenIndex], access ) )
				continue;
			if( memoryOrderRequiresDependency( storeAccess,
			                                  access,
			                                  *indexedTokens[store.tokenIndex],
			                                  *indexedTokens[tokenIndex] )
			    || memoryOrderRequiresDependency( access,
			                                     storeAccess,
			                                     *indexedTokens[tokenIndex],
			                                     *indexedTokens[store.tokenIndex] ) )
				return true;
		}

		return false;
	}

	void collectTokenWriteKeys( const std::vector<unsigned int>& tokenIndices,
	                            const std::vector<const Token*>& indexedTokens,
	                            std::list<std::string>& writes )
	{
		for( std::vector<unsigned int>::const_iterator i = tokenIndices.begin(); i != tokenIndices.end(); ++i )
		{
			if( *i >= indexedTokens.size() )
				continue;
			std::list<std::string> tokenWrites;
			collectVuRegisterWriteKeys( *indexedTokens[*i], tokenWrites );
			for( std::list<std::string>::const_iterator write = tokenWrites.begin(); write != tokenWrites.end(); ++write )
				addUniqueString( writes, *write );
		}
	}

	void collectTokenWriteBaseKeys( const std::vector<unsigned int>& tokenIndices,
	                                const std::vector<const Token*>& indexedTokens,
	                                std::list<std::string>& bases )
	{
		std::list<std::string> writes;
		collectTokenWriteKeys( tokenIndices, indexedTokens, writes );
		collectRotatedRegisterBaseKeys( writes, bases );
	}

	void retainPrefetchedRotations( VuLoopPipelineOpportunity& opportunity,
	                                const std::vector<const Token*>& indexedTokens )
	{
		std::vector<unsigned int> prefetchTokenIndices;
		for( std::vector<VuSoftwarePipelinePrefetch>::const_iterator i = opportunity.softwarePipelinePrefetches.begin();
		     i != opportunity.softwarePipelinePrefetches.end(); ++i )
			prefetchTokenIndices.push_back( i->tokenIndex );

		std::list<std::string> prefetchWriteBases;
		collectTokenWriteBaseKeys( prefetchTokenIndices, indexedTokens, prefetchWriteBases );

		for( std::vector<VuSoftwarePipelineRotation>::iterator i = opportunity.softwarePipelineRotations.begin();
		     i != opportunity.softwarePipelineRotations.end(); )
		{
			if( containsKey( prefetchWriteBases, i->registerBase ) )
				++i;
			else
				i = opportunity.softwarePipelineRotations.erase( i );
		}

		for( std::list<std::string>::iterator i = opportunity.softwarePipelineRotatedRegisters.begin();
		     i != opportunity.softwarePipelineRotatedRegisters.end(); )
		{
			if( containsKey( prefetchWriteBases, *i ) )
				++i;
			else
				i = opportunity.softwarePipelineRotatedRegisters.erase( i );
		}
	}

	bool rotationBaseListContains( const std::list<std::string>& bases, const std::string& base )
	{
		return containsKey( bases, base );
	}

	bool scheduledBlockLabel( const VuScheduledBasicBlock& block, std::string& label )
	{
		for( std::vector<const Token*>::const_iterator i = block.block.tokens.begin();
		     i != block.block.tokens.end(); ++i )
		{
			if( (*i)->label().length() != 0 )
			{
				label = (*i)->label();
				return true;
			}
		}
		return false;
	}

	bool softwarePipelineRotationsCanEmit( const VuLoopPipelineOpportunity& opportunity,
	                                       const std::vector<const Token*>& indexedTokens )
	{
		if( opportunity.softwarePipelineRotations.empty() )
			return true;

		std::vector<unsigned int> prefetchTokenIndices;
		for( std::vector<VuSoftwarePipelinePrefetch>::const_iterator i = opportunity.softwarePipelinePrefetches.begin();
		     i != opportunity.softwarePipelinePrefetches.end(); ++i )
			prefetchTokenIndices.push_back( i->tokenIndex );

		std::list<std::string> prefetchWrites;
		std::list<std::string> prefetchWriteBases;
		collectTokenWriteKeys( prefetchTokenIndices, indexedTokens, prefetchWrites );
		collectRotatedRegisterBaseKeys( prefetchWrites, prefetchWriteBases );

		for( std::vector<VuSoftwarePipelineRotation>::const_iterator i = opportunity.softwarePipelineRotations.begin();
		     i != opportunity.softwarePipelineRotations.end(); ++i )
		{
			if( !i->hasScratchRegister )
				return false;
			if( !rotationBaseListContains( prefetchWriteBases, i->registerBase ) )
				return false;
		}

		if( opportunity.qConsumerTokenIndices.empty()
		    || opportunity.qConsumerTokenIndices.back() + 1 > opportunity.branchTokenIndex )
			return true;

		std::list<std::string> rotatedBases;
		for( std::vector<VuSoftwarePipelineRotation>::const_iterator i = opportunity.softwarePipelineRotations.begin();
		     i != opportunity.softwarePipelineRotations.end(); ++i )
			addUniqueString( rotatedBases, i->registerBase );

		for( unsigned int tokenIndex = opportunity.qConsumerTokenIndices.back() + 1;
		     tokenIndex <= opportunity.branchTokenIndex && tokenIndex < indexedTokens.size(); ++tokenIndex )
		{
			std::list<std::string> writes;
			std::list<std::string> writeBases;
			collectVuRegisterWriteKeys( *indexedTokens[tokenIndex], writes );
			collectRotatedRegisterBaseKeys( writes, writeBases );
			for( std::list<std::string>::const_iterator i = writeBases.begin(); i != writeBases.end(); ++i )
			{
				if( containsKey( rotatedBases, *i ) )
					return false;
			}
		}

		return true;
	}

	void removeRotationCoveredWrites( std::list<std::string>& writes,
	                                  const std::vector<VuSoftwarePipelineRotation>& rotations )
	{
		for( std::list<std::string>::iterator i = writes.begin(); i != writes.end(); )
		{
			if( findRotationForBase( rotations, registerBaseKey( *i ) ) )
				i = writes.erase( i );
			else
				++i;
		}
	}

	void collectSoftwarePipelinePrefetchWrites( const VuLoopPipelineOpportunity& opportunity,
	                                            const std::vector<const Token*>& indexedTokens,
	                                            bool rotationsWillUseScratch,
	                                            std::list<std::string>& prefetchWrites )
	{
		std::vector<unsigned int> prefetchTokenIndices;
		for( std::vector<VuSoftwarePipelinePrefetch>::const_iterator i = opportunity.softwarePipelinePrefetches.begin();
		     i != opportunity.softwarePipelinePrefetches.end(); ++i )
			prefetchTokenIndices.push_back( i->tokenIndex );

		collectTokenWriteKeys( prefetchTokenIndices, indexedTokens, prefetchWrites );
		if( rotationsWillUseScratch )
			removeRotationCoveredWrites( prefetchWrites, opportunity.softwarePipelineRotations );
	}

	const VuSoftwarePipelineSuffixStore* findSuffixStoreForTokenIndex(
	    const std::vector<VuSoftwarePipelineSuffixStore>& stores,
	    unsigned int tokenIndex )
	{
		for( std::vector<VuSoftwarePipelineSuffixStore>::const_iterator i = stores.begin();
		     i != stores.end(); ++i )
		{
			if( i->tokenIndex == tokenIndex )
				return &*i;
		}
		return NULL;
	}

	bool suffixStoreRotationCoversReads( const VuSoftwarePipelineSuffixStore& store,
	                                     const VuTokenResourceAccess& access,
	                                     const std::list<std::string>& clobberedKeys )
	{
		if( !store.requiresValueRotation || !store.hasValueScratchRegister )
			return false;
		if( intersects( access.registerWrites, clobberedKeys ) )
			return false;

		std::list<std::string> valueKeys;
		collectSuffixStoreValueKeys( store, valueKeys );
		for( std::list<std::string>::const_iterator read = access.registerReads.begin();
		     read != access.registerReads.end(); ++read )
		{
			if( containsKey( clobberedKeys, *read ) && !containsKey( valueKeys, *read ) )
				return false;
		}

		return true;
	}

	bool prefetchWritesClobberSuffix( const VuLoopPipelineOpportunity& opportunity,
	                                  const std::vector<const Token*>& indexedTokens,
	                                  const std::list<std::string>& prefetchWrites )
	{
		if( prefetchWrites.empty() )
			return false;

		const unsigned int insertAfter = opportunity.qConsumerTokenIndices.back();
		if( insertAfter + 1 > opportunity.branchTokenIndex )
			return false;

		for( unsigned int tokenIndex = insertAfter + 1;
		     tokenIndex <= opportunity.branchTokenIndex && tokenIndex < indexedTokens.size();
		     ++tokenIndex )
		{
			VuTokenResourceAccess access;
			if( !buildVuTokenResourceAccess( *indexedTokens[tokenIndex], access ) )
				continue;
			if( !intersects( access.registerReads, prefetchWrites )
			    && !intersects( access.registerWrites, prefetchWrites ) )
				continue;

			const VuSoftwarePipelineSuffixStore* store =
			    findSuffixStoreForTokenIndex( opportunity.softwarePipelineSuffixStores, tokenIndex );
			if( store && suffixStoreRotationCoversReads( *store, access, prefetchWrites ) )
				continue;
			return true;
		}

		return false;
	}

	bool softwarePipelinePrefetchesCanEmit( const VuLoopPipelineOpportunity& opportunity,
	                                        const std::vector<const Token*>& indexedTokens,
	                                        bool rotationsWillUseScratch )
	{
		for( std::vector<VuSoftwarePipelinePrefetch>::const_iterator i = opportunity.softwarePipelinePrefetches.begin();
		     i != opportunity.softwarePipelinePrefetches.end(); ++i )
		{
			if( i->memoryKind != VU_MEMORY_NONE && i->memoryKind != VU_MEMORY_LOAD )
				return false;
			if( i->memoryFlags != VU_MEMORY_FLAG_NONE )
				return false;
			if( i->readsInductionRegister && i->memoryKind != VU_MEMORY_LOAD )
				return false;
			if( i->readsInductionRegister && !i->hasNextIterationOffset )
				return false;
		}

		if( opportunity.qConsumerTokenIndices.empty() )
			return false;

		std::list<std::string> prefetchWrites;
		collectSoftwarePipelinePrefetchWrites( opportunity,
		                                       indexedTokens,
		                                       rotationsWillUseScratch,
		                                       prefetchWrites );
		return !prefetchWritesClobberSuffix( opportunity, indexedTokens, prefetchWrites );
	}

	bool softwarePipelineQDrainCanEmit( const VuLoopPipelineOpportunity& opportunity,
	                                    const VuLoopCandidate& loop,
	                                    unsigned int qProducerOffset )
	{
		if( qProducerOffset >= loop.bodyTokens.size() )
			return false;
		if( !opportunity.softwarePipelinePrefetches.empty()
		    || !opportunity.softwarePipelineRotations.empty() )
			return false;

		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess( *loop.bodyTokens[qProducerOffset], access ) )
			return false;
		if( access.memoryKind != VU_MEMORY_NONE )
			return false;
		if( intersects( access.registerReads, opportunity.inductionRegisters ) )
			return false;
		if( intersects( access.registerReads, opportunity.loopReadWriteRegisters ) )
			return false;

		return true;
	}

	bool qProducerCanMoveIntoBranchDelaySlot( const VuLoopPipelineOpportunity& opportunity,
	                                          const std::vector<const Token*>& indexedTokens,
	                                          unsigned int* suffixBlockerTokenIndex )
	{
		if( suffixBlockerTokenIndex )
			*suffixBlockerTokenIndex = 0;
		if( opportunity.qLiveOut
		    || opportunity.qConsumerTokenIndices.empty()
		    || opportunity.qProducerTokenIndex >= indexedTokens.size()
		    || opportunity.branchTokenIndex >= indexedTokens.size() )
			return false;

		const Token& qProducer = *indexedTokens[opportunity.qProducerTokenIndex];
		const Token& branch = *indexedTokens[opportunity.branchTokenIndex];
		if( vuTokenBranchDelaySlots( branch ) != 1 )
			return false;
		if( !qProducer.operand() || !qProducer.operand()->isLowerExecutionPath() )
			return false;

		VuTokenResourceAccess qProducerAccess;
		VuTokenResourceAccess branchAccess;
		if( !buildVuTokenResourceAccess( qProducer, qProducerAccess )
		    || !buildVuTokenResourceAccess( branch, branchAccess ) )
			return false;
		if( branchAccess.instructionFlags & (VU_INSTR_LINK_BRANCH | VU_INSTR_REGISTER_BRANCH) )
			return false;
		if( qProducerAccess.memoryKind != VU_MEMORY_NONE
		    || qProducerAccess.memoryFlags != VU_MEMORY_FLAG_NONE
		    || qProducerAccess.branchDelaySlots > 0 )
			return false;
		if( qProducerAccess.instructionFlags & (VU_INSTR_BRANCH
		                                      | VU_INSTR_WAIT_Q
		                                      | VU_INSTR_WAIT_P
		                                      | VU_INSTR_XGKICK) )
			return false;
		if( (qProducerAccess.instructionFlags & (VU_INSTR_WRITES_Q | VU_INSTR_WRITES_P)) == 0 )
			return false;

		if( intersects( qProducerAccess.registerWrites, branchAccess.registerReads )
		    || intersects( qProducerAccess.registerWrites, branchAccess.registerWrites )
		    || intersects( qProducerAccess.registerReads, branchAccess.registerWrites ) )
			return false;
		if( qProducerAccess.implicitWrites & (branchAccess.implicitReads | branchAccess.implicitWrites) )
			return false;
		if( branchAccess.implicitWrites & (qProducerAccess.implicitReads | qProducerAccess.implicitWrites) )
			return false;

		for( unsigned int tokenIndex = opportunity.qConsumerTokenIndices.back() + 1;
		     tokenIndex < opportunity.branchTokenIndex && tokenIndex < indexedTokens.size();
		     ++tokenIndex )
		{
			// Keep every suffix instruction before the branch unless it cannot safely
			// stay ahead of the cloned next-iteration Q producer.
			if( !vuTokenCanMoveBefore( *indexedTokens[tokenIndex], qProducer, VU_RESOURCE_NONE ) )
			{
				if( suffixBlockerTokenIndex )
					*suffixBlockerTokenIndex = tokenIndex;
				return false;
			}
		}

		return true;
	}

	void collectSoftwarePipelinePrefetchDescriptors( const std::vector<unsigned int>& prologTokenIndices,
	                                                unsigned int qProducerTokenIndex,
	                                                const std::vector<const Token*>& indexedTokens,
	                                                VuLoopPipelineOpportunity& opportunity )
	{
		for( std::vector<unsigned int>::const_iterator i = prologTokenIndices.begin();
		     i != prologTokenIndices.end(); ++i )
		{
			if( *i == qProducerTokenIndex || *i >= indexedTokens.size() )
				continue;
			appendSoftwarePipelinePrefetchDescriptor( *indexedTokens[*i],
			                                          *i,
			                                          opportunity,
			                                          opportunity.softwarePipelinePrefetches );
		}
	}

	void collectSoftwarePipelineSuffixStoreDescriptors( const std::vector<unsigned int>& tokenIndices,
	                                                   const std::vector<const Token*>& indexedTokens,
	                                                   VuLoopPipelineOpportunity& opportunity )
	{
		for( std::vector<unsigned int>::const_iterator i = tokenIndices.begin();
		     i != tokenIndices.end(); ++i )
		{
			if( *i >= indexedTokens.size() )
				continue;

			VuTokenResourceAccess access;
			if( !buildVuTokenResourceAccess( *indexedTokens[*i], access )
			    || access.memoryKind != VU_MEMORY_STORE
			    || access.memoryFlags != VU_MEMORY_FLAG_NONE )
				continue;

			appendSoftwarePipelineSuffixStoreDescriptor( *indexedTokens[*i],
			                                             *i,
			                                             opportunity,
			                                             opportunity.softwarePipelineSuffixStores );
		}
	}

	void collectAdjustedCyclicPrefixWriteKeys( const VuLoopPipelineOpportunity& opportunity,
	                                           const std::vector<const Token*>& indexedTokens,
	                                           std::list<std::string>& writes )
	{
		for( std::vector<unsigned int>::const_iterator i =
		         opportunity.multiQCyclicPrefixTokenIndices.begin();
		     i != opportunity.multiQCyclicPrefixTokenIndices.end(); ++i )
		{
			if( *i >= indexedTokens.size() )
				continue;
			const Token adjusted =
			    adjustedMultiQCyclicPrefixToken( *indexedTokens[*i],
			                                     opportunity.multiQCyclicPrefixRotations,
			                                     opportunity.inductionUpdates,
			                                     opportunity.multiQCyclicPrefixInsertBeforeTokenIndex );
			std::list<std::string> tokenWrites;
			collectVuRegisterWriteKeys( adjusted, tokenWrites );
			for( std::list<std::string>::const_iterator write = tokenWrites.begin();
			     write != tokenWrites.end(); ++write )
				addUniqueString( writes, *write );
		}
	}

	bool tokenIndicesLoadFromMemoryBase( const std::vector<unsigned int>& tokenIndices,
	                                     const std::vector<const Token*>& indexedTokens,
	                                     const std::string& memoryBaseRegister )
	{
		for( std::vector<unsigned int>::const_iterator i = tokenIndices.begin();
		     i != tokenIndices.end(); ++i )
		{
			if( *i >= indexedTokens.size() )
				continue;
			VuTokenResourceAccess access;
			if( !buildVuTokenResourceAccess( *indexedTokens[*i], access ) )
				continue;
			if( access.memoryKind == VU_MEMORY_LOAD
			    && access.hasMemoryBase
			    && access.memoryBaseRegister == memoryBaseRegister )
				return true;
		}
		return false;
	}

	bool suffixStoreHasReadWriteMemoryStreamConflict( const VuSoftwarePipelineSuffixStore& store,
	                                                  const VuLoopPipelineOpportunity& opportunity,
	                                                  const std::vector<const Token*>& indexedTokens )
	{
		if( !store.hasMemoryBase )
			return true;
		return tokenIndicesLoadFromMemoryBase( opportunity.multiQMainTokenIndices,
		                                       indexedTokens,
		                                       store.memoryBaseRegister )
		    || tokenIndicesLoadFromMemoryBase( opportunity.multiQCyclicPrefixTokenIndices,
		                                       indexedTokens,
		                                       store.memoryBaseRegister );
	}

	bool loadedMultiQDrainLeavesPartialStoreStream(
	    const std::vector<VuSoftwarePipelineSuffixStore>& stores )
	{
		std::list<std::string> delayedBases;
		for( std::vector<VuSoftwarePipelineSuffixStore>::const_iterator store =
		         stores.begin(); store != stores.end(); ++store )
		{
			if( store->delayedDrain && store->hasMemoryBase )
				addUniqueString( delayedBases, store->memoryBaseRegister );
		}
		if( delayedBases.empty() )
			return false;

		for( std::vector<VuSoftwarePipelineSuffixStore>::const_iterator store =
		         stores.begin(); store != stores.end(); ++store )
		{
			if( store->drainCandidate
			    && store->hasMemoryBase
			    && containsKey( delayedBases, store->memoryBaseRegister )
			    && !store->delayedDrain )
				return true;
		}

		return false;
	}

	void clearSuffixStoreDrainSelection( std::vector<VuSoftwarePipelineSuffixStore>& stores )
	{
		for( std::vector<VuSoftwarePipelineSuffixStore>::iterator store =
		         stores.begin(); store != stores.end(); ++store )
		{
			store->requiresValueRotation = false;
			store->hasValueScratchRegister = false;
			store->valueScratchRegister = "";
			store->delayedDrain = false;
			store->rotateValueBeforePrefetch = false;
			store->rotateValueAtStore = false;
		}
	}

	void classifyMultiQCyclicPrefixSuffixStoreDrains( VuLoopPipelineOpportunity& opportunity,
	                                                  const VuLoopCandidate& loop,
	                                                  const std::vector<const Token*>& indexedTokens )
	{
		opportunity.hasSuffixStoreDrainPlan = false;
		opportunity.canEmitSuffixStoreDrain = false;
		opportunity.suffixStoreDrainBlockers.clear();
		opportunity.softwarePipelineSuffixStores.clear();

		collectSoftwarePipelineSuffixStoreDescriptors( opportunity.multiQMainTokenIndices,
		                                               indexedTokens,
		                                               opportunity );
		if( opportunity.softwarePipelineSuffixStores.empty() )
			return;

		bool hasDrainCandidate = false;
		for( std::vector<VuSoftwarePipelineSuffixStore>::const_iterator i =
		         opportunity.softwarePipelineSuffixStores.begin();
		     i != opportunity.softwarePipelineSuffixStores.end(); ++i )
		{
			if( i->drainCandidate )
				hasDrainCandidate = true;
		}
		if( !hasDrainCandidate )
			return;

		opportunity.hasSuffixStoreDrainPlan = true;
		if( opportunity.branchTokenIndex >= indexedTokens.size()
		    || !branchCanInvertToDrain( *indexedTokens[opportunity.branchTokenIndex] ) )
			addSuffixStoreDrainBlocker( opportunity, "non_invertible_loop_branch" );
		if( opportunity.qLiveOut )
			addSuffixStoreDrainBlocker( opportunity, "q_live_out" );
		if( !opportunity.suffixStoreDrainBlockers.empty() )
			return;

		std::list<std::string> cyclicPrefixWrites;
		collectAdjustedCyclicPrefixWriteKeys( opportunity, indexedTokens, cyclicPrefixWrites );
		std::list<std::string> used;
		collectUsedScratchVfRegisters( loop,
		                               opportunity.multiQCyclicPrefixRotations,
		                               opportunity.softwarePipelineSuffixStores,
		                               used );

		bool readWriteMemoryStreamConflict = false;
		for( std::vector<VuSoftwarePipelineSuffixStore>::iterator store =
		         opportunity.softwarePipelineSuffixStores.begin();
		     store != opportunity.softwarePipelineSuffixStores.end(); ++store )
		{
			if( !store->drainCandidate )
				continue;
			if( !store->hasNextIterationOffset )
				continue;
			if( !store->hasStoredValueRegister )
				continue;
			if( store->tokenIndex >= indexedTokens.size() )
				continue;
			if( suffixStoreHasReadWriteMemoryStreamConflict( *store,
			                                                 opportunity,
			                                                 indexedTokens ) )
			{
				readWriteMemoryStreamConflict = true;
				continue;
			}
			if( indexedTokens[store->tokenIndex]->flags() & (Token::PREORDERED | Token::E | Token::D
			                                                | Token::T | Token::BRANCH_DELAY_FILLER) )
				continue;

			if( tokenRangeHasMemoryDependencyWithStore( *store,
			                                           store->tokenIndex + 1,
			                                           opportunity.branchTokenIndex,
			                                           indexedTokens,
			                                           opportunity.softwarePipelineSuffixStores ) )
				continue;

			std::list<std::string> valueKeys;
			collectSuffixStoreValueKeys( *store, valueKeys );
			const bool valueWrittenAfterStore =
			    store->tokenIndex + 1 <= opportunity.branchTokenIndex
			    && tokenRangeWritesAny( store->tokenIndex + 1,
			                            opportunity.branchTokenIndex,
			                            indexedTokens,
			                            valueKeys );
			const bool cyclicPrefixClobbersValue =
			    suffixStoreNeedsValueRotation( *store, cyclicPrefixWrites );
			if( cyclicPrefixClobbersValue
			    && store->tokenIndex >= opportunity.multiQCyclicPrefixInsertBeforeTokenIndex )
				continue;
			if( valueWrittenAfterStore || cyclicPrefixClobbersValue )
			{
				if( !store->storedValueIsFloatRegister )
					continue;
				store->requiresValueRotation = true;
				store->rotateValueAtStore = true;
				if( !assignOneSuffixStoreValueScratchRegister( *store, used ) )
					continue;
			}
			store->delayedDrain = true;
		}

		bool hasDelayedStore = false;
		unsigned int delayedStoreCount = 0;
		for( std::vector<VuSoftwarePipelineSuffixStore>::const_iterator store =
		         opportunity.softwarePipelineSuffixStores.begin();
		     store != opportunity.softwarePipelineSuffixStores.end(); ++store )
		{
			if( store->delayedDrain )
			{
				hasDelayedStore = true;
				++delayedStoreCount;
			}
		}
		if( hasDelayedStore
		    && delayedStoreCount > 1
		    && opportunity.memoryLoadCount > 0
		    && opportunity.qProducerTokenIndices.size() > 1 )
		{
			addSuffixStoreDrainBlocker( opportunity, "multi_store_loaded_suffix_stream" );
			addMultiQPipelineBlocker( opportunity, "multi_store_loaded_suffix_stream" );
			opportunity.eligibleMultiQSoftwarePipeline = false;
			opportunity.canEmitMultiQSoftwarePipeline = false;
			clearSuffixStoreDrainSelection( opportunity.softwarePipelineSuffixStores );
			hasDelayedStore = false;
		}
		if( hasDelayedStore
		    && opportunity.memoryLoadCount > 0
		    && opportunity.qProducerTokenIndices.size() > 1
		    && loadedMultiQDrainLeavesPartialStoreStream( opportunity.softwarePipelineSuffixStores ) )
		{
			addSuffixStoreDrainBlocker( opportunity, "partial_loaded_suffix_store_stream" );
			addMultiQPipelineBlocker( opportunity, "partial_loaded_suffix_store_stream" );
			opportunity.eligibleMultiQSoftwarePipeline = false;
			opportunity.canEmitMultiQSoftwarePipeline = false;
			clearSuffixStoreDrainSelection( opportunity.softwarePipelineSuffixStores );
			hasDelayedStore = false;
		}
		if( !hasDelayedStore && readWriteMemoryStreamConflict )
		{
			addSuffixStoreDrainBlocker( opportunity, "read_write_memory_stream_conflict" );
			if( opportunity.memoryLoadCount > 0 && opportunity.qProducerTokenIndices.size() > 1 )
			{
				addMultiQPipelineBlocker( opportunity, "read_write_memory_stream_conflict" );
				opportunity.eligibleMultiQSoftwarePipeline = false;
				opportunity.canEmitMultiQSoftwarePipeline = false;
			}
		}
		if( hasDelayedStore && opportunity.suffixStoreDrainBlockers.empty() )
			opportunity.canEmitSuffixStoreDrain = true;
	}

	bool tokenHasGuardableMultiQCyclicPrefixSideEffect( const Token& token )
	{
		if( token.flags() & (Token::PREORDERED | Token::BRANCH_DELAY_FILLER
		                   | Token::E | Token::D | Token::T) )
			return false;

		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess( token, access ) )
			return false;
		if( access.branchDelaySlots > 0 )
			return false;
		if( access.memoryFlags != VU_MEMORY_FLAG_NONE )
			return false;
		return access.memoryKind == VU_MEMORY_STORE;
	}

	bool tokenHasUnsafeMultiQCyclicPrefixSideEffect( const Token& token,
	                                                bool allowGuardedPlainStores )
	{
		if( token.flags() & (Token::PREORDERED | Token::BRANCH_DELAY_FILLER) )
			return true;

		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess( token, access ) )
			return true;
		if( access.branchDelaySlots > 0 )
			return true;
		if( access.memoryFlags & (VU_MEMORY_FLAG_PREDEC | VU_MEMORY_FLAG_POSTINC) )
			return true;
		if( allowGuardedPlainStores
		    && access.memoryKind == VU_MEMORY_STORE
		    && tokenHasGuardableMultiQCyclicPrefixSideEffect( token ) )
			return false;
		return access.memoryKind == VU_MEMORY_STORE
		    || access.memoryKind == VU_MEMORY_XGKICK;
	}

	bool tokenIndicesHaveUnsafeMultiQCyclicPrefixSideEffects( const std::vector<unsigned int>& tokenIndices,
	                                                          const std::vector<const Token*>& indexedTokens,
	                                                          bool allowGuardedPlainStores = false )
	{
		for( std::vector<unsigned int>::const_iterator i = tokenIndices.begin(); i != tokenIndices.end(); ++i )
		{
			if( *i >= indexedTokens.size()
			    || tokenHasUnsafeMultiQCyclicPrefixSideEffect( *indexedTokens[*i],
			                                                  allowGuardedPlainStores ) )
				return true;
		}
		return false;
	}

	bool tokenIndicesHaveGuardableMultiQCyclicPrefixSideEffects( const std::vector<unsigned int>& tokenIndices,
	                                                             const std::vector<const Token*>& indexedTokens )
	{
		for( std::vector<unsigned int>::const_iterator i = tokenIndices.begin(); i != tokenIndices.end(); ++i )
		{
			if( *i >= indexedTokens.size() )
				return false;
			if( tokenHasGuardableMultiQCyclicPrefixSideEffect( *indexedTokens[*i] ) )
				return true;
		}
		return false;
	}

	bool tokenIndicesContainVisibleLabel( const std::vector<unsigned int>& tokenIndices,
	                                      const std::vector<const Token*>& indexedTokens,
	                                      unsigned int allowedLabelTokenIndex )
	{
		for( std::vector<unsigned int>::const_iterator i = tokenIndices.begin(); i != tokenIndices.end(); ++i )
		{
			if( *i >= indexedTokens.size() )
				return true;
			if( *i != allowedLabelTokenIndex && indexedTokens[*i]->label().length() != 0 )
				return true;
		}
		return false;
	}

	bool loopBodyContainsVisibleInternalLabel( const VuLoopPipelineOpportunity& opportunity,
	                                           const std::vector<const Token*>& indexedTokens )
	{
		if( opportunity.labelTokenIndex >= indexedTokens.size()
		    || opportunity.branchTokenIndex >= indexedTokens.size()
		    || opportunity.labelTokenIndex + 1 >= opportunity.branchTokenIndex )
			return false;
		for( unsigned int i = opportunity.labelTokenIndex + 1; i < opportunity.branchTokenIndex; ++i )
		{
			if( indexedTokens[i]->label().length() != 0 )
				return true;
		}
		return false;
	}

	bool cyclicPrefixClobbersBranch( const VuLoopPipelineOpportunity& opportunity,
	                                 const std::vector<const Token*>& indexedTokens )
	{
		if( opportunity.branchTokenIndex >= indexedTokens.size() )
			return true;

		VuTokenResourceAccess branchAccess;
		if( !buildVuTokenResourceAccess( *indexedTokens[opportunity.branchTokenIndex], branchAccess ) )
			return true;

		std::list<std::string> prefixWrites;
		unsigned int prefixImplicitWrites = VU_RESOURCE_NONE;
		for( std::vector<unsigned int>::const_iterator i = opportunity.multiQCyclicPrefixTokenIndices.begin();
		     i != opportunity.multiQCyclicPrefixTokenIndices.end(); ++i )
		{
			if( *i >= indexedTokens.size() )
				return true;
			VuTokenResourceAccess access;
			if( !buildVuTokenResourceAccess( *indexedTokens[*i], access ) )
				return true;
			for( std::list<std::string>::const_iterator write = access.registerWrites.begin();
			     write != access.registerWrites.end(); ++write )
				addUniqueString( prefixWrites, *write );
			prefixImplicitWrites |= access.implicitWrites;
		}

		if( intersects( prefixWrites, branchAccess.registerReads )
		    || intersects( prefixWrites, branchAccess.registerWrites ) )
			return true;
		return (prefixImplicitWrites & (branchAccess.implicitReads | branchAccess.implicitWrites)) != 0;
	}

	bool cyclicPrefixReadsSuffixClobbers( const VuLoopPipelineOpportunity& opportunity,
	                                      const std::vector<const Token*>& indexedTokens );
	bool cyclicPrefixReadsSuffixClobbersBeforeInsertion( const VuLoopPipelineOpportunity& opportunity,
	                                                     const std::vector<const Token*>& indexedTokens );
	Token tokenWithoutLabel( const Token& token );
	bool setIndirectMemoryOffset( Token& token, const std::string& baseRegister, long offset );
	void rewriteRotatedRegistersToScratch( Token& token,
	                                       const std::vector<VuSoftwarePipelineRotation>& rotations );
	Token makeRotationMoveToken( const Token& donor,
	                             const VuSoftwarePipelineRotation& rotation );

	bool loopUsesLoopExtraDirective( const VuLoopCandidate& loop,
	                                 const std::vector<const Token*>& indexedTokens )
	{
		if( loop.labelTokenIndex >= indexedTokens.size()
		    || loop.branchTokenIndex >= indexedTokens.size()
		    || loop.labelTokenIndex + 1 >= loop.branchTokenIndex )
			return false;
		for( unsigned int i = loop.labelTokenIndex + 1; i < loop.branchTokenIndex; ++i )
		{
			if( i < indexedTokens.size() && tokenIsLoopExtraDirective( *indexedTokens[i] ) )
				return true;
		}
		return false;
	}

	void adjustMultiQCyclicPrefixInductionOffsets( Token& token,
	                                               const std::vector<VuLoopInductionUpdate>& inductionUpdates,
	                                               unsigned int insertionTokenIndex )
	{
		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess( token, access )
		    || access.memoryKind == VU_MEMORY_NONE
		    || !access.hasMemoryBase
		    || !access.hasMemoryOffset )
			return;

		for( std::vector<VuLoopInductionUpdate>::const_iterator i = inductionUpdates.begin();
		     i != inductionUpdates.end(); ++i )
		{
			if( !i->stepKnown
			    || i->registerName != access.memoryBaseRegister
			    || i->tokenIndex < insertionTokenIndex )
				continue;
			setIndirectMemoryOffset( token,
			                         i->registerName,
			                         access.memoryOffset + i->step );
			return;
		}
	}

	Token adjustedMultiQCyclicPrefixToken( const Token& token,
	                                       const std::vector<VuSoftwarePipelineRotation>& rotations,
	                                       const std::vector<VuLoopInductionUpdate>& inductionUpdates,
	                                       unsigned int insertionTokenIndex )
	{
		Token copy = tokenWithoutLabel( token );
		adjustMultiQCyclicPrefixInductionOffsets( copy,
		                                          inductionUpdates,
		                                          insertionTokenIndex );
		rewriteRotatedRegistersToScratch( copy, rotations );
		return copy;
	}

	Token adjustedMultiQCyclicPrefixToken( const Token& token,
	                                       const std::vector<VuSoftwarePipelineRotation>& rotations )
	{
		return adjustedMultiQCyclicPrefixToken( token,
		                                       rotations,
		                                       std::vector<VuLoopInductionUpdate>(),
		                                       VU_SCHEDULED_TOKEN_INDEX_NONE );
	}

	void collectFloatWriteRotationFields( const Token& token,
	                                      std::vector<VuSoftwarePipelineRotation>& rotations )
	{
		for( std::list<Token::Argument>::const_iterator i = token.arguments().begin();
		     i != token.arguments().end(); ++i )
		{
			if( i->type() != Token::Argument::FLOAT_REGISTER
			    || !(i->flags() & Token::Argument::WRITE)
			    || (i->flags() & Token::Argument::INDIRECT) )
				continue;
			std::string key;
			if( !vuRegisterKey( *i, key ) )
				continue;
			const unsigned int fields = vuWriteFieldMask( token, *i );
			if( fields & Token::X ) addRotationField( rotations, registerBaseKey( key ) + ".x", false );
			if( fields & Token::Y ) addRotationField( rotations, registerBaseKey( key ) + ".y", false );
			if( fields & Token::Z ) addRotationField( rotations, registerBaseKey( key ) + ".z", false );
			if( fields & Token::W ) addRotationField( rotations, registerBaseKey( key ) + ".w", false );
		}
	}

	bool prefixRotationsHaveNoReadBeforeWrite( const std::vector<unsigned int>& tokenIndices,
	                                           const std::vector<const Token*>& indexedTokens,
	                                           const std::vector<VuSoftwarePipelineRotation>& rotations )
	{
		std::list<std::string> producedBases;
		for( std::vector<unsigned int>::const_iterator tokenIndex = tokenIndices.begin();
		     tokenIndex != tokenIndices.end(); ++tokenIndex )
		{
			if( *tokenIndex >= indexedTokens.size() )
				return false;

			VuTokenResourceAccess access;
			if( !buildVuTokenResourceAccess( *indexedTokens[*tokenIndex], access ) )
				return false;

			for( std::list<std::string>::const_iterator read = access.registerReads.begin();
			     read != access.registerReads.end(); ++read )
			{
				const std::string base = registerBaseKey( *read );
				if( findRotationForBase( rotations, base )
				    && !containsKey( producedBases, base ) )
					return false;
			}

			for( std::list<std::string>::const_iterator write = access.registerWrites.begin();
			     write != access.registerWrites.end(); ++write )
			{
				const std::string base = registerBaseKey( *write );
				if( findRotationForBase( rotations, base ) )
					addUniqueString( producedBases, base );
			}
		}

		return true;
	}

	bool assignMultiQCyclicPrefixRotations( const VuLoopCandidate& loop,
	                                        const std::vector<const Token*>& indexedTokens,
	                                        VuLoopPipelineOpportunity& opportunity )
	{
		opportunity.multiQCyclicPrefixRotations.clear();
		for( std::vector<unsigned int>::const_iterator i = opportunity.multiQCyclicPrefixTokenIndices.begin();
		     i != opportunity.multiQCyclicPrefixTokenIndices.end(); ++i )
		{
			if( *i < indexedTokens.size() )
				collectFloatWriteRotationFields( *indexedTokens[*i],
				                                  opportunity.multiQCyclicPrefixRotations );
		}

		if( opportunity.multiQCyclicPrefixRotations.empty() )
			return false;
		if( !prefixRotationsHaveNoReadBeforeWrite( opportunity.multiQCyclicPrefixTokenIndices,
		                                           indexedTokens,
		                                           opportunity.multiQCyclicPrefixRotations ) )
		{
			opportunity.multiQCyclicPrefixRotations.clear();
			return false;
		}

		assignRotationScratchRegisters( loop, opportunity.multiQCyclicPrefixRotations );
		for( std::vector<VuSoftwarePipelineRotation>::const_iterator i =
		         opportunity.multiQCyclicPrefixRotations.begin();
		     i != opportunity.multiQCyclicPrefixRotations.end(); ++i )
		{
			if( !i->hasScratchRegister )
			{
				opportunity.multiQCyclicPrefixRotations.clear();
				return false;
			}
		}
		return true;
	}

	void assignMultiQPipelineCandidate( const VuLoopCandidate& loop,
	                                    unsigned int prefixEndOffset,
	                                    unsigned int mainBeginOffset,
	                                    unsigned int branchOffset,
	                                    VuLoopPipelineOpportunity& opportunity )
	{
		opportunity.multiQPrologTokenIndices.clear();
		opportunity.multiQMainTokenIndices.clear();
		opportunity.multiQCyclicPrefixTokenIndices.clear();
		opportunity.multiQCyclicPrefixRotations.clear();
		opportunity.multiQCyclicPrefixNeedsGuard = false;
		opportunity.multiQCyclicPrefixLastTokenInBranchDelaySlot = false;
		appendPipelineInstructionIndices( loop,
		                                  0,
		                                  prefixEndOffset,
		                                  opportunity.multiQPrologTokenIndices );
		opportunity.multiQCyclicPrefixTokenIndices = opportunity.multiQPrologTokenIndices;
		appendPipelineInstructionIndices( loop,
		                                  mainBeginOffset,
		                                  branchOffset + 1,
		                                  opportunity.multiQMainTokenIndices );
		opportunity.multiQCyclicPrefixInsertBeforeTokenIndex =
		    loop.firstBodyTokenIndex + branchOffset;
	}

	bool multiQPipelineCandidateStructurallySafe( const VuLoopPipelineOpportunity& candidate,
	                                              const std::vector<const Token*>& indexedTokens,
	                                              bool allowGuardedPlainStores = false,
	                                              bool checkSuffixClobbers = true )
	{
		if( candidate.multiQPrologTokenIndices.empty()
		    || candidate.multiQMainTokenIndices.empty()
		    || candidate.multiQCyclicPrefixTokenIndices.empty() )
			return false;
		if( tokenIndicesHaveUnsafeMultiQCyclicPrefixSideEffects( candidate.multiQCyclicPrefixTokenIndices,
		                                                         indexedTokens,
		                                                         allowGuardedPlainStores ) )
			return false;
		if( tokenIndicesContainVisibleLabel( candidate.multiQCyclicPrefixTokenIndices,
		                                     indexedTokens,
		                                     candidate.labelTokenIndex )
		    || tokenIndicesContainVisibleLabel( candidate.multiQMainTokenIndices,
		                                        indexedTokens,
		                                        candidate.labelTokenIndex )
		    || loopBodyContainsVisibleInternalLabel( candidate, indexedTokens ) )
			return false;
		if( checkSuffixClobbers
		    && cyclicPrefixReadsSuffixClobbers( candidate, indexedTokens ) )
			return false;
		if( cyclicPrefixClobbersBranch( candidate, indexedTokens ) )
			return false;
		return true;
	}

	void appendUnlabeledTokenForScheduleCost( std::list<Token>& tokens, const Token& token )
	{
		Token copy( token );
		copy.setLabel( "" );
		tokens.push_back( copy );
	}

	void appendMultiQCyclicPrefixForScheduleCost( std::list<Token>& tokens,
	                                              const VuLoopPipelineOpportunity& candidate,
	                                              const std::vector<const Token*>& indexedTokens )
	{
		for( std::vector<unsigned int>::const_iterator i = candidate.multiQCyclicPrefixTokenIndices.begin();
		     i != candidate.multiQCyclicPrefixTokenIndices.end(); ++i )
		{
			if( *i < indexedTokens.size() )
				tokens.push_back( adjustedMultiQCyclicPrefixToken( *indexedTokens[*i],
				                                                   candidate.multiQCyclicPrefixRotations,
				                                                   candidate.inductionUpdates,
				                                                   candidate.multiQCyclicPrefixInsertBeforeTokenIndex ) );
		}
	}

	void appendMultiQRotationMovesForScheduleCost( std::list<Token>& tokens,
	                                               const VuLoopPipelineOpportunity& candidate,
	                                               const std::vector<const Token*>& indexedTokens )
	{
		if( candidate.multiQCyclicPrefixRotations.empty() )
			return;
		const Token* donor = NULL;
		if( !candidate.multiQMainTokenIndices.empty()
		    && candidate.multiQMainTokenIndices.front() < indexedTokens.size() )
			donor = indexedTokens[candidate.multiQMainTokenIndices.front()];
		else if( !candidate.multiQCyclicPrefixTokenIndices.empty()
		         && candidate.multiQCyclicPrefixTokenIndices.front() < indexedTokens.size() )
			donor = indexedTokens[candidate.multiQCyclicPrefixTokenIndices.front()];
		if( !donor )
			return;
		for( std::vector<VuSoftwarePipelineRotation>::const_iterator i =
		         candidate.multiQCyclicPrefixRotations.begin();
		     i != candidate.multiQCyclicPrefixRotations.end(); ++i )
			tokens.push_back( makeRotationMoveToken( *donor, *i ) );
	}

	bool cyclicPrefixCanCrossAdjustedInductionUpdate( const Token& adjustedPrefix,
	                                                  const Token& crossed,
	                                                  unsigned int crossedTokenIndex,
	                                                  const std::vector<VuLoopInductionUpdate>& inductionUpdates )
	{
		const VuLoopInductionUpdate* crossedUpdate = NULL;
		for( std::vector<VuLoopInductionUpdate>::const_iterator i = inductionUpdates.begin();
		     i != inductionUpdates.end(); ++i )
		{
			if( i->tokenIndex == crossedTokenIndex && i->stepKnown )
			{
				crossedUpdate = &*i;
				break;
			}
		}
		if( !crossedUpdate )
			return false;

		VuTokenResourceAccess movedAccess;
		VuTokenResourceAccess crossedAccess;
		if( !buildVuTokenResourceAccess( adjustedPrefix, movedAccess )
		    || !buildVuTokenResourceAccess( crossed, crossedAccess ) )
			return false;
		if( movedAccess.memoryKind == VU_MEMORY_NONE
		    || movedAccess.memoryFlags != VU_MEMORY_FLAG_NONE
		    || !movedAccess.hasMemoryBase
		    || movedAccess.memoryBaseRegister != crossedUpdate->registerName )
			return false;
		if( crossedAccess.memoryKind != VU_MEMORY_NONE
		    || crossedAccess.implicitReads != VU_RESOURCE_NONE
		    || crossedAccess.implicitWrites != VU_RESOURCE_NONE )
			return false;
		if( !containsKey( crossedAccess.registerWrites, crossedUpdate->registerName ) )
			return false;
		if( containsKey( movedAccess.registerWrites, crossedUpdate->registerName ) )
			return false;

		std::list<std::string> movedReads = movedAccess.registerReads;
		std::list<std::string> crossedWrites = crossedAccess.registerWrites;
		movedReads.remove( crossedUpdate->registerName );
		crossedWrites.remove( crossedUpdate->registerName );

		if( intersects( movedAccess.registerWrites, crossedAccess.registerReads )
		    || intersects( movedAccess.registerWrites, crossedWrites ) )
			return false;
		if( intersects( crossedWrites, movedReads )
		    || intersects( crossedWrites, movedAccess.registerWrites ) )
			return false;
		return true;
	}

	bool cyclicPrefixTokenCanFillBranchDelaySlot( const Token& token, const Token& branch )
	{
		if( token.label().length() != 0 )
			return false;
		if( token.flags() & (Token::PREORDERED | Token::E | Token::D | Token::T
		                   | Token::BRANCH_DELAY_FILLER) )
			return false;
		if( !isVuEmittableInstruction( token ) )
			return false;
		if( vuTokenBranchDelaySlots( token ) > 0 )
			return false;
		if( vuTokenBranchDelaySlots( branch ) != 1 )
			return false;

		VuTokenResourceAccess tokenAccess;
		VuTokenResourceAccess branchAccess;
		if( !buildVuTokenResourceAccess( token, tokenAccess )
		    || !buildVuTokenResourceAccess( branch, branchAccess ) )
			return false;
		if( tokenAccess.branchDelaySlots > 0 )
			return false;
		if( tokenAccess.instructionFlags & (VU_INSTR_BRANCH
		                                  | VU_INSTR_WAIT_Q
		                                  | VU_INSTR_WAIT_P
		                                  | VU_INSTR_XGKICK) )
			return false;
		if( tokenAccess.implicitReads & (VU_RESOURCE_Q | VU_RESOURCE_P) )
			return false;
		if( branchAccess.instructionFlags & (VU_INSTR_LINK_BRANCH | VU_INSTR_REGISTER_BRANCH) )
			return false;
		if( tokenAccess.memoryKind != VU_MEMORY_NONE
		    || tokenAccess.memoryFlags != VU_MEMORY_FLAG_NONE )
			return false;

		if( intersects( tokenAccess.registerWrites, branchAccess.registerReads )
		    || intersects( tokenAccess.registerWrites, branchAccess.registerWrites )
		    || intersects( tokenAccess.registerReads, branchAccess.registerWrites ) )
			return false;
		if( tokenAccess.implicitWrites & (branchAccess.implicitReads | branchAccess.implicitWrites) )
			return false;
		if( branchAccess.implicitWrites & (tokenAccess.implicitReads | tokenAccess.implicitWrites) )
			return false;
		return true;
	}

	bool cyclicPrefixLastTokenCanMoveToBranchDelaySlot( const VuLoopPipelineOpportunity& candidate,
	                                                    const std::vector<const Token*>& indexedTokens )
	{
		if( candidate.multiQCyclicPrefixNeedsGuard
		    || candidate.multiQCyclicPrefixTokenIndices.empty()
		    || candidate.branchTokenIndex >= indexedTokens.size() )
			return false;

		const unsigned int delayedTokenIndex = candidate.multiQCyclicPrefixTokenIndices.back();
		if( delayedTokenIndex >= indexedTokens.size() )
			return false;

		const Token delayedToken =
		    adjustedMultiQCyclicPrefixToken( *indexedTokens[delayedTokenIndex],
		                                     candidate.multiQCyclicPrefixRotations,
		                                     candidate.inductionUpdates,
		                                     candidate.multiQCyclicPrefixInsertBeforeTokenIndex );
		if( !cyclicPrefixTokenCanFillBranchDelaySlot( delayedToken,
		                                              *indexedTokens[candidate.branchTokenIndex] ) )
			return false;

		for( std::vector<unsigned int>::const_iterator i = candidate.multiQMainTokenIndices.begin();
		     i != candidate.multiQMainTokenIndices.end(); ++i )
		{
			if( *i < candidate.multiQCyclicPrefixInsertBeforeTokenIndex
			    || *i >= candidate.branchTokenIndex
			    || *i >= indexedTokens.size() )
				continue;
			if( !vuTokenCanMoveBefore( *indexedTokens[*i], delayedToken ) )
				return false;
		}

		return true;
	}

	bool multiQPipelinePrefixCanMoveBeforeTail( const VuLoopPipelineOpportunity& candidate,
	                                            const std::vector<const Token*>& indexedTokens,
	                                            unsigned int insertionTokenIndex )
	{
		for( std::vector<unsigned int>::const_iterator crossed = candidate.multiQMainTokenIndices.begin();
		     crossed != candidate.multiQMainTokenIndices.end(); ++crossed )
		{
			if( *crossed < insertionTokenIndex || *crossed == candidate.branchTokenIndex )
				continue;
			if( *crossed >= indexedTokens.size() )
				return false;
			for( std::vector<unsigned int>::const_iterator prefix = candidate.multiQCyclicPrefixTokenIndices.begin();
			     prefix != candidate.multiQCyclicPrefixTokenIndices.end(); ++prefix )
			{
				if( *prefix >= indexedTokens.size() )
					return false;
				const Token adjustedPrefix =
				    adjustedMultiQCyclicPrefixToken( *indexedTokens[*prefix],
				                                     candidate.multiQCyclicPrefixRotations,
				                                     candidate.inductionUpdates,
				                                     insertionTokenIndex );
				if( !vuTokenCanMoveBefore( adjustedPrefix, *indexedTokens[*crossed] )
				    && !cyclicPrefixCanCrossAdjustedInductionUpdate( adjustedPrefix,
				                                                     *indexedTokens[*crossed],
				                                                     *crossed,
				                                                     candidate.inductionUpdates ) )
					return false;
			}
		}
		return true;
	}

	void setMultiQPipelineCandidateInsertionPoint( VuLoopPipelineOpportunity& candidate,
	                                              const std::vector<const Token*>& indexedTokens,
	                                              unsigned int insertionTokenIndex )
	{
		if( insertionTokenIndex == candidate.branchTokenIndex
		    || multiQPipelinePrefixCanMoveBeforeTail( candidate,
		                                             indexedTokens,
		                                             insertionTokenIndex ) )
			candidate.multiQCyclicPrefixInsertBeforeTokenIndex = insertionTokenIndex;
	}

	bool branchConditionReadyAtInsertionPoint( const VuLoopPipelineOpportunity& candidate,
	                                           const std::vector<const Token*>& indexedTokens,
	                                           unsigned int insertionTokenIndex )
	{
		if( insertionTokenIndex == candidate.branchTokenIndex )
			return true;
		if( candidate.branchTokenIndex >= indexedTokens.size() )
			return false;

		VuTokenResourceAccess branchAccess;
		if( !buildVuTokenResourceAccess( *indexedTokens[candidate.branchTokenIndex], branchAccess ) )
			return false;

		for( std::vector<unsigned int>::const_iterator i = candidate.multiQMainTokenIndices.begin();
		     i != candidate.multiQMainTokenIndices.end(); ++i )
		{
			if( *i < insertionTokenIndex
			    || *i >= candidate.branchTokenIndex
			    || *i >= indexedTokens.size() )
				continue;

			VuTokenResourceAccess access;
			if( !buildVuTokenResourceAccess( *indexedTokens[*i], access ) )
				return false;
			if( intersects( access.registerWrites, branchAccess.registerReads )
			    || intersects( access.registerWrites, branchAccess.registerWrites ) )
				return false;
			if( access.implicitWrites & (branchAccess.implicitReads | branchAccess.implicitWrites) )
				return false;
		}
		return true;
	}

	void assignMultiQGuardDrainTokenIndices( VuLoopPipelineOpportunity& candidate )
	{
		candidate.drainTokenIndices.clear();
		if( !candidate.multiQCyclicPrefixNeedsGuard )
			return;
		for( std::vector<unsigned int>::const_iterator i = candidate.multiQMainTokenIndices.begin();
		     i != candidate.multiQMainTokenIndices.end(); ++i )
		{
			if( *i >= candidate.multiQCyclicPrefixInsertBeforeTokenIndex
			    && *i < candidate.branchTokenIndex )
				candidate.drainTokenIndices.push_back( *i );
		}
	}

	bool tokenIndicesTouchImplicitResources( const std::vector<unsigned int>& tokenIndices,
	                                         const std::vector<const Token*>& indexedTokens,
	                                         unsigned int resources )
	{
		for( std::vector<unsigned int>::const_iterator i = tokenIndices.begin();
		     i != tokenIndices.end(); ++i )
		{
			if( *i >= indexedTokens.size() )
				return true;
			VuTokenResourceAccess access;
			if( !buildVuTokenResourceAccess( *indexedTokens[*i], access ) )
				return true;
			if( (access.implicitReads | access.implicitWrites) & resources )
				return true;
		}
		return false;
	}

	unsigned int multiQPipelineCandidateMainCycles( const VuLoopPipelineOpportunity& candidate,
	                                                const std::vector<const Token*>& indexedTokens )
	{
		std::list<Token> mainTokens;
		appendMultiQRotationMovesForScheduleCost( mainTokens, candidate, indexedTokens );
		bool emittedPrefix = false;
		for( std::vector<unsigned int>::const_iterator i = candidate.multiQMainTokenIndices.begin();
		     i != candidate.multiQMainTokenIndices.end(); ++i )
		{
			if( !emittedPrefix && *i == candidate.multiQCyclicPrefixInsertBeforeTokenIndex )
			{
				if( candidate.multiQCyclicPrefixNeedsGuard
				    && candidate.branchTokenIndex < indexedTokens.size() )
					mainTokens.push_back( invertedBranchToDrainToken( *indexedTokens[candidate.branchTokenIndex],
					                                                  candidate.label + "__DRAIN" ) );
				appendMultiQCyclicPrefixForScheduleCost( mainTokens, candidate, indexedTokens );
				emittedPrefix = true;
			}
			if( *i < indexedTokens.size() && *i != candidate.branchTokenIndex )
				appendUnlabeledTokenForScheduleCost( mainTokens, *indexedTokens[*i] );
		}
		if( !emittedPrefix )
			appendMultiQCyclicPrefixForScheduleCost( mainTokens, candidate, indexedTokens );
		if( candidate.branchTokenIndex < indexedTokens.size() )
			appendUnlabeledTokenForScheduleCost( mainTokens, *indexedTokens[candidate.branchTokenIndex] );
		const unsigned int cycles = scheduleVuProgramReadyIssueSlotsWithFlagLiveness( mainTokens ).cycleCount;
		return candidate.multiQCyclicPrefixLastTokenInBranchDelaySlot && cycles > 0
		     ? cycles - 1
		     : cycles;
	}

	bool considerMultiQPipelineCandidate( const VuLoopCandidate& loop,
	                                      const std::vector<const Token*>& indexedTokens,
	                                      const VuLoopPipelineOpportunity& baseOpportunity,
	                                      unsigned int prefixEndOffset,
	                                      unsigned int mainBeginOffset,
	                                      unsigned int branchOffset,
	                                      bool allowGuardedPlainStores,
	                                      unsigned int& bestMainCycles,
	                                      VuLoopPipelineOpportunity& bestOpportunity )
	{
		VuLoopPipelineOpportunity candidate = baseOpportunity;
		assignMultiQPipelineCandidate( loop,
		                               prefixEndOffset,
		                               mainBeginOffset,
		                               branchOffset,
		                               candidate );
		std::vector<VuLoopPipelineOpportunity> candidates;
		candidates.push_back( candidate );
		VuLoopPipelineOpportunity rotatedCandidate = candidate;
		if( assignMultiQCyclicPrefixRotations( loop, indexedTokens, rotatedCandidate ) )
			candidates.push_back( rotatedCandidate );

		bool found = false;
		for( std::vector<VuLoopPipelineOpportunity>::const_iterator candidateIt = candidates.begin();
		     candidateIt != candidates.end(); ++candidateIt )
		{
			const bool needsGuard =
			    allowGuardedPlainStores
			    && tokenIndicesHaveGuardableMultiQCyclicPrefixSideEffects( candidateIt->multiQCyclicPrefixTokenIndices,
			                                                               indexedTokens );
			if( needsGuard
			    && (candidateIt->branchTokenIndex >= indexedTokens.size()
			        || !branchCanInvertToDrain( *indexedTokens[candidateIt->branchTokenIndex] )) )
				continue;
			if( !multiQPipelineCandidateStructurallySafe( *candidateIt,
			                                              indexedTokens,
			                                              needsGuard,
			                                              false ) )
				continue;
			if( needsGuard )
			{
				const unsigned int kMaxGuardedSplitPrefixTokens = 32;
				const bool prefixTouchesLongLatencyResult =
				    tokenIndicesTouchImplicitResources( candidateIt->multiQCyclicPrefixTokenIndices,
				                                        indexedTokens,
				                                        VU_RESOURCE_Q | VU_RESOURCE_P );
				const unsigned int firstLegalInsertionTokenIndex =
				    prefixTouchesLongLatencyResult
				    ? candidateIt->lastQConsumerTokenIndex + 1
				    : (candidateIt->multiQMainTokenIndices.empty()
				       ? candidateIt->lastQConsumerTokenIndex + 1
				       : candidateIt->multiQMainTokenIndices.front());

				std::vector<unsigned int> insertionCandidates;
				if( candidateIt->multiQCyclicPrefixTokenIndices.size() <= kMaxGuardedSplitPrefixTokens )
				{
					for( std::vector<unsigned int>::const_iterator i = candidateIt->multiQMainTokenIndices.begin();
					     i != candidateIt->multiQMainTokenIndices.end(); ++i )
					{
						if( *i < firstLegalInsertionTokenIndex || *i > candidateIt->branchTokenIndex )
							continue;
						VuLoopPipelineOpportunity insertedCandidate = *candidateIt;
						insertedCandidate.multiQCyclicPrefixNeedsGuard = true;
						insertedCandidate.multiQCyclicPrefixLastTokenInBranchDelaySlot = false;
						setMultiQPipelineCandidateInsertionPoint( insertedCandidate, indexedTokens, *i );
						if( insertedCandidate.multiQCyclicPrefixInsertBeforeTokenIndex != *i )
							continue;
						if( cyclicPrefixReadsSuffixClobbersBeforeInsertion( insertedCandidate,
						                                                    indexedTokens ) )
							continue;
						if( !branchConditionReadyAtInsertionPoint( insertedCandidate, indexedTokens, *i ) )
							continue;
						insertionCandidates.push_back( *i );
						break;
					}
				}
				if( insertionCandidates.empty()
				    || insertionCandidates.back() != candidateIt->branchTokenIndex )
					insertionCandidates.push_back( candidateIt->branchTokenIndex );

				for( std::vector<unsigned int>::const_iterator i = insertionCandidates.begin();
				     i != insertionCandidates.end(); ++i )
				{
					VuLoopPipelineOpportunity insertedCandidate = *candidateIt;
					insertedCandidate.multiQCyclicPrefixNeedsGuard = true;
					insertedCandidate.multiQCyclicPrefixLastTokenInBranchDelaySlot = false;
					setMultiQPipelineCandidateInsertionPoint( insertedCandidate, indexedTokens, *i );
					if( insertedCandidate.multiQCyclicPrefixInsertBeforeTokenIndex != *i )
						continue;
					if( cyclicPrefixReadsSuffixClobbersBeforeInsertion( insertedCandidate,
					                                                    indexedTokens ) )
						continue;
					assignMultiQGuardDrainTokenIndices( insertedCandidate );
					const unsigned int cycles = multiQPipelineCandidateMainCycles( insertedCandidate, indexedTokens );
					const bool preferEarlyGuardedDrain =
					    !insertedCandidate.drainTokenIndices.empty()
					    && candidateIt->multiQMainTokenIndices.size() <= 32;
					const bool keepEarlyGuardedDrain =
					    insertedCandidate.drainTokenIndices.empty()
					    && bestOpportunity.multiQCyclicPrefixNeedsGuard
					    && !bestOpportunity.drainTokenIndices.empty()
					    && candidateIt->multiQMainTokenIndices.size() <= 32;
					if( (cycles < bestMainCycles && !keepEarlyGuardedDrain)
					    || preferEarlyGuardedDrain )
					{
						bestMainCycles = cycles;
						bestOpportunity = insertedCandidate;
					}
					found = true;
				}
				continue;
			}
			const bool prefixTouchesLongLatencyResult =
			    tokenIndicesTouchImplicitResources( candidateIt->multiQCyclicPrefixTokenIndices,
			                                        indexedTokens,
			                                        VU_RESOURCE_Q | VU_RESOURCE_P );
			const unsigned int firstLegalInsertionTokenIndex =
			    prefixTouchesLongLatencyResult
			    ? candidateIt->lastQConsumerTokenIndex + 1
			    : (candidateIt->multiQMainTokenIndices.empty()
			       ? candidateIt->lastQConsumerTokenIndex + 1
			       : candidateIt->multiQMainTokenIndices.front());
			for( std::vector<unsigned int>::const_iterator i = candidateIt->multiQMainTokenIndices.begin();
			     i != candidateIt->multiQMainTokenIndices.end(); ++i )
			{
				if( *i < firstLegalInsertionTokenIndex || *i > candidateIt->branchTokenIndex )
					continue;
				VuLoopPipelineOpportunity insertedCandidate = *candidateIt;
				setMultiQPipelineCandidateInsertionPoint( insertedCandidate, indexedTokens, *i );
				if( insertedCandidate.multiQCyclicPrefixInsertBeforeTokenIndex != *i )
					continue;
				if( cyclicPrefixReadsSuffixClobbersBeforeInsertion( insertedCandidate,
				                                                    indexedTokens ) )
					continue;
				insertedCandidate.multiQCyclicPrefixLastTokenInBranchDelaySlot =
				    cyclicPrefixLastTokenCanMoveToBranchDelaySlot( insertedCandidate, indexedTokens );
				const unsigned int cycles = multiQPipelineCandidateMainCycles( insertedCandidate, indexedTokens );
				if( cycles < bestMainCycles )
				{
					bestMainCycles = cycles;
					bestOpportunity = insertedCandidate;
				}
				found = true;
			}
		}
		return found;
	}

	unsigned int scheduledLoopBodyCycles( const std::vector<unsigned int>& tokenIndices,
	                                      const std::vector<const Token*>& indexedTokens )
	{
		std::list<Token> bodyTokens;
		for( std::vector<unsigned int>::const_iterator i = tokenIndices.begin();
		     i != tokenIndices.end(); ++i )
		{
			if( *i < indexedTokens.size() )
				appendUnlabeledTokenForScheduleCost( bodyTokens, *indexedTokens[*i] );
		}
		return scheduleVuProgramReadyIssueSlotsWithFlagLiveness( bodyTokens ).cycleCount;
	}

	// Diagnostic: schedule prolog ++ main as one segment, return cycles attributed to the
	// main portion (i.e. cycles spent on tokens after the prolog's last issue slot).
	// This approximates how the emitter sees the main body when long-latency results from
	// the prolog (Q, P, FMAC ACC, flag clauses, etc.) are still in flight at main entry.
	unsigned int scheduledLoopBodyCyclesInContext( const std::vector<unsigned int>& prologTokenIndices,
	                                               const std::vector<unsigned int>& mainTokenIndices,
	                                               const std::vector<const Token*>& indexedTokens )
	{
		std::list<Token> combinedTokens;
		for( std::vector<unsigned int>::const_iterator i = prologTokenIndices.begin();
		     i != prologTokenIndices.end(); ++i )
		{
			if( *i < indexedTokens.size() )
				appendUnlabeledTokenForScheduleCost( combinedTokens, *indexedTokens[*i] );
		}
		const unsigned int prologCycles =
		    scheduleVuProgramReadyIssueSlotsWithFlagLiveness( combinedTokens ).cycleCount;
		for( std::vector<unsigned int>::const_iterator i = mainTokenIndices.begin();
		     i != mainTokenIndices.end(); ++i )
		{
			if( *i < indexedTokens.size() )
				appendUnlabeledTokenForScheduleCost( combinedTokens, *indexedTokens[*i] );
		}
		const unsigned int totalCycles =
		    scheduleVuProgramReadyIssueSlotsWithFlagLiveness( combinedTokens ).cycleCount;
		return totalCycles >= prologCycles ? totalCycles - prologCycles : 0;
	}

	bool cyclicPrefixMainStartsWithStore( const VuLoopPipelineOpportunity& candidate,
	                                      const std::vector<const Token*>& indexedTokens )
	{
		if( candidate.multiQMainTokenIndices.empty() )
			return false;
		const unsigned int tokenIndex = candidate.multiQMainTokenIndices.front();
		if( tokenIndex >= indexedTokens.size() )
			return false;
		VuTokenResourceAccess access;
		return buildVuTokenResourceAccess( *indexedTokens[tokenIndex], access )
		    && access.memoryKind == VU_MEMORY_STORE;
	}

	// Track 9.E step 2 (diagnostic only): estimate the steady-state per-iteration
	// cycle count of a 2-stage software-pipelined kernel built from a single-Q
	// eligible opportunity. Builds a replicated kernel where iteration N+1 uses
	// scratch VF registers in place of the loop-carried Q-output VF registers,
	// breaking the iter->iter WAR/WAW on those carried regs. The Q register and
	// FMAC accumulator deps that the hardware serializes are preserved.
	//
	// Returns false if rotation allocation fails or the opportunity isn't
	// shaped like a single-Q SWP candidate. On success outKernelCycles is the
	// total scheduled cycles of (iter1 ++ iter2_renamed); per-iter cost is
	// outKernelCycles / 2.
	bool synthesizeTwoStageKernelCycles( const VuLoopCandidate& loop,
	                                     const std::vector<const Token*>& indexedTokens,
	                                     const VuLoopPipelineOpportunity& opportunity,
	                                     unsigned int& outKernelCycles,
	                                     std::vector<VuSoftwarePipelineRotation>& outRotations )
	{
		outKernelCycles = 0;
		outRotations.clear();
		if( !opportunity.eligibleSingleQSoftwarePipeline )
			return false;
		if( opportunity.mainTokenIndices.empty() )
			return false;
		if( opportunity.carriedQOutputRegisters.empty() )
			return false;

		std::vector<VuSoftwarePipelineRotation> rotations;
		for( std::list<std::string>::const_iterator reg =
		         opportunity.carriedQOutputRegisters.begin();
		     reg != opportunity.carriedQOutputRegisters.end(); ++reg )
		{
			VuSoftwarePipelineRotation rot;
			rot.registerBase = *reg;
			rot.hasScratchRegister = false;
			rotations.push_back( rot );
		}
		assignRotationScratchRegisters( loop, rotations );
		for( std::vector<VuSoftwarePipelineRotation>::const_iterator r = rotations.begin();
		     r != rotations.end(); ++r )
		{
			if( !r->hasScratchRegister )
				return false;
		}

		std::list<Token> kernelTokens;
		for( std::vector<unsigned int>::const_iterator i = opportunity.mainTokenIndices.begin();
		     i != opportunity.mainTokenIndices.end(); ++i )
		{
			if( *i < indexedTokens.size() )
				appendUnlabeledTokenForScheduleCost( kernelTokens, *indexedTokens[*i] );
		}
		for( std::vector<unsigned int>::const_iterator i = opportunity.mainTokenIndices.begin();
		     i != opportunity.mainTokenIndices.end(); ++i )
		{
			if( *i < indexedTokens.size() )
				kernelTokens.push_back(
				    adjustedMultiQCyclicPrefixToken( *indexedTokens[*i], rotations ) );
		}

		outKernelCycles =
		    scheduleVuProgramReadyIssueSlotsWithFlagLiveness( kernelTokens ).cycleCount;
		outRotations.swap( rotations );
		return true;
	}

	// Track 9.E step 3b (diagnostic only): estimate the steady-state per-iter
	// cycle count of a 2-stage software-pipelined kernel that interleaves
	// stage1 of iter N+1 BEFORE stage2 of iter N at the source level, so the
	// downstream scheduler can issue the next div/mulax chain while iter N's
	// mulq->ftoi4 chain is still draining Q. Uses depth-2 rotation banks
	// from step 3a so two iterations can hold renamed VF carriers simultaneously.
	//
	// Source order emitted to the scheduler (3-iter window):
	//   stage1_A_b0, stage1_B_b1, stage2_A_b0, stage1_C_b0, stage2_B_b1, stage2_C_b0
	//                                          |                       |
	//                                      boundary1               boundary2
	// perIter = boundary2 - boundary1 = steady-state cost of one iteration
	// once both rotation banks are in flight.
	//
	// Returns false if the opportunity is not single-Q SWP shaped, if the q
	// producer doesn't live inside mainTokenIndices, or if depth-2 rotation
	// allocation fails. On success outPerIterCycles holds the per-iter estimate
	// and outRotations holds the depth-2 banks used.
	bool synthesizeQInterleavedKernelCycles( const VuLoopCandidate& loop,
	                                         const std::vector<const Token*>& indexedTokens,
	                                         const VuLoopPipelineOpportunity& opportunity,
	                                         unsigned int& outPerIterCycles,
	                                         unsigned int& outStage1Cycles,
	                                         unsigned int& outStage2Cycles,
	                                         std::vector<VuSoftwarePipelineRotation>& outRotations,
	                                         std::string& outFailReason )
	{
		outPerIterCycles = 0;
		outStage1Cycles = 0;
		outStage2Cycles = 0;
		outRotations.clear();
		outFailReason.clear();

		if( !opportunity.eligibleSingleQSoftwarePipeline )
		{
			outFailReason = "not_eligible_single_q_swp";
			return false;
		}
		if( opportunity.mainTokenIndices.empty() )
		{
			outFailReason = "empty_main";
			return false;
		}
		if( opportunity.carriedQOutputRegisters.empty() )
		{
			outFailReason = "no_carried_q_outputs";
			return false;
		}

		// Split mainTokenIndices at the LAST q consumer (mulq): stage1 = tokens
		// up to and including the last q consumer of iter N (the chain that
		// uses Q from iter N-1's hoisted div); stage2 = tokens after (post-mulq
		// chain, e.g. ftoi4/sq/induction). At source-level interleave we want
		// stage1 of iter N+1 (which will be using iter N's pending Q) to be
		// scheduled alongside stage2 of iter N. The hoisted q producer for
		// iter N+1 lives outside mainTokenIndices and so isn't part of either
		// stage here.
		if( opportunity.qConsumerTokenIndices.empty() )
		{
			outFailReason = "no_q_consumers";
			return false;
		}
		const unsigned int qConsumerBoundary = opportunity.qConsumerTokenIndices.back();
		std::vector<unsigned int> stage1;
		std::vector<unsigned int> stage2;
		bool sawBoundary = false;
		for( std::vector<unsigned int>::const_iterator i = opportunity.mainTokenIndices.begin();
		     i != opportunity.mainTokenIndices.end(); ++i )
		{
			if( !sawBoundary )
			{
				stage1.push_back( *i );
				if( *i == qConsumerBoundary )
					sawBoundary = true;
			}
			else
			{
				stage2.push_back( *i );
			}
		}
		if( !sawBoundary || stage1.empty() || stage2.empty() )
		{
			outFailReason = !sawBoundary ? "q_consumer_not_in_main"
			                             : ( stage1.empty() ? "empty_stage1" : "empty_stage2" );
			return false;
		}

		// Allocate depth-2 rotation banks. Collapse carriedQOutputRegisters to
		// their base register key first: rewriteRotatedRegistersToScratch
		// (line ~4288) looks up rotations via findRotationForBase against the
		// base register name (registerBaseKey strips .x/.y/.z). Field-keyed
		// rotations are silently ignored by the rewriter, so we MUST keep
		// registerBase as the bare VF name. This also reduces the depth-2
		// scratch VF demand from N_fields to N_bases (e.g. mulq.xyz VF8 uses
		// 1 base, not 3 fields).
		std::list<std::string> uniqueBases;
		for( std::list<std::string>::const_iterator reg =
		         opportunity.carriedQOutputRegisters.begin();
		     reg != opportunity.carriedQOutputRegisters.end(); ++reg )
			addUniqueString( uniqueBases, registerBaseKey( *reg ) );
		std::vector<VuSoftwarePipelineRotation> banks;
		for( std::list<std::string>::const_iterator base = uniqueBases.begin();
		     base != uniqueBases.end(); ++base )
		{
			VuSoftwarePipelineRotation rot;
			rot.registerBase = *base;
			rot.hasScratchRegister = false;
			banks.push_back( rot );
		}
		assignRotationScratchRegisters( loop, banks, 2 );
		for( std::vector<VuSoftwarePipelineRotation>::const_iterator r = banks.begin();
		     r != banks.end(); ++r )
		{
			if( r->rotationBank.size() < 2 )
			{
				outFailReason = "depth2_alloc_failed";
				return false;
			}
		}

		// Build per-bank rotation views. bank0 uses rotationBank[0] (same as
		// hasScratchRegister/scratchRegister); bank1 overrides scratchRegister
		// with rotationBank[1] so rewriteRotatedRegistersToScratch emits the
		// alternate scratch VF.
		std::vector<VuSoftwarePipelineRotation> bank0 = banks;
		std::vector<VuSoftwarePipelineRotation> bank1 = banks;
		for( std::size_t k = 0; k < bank0.size(); ++k )
		{
			bank0[k].scratchRegister = bank0[k].rotationBank[0];
			bank0[k].hasScratchRegister = true;
			bank1[k].scratchRegister = bank1[k].rotationBank[1];
			bank1[k].hasScratchRegister = true;
		}

		// Stage helpers: append the given index list as adjusted tokens under
		// the supplied bank. Source-order interleave is performed by the caller
		// via the order in which these helpers are invoked.
		struct Local
		{
			static void appendStage( std::list<Token>& dst,
			                         const std::vector<unsigned int>& indices,
			                         const std::vector<const Token*>& src,
			                         const std::vector<VuSoftwarePipelineRotation>& rotations )
			{
				for( std::vector<unsigned int>::const_iterator i = indices.begin();
				     i != indices.end(); ++i )
				{
					if( *i < src.size() )
						dst.push_back(
						    adjustedMultiQCyclicPrefixToken( *src[*i], rotations ) );
				}
			}
		};

		// Schedule three prefixes of the interleaved sequence and subtract to
		// extract per-stage and per-iter cycle costs.
		// Sequence: stage1_A_b0, stage1_B_b1, stage2_A_b0, stage1_C_b0, stage2_B_b1, stage2_C_b0
		std::list<Token> seq_b1;
		Local::appendStage( seq_b1, stage1, indexedTokens, bank0 );
		Local::appendStage( seq_b1, stage1, indexedTokens, bank1 );
		Local::appendStage( seq_b1, stage2, indexedTokens, bank0 );
		const unsigned int boundary1 =
		    scheduleVuProgramReadyIssueSlotsWithFlagLiveness( seq_b1 ).cycleCount;

		std::list<Token> seq_b2;
		Local::appendStage( seq_b2, stage1, indexedTokens, bank0 );
		Local::appendStage( seq_b2, stage1, indexedTokens, bank1 );
		Local::appendStage( seq_b2, stage2, indexedTokens, bank0 );
		Local::appendStage( seq_b2, stage1, indexedTokens, bank0 );
		Local::appendStage( seq_b2, stage2, indexedTokens, bank1 );
		const unsigned int boundary2 =
		    scheduleVuProgramReadyIssueSlotsWithFlagLiveness( seq_b2 ).cycleCount;

		// Also measure stage1 and stage2 in isolation (no warmup) for reporting.
		std::list<Token> seq_s1_only;
		Local::appendStage( seq_s1_only, stage1, indexedTokens, bank0 );
		outStage1Cycles =
		    scheduleVuProgramReadyIssueSlotsWithFlagLiveness( seq_s1_only ).cycleCount;
		std::list<Token> seq_s2_only;
		Local::appendStage( seq_s2_only, stage2, indexedTokens, bank0 );
		outStage2Cycles =
		    scheduleVuProgramReadyIssueSlotsWithFlagLiveness( seq_s2_only ).cycleCount;

		if( boundary2 <= boundary1 )
		{
			outFailReason = "boundary_non_positive";
			return false;
		}
		outPerIterCycles = boundary2 - boundary1;
		outRotations.swap( banks );
		return true;
	}

	bool loopBodyHasQProducer( const VuLoopCandidate& loop )
	{
		for( std::vector<const Token*>::const_iterator i = loop.bodyTokens.begin();
		     i != loop.bodyTokens.end(); ++i )
		{
			if( vuTokenWritesQ( **i ) )
				return true;
		}
		return false;
	}

	bool buildGenericCyclicPrefixRewritePlan( const VuLoopCandidate& loop,
	                                          const std::vector<const Token*>& indexedTokens,
	                                          VuSoftwarePipelineRewritePlan& plan )
	{
		const bool hasLoopExtra = loopUsesLoopExtraDirective( loop, indexedTokens );
		if( !loop.simpleCountedLoop
		    || loopBodyHasQProducer( loop )
		    || loop.hasMemoryPreOrPostIncrement
		    || loop.hasXgkick
		    || loop.inductionRegisters.empty()
		    || loop.branchTokenIndex >= indexedTokens.size()
		    || loop.firstBodyTokenIndex >= loop.branchTokenIndex )
			return false;
		if( hasLoopExtra && loop.memoryStoreCount > 1 )
			return false;

		VuLoopPipelineOpportunity base;
		base.label = loop.label;
		base.labelTokenIndex = loop.labelTokenIndex;
		base.branchTokenIndex = loop.branchTokenIndex;
		base.branchDelaySlots = loop.branchToken ? vuTokenBranchDelaySlots( *loop.branchToken ) : 0;
		base.loopCsClid = loop.loopCsClid;
		base.loopCsMlid = loop.loopCsMlid;
		base.simpleCountedLoop = loop.simpleCountedLoop;
		base.memoryLoadCount = loop.memoryLoadCount;
		base.memoryStoreCount = loop.memoryStoreCount;
		base.hasMemoryPreOrPostIncrement = loop.hasMemoryPreOrPostIncrement;
		base.hasXgkick = loop.hasXgkick;
		base.inductionRegisters = loop.inductionRegisters;
		base.inductionUpdates = loop.inductionUpdates;
		base.loopReadWriteRegisters = loop.loopReadWriteRegisters;

		if( base.branchDelaySlots == 0
		    || tokenIndicesContainVisibleLabel( std::vector<unsigned int>(),
		                                        indexedTokens,
		                                        base.labelTokenIndex )
		    || loopBodyContainsVisibleInternalLabel( base, indexedTokens ) )
			return false;

		const unsigned int branchOffset = loop.branchTokenIndex - loop.firstBodyTokenIndex;
		std::vector<unsigned int> originalTokenIndices;
		appendPipelineInstructionIndices( loop, 0, branchOffset + 1, originalTokenIndices );
		if( originalTokenIndices.empty() )
			return false;

		unsigned int bestCycles = scheduledLoopBodyCycles( originalTokenIndices, indexedTokens );
		VuLoopPipelineOpportunity bestCandidate;
		bool bestStartsWithStore = false;
		bool found = false;

		const bool dumpRejections =
		    std::getenv( "OPENVCL_DUMP_CYCLIC_PREFIX_REJECTIONS" ) != NULL;
		if( dumpRejections )
		{
			std::cerr << "[cyclic-prefix-trace] loop=" << loop.label
			          << " branchOffset=" << branchOffset
			          << " baselineCycles=" << bestCycles << "\n";
		}

		for( unsigned int splitOffset = 1; splitOffset < branchOffset; ++splitOffset )
		{
			VuLoopPipelineOpportunity candidate = base;
			assignMultiQPipelineCandidate( loop,
			                               splitOffset,
			                               splitOffset,
			                               branchOffset,
			                               candidate );
			if( candidate.multiQPrologTokenIndices.empty()
			    || candidate.multiQMainTokenIndices.empty() )
			{
				if( dumpRejections )
					std::cerr << "[cyclic-prefix-trace]   split=" << splitOffset
					          << " rejected=empty_partition\n";
				continue;
			}
			if( !assignMultiQCyclicPrefixRotations( loop, indexedTokens, candidate ) )
			{
				if( dumpRejections )
					std::cerr << "[cyclic-prefix-trace]   split=" << splitOffset
					          << " rejected=rotation_assign_failed\n";
				continue;
			}
			if( !multiQPipelineCandidateStructurallySafe( candidate, indexedTokens ) )
			{
				if( dumpRejections )
					std::cerr << "[cyclic-prefix-trace]   split=" << splitOffset
					          << " rejected=structurally_unsafe"
					          << " (suffix_clobber_at_branch="
					          << ( cyclicPrefixReadsSuffixClobbers( candidate, indexedTokens ) ? 1 : 0 )
					          << ")\n";
				continue;
			}

			bool evaluatedInsertion = false;
			for( std::vector<unsigned int>::const_iterator i = candidate.multiQMainTokenIndices.begin();
			     i != candidate.multiQMainTokenIndices.end(); ++i )
			{
				if( *i > candidate.branchTokenIndex )
					continue;
				VuLoopPipelineOpportunity insertedCandidate = candidate;
				setMultiQPipelineCandidateInsertionPoint( insertedCandidate, indexedTokens, *i );
				if( insertedCandidate.multiQCyclicPrefixInsertBeforeTokenIndex != *i )
					continue;
				insertedCandidate.multiQCyclicPrefixLastTokenInBranchDelaySlot =
				    cyclicPrefixLastTokenCanMoveToBranchDelaySlot( insertedCandidate, indexedTokens );
				evaluatedInsertion = true;
				const bool clobbersHere =
				    cyclicPrefixReadsSuffixClobbersBeforeInsertion( insertedCandidate, indexedTokens );
				const unsigned int cycles = multiQPipelineCandidateMainCycles( insertedCandidate, indexedTokens );
				const bool startsWithStore =
				    hasLoopExtra && cyclicPrefixMainStartsWithStore( insertedCandidate, indexedTokens );
				if( dumpRejections )
					std::cerr << "[cyclic-prefix-trace]   split=" << splitOffset
					          << " insertBefore=" << *i
					          << " cycles=" << cycles
					          << " suffixClobberHere=" << ( clobbersHere ? 1 : 0 )
					          << "\n";
				if( cycles < bestCycles
				    || (startsWithStore && !bestStartsWithStore && cycles == bestCycles) )
				{
					bestCycles = cycles;
					bestCandidate = insertedCandidate;
					bestStartsWithStore = startsWithStore;
					found = true;
				}
			}

			if( !evaluatedInsertion )
			{
				candidate.multiQCyclicPrefixLastTokenInBranchDelaySlot =
				    cyclicPrefixLastTokenCanMoveToBranchDelaySlot( candidate, indexedTokens );
				const unsigned int cycles = multiQPipelineCandidateMainCycles( candidate, indexedTokens );
				const bool startsWithStore =
				    hasLoopExtra && cyclicPrefixMainStartsWithStore( candidate, indexedTokens );
				if( dumpRejections )
					std::cerr << "[cyclic-prefix-trace]   split=" << splitOffset
					          << " insertBefore=branch cycles=" << cycles << "\n";
				if( cycles < bestCycles
				    || (startsWithStore && !bestStartsWithStore && cycles == bestCycles) )
				{
					bestCycles = cycles;
					bestCandidate = candidate;
					bestStartsWithStore = startsWithStore;
					found = true;
				}
			}
		}

		if( dumpRejections )
		{
			std::cerr << "[cyclic-prefix-trace] loop=" << loop.label
			          << " result=" << ( found ? "ACCEPTED" : "no_improvement" )
			          << " bestCycles=" << bestCycles << "\n";
		}

		if( !found )
			return false;

		plan.label = loop.label;
		plan.prologLabel = loop.label + "__PROLOG";
		plan.mainLabel = loop.label;
		plan.drainLabel = loop.label + "__DRAIN";
		plan.labelTokenIndex = loop.labelTokenIndex;
		plan.branchTokenIndex = loop.branchTokenIndex;
		plan.qProducerTokenIndex = VU_SCHEDULED_TOKEN_INDEX_NONE;
		plan.cyclicPrefixBeforeBranch = true;
		plan.prologTokenIndices = bestCandidate.multiQPrologTokenIndices;
		plan.mainTokenIndices = bestCandidate.multiQMainTokenIndices;
		plan.cyclicPrefixTokenIndices = bestCandidate.multiQCyclicPrefixTokenIndices;
		plan.cyclicPrefixRotations = bestCandidate.multiQCyclicPrefixRotations;
		plan.cyclicPrefixInsertBeforeTokenIndex =
		    bestCandidate.multiQCyclicPrefixInsertBeforeTokenIndex;
		plan.cyclicPrefixNeedsGuard = bestCandidate.multiQCyclicPrefixNeedsGuard;
		plan.cyclicPrefixLastTokenInBranchDelaySlot =
		    bestCandidate.multiQCyclicPrefixLastTokenInBranchDelaySlot;
		plan.emitsDrain = bestCandidate.multiQCyclicPrefixNeedsGuard;
		plan.inductionUpdates = bestCandidate.inductionUpdates;
		plan.drainTokenIndices = bestCandidate.drainTokenIndices;
		return true;
	}

	bool cyclicPrefixReadsSuffixClobbers( const VuLoopPipelineOpportunity& opportunity,
	                                      const std::vector<const Token*>& indexedTokens )
	{
		VuLoopPipelineOpportunity branchInsertion = opportunity;
		branchInsertion.multiQCyclicPrefixInsertBeforeTokenIndex =
		    opportunity.branchTokenIndex;
		return cyclicPrefixReadsSuffixClobbersBeforeInsertion( branchInsertion,
		                                                       indexedTokens );
	}

	bool cyclicPrefixReadsSuffixClobbersBeforeInsertion( const VuLoopPipelineOpportunity& opportunity,
	                                                     const std::vector<const Token*>& indexedTokens )
	{
		std::list<std::string> suffixWrites;
		const unsigned int insertionTokenIndex =
		    opportunity.multiQCyclicPrefixInsertBeforeTokenIndex == VU_SCHEDULED_TOKEN_INDEX_NONE
		    ? opportunity.branchTokenIndex
		    : opportunity.multiQCyclicPrefixInsertBeforeTokenIndex;
		for( std::vector<unsigned int>::const_iterator i = opportunity.multiQMainTokenIndices.begin();
		     i != opportunity.multiQMainTokenIndices.end(); ++i )
		{
			if( *i >= indexedTokens.size()
			    || *i == opportunity.branchTokenIndex
			    || *i >= insertionTokenIndex )
				continue;
			std::list<std::string> writes;
			collectVuRegisterWriteKeys( *indexedTokens[*i], writes );
			for( std::list<std::string>::const_iterator write = writes.begin(); write != writes.end(); ++write )
				addUniqueString( suffixWrites, *write );
		}

		std::list<std::string> prefixWritesSoFar;
		for( std::vector<unsigned int>::const_iterator i = opportunity.multiQCyclicPrefixTokenIndices.begin();
		     i != opportunity.multiQCyclicPrefixTokenIndices.end(); ++i )
		{
			if( *i >= indexedTokens.size() )
				return true;
			VuTokenResourceAccess access;
			if( !buildVuTokenResourceAccess( *indexedTokens[*i], access ) )
				return true;
			for( std::list<std::string>::const_iterator read = access.registerReads.begin();
			     read != access.registerReads.end(); ++read )
			{
				if( !containsKey( suffixWrites, *read )
				    || containsKey( prefixWritesSoFar, *read )
				    || containsKey( opportunity.inductionRegisters, *read ) )
					continue;
				return true;
			}
			for( std::list<std::string>::const_iterator write = access.registerWrites.begin();
			     write != access.registerWrites.end(); ++write )
				addUniqueString( prefixWritesSoFar, *write );
		}

		return false;
	}

	VuLoopQSchedulingStrategy classifyLoopQSchedulingStrategy( unsigned int qProducerConsumerGapDeficitCycles,
	                                                           unsigned int loopCarriedQGapCycles,
	                                                           unsigned int qProducerLatency )
	{
		if( qProducerConsumerGapDeficitCycles == 0 )
			return VU_LOOP_Q_SCHEDULE_LOCAL;
		if( loopCarriedQGapCycles >= qProducerLatency )
			return VU_LOOP_Q_SCHEDULE_LOOP_CARRIED;
		return VU_LOOP_Q_SCHEDULE_INSUFFICIENT;
	}

	VuLoopQSchedulingStrategy classifyLoopQSchedulingStrategy( const VuLoopPipelineOpportunity& opportunity )
	{
		return classifyLoopQSchedulingStrategy( opportunity.qProducerConsumerGapDeficitCycles,
		                                        opportunity.loopCarriedQGapCycles,
		                                        opportunity.qProducerLatency );
	}

	void classifySoftwarePipelineEmissionSafety( VuLoopPipelineOpportunity& opportunity,
	                                             const VuLoopCandidate& loop,
	                                             unsigned int qProducerOffset,
	                                             const std::vector<const Token*>& indexedTokens )
	{
		if( !opportunity.simpleCountedLoop )
			addPipelineBlocker( opportunity, "not_simple_counted_loop" );
		if( !opportunity.hasSingleQProducer )
			addPipelineBlocker( opportunity, "multiple_q_producers" );
		if( opportunity.branchDelaySlots == 0 )
			addPipelineBlocker( opportunity, "missing_branch_delay_slot" );
		if( (opportunity.sourcePrefixCycles + opportunity.sourceSuffixCycles) < opportunity.qProducerLatency )
			addPipelineBlocker( opportunity, "insufficient_independent_cycles" );
		if( opportunity.qSchedulingStrategy == VU_LOOP_Q_SCHEDULE_LOCAL )
			addPipelineBlocker( opportunity, "q_latency_already_local" );
		if( opportunity.qSchedulingStrategy == VU_LOOP_Q_SCHEDULE_INSUFFICIENT )
			addPipelineBlocker( opportunity, "insufficient_loop_carried_q_gap" );
		if( opportunity.qProducerInsertionGapDeficitCycles != 0 )
			addPipelineBlocker( opportunity, "insufficient_q_insertion_gap" );
		if( opportunity.hasMemoryPreOrPostIncrement )
			addPipelineBlocker( opportunity, "pre_or_post_increment_memory" );
		if( opportunity.hasXgkick )
			addPipelineBlocker( opportunity, "xgkick_barrier" );
		if( opportunity.inductionRegisters.empty() )
			addPipelineBlocker( opportunity, "missing_induction_register" );
		collectRotatedRegisterBaseKeys( opportunity.carriedQInputRegisters,
		                                opportunity.softwarePipelineRotatedRegisters );
		collectRotatedRegisterBaseKeys( opportunity.carriedQOutputRegisters,
		                                opportunity.softwarePipelineRotatedRegisters );
		collectRotationDescriptors( opportunity.carriedQInputRegisters,
		                            true,
		                            opportunity.softwarePipelineRotations );
		collectRotationDescriptors( opportunity.carriedQOutputRegisters,
		                            false,
		                            opportunity.softwarePipelineRotations );
		collectSoftwarePipelinePrefetchDescriptors( opportunity.prologTokenIndices,
		                                            opportunity.qProducerTokenIndex,
		                                            indexedTokens,
		                                            opportunity );
		retainPrefetchedRotations( opportunity, indexedTokens );
		assignRotationScratchRegisters( loop, opportunity.softwarePipelineRotations );

		const bool canEmitRotations = softwarePipelineRotationsCanEmit( opportunity, indexedTokens );
		if( !opportunity.softwarePipelineRotatedRegisters.empty() && !canEmitRotations )
			addPipelineBlocker( opportunity, "requires_register_rotation" );
		for( std::vector<VuSoftwarePipelineRotation>::const_iterator rotation = opportunity.softwarePipelineRotations.begin();
		     rotation != opportunity.softwarePipelineRotations.end(); ++rotation )
		{
			if( !rotation->hasScratchRegister )
				addPipelineBlocker( opportunity, "missing_rotation_scratch" );
		}

		std::list<std::string> prefetchWritesForSuffixStores;
		collectSoftwarePipelinePrefetchWrites( opportunity,
		                                       indexedTokens,
		                                       canEmitRotations,
		                                       prefetchWritesForSuffixStores );
		if( !opportunity.qConsumerTokenIndices.empty() )
		{
			assignSuffixStoreValueScratchRegisters( loop,
			                                        prefetchWritesForSuffixStores,
			                                        opportunity.softwarePipelineRotations,
			                                        indexedTokens,
			                                        opportunity.qConsumerTokenIndices.back(),
			                                        opportunity.softwarePipelineSuffixStores );
		}
		for( std::vector<VuSoftwarePipelineSuffixStore>::const_iterator store = opportunity.softwarePipelineSuffixStores.begin();
		     store != opportunity.softwarePipelineSuffixStores.end(); ++store )
		{
			if( store->requiresValueRotation && !store->hasValueScratchRegister )
				addPipelineBlocker( opportunity, "missing_suffix_store_value_scratch" );
		}

		classifySuffixStoreDrainOpportunity( opportunity,
		                                     loop,
		                                     canEmitRotations,
		                                     indexedTokens );

		const bool canEmitPrefetches = softwarePipelinePrefetchesCanEmit( opportunity,
		                                                                  indexedTokens,
		                                                                  canEmitRotations );
		if( opportunity.prologTokenIndices.size() != 1
		    || opportunity.prologTokenIndices.front() != opportunity.qProducerTokenIndex )
		{
			if( !canEmitPrefetches )
				addPipelineBlocker( opportunity, "multi_instruction_prefetch" );
		}

		if( !canEmitPrefetches )
		{
			for( std::vector<VuSoftwarePipelinePrefetch>::const_iterator p = opportunity.softwarePipelinePrefetches.begin();
			     p != opportunity.softwarePipelinePrefetches.end(); ++p )
			{
				if( p->memoryKind != VU_MEMORY_NONE )
					addPipelineBlocker( opportunity, "multi_instruction_prefetch_memory" );
				if( p->readsInductionRegister )
					addPipelineBlocker( opportunity, "multi_instruction_prefetch_reads_induction" );
				if( p->readsInductionRegister && !p->hasNextIterationOffset )
					addPipelineBlocker( opportunity, "multi_instruction_prefetch_unknown_next_offset" );
			}

			std::list<std::string> prefetchWrites;
			collectSoftwarePipelinePrefetchWrites( opportunity,
			                                       indexedTokens,
			                                       canEmitRotations,
			                                       prefetchWrites );
			if( prefetchWritesClobberSuffix( opportunity, indexedTokens, prefetchWrites ) )
				addPipelineBlocker( opportunity, "prefetch_clobbers_suffix" );
		}

		if( qProducerOffset < loop.bodyTokens.size() )
		{
			VuTokenResourceAccess access;
			if( buildVuTokenResourceAccess( *loop.bodyTokens[qProducerOffset], access ) )
			{
				if( access.memoryKind != VU_MEMORY_NONE )
					addPipelineBlocker( opportunity, "q_producer_memory" );
				if( intersects( access.registerReads, opportunity.inductionRegisters ) )
					addPipelineBlocker( opportunity, "q_producer_reads_induction" );
			}
		}

		for( unsigned int i = opportunity.branchTokenIndex + 1; i < indexedTokens.size(); ++i )
		{
			if( vuTokenWritesQ( *indexedTokens[i] ) )
				break;
			if( vuTokenReadsQ( *indexedTokens[i] ) )
			{
				opportunity.qLiveOut = true;
				if( !softwarePipelineQDrainCanEmit( opportunity, loop, qProducerOffset ) )
					addPipelineBlocker( opportunity, "q_live_out" );
				break;
			}
		}

		opportunity.canEmitSoftwarePipeline =
		    opportunity.hasSoftwarePipelinePlan && opportunity.softwarePipelineBlockers.empty();
	}

	void classifyMultiQSoftwarePipelineOpportunity( VuLoopPipelineOpportunity& opportunity,
	                                                unsigned int qProducerCount,
	                                                const std::vector<const Token*>& indexedTokens )
	{
		if( qProducerCount <= 1 )
			return;

		opportunity.hasMultiQSoftwarePipelinePlan =
		    !opportunity.multiQPrologTokenIndices.empty()
		    && !opportunity.multiQMainTokenIndices.empty()
		    && !opportunity.multiQCyclicPrefixTokenIndices.empty();
		if( !opportunity.hasMultiQSoftwarePipelinePlan )
			addMultiQPipelineBlocker( opportunity, "missing_cyclic_prefix" );
		if( !opportunity.simpleCountedLoop )
			addMultiQPipelineBlocker( opportunity, "not_simple_counted_loop" );
		if( opportunity.branchDelaySlots == 0 )
			addMultiQPipelineBlocker( opportunity, "missing_branch_delay_slot" );
		if( opportunity.hasMemoryPreOrPostIncrement )
			addMultiQPipelineBlocker( opportunity, "pre_or_post_increment_memory" );
		if( opportunity.hasXgkick )
			addMultiQPipelineBlocker( opportunity, "xgkick_barrier" );
		if( opportunity.inductionRegisters.empty() )
			addMultiQPipelineBlocker( opportunity, "missing_induction_register" );
		if( opportunity.qProducerInsertionGapDeficitCycles != 0 )
			addMultiQPipelineBlocker( opportunity, "insufficient_q_insertion_gap" );
		if( opportunity.qLiveOut )
			addMultiQPipelineBlocker( opportunity, "q_live_out" );
		if( tokenIndicesHaveUnsafeMultiQCyclicPrefixSideEffects( opportunity.multiQCyclicPrefixTokenIndices,
		                                                         indexedTokens,
		                                                         opportunity.multiQCyclicPrefixNeedsGuard ) )
			addMultiQPipelineBlocker( opportunity, "cyclic_prefix_side_effect" );
		if( tokenIndicesContainVisibleLabel( opportunity.multiQCyclicPrefixTokenIndices,
		                                     indexedTokens,
		                                     opportunity.labelTokenIndex )
		    || tokenIndicesContainVisibleLabel( opportunity.multiQMainTokenIndices,
		                                        indexedTokens,
		                                        opportunity.labelTokenIndex )
		    || loopBodyContainsVisibleInternalLabel( opportunity, indexedTokens ) )
			addMultiQPipelineBlocker( opportunity, "cyclic_prefix_or_main_label" );
		if( cyclicPrefixReadsSuffixClobbersBeforeInsertion( opportunity, indexedTokens ) )
			addMultiQPipelineBlocker( opportunity, "cyclic_prefix_reads_suffix_clobber" );
		if( cyclicPrefixClobbersBranch( opportunity, indexedTokens ) )
			addMultiQPipelineBlocker( opportunity, "cyclic_prefix_clobbers_branch" );

		for( std::vector<VuLoopQStage>::const_iterator stage = opportunity.qStages.begin();
		     stage != opportunity.qStages.end(); ++stage )
		{
			if( stage->qConsumerTokenIndices.empty() )
				addMultiQPipelineBlocker( opportunity, "stage_without_consumers" );
		}

		opportunity.eligibleMultiQSoftwarePipeline =
		    opportunity.hasMultiQSoftwarePipelinePlan
		    && opportunity.multiQSoftwarePipelineBlockers.empty();
		opportunity.canEmitMultiQSoftwarePipeline = opportunity.eligibleMultiQSoftwarePipeline;
	}

	Token tokenWithoutLabel( const Token& token )
	{
		Token copy( token );
		copy.setLabel( "" );
		return copy;
	}

	const Operand* syntheticMoveOperand()
	{
		static const Operand moveOperand( "MOVE",
		                                  2,
		                                  Operand::LOWER | Operand::DEST,
		                                  "vf:dest:write,vf:dest",
		                                  Operand::INVALID,
		                                  1,
		                                  4 );
		return &moveOperand;
	}

	unsigned int vfRegisterNumber( const std::string& reg )
	{
		if( reg.size() < 3 )
			return 0;
		return static_cast<unsigned int>( std::atoi( reg.substr( 2 ).c_str() ) );
	}

	bool physicalVfRegisterName( const std::string& reg )
	{
		return reg.size() >= 3
		    && reg[0] == 'V'
		    && reg[1] == 'F'
		    && reg[2] >= '0'
		    && reg[2] <= '9';
	}

	void setFloatRegisterArgumentBase( Token::Argument& argument,
	                                   const std::string& base )
	{
		if( physicalVfRegisterName( base ) )
			argument.setRegNumber( static_cast<int>( vfRegisterNumber( base ) ) );
		else
			argument.setAlias( base );
	}

	unsigned int fieldMaskForFieldList( const std::list<std::string>& fields )
	{
		unsigned int mask = 0;
		for( std::list<std::string>::const_iterator i = fields.begin(); i != fields.end(); ++i )
		{
			if( i->find( 'x' ) != std::string::npos ) mask |= Token::X;
			if( i->find( 'y' ) != std::string::npos ) mask |= Token::Y;
			if( i->find( 'z' ) != std::string::npos ) mask |= Token::Z;
			if( i->find( 'w' ) != std::string::npos ) mask |= Token::W;
		}
		return mask;
	}

	unsigned int rotationFieldMask( const VuSoftwarePipelineRotation& rotation )
	{
		unsigned int mask = fieldMaskForFieldList( rotation.inputFields )
		                  | fieldMaskForFieldList( rotation.outputFields );
		return mask ? mask : (Token::X | Token::Y | Token::Z | Token::W);
	}

	void rewriteRotatedRegistersToScratch( Token& token,
	                                       const std::vector<VuSoftwarePipelineRotation>& rotations )
	{
		for( std::list<Token::Argument>::iterator i = token.arguments().begin(); i != token.arguments().end(); ++i )
		{
			if( i->type() != Token::Argument::FLOAT_REGISTER )
				continue;
			std::string key;
			if( !vuRegisterKey( *i, key ) )
				continue;
			const VuSoftwarePipelineRotation* rotation = findRotationForBase( rotations, registerBaseKey( key ) );
			if( !rotation || !rotation->hasScratchRegister )
				continue;
			i->setRegNumber( static_cast<int>( vfRegisterNumber( rotation->scratchRegister ) ) );
		}
	}

	Token makeRotationMoveToken( const Token& donor, const VuSoftwarePipelineRotation& rotation )
	{
		Token token( donor );
		token.setLabel( "" );
		token.setName( "move" );
		token.setOperand( syntheticMoveOperand() );
		token.setFlags( Token::PROCESSED );
		token.setBroadcast( 0 );
		token.setFields( rotationFieldMask( rotation ) );
		token.arguments().clear();

		Token::Argument dst( rotation.registerBase );
		dst.setType( Token::Argument::FLOAT_REGISTER );
		setFloatRegisterArgumentBase( dst, rotation.registerBase );
		dst.setFlags( Token::Argument::WRITE | Token::Argument::DEST );
		dst.setFields( rotationFieldMask( rotation ) );
		token.arguments().push_back( dst );

		Token::Argument src( rotation.scratchRegister );
		src.setType( Token::Argument::FLOAT_REGISTER );
		setFloatRegisterArgumentBase( src, rotation.scratchRegister );
		src.setFlags( 0 );
		src.setFields( 0 );
		token.arguments().push_back( src );

		return token;
	}

	Token makeSuffixStoreValueMoveToken( const Token& donor,
	                                     const VuSoftwarePipelineSuffixStore& store )
	{
		Token token( donor );
		token.setLabel( "" );
		token.setName( "move" );
		token.setOperand( syntheticMoveOperand() );
		token.setFlags( Token::PROCESSED );
		token.setBroadcast( 0 );
		token.setFields( fieldMaskForFieldList( store.storedValueFields ) );
		token.arguments().clear();

		Token::Argument dst( store.valueScratchRegister );
		dst.setType( Token::Argument::FLOAT_REGISTER );
		setFloatRegisterArgumentBase( dst, store.valueScratchRegister );
		dst.setFlags( Token::Argument::WRITE | Token::Argument::DEST );
		dst.setFields( fieldMaskForFieldList( store.storedValueFields ) );
		token.arguments().push_back( dst );

		Token::Argument src( store.storedValueRegister );
		src.setType( Token::Argument::FLOAT_REGISTER );
		setFloatRegisterArgumentBase( src, store.storedValueRegister );
		src.setFlags( 0 );
		src.setFields( 0 );
		token.arguments().push_back( src );

		return token;
	}

	// Track 9.G step 8b-2c-1: per-field op-splitter for kernel rename.
	//
	// vuOpIsSplittableForKernelRename: returns true when an FMAC op is
	// safe to split into single-field clones. "Safe" means each output
	// field depends only on the corresponding input field (no cross-
	// field arithmetic), the destination is a single VFn with a field
	// mask matching the op's field set, and rewriting the destination
	// register/field-mask preserves observable semantics.
	//
	// Two allowlists:
	//   - splittableBases: FMAC ops that take an optional broadcast
	//     suffix (""/i/q/w/x/y/z).
	//   - splittableSingleForms: per-field-safe ops that have NO
	//     broadcast suffix; matched as exact bare mnemonics.
	//
	// Excludes opmula/opmsub/opmadd/opmsubAcc (cross-field outer-
	// product), clip*, all FDIV/EFU/memory/integer/flag/branch ops
	// other than the per-field-safe singletons listed below, and mr32
	// (lane rotation).
	bool vuOpIsSplittableForKernelRenameByName( const std::string& opNameRaw )
	{
		// Strip the field-mask suffix (".xyzw") and optional flag suffix
		// ("[..]"), then lowercase, to obtain the bare mnemonic. The
		// allowlist is matched against the bare mnemonic.
		std::string opName = opNameRaw;
		std::string::size_type bracket = opName.find( '[' );
		if( bracket != std::string::npos )
			opName = opName.substr( 0, bracket );
		std::string::size_type dot = opName.find( '.' );
		if( dot != std::string::npos )
			opName = opName.substr( 0, dot );
		for( std::string::size_type i = 0; i < opName.size(); ++i )
		{
			if( opName[i] >= 'A' && opName[i] <= 'Z' )
				opName[i] = static_cast<char>( opName[i] - 'A' + 'a' );
		}

		// FMAC broadcast-style bases: base + optional broadcast suffix.
		static const char* const splittableBases[] = {
			"add", "sub", "mul", "madd", "msub", "max", "mini", NULL
		};
		static const char* const splittableSuffixes[] = {
			"", "i", "q", "w", "x", "y", "z", NULL
		};
		for( unsigned int b = 0; splittableBases[b] != NULL; ++b )
		{
			const std::string base = splittableBases[b];
			for( unsigned int s = 0; splittableSuffixes[s] != NULL; ++s )
			{
				std::string candidate = base + splittableSuffixes[s];
				if( opName == candidate )
					return true;
			}
		}

		// Track 9.G step 8b-2d-2: per-field-safe single-form ops with
		// no broadcast variant. Each lane of the destination depends
		// only on the corresponding lane of the source (or, for mfir,
		// is independent of any other lane), so cloning per
		// destination field is semantics-preserving.
		//   ftoi*/itof* : float<->int conversion, lane-wise.
		//   abs         : absolute value, lane-wise.
		//   move        : register copy, lane-wise.
		//   mfir        : move from integer reg into a single VF lane;
		//                 destination is always one lane, so the
		//                 single-clone case is the identity.
		static const char* const splittableSingleForms[] = {
			"ftoi0", "ftoi4", "ftoi12", "ftoi15",
			"itof0", "itof4", "itof12", "itof15",
			"abs", "move", "mfir", NULL
		};
		for( unsigned int s = 0; splittableSingleForms[s] != NULL; ++s )
		{
			if( opName == splittableSingleForms[s] )
				return true;
		}
		return false;
	}

	// Extract the field mask encoded in a decision.reg string of the
	// form "VF13.w" or "VF13.xyz". Returns 0 if there is no dot.
	unsigned int decisionRegFieldMask( const std::string& reg )
	{
		std::string::size_type dot = reg.find( '.' );
		if( dot == std::string::npos )
			return 0;
		std::list<std::string> oneField;
		oneField.push_back( reg.substr( dot + 1 ) );
		return fieldMaskForFieldList( oneField );
	}

	std::string decisionRegBase( const std::string& reg )
	{
		return registerBaseKey( reg );
	}

	// Find the destination FLOAT_REGISTER argument of a token, or NULL
	// if there isn't exactly one.
	Token::Argument* tokenDestinationFloatArgument( Token& token )
	{
		Token::Argument* dest = NULL;
		for( std::list<Token::Argument>::iterator i = token.arguments().begin();
		     i != token.arguments().end(); ++i )
		{
			if( i->type() != Token::Argument::FLOAT_REGISTER )
				continue;
			if( ( i->flags() & Token::Argument::DEST ) == 0 )
				continue;
			if( ( i->flags() & Token::Argument::WRITE ) == 0 )
				continue;
			if( dest != NULL )
				return NULL; // more than one dest — unsupported
			dest = &*i;
		}
		return dest;
	}

	bool destinationBaseMatchesAnyDecision( const Token& token,
	                                        const std::vector<VuKernelRenameDecision>& decisions,
	                                        std::string& destBaseOut )
	{
		for( std::list<Token::Argument>::const_iterator i = token.arguments().begin();
		     i != token.arguments().end(); ++i )
		{
			if( i->type() != Token::Argument::FLOAT_REGISTER )
				continue;
			if( ( i->flags() & Token::Argument::DEST ) == 0 )
				continue;
			if( ( i->flags() & Token::Argument::WRITE ) == 0 )
				continue;
			std::string key;
			if( !vuRegisterKey( *i, key ) )
				continue;
			std::string base = registerBaseKey( key );
			for( unsigned int d = 0; d < decisions.size(); ++d )
			{
				if( !decisions[d].assigned )
					continue;
				if( decisionRegBase( decisions[d].reg ) == base )
				{
					destBaseOut = base;
					return true;
				}
			}
		}
		return false;
	}

	// Split a multi-field FMAC op into single-field clones, one per
	// destination field that is targeted by a decision matching the
	// op's destination base. Fields not covered by any decision are
	// emitted as one residual clone with the destination unchanged.
	//
	// Pre-conditions (caller's responsibility):
	//   - vuOpIsSplittableForKernelRename( token.name() ) is true,
	//   - the token has exactly one destination FLOAT_REGISTER arg.
	//
	// If the op is unsplittable OR no decision matches the destination
	// base, the token is passed through unchanged (single clone in
	// `out`). This makes the helper safe to call unconditionally.
	void splitMultiFieldOpByFieldDecisionsImpl( const Token& token,
	                                            const std::vector<VuKernelRenameDecision>& decisions,
	                                            std::list<Token>& out )
	{
		if( !vuOpIsSplittableForKernelRenameByName( token.name() ) )
		{
			out.push_back( token );
			return;
		}

		std::string destBase;
		if( !destinationBaseMatchesAnyDecision( token, decisions, destBase ) )
		{
			out.push_back( token );
			return;
		}

		// Gather (mask, scratch) for every decision whose base matches.
		std::vector<unsigned int> decisionMasks;
		std::vector<std::string>  decisionScratches;
		unsigned int coveredMask = 0;
		for( unsigned int d = 0; d < decisions.size(); ++d )
		{
			if( !decisions[d].assigned )
				continue;
			if( decisionRegBase( decisions[d].reg ) != destBase )
				continue;
			unsigned int m = decisionRegFieldMask( decisions[d].reg );
			if( m == 0 ) m = Token::X | Token::Y | Token::Z | Token::W;
			decisionMasks.push_back( m );
			decisionScratches.push_back( decisions[d].scratch );
			coveredMask |= m;
		}

		// Determine the destination's effective field-mask. Some parser
		// paths leave token.fields() as zero and store the mask on the
		// destination argument; others set token.fields(). Use the
		// union, and fall back to all four fields when neither is set.
		unsigned int tokenMask = token.fields();
		{
			for( std::list<Token::Argument>::const_iterator i = token.arguments().begin();
			     i != token.arguments().end(); ++i )
			{
				if( i->type() != Token::Argument::FLOAT_REGISTER ) continue;
				if( ( i->flags() & Token::Argument::DEST ) == 0 ) continue;
				if( ( i->flags() & Token::Argument::WRITE ) == 0 ) continue;
				tokenMask |= i->fields();
			}
		}
		if( tokenMask == 0 )
			tokenMask = Token::X | Token::Y | Token::Z | Token::W;
		const unsigned int touchedMask = tokenMask & coveredMask;
		const unsigned int residualMask = tokenMask & ~coveredMask;

		if( touchedMask == 0 )
		{
			// Decisions exist for the base but don't intersect this op's
			// field set; pass the op through unchanged.
			out.push_back( token );
			return;
		}

		// Emit one single-field clone per touched field, retargeted to
		// the scratch register of the decision that owns that field.
		const unsigned int fieldBits[ 4 ] = { Token::X, Token::Y, Token::Z, Token::W };
		for( unsigned int fi = 0; fi < 4; ++fi )
		{
			const unsigned int bit = fieldBits[ fi ];
			if( ( touchedMask & bit ) == 0 )
				continue;
			std::string scratch;
			for( unsigned int d = 0; d < decisionMasks.size(); ++d )
			{
				if( decisionMasks[d] & bit )
				{
					scratch = decisionScratches[d];
					break;
				}
			}
			if( scratch.empty() )
				continue; // should not happen; coveredMask is the union
			Token clone( token );
			clone.setFields( bit );
			Token::Argument* dst = tokenDestinationFloatArgument( clone );
			if( dst != NULL )
			{
				setFloatRegisterArgumentBase( *dst, scratch );
				dst->setFields( bit );
			}
			// Track 9.G step 8b-2e: clear multi-bit field annotations on
			// non-destination FLOAT_REGISTER sources. The parser may
			// stamp the opcode-level mask (e.g. .xyz) onto source args
			// too; the printer would then render the source as
			// `VFnxyz` (no dot), which dvp-as rejects. Genuine
			// broadcast sources carry exactly one bit and are kept.
			for( std::list<Token::Argument>::iterator a = clone.arguments().begin();
			     a != clone.arguments().end(); ++a )
			{
				if( a->type() != Token::Argument::FLOAT_REGISTER )
					continue;
				if( ( a->flags() & Token::Argument::DEST ) && ( a->flags() & Token::Argument::WRITE ) )
					continue;
				const unsigned int af = a->fields();
				const bool singleBit = ( af != 0 ) && ( ( af & ( af - 1 ) ) == 0 );
				if( !singleBit )
					a->setFields( 0 );
			}
			out.push_back( clone );
		}

		if( residualMask != 0 )
		{
			Token clone( token );
			clone.setFields( residualMask );
			Token::Argument* dst = tokenDestinationFloatArgument( clone );
			if( dst != NULL )
				dst->setFields( residualMask );
			// Same source-arg sanitization as the per-field clones.
			for( std::list<Token::Argument>::iterator a = clone.arguments().begin();
			     a != clone.arguments().end(); ++a )
			{
				if( a->type() != Token::Argument::FLOAT_REGISTER )
					continue;
				if( ( a->flags() & Token::Argument::DEST ) && ( a->flags() & Token::Argument::WRITE ) )
					continue;
				const unsigned int af = a->fields();
				const bool singleBit = ( af != 0 ) && ( ( af & ( af - 1 ) ) == 0 );
				if( !singleBit )
					a->setFields( 0 );
			}
			out.push_back( clone );
		}
	}

	// Track 9.G step 8b-2c-3: per-field MOVE for kernel rename.
	// Emits `move.<field> <decision.reg base>, <decision.scratch>` where
	// <field> is derived from the decision's reg suffix (e.g. "VF13.w"
	// -> W); if no suffix, defaults to XYZW. The donor token supplies
	// source-position metadata only; its arguments are discarded.
	Token makeKernelRenameMoveToken( const Token& donor, const VuKernelRenameDecision& decision )
	{
		const unsigned int fieldMask = decisionRegFieldMask( decision.reg ) != 0
		                                   ? decisionRegFieldMask( decision.reg )
		                                   : (Token::X | Token::Y | Token::Z | Token::W);
		const std::string  dstBase   = decisionRegBase( decision.reg );

		Token token( donor );
		token.setLabel( "" );
		token.setName( "move" );
		token.setOperand( syntheticMoveOperand() );
		token.setFlags( Token::PROCESSED );
		token.setBroadcast( 0 );
		token.setFields( fieldMask );
		token.arguments().clear();

		Token::Argument dst( dstBase );
		dst.setType( Token::Argument::FLOAT_REGISTER );
		setFloatRegisterArgumentBase( dst, dstBase );
		dst.setFlags( Token::Argument::WRITE | Token::Argument::DEST );
		dst.setFields( fieldMask );
		token.arguments().push_back( dst );

		Token::Argument src( decision.scratch );
		src.setType( Token::Argument::FLOAT_REGISTER );
		setFloatRegisterArgumentBase( src, decision.scratch );
		src.setFlags( 0 );
		src.setFields( 0 );
		token.arguments().push_back( src );

		return token;
	}

	const VuSoftwarePipelinePrefetch* findPrefetchForTokenIndex( const std::vector<VuSoftwarePipelinePrefetch>& prefetches,
	                                                             unsigned int tokenIndex )
	{
		for( std::vector<VuSoftwarePipelinePrefetch>::const_iterator i = prefetches.begin(); i != prefetches.end(); ++i )
		{
			if( i->tokenIndex == tokenIndex )
				return &*i;
		}
		return NULL;
	}

	bool setIndirectMemoryOffset( Token& token, const std::string& baseRegister, long offset )
	{
		for( std::list<Token::Argument>::iterator i = token.arguments().begin(); i != token.arguments().end(); ++i )
		{
			if( i->type() != Token::Argument::INTEGER_REGISTER
			    || !(i->flags() & Token::Argument::INDIRECT) )
				continue;
			std::string key;
			if( !vuRegisterKey( *i, key ) || key != baseRegister )
				continue;
			std::stringstream s;
			s << offset;
			i->setImmediate( s.str() );
			return true;
		}
		return false;
	}

	bool tokenHasSchedulingBoundaryForStoreBaseAdvance( const Token& token )
	{
		if( token.label().length() != 0 )
			return true;
		if( isVuSchedulingBarrier( token ) )
			return true;
		if( token.flags() & (Token::BRANCH_DELAY_FILLER | Token::PREORDERED) )
			return true;
		if( token.operand() && token.operand()->isPreprocessor() )
			return true;
		return false;
	}

	bool storeUsesBaseWithKnownOffset( const Token& token,
	                                   const std::string& baseRegister,
	                                   long& offset )
	{
		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess( token, access ) )
			return false;
		if( access.memoryKind != VU_MEMORY_STORE )
			return false;
		if( access.memoryFlags != VU_MEMORY_FLAG_NONE )
			return false;
		if( !access.hasMemoryBase || !access.hasMemoryOffset )
			return false;
		if( access.memoryBaseRegister != baseRegister )
			return false;
		offset = access.memoryOffset;
		return true;
	}

	bool tokenReadsOrWritesRegisterKey( const Token& token, const std::string& registerKey )
	{
		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess( token, access ) )
			return false;
		return containsKey( access.registerReads, registerKey )
		    || containsKey( access.registerWrites, registerKey );
	}

	Token adjustedPrefetchToken( const Token& token,
	                             const VuSoftwarePipelinePrefetch* prefetch,
	                             const std::vector<VuSoftwarePipelineRotation>& rotations )
	{
		Token copy = tokenWithoutLabel( token );
		if( prefetch && prefetch->hasNextIterationOffset && prefetch->hasMemoryBase )
			setIndirectMemoryOffset( copy, prefetch->memoryBaseRegister, prefetch->nextIterationOffset );
		rewriteRotatedRegistersToScratch( copy, rotations );
		return copy;
	}

	Token adjustedQProducerToken( const Token& token,
	                              const std::vector<VuSoftwarePipelineRotation>& rotations )
	{
		Token copy = tokenWithoutLabel( token );
		rewriteRotatedRegistersToScratch( copy, rotations );
		return copy;
	}

	void rewriteSuffixStoreValueToScratch( Token& token,
	                                       const VuSoftwarePipelineSuffixStore& store );

	Token adjustedDelayedSuffixStoreToken( const Token& token,
	                                       const VuSoftwarePipelineSuffixStore& store )
	{
		Token copy = tokenWithoutLabel( token );
		if( store.hasNextIterationOffset && store.hasMemoryBase )
			setIndirectMemoryOffset( copy, store.memoryBaseRegister, store.nextIterationOffset );
		rewriteSuffixStoreValueToScratch( copy, store );
		return copy;
	}

	void rewriteSuffixStoreValueToScratch( Token& token,
	                                       const VuSoftwarePipelineSuffixStore& store )
	{
		if( !store.requiresValueRotation || !store.hasValueScratchRegister )
			return;
		for( std::list<Token::Argument>::iterator i = token.arguments().begin();
		     i != token.arguments().end(); ++i )
		{
			if( i->type() != Token::Argument::FLOAT_REGISTER
			    || (i->flags() & (Token::Argument::INDIRECT | Token::Argument::WRITE)) )
				continue;
			std::string key;
			if( !vuRegisterKey( *i, key ) || key != store.storedValueRegister )
				continue;
			i->setRegNumber( static_cast<int>( vfRegisterNumber( store.valueScratchRegister ) ) );
			return;
		}
	}

	void appendRotationMoves( std::list<Token>& output,
	                          const Token& donor,
	                          const std::vector<VuSoftwarePipelineRotation>& rotations )
	{
		for( std::vector<VuSoftwarePipelineRotation>::const_iterator i = rotations.begin(); i != rotations.end(); ++i )
		{
			if( i->hasScratchRegister )
				output.push_back( makeRotationMoveToken( donor, *i ) );
		}
	}

	void appendSuffixStoreValueRotationMoves( std::list<Token>& output,
	                                          const Token& donor,
	                                          const std::vector<VuSoftwarePipelineSuffixStore>& stores )
	{
		for( std::vector<VuSoftwarePipelineSuffixStore>::const_iterator i = stores.begin();
		     i != stores.end(); ++i )
		{
			if( i->requiresValueRotation
			    && i->hasValueScratchRegister
			    && (!i->delayedDrain || i->rotateValueBeforePrefetch) )
				output.push_back( makeSuffixStoreValueMoveToken( donor, *i ) );
		}
	}

	void appendDelayedSuffixStores( std::list<Token>& output,
	                                const std::vector<const Token*>& indexedTokens,
	                                const VuSoftwarePipelineRewritePlan& rewrite )
	{
		for( std::vector<VuSoftwarePipelineSuffixStore>::const_iterator i = rewrite.suffixStores.begin();
		     i != rewrite.suffixStores.end(); ++i )
		{
			if( !i->delayedDrain || i->tokenIndex >= indexedTokens.size() )
				continue;
			output.push_back( adjustedDelayedSuffixStoreToken( *indexedTokens[i->tokenIndex], *i ) );
		}
	}

	bool rewriteDelaysCyclicPrefixTokenInBranchSlot( const VuSoftwarePipelineRewritePlan& rewrite,
	                                                 unsigned int tokenIndex )
	{
		return rewrite.cyclicPrefixLastTokenInBranchDelaySlot
		    && !rewrite.cyclicPrefixTokenIndices.empty()
		    && rewrite.cyclicPrefixTokenIndices.back() == tokenIndex;
	}

	Token adjustedCyclicPrefixBranchDelayToken( const Token& token,
	                                            const VuSoftwarePipelineRewritePlan& rewrite )
	{
		Token copy =
		    adjustedMultiQCyclicPrefixToken( token,
		                                     rewrite.cyclicPrefixRotations,
		                                     rewrite.inductionUpdates,
		                                     rewrite.cyclicPrefixInsertBeforeTokenIndex );
		copy.setFlags( copy.flags() | Token::BRANCH_DELAY_FILLER );
		return copy;
	}

	void appendCyclicPrefixTokens( std::list<Token>& output,
	                               const std::vector<const Token*>& indexedTokens,
	                               const VuSoftwarePipelineRewritePlan& rewrite,
	                               bool includeBranchDelayToken )
	{
		for( std::vector<unsigned int>::const_iterator p = rewrite.cyclicPrefixTokenIndices.begin();
		     p != rewrite.cyclicPrefixTokenIndices.end(); ++p )
		{
			if( *p >= indexedTokens.size() )
				continue;
			if( !includeBranchDelayToken
			    && rewriteDelaysCyclicPrefixTokenInBranchSlot( rewrite, *p ) )
				continue;
			output.push_back( adjustedMultiQCyclicPrefixToken( *indexedTokens[*p],
			                                                   rewrite.cyclicPrefixRotations,
			                                                   rewrite.inductionUpdates,
			                                                   rewrite.cyclicPrefixInsertBeforeTokenIndex ) );
		}
	}

	void appendDelayedCyclicPrefixBranchToken( std::list<Token>& output,
	                                           const std::vector<const Token*>& indexedTokens,
	                                           const VuSoftwarePipelineRewritePlan& rewrite )
	{
		if( !rewrite.cyclicPrefixLastTokenInBranchDelaySlot
		    || rewrite.cyclicPrefixTokenIndices.empty() )
			return;
		const unsigned int tokenIndex = rewrite.cyclicPrefixTokenIndices.back();
		if( tokenIndex < indexedTokens.size() )
			output.push_back( adjustedCyclicPrefixBranchDelayToken( *indexedTokens[tokenIndex], rewrite ) );
	}

	void appendRewriteMainToken( std::list<Token>& output,
	                             const std::vector<const Token*>& indexedTokens,
	                             const VuSoftwarePipelineRewritePlan& rewrite,
	                             unsigned int tokenIndex,
	                             bool skipDelayedStores,
	                             bool invertBranchToDrain )
	{
		if( tokenIndex >= indexedTokens.size() )
			return;

		Token token = tokenWithoutLabel( *indexedTokens[tokenIndex] );
		const VuSoftwarePipelineSuffixStore* suffixStore =
		    findSuffixStoreForTokenIndex( rewrite.suffixStores, tokenIndex );
		if( suffixStore && suffixStore->delayedDrain && skipDelayedStores )
		{
			if( suffixStore->requiresValueRotation
			    && suffixStore->hasValueScratchRegister
			    && suffixStore->rotateValueAtStore )
				output.push_back( makeSuffixStoreValueMoveToken( *indexedTokens[tokenIndex],
				                                                 *suffixStore ) );
			return;
		}
		if( suffixStore )
			rewriteSuffixStoreValueToScratch( token, *suffixStore );

		if( tokenIndex == rewrite.branchTokenIndex && invertBranchToDrain )
			output.push_back( invertedBranchToDrainToken( *indexedTokens[tokenIndex],
			                                             rewrite.drainLabel ) );
		else
			output.push_back( token );
	}

	void appendCyclicPrefixMainBody( std::list<Token>& output,
	                                 const std::vector<const Token*>& indexedTokens,
	                                 const VuSoftwarePipelineRewritePlan& rewrite,
	                                 bool skipDelayedStores,
	                                 bool invertBranchToDrain,
	                                 bool allowBranchDelayCyclicPrefix )
	{
		bool emittedCyclicPrefix = false;
		for( std::vector<unsigned int>::const_iterator m = rewrite.mainTokenIndices.begin();
		     m != rewrite.mainTokenIndices.end(); ++m )
		{
			if( !emittedCyclicPrefix && *m == rewrite.cyclicPrefixInsertBeforeTokenIndex )
			{
				if( rewrite.cyclicPrefixNeedsGuard && rewrite.branchTokenIndex < indexedTokens.size() )
					output.push_back( invertedBranchToDrainToken( *indexedTokens[rewrite.branchTokenIndex],
					                                               rewrite.drainLabel ) );
				appendCyclicPrefixTokens( output, indexedTokens, rewrite, false );
				emittedCyclicPrefix = true;
			}
			if( *m < indexedTokens.size() && *m != rewrite.branchTokenIndex )
				appendRewriteMainToken( output,
				                        indexedTokens,
				                        rewrite,
				                        *m,
				                        skipDelayedStores,
				                        invertBranchToDrain );
		}
		if( !emittedCyclicPrefix )
		{
			if( rewrite.cyclicPrefixNeedsGuard && rewrite.branchTokenIndex < indexedTokens.size() )
				output.push_back( invertedBranchToDrainToken( *indexedTokens[rewrite.branchTokenIndex],
				                                               rewrite.drainLabel ) );
			appendCyclicPrefixTokens( output, indexedTokens, rewrite, false );
		}
		if( rewrite.branchTokenIndex < indexedTokens.size() )
		{
			appendRewriteMainToken( output,
			                        indexedTokens,
			                        rewrite,
			                        rewrite.branchTokenIndex,
			                        skipDelayedStores,
			                        invertBranchToDrain );
			if( allowBranchDelayCyclicPrefix && !rewrite.cyclicPrefixNeedsGuard )
				appendDelayedCyclicPrefixBranchToken( output, indexedTokens, rewrite );
		}
	}

	void appendTokenRangeWithInsertedPrefetchAndQProducer( std::list<Token>& output,
	                                                       const std::vector<const Token*>& indexedTokens,
	                                                       const VuSoftwarePipelineRewritePlan& rewrite,
	                                                       unsigned int beginIndex,
	                                                       unsigned int endIndex,
	                                                       bool skipDelayedStores = false,
	                                                       bool invertBranchToDrain = false )
	{
		for( unsigned int i = beginIndex; i <= endIndex && i < indexedTokens.size(); ++i )
		{
			if( i == rewrite.branchTokenIndex && !rewrite.rotations.empty() )
				appendRotationMoves( output, *indexedTokens[i], rewrite.rotations );
			Token token = tokenWithoutLabel( *indexedTokens[i] );
			const VuSoftwarePipelineSuffixStore* suffixStore =
			    findSuffixStoreForTokenIndex( rewrite.suffixStores, i );
			if( suffixStore && suffixStore->delayedDrain && skipDelayedStores )
			{
				if( suffixStore->requiresValueRotation
				    && suffixStore->hasValueScratchRegister
				    && suffixStore->rotateValueAtStore )
					output.push_back( makeSuffixStoreValueMoveToken( *indexedTokens[i], *suffixStore ) );
			}
			else if( suffixStore )
				rewriteSuffixStoreValueToScratch( token, *suffixStore );
			if( !(suffixStore && suffixStore->delayedDrain && skipDelayedStores) )
			{
				if( i == rewrite.branchTokenIndex && invertBranchToDrain )
					output.push_back( invertedBranchToDrainToken( *indexedTokens[i], rewrite.drainLabel ) );
				else
					output.push_back( token );
			}
			if( i == rewrite.prefetchInsertAfterTokenIndex )
			{
				appendSuffixStoreValueRotationMoves( output,
				                                     *indexedTokens[i],
				                                     rewrite.suffixStores );
				for( std::vector<unsigned int>::const_iterator p = rewrite.prefetchTokenIndices.begin();
				     p != rewrite.prefetchTokenIndices.end(); ++p )
				{
					if( *p >= indexedTokens.size() )
						continue;
					output.push_back( adjustedPrefetchToken( *indexedTokens[*p],
					                                        findPrefetchForTokenIndex( rewrite.prefetches, *p ),
					                                        rewrite.rotations ) );
				}
			}
			if( i == rewrite.qProducerInsertAfterTokenIndex )
			{
				if( rewrite.qProducerTokenIndex < indexedTokens.size() )
				{
					Token qProducer = adjustedQProducerToken( *indexedTokens[rewrite.qProducerTokenIndex],
					                                          rewrite.rotations );
					if( rewrite.qProducerInBranchDelaySlot )
						qProducer.setFlags( qProducer.flags() | Token::BRANCH_DELAY_FILLER );
					output.push_back( qProducer );
				}
			}
		}
	}
}

VuScheduledPaddingKind vuScheduledPaddingKindForReadHazard( const Token& token,
                                                            const Token* partner,
                                                            const VuLatencyTracker& latencyTracker,
                                                            int currentCycle )
{
	bool readsQ = vuTokenReadsQ( token );
	bool readsP = vuTokenReadsP( token );
	if( partner )
	{
		readsQ = readsQ || vuTokenReadsQ( *partner );
		readsP = readsP || vuTokenReadsP( *partner );
	}

	const int qGap = readsQ ? (latencyTracker.qReadyCycle() - currentCycle) : 0;
	const int pGap = readsP ? (latencyTracker.pReadyCycle() - currentCycle) : 0;
	if( qGap > 1 && qGap >= pGap )
		return VU_SCHEDULED_PADDING_WAITQ;
	if( pGap > 1 )
		return VU_SCHEDULED_PADDING_WAITP;
	return VU_SCHEDULED_PADDING_NOP;
}

VuBasicBlock::VuBasicBlock()
{
	firstTokenIndex = 0;
	terminatedByBarrier = false;
	terminatorKind = VU_BASIC_BLOCK_TERMINATOR_NONE;
	terminator = NULL;
}

VuDependencyEdge::VuDependencyEdge()
{
	before = 0;
	after = 0;
	kind = VU_DEPENDENCY_REGISTER_RAW;
}

VuDependencyEdge::VuDependencyEdge( unsigned int beforeToken, unsigned int afterToken, VuDependencyKind dependencyKind )
{
	before = beforeToken;
	after = afterToken;
	kind = dependencyKind;
}

VuScheduledIssueSlot::VuScheduledIssueSlot()
{
	firstToken = NULL;
	secondToken = NULL;
	upperToken = NULL;
	lowerToken = NULL;
	firstTokenIndex = VU_SCHEDULED_TOKEN_INDEX_NONE;
	secondTokenIndex = VU_SCHEDULED_TOKEN_INDEX_NONE;
	upperTokenIndex = VU_SCHEDULED_TOKEN_INDEX_NONE;
	lowerTokenIndex = VU_SCHEDULED_TOKEN_INDEX_NONE;
	padding = false;
	paddingKind = VU_SCHEDULED_PADDING_NONE;
	ignoredImplicitWawResources = VU_RESOURCE_NONE;
	issueCycle = 0;
	cycleCount = 1;
	emitCycleCount = 1;
}

VuScheduledBasicBlock::VuScheduledBasicBlock()
{
	firstIssueCycle = 0;
	cycleCount = 0;
}

VuScheduledProgram::VuScheduledProgram()
{
	cycleCount = 0;
}

VuLoopCandidate::VuLoopCandidate()
{
	labelTokenIndex = 0;
	branchTokenIndex = 0;
	firstBodyTokenIndex = 0;
	lastBodyTokenIndex = 0;
	hasLoopDirective = false;
	loopCsClid = 0;
	loopCsMlid = 0;
	simpleCountedLoop = false;
	memoryLoadCount = 0;
	memoryStoreCount = 0;
	hasMemoryPreOrPostIncrement = false;
	hasXgkick = false;
	branchToken = NULL;
}

VuLoopQStage::VuLoopQStage()
{
	qProducerTokenIndex = 0;
	qProducerLatency = 0;
	qProducerConsumerGapCycles = 0;
	qProducerConsumerGapDeficitCycles = 0;
	loopCarriedQGapCycles = 0;
	qProducerInsertionGapCycles = 0;
	qProducerInsertionGapDeficitCycles = 0;
	qSchedulingStrategy = VU_LOOP_Q_SCHEDULE_INSUFFICIENT;
}

VuLoopPipelineOpportunity::VuLoopPipelineOpportunity()
{
	labelTokenIndex = 0;
	branchTokenIndex = 0;
	qProducerTokenIndex = 0;
	firstQConsumerTokenIndex = 0;
	lastQConsumerTokenIndex = 0;
	qProducerLatency = 0;
	qProducerConsumerGapCycles = 0;
	qProducerConsumerGapDeficitCycles = 0;
	loopCarriedQGapCycles = 0;
	qProducerInsertionGapCycles = 0;
	qProducerInsertionGapDeficitCycles = 0;
	qSchedulingStrategy = VU_LOOP_Q_SCHEDULE_INSUFFICIENT;
	sourcePrefixCycles = 0;
	sourceSuffixCycles = 0;
	branchDelaySlots = 0;
	loopCsClid = 0;
	loopCsMlid = 0;
	simpleCountedLoop = false;
	hasSingleQProducer = false;
	requiresPrologEpilog = false;
	requiresLoopCarriedRegisters = false;
	qLiveOut = false;
	eligibleSingleQSoftwarePipeline = false;
	hasSoftwarePipelinePlan = false;
	canEmitSoftwarePipeline = false;
	eligibleMultiQSoftwarePipeline = false;
	hasMultiQSoftwarePipelinePlan = false;
	canEmitMultiQSoftwarePipeline = false;
	hasSuffixStoreDrainPlan = false;
	canEmitSuffixStoreDrain = false;
	multiQCyclicPrefixInsertBeforeTokenIndex = VU_SCHEDULED_TOKEN_INDEX_NONE;
	multiQCyclicPrefixNeedsGuard = false;
	multiQCyclicPrefixLastTokenInBranchDelaySlot = false;
	memoryLoadCount = 0;
	memoryStoreCount = 0;
	hasMemoryPreOrPostIncrement = false;
	hasXgkick = false;
	stageCount = 1;
	kernelRewriteII = 0;
	kernelRewriteStageCount = 0;
	kernelRewriteConflicts = 0;
	kernelRewriteRegCount = 0;
	kernelRewriteWawCount = 0;
	kernelRewriteRawCount = 0;
	kernelRewriteWarCount = 0;
	kernelEnvelopeKernelTokens = 0;
	kernelEnvelopePrologueCycles = 0;
	kernelEnvelopeEpilogueCycles = 0;
	kernelEnvelopeConflicts = 0;
	kernelRewriteRefitII = 0;
	kernelRewriteRefitStageCount = 0;
	kernelRewriteRefitConflicts = 0;
	kernelRewriteRefitMainTokenCount = 0;
}

VuSoftwarePipelineRewritePlan::VuSoftwarePipelineRewritePlan()
{
	labelTokenIndex = 0;
	branchTokenIndex = 0;
	qProducerTokenIndex = 0;
	prefetchInsertAfterTokenIndex = 0;
	qProducerInsertAfterTokenIndex = 0;
	qProducerInBranchDelaySlot = false;
	qProducerBranchDelayBlockedBySuffixDependency = false;
	qProducerBranchDelaySuffixBlockerTokenIndex = 0;
	cyclicPrefixBeforeBranch = false;
	cyclicPrefixInsertBeforeTokenIndex = VU_SCHEDULED_TOKEN_INDEX_NONE;
	cyclicPrefixNeedsGuard = false;
	cyclicPrefixLastTokenInBranchDelaySlot = false;
	drainsSuffixStores = false;
	emitsDrain = false;
	stageCount = 1;
	kernelRewriteII = 0;
	kernelRewriteStageCount = 0;
	kernelRewriteConflicts = 0;
	kernelRewriteRegCount = 0;
	kernelRewriteWawCount = 0;
	kernelRewriteRawCount = 0;
	kernelRewriteWarCount = 0;
	kernelEnvelopeKernelTokens = 0;
	kernelEnvelopePrologueCycles = 0;
	kernelEnvelopeEpilogueCycles = 0;
	kernelEnvelopeConflicts = 0;
	kernelRewriteRefitII = 0;
	kernelRewriteRefitStageCount = 0;
	kernelRewriteRefitConflicts = 0;
	kernelRewriteRefitMainTokenCount = 0;
}

std::vector<VuBasicBlock> buildVuBasicBlocks( const std::list<Token>& tokens )
{
	std::vector<VuBasicBlock> blocks;
	VuBasicBlock current;
	bool hasCurrent = false;
	unsigned int index = 0;

	for( std::list<Token>::const_iterator i = tokens.begin(); i != tokens.end(); ++i, ++index )
	{
		if( (*i).label().length() != 0 && hasCurrent )
		{
			blocks.push_back( current );
			current = VuBasicBlock();
			hasCurrent = false;
		}

		if( !hasCurrent )
		{
			current = VuBasicBlock();
			current.firstTokenIndex = index;
			hasCurrent = true;
		}

		current.tokens.push_back( &*i );

		if( isVuSchedulingBarrier( *i ) )
		{
			current.terminatedByBarrier = true;
			current.terminatorKind = vuBlockTerminatorKind( *i );
			current.terminator = &*i;
			blocks.push_back( current );
			current = VuBasicBlock();
			hasCurrent = false;
		}
	}

	if( hasCurrent )
		blocks.push_back( current );

	return blocks;
}

std::vector<VuDependencyEdge> buildVuDependencyGraph( const VuBasicBlock& block,
                                                      unsigned int ignoredImplicitWawResources )
{
	std::vector<VuDependencyEdge> edges;
	std::vector<VuTokenResourceAccess> accesses;
	for( std::vector<const Token*>::const_iterator i = block.tokens.begin(); i != block.tokens.end(); ++i )
	{
		VuTokenResourceAccess access;
		buildVuTokenResourceAccess( **i, access );
		accesses.push_back( access );
	}

	for( unsigned int before = 0; before < accesses.size(); ++before )
	{
		for( unsigned int after = before + 1; after < accesses.size(); ++after )
		{
			const VuTokenResourceAccess& a = accesses[before];
			const VuTokenResourceAccess& b = accesses[after];
			// STATUS is here for the same reason ACC/MAC/CLIP are, and more urgently:
			// left in the pairwise set its WAW test would fire between every pair of
			// FMACs in the block. addAccumulatingImplicitFlagDependencies models it.
			// The WAR test below is deliberately NOT masked - it is what keeps a flag
			// reader in front of the writers that follow it, for every resource.
			const unsigned int preciseImplicitResources =
				VU_RESOURCE_ACC | VU_RESOURCE_MAC | VU_RESOURCE_CLIP | VU_RESOURCE_STATUS;
			const unsigned int pairwiseImplicitResources = ~preciseImplicitResources;

			if( intersects( a.registerWrites, b.registerReads ) )
				addEdge( edges, before, after, VU_DEPENDENCY_REGISTER_RAW );
			if( intersects( a.registerReads, b.registerWrites ) )
				addEdge( edges, before, after, VU_DEPENDENCY_REGISTER_WAR );
			if( intersects( a.registerWrites, b.registerWrites ) )
				addEdge( edges, before, after, VU_DEPENDENCY_REGISTER_WAW );

			if( a.implicitWrites & b.implicitReads & pairwiseImplicitResources )
				addEdge( edges, before, after, VU_DEPENDENCY_RESOURCE_RAW );
			if( a.implicitReads & b.implicitWrites )
				addEdge( edges, before, after, VU_DEPENDENCY_RESOURCE_WAR );
			if( (a.implicitWrites & b.implicitWrites & pairwiseImplicitResources
			     & ~ignoredImplicitWawResources) != 0 )
				addEdge( edges, before, after, VU_DEPENDENCY_RESOURCE_WAW );

			if( memoryOrderRequiresDependency( a,
			                                  b,
			                                  *block.tokens[before],
			                                  *block.tokens[after] ) )
				addEdge( edges, before, after, VU_DEPENDENCY_MEMORY );
		}
	}

		addPreciseImplicitFlagDependencies( edges,
		                                    accesses,
		                                    VU_RESOURCE_ACC,
		                                    ignoredImplicitWawResources );
		addPreciseImplicitFlagDependencies( edges,
		                                    accesses,
		                                    VU_RESOURCE_MAC,
		                                    ignoredImplicitWawResources );
	addPreciseImplicitFlagDependencies( edges,
	                                    accesses,
	                                    VU_RESOURCE_CLIP,
	                                    ignoredImplicitWawResources );
	addAccumulatingImplicitFlagDependencies( edges,
	                                         accesses,
	                                         block.tokens,
	                                         VU_RESOURCE_STATUS );

	return edges;
}

namespace
{
	std::vector<VuScheduledIssueSlot> scheduleVuBasicBlockReadyIssueSlotsWithLatency(
	    const VuBasicBlock& block,
	    unsigned int ignoredImplicitWawResources,
	    VuLatencyTracker& latencyTracker,
	    unsigned int blockStartCycle )
	{
		std::vector<VuScheduledIssueSlot> slots;
		std::vector<const Token*> segment;
		unsigned int currentCycle = blockStartCycle;

		for( std::vector<const Token*>::const_iterator i = block.tokens.begin(); i != block.tokens.end(); ++i )
		{
			if( isVuReadyScheduleCandidate( **i ) )
			{
				segment.push_back( *i );
				continue;
			}

			std::vector<VuScheduledIssueSlot> segmentSlots =
				scheduleReadySegmentIssueSlots( segment,
				                                ignoredImplicitWawResources,
				                                latencyTracker,
				                                blockStartCycle,
				                                currentCycle );
			slots.insert( slots.end(), segmentSlots.begin(), segmentSlots.end() );
			segment.clear();
			bool pairedWithPreviousSlot = false;
			if( !slots.empty() )
			{
				const unsigned int pairedCycle = blockStartCycle + slots.back().issueCycle;
				if( latencyTracker.readHazardDelay( **i, NULL, static_cast<int>( pairedCycle ) ) <= 0 )
					pairedWithPreviousSlot =
						tryPairTailWithPreviousSlot( slots,
						                             **i,
						                             ignoredImplicitWawResources );
			}
			if( !pairedWithPreviousSlot )
			{
				appendReadHazardPaddingSlots( slots,
				                              **i,
				                              NULL,
				                              latencyTracker,
				                              blockStartCycle,
				                              currentCycle );
				slots.push_back( makeIssueSlot( *i,
				                                NULL,
				                                currentCycle - blockStartCycle,
				                                1,
				                                ignoredImplicitWawResources ) );
				latencyTracker.recordWrites( **i, static_cast<int>( currentCycle ) );
				++currentCycle;
			}
			else
			{
				const unsigned int pairedCycle = blockStartCycle + slots.back().issueCycle;
				latencyTracker.recordWrites( **i, static_cast<int>( pairedCycle ) );
			}
		}

		std::vector<VuScheduledIssueSlot> segmentSlots =
			scheduleReadySegmentIssueSlots( segment,
			                                ignoredImplicitWawResources,
			                                latencyTracker,
			                                blockStartCycle,
			                                currentCycle );
		slots.insert( slots.end(), segmentSlots.begin(), segmentSlots.end() );

		assignScheduledIssueSlotTokenIndices( block, slots );
		return slots;
	}
}

std::vector<VuScheduledIssueSlot> scheduleVuBasicBlockReadyIssueSlots( const VuBasicBlock& block,
                                                                       unsigned int ignoredImplicitWawResources )
{
	VuLatencyTracker latencyTracker;
	return scheduleVuBasicBlockReadyIssueSlotsWithLatency( block,
	                                                       ignoredImplicitWawResources,
	                                                       latencyTracker,
	                                                       0 );
}

namespace
{
	bool vuBranchTargetLabelOf( const Token& branch, std::string& label )
	{
		for( std::list<Token::Argument>::const_iterator i = branch.arguments().begin();
		     i != branch.arguments().end(); ++i )
		{
			if( !(i->flags() & Token::Argument::BRANCH) )
				continue;
			if( i->type() != Token::Argument::IMMEDIATE )
				return false;
			label = i->immediate();
			return true;
		}
		return false;
	}

	// HOW MUCH EARLIER THAN THE FALL-THROUGH TIMELINE A BLOCK CAN BE ENTERED.
	//
	// Blocks are laid out and clocked in FILE ORDER, and the latency tracker is
	// carried from one to the next along that order. That is exactly right for a
	// block reached by falling through, and wrong for one reached by a branch:
	// the branch and its delay slot are the only two rows between the branch and
	// the target, while the linear clock counted every row in between. A `div`
	// above the branch and its `mulq` at the target look far apart in the file
	// and are six cycles apart on the wire.
	//
	//   skew(B) = (B's linear entry cycle) - (shortest path in cycles to B)
	//
	// and it is built edge by edge. Along a fall-through edge the skew is
	// inherited unchanged. Along a branch from a block whose own skew is s, with
	// the branch issuing at absolute cycle X and the target's linear entry at L,
	// the shortest path to L through that edge is (X - s) + 2, so the edge's
	// skew is s + L - X - 2. The block takes the LARGEST of its edges, because
	// the largest skew is the shortest real path.
	//
	// A BACKWARD edge can never win that maximum and is not recorded: it has
	// X > L, and the loop body between L and X is on every path through it, so
	// s + L - X - 2 is below the skew the loop head already carries. What a back
	// edge CAN do is make a producer BELOW a consumer feed it on the next
	// iteration, and that is a different question this does not answer - see
	// the note in the fork's CHANGELOG.
	class VuEntrySkew
	{
	public:
		VuEntrySkew() : m_applied( 0 ) {}

		// The cycles by which the outstanding Q/P results must be pushed back
		// before this block is scheduled. Monotone: the skew only ever grows, so
		// a block that inherits a larger skew than its own edges justify is
		// over-, never under-, estimated.
		int enter( const VuBasicBlock& block, unsigned int linearEntryCycle )
		{
			int skew = m_applied;
			if( !block.tokens.empty() && block.tokens[0]->label().length() != 0 )
			{
				std::map<std::string, int>::const_iterator i =
				    m_edgeBase.find( block.tokens[0]->label() );
				if( i != m_edgeBase.end() )
				{
					const int viaBranch = i->second + static_cast<int>( linearEntryCycle );
					if( viaBranch > skew )
						skew = viaBranch;
				}
			}
			const int delta = skew - m_applied;
			m_applied = skew;
			return delta > 0 ? delta : 0;
		}

		// L is not known when the branch is scheduled, so what is stored is the
		// part of the edge's skew that does not depend on it.
		void recordBranch( const Token& branch, int absoluteIssueCycle )
		{
			std::string label;
			if( !vuBranchTargetLabelOf( branch, label ) )
				return;
			const int base = m_applied - absoluteIssueCycle - 2;
			std::map<std::string, int>::iterator i = m_edgeBase.find( label );
			if( i == m_edgeBase.end() || base > i->second )
				m_edgeBase[label] = base;
		}

		void recordBlockBranches( const std::vector<VuScheduledIssueSlot>& slots,
		                          unsigned int blockStartCycle )
		{
			for( std::vector<VuScheduledIssueSlot>::const_iterator s = slots.begin();
			     s != slots.end(); ++s )
			{
				const Token* pair[2] = { s->firstToken, s->secondToken };
				for( int k = 0; k < 2; ++k )
				{
					if( !pair[k] )
						continue;
					if( !vuTokenHasInstructionFlag( *pair[k], VU_INSTR_BRANCH ) )
						continue;
					recordBranch( *pair[k],
					              static_cast<int>( blockStartCycle + s->issueCycle ) );
				}
			}
		}

	private:
		int m_applied;
		std::map<std::string, int> m_edgeBase;
	};

	VuScheduledProgram scheduleVuProgramReadyIssueSlotsInternal( const std::list<Token>& tokens,
	                                                             unsigned int ignoredImplicitWawResources,
	                                                             bool carryLatencyAcrossBlocks )
	{
		VuScheduledProgram program;
		const std::vector<VuBasicBlock> blocks = buildVuBasicBlocks( tokens );
		VuLatencyTracker programLatencyTracker;
		VuEntrySkew skew;

		for( std::vector<VuBasicBlock>::const_iterator block = blocks.begin(); block != blocks.end(); ++block )
		{
			VuScheduledBasicBlock scheduledBlock;
			scheduledBlock.block = *block;
			scheduledBlock.firstIssueCycle = program.cycleCount;
			const int pushBack = skew.enter( *block, program.cycleCount );
			if( carryLatencyAcrossBlocks )
			{
				programLatencyTracker.delayPipelinedResultsBy( pushBack );
				scheduledBlock.issueSlots =
					scheduleVuBasicBlockReadyIssueSlotsWithLatency( *block,
					                                                ignoredImplicitWawResources,
					                                                programLatencyTracker,
					                                                program.cycleCount );
			}
			else
			{
				VuLatencyTracker blockLatencyTracker;
				scheduledBlock.issueSlots =
					scheduleVuBasicBlockReadyIssueSlotsWithLatency( *block,
					                                                ignoredImplicitWawResources,
					                                                blockLatencyTracker,
					                                                program.cycleCount );
			}
			skew.recordBlockBranches( scheduledBlock.issueSlots, program.cycleCount );
			for( std::vector<VuScheduledIssueSlot>::const_iterator slot = scheduledBlock.issueSlots.begin();
			     slot != scheduledBlock.issueSlots.end(); ++slot )
				scheduledBlock.cycleCount += slot->cycleCount;
			program.cycleCount += scheduledBlock.cycleCount;
			program.blocks.push_back( scheduledBlock );
		}

		return program;
	}
}

VuScheduledProgram scheduleVuProgramReadyIssueSlots( const std::list<Token>& tokens,
                                                     unsigned int ignoredImplicitWawResources )
{
	return scheduleVuProgramReadyIssueSlotsInternal( tokens,
	                                                ignoredImplicitWawResources,
	                                                true );
}

std::vector< std::vector<VuScheduledIssueSlot> > scheduleVuBasicBlocksReadyIssueSlotsWithFlagLiveness(
    const std::list<Token>& tokens )
{
	std::vector< std::vector<VuScheduledIssueSlot> > result;
	const VuScheduledProgram program = scheduleVuProgramReadyIssueSlotsWithFlagLiveness( tokens );

	for( std::vector<VuScheduledBasicBlock>::const_iterator block = program.blocks.begin();
	     block != program.blocks.end(); ++block )
		result.push_back( block->issueSlots );

	return result;
}

namespace
{
	VuScheduledProgram scheduleVuProgramReadyIssueSlotsWithFlagLivenessInternal(
	    const std::list<Token>& tokens,
	    bool carryLatencyAcrossBlocks )
	{
		VuScheduledProgram program;
		const std::vector<VuBasicBlock> blocks = buildVuBasicBlocks( tokens );
		const VuFlagLiveness liveness = analyzeFlagLiveness( tokens );
		VuLatencyTracker programLatencyTracker;
		VuEntrySkew skew;

		for( std::vector<VuBasicBlock>::const_iterator block = blocks.begin(); block != blocks.end(); ++block )
		{
			VuScheduledBasicBlock scheduledBlock;
			scheduledBlock.block = *block;
			scheduledBlock.firstIssueCycle = program.cycleCount;
			const int pushBack = skew.enter( *block, program.cycleCount );
			if( carryLatencyAcrossBlocks )
			{
				programLatencyTracker.delayPipelinedResultsBy( pushBack );
				scheduledBlock.issueSlots =
					scheduleVuBasicBlockReadyIssueSlotsWithFlagLiveness( *block,
					                                                     liveness,
					                                                     programLatencyTracker,
					                                                     program.cycleCount );
			}
			else
			{
				VuLatencyTracker blockLatencyTracker;
				scheduledBlock.issueSlots =
					scheduleVuBasicBlockReadyIssueSlotsWithFlagLiveness( *block,
					                                                     liveness,
					                                                     blockLatencyTracker,
					                                                     program.cycleCount );
			}
			skew.recordBlockBranches( scheduledBlock.issueSlots, program.cycleCount );
			for( std::vector<VuScheduledIssueSlot>::const_iterator slot = scheduledBlock.issueSlots.begin();
			     slot != scheduledBlock.issueSlots.end(); ++slot )
				scheduledBlock.cycleCount += slot->cycleCount;
			program.cycleCount += scheduledBlock.cycleCount;
			program.blocks.push_back( scheduledBlock );
		}

		return program;
	}
}

VuScheduledProgram scheduleVuProgramReadyIssueSlotsWithFlagLiveness( const std::list<Token>& tokens )
{
	return scheduleVuProgramReadyIssueSlotsWithFlagLivenessInternal( tokens, true );
}

unsigned int vuScheduledProgramEmittedWordCount( const VuScheduledProgram& program )
{
	unsigned int words = 0;
	for( std::vector<VuScheduledBasicBlock>::const_iterator block = program.blocks.begin();
	     block != program.blocks.end(); ++block )
		words += issueSlotEmittedWordCount( block->issueSlots );
	return words;
}

std::list<Token> flattenVuScheduledProgramTokens( const VuScheduledProgram& program )
{
	std::list<Token> scheduled;

	for( std::vector<VuScheduledBasicBlock>::const_iterator block = program.blocks.begin();
	     block != program.blocks.end(); ++block )
		appendIssueSlotsFlat( block->issueSlots, scheduled );

	return scheduled;
}

unsigned int vuIgnoredFlagWawResourcesForRemaining( std::list<Token>::const_iterator begin,
                                                    std::list<Token>::const_iterator end )
{
	return vuIgnoredFlagWawResourcesForRemaining( begin, begin, end );
}

unsigned int vuIgnoredFlagWawResourcesForRemaining( std::list<Token>::const_iterator programBegin,
                                                    std::list<Token>::const_iterator begin,
                                                    std::list<Token>::const_iterator end )
{
	bool readsMac = false;
	bool readsClip = false;
	bool readsStatus = false;
	unsigned int declared = VU_RESOURCE_NONE;
	for( std::list<Token>::const_iterator i = begin; i != end; ++i )
	{
		readsMac = readsMac || tokenReadsMac( *i );
		readsClip = readsClip || tokenReadsClip( *i );
		readsStatus = readsStatus || tokenReadsStatus( *i );
		declared |= vuDeclaredHardwareResource( *i );
	}

	// The same back edge the scheduler's liveness has to cross. "Remaining" is
	// the rest of the LIST; a loop's remaining execution includes everything
	// from the back edge's target, which is behind `begin`. A reader there is
	// reached by the next iteration, so a writer here is not dead. Only the
	// labels the remaining range actually branches BACK to are re-scanned, so a
	// straight-line tail costs nothing and behaves exactly as before.
	std::set<std::string> backTargets;
	for( std::list<Token>::const_iterator i = begin; i != end; ++i )
	{
		std::string label;
		if( branchTargetLabel( *i, label ) )
			backTargets.insert( label );
	}
	if( !backTargets.empty() && begin != programBegin )
	{
		bool inLoop = false;
		for( std::list<Token>::const_iterator i = programBegin; i != begin; ++i )
		{
			if( (*i).label().length() != 0
			    && backTargets.find( (*i).label() ) != backTargets.end() )
				inLoop = true;
			if( !inLoop )
				continue;
			readsMac = readsMac || tokenReadsMac( *i );
			readsClip = readsClip || tokenReadsClip( *i );
			readsStatus = readsStatus || tokenReadsStatus( *i );
		}
	}

	unsigned int mask = VU_RESOURCE_NONE;
	if( !readsMac )
		mask |= VU_RESOURCE_MAC;
	if( !readsClip )
		mask |= VU_RESOURCE_CLIP;
	if( !readsStatus )
		mask |= VU_RESOURCE_STATUS;
	// An out_hw_*/in_hw_* declaration keeps its resource live past every reader, and
	// past the end of the program - that is what the declaration is for. The
	// dead-write pass has always honoured it and this did not, so the two disagreed
	// about whether CLIP was dead in exactly the programs that say it is not.
	return mask & ~declared;
}

std::vector<VuLoopCandidate> findVuLoopCandidates( const std::list<Token>& tokens )
{
	std::vector<const Token*> indexedTokens;
	std::map<std::string, unsigned int> labels;
	unsigned int index = 0;

	for( std::list<Token>::const_iterator i = tokens.begin(); i != tokens.end(); ++i, ++index )
	{
		indexedTokens.push_back( &*i );
		if( (*i).label().length() != 0 )
			labels[(*i).label()] = index;
	}

	std::vector<VuLoopCandidate> result;
	for( unsigned int branchIndex = 0; branchIndex < indexedTokens.size(); ++branchIndex )
	{
		const Token& token = *indexedTokens[branchIndex];
		std::string label;
		if( !branchTargetLabel( token, label ) )
			continue;

		std::map<std::string, unsigned int>::const_iterator target = labels.find( label );
		if( target == labels.end() || target->second >= branchIndex )
			continue;

		VuLoopCandidate candidate;
		candidate.label = label;
		candidate.labelTokenIndex = target->second;
		candidate.branchTokenIndex = branchIndex;
		candidate.firstBodyTokenIndex = target->second;
		candidate.lastBodyTokenIndex = branchIndex;
		candidate.hasLoopDirective = loopTargetHasDirective( indexedTokens, target->second );
		describeLoopCsDirectiveAtLabel( indexedTokens,
		                                target->second,
		                                candidate.loopCsClid,
		                                candidate.loopCsMlid );
		candidate.simpleCountedLoop = candidate.hasLoopDirective
		                           && !isVuTerminalUnconditionalBranch( token )
		                           && vuTokenBranchDelaySlots( token ) > 0;
		candidate.branchToken = &token;

		for( unsigned int body = candidate.firstBodyTokenIndex; body <= candidate.lastBodyTokenIndex; ++body )
			candidate.bodyTokens.push_back( indexedTokens[body] );
		analyzeLoopCandidateResources( candidate );

		result.push_back( candidate );
	}

	return result;
}

namespace
{
	// Track 9.G-1h step 4b-3b-1: expanded-node descriptor. Mirrors the
	// rename-emission walk in applyVuGenericKernelRewritePlans (per cell:
	// either pass-through, materialize-MOVEs + unsplit op, or N split
	// clones; then per-decision tail MOVEs). Each emitted sub-token
	// becomes one node here. The refit placer (4b-3b-3 onward) will
	// consume this node list. Diagnostic-only at 4b-3b-1.
	struct VuKernelExpandedNode
	{
		enum Role
		{
			ROLE_PASSTHROUGH,
			ROLE_SPLIT_CLONE,
			ROLE_MATERIALIZE_MOVE,
			ROLE_TAIL_MOVE
		};
		Role         role;
		unsigned int sourceCellIndex;   // index into plan.kernelRewriteMainTokens; -1u for TAIL_MOVE
		unsigned int sourceTokenIndex;  // index into indexedTokens;                -1u for TAIL_MOVE
		unsigned int decisionIndex;     // index into plan.kernelRewriteRenameDecisions; -1u for plain passthrough/split
		unsigned int cloneOrdinal;      // 0-based ordinal within a SPLIT_CLONE group
	};

	// Track 9.G-1h step 4b-8a: live-range narrowing for materialize
	// MOVEs. Returns true iff `token` has any FLOAT_REGISTER argument
	// whose base register key equals `base`. Used to skip emission of
	// a per-decision materialize MOVE before a soft-blocker token that
	// does not actually reference that decision's original base — i.e.
	// for stores like `sq A, ofs(vi)`, only one of the N rename
	// decisions touches `A` and the others should not get spurious
	// scratch->base MOVEs. The materialize-candidate gate already
	// excludes WRITEs of any decision base, so any match here is a
	// READ (the only case where a pre-token MOVE is semantically
	// required).
	bool tokenReadsDecisionBase( const Token& token, const std::string& decisionReg )
	{
		const std::string base = registerBaseKey( decisionReg );
		for( std::list<Token::Argument>::const_iterator a = token.arguments().begin();
		     a != token.arguments().end(); ++a )
		{
			if( a->type() != Token::Argument::FLOAT_REGISTER )
				continue;
			std::string key;
			if( !vuRegisterKey( *a, key ) )
				continue;
			if( registerBaseKey( key ) == base )
				return true;
		}
		return false;
	}

	// Track 9.G-1h step 4b-8b: tail-MOVE coalescing. Returns true iff any
	// drain token in the plan reads the decision's original base register.
	// Used to skip emission of the tail scratch->base MOVE for decisions
	// whose renamed value is dead at loop exit: in-loop reads of the base
	// are already handled by pre-soft-blocker materialize MOVEs (see
	// 4b-8a), so the tail MOVE is only required to keep the base live for
	// drain-stage consumers.
	bool drainReadsDecisionBase(
		const VuSoftwarePipelineRewritePlan& plan,
		const std::vector<const Token*>&     indexedTokens,
		const std::string&                   decisionReg )
	{
		for( std::vector<unsigned int>::const_iterator dt = plan.kernelRewriteDrainTokens.begin();
		     dt != plan.kernelRewriteDrainTokens.end(); ++dt )
		{
			if( *dt >= indexedTokens.size() )
				continue;
			if( tokenReadsDecisionBase( *indexedTokens[*dt], decisionReg ) )
				return true;
		}
		return false;
	}

	void buildVuKernelExpandedNodes(
		const VuSoftwarePipelineRewritePlan& plan,
		const std::vector<const Token*>& indexedTokens,
		std::vector<VuKernelExpandedNode>& out )
	{
		out.clear();
		const unsigned int NO_CELL = static_cast<unsigned int>( -1 );
		const unsigned int NO_DEC  = static_cast<unsigned int>( -1 );
		for( unsigned int m = 0; m < plan.kernelRewriteMainTokens.size(); ++m )
		{
			const unsigned int idx = plan.kernelRewriteMainTokens[m];
			if( idx == VuKernelRewritePlan::NO_TOKEN )
				continue;
			if( idx >= indexedTokens.size() )
				continue;
			const Token& src = *indexedTokens[idx];
			if( tokenIsKernelRenameMaterializeCandidate( src, plan ) )
			{
				for( unsigned int d = 0; d < plan.kernelRewriteRenameDecisions.size(); ++d )
				{
					if( !plan.kernelRewriteRenameDecisions[d].assigned )
						continue;
					// 4b-8a: only materialize decisions whose original
					// base is actually read by this soft-blocker token.
					if( !tokenReadsDecisionBase( src, plan.kernelRewriteRenameDecisions[d].reg ) )
						continue;
					VuKernelExpandedNode n;
					n.role             = VuKernelExpandedNode::ROLE_MATERIALIZE_MOVE;
					n.sourceCellIndex  = m;
					n.sourceTokenIndex = idx;
					n.decisionIndex    = d;
					n.cloneOrdinal     = 0;
					out.push_back( n );
				}
				VuKernelExpandedNode p;
				p.role             = VuKernelExpandedNode::ROLE_PASSTHROUGH;
				p.sourceCellIndex  = m;
				p.sourceTokenIndex = idx;
				p.decisionIndex    = NO_DEC;
				p.cloneOrdinal     = 0;
				out.push_back( p );
				continue;
			}
			std::list<Token> split;
			splitMultiFieldOpByFieldDecisions( src, plan.kernelRewriteRenameDecisions, split );
			if( split.size() > 1u )
			{
				unsigned int ord = 0;
				for( std::list<Token>::const_iterator s = split.begin(); s != split.end(); ++s, ++ord )
				{
					VuKernelExpandedNode n;
					n.role             = VuKernelExpandedNode::ROLE_SPLIT_CLONE;
					n.sourceCellIndex  = m;
					n.sourceTokenIndex = idx;
					n.decisionIndex    = NO_DEC;
					n.cloneOrdinal     = ord;
					out.push_back( n );
				}
			}
			else
			{
				VuKernelExpandedNode p;
				p.role             = VuKernelExpandedNode::ROLE_PASSTHROUGH;
				p.sourceCellIndex  = m;
				p.sourceTokenIndex = idx;
				p.decisionIndex    = NO_DEC;
				p.cloneOrdinal     = 0;
				out.push_back( p );
			}
		}
		for( unsigned int d = 0; d < plan.kernelRewriteRenameDecisions.size(); ++d )
		{
			if( !plan.kernelRewriteRenameDecisions[d].assigned )
				continue;
			// 4b-8b: only emit tail MOVE for decisions whose base is
			// read by a drain token. In-loop reads of the base are
			// covered by per-soft-blocker materialize MOVEs (4b-8a);
			// if drain doesn't read the base, the renamed value is
			// dead at loop exit.
			if( !drainReadsDecisionBase( plan, indexedTokens, plan.kernelRewriteRenameDecisions[d].reg ) )
				continue;
			VuKernelExpandedNode n;
			n.role             = VuKernelExpandedNode::ROLE_TAIL_MOVE;
			n.sourceCellIndex  = NO_CELL;
			n.sourceTokenIndex = static_cast<unsigned int>( -1 );
			n.decisionIndex    = d;
			n.cloneOrdinal     = 0;
			out.push_back( n );
		}
	}

	const char* vuKernelExpandedNodeRoleName( unsigned int role )
	{
		switch( role )
		{
		case VuKernelExpandedNode::ROLE_PASSTHROUGH:       return "pass";
		case VuKernelExpandedNode::ROLE_SPLIT_CLONE:       return "split";
		case VuKernelExpandedNode::ROLE_MATERIALIZE_MOVE:  return "matMOVE";
		case VuKernelExpandedNode::ROLE_TAIL_MOVE:         return "tailMOVE";
		default: return "?";
		}
	}

	// Track 9.G-1h step 4b-3b-2: extracted modulo-placer + kernel-rewrite
	// scaffolding body. Pulled verbatim out of findVuLoopPipelineOpportunities
	// (originally under "Track 9.G step 4d" / "Track 9.G step 8a") so the
	// same logic can be reused by the upcoming refit placer (4b-3b-3) over
	// expanded-DDG nodes. Body is byte-identical: only outer-scope
	// dependencies are passed in as parameters (opportunity, indexedTokens,
	// tokens).
	static void runVuKernelPlacerAndScaffolding(
	    VuLoopPipelineOpportunity& opportunity,
	    const std::vector<const Token*>& indexedTokens,
	    const std::list<Token>& tokens,
	    bool emitExpandedDDGDiagnostic = true )
	{
			// Track 9.G step 4d: iterative modulo placement (diagnostic).
			// Drive the priority order into the Modulo Reservation Table.
			// For each node, classify its pipe (Upper / Lower / FDIV / EFU)
			// from VuInstructionInfo and try slots in [asap, alap]; if no
			// slot fits, extend up to scheduleLength. Record (slot, stage)
			// per node. Planner/emission still untouched — this validates
			// that the MRT can absorb a full loop body at the current MII
			// using only intra-iteration constraints. Loop-carried hazards
			// are out of scope here (RecMII already bounds II).
			const std::vector<unsigned int>& mt = opportunity.mainTokenIndices;
			// Track 9.G step 1c-2: under OPENVCL_USE_GENERIC_KERNEL_REWRITE,
			// substitute the rename-aware RecMII (step 1c-1) for the
			// baseline value. This lowers the lower bound on II for loops
			// whose carried recurrences flow through VF bases the kernel
			// rename machinery rotates per iteration. The placer's own
			// dist=1 cross-iteration edges (built below) may still bump
			// II back up; that bump is observable via OPENVCL_DUMP_LOOP_SCHEDULE
			// and is the diagnostic for the follow-on placer-relaxation step.
			const bool useRenamedRecMII =
			    ( std::getenv( "OPENVCL_USE_GENERIC_KERNEL_REWRITE" ) != NULL );
			const unsigned int recmii = useRenamedRecMII
			    ? computeLoopRecMIIRenamed( mt, indexedTokens, NULL )
			    : computeLoopRecMII( mt, indexedTokens );
			const unsigned int resmii = computeLoopResMII( mt, indexedTokens );
			const unsigned int mii    = ( recmii > resmii ) ? recmii : resmii;
			LoopPriorityResult pr;
			computeLoopPriority( mt, indexedTokens, mii, pr );
			const unsigned int n = static_cast<unsigned int>( mt.size() );
			std::vector<unsigned int> slotOf( n, static_cast<unsigned int>( -1 ) );
			std::vector<unsigned int> stageOf( n, 0 );
			std::vector<int>          pipeOf( n, 0 ); // 0=none 1=U 2=L 3=FDIV 4=EFU
			std::vector<unsigned int> durationOf( n, 1 );
			std::vector<bool>         placed( n, false );
			unsigned int placedCount = 0, failedCount = 0, maxStage = 0;
			// Track 9.G step 4f: II-bumping retry. If placement leaves any node
			// failed at the start II, bump II by one and retry until either
			// every node is placed or we hit a safety cap.
			const unsigned int miiStart = mii;
			const unsigned int iiCap    = miiStart + 32;
			unsigned int tryII = miiStart;
			unsigned int bumps = 0;
			unsigned int evictions = 0, recoveries = 0;
			unsigned int upperOccFinal = 0, lowerOccFinal = 0;
			unsigned int fdivOccFinal  = 0, efuOccFinal   = 0;

			// Track 9.G step 4e: build the full DDG (intra dist=0, loop-carried
			// dist=1) so the placer can enforce cross-iteration RAW/WAW/WAR
			// feasibility. Constraint per edge (a->b, lat L, dist d):
			//     slot[b] - slot[a] >= L - d*II
			// When the predecessor is already placed and we trial slot s for b,
			// reject s if s < slot[a] + L - d*II. Symmetric check when b is
			// placed and a is being trialled.
			std::vector< std::vector<std::string> > nodeWritesP( n ), nodeReadsP( n );
			for( unsigned int k = 0; k < n; ++k )
			{
				if( mt[k] >= indexedTokens.size() ) continue;
				VuTokenResourceAccess acc;
				if( !buildVuTokenResourceAccess( *indexedTokens[mt[k]], acc ) ) continue;
				for( std::list<std::string>::const_iterator it = acc.registerWrites.begin();
				     it != acc.registerWrites.end(); ++it )
					nodeWritesP[k].push_back( registerBaseKey( *it ) );
				for( std::list<std::string>::const_iterator it = acc.registerReads.begin();
				     it != acc.registerReads.end(); ++it )
					nodeReadsP[k].push_back( registerBaseKey( *it ) );
				const unsigned int iw = acc.implicitWrites;
				const unsigned int ir = acc.implicitReads;
				if( iw & VU_RESOURCE_ACC )  nodeWritesP[k].push_back( "@ACC" );
				if( iw & VU_RESOURCE_Q )    nodeWritesP[k].push_back( "@Q" );
				if( iw & VU_RESOURCE_P )    nodeWritesP[k].push_back( "@P" );
				if( iw & VU_RESOURCE_R )    nodeWritesP[k].push_back( "@R" );
				if( iw & VU_RESOURCE_I )    nodeWritesP[k].push_back( "@I" );
				if( iw & VU_RESOURCE_MAC )  nodeWritesP[k].push_back( "@MAC" );
				if( iw & VU_RESOURCE_CLIP ) nodeWritesP[k].push_back( "@CLIP" );
				if( ir & VU_RESOURCE_ACC )  nodeReadsP[k].push_back( "@ACC" );
				if( ir & VU_RESOURCE_Q )    nodeReadsP[k].push_back( "@Q" );
				if( ir & VU_RESOURCE_P )    nodeReadsP[k].push_back( "@P" );
				if( ir & VU_RESOURCE_R )    nodeReadsP[k].push_back( "@R" );
				if( ir & VU_RESOURCE_I )    nodeReadsP[k].push_back( "@I" );
				if( ir & VU_RESOURCE_MAC )  nodeReadsP[k].push_back( "@MAC" );
				if( ir & VU_RESOURCE_CLIP ) nodeReadsP[k].push_back( "@CLIP" );
			}
			std::vector<unsigned int> dFrom, dTo, dDist, dLat;
			for( unsigned int i = 0; i < n; ++i )
			{
				for( unsigned int j = 0; j < n; ++j )
				{
					if( i == j ) continue;
					const unsigned int dist = ( i < j ) ? 0u : 1u;
					std::string sharedRaw;
					for( unsigned int a = 0; a < nodeWritesP[i].size() && sharedRaw.empty(); ++a )
						for( unsigned int b = 0; b < nodeReadsP[j].size() && sharedRaw.empty(); ++b )
							if( nodeWritesP[i][a] == nodeReadsP[j][b] )
								sharedRaw = nodeWritesP[i][a];
					if( !sharedRaw.empty()
					    && mt[i] < indexedTokens.size()
					    && mt[j] < indexedTokens.size() )
					{
						VuLatencyTracker tr;
						tr.reset();
						tr.recordWrites( *indexedTokens[mt[i]], 0 );
						const int d = tr.readHazardDelay( *indexedTokens[mt[j]], NULL, 0 );
						const unsigned int lat = static_cast<unsigned int>( d > 0 ? d : 1 );
						dFrom.push_back( i ); dTo.push_back( j );
						dDist.push_back( dist ); dLat.push_back( lat );
					}
					std::string sharedWaw;
					for( unsigned int a = 0; a < nodeWritesP[i].size() && sharedWaw.empty(); ++a )
						for( unsigned int b = 0; b < nodeWritesP[j].size() && sharedWaw.empty(); ++b )
							if( nodeWritesP[i][a] == nodeWritesP[j][b] )
								sharedWaw = nodeWritesP[i][a];
					if( !sharedWaw.empty() )
					{
						dFrom.push_back( i ); dTo.push_back( j );
						dDist.push_back( dist ); dLat.push_back( 1 );
					}
					std::string sharedWar;
					for( unsigned int a = 0; a < nodeReadsP[i].size() && sharedWar.empty(); ++a )
						for( unsigned int b = 0; b < nodeWritesP[j].size() && sharedWar.empty(); ++b )
							if( nodeReadsP[i][a] == nodeWritesP[j][b] )
								sharedWar = nodeReadsP[i][a];
					if( !sharedWar.empty() )
					{
						dFrom.push_back( i ); dTo.push_back( j );
						dDist.push_back( dist ); dLat.push_back( 1 );
					}
				}
			}
			const unsigned int totalEdges = static_cast<unsigned int>( dFrom.size() );
			unsigned int edgeViolations = 0;
			// Track 9.G step 1d-1: per-node placement failure reason
			// counters (edge-bound vs MRT-bound) used by the
			// OPENVCL_DUMP_LOOP_BUMP_REASONS diagnostic. Reset each
			// placer attempt; only consulted when failedCount > 0
			// before the II bump.
			std::vector< unsigned int > edgeFailsN( n, 0u );
			std::vector< unsigned int > mrtFailsN ( n, 0u );
			while( true )
			{
				ModuloReservationTable mrt( tryII );
				for( unsigned int k = 0; k < n; ++k )
				{
					slotOf[k]  = static_cast<unsigned int>( -1 );
					stageOf[k] = 0;
					pipeOf[k]  = 0;
					durationOf[k] = 1;
					placed[k]  = false;
					edgeFailsN[k] = 0u;
					mrtFailsN[k]  = 0u;
				}
				placedCount = 0; failedCount = 0; maxStage = 0;
				edgeViolations = 0;
				evictions = 0; recoveries = 0;
			for( unsigned int rank = 0; rank < pr.order.size(); ++rank )
			{
				const unsigned int i = pr.order[rank];
				const unsigned int tokIdx = mt[i];
				if( tokIdx >= indexedTokens.size() ) { ++failedCount; continue; }
				const Token& tk = *indexedTokens[tokIdx];
				if( !tk.operand() ) { ++failedCount; continue; }
				const VuInstructionInfo* info =
				    findVuInstructionInfo( normalizeVuMnemonic( tk.operand()->name() ) );
				int pipeKind = 0;
				unsigned int duration = 1;
				if( info )
				{
					if( info->unit == VU_EXEC_FDIV )
					{
						pipeKind = 3;
						duration = info->throughput > 0 ? info->throughput : 1;
					}
					else if( info->unit == VU_EXEC_EFU )
					{
						pipeKind = 4;
						duration = info->throughput > 0 ? info->throughput : 1;
					}
					else if( info->pipe == VU_PIPE_UPPER )
					{
						pipeKind = 1;
					}
					else if( info->pipe == VU_PIPE_LOWER )
					{
						pipeKind = 2;
					}
				}
				pipeOf[i] = pipeKind;
				durationOf[i] = duration;
				if( pipeKind == 0 )
				{
					// No resource modeled (NOP / unknown). Treat as placed at ASAP.
					slotOf[i]  = pr.asap[i];
					stageOf[i] = tryII > 0 ? ( slotOf[i] / tryII ) : 0;
					placed[i]  = true;
					++placedCount;
					if( stageOf[i] > maxStage ) maxStage = stageOf[i];
					continue;
				}
				const unsigned int lo = pr.asap[i];
				unsigned int hi = pr.alap[i];
				if( hi < lo ) hi = lo;
				if( hi < pr.scheduleLength ) hi = pr.scheduleLength;
				if( hi < lo + tryII ) hi = lo + tryII; // ensure full mod ring is exercised
				bool ok = false;
				for( unsigned int s = lo; s <= hi && !ok; ++s )
				{
					// Track 9.G step 4e: edge feasibility against placed nodes.
					// For each edge touching i where the other end is already
					// placed, require slot[to] - slot[from] >= lat - dist*II.
					// Use signed int math (II*dist can exceed lat).
					bool edgeOk = true;
					for( unsigned int e = 0; e < totalEdges && edgeOk; ++e )
					{
						const int Lat = static_cast<int>( dLat[e] );
						const int Dii = static_cast<int>( dDist[e] ) * static_cast<int>( tryII );
						if( dTo[e] == i && placed[ dFrom[e] ] )
						{
							const int from = static_cast<int>( slotOf[ dFrom[e] ] );
							if( static_cast<int>( s ) - from < Lat - Dii )
								edgeOk = false;
						}
						else if( dFrom[e] == i && placed[ dTo[e] ] )
						{
							const int to = static_cast<int>( slotOf[ dTo[e] ] );
							if( to - static_cast<int>( s ) < Lat - Dii )
								edgeOk = false;
						}
					}
					if( !edgeOk ) { ++edgeViolations; ++edgeFailsN[i]; continue; }
					const unsigned int mod = tryII > 0 ? ( s % tryII ) : 0;
					bool canPlace = false;
					switch( pipeKind )
					{
					case 1: canPlace = mrt.canReserveUpper( mod ); break;
					case 2: canPlace = mrt.canReserveLower( mod ); break;
					case 3: canPlace = mrt.canReserveFdiv( mod, duration ); break;
					case 4: canPlace = mrt.canReserveEfu( mod, duration ); break;
					default: break;
					}
					if( !canPlace ) { ++mrtFailsN[i]; continue; }
					switch( pipeKind )
					{
					case 1: mrt.reserveUpper( mod, tokIdx ); break;
					case 2: mrt.reserveLower( mod, tokIdx ); break;
					case 3: mrt.reserveFdiv( mod, duration, tokIdx ); break;
					case 4: mrt.reserveEfu( mod, duration, tokIdx ); break;
					default: break;
					}
					slotOf[i]  = s;
					stageOf[i] = tryII > 0 ? ( s / tryII ) : 0;
					placed[i]  = true;
					if( stageOf[i] > maxStage ) maxStage = stageOf[i];
					ok = true;
				}
				if( ok ) ++placedCount;
				else     ++failedCount;
			}
				upperOccFinal = mrt.upperOccupancy();
				lowerOccFinal = mrt.lowerOccupancy();
				fdivOccFinal  = mrt.fdivOccupancy();
				efuOccFinal   = mrt.efuOccupancy();
				if( failedCount == 0 ) break;
				// Track 9.G step 4g: bounded backtracking. For each failed
				// node f, try evicting a single already-placed node b that
				// occupies a same-pipe MRT slot in f's [asap..hi] range. If
				// f then fits, attempt to re-place b anywhere in b's range.
				// Accept the swap when both succeed; otherwise revert.
				const unsigned int evictionBudget = n;
				unsigned int spent = 0;
				for( unsigned int rank = 0; rank < pr.order.size() && spent < evictionBudget; ++rank )
				{
					const unsigned int f = pr.order[rank];
					if( placed[f] ) continue;
					if( pipeOf[f] == 0 ) continue;
					const unsigned int loF = pr.asap[f];
					unsigned int hiF = pr.alap[f];
					if( hiF < loF ) hiF = loF;
					if( hiF < pr.scheduleLength ) hiF = pr.scheduleLength;
					if( hiF < loF + tryII ) hiF = loF + tryII;
					bool swapped = false;
					for( unsigned int rb = 0; rb < pr.order.size() && !swapped; ++rb )
					{
						const unsigned int b = pr.order[rb];
						if( b == f ) continue;
						if( !placed[b] ) continue;
						if( pipeOf[b] != pipeOf[f] ) continue;
						const unsigned int slotB = slotOf[b];
						const unsigned int durB  = durationOf[b];
						const int          pipeB = pipeOf[b];
						++spent; ++evictions;
						// Evict b
						switch( pipeB )
						{
						case 1: mrt.releaseUpper( slotB % tryII ); break;
						case 2: mrt.releaseLower( slotB % tryII ); break;
						case 3: mrt.releaseFdiv ( slotB % tryII, durB ); break;
						case 4: mrt.releaseEfu  ( slotB % tryII, durB ); break;
						default: break;
						}
						placed[b] = false;
						const unsigned int savedSlotB = slotB;
						// Try to place f at any slot in [loF,hiF]
						bool fPlaced = false;
						for( unsigned int s = loF; s <= hiF && !fPlaced; ++s )
						{
							bool edgeOk = true;
							for( unsigned int e = 0; e < totalEdges && edgeOk; ++e )
							{
								const int Lat = static_cast<int>( dLat[e] );
								const int Dii = static_cast<int>( dDist[e] ) * static_cast<int>( tryII );
								if( dTo[e] == f && placed[ dFrom[e] ] )
								{
									const int from = static_cast<int>( slotOf[ dFrom[e] ] );
									if( static_cast<int>( s ) - from < Lat - Dii ) edgeOk = false;
								}
								else if( dFrom[e] == f && placed[ dTo[e] ] )
								{
									const int to = static_cast<int>( slotOf[ dTo[e] ] );
									if( to - static_cast<int>( s ) < Lat - Dii ) edgeOk = false;
								}
							}
							if( !edgeOk ) continue;
							const unsigned int mod = s % tryII;
							bool canP = false;
							switch( pipeOf[f] )
							{
							case 1: canP = mrt.canReserveUpper( mod ); break;
							case 2: canP = mrt.canReserveLower( mod ); break;
							case 3: canP = mrt.canReserveFdiv( mod, durationOf[f] ); break;
							case 4: canP = mrt.canReserveEfu( mod, durationOf[f] ); break;
							default: break;
							}
							if( !canP ) continue;
							switch( pipeOf[f] )
							{
							case 1: mrt.reserveUpper( mod, mt[f] ); break;
							case 2: mrt.reserveLower( mod, mt[f] ); break;
							case 3: mrt.reserveFdiv( mod, durationOf[f], mt[f] ); break;
							case 4: mrt.reserveEfu( mod, durationOf[f], mt[f] ); break;
							default: break;
							}
							slotOf[f] = s;
							stageOf[f] = tryII > 0 ? ( s / tryII ) : 0;
							placed[f] = true;
							fPlaced = true;
						}
						if( !fPlaced )
						{
							// Restore b at its original slot.
							switch( pipeB )
							{
							case 1: mrt.reserveUpper( savedSlotB % tryII, mt[b] ); break;
							case 2: mrt.reserveLower( savedSlotB % tryII, mt[b] ); break;
							case 3: mrt.reserveFdiv( savedSlotB % tryII, durB, mt[b] ); break;
							case 4: mrt.reserveEfu ( savedSlotB % tryII, durB, mt[b] ); break;
							default: break;
							}
							placed[b] = true;
							continue;
						}
						// Try to re-place b somewhere in its range.
						const unsigned int loB = pr.asap[b];
						unsigned int hiB = pr.alap[b];
						if( hiB < loB ) hiB = loB;
						if( hiB < pr.scheduleLength ) hiB = pr.scheduleLength;
						if( hiB < loB + tryII ) hiB = loB + tryII;
						bool bPlaced = false;
						for( unsigned int s = loB; s <= hiB && !bPlaced; ++s )
						{
							bool edgeOk = true;
							for( unsigned int e = 0; e < totalEdges && edgeOk; ++e )
							{
								const int Lat = static_cast<int>( dLat[e] );
								const int Dii = static_cast<int>( dDist[e] ) * static_cast<int>( tryII );
								if( dTo[e] == b && placed[ dFrom[e] ] )
								{
									const int from = static_cast<int>( slotOf[ dFrom[e] ] );
									if( static_cast<int>( s ) - from < Lat - Dii ) edgeOk = false;
								}
								else if( dFrom[e] == b && placed[ dTo[e] ] )
								{
									const int to = static_cast<int>( slotOf[ dTo[e] ] );
									if( to - static_cast<int>( s ) < Lat - Dii ) edgeOk = false;
								}
							}
							if( !edgeOk ) continue;
							const unsigned int mod = s % tryII;
							bool canP = false;
							switch( pipeB )
							{
							case 1: canP = mrt.canReserveUpper( mod ); break;
							case 2: canP = mrt.canReserveLower( mod ); break;
							case 3: canP = mrt.canReserveFdiv( mod, durB ); break;
							case 4: canP = mrt.canReserveEfu( mod, durB ); break;
							default: break;
							}
							if( !canP ) continue;
							switch( pipeB )
							{
							case 1: mrt.reserveUpper( mod, mt[b] ); break;
							case 2: mrt.reserveLower( mod, mt[b] ); break;
							case 3: mrt.reserveFdiv( mod, durB, mt[b] ); break;
							case 4: mrt.reserveEfu( mod, durB, mt[b] ); break;
							default: break;
							}
							slotOf[b] = s;
							stageOf[b] = tryII > 0 ? ( s / tryII ) : 0;
							placed[b] = true;
							bPlaced = true;
						}
						if( bPlaced )
						{
							// Swap succeeded.
							++placedCount;
							--failedCount;
							++recoveries;
							if( stageOf[f] > maxStage ) maxStage = stageOf[f];
							if( stageOf[b] > maxStage ) maxStage = stageOf[b];
							swapped = true;
						}
						else
						{
							// Revert: evict f, restore b.
							switch( pipeOf[f] )
							{
							case 1: mrt.releaseUpper( slotOf[f] % tryII ); break;
							case 2: mrt.releaseLower( slotOf[f] % tryII ); break;
							case 3: mrt.releaseFdiv( slotOf[f] % tryII, durationOf[f] ); break;
							case 4: mrt.releaseEfu ( slotOf[f] % tryII, durationOf[f] ); break;
							default: break;
							}
							placed[f] = false;
							slotOf[f] = static_cast<unsigned int>( -1 );
							stageOf[f] = 0;
							switch( pipeB )
							{
							case 1: mrt.reserveUpper( savedSlotB % tryII, mt[b] ); break;
							case 2: mrt.reserveLower( savedSlotB % tryII, mt[b] ); break;
							case 3: mrt.reserveFdiv( savedSlotB % tryII, durB, mt[b] ); break;
							case 4: mrt.reserveEfu ( savedSlotB % tryII, durB, mt[b] ); break;
							default: break;
							}
							placed[b] = true;
						}
					}
				}
				upperOccFinal = mrt.upperOccupancy();
				lowerOccFinal = mrt.lowerOccupancy();
				fdivOccFinal  = mrt.fdivOccupancy();
				efuOccFinal   = mrt.efuOccupancy();
				if( failedCount == 0 ) break;
				if( tryII >= iiCap ) break;
				// Track 9.G step 1d-1: per-bump diagnostic. Emit one
				// line per attempt that is about to bump, summarizing
				// which failed nodes were edge-bound vs MRT-bound. Used
				// to determine whether a low MII can ever be realized:
				// edge-bound failures point at unbreakable recurrences;
				// MRT-bound failures point at pipe-resource conflicts.
				if( std::getenv( "OPENVCL_DUMP_LOOP_BUMP_REASONS" ) != NULL )
				{
					unsigned int edgeBound = 0, mrtBound = 0, mixedBound = 0;
					for( unsigned int k = 0; k < n; ++k )
					{
						if( placed[k] ) continue;
						if( pipeOf[k] == 0 ) continue;
						if( edgeFailsN[k] > 0u && mrtFailsN[k] == 0u ) ++edgeBound;
						else if( edgeFailsN[k] == 0u && mrtFailsN[k] > 0u ) ++mrtBound;
						else if( edgeFailsN[k] > 0u && mrtFailsN[k] > 0u ) ++mixedBound;
					}
					std::cerr << "[loop-bump] loop=" << opportunity.label
					          << " bumpFrom=" << tryII
					          << " failed=" << failedCount
					          << " edgeBound=" << edgeBound
					          << " mrtBound=" << mrtBound
					          << " mixedBound=" << mixedBound
					          << " edgeViolations=" << edgeViolations
					          << " evictions=" << evictions
					          << "\n";
					for( unsigned int k = 0; k < n; ++k )
					{
						if( placed[k] ) continue;
						if( pipeOf[k] == 0 ) continue;
						const unsigned int tokIdx = mt[k];
						const char* opName = "?";
						if( tokIdx < indexedTokens.size()
						 && indexedTokens[tokIdx]->operand() )
							opName = indexedTokens[tokIdx]->operand()->name().c_str();
						const char* pname = "none";
						switch( pipeOf[k] )
						{
						case 1: pname = "upper"; break;
						case 2: pname = "lower"; break;
						case 3: pname = "fdiv";  break;
						case 4: pname = "efu";   break;
						default: break;
						}
						std::cerr << "[loop-bump-node] loop=" << opportunity.label
						          << " bumpFrom=" << tryII
						          << " node=" << k
						          << " op=" << opName
						          << " pipe=" << pname
						          << " edgeFails=" << edgeFailsN[k]
						          << " mrtFails=" << mrtFailsN[k]
						          << "\n";
						// Track 9.G step 1d-2: also dump every DDG edge
						// incident on this failed node so the carrier
						// recurrence is visible. Helps decide which
						// rotatable-predicate extension (integer ptr,
						// store-port, etc.) is needed to break it.
						for( unsigned int e = 0; e < totalEdges; ++e )
						{
							if( dFrom[e] != k && dTo[e] != k ) continue;
							const unsigned int other = ( dFrom[e] == k ) ? dTo[e] : dFrom[e];
							const char* otherOp = "?";
							if( other < n )
							{
								const unsigned int oTok = mt[other];
								if( oTok < indexedTokens.size()
								 && indexedTokens[oTok]->operand() )
									otherOp = indexedTokens[oTok]->operand()->name().c_str();
							}
							std::cerr << "[loop-bump-edge] loop=" << opportunity.label
							          << " bumpFrom=" << tryII
							          << " node=" << k
							          << " dir=" << ( dFrom[e] == k ? "out" : "in" )
							          << " other=" << other
							          << " otherOp=" << otherOp
							          << " lat=" << dLat[e]
							          << " dist=" << dDist[e]
							          << "\n";
						}
					}
				}
				++tryII; ++bumps;
			}
			// Track 9.G step 5a: structured kernel layout handoff. Populate a
			// VuKernelLayout from the final placer state. The struct mirrors
			// what a stage-aware emitter will need to consume (stage, modSlot,
			// pipe, duration). Dumped under OPENVCL_DUMP_KERNEL_LAYOUT and
			// (per-entry) OPENVCL_DUMP_KERNEL_LAYOUT_ENTRIES.
			if( std::getenv( "OPENVCL_DUMP_KERNEL_LAYOUT" ) != NULL )
			{
				VuKernelLayout layout;
				layout.II         = tryII;
				layout.miiStart   = miiStart;
				layout.bumps      = bumps;
				layout.feasible   = ( failedCount == 0 );
				layout.stageCount = layout.feasible ? ( maxStage + 1 ) : 0;
				for( unsigned int k = 0; k < n; ++k )
				{
					if( !placed[k] ) continue;
					VuKernelLayoutEntry ent;
					ent.nodeIndex  = k;
					ent.tokenIndex = mt[k];
					ent.slot       = slotOf[k];
					ent.stage      = stageOf[k];
					ent.modSlot    = tryII > 0 ? ( slotOf[k] % tryII ) : 0;
					ent.pipe       = pipeOf[k];
					ent.duration   = durationOf[k];
					layout.entries.push_back( ent );
				}
				// std::sort is allowed (C++98); the comparator is a free
				// function in the VuKernelLayout TU.
				std::sort( layout.entries.begin(), layout.entries.end(),
				           vuKernelLayoutEntryLess );
				std::cerr << "[kernel-layout] loop=" << opportunity.label
				          << " II=" << layout.II
				          << " miiStart=" << layout.miiStart
				          << " bumps=" << layout.bumps
				          << " stageCount=" << layout.stageCount
				          << " feasible=" << ( layout.feasible ? 1 : 0 )
				          << " entries=" << layout.entries.size()
				          << "\n";
				if( std::getenv( "OPENVCL_DUMP_KERNEL_LAYOUT_ENTRIES" ) != NULL )
				{
					for( unsigned int e = 0; e < layout.entries.size(); ++e )
					{
						const VuKernelLayoutEntry& ent = layout.entries[e];
						const char* pname = "none";
						switch( ent.pipe )
						{
						case 1: pname = "upper"; break;
						case 2: pname = "lower"; break;
						case 3: pname = "fdiv";  break;
						case 4: pname = "efu";   break;
						default: break;
						}
						std::cerr << "[kernel-entry] loop=" << opportunity.label
						          << " idx=" << e
						          << " node=" << ent.nodeIndex
						          << " token=" << ent.tokenIndex
						          << " stage=" << ent.stage
						          << " modSlot=" << ent.modSlot
						          << " slot=" << ent.slot
						          << " pipe=" << pname
						          << " duration=" << ent.duration
						          << "\n";
					}
				}
				// Track 9.G step 5b: derived per-modSlot VLIW template.
				// Diagnostic-only: validates that the layout forms a
				// conflict-free kernel body before any emitter consumes it.
				if( std::getenv( "OPENVCL_DUMP_KERNEL_TEMPLATE" ) != NULL )
				{
					VuKernelTemplate tmpl;
					buildVuKernelTemplate( layout, tmpl );
					unsigned int filledUpper = 0, filledLower = 0;
					unsigned int filledFdiv  = 0, filledEfu   = 0;
					for( unsigned int s = 0; s < tmpl.slots.size(); ++s )
					{
						if( tmpl.slots[s].upper != VuKernelTemplateSlot::NO_ENTRY ) ++filledUpper;
						if( tmpl.slots[s].lower != VuKernelTemplateSlot::NO_ENTRY ) ++filledLower;
						if( tmpl.slots[s].fdiv  != VuKernelTemplateSlot::NO_ENTRY ) ++filledFdiv;
						if( tmpl.slots[s].efu   != VuKernelTemplateSlot::NO_ENTRY ) ++filledEfu;
					}
					std::cerr << "[kernel-template] loop=" << opportunity.label
					          << " II=" << tmpl.II
					          << " conflicts=" << tmpl.conflicts
					          << " upper=" << filledUpper
					          << " lower=" << filledLower
					          << " fdiv=" << filledFdiv
					          << " efu=" << filledEfu
					          << "\n";
					if( std::getenv( "OPENVCL_DUMP_KERNEL_TEMPLATE_SLOTS" ) != NULL )
					{
						for( unsigned int s = 0; s < tmpl.slots.size(); ++s )
						{
							const VuKernelTemplateSlot& ks = tmpl.slots[s];
							std::cerr << "[kernel-template-slot] loop=" << opportunity.label
							          << " modSlot=" << s
							          << " upper=" << ks.upper
							          << " lower=" << ks.lower
							          << " fdiv="  << ks.fdiv
							          << " efu="   << ks.efu
							          << "\n";
						}
					}
				}
				// Track 9.G step 5c: pipeline envelope (prologue + epilogue).
				// Diagnostic-only: report how many tokens each prologue /
				// epilogue copy would issue if the modulo schedule were
				// emitted.
				if( std::getenv( "OPENVCL_DUMP_KERNEL_ENVELOPE" ) != NULL )
				{
					VuKernelEnvelope env;
					buildVuKernelEnvelope( layout, env );
					std::cerr << "[kernel-envelope] loop=" << opportunity.label
					          << " II=" << env.II
					          << " stageCount=" << env.stageCount
					          << " kernelTokens=" << env.kernelTokens
					          << " prologueCycles=" << env.prologueCycles
					          << " epilogueCycles=" << env.epilogueCycles
					          << " prologueCopies=" << env.prologueTokenCounts.size()
					          << " epilogueCopies=" << env.epilogueTokenCounts.size()
					          << " conflicts=" << env.conflicts
					          << "\n";
					if( std::getenv( "OPENVCL_DUMP_KERNEL_ENVELOPE_COPIES" ) != NULL )
					{
						for( unsigned int p = 0; p < env.prologueTokenCounts.size(); ++p )
							std::cerr << "[kernel-envelope-prologue] loop=" << opportunity.label
							          << " copy=" << p
							          << " tokens=" << env.prologueTokenCounts[p]
							          << "\n";
						for( unsigned int q = 0; q < env.epilogueTokenCounts.size(); ++q )
							std::cerr << "[kernel-envelope-epilogue] loop=" << opportunity.label
							          << " copy=" << ( q + 1 )
							          << " tokens=" << env.epilogueTokenCounts[q]
							          << "\n";
					}
					if( std::getenv( "OPENVCL_DUMP_KERNEL_ENVELOPE_ROWS" ) != NULL )
					{
						for( unsigned int p = 0; p < env.prologueTokenCounts.size(); ++p )
						{
							for( unsigned int c = 0; c < env.II; ++c )
							{
								const VuKernelTemplateSlot& r = env.prologueRows[p * env.II + c];
								std::cerr << "[kernel-envelope-prologue-row] loop=" << opportunity.label
								          << " copy=" << p
								          << " modSlot=" << c
								          << " upper=" << r.upper
								          << " lower=" << r.lower
								          << " fdiv="  << r.fdiv
								          << " efu="   << r.efu
								          << "\n";
							}
						}
						for( unsigned int q = 0; q < env.epilogueTokenCounts.size(); ++q )
						{
							for( unsigned int c = 0; c < env.II; ++c )
							{
								const VuKernelTemplateSlot& r = env.epilogueRows[q * env.II + c];
								std::cerr << "[kernel-envelope-epilogue-row] loop=" << opportunity.label
								          << " copy=" << ( q + 1 )
								          << " modSlot=" << c
								          << " upper=" << r.upper
								          << " lower=" << r.lower
								          << " fdiv="  << r.fdiv
								          << " efu="   << r.efu
								          << "\n";
							}
						}
					}
				}
				// Track 9.G step 5e: register-reuse / write-generation.
				// Detects cross-stage hazards on VF registers shared by
				// concurrently-active iterations. Diagnostic-only.
				if( std::getenv( "OPENVCL_DUMP_KERNEL_REGISTER_PLAN" ) != NULL )
				{
					std::vector< VuKernelEntryRegisters > entryRegs;
					entryRegs.resize( layout.entries.size() );
					for( unsigned int e = 0; e < layout.entries.size(); ++e )
					{
						const unsigned int ti = layout.entries[e].tokenIndex;
						if( ti >= indexedTokens.size() ) continue;
						VuTokenResourceAccess acc;
						if( !buildVuTokenResourceAccess( *indexedTokens[ti], acc ) ) continue;
						for( std::list<std::string>::const_iterator it = acc.registerReads.begin();
						     it != acc.registerReads.end(); ++it )
						{
							if( it->size() >= 2 && (*it)[0] == 'V' && (*it)[1] == 'F' )
								entryRegs[e].reads.push_back( *it );
						}
						for( std::list<std::string>::const_iterator it = acc.registerWrites.begin();
						     it != acc.registerWrites.end(); ++it )
						{
							if( it->size() >= 2 && (*it)[0] == 'V' && (*it)[1] == 'F' )
								entryRegs[e].writes.push_back( *it );
						}
					}
					VuKernelRegisterPlan plan;
					buildVuKernelRegisterPlan( layout, entryRegs, plan );
					std::cerr << "[kernel-regplan] loop=" << opportunity.label
					          << " regs=" << plan.regCount
					          << " hazards=" << plan.hazards.size()
					          << " waw=" << plan.wawCount
					          << " raw=" << plan.rawCount
					          << " war=" << plan.warCount
					          << "\n";
					// Track 9.G step 6c: rename hints derived from hazards.
					if( std::getenv( "OPENVCL_DUMP_KERNEL_RENAME_HINTS" ) != NULL )
					{
						std::vector< VuKernelRenameHint > hints;
						buildVuKernelRenameHints( plan, hints );
						std::cerr << "[kernel-rename-hints] loop=" << opportunity.label
						          << " count=" << hints.size()
						          << "\n";
						if( std::getenv( "OPENVCL_DUMP_KERNEL_RENAME_HINTS_DETAIL" ) != NULL )
						{
							for( unsigned int h = 0; h < hints.size(); ++h )
							{
								const VuKernelRenameHint& ht = hints[ h ];
								std::cerr << "[kernel-rename-hint] loop=" << opportunity.label
								          << " reg=" << ht.reg
								          << " entry=" << ht.entry
								          << " stage=" << ht.stage
								          << " kind=" << ( ht.kind == 1 ? "W" : "R" )
								          << "\n";
							}
						}
					}
					if( std::getenv( "OPENVCL_DUMP_KERNEL_REGISTER_PLAN_HAZARDS" ) != NULL )
					{
						for( unsigned int h = 0; h < plan.hazards.size(); ++h )
						{
							const VuKernelRegisterHazard& hz = plan.hazards[h];
							const char* ka = ( hz.kindA == 1 ) ? "W" : "R";
							const char* kb = ( hz.kindB == 1 ) ? "W" : "R";
							std::cerr << "[kernel-regplan-hazard] loop=" << opportunity.label
							          << " reg=" << hz.reg
							          << " entryA=" << hz.entryA
							          << " stageA=" << hz.stageA
							          << " kindA=" << ka
							          << " entryB=" << hz.entryB
							          << " stageB=" << hz.stageB
							          << " kindB=" << kb
							          << "\n";
						}
					}
					// Track 9.G step 5f: cross-validate regplan RAW hazards
					// against the loop-carried RAW edges the placer used.
					if( std::getenv( "OPENVCL_DUMP_KERNEL_REGISTER_CROSSCHECK" ) != NULL )
					{
						std::set<std::string> expectedRaw;
						for( unsigned int i = 0; i < n; ++i )
						{
							for( unsigned int j = 0; j < n; ++j )
							{
								if( i == j ) continue;
								if( i < j ) continue; // loop-carried only (dist=1)
								for( unsigned int a = 0; a < nodeWritesP[i].size(); ++a )
								{
									const std::string& w = nodeWritesP[i][a];
									if( w.size() < 2 || w[0] != 'V' || w[1] != 'F' ) continue;
									for( unsigned int b = 0; b < nodeReadsP[j].size(); ++b )
									{
										if( nodeReadsP[j][b] == w )
										{
											expectedRaw.insert( w );
											break;
										}
									}
								}
							}
						}
						std::set<std::string> actualRaw;
						for( unsigned int h = 0; h < plan.hazards.size(); ++h )
						{
							const VuKernelRegisterHazard& hz = plan.hazards[h];
							const bool isRaw = ( hz.kindA == 1 && hz.kindB == 0 )
							                || ( hz.kindA == 0 && hz.kindB == 1 );
							if( isRaw ) actualRaw.insert( registerBaseKey( hz.reg ) );
						}
						unsigned int matched = 0, onlyExpected = 0, onlyActual = 0;
						for( std::set<std::string>::const_iterator it = expectedRaw.begin();
						     it != expectedRaw.end(); ++it )
						{
							if( actualRaw.count( *it ) ) ++matched;
							else ++onlyExpected;
						}
						for( std::set<std::string>::const_iterator it = actualRaw.begin();
						     it != actualRaw.end(); ++it )
						{
							if( !expectedRaw.count( *it ) ) ++onlyActual;
						}
						std::cerr << "[kernel-regplan-crosscheck] loop=" << opportunity.label
						          << " expectedRaw=" << expectedRaw.size()
						          << " actualRaw=" << actualRaw.size()
						          << " matched=" << matched
						          << " onlyExpected=" << onlyExpected
						          << " onlyActual=" << onlyActual
						          << "\n";
						if( std::getenv( "OPENVCL_DUMP_KERNEL_REGISTER_CROSSCHECK_DETAIL" ) != NULL )
						{
							for( std::set<std::string>::const_iterator it = expectedRaw.begin();
							     it != expectedRaw.end(); ++it )
								std::cerr << "[kernel-regplan-crosscheck-reg] loop=" << opportunity.label
								          << " reg=" << *it
								          << " expected=1"
								          << " actual=" << ( actualRaw.count( *it ) ? 1 : 0 )
								          << "\n";
							for( std::set<std::string>::const_iterator it = actualRaw.begin();
							     it != actualRaw.end(); ++it )
							{
								if( expectedRaw.count( *it ) ) continue;
								std::cerr << "[kernel-regplan-crosscheck-reg] loop=" << opportunity.label
								          << " reg=" << *it
								          << " expected=0 actual=1"
								          << "\n";
							}
						}
					}
				}
				// Track 9.G step 6a: rewrite-plan synthesis (diagnostic).
				// Reformulates the kernel template + envelope as flat,
				// per-cycle, per-lane token-index sequences suitable for
				// a future stage-aware emitter. Diagnostic-only.
				if( std::getenv( "OPENVCL_DUMP_KERNEL_REWRITE_PLAN" ) != NULL )
				{
					VuKernelEnvelope rpEnv;
					buildVuKernelEnvelope( layout, rpEnv );
					VuKernelRewritePlan rp;
					buildVuKernelRewritePlan( layout, rpEnv, rp );
					std::cerr << "[kernel-rewrite-plan] loop=" << opportunity.label
					          << " II=" << rp.II
					          << " stageCount=" << rp.stageCount
					          << " prolog=" << rp.prologTokens.size()
					          << " main=" << rp.mainTokens.size()
					          << " drain=" << rp.drainTokens.size()
					          << " entryStages=" << rp.entryStages.size()
					          << " stageCells=" << rp.stageCells.size()
					          << " conflicts=" << rp.conflicts
					          << "\n";
					if( std::getenv( "OPENVCL_DUMP_KERNEL_REWRITE_PLAN_STAGES" ) != NULL )
					{
						for( unsigned int s = 0; s < rp.stageCount; ++s )
						{
							for( unsigned int c = 0; c < rp.II; ++c )
							{
								const VuKernelTemplateSlot& cell = rp.stageCells[ s * rp.II + c ];
								if( cell.upper == VuKernelTemplateSlot::NO_ENTRY
								 && cell.lower == VuKernelTemplateSlot::NO_ENTRY
								 && cell.fdiv  == VuKernelTemplateSlot::NO_ENTRY
								 && cell.efu   == VuKernelTemplateSlot::NO_ENTRY ) continue;
								std::cerr << "[kernel-rewrite-plan-stage] loop=" << opportunity.label
								          << " stage=" << s
								          << " modSlot=" << c
								          << " upper=" << cell.upper
								          << " lower=" << cell.lower
								          << " fdiv="  << cell.fdiv
								          << " efu="   << cell.efu
								          << "\n";
							}
						}
					}
					if( std::getenv( "OPENVCL_DUMP_KERNEL_REWRITE_PLAN_DETAIL" ) != NULL )
					{
						static const char* kSectionNames[3] = { "PRO", "MAIN", "DRAIN" };
						static const char* kLaneNames[4] = { "upper", "lower", "fdiv", "efu" };
						const std::vector< unsigned int >* secs[3] = {
							&rp.prologTokens, &rp.mainTokens, &rp.drainTokens };
						for( unsigned int s = 0; s < 3; ++s )
						{
							const std::vector< unsigned int >& v = *secs[s];
							for( unsigned int i = 0; i < v.size(); ++i )
							{
								const unsigned int cycle = i / 4;
								const unsigned int lane  = i % 4;
								std::cerr << "[kernel-rewrite-plan-cell] loop=" << opportunity.label
								          << " section=" << kSectionNames[s]
								          << " cycle=" << cycle
								          << " lane=" << kLaneNames[lane]
								          << " tokenIndex=";
								if( v[i] == VuKernelRewritePlan::NO_TOKEN )
									std::cerr << "-";
								else
									std::cerr << v[i];
								std::cerr << "\n";
							}
						}
					}
					// Track 9.G step 6d: rewrite-plan coverage self-validation.
					// Verify the modulo placer covered the original loop body
					// exactly once (no dropped/duplicated tokens) and that the
					// synthesized MAIN section of VuKernelRewritePlan reproduces
					// the full layout. All comparisons are multiset-based and
					// diagnostic-only; no emission change.
					if( std::getenv( "OPENVCL_DUMP_KERNEL_REWRITE_COVERAGE" ) != NULL )
					{
						std::multiset< unsigned int > bodyTokens(
							opportunity.mainTokenIndices.begin(),
							opportunity.mainTokenIndices.end() );
						std::multiset< unsigned int > layoutTokens;
						for( unsigned int i = 0; i < layout.entries.size(); ++i )
							layoutTokens.insert( layout.entries[i].tokenIndex );
						std::multiset< unsigned int > mainTokens;
						for( unsigned int i = 0; i < rp.mainTokens.size(); ++i )
							if( rp.mainTokens[i] != VuKernelRewritePlan::NO_TOKEN )
								mainTokens.insert( rp.mainTokens[i] );
						const bool bodyEqualsLayout = ( bodyTokens == layoutTokens );
						const bool layoutEqualsMain = ( layoutTokens == mainTokens );
						unsigned int missingFromLayout = 0;
						unsigned int extraInLayout = 0;
						{
							std::multiset< unsigned int >::const_iterator bi = bodyTokens.begin();
							std::multiset< unsigned int >::const_iterator li = layoutTokens.begin();
							while( bi != bodyTokens.end() || li != layoutTokens.end() )
							{
								if( li == layoutTokens.end() ) { ++missingFromLayout; ++bi; continue; }
								if( bi == bodyTokens.end() )   { ++extraInLayout;     ++li; continue; }
								if( *bi < *li )      { ++missingFromLayout; ++bi; }
								else if( *li < *bi ) { ++extraInLayout;     ++li; }
								else                 { ++bi; ++li; }
							}
						}
						std::cerr << "[kernel-rewrite-coverage] loop=" << opportunity.label
						          << " body=" << bodyTokens.size()
						          << " layout=" << layoutTokens.size()
						          << " main=" << mainTokens.size()
						          << " bodyEqualsLayout=" << ( bodyEqualsLayout ? 1 : 0 )
						          << " layoutEqualsMain=" << ( layoutEqualsMain ? 1 : 0 )
						          << " missingFromLayout=" << missingFromLayout
						          << " extraInLayout=" << extraInLayout
						          << "\n";
					}
				}
			}
			// Track 9.G step 6e: VuKernelRewritePlan scaffolding propagation.
			// Always build the kernel layout + rewrite plan from the modulo
			// placer state and stash the result on the opportunity. This is
			// scaffolding only — no consumer reads these fields yet. The
			// cost is minor (a sort + a handful of pushes per loop body).
			{
				VuKernelLayout krLayout;
				krLayout.II         = tryII;
				krLayout.miiStart   = miiStart;
				krLayout.bumps      = bumps;
				krLayout.feasible   = ( failedCount == 0 );
				krLayout.stageCount = krLayout.feasible ? ( maxStage + 1 ) : 0;
				for( unsigned int k = 0; k < n; ++k )
				{
					if( !placed[k] ) continue;
					VuKernelLayoutEntry ent;
					ent.nodeIndex  = k;
					ent.tokenIndex = mt[k];
					ent.slot       = slotOf[k];
					ent.stage      = stageOf[k];
					ent.modSlot    = tryII > 0 ? ( slotOf[k] % tryII ) : 0;
					ent.pipe       = pipeOf[k];
					ent.duration   = durationOf[k];
					krLayout.entries.push_back( ent );
				}
				std::sort( krLayout.entries.begin(), krLayout.entries.end(),
				           vuKernelLayoutEntryLess );
				if( krLayout.feasible && krLayout.II > 0 && !krLayout.entries.empty() )
				{
					VuKernelEnvelope krEnv;
					buildVuKernelEnvelope( krLayout, krEnv );
					VuKernelRewritePlan krRp;
					buildVuKernelRewritePlan( krLayout, krEnv, krRp );
					opportunity.kernelRewriteII         = krRp.II;
					opportunity.kernelRewriteStageCount = krRp.stageCount;
					opportunity.kernelRewriteConflicts  = krRp.conflicts;
					opportunity.kernelRewritePrologTokens = krRp.prologTokens;
					opportunity.kernelRewriteMainTokens   = krRp.mainTokens;
					opportunity.kernelRewriteDrainTokens  = krRp.drainTokens;
					opportunity.kernelRewriteEntryStages  = krRp.entryStages;

					// Track 9.G step 6f: register-plan + rename-hint scaffolding.
					std::vector< VuKernelEntryRegisters > krEntryRegs;
					krEntryRegs.resize( krLayout.entries.size() );
					for( unsigned int e = 0; e < krLayout.entries.size(); ++e )
					{
						const unsigned int ti = krLayout.entries[e].tokenIndex;
						if( ti >= indexedTokens.size() ) continue;
						VuTokenResourceAccess acc;
						if( !buildVuTokenResourceAccess( *indexedTokens[ti], acc ) ) continue;
						for( std::list<std::string>::const_iterator it = acc.registerReads.begin();
						     it != acc.registerReads.end(); ++it )
						{
							if( it->size() >= 2 && (*it)[0] == 'V' && (*it)[1] == 'F' )
								krEntryRegs[e].reads.push_back( *it );
						}
						for( std::list<std::string>::const_iterator it = acc.registerWrites.begin();
						     it != acc.registerWrites.end(); ++it )
						{
							if( it->size() >= 2 && (*it)[0] == 'V' && (*it)[1] == 'F' )
								krEntryRegs[e].writes.push_back( *it );
						}
					}
					VuKernelRegisterPlan krRegPlan;
					buildVuKernelRegisterPlan( krLayout, krEntryRegs, krRegPlan );
					opportunity.kernelRewriteRegCount = krRegPlan.regCount;
					opportunity.kernelRewriteWawCount = krRegPlan.wawCount;
					opportunity.kernelRewriteRawCount = krRegPlan.rawCount;
					opportunity.kernelRewriteWarCount = krRegPlan.warCount;
					opportunity.kernelRewriteHazards  = krRegPlan.hazards;
					std::vector< VuKernelRenameHint > krHints;
					buildVuKernelRenameHints( krRegPlan, krHints );
					opportunity.kernelRewriteRenameHints = krHints;

					// Track 9.G step 8b-1: per-register rename decisions.
					// Allocate a scratch VF per unique hint reg using the
					// loop body's actual VF base set as the reserved pool.
					{
						std::vector< std::string > krUsedBases;
						for( unsigned int e = 0; e < krEntryRegs.size(); ++e )
						{
							const VuKernelEntryRegisters& er = krEntryRegs[ e ];
							for( unsigned int k = 0; k < er.reads.size(); ++k )
								krUsedBases.push_back( er.reads[ k ] );
							for( unsigned int k = 0; k < er.writes.size(); ++k )
								krUsedBases.push_back( er.writes[ k ] );
						}
						std::vector< VuKernelRenameDecision > krDecisions;
						buildVuKernelRenameDecisions( krHints, krUsedBases, krDecisions );
						opportunity.kernelRewriteRenameDecisions = krDecisions;

						// Track 9.G step 8b-2a: per-decision MOVE slot.
						// Picks (modSlot, lane) for the `reg <- scratch`
						// move inside MAIN_LOOP. Diagnostic-only; the
						// eligibility predicate still rejects plans with
						// non-empty renameHints.
						VuKernelTemplate krMoveTmpl;
						buildVuKernelTemplate( krLayout, krMoveTmpl );
						std::vector< VuKernelRenameMoveSlot > krMoveSlots;
						buildVuKernelRenameMoveSlots( krDecisions, krLayout,
						                              krEntryRegs, krMoveTmpl,
						                              krMoveSlots );
						opportunity.kernelRewriteRenameMoveSlots = krMoveSlots;
					}

					// Track 9.G step 6g: envelope scaffolding.
					opportunity.kernelEnvelopeKernelTokens   = krEnv.kernelTokens;
					opportunity.kernelEnvelopePrologueCycles = krEnv.prologueCycles;
					opportunity.kernelEnvelopeEpilogueCycles = krEnv.epilogueCycles;
					opportunity.kernelEnvelopeConflicts      = krEnv.conflicts;
					opportunity.kernelEnvelopePrologueTokenCounts = krEnv.prologueTokenCounts;
					opportunity.kernelEnvelopeEpilogueTokenCounts = krEnv.epilogueTokenCounts;

					// Track 9.G step 6h: stageCells VLIW grid.
					opportunity.kernelRewriteStageCells = krRp.stageCells;

					// Track 9.G-1h step 4b-3a: per-loop expanded-DDG placer
					// decision. After the placer converges and rename
					// decisions are known, project the rename-emission
					// expansion (split clones + materialize MOVEs + tail
					// MOVEs) and compare against the placer grid capacity
					// (II*4). Emits a [expanded-ddg-placer] verdict per
					// rename-eligible loop. Today this is diagnostic-only
					// and always "passthrough" (4b-1 confirmed fits=1 for
					// all 7 plans). 4b-3b will pivot on "refit-needed" by
					// rebuilding the DDG with expanded nodes and re-running
					// the placer at this same scope. Gated by
					// OPENVCL_USE_EXPANDED_DDG_PLACER to keep the default
					// path byte-identical until the refit path lands.
					// Suppressed when this helper is recursed from
					// runExpandedDDGRefitDiagnostic on a shadow opportunity
					// (emitExpandedDDGDiagnostic=false) to avoid duplicate /
					// misleading diag lines.
					if( emitExpandedDDGDiagnostic
					    && std::getenv( "OPENVCL_USE_EXPANDED_DDG_PLACER" ) != NULL )
					{
						VuSoftwarePipelineRewritePlan probe;
						probe.label                        = opportunity.label;
						probe.kernelRewriteII              = opportunity.kernelRewriteII;
						probe.kernelRewriteStageCount      = opportunity.kernelRewriteStageCount;
						probe.kernelRewriteConflicts       = opportunity.kernelRewriteConflicts;
						probe.kernelRewriteMainTokens      = opportunity.kernelRewriteMainTokens;
						probe.kernelRewriteRenameHints     = opportunity.kernelRewriteRenameHints;
						probe.kernelRewriteRenameDecisions = opportunity.kernelRewriteRenameDecisions;
						probe.kernelRewriteRenameMoveSlots = opportunity.kernelRewriteRenameMoveSlots;
						if( isVuPlanEligibleForKernelRenameEmission( probe, indexedTokens ) )
						{
							unsigned int assignedDecisions = 0;
							for( unsigned int d = 0; d < probe.kernelRewriteRenameDecisions.size(); ++d )
								if( probe.kernelRewriteRenameDecisions[d].assigned )
									++assignedDecisions;
							unsigned int splitGroups        = 0;
							unsigned int expandedFromSplits = 0;
							unsigned int materializeMOVEs   = 0;
							unsigned int passthrough        = 0;
							for( unsigned int m = 0; m < probe.kernelRewriteMainTokens.size(); ++m )
							{
								const unsigned int idx = probe.kernelRewriteMainTokens[m];
								if( idx == VuKernelRewritePlan::NO_TOKEN )
									continue;
								if( idx >= indexedTokens.size() )
									continue;
								const Token& src = *indexedTokens[idx];
								if( tokenIsKernelRenameMaterializeCandidate( src, probe ) )
								{
									// 4b-8a: count only decisions whose original
									// base is actually read by `src`.
									for( unsigned int d = 0; d < probe.kernelRewriteRenameDecisions.size(); ++d )
									{
										if( !probe.kernelRewriteRenameDecisions[d].assigned )
											continue;
										if( tokenReadsDecisionBase( src, probe.kernelRewriteRenameDecisions[d].reg ) )
											++materializeMOVEs;
									}
									++passthrough;
									continue;
								}
								std::list<Token> split;
								splitMultiFieldOpByFieldDecisions( src, probe.kernelRewriteRenameDecisions, split );
								const unsigned int sz = static_cast<unsigned int>( split.size() );
								if( sz > 1u )
								{
									++splitGroups;
									expandedFromSplits += sz;
								}
								else
								{
									++passthrough;
								}
							}
							// 4b-8b: count only decisions whose base is read by
							// a drain token (others' tail MOVEs are skipped).
							unsigned int tailMOVEs = 0;
							for( unsigned int d = 0; d < probe.kernelRewriteRenameDecisions.size(); ++d )
							{
								if( !probe.kernelRewriteRenameDecisions[d].assigned )
									continue;
								if( drainReadsDecisionBase( probe, indexedTokens, probe.kernelRewriteRenameDecisions[d].reg ) )
									++tailMOVEs;
							}
							const unsigned int expandedMainTokens = expandedFromSplits + materializeMOVEs + tailMOVEs + passthrough;
							const unsigned int gridCapacity       = probe.kernelRewriteII * 4u;
							const bool         needsRefit         = expandedMainTokens > gridCapacity;
							std::cerr << "[expanded-ddg-placer] loop=" << probe.label
							          << " II=" << probe.kernelRewriteII
							          << " gridCapacity=" << gridCapacity
							          << " expandedMainTokens=" << expandedMainTokens
							          << " splitGroups=" << splitGroups
							          << " expandedFromSplits=" << expandedFromSplits
							          << " materializeMOVEs=" << materializeMOVEs
							          << " tailMOVEs=" << tailMOVEs
							          << " passthrough=" << passthrough
							          << " decision=" << ( needsRefit ? "refit-needed" : "passthrough" )
							          << "\n";

							// Track 9.G-1h step 4b-3b-1: per-node descriptor dump.
							// Builds the expanded node list (split clones +
							// materialize MOVEs + tail MOVEs) and emits one
							// [expanded-node] line per node. Diagnostic-only;
							// the refit placer (4b-3b-3 onward) will consume
							// these descriptors.
							if( std::getenv( "OPENVCL_DUMP_EXPANDED_NODES" ) != NULL )
							{
								std::vector<VuKernelExpandedNode> nodes;
								buildVuKernelExpandedNodes( probe, indexedTokens, nodes );
								std::cerr << "[expanded-nodes] loop=" << probe.label
								          << " count=" << nodes.size() << "\n";
								for( unsigned int q = 0; q < nodes.size(); ++q )
								{
									const VuKernelExpandedNode& en = nodes[q];
									const char* op = "?";
									if( en.sourceTokenIndex < indexedTokens.size() )
									{
										const Token* st = indexedTokens[ en.sourceTokenIndex ];
										if( st && st->operand() )
											op = st->operand()->name().c_str();
									}
									std::cerr << "[expanded-node] loop=" << probe.label
									          << " idx=" << q
									          << " role=" << vuKernelExpandedNodeRoleName( en.role )
									          << " cell=";
									if( en.sourceCellIndex == static_cast<unsigned int>(-1) ) std::cerr << "-";
									else                                                       std::cerr << en.sourceCellIndex;
									std::cerr << " srcTok=";
									if( en.sourceTokenIndex == static_cast<unsigned int>(-1) ) std::cerr << "-";
									else                                                        std::cerr << en.sourceTokenIndex;
									std::cerr << " op=" << op
									          << " decIdx=";
									if( en.decisionIndex == static_cast<unsigned int>(-1) ) std::cerr << "-";
									else                                                     std::cerr << en.decisionIndex;
									std::cerr << " cloneOrd=" << en.cloneOrdinal
									          << "\n";
								}
							}
						}
					}

					// Track 9.G-1h step 2: emission-order vs placer-grid diff.
					// Walks krRp.mainTokens (the order the rewrite emitter will
					// hand to the downstream pipe-pair scheduler) and reports
					// each token's (modSlot, lane) from the placer layout.
					// Diagnostic-only; surfaces how far the emitted order
					// drifts from the placer's chosen schedule. Counts
					// adjacent inversions vs (modSlot, upper-before-lower)
					// canonical order as `outOfOrder`. MD5-invariant.
					if( std::getenv( "OPENVCL_DUMP_KERNEL_REWRITE_ORDER" ) != NULL )
					{
						// Build tokenIndex -> (modSlot, pipe) map from layout.
						std::map< unsigned int, std::pair< unsigned int, unsigned int > > tokToSlot;
						for( unsigned int e = 0; e < krLayout.entries.size(); ++e )
						{
							const VuKernelLayoutEntry& en = krLayout.entries[ e ];
							tokToSlot[ en.tokenIndex ] = std::make_pair( en.modSlot, en.pipe );
						}
						unsigned int outOfOrder = 0;
						unsigned int unmapped   = 0;
						bool         havePrev   = false;
						unsigned int prevSlot   = 0;
						unsigned int prevLane   = 0;
						for( unsigned int m = 0; m < krRp.mainTokens.size(); ++m )
						{
							const unsigned int ti = krRp.mainTokens[ m ];
							std::map< unsigned int, std::pair< unsigned int, unsigned int > >::const_iterator it = tokToSlot.find( ti );
							if( it == tokToSlot.end() ) { ++unmapped; continue; }
							const unsigned int sl = it->second.first;
							const unsigned int la = it->second.second;
							if( havePrev )
							{
								// Canonical sort key is (modSlot, pipe). Upper=1,
								// Lower=2 keeps upper before lower at same slot.
								if( sl < prevSlot || ( sl == prevSlot && la < prevLane ) )
									++outOfOrder;
							}
							havePrev = true;
							prevSlot = sl;
							prevLane = la;
						}
						std::cerr << "[kernel-rewrite-order] loop=" << opportunity.label
						          << " II=" << krRp.II
						          << " mainTokens=" << krRp.mainTokens.size()
						          << " unmapped=" << unmapped
						          << " outOfOrder=" << outOfOrder
						          << "\n";
						if( std::getenv( "OPENVCL_DUMP_KERNEL_REWRITE_ORDER_TOKENS" ) != NULL )
						{
							for( unsigned int m = 0; m < krRp.mainTokens.size(); ++m )
							{
								const unsigned int ti = krRp.mainTokens[ m ];
								std::map< unsigned int, std::pair< unsigned int, unsigned int > >::const_iterator it = tokToSlot.find( ti );
								const char* op = "?";
								if( ti < indexedTokens.size() && indexedTokens[ ti ]->operand() )
									op = indexedTokens[ ti ]->operand()->name().c_str();
								if( it == tokToSlot.end() )
								{
									std::cerr << "[kernel-rewrite-order-tok] loop=" << opportunity.label
									          << " emitOrder=" << m
									          << " token=" << ti
									          << " op=" << op
									          << " modSlot=- lane=- (unmapped)\n";
								}
								else
								{
									const char* pname = "none";
									switch( it->second.second )
									{
									case 1: pname = "upper"; break;
									case 2: pname = "lower"; break;
									case 3: pname = "fdiv";  break;
									case 4: pname = "efu";   break;
									default: break;
									}
									std::cerr << "[kernel-rewrite-order-tok] loop=" << opportunity.label
									          << " emitOrder=" << m
									          << " token=" << ti
									          << " op=" << op
									          << " modSlot=" << it->second.first
									          << " lane=" << pname
									          << "\n";
								}
							}
						}
					}

					// Track 9.G-1h step 3: per-cycle latency validation
					// of the placer grid. Builds the kernel template
					// (modSlot -> upper/lower/fdiv/efu entry indices),
					// simulates 2 x II cycles through VuLatencyTracker
					// (iteration 0 = warm-up, iteration 1 = steady
					// state), and counts any non-zero readHazardDelay
					// returned during the steady-state iteration. Any
					// violation means the placer's modulo schedule is
					// not honoured by hardware latency rules. Surface
					// under OPENVCL_DUMP_KERNEL_GRID_LATENCY; per-cell
					// detail under OPENVCL_DUMP_KERNEL_GRID_LATENCY_DETAIL.
					// MD5-invariant.
					if( std::getenv( "OPENVCL_DUMP_KERNEL_GRID_LATENCY" ) != NULL )
					{
						VuKernelTemplate krGrid;
						buildVuKernelTemplate( krLayout, krGrid );
						const unsigned int II2 = krLayout.II;
						unsigned int       violations = 0;
						unsigned int       maxStall   = 0;
						const bool         detail     = ( std::getenv( "OPENVCL_DUMP_KERNEL_GRID_LATENCY_DETAIL" ) != NULL );
						VuLatencyTracker   tracker;
						for( unsigned int iter = 0; iter < 2; ++iter )
						{
							for( unsigned int ms = 0; ms < II2; ++ms )
							{
								const int cycle = (int)( iter * II2 + ms );
								const VuKernelTemplateSlot& sl = krGrid.slots[ ms ];
								const Token* upperTok = NULL;
								const Token* lowerTok = NULL;
								if( sl.upper != VuKernelTemplateSlot::NO_ENTRY )
								{
									const unsigned int ti = krLayout.entries[ sl.upper ].tokenIndex;
									if( ti < indexedTokens.size() ) upperTok = indexedTokens[ ti ];
								}
								if( sl.lower != VuKernelTemplateSlot::NO_ENTRY )
								{
									const unsigned int ti = krLayout.entries[ sl.lower ].tokenIndex;
									if( ti < indexedTokens.size() ) lowerTok = indexedTokens[ ti ];
								}
								if( upperTok )
								{
									const int d = tracker.readHazardDelay( *upperTok, lowerTok, cycle );
									if( iter == 1 && d > 0 )
									{
										++violations;
										if( (unsigned int)d > maxStall ) maxStall = (unsigned int)d;
										if( detail )
											std::cerr << "[kernel-grid-latency-violation] loop=" << opportunity.label
											          << " cycle=" << cycle
											          << " modSlot=" << ms
											          << " lane=upper"
											          << " op=" << ( upperTok->operand() ? upperTok->operand()->name().c_str() : "?" )
											          << " stall=" << d
											          << "\n";
									}
								}
								if( lowerTok )
								{
									const int d = tracker.readHazardDelay( *lowerTok, upperTok, cycle );
									if( iter == 1 && d > 0 )
									{
										++violations;
										if( (unsigned int)d > maxStall ) maxStall = (unsigned int)d;
										if( detail )
											std::cerr << "[kernel-grid-latency-violation] loop=" << opportunity.label
											          << " cycle=" << cycle
											          << " modSlot=" << ms
											          << " lane=lower"
											          << " op=" << ( lowerTok->operand() ? lowerTok->operand()->name().c_str() : "?" )
											          << " stall=" << d
											          << "\n";
									}
								}
								if( upperTok ) tracker.recordWrites( *upperTok, cycle );
								if( lowerTok ) tracker.recordWrites( *lowerTok, cycle );
							}
						}
						std::cerr << "[kernel-grid-latency] loop=" << opportunity.label
						          << " II=" << II2
						          << " violations=" << violations
						          << " maxStall=" << maxStall
						          << "\n";
					}
				}
			}
			if( std::getenv( "OPENVCL_DUMP_LOOP_SCHEDULE" ) != NULL )
			{
				const bool overlapForced = ( n > tryII );
				std::cerr << "[loop-schedule] loop=" << opportunity.label
				          << " II=" << tryII
				          << " miiStart=" << miiStart
				          << " bumps=" << bumps
				          << " bodyNodes=" << n
				          << " overlapForced=" << ( overlapForced ? 1 : 0 )
				          << " placed=" << placedCount
				          << " failed=" << failedCount
				          << " maxStage=" << maxStage
				          << " edges=" << totalEdges
				          << " edgeViolations=" << edgeViolations
				          << " evictions=" << evictions
				          << " recoveries=" << recoveries
				          << " upperOcc=" << upperOccFinal
				          << " lowerOcc=" << lowerOccFinal
				          << " fdivOcc=" << fdivOccFinal
				          << " efuOcc=" << efuOccFinal
				          << "\n";
			}
			if( std::getenv( "OPENVCL_DUMP_LOOP_SCHEDULE_NODES" ) != NULL )
			{
				for( unsigned int rank = 0; rank < pr.order.size(); ++rank )
				{
					const unsigned int i = pr.order[rank];
					const char* pname = "none";
					switch( pipeOf[i] )
					{
					case 1: pname = "upper"; break;
					case 2: pname = "lower"; break;
					case 3: pname = "fdiv";  break;
					case 4: pname = "efu";   break;
					default: break;
					}
					std::cerr << "[loop-schedule-node] loop=" << opportunity.label
					          << " rank=" << rank
					          << " node=" << i
					          << " token=" << mt[i]
					          << " pipe=" << pname
					          << " slot=" << ( placed[i] ? static_cast<int>( slotOf[i] ) : -1 )
					          << " stage=" << ( placed[i] ? static_cast<int>( stageOf[i] ) : -1 )
					          << " status=" << ( placed[i] ? "placed" : "failed" )
					          << "\n";
				}
			}
	}

	// Track 9.G-1h step 4b-3b-3: shadow-opportunity refit diagnostic.
	//
	// After the original placer + scaffolding has run on `opportunity`,
	// build the expanded-DDG node list (split clones + materialize MOVEs
	// + tail MOVEs) and synthesize a SHADOW VuLoopPipelineOpportunity
	// whose mainTokenIndices reflect the post-expansion sequence. Real
	// source-token indices are reused for PASSTHROUGH/SPLIT_CLONE nodes;
	// MATERIALIZE_MOVE / TAIL_MOVE nodes are backed by synthetic MOVE
	// tokens (via syntheticMoveOperand()) owned in a local std::list,
	// then appended to a shadow indexedTokens vector. The shadow run is
	// invoked with emitExpandedDDGDiagnostic=false so the inner
	// [expanded-ddg-placer] / [expanded-nodes] gates stay silent for the
	// shadow, leaving this helper as the single source of the
	// [expanded-ddg-refit] line.
	//
	// Track 9.G-1h step 4b-6 (cleanup): the shadow-placer work and the
	// `opportunity.kernelRewriteRefit*` field population always run so
	// downstream consumers can read the refit verdict without setting
	// any env var. Only the `[expanded-ddg-refit]` stderr line below
	// remains gated on OPENVCL_USE_EXPANDED_DDG_PLACER (same env var as
	// 4b-3a / 4b-3b-1) for diagnostic toggling. The shadow opportunity
	// is a value copy with its kernelRewrite* / kernelEnvelope* fields
	// cleared and then re-populated by runVuKernelPlacerAndScaffolding;
	// the caller's opportunity is untouched apart from the four
	// kernelRewriteRefit* scalar fields. tokens (3rd arg) is forwarded
	// as-is because the helper does not consume the bare std::list
	// (audited at extraction time).
	static void runExpandedDDGRefitDiagnostic(
	    VuLoopPipelineOpportunity& opportunity,
	    const std::vector<const Token*>& indexedTokens,
	    const std::list<Token>& tokens )
	{
		if( !opportunity.simpleCountedLoop )
			return;
		if( opportunity.mainTokenIndices.empty() )
			return;
		if( opportunity.kernelRewriteMainTokens.empty() )
			return;

		// Build the shadow probe in the same shape as the existing
		// [expanded-ddg-placer] / [expanded-nodes] diagnostics consume.
		VuSoftwarePipelineRewritePlan probe;
		probe.label                        = opportunity.label;
		probe.kernelRewriteII              = opportunity.kernelRewriteII;
		probe.kernelRewriteStageCount      = opportunity.kernelRewriteStageCount;
		probe.kernelRewriteConflicts       = opportunity.kernelRewriteConflicts;
		probe.kernelRewriteMainTokens      = opportunity.kernelRewriteMainTokens;
		probe.kernelRewriteRenameHints     = opportunity.kernelRewriteRenameHints;
		probe.kernelRewriteRenameDecisions = opportunity.kernelRewriteRenameDecisions;
		probe.kernelRewriteRenameMoveSlots = opportunity.kernelRewriteRenameMoveSlots;

		std::vector<VuKernelExpandedNode> nodes;
		buildVuKernelExpandedNodes( probe, indexedTokens, nodes );
		if( nodes.empty() )
			return;

		// Pick a donor token (any real token in the loop) to satisfy
		// Token's `const Line&` reference requirement when copy-
		// constructing synthetic MOVEs.
		const Token* donor = NULL;
		for( unsigned int i = 0; i < opportunity.mainTokenIndices.size(); ++i )
		{
			const unsigned int idx = opportunity.mainTokenIndices[i];
			if( idx < indexedTokens.size() && indexedTokens[idx] != NULL )
			{
				donor = indexedTokens[idx];
				break;
			}
		}
		if( donor == NULL )
			return;

		std::list<Token>                shadowOwned;
		std::vector<const Token*>       shadowIndexed = indexedTokens;
		std::vector<unsigned int>       shadowMainTokenIndices;
		shadowMainTokenIndices.reserve( nodes.size() );

		// Track 9.G-1h step 4b-4: per-source-cell cache of synthesized
		// split-clone token indices. The first SPLIT_CLONE node for a
		// cell triggers splitMultiFieldOpByFieldDecisions(); the resulting
		// per-field clones (each with its own DEST scratch + narrowed
		// field mask, exactly as the rename emitter will produce) are
		// appended to shadowOwned / shadowIndexed and their shadow
		// indices recorded here. Subsequent SPLIT_CLONE nodes for the
		// same cell read the cached index by cloneOrdinal. Before this
		// step the helper reused n.sourceTokenIndex for every clone,
		// which caused the shadow DDG to see N identical FMACs (same
		// dest, same fields) producing false WAW / lane conflicts and
		// inflating shadowII far above origII.
		std::map<unsigned int, std::vector<unsigned int> > splitCloneIndexCache;

		// Track 9.G-1h step 4b-7a: inverse map shadowIndexed-idx -> node
		// index, populated as we push each shadow main token. After the
		// shadow placer runs we translate shadow.kernelRewriteMainTokens
		// (a flat II*4 grid with VuKernelRewritePlan::NO_TOKEN sentinels
		// over shadowIndexed indices) into a refit grid whose non-empty
		// cells reference the publicly-published
		// opportunity.kernelRewriteRefitNodes vector. This is the only
		// way codegen (4b-7b/c) can reconstruct the rewritten MAIN body
		// since the shadow layout/template lives only inside this helper.
		std::map<unsigned int, unsigned int> shadowIdxToNodeIdx;

		unsigned int passCount   = 0;
		unsigned int splitCount  = 0;
		unsigned int matMoveCount = 0;
		unsigned int tailMoveCount = 0;

		for( unsigned int i = 0; i < nodes.size(); ++i )
		{
			const VuKernelExpandedNode& n = nodes[i];
			switch( n.role )
			{
				case VuKernelExpandedNode::ROLE_PASSTHROUGH:
					++passCount;
					if( n.sourceTokenIndex < indexedTokens.size() )
					{
						shadowIdxToNodeIdx[ n.sourceTokenIndex ] = i;
						shadowMainTokenIndices.push_back( n.sourceTokenIndex );
					}
					break;
				case VuKernelExpandedNode::ROLE_SPLIT_CLONE:
				{
					++splitCount;
					if( n.sourceTokenIndex >= indexedTokens.size() )
						break;
					std::map<unsigned int, std::vector<unsigned int> >::iterator
					    cacheIt = splitCloneIndexCache.find( n.sourceCellIndex );
					if( cacheIt == splitCloneIndexCache.end() )
					{
						std::list<Token> split;
						splitMultiFieldOpByFieldDecisions(
						    *indexedTokens[ n.sourceTokenIndex ],
						    opportunity.kernelRewriteRenameDecisions,
						    split );
						std::vector<unsigned int> cloneShadowIndices;
						cloneShadowIndices.reserve( split.size() );
						for( std::list<Token>::const_iterator s = split.begin();
						     s != split.end(); ++s )
						{
							shadowOwned.push_back( *s );
							const unsigned int newIdx = static_cast<unsigned int>(
							    shadowIndexed.size() );
							shadowIndexed.push_back( &shadowOwned.back() );
							cloneShadowIndices.push_back( newIdx );
						}
						cacheIt = splitCloneIndexCache.insert(
						    std::make_pair( n.sourceCellIndex, cloneShadowIndices ) ).first;
					}
					if( n.cloneOrdinal < cacheIt->second.size() )
					{
						const unsigned int sIdx = cacheIt->second[ n.cloneOrdinal ];
						shadowIdxToNodeIdx[ sIdx ] = i;
						shadowMainTokenIndices.push_back( sIdx );
					}
					break;
				}
				case VuKernelExpandedNode::ROLE_MATERIALIZE_MOVE:
				case VuKernelExpandedNode::ROLE_TAIL_MOVE:
				{
					if( n.role == VuKernelExpandedNode::ROLE_MATERIALIZE_MOVE )
						++matMoveCount;
					else
						++tailMoveCount;
					Token mv( *donor );
					mv.setLabel( "" );
					mv.setName( "move" );
					mv.setOperand( syntheticMoveOperand() );
					mv.setFlags( Token::PROCESSED );
					mv.setBroadcast( 0 );
					mv.setFields( 0 );
					mv.arguments().clear();
					shadowOwned.push_back( mv );
					const unsigned int newIdx = static_cast<unsigned int>( shadowIndexed.size() );
					shadowIndexed.push_back( &shadowOwned.back() );
					shadowIdxToNodeIdx[ newIdx ] = i;
					shadowMainTokenIndices.push_back( newIdx );
					break;
				}
			}
		}

		if( shadowMainTokenIndices.empty() )
			return;

		VuLoopPipelineOpportunity shadow = opportunity;
		shadow.mainTokenIndices = shadowMainTokenIndices;
		// Clear placer outputs so we measure a fresh shadow placement.
		shadow.kernelRewriteII             = 0;
		shadow.kernelRewriteStageCount     = 0;
		shadow.kernelRewriteConflicts      = 0;
		shadow.kernelRewritePrologTokens.clear();
		shadow.kernelRewriteMainTokens.clear();
		shadow.kernelRewriteDrainTokens.clear();
		shadow.kernelRewriteEntryStages.clear();
		shadow.kernelRewriteStageCells.clear();

		runVuKernelPlacerAndScaffolding( shadow, shadowIndexed, tokens,
		                                 /*emitExpandedDDGDiagnostic=*/false );

		// Track 9.G-1h step 4b-3b-4: publish scalar refit metrics back to
		// the caller's opportunity so downstream consumers (4b-3b-5
		// onward) can see the shadow placer's verdict without rerunning
		// it. Cells are deliberately not published — their layout-entry
		// indices reference the shadow VuKernelLayout which lives only
		// inside the helper.
		opportunity.kernelRewriteRefitII             = shadow.kernelRewriteII;
		opportunity.kernelRewriteRefitStageCount     = shadow.kernelRewriteStageCount;
		opportunity.kernelRewriteRefitConflicts      = shadow.kernelRewriteConflicts;
		opportunity.kernelRewriteRefitMainTokenCount =
		    static_cast<unsigned int>( shadowMainTokenIndices.size() );

		// Track 9.G-1h step 4b-7a: publish the refit node vector and the
		// translated refit main grid. The grid mirrors
		// shadow.kernelRewriteMainTokens cell-for-cell but maps each non-
		// empty shadowIndexed-idx through shadowIdxToNodeIdx so that each
		// non-empty cell references an entry in
		// opportunity.kernelRewriteRefitNodes. Empty cells use
		// VuKernelRefitNode::NO_INDEX (== -1u). Both vectors are left
		// empty if the refit placer reported conflicts so downstream
		// consumers (4b-7b/c) can treat (refitConflicts==0 &&
		// !refitMainTokens.empty()) as the single eligibility predicate.
		opportunity.kernelRewriteRefitNodes.clear();
		opportunity.kernelRewriteRefitMainTokens.clear();
		if( shadow.kernelRewriteConflicts == 0 && !shadow.kernelRewriteMainTokens.empty() )
		{
			opportunity.kernelRewriteRefitNodes.reserve( nodes.size() );
			for( unsigned int ni = 0; ni < nodes.size(); ++ni )
			{
				const VuKernelExpandedNode& src = nodes[ni];
				VuKernelRefitNode dst;
				switch( src.role )
				{
				case VuKernelExpandedNode::ROLE_PASSTHROUGH:
					dst.role = VuKernelRefitNode::ROLE_PASSTHROUGH; break;
				case VuKernelExpandedNode::ROLE_SPLIT_CLONE:
					dst.role = VuKernelRefitNode::ROLE_SPLIT_CLONE; break;
				case VuKernelExpandedNode::ROLE_MATERIALIZE_MOVE:
					dst.role = VuKernelRefitNode::ROLE_MATERIALIZE_MOVE; break;
				case VuKernelExpandedNode::ROLE_TAIL_MOVE:
					dst.role = VuKernelRefitNode::ROLE_TAIL_MOVE; break;
				}
				dst.sourceCellIndex  = src.sourceCellIndex;
				dst.sourceTokenIndex = src.sourceTokenIndex;
				dst.decisionIndex    = src.decisionIndex;
				dst.cloneOrdinal     = src.cloneOrdinal;
				opportunity.kernelRewriteRefitNodes.push_back( dst );
			}
			opportunity.kernelRewriteRefitMainTokens.reserve(
			    shadow.kernelRewriteMainTokens.size() );
			for( unsigned int g = 0; g < shadow.kernelRewriteMainTokens.size(); ++g )
			{
				const unsigned int sIdx = shadow.kernelRewriteMainTokens[g];
				if( sIdx == VuKernelRewritePlan::NO_TOKEN )
				{
					opportunity.kernelRewriteRefitMainTokens.push_back(
					    VuKernelRefitNode::NO_INDEX );
					continue;
				}
				std::map<unsigned int, unsigned int>::const_iterator
				    mit = shadowIdxToNodeIdx.find( sIdx );
				if( mit == shadowIdxToNodeIdx.end() )
				{
					// Inverse map miss: refit placement references a
					// shadow token we did not push (should not happen,
					// but stay conservative and drop the publish).
					opportunity.kernelRewriteRefitNodes.clear();
					opportunity.kernelRewriteRefitMainTokens.clear();
					break;
				}
				opportunity.kernelRewriteRefitMainTokens.push_back( mit->second );
			}
		}

		if( std::getenv( "OPENVCL_USE_EXPANDED_DDG_PLACER" ) != NULL )
		{
			std::cerr << "[expanded-ddg-refit] loop=" << opportunity.label
			          << " nodes=" << nodes.size()
			          << " pass=" << passCount
			          << " split=" << splitCount
			          << " matMOVE=" << matMoveCount
			          << " tailMOVE=" << tailMoveCount
			          << " origII=" << opportunity.kernelRewriteII
			          << " shadowII=" << shadow.kernelRewriteII
			          << " origStages=" << opportunity.kernelRewriteStageCount
			          << " shadowStages=" << shadow.kernelRewriteStageCount
			          << " origMain=" << opportunity.mainTokenIndices.size()
			          << " shadowMain=" << shadowMainTokenIndices.size()
			          << "\n";
		}
	}
}

std::vector<VuLoopPipelineOpportunity> findVuLoopPipelineOpportunities( const std::list<Token>& tokens )
{
	std::vector<VuLoopPipelineOpportunity> result;
	std::vector<const Token*> indexedTokens;
	for( std::list<Token>::const_iterator i = tokens.begin(); i != tokens.end(); ++i )
		indexedTokens.push_back( &*i );

	std::vector<VuLoopCandidate> loops = findVuLoopCandidates( tokens );

	for( std::vector<VuLoopCandidate>::const_iterator loop = loops.begin(); loop != loops.end(); ++loop )
	{
		unsigned int qProducerCount = 0;
		unsigned int qProducerOffset = 0;
		std::vector<unsigned int> qProducerOffsets;
		for( unsigned int i = 0; i < loop->bodyTokens.size(); ++i )
		{
			if( vuTokenWritesQ( *loop->bodyTokens[i] ) )
			{
				++qProducerCount;
				qProducerOffset = i;
				qProducerOffsets.push_back( i );
			}
		}

		if( qProducerCount == 0 )
			continue;

		std::vector<unsigned int> lastQConsumerOffsets;
		for( unsigned int i = qProducerOffset + 1; i < loop->bodyTokens.size(); ++i )
		{
			if( vuTokenReadsQ( *loop->bodyTokens[i] ) )
				lastQConsumerOffsets.push_back( i );
		}

		const unsigned int branchDelaySlots = loop->branchToken ? vuTokenBranchDelaySlots( *loop->branchToken ) : 0;
		const unsigned int branchOffset = loop->bodyTokens.empty()
		                                ? 0
		                                : static_cast<unsigned int>( loop->bodyTokens.size() - 1 );

		VuLoopPipelineOpportunity opportunity;
		opportunity.label = loop->label;
		opportunity.labelTokenIndex = loop->labelTokenIndex;
		opportunity.branchTokenIndex = loop->branchTokenIndex;
		opportunity.qProducerTokenIndex = loop->firstBodyTokenIndex + qProducerOffset;
		for( std::vector<unsigned int>::const_iterator producer = qProducerOffsets.begin();
		     producer != qProducerOffsets.end(); ++producer )
			opportunity.qProducerTokenIndices.push_back( loop->firstBodyTokenIndex + *producer );
		std::vector<unsigned int> allQConsumerOffsets;
		for( unsigned int producerIndex = 0; producerIndex < qProducerOffsets.size(); ++producerIndex )
		{
			const unsigned int producerOffset = qProducerOffsets[producerIndex];
			const unsigned int nextProducerOffset =
			    producerIndex + 1 < qProducerOffsets.size()
			    ? qProducerOffsets[producerIndex + 1]
			    : static_cast<unsigned int>( loop->bodyTokens.size() );
			VuLoopQStage stage;
			stage.qProducerTokenIndex = loop->firstBodyTokenIndex + producerOffset;
			stage.qProducerLatency = loop->bodyTokens[producerOffset]->operand()
			                       ? loop->bodyTokens[producerOffset]->operand()->latency()
			                       : 0;
			for( unsigned int consumerOffset = producerOffset + 1;
			     consumerOffset < nextProducerOffset && consumerOffset < loop->bodyTokens.size();
			     ++consumerOffset )
			{
				if( vuTokenReadsQ( *loop->bodyTokens[consumerOffset] ) )
				{
					stage.qConsumerTokenIndices.push_back( loop->firstBodyTokenIndex + consumerOffset );
					allQConsumerOffsets.push_back( consumerOffset );
				}
			}
			if( !stage.qConsumerTokenIndices.empty() )
			{
				const unsigned int firstConsumerOffset =
				    stage.qConsumerTokenIndices.front() - loop->firstBodyTokenIndex;
				const unsigned int lastConsumerOffset =
				    stage.qConsumerTokenIndices.back() - loop->firstBodyTokenIndex;
				stage.qProducerConsumerGapCycles =
				    countEmittableTokens( *loop, producerOffset + 1, firstConsumerOffset );
				stage.qProducerConsumerGapDeficitCycles =
				    stage.qProducerLatency > stage.qProducerConsumerGapCycles
				    ? stage.qProducerLatency - stage.qProducerConsumerGapCycles
				    : 0;
				stage.loopCarriedQGapCycles =
				    countEmittableTokens( *loop, firstConsumerOffset + 1, branchOffset )
				    + branchDelaySlots
				    + countEmittableTokens( *loop, 0, producerOffset );
				stage.qProducerInsertionGapCycles =
				    countEmittableTokens( *loop, lastConsumerOffset + 1, branchOffset )
				    + branchDelaySlots
				    + countEmittableTokens( *loop, 0, producerOffset );
				stage.qProducerInsertionGapDeficitCycles =
				    stage.qProducerLatency > stage.qProducerInsertionGapCycles
				    ? stage.qProducerLatency - stage.qProducerInsertionGapCycles
				    : 0;
				stage.qSchedulingStrategy =
				    classifyLoopQSchedulingStrategy( stage.qProducerConsumerGapDeficitCycles,
				                                     stage.loopCarriedQGapCycles,
				                                     stage.qProducerLatency );
			}
			opportunity.qStages.push_back( stage );
		}
		if( allQConsumerOffsets.empty() )
			continue;
		const std::vector<unsigned int>& primaryQConsumerOffsets =
			lastQConsumerOffsets.empty() ? allQConsumerOffsets : lastQConsumerOffsets;
		opportunity.firstQConsumerTokenIndex = loop->firstBodyTokenIndex + allQConsumerOffsets.front();
		opportunity.lastQConsumerTokenIndex = loop->firstBodyTokenIndex + allQConsumerOffsets.back();
		opportunity.qProducerLatency = loop->bodyTokens[qProducerOffset]->operand()
		                             ? loop->bodyTokens[qProducerOffset]->operand()->latency()
		                             : 0;
		opportunity.qProducerConsumerGapCycles = countEmittableTokens( *loop,
		                                                                qProducerOffset + 1,
		                                                                primaryQConsumerOffsets.front() );
		opportunity.sourcePrefixCycles = countEmittableTokens( *loop, 0, qProducerOffset );
		opportunity.sourceSuffixCycles = countEmittableTokens( *loop,
		                                                       primaryQConsumerOffsets.front() + 1,
		                                                       static_cast<unsigned int>( loop->bodyTokens.size() - 1 ) );
		opportunity.branchDelaySlots = branchDelaySlots;
		opportunity.loopCsClid = loop->loopCsClid;
		opportunity.loopCsMlid = loop->loopCsMlid;
		opportunity.qProducerConsumerGapDeficitCycles =
			opportunity.qProducerLatency > opportunity.qProducerConsumerGapCycles
			? opportunity.qProducerLatency - opportunity.qProducerConsumerGapCycles
			: 0;
		opportunity.loopCarriedQGapCycles =
			opportunity.sourceSuffixCycles + opportunity.branchDelaySlots + opportunity.sourcePrefixCycles;
		opportunity.qProducerInsertionGapCycles =
			countEmittableTokens( *loop,
			                      primaryQConsumerOffsets.back() + 1,
			                      static_cast<unsigned int>( loop->bodyTokens.size() - 1 ) )
		    + opportunity.branchDelaySlots
		    + opportunity.sourcePrefixCycles;
		opportunity.qProducerInsertionGapDeficitCycles =
			opportunity.qProducerLatency > opportunity.qProducerInsertionGapCycles
			? opportunity.qProducerLatency - opportunity.qProducerInsertionGapCycles
			: 0;
		opportunity.qSchedulingStrategy = classifyLoopQSchedulingStrategy( opportunity );
		opportunity.simpleCountedLoop = loop->simpleCountedLoop;
		opportunity.hasSingleQProducer = qProducerCount == 1;
		opportunity.requiresPrologEpilog = loop->simpleCountedLoop && qProducerCount == 1;
		opportunity.memoryLoadCount = loop->memoryLoadCount;
		opportunity.memoryStoreCount = loop->memoryStoreCount;
		opportunity.hasMemoryPreOrPostIncrement = loop->hasMemoryPreOrPostIncrement;
		opportunity.hasXgkick = loop->hasXgkick;
		opportunity.inductionRegisters = loop->inductionRegisters;
		opportunity.inductionUpdates = loop->inductionUpdates;
		opportunity.loopReadWriteRegisters = loop->loopReadWriteRegisters;

		for( std::vector<unsigned int>::const_iterator q = allQConsumerOffsets.begin(); q != allQConsumerOffsets.end(); ++q )
		{
			opportunity.qConsumerTokenIndices.push_back( loop->firstBodyTokenIndex + *q );
		}

		for( std::vector<VuLoopQStage>::const_iterator stage = opportunity.qStages.begin();
		     stage != opportunity.qStages.end(); ++stage )
		{
			for( std::vector<unsigned int>::const_iterator q = stage->qConsumerTokenIndices.begin();
			     q != stage->qConsumerTokenIndices.end(); ++q )
			{
				if( *q >= loop->firstBodyTokenIndex )
				{
					const unsigned int qOffset = *q - loop->firstBodyTokenIndex;
					collectLoopCarriedQInputs( *loop, qOffset, opportunity.carriedQInputRegisters );
					collectLoopCarriedQOutputs( *loop, qOffset, opportunity.carriedQOutputRegisters );
				}
			}
		}

		opportunity.requiresLoopCarriedRegisters = !opportunity.carriedQInputRegisters.empty()
		                                        || !opportunity.carriedQOutputRegisters.empty();
		opportunity.eligibleSingleQSoftwarePipeline = opportunity.simpleCountedLoop
		                                           && opportunity.hasSingleQProducer
		                                           && opportunity.branchDelaySlots > 0
		                                           && (opportunity.sourcePrefixCycles + opportunity.sourceSuffixCycles) >= opportunity.qProducerLatency;

		if( opportunity.eligibleSingleQSoftwarePipeline )
		{
			const unsigned int firstConsumerOffset = primaryQConsumerOffsets.front();
			const unsigned int branchOffset = loop->branchTokenIndex - loop->firstBodyTokenIndex;
			opportunity.hasSoftwarePipelinePlan = true;
			appendPipelineInstructionIndices( *loop, 0, firstConsumerOffset, opportunity.prologTokenIndices );
			appendPipelineInstructionIndices( *loop, firstConsumerOffset, branchOffset + 1, opportunity.mainTokenIndices );
			appendPipelineInstructionIndices( *loop, firstConsumerOffset, branchOffset, opportunity.drainTokenIndices );
			collectSoftwarePipelineSuffixStoreDescriptors( opportunity.drainTokenIndices,
			                                               indexedTokens,
			                                               opportunity );
		}

		if( qProducerCount > 1
		    && !opportunity.qStages.empty()
		    && !opportunity.qStages.front().qConsumerTokenIndices.empty() )
		{
			const unsigned int branchOffset = loop->branchTokenIndex - loop->firstBodyTokenIndex;
			const VuLoopQStage& firstStage = opportunity.qStages.front();
			const unsigned int firstProducerOffset =
			    firstStage.qProducerTokenIndex - loop->firstBodyTokenIndex;
			const unsigned int firstConsumerOffset =
			    firstStage.qConsumerTokenIndices.front() - loop->firstBodyTokenIndex;
			bool foundSafeCyclicPrefix = false;
			unsigned int bestMainCycles = ~0u;
			VuLoopPipelineOpportunity bestOpportunity = opportunity;
			if( firstProducerOffset > 0
			    && firstProducerOffset < branchOffset
			    && countEmittableTokens( *loop, firstProducerOffset, branchOffset ) != 0 )
			{
				foundSafeCyclicPrefix =
				    considerMultiQPipelineCandidate( *loop,
				                                     indexedTokens,
				                                     opportunity,
				                                     firstProducerOffset,
				                                     firstProducerOffset,
				                                     branchOffset,
				                                     true,
				                                     bestMainCycles,
				                                     bestOpportunity )
				    || foundSafeCyclicPrefix;
			}
			if( firstConsumerOffset < branchOffset
			    && firstStage.qProducerInsertionGapDeficitCycles == 0
			    && countEmittableTokens( *loop, firstConsumerOffset, branchOffset ) != 0 )
			{
				foundSafeCyclicPrefix =
				    considerMultiQPipelineCandidate( *loop,
				                                     indexedTokens,
				                                     opportunity,
				                                     firstConsumerOffset,
				                                     firstConsumerOffset,
				                                     branchOffset,
				                                     true,
				                                     bestMainCycles,
				                                     bestOpportunity )
				    || foundSafeCyclicPrefix;
			}

			unsigned int cyclicPrefixLastConsumerOffset = 0;
			bool foundFallbackCyclicPrefix = false;
			for( std::vector<VuLoopQStage>::const_iterator stage = opportunity.qStages.begin();
			     stage != opportunity.qStages.end(); ++stage )
			{
				if( stage->qConsumerTokenIndices.empty() )
					continue;
				const unsigned int lastConsumerOffset =
				    stage->qConsumerTokenIndices.back() - loop->firstBodyTokenIndex;
				if( lastConsumerOffset >= branchOffset )
					continue;
				if( countEmittableTokens( *loop, lastConsumerOffset + 1, branchOffset ) == 0 )
					continue;
				cyclicPrefixLastConsumerOffset = lastConsumerOffset;
				foundFallbackCyclicPrefix = true;
				foundSafeCyclicPrefix =
				    considerMultiQPipelineCandidate( *loop,
				                                     indexedTokens,
				                                     opportunity,
				                                     lastConsumerOffset + 1,
				                                     lastConsumerOffset + 1,
				                                     branchOffset,
				                                     true,
				                                     bestMainCycles,
				                                     bestOpportunity )
				    || foundSafeCyclicPrefix;
			}
			if( loop->simpleCountedLoop
			    && !loop->hasXgkick
			    && !loop->hasMemoryPreOrPostIncrement
			    && loop->branchTokenIndex < indexedTokens.size()
			    && branchCanInvertToDrain( *indexedTokens[loop->branchTokenIndex] ) )
			{
				for( unsigned int offset = 1; offset < branchOffset; ++offset )
				{
					const unsigned int tokenIndex = loop->firstBodyTokenIndex + offset - 1;
					if( tokenIndex >= indexedTokens.size()
					    || !tokenHasGuardableMultiQCyclicPrefixSideEffect( *indexedTokens[tokenIndex] ) )
						continue;
					if( countEmittableTokens( *loop, offset, branchOffset ) == 0 )
						continue;
					foundSafeCyclicPrefix =
					    considerMultiQPipelineCandidate( *loop,
					                                     indexedTokens,
					                                     opportunity,
					                                     offset,
					                                     offset,
					                                     branchOffset,
					                                     true,
					                                     bestMainCycles,
					                                     bestOpportunity )
					    || foundSafeCyclicPrefix;
				}
			}
			if( foundSafeCyclicPrefix )
			{
				opportunity.multiQPrologTokenIndices = bestOpportunity.multiQPrologTokenIndices;
				opportunity.multiQMainTokenIndices = bestOpportunity.multiQMainTokenIndices;
				opportunity.multiQCyclicPrefixTokenIndices = bestOpportunity.multiQCyclicPrefixTokenIndices;
				opportunity.multiQCyclicPrefixRotations = bestOpportunity.multiQCyclicPrefixRotations;
				opportunity.multiQCyclicPrefixInsertBeforeTokenIndex =
				    bestOpportunity.multiQCyclicPrefixInsertBeforeTokenIndex;
				opportunity.multiQCyclicPrefixNeedsGuard =
				    bestOpportunity.multiQCyclicPrefixNeedsGuard;
				opportunity.multiQCyclicPrefixLastTokenInBranchDelaySlot =
				    bestOpportunity.multiQCyclicPrefixLastTokenInBranchDelaySlot;
				opportunity.drainTokenIndices = bestOpportunity.drainTokenIndices;
			}
			else if( foundFallbackCyclicPrefix )
			{
				assignMultiQPipelineCandidate( *loop,
				                               cyclicPrefixLastConsumerOffset + 1,
				                               cyclicPrefixLastConsumerOffset + 1,
				                               branchOffset,
				                               opportunity );
			}
		}

		classifySoftwarePipelineEmissionSafety( opportunity, *loop, qProducerOffset, indexedTokens );
		classifyMultiQSoftwarePipelineOpportunity( opportunity, qProducerCount, indexedTokens );
		if( opportunity.canEmitMultiQSoftwarePipeline
		    && !opportunity.multiQMainTokenIndices.empty()
		    && !opportunity.multiQCyclicPrefixTokenIndices.empty()
		    && !opportunity.multiQCyclicPrefixNeedsGuard
		    && !loopUsesLoopExtraDirective( *loop, indexedTokens ) )
			classifyMultiQCyclicPrefixSuffixStoreDrains( opportunity,
			                                             *loop,
			                                             indexedTokens );

		if( std::getenv( "OPENVCL_DUMP_PIPELINE_OPPORTUNITIES" ) != NULL )
		{
			std::cerr << "[pipeline-opportunity] loop=" << opportunity.label
			          << " qProducerCount=" << qProducerCount
			          << " qProducerLatency=" << opportunity.qProducerLatency
			          << " gapCycles=" << opportunity.qProducerConsumerGapCycles
			          << " gapDeficit=" << opportunity.qProducerConsumerGapDeficitCycles
			          << " loopCarriedQGap=" << opportunity.loopCarriedQGapCycles
			          << " producerInsertionGap=" << opportunity.qProducerInsertionGapCycles
			          << " producerInsertionDeficit=" << opportunity.qProducerInsertionGapDeficitCycles
			          << " sourcePrefix=" << opportunity.sourcePrefixCycles
			          << " sourceSuffix=" << opportunity.sourceSuffixCycles
			          << " branchDelaySlots=" << opportunity.branchDelaySlots
			          << " simpleCounted=" << ( opportunity.simpleCountedLoop ? 1 : 0 )
			          << " requiresLoopCarried=" << ( opportunity.requiresLoopCarriedRegisters ? 1 : 0 )
			          << " eligibleSingleQ=" << ( opportunity.eligibleSingleQSoftwarePipeline ? 1 : 0 )
			          << " hasSwpPlan=" << ( opportunity.hasSoftwarePipelinePlan ? 1 : 0 )
			          << " canEmitMultiQ=" << ( opportunity.canEmitMultiQSoftwarePipeline ? 1 : 0 )
			          << " multiQNeedsGuard=" << ( opportunity.multiQCyclicPrefixNeedsGuard ? 1 : 0 )
			          << " prologSize=" << opportunity.prologTokenIndices.size()
			          << " mainSize=" << opportunity.mainTokenIndices.size()
			          << " drainSize=" << opportunity.drainTokenIndices.size()
			          << " multiQPrologSize=" << opportunity.multiQPrologTokenIndices.size()
			          << " multiQMainSize=" << opportunity.multiQMainTokenIndices.size()
			          << " multiQCyclicPrefixSize=" << opportunity.multiQCyclicPrefixTokenIndices.size()
			          << "\n";
			if( !opportunity.mainTokenIndices.empty() )
			{
				const unsigned int singleQMainEstCycles =
				    scheduledLoopBodyCycles( opportunity.mainTokenIndices, indexedTokens );
				std::cerr << "[pipeline-opportunity]   singleQ_main_estimated_cycles="
				          << singleQMainEstCycles << "\n";
				if( !opportunity.prologTokenIndices.empty() )
				{
					const unsigned int singleQMainInContextCycles =
					    scheduledLoopBodyCyclesInContext( opportunity.prologTokenIndices,
					                                      opportunity.mainTokenIndices,
					                                      indexedTokens );
					std::cerr << "[pipeline-opportunity]   singleQ_main_in_context_cycles="
					          << singleQMainInContextCycles << "\n";
				}
			}
			if( !opportunity.multiQMainTokenIndices.empty() )
			{
				const unsigned int multiQMainEstCycles =
				    scheduledLoopBodyCycles( opportunity.multiQMainTokenIndices, indexedTokens );
				std::cerr << "[pipeline-opportunity]   multiQ_main_estimated_cycles="
				          << multiQMainEstCycles << "\n";
				if( !opportunity.multiQPrologTokenIndices.empty() )
				{
					const unsigned int multiQMainInContextCycles =
					    scheduledLoopBodyCyclesInContext( opportunity.multiQPrologTokenIndices,
					                                      opportunity.multiQMainTokenIndices,
					                                      indexedTokens );
					std::cerr << "[pipeline-opportunity]   multiQ_main_in_context_cycles="
					          << multiQMainInContextCycles << "\n";
				}
			}
		}

		if( std::getenv( "OPENVCL_DUMP_LOOP_DDG" ) != NULL
		    && opportunity.simpleCountedLoop
		    && !opportunity.mainTokenIndices.empty() )
		{
			// Track 9.G step 1: extract a dependence DAG over the simple-counted
			// loop body. Nodes = opportunity.mainTokenIndices. Edges encode
			// (kind, dist, latency, resource):
			//   kind  in {RAW, WAW, WAR}; intra-iter dist=0 (i<j), loop-carried dist=1 (i>j)
			//   latency for RAW comes from VuLatencyTracker; WAW/WAR use 1 (ordering only)
			//   resource = base register key (collapsed via registerBaseKey) or
			//              implicit pipe tag (@Q, @P, @ACC, @MAC, @CLIP, @R, @I).
			// Diagnostic-only; planner / emission untouched. Output is gated on
			// OPENVCL_DUMP_LOOP_DDG; per-edge detail additionally needs
			// OPENVCL_DUMP_LOOP_DDG_EDGES to avoid drowning the log on large bodies.
			const std::vector<unsigned int>& mt = opportunity.mainTokenIndices;
			const unsigned int n = static_cast<unsigned int>( mt.size() );
			std::vector< std::vector<std::string> > nodeWrites( n ), nodeReads( n );
			for( unsigned int k = 0; k < n; ++k )
			{
				if( mt[k] >= indexedTokens.size() ) continue;
				VuTokenResourceAccess acc;
				if( !buildVuTokenResourceAccess( *indexedTokens[mt[k]], acc ) ) continue;
				for( std::list<std::string>::const_iterator it = acc.registerWrites.begin();
				     it != acc.registerWrites.end(); ++it )
					nodeWrites[k].push_back( registerBaseKey( *it ) );
				for( std::list<std::string>::const_iterator it = acc.registerReads.begin();
				     it != acc.registerReads.end(); ++it )
					nodeReads[k].push_back( registerBaseKey( *it ) );
				const unsigned int iw = acc.implicitWrites;
				const unsigned int ir = acc.implicitReads;
				if( iw & VU_RESOURCE_ACC )  nodeWrites[k].push_back( "@ACC" );
				if( iw & VU_RESOURCE_Q )    nodeWrites[k].push_back( "@Q" );
				if( iw & VU_RESOURCE_P )    nodeWrites[k].push_back( "@P" );
				if( iw & VU_RESOURCE_R )    nodeWrites[k].push_back( "@R" );
				if( iw & VU_RESOURCE_I )    nodeWrites[k].push_back( "@I" );
				if( iw & VU_RESOURCE_MAC )  nodeWrites[k].push_back( "@MAC" );
				if( iw & VU_RESOURCE_CLIP ) nodeWrites[k].push_back( "@CLIP" );
				if( ir & VU_RESOURCE_ACC )  nodeReads[k].push_back( "@ACC" );
				if( ir & VU_RESOURCE_Q )    nodeReads[k].push_back( "@Q" );
				if( ir & VU_RESOURCE_P )    nodeReads[k].push_back( "@P" );
				if( ir & VU_RESOURCE_R )    nodeReads[k].push_back( "@R" );
				if( ir & VU_RESOURCE_I )    nodeReads[k].push_back( "@I" );
				if( ir & VU_RESOURCE_MAC )  nodeReads[k].push_back( "@MAC" );
				if( ir & VU_RESOURCE_CLIP ) nodeReads[k].push_back( "@CLIP" );
			}
			const bool dumpEdges = ( std::getenv( "OPENVCL_DUMP_LOOP_DDG_EDGES" ) != NULL );
			unsigned int edges = 0, intra = 0, carried = 0;
			unsigned int maxIntraLat = 0, maxCarriedLat = 0;
			// Track 9.G step 2: collect edges into parallel arrays so we can
			// compute RecMII (max cycle ratio of lat/dist) once enumeration is done.
			std::vector<unsigned int> eFrom, eTo, eDist;
			std::vector<int> eLat;
			for( unsigned int i = 0; i < n; ++i )
			{
				for( unsigned int j = 0; j < n; ++j )
				{
					if( i == j ) continue;
					const unsigned int dist = ( i < j ) ? 0u : 1u;
					std::string sharedRaw;
					for( unsigned int a = 0; a < nodeWrites[i].size() && sharedRaw.empty(); ++a )
						for( unsigned int b = 0; b < nodeReads[j].size() && sharedRaw.empty(); ++b )
							if( nodeWrites[i][a] == nodeReads[j][b] )
								sharedRaw = nodeWrites[i][a];
					if( !sharedRaw.empty()
					    && mt[i] < indexedTokens.size()
					    && mt[j] < indexedTokens.size() )
					{
						VuLatencyTracker tr;
						tr.reset();
						tr.recordWrites( *indexedTokens[mt[i]], 0 );
						const int d = tr.readHazardDelay( *indexedTokens[mt[j]], NULL, 0 );
						const unsigned int lat = static_cast<unsigned int>( d > 0 ? d : 1 );
						if( dumpEdges )
							std::cerr << "[loop-ddg-edge] loop=" << opportunity.label
							          << " i=" << mt[i] << " j=" << mt[j]
							          << " dist=" << dist
							          << " kind=RAW lat=" << lat
							          << " res=" << sharedRaw << "\n";
						++edges;
						if( dist == 0 ) { ++intra;   if( lat > maxIntraLat   ) maxIntraLat   = lat; }
						else            { ++carried; if( lat > maxCarriedLat ) maxCarriedLat = lat; }
						eFrom.push_back( i ); eTo.push_back( j ); eDist.push_back( dist );
						eLat.push_back( static_cast<int>( lat ) );
					}
					std::string sharedWaw;
					for( unsigned int a = 0; a < nodeWrites[i].size() && sharedWaw.empty(); ++a )
						for( unsigned int b = 0; b < nodeWrites[j].size() && sharedWaw.empty(); ++b )
							if( nodeWrites[i][a] == nodeWrites[j][b] )
								sharedWaw = nodeWrites[i][a];
					if( !sharedWaw.empty() )
					{
						if( dumpEdges )
							std::cerr << "[loop-ddg-edge] loop=" << opportunity.label
							          << " i=" << mt[i] << " j=" << mt[j]
							          << " dist=" << dist
							          << " kind=WAW lat=1"
							          << " res=" << sharedWaw << "\n";
						++edges;
						if( dist == 0 ) ++intra; else ++carried;
						eFrom.push_back( i ); eTo.push_back( j ); eDist.push_back( dist );
						eLat.push_back( 1 );
					}
					std::string sharedWar;
					for( unsigned int a = 0; a < nodeReads[i].size() && sharedWar.empty(); ++a )
						for( unsigned int b = 0; b < nodeWrites[j].size() && sharedWar.empty(); ++b )
							if( nodeReads[i][a] == nodeWrites[j][b] )
								sharedWar = nodeReads[i][a];
					if( !sharedWar.empty() )
					{
						if( dumpEdges )
							std::cerr << "[loop-ddg-edge] loop=" << opportunity.label
							          << " i=" << mt[i] << " j=" << mt[j]
							          << " dist=" << dist
							          << " kind=WAR lat=1"
							          << " res=" << sharedWar << "\n";
						++edges;
						if( dist == 0 ) ++intra; else ++carried;
						eFrom.push_back( i ); eTo.push_back( j ); eDist.push_back( dist );
						eLat.push_back( 1 );
					}
				}
			}
			std::cerr << "[loop-ddg] loop=" << opportunity.label
			          << " mainSize=" << n
			          << " edges=" << edges
			          << " intra=" << intra
			          << " carried=" << carried
			          << " maxIntraLat=" << maxIntraLat
			          << " maxCarriedLat=" << maxCarriedLat
			          << "\n";

			// Track 9.G step 2: RecMII = max over cycles C of sum(lat)/sum(dist).
			// dist in {0,1}; only cycles with at least one dist=1 edge are finite.
			// Solve via binary search on lambda: a positive cycle in the reweighted
			// graph w(e) = lat(e) - lambda*dist(e) exists iff max cycle ratio > lambda.
			// Use Bellman-Ford longest-path detection (init d[v]=0 for all v, relax
			// n times, then test for one more relaxation).
			if( n > 0 && carried > 0 && !eFrom.empty() )
			{
				double hi = 0.0;
				for( unsigned int e = 0; e < eLat.size(); ++e ) hi += (double)eLat[e];
				if( hi < 1.0 ) hi = 1.0;
				double lo = 0.0;
				const unsigned int E = static_cast<unsigned int>( eFrom.size() );
				std::vector<double> d( n, 0.0 );
				for( int iter = 0; iter < 60; ++iter )
				{
					const double mid = 0.5 * ( lo + hi );
					for( unsigned int v = 0; v < n; ++v ) d[v] = 0.0;
					// Relax n times.
					for( unsigned int pass = 0; pass < n; ++pass )
					{
						bool changed = false;
						for( unsigned int e = 0; e < E; ++e )
						{
							const double w = (double)eLat[e] - mid * (double)eDist[e];
							const double nd = d[ eFrom[e] ] + w;
							if( nd > d[ eTo[e] ] + 1e-12 )
							{
								d[ eTo[e] ] = nd;
								changed = true;
							}
						}
						if( !changed ) break;
					}
					// One more pass: if anything still relaxes, positive cycle exists.
					bool positive = false;
					for( unsigned int e = 0; e < E && !positive; ++e )
					{
						const double w = (double)eLat[e] - mid * (double)eDist[e];
						if( d[ eFrom[e] ] + w > d[ eTo[e] ] + 1e-9 )
							positive = true;
					}
					if( positive ) lo = mid;
					else           hi = mid;
				}
				const double recmiiFract = lo;
				unsigned int recmiiInt = static_cast<unsigned int>( recmiiFract );
				if( (double)recmiiInt + 1e-6 < recmiiFract ) ++recmiiInt;
				if( recmiiInt < 1 ) recmiiInt = 1;
				std::cerr << "[loop-recmii] loop=" << opportunity.label
				          << " recmii_int=" << recmiiInt
				          << " recmii_fract=" << recmiiFract
				          << "\n";
			}
			else
			{
				std::cerr << "[loop-recmii] loop=" << opportunity.label
				          << " recmii_int=1 recmii_fract=0 (no carried edges)\n";
			}
		}

		if( std::getenv( "OPENVCL_DUMP_LOOP_RESMII" ) != NULL
		    && opportunity.simpleCountedLoop
		    && !opportunity.mainTokenIndices.empty() )
		{
			// Track 9.G step 3: resource MII (ResMII) per VU execution pipe.
			// Pipeline model (PS2 VU first-order):
			//   * 1 UPPER issue / cycle  — VU_PIPE_UPPER (FMAC ops)
			//   * 1 LOWER issue / cycle  — VU_PIPE_LOWER (LSU/IALU/BRU/RANDU
			//                              plus FDIV/EFU which dispatch from
			//                              the lower slot)
			//   * FDIV unit non-pipelined: each FDIV op occupies it for
			//                              info->throughput cycles
			//   * EFU  unit non-pipelined: same model, info->throughput cycles
			// NOPs and waitq/waitp are excluded from issue counts.
			//
			// ResMII = max( nUpper, nLower, sum_FDIV(throughput),
			//               sum_EFU(throughput) )
			// MII   = max( RecMII, ResMII ); the per-iter cycle count of any
			//         valid modulo schedule.
			const std::vector<unsigned int>& mt = opportunity.mainTokenIndices;
			unsigned int nUpper = 0, nLower = 0, nNop = 0;
			unsigned int fdivBusy = 0, efuBusy = 0;
			unsigned int nFmac = 0, nLsu = 0, nIalu = 0, nBru = 0;
			unsigned int nFdiv = 0, nEfu = 0, nRandu = 0;
			for( unsigned int k = 0; k < mt.size(); ++k )
			{
				if( mt[k] >= indexedTokens.size() ) continue;
				const Token& tk = *indexedTokens[mt[k]];
				if( !tk.operand() ) continue;
				const VuInstructionInfo* info =
				    findVuInstructionInfo( normalizeVuMnemonic( tk.operand()->name() ) );
				if( !info ) continue;
				if( info->pipe == VU_PIPE_NOP ) { ++nNop; continue; }
				if( info->pipe == VU_PIPE_UPPER ) ++nUpper;
				else if( info->pipe == VU_PIPE_LOWER ) ++nLower;
				switch( info->unit )
				{
				case VU_EXEC_FMAC:  ++nFmac;  break;
				case VU_EXEC_LSU:   ++nLsu;   break;
				case VU_EXEC_IALU:  ++nIalu;  break;
				case VU_EXEC_BRU:   ++nBru;   break;
				case VU_EXEC_RANDU: ++nRandu; break;
				case VU_EXEC_FDIV:  ++nFdiv; fdivBusy += info->throughput; break;
				case VU_EXEC_EFU:   ++nEfu;  efuBusy  += info->throughput; break;
				default: break;
				}
			}
			unsigned int resmii = nUpper;
			if( nLower    > resmii ) resmii = nLower;
			if( fdivBusy  > resmii ) resmii = fdivBusy;
			if( efuBusy   > resmii ) resmii = efuBusy;
			if( resmii < 1 ) resmii = 1;
			std::cerr << "[loop-resmii] loop=" << opportunity.label
			          << " mainSize=" << mt.size()
			          << " nUpper=" << nUpper
			          << " nLower=" << nLower
			          << " fdivBusy=" << fdivBusy
			          << " efuBusy=" << efuBusy
			          << " nop=" << nNop
			          << " fmac=" << nFmac
			          << " lsu=" << nLsu
			          << " ialu=" << nIalu
			          << " bru=" << nBru
			          << " randu=" << nRandu
			          << " fdiv=" << nFdiv
			          << " efu=" << nEfu
			          << " resmii=" << resmii
			          << "\n";
		}

		if( std::getenv( "OPENVCL_DUMP_LOOP_MII" ) != NULL
		    && opportunity.simpleCountedLoop
		    && !opportunity.mainTokenIndices.empty() )
		{
			// Track 9.G step 4a: combined Minimum Initiation Interval.
			// MII = max(RecMII, ResMII) is the lower bound on any valid modulo
			// schedule's II. RecMII (recurrence-bound) is computed via the same
			// Bellman-Ford max-cycle-ratio algorithm used by OPENVCL_DUMP_LOOP_DDG;
			// ResMII (resource-bound) is the per-pipe count used by
			// OPENVCL_DUMP_LOOP_RESMII. Step 4b+ will consume this MII as the
			// starting II for iterative modulo scheduling.
			const std::vector<unsigned int>& mt = opportunity.mainTokenIndices;
			const unsigned int recmii = computeLoopRecMII( mt, indexedTokens );
			const unsigned int resmii = computeLoopResMII( mt, indexedTokens );
			const unsigned int mii    = ( recmii > resmii ) ? recmii : resmii;
			std::cerr << "[loop-mii] loop=" << opportunity.label
			          << " mainSize=" << mt.size()
			          << " recmii=" << recmii
			          << " resmii=" << resmii
			          << " mii=" << mii
			          << "\n";
		}

		if( std::getenv( "OPENVCL_DUMP_LOOP_RENAME_RECMII" ) != NULL
		    && opportunity.simpleCountedLoop
		    && !opportunity.mainTokenIndices.empty() )
		{
			// Track 9.G step 1c-1 (diagnostic): rename-aware RecMII.
			// Prints baseline RecMII alongside the rename-aware variant
			// that drops loop-carried RAW edges whose producer is a
			// kernel-rename splittable FMAC writing a VF base. The
			// rename machinery (steps 8b-1 / 8b-2x) already redirects
			// those carried values to scratch registers, so the
			// corresponding recurrences are not actually iter-to-iter.
			// Step 1c-1 only reports; step 1c-2 will consume this value
			// when OPENVCL_USE_GENERIC_KERNEL_REWRITE is set.
			const std::vector<unsigned int>& mt = opportunity.mainTokenIndices;
			const unsigned int recmiiBase = computeLoopRecMII( mt, indexedTokens );
			const unsigned int resmii     = computeLoopResMII( mt, indexedTokens );
			unsigned int droppedCarried   = 0;
			const unsigned int recmiiRenamed =
			    computeLoopRecMIIRenamed( mt, indexedTokens, &droppedCarried );
			const unsigned int miiBase =
			    ( recmiiBase    > resmii ) ? recmiiBase    : resmii;
			const unsigned int miiRenamed =
			    ( recmiiRenamed > resmii ) ? recmiiRenamed : resmii;
			std::cerr << "[loop-recmii-renamed] loop=" << opportunity.label
			          << " mainSize=" << mt.size()
			          << " recmii_base=" << recmiiBase
			          << " recmii_renamed=" << recmiiRenamed
			          << " resmii=" << resmii
			          << " mii_base=" << miiBase
			          << " mii_renamed=" << miiRenamed
			          << " droppedCarriedEdges=" << droppedCarried
			          << "\n";
		}

		if( std::getenv( "OPENVCL_DUMP_LOOP_MRT" ) != NULL
		    && opportunity.simpleCountedLoop
		    && !opportunity.mainTokenIndices.empty() )
		{
			// Track 9.G step 4b: Modulo Reservation Table scaffolding.
			// Construct an empty MRT sized at the current MII and report its
			// dimensions. Step 4d will drive reservations from the priority
			// list; for now this only validates that the resource model
			// (Upper/Lower issue lanes + FDIV/EFU multi-cycle pipes) wires
			// through the build and can be sized per loop without affecting
			// emission.
			const std::vector<unsigned int>& mt = opportunity.mainTokenIndices;
			const unsigned int recmii = computeLoopRecMII( mt, indexedTokens );
			const unsigned int resmii = computeLoopResMII( mt, indexedTokens );
			const unsigned int mii    = ( recmii > resmii ) ? recmii : resmii;
			ModuloReservationTable mrt( mii );
			std::cerr << "[loop-mrt] loop=" << opportunity.label
			          << " II=" << mrt.initiationInterval()
			          << " upperCap=" << mrt.initiationInterval()
			          << " lowerCap=" << mrt.initiationInterval()
			          << " fdivLanes=1"
			          << " efuLanes=1"
			          << " upperOcc=" << mrt.upperOccupancy()
			          << " lowerOcc=" << mrt.lowerOccupancy()
			          << " fdivOcc=" << mrt.fdivOccupancy()
			          << " efuOcc=" << mrt.efuOccupancy()
			          << "\n";
		}

		if( std::getenv( "OPENVCL_DUMP_LOOP_PRIORITY" ) != NULL
		    && opportunity.simpleCountedLoop
		    && !opportunity.mainTokenIndices.empty() )
		{
			// Track 9.G step 4c: node priority for iterative modulo scheduling.
			// Compute ASAP/ALAP/height/mobility over the intra DDG and print
			// an aggregate summary plus the priority order. Per-node detail
			// behind OPENVCL_DUMP_LOOP_PRIORITY_NODES to avoid flooding the
			// log on large bodies. Step 4d will consume this ordering to
			// drive insertion into the Modulo Reservation Table.
			const std::vector<unsigned int>& mt = opportunity.mainTokenIndices;
			const unsigned int recmii = computeLoopRecMII( mt, indexedTokens );
			const unsigned int resmii = computeLoopResMII( mt, indexedTokens );
			const unsigned int mii    = ( recmii > resmii ) ? recmii : resmii;
			LoopPriorityResult pr;
			computeLoopPriority( mt, indexedTokens, mii, pr );
			unsigned int maxHeight = 0, maxMobility = 0, maxAsap = 0, maxAlap = 0;
			for( unsigned int i = 0; i < pr.height.size(); ++i )
			{
				if( pr.height[i]   > maxHeight )   maxHeight   = pr.height[i];
				if( pr.mobility[i] > maxMobility ) maxMobility = pr.mobility[i];
				if( pr.asap[i]     > maxAsap )     maxAsap     = pr.asap[i];
				if( pr.alap[i]     > maxAlap )     maxAlap     = pr.alap[i];
			}
			std::cerr << "[loop-priority] loop=" << opportunity.label
			          << " mainSize=" << mt.size()
			          << " II=" << mii
			          << " scheduleLength=" << pr.scheduleLength
			          << " maxHeight=" << maxHeight
			          << " maxMobility=" << maxMobility
			          << " maxAsap=" << maxAsap
			          << " maxAlap=" << maxAlap
			          << "\n";
			if( std::getenv( "OPENVCL_DUMP_LOOP_PRIORITY_NODES" ) != NULL )
			{
				for( unsigned int rank = 0; rank < pr.order.size(); ++rank )
				{
					const unsigned int i = pr.order[rank];
					std::cerr << "[loop-priority-node] loop=" << opportunity.label
					          << " rank=" << rank
					          << " node=" << i
					          << " token=" << mt[i]
					          << " height=" << pr.height[i]
					          << " mobility=" << pr.mobility[i]
					          << " asap=" << pr.asap[i]
					          << " alap=" << pr.alap[i]
					          << "\n";
				}
			}
		}

		// Track 9.G step 8a: the modulo placer + 6a-6h kernel-rewrite
		// scaffolding always runs when the loop is a simple counted loop
		// with a non-empty body. The OPENVCL_DUMP_LOOP_SCHEDULE env var
		// (and the nested OPENVCL_DUMP_KERNEL_* gates) now only control
		// the std::cerr writes; the computation itself is unconditional
		// so OPENVCL_USE_GENERIC_KERNEL_REWRITE can see real scaffolding
		// on production builds.
		if( opportunity.simpleCountedLoop
		    && !opportunity.mainTokenIndices.empty() )
		{
			runVuKernelPlacerAndScaffolding( opportunity, indexedTokens, tokens );
			runExpandedDDGRefitDiagnostic( opportunity, indexedTokens, tokens );
		}

		if( std::getenv( "OPENVCL_DUMP_MULTISTAGE_CANDIDATES" ) != NULL )
		{
			std::cerr << "[multistage] loop=" << opportunity.label
			          << " stageCount=" << opportunity.stageCount
			          << " kernelTokens=" << opportunity.kernelTokenIndices.size()
			          << " tokenStageOffsets=" << opportunity.tokenStageOffsets.size()
			          << " rotations=" << opportunity.stageRotationRegisters.size()
			          << "\n";

			// Track 9.E step 2 (cost-only): synthesize a 2-stage kernel candidate
			// and report per-iter cost vs the 1-stage main body. Planner still
			// picks the 1-stage plan; this number informs design of step 3+4.
			if( opportunity.eligibleSingleQSoftwarePipeline )
			{
				const unsigned int singleStageMainCycles =
				    scheduledLoopBodyCycles( opportunity.mainTokenIndices, indexedTokens );
				unsigned int kernelCycles = 0;
				std::vector<VuSoftwarePipelineRotation> rotations;
				const bool ok = synthesizeTwoStageKernelCycles( *loop,
				                                                indexedTokens,
				                                                opportunity,
				                                                kernelCycles,
				                                                rotations );
				std::cerr << "[multistage-2stage] loop=" << opportunity.label
				          << " singleStageMainCycles=" << singleStageMainCycles
				          << " twoStage=" << ( ok ? "ok" : "fail" )
				          << " kernelCycles=" << kernelCycles
				          << " perIter=" << ( ok ? ( kernelCycles / 2.0 ) : 0.0 )
				          << " rotations=" << rotations.size();
				for( std::vector<VuSoftwarePipelineRotation>::const_iterator r =
				         rotations.begin(); r != rotations.end(); ++r )
				{
					std::cerr << " " << r->registerBase << "->" << r->scratchRegister;
				}
				std::cerr << "\n";

				// Track 9.E step 3c follow-up (diagnostic only): the existing
				// 2-stage synthesis above is main1 ++ main2_renamed. The cyclic
				// prefix (prolog: mul_pt_mat_44 + div q) is missing from the
				// kernel, so iter2's mulq has no fresh div upstream and the
				// scheduler has nothing new to overlap. Extend the kernel to:
				//   main_iter1 ++ prolog_iter2_renamed ++ main_iter2_renamed
				// This is the smallest shape that gives the scheduler a window
				// to overlap iter N+1's mul_pt_mat_44 + div under iter N's
				// mulq + ftoi4 tail. perIter = kernelCycles / 2.
				if( ok && !opportunity.prologTokenIndices.empty() )
				{
					std::list<Token> extKernelTokens;
					// iter1 main (no rename)
					for( std::vector<unsigned int>::const_iterator i =
					         opportunity.mainTokenIndices.begin();
					     i != opportunity.mainTokenIndices.end(); ++i )
					{
						if( *i < indexedTokens.size() )
							appendUnlabeledTokenForScheduleCost(
							    extKernelTokens, *indexedTokens[*i] );
					}
					// iter2 prolog (renamed via rotation map)
					for( std::vector<unsigned int>::const_iterator i =
					         opportunity.prologTokenIndices.begin();
					     i != opportunity.prologTokenIndices.end(); ++i )
					{
						if( *i < indexedTokens.size() )
							extKernelTokens.push_back(
							    adjustedMultiQCyclicPrefixToken(
							        *indexedTokens[*i], rotations ) );
					}
					// iter2 main (renamed via rotation map)
					for( std::vector<unsigned int>::const_iterator i =
					         opportunity.mainTokenIndices.begin();
					     i != opportunity.mainTokenIndices.end(); ++i )
					{
						if( *i < indexedTokens.size() )
							extKernelTokens.push_back(
							    adjustedMultiQCyclicPrefixToken(
							        *indexedTokens[*i], rotations ) );
					}
					const unsigned int extKernelCycles =
					    scheduleVuProgramReadyIssueSlotsWithFlagLiveness(
					        extKernelTokens ).cycleCount;
					const unsigned int prologAlone =
					    scheduledLoopBodyCycles( opportunity.prologTokenIndices,
					                             indexedTokens );
					// Naive serial: main + prolog + main = singleStage*2 + prolog
					const unsigned int serial =
					    singleStageMainCycles * 2 + prologAlone;
					const unsigned int savings =
					    serial > extKernelCycles ? serial - extKernelCycles : 0;
					std::cerr << "[multistage-2stage-cpfx] loop="
					          << opportunity.label
					          << " singleStageMain=" << singleStageMainCycles
					          << " prologAlone=" << prologAlone
					          << " extKernel=" << extKernelCycles
					          << " perIter=" << ( extKernelCycles / 2.0 )
					          << " serial=" << serial
					          << " savingsVsSerial=" << savings
					          << "\n";
				}
			}
		}

		if( std::getenv( "OPENVCL_DUMP_ROTATION_ALLOCATIONS" ) != NULL )
		{
			if( opportunity.eligibleSingleQSoftwarePipeline
			    && !opportunity.carriedQOutputRegisters.empty() )
			{
				std::vector<VuSoftwarePipelineRotation> bankProbe;
				for( std::list<std::string>::const_iterator reg =
				         opportunity.carriedQOutputRegisters.begin();
				     reg != opportunity.carriedQOutputRegisters.end(); ++reg )
				{
					VuSoftwarePipelineRotation rot;
					rot.registerBase = *reg;
					rot.hasScratchRegister = false;
					bankProbe.push_back( rot );
				}
				assignRotationScratchRegisters( *loop, bankProbe, 2 );
				unsigned int fullDepth2 = 0;
				for( std::vector<VuSoftwarePipelineRotation>::const_iterator r =
				         bankProbe.begin(); r != bankProbe.end(); ++r )
				{
					if( r->rotationBank.size() >= 2 )
						++fullDepth2;
				}
				std::cerr << "[rotation-alloc] loop=" << opportunity.label
				          << " carried=" << opportunity.carriedQOutputRegisters.size()
				          << " depth2_full=" << fullDepth2;
				for( std::vector<VuSoftwarePipelineRotation>::const_iterator r =
				         bankProbe.begin(); r != bankProbe.end(); ++r )
				{
					std::cerr << " " << r->registerBase << "->[";
					for( std::vector<std::string>::const_iterator b = r->rotationBank.begin();
					     b != r->rotationBank.end(); ++b )
					{
						if( b != r->rotationBank.begin() )
							std::cerr << ",";
						std::cerr << *b;
					}
					std::cerr << "]";
				}
				std::cerr << "\n";
			}
		}

		if( std::getenv( "OPENVCL_DUMP_QCHAIN_INTERLEAVE" ) != NULL )
		{
			// Track 9.E step 3b (cost-only): interleave stage1 of iter N+1
			// before stage2 of iter N at the source level so the scheduler can
			// overlap the next div/mulax chain with the current mulq/ftoi4
			// chain. Reports per-iter cycle savings vs the 1-stage main body.
			// Planner unchanged.
			if( opportunity.eligibleSingleQSoftwarePipeline
			    && !opportunity.mainTokenIndices.empty() )
			{
				const unsigned int singleStageMainCycles =
				    scheduledLoopBodyCycles( opportunity.mainTokenIndices, indexedTokens );
				unsigned int perIter = 0;
				unsigned int s1 = 0;
				unsigned int s2 = 0;
				std::vector<VuSoftwarePipelineRotation> rotations;
				std::string reason;
				const bool ok = synthesizeQInterleavedKernelCycles( *loop,
				                                                    indexedTokens,
				                                                    opportunity,
				                                                    perIter,
				                                                    s1,
				                                                    s2,
				                                                    rotations,
				                                                    reason );
				std::cerr << "[qchain-interleave] loop=" << opportunity.label
				          << " singleStageMainCycles=" << singleStageMainCycles
				          << " interleave=" << ( ok ? "ok" : "fail" )
				          << " reason=" << ( ok ? "-" : reason )
				          << " carriedQOutputs=" << opportunity.carriedQOutputRegisters.size()
				          << " qProducerTokenIdx=" << opportunity.qProducerTokenIndex
				          << " qConsumers=" << opportunity.qConsumerTokenIndices.size()
				          << " mainSize=" << opportunity.mainTokenIndices.size()
				          << " stage1Cycles=" << s1
				          << " stage2Cycles=" << s2
				          << " steadyPerIter=" << perIter
				          << " savingsVsSingleStage=" << ( ok && perIter < singleStageMainCycles
				                                            ? ( singleStageMainCycles - perIter )
				                                            : 0 )
				          << " rotations=" << rotations.size();
				for( std::vector<VuSoftwarePipelineRotation>::const_iterator r =
				         rotations.begin(); r != rotations.end(); ++r )
				{
					std::cerr << " " << r->registerBase << "->[";
					for( std::vector<std::string>::const_iterator b = r->rotationBank.begin();
					     b != r->rotationBank.end(); ++b )
					{
						if( b != r->rotationBank.begin() )
							std::cerr << ",";
						std::cerr << *b;
					}
					std::cerr << "]";
				}
				std::cerr << "\n";
			}
		}

		// Track 9.E step 3c (diagnostic only): identify Chain A (Q-consumer
		// dataflow, forward closure from mulq writes) and Chain B
		// (Q-producer dataflow, backward closure from div q reads) within
		// mainTokenIndices. Reports sizes, overlap and neutral tokens so we
		// can verify the SCEI-style overlap hypothesis on all eligible
		// shaders before designing a chain-aware planner.
		if( std::getenv( "OPENVCL_DUMP_QCHAIN_CHAINS" ) != NULL )
		{
			if( opportunity.eligibleSingleQSoftwarePipeline
			    && !opportunity.mainTokenIndices.empty()
			    && opportunity.qProducerTokenIndex < indexedTokens.size() )
			{
				// Build "in main" set for fast membership.
				std::list<unsigned int> mainSet;
				for( std::vector<unsigned int>::const_iterator m =
				         opportunity.mainTokenIndices.begin();
				     m != opportunity.mainTokenIndices.end(); ++m )
					mainSet.push_back( *m );

				// Chain B: backward closure from div q reads.
				std::list<std::string> chainBKeys;
				collectVuRegisterReadKeys(
				    *indexedTokens[opportunity.qProducerTokenIndex], chainBKeys );
				if( std::getenv( "OPENVCL_DUMP_QCHAIN_CHAINS_KEYS" ) != NULL )
				{
					std::cerr << "  qProducerReads=[";
					bool fk = true;
					for( std::list<std::string>::const_iterator k = chainBKeys.begin();
					     k != chainBKeys.end(); ++k )
					{
						if( !fk ) std::cerr << ",";
						std::cerr << *k;
						fk = false;
					}
					std::cerr << "]\n";
					for( std::vector<unsigned int>::const_iterator m =
					         opportunity.mainTokenIndices.begin();
					     m != opportunity.mainTokenIndices.end(); ++m )
					{
						std::list<std::string> w;
						collectVuRegisterWriteKeys( *indexedTokens[*m], w );
						std::cerr << "  main[" << *m << "]writes=[";
						bool first2 = true;
						for( std::list<std::string>::const_iterator k = w.begin();
						     k != w.end(); ++k )
						{
							if( !first2 ) std::cerr << ",";
							std::cerr << *k;
							first2 = false;
						}
						std::cerr << "]\n";
					}
				}
				std::list<unsigned int> chainB;
				bool changed = true;
				while( changed )
				{
					changed = false;
					for( std::vector<unsigned int>::const_reverse_iterator m =
					         opportunity.mainTokenIndices.rbegin();
					     m != opportunity.mainTokenIndices.rend(); ++m )
					{
						bool already = false;
						for( std::list<unsigned int>::const_iterator c = chainB.begin();
						     c != chainB.end(); ++c )
						{
							if( *c == *m ) { already = true; break; }
						}
						if( already )
							continue;
						std::list<std::string> writes;
						collectVuRegisterWriteKeys( *indexedTokens[*m], writes );
						if( !intersects( writes, chainBKeys ) )
							continue;
						chainB.push_back( *m );
						std::list<std::string> reads;
						collectVuRegisterReadKeys( *indexedTokens[*m], reads );
						for( std::list<std::string>::const_iterator r = reads.begin();
						     r != reads.end(); ++r )
						{
							if( !containsKey( chainBKeys, *r ) )
							{
								chainBKeys.push_back( *r );
								changed = true;
							}
						}
					}
				}

				// Chain A: forward closure from mulq consumer writes.
				std::list<std::string> chainAKeys;
				for( std::vector<unsigned int>::const_iterator q =
				         opportunity.qConsumerTokenIndices.begin();
				     q != opportunity.qConsumerTokenIndices.end(); ++q )
				{
					if( *q >= indexedTokens.size() )
						continue;
					std::list<std::string> writes;
					collectVuRegisterWriteKeys( *indexedTokens[*q], writes );
					for( std::list<std::string>::const_iterator w = writes.begin();
					     w != writes.end(); ++w )
						addUniqueString( chainAKeys, *w );
				}
				std::list<unsigned int> chainA;
				for( std::vector<unsigned int>::const_iterator q =
				         opportunity.qConsumerTokenIndices.begin();
				     q != opportunity.qConsumerTokenIndices.end(); ++q )
					chainA.push_back( *q );
				changed = true;
				while( changed )
				{
					changed = false;
					for( std::vector<unsigned int>::const_iterator m =
					         opportunity.mainTokenIndices.begin();
					     m != opportunity.mainTokenIndices.end(); ++m )
					{
						bool already = false;
						for( std::list<unsigned int>::const_iterator c = chainA.begin();
						     c != chainA.end(); ++c )
						{
							if( *c == *m ) { already = true; break; }
						}
						if( already )
							continue;
						std::list<std::string> reads;
						collectVuRegisterReadKeys( *indexedTokens[*m], reads );
						if( !intersects( reads, chainAKeys ) )
							continue;
						chainA.push_back( *m );
						std::list<std::string> writes;
						collectVuRegisterWriteKeys( *indexedTokens[*m], writes );
						for( std::list<std::string>::const_iterator w = writes.begin();
						     w != writes.end(); ++w )
						{
							if( !containsKey( chainAKeys, *w ) )
							{
								chainAKeys.push_back( *w );
								changed = true;
							}
						}
					}
				}

				// Overlap and neutral.
				unsigned int overlap = 0;
				for( std::list<unsigned int>::const_iterator a = chainA.begin();
				     a != chainA.end(); ++a )
				{
					for( std::list<unsigned int>::const_iterator b = chainB.begin();
					     b != chainB.end(); ++b )
					{
						if( *a == *b ) { ++overlap; break; }
					}
				}
				unsigned int neutral = 0;
				for( std::vector<unsigned int>::const_iterator m =
				         opportunity.mainTokenIndices.begin();
				     m != opportunity.mainTokenIndices.end(); ++m )
				{
					bool inA = false;
					for( std::list<unsigned int>::const_iterator a = chainA.begin();
					     a != chainA.end(); ++a )
					{
						if( *a == *m ) { inA = true; break; }
					}
					bool inB = false;
					for( std::list<unsigned int>::const_iterator b = chainB.begin();
					     b != chainB.end(); ++b )
					{
						if( *b == *m ) { inB = true; break; }
					}
					if( !inA && !inB )
						++neutral;
				}

				std::cerr << "[qchain-chains] loop=" << opportunity.label
				          << " mainSize=" << opportunity.mainTokenIndices.size()
				          << " chainA=" << chainA.size()
				          << " chainB=" << chainB.size()
				          << " overlap=" << overlap
				          << " neutral=" << neutral
				          << " qProducerIdx=" << opportunity.qProducerTokenIndex
				          << " qConsumers=" << opportunity.qConsumerTokenIndices.size();
				std::cerr << " chainA_idx=[";
				bool first = true;
				for( std::list<unsigned int>::const_iterator a = chainA.begin();
				     a != chainA.end(); ++a )
				{
					if( !first ) std::cerr << ",";
					std::cerr << *a;
					first = false;
				}
				std::cerr << "] chainB_idx=[";
				first = true;
				for( std::list<unsigned int>::const_iterator b = chainB.begin();
				     b != chainB.end(); ++b )
				{
					if( !first ) std::cerr << ",";
					std::cerr << *b;
					first = false;
				}
				std::cerr << "]\n";
			}
		}

		// Track 9.E step 3c follow-up (diagnostic only): the cyclic prefix
		// (prolog tokens) is emitted at end-of-main in the steady-state
		// body. Current codegen schedules main and the appended cyclic
		// prefix as two sequential blocks. SCEI's MAIN_LOOP interleaves
		// both. This diagnostic compares:
		//   alone       = schedule(main)                      [step 3b's baseline]
		//   prologAlone = schedule(prolog)
		//   serial      = schedule(main) + schedule(prolog)   [serial upper bound]
		//   inContext   = schedule(prolog; then main)         [scheduledLoopBodyCyclesInContext]
		//   combined    = schedule(main ++ prolog)            [SCEI-style single pass]
		// "savings" = serial - combined; that's how much an interleaved
		// scheduler over (main + cyclic prefix) could buy per iteration.
		if( std::getenv( "OPENVCL_DUMP_COMBINED_SCHEDULE" ) != NULL )
		{
			if( opportunity.eligibleSingleQSoftwarePipeline
			    && !opportunity.mainTokenIndices.empty() )
			{
				const unsigned int aloneCycles =
				    scheduledLoopBodyCycles( opportunity.mainTokenIndices, indexedTokens );
				const unsigned int prologAloneCycles =
				    scheduledLoopBodyCycles( opportunity.prologTokenIndices, indexedTokens );
				const unsigned int inContextCycles =
				    scheduledLoopBodyCyclesInContext( opportunity.prologTokenIndices,
				                                      opportunity.mainTokenIndices,
				                                      indexedTokens );
				// Combined: schedule main ++ prolog as ONE bodyTokens list.
				std::list<Token> combinedTokens;
				for( std::vector<unsigned int>::const_iterator i =
				         opportunity.mainTokenIndices.begin();
				     i != opportunity.mainTokenIndices.end(); ++i )
				{
					if( *i < indexedTokens.size() )
						appendUnlabeledTokenForScheduleCost(
						    combinedTokens, *indexedTokens[*i] );
				}
				for( std::vector<unsigned int>::const_iterator i =
				         opportunity.prologTokenIndices.begin();
				     i != opportunity.prologTokenIndices.end(); ++i )
				{
					if( *i < indexedTokens.size() )
						appendUnlabeledTokenForScheduleCost(
						    combinedTokens, *indexedTokens[*i] );
				}
				const unsigned int combinedCycles =
				    scheduleVuProgramReadyIssueSlotsWithFlagLiveness( combinedTokens ).cycleCount;
				const unsigned int serialCycles = aloneCycles + prologAloneCycles;
				const unsigned int savings = serialCycles > combinedCycles
				                                 ? serialCycles - combinedCycles
				                                 : 0;
				std::cerr << "[combined-schedule] loop=" << opportunity.label
				          << " mainAlone=" << aloneCycles
				          << " prologAlone=" << prologAloneCycles
				          << " serial=" << serialCycles
				          << " inContext=" << inContextCycles
				          << " combined=" << combinedCycles
				          << " savingsIfInterleaved=" << savings
				          << " mainSize=" << opportunity.mainTokenIndices.size()
				          << " prologSize=" << opportunity.prologTokenIndices.size()
				          << "\n";
			}
		}

		result.push_back( opportunity );
	}

	return result;
}

std::vector<VuSoftwarePipelineRewritePlan> buildVuSoftwarePipelineRewritePlans( const std::list<Token>& tokens )
{
	std::vector<VuSoftwarePipelineRewritePlan> plans;
	std::vector<const Token*> indexedTokens;
	for( std::list<Token>::const_iterator token = tokens.begin(); token != tokens.end(); ++token )
		indexedTokens.push_back( &*token );

	const std::vector<VuLoopPipelineOpportunity> opportunities = findVuLoopPipelineOpportunities( tokens );
	std::map<unsigned int, bool> plannedLabelIndices;
	for( std::vector<VuLoopPipelineOpportunity>::const_iterator i = opportunities.begin(); i != opportunities.end(); ++i )
	{
		if( !i->canEmitSoftwarePipeline
		    && !i->canEmitMultiQSoftwarePipeline )
			continue;

		VuSoftwarePipelineRewritePlan plan;
		plan.label = i->label;
		plan.prologLabel = i->label + "__PROLOG";
		plan.mainLabel = i->label;
		plan.drainLabel = i->label + "__DRAIN";
		plan.labelTokenIndex = i->labelTokenIndex;
		plan.branchTokenIndex = i->branchTokenIndex;
		plan.qProducerTokenIndex = i->qProducerTokenIndex;
		// Track 9.G step 6e: kernel-rewrite scaffolding (diagnostic-only).
		plan.kernelRewriteII           = i->kernelRewriteII;
		plan.kernelRewriteStageCount   = i->kernelRewriteStageCount;
		plan.kernelRewriteConflicts    = i->kernelRewriteConflicts;
		plan.kernelRewritePrologTokens = i->kernelRewritePrologTokens;
		plan.kernelRewriteMainTokens   = i->kernelRewriteMainTokens;
		plan.kernelRewriteDrainTokens  = i->kernelRewriteDrainTokens;
		plan.kernelRewriteEntryStages  = i->kernelRewriteEntryStages;
		// Track 9.G step 6f: register-plan + rename-hint scaffolding.
		plan.kernelRewriteRegCount    = i->kernelRewriteRegCount;
		plan.kernelRewriteWawCount    = i->kernelRewriteWawCount;
		plan.kernelRewriteRawCount    = i->kernelRewriteRawCount;
		plan.kernelRewriteWarCount    = i->kernelRewriteWarCount;
		plan.kernelRewriteHazards     = i->kernelRewriteHazards;
		plan.kernelRewriteRenameHints = i->kernelRewriteRenameHints;
		// Track 9.G step 8b-1: rename decisions.
		plan.kernelRewriteRenameDecisions = i->kernelRewriteRenameDecisions;
		// Track 9.G step 8b-2a: rename MOVE slots.
		plan.kernelRewriteRenameMoveSlots = i->kernelRewriteRenameMoveSlots;
		// Track 9.G step 6g: envelope scaffolding.
		plan.kernelEnvelopeKernelTokens       = i->kernelEnvelopeKernelTokens;
		plan.kernelEnvelopePrologueCycles     = i->kernelEnvelopePrologueCycles;
		plan.kernelEnvelopeEpilogueCycles     = i->kernelEnvelopeEpilogueCycles;
		plan.kernelEnvelopeConflicts          = i->kernelEnvelopeConflicts;
		plan.kernelEnvelopePrologueTokenCounts = i->kernelEnvelopePrologueTokenCounts;
		plan.kernelEnvelopeEpilogueTokenCounts = i->kernelEnvelopeEpilogueTokenCounts;
		// Track 9.G step 6h: stageCells VLIW grid.
		plan.kernelRewriteStageCells           = i->kernelRewriteStageCells;
		// Track 9.G-1h step 4b-3b-5: mirror refit scalar metrics so
		// emission-side consumers can see the shadow-placer verdict.
		plan.kernelRewriteRefitII             = i->kernelRewriteRefitII;
		plan.kernelRewriteRefitStageCount     = i->kernelRewriteRefitStageCount;
		plan.kernelRewriteRefitConflicts      = i->kernelRewriteRefitConflicts;
		plan.kernelRewriteRefitMainTokenCount = i->kernelRewriteRefitMainTokenCount;
		// Track 9.G-1h step 4b-7a: mirror the refit node vector + grid
		// so the emission-side bake-in builder (CodeGenerator) can
		// reconstruct the rewritten MAIN body without reaching back
		// into analysis state.
		plan.kernelRewriteRefitNodes          = i->kernelRewriteRefitNodes;
		plan.kernelRewriteRefitMainTokens     = i->kernelRewriteRefitMainTokens;

		if( i->canEmitMultiQSoftwarePipeline )
		{
			if( !i->qProducerTokenIndices.empty() )
				plan.qProducerTokenIndex = i->qProducerTokenIndices.front();
			plan.cyclicPrefixBeforeBranch = true;
			plan.prologTokenIndices = i->multiQPrologTokenIndices;
			plan.mainTokenIndices = i->multiQMainTokenIndices;
			plan.cyclicPrefixTokenIndices = i->multiQCyclicPrefixTokenIndices;
			plan.cyclicPrefixRotations = i->multiQCyclicPrefixRotations;
			plan.cyclicPrefixInsertBeforeTokenIndex =
			    i->multiQCyclicPrefixInsertBeforeTokenIndex;
			plan.cyclicPrefixNeedsGuard = i->multiQCyclicPrefixNeedsGuard;
			plan.cyclicPrefixLastTokenInBranchDelaySlot =
			    i->multiQCyclicPrefixLastTokenInBranchDelaySlot;
			plan.emitsDrain = i->multiQCyclicPrefixNeedsGuard;
			plan.drainsSuffixStores = i->canEmitSuffixStoreDrain;
			plan.suffixStores = i->softwarePipelineSuffixStores;
			plan.inductionUpdates = i->inductionUpdates;
			plan.drainTokenIndices = i->drainTokenIndices;
			plans.push_back( plan );
			plannedLabelIndices[plan.labelTokenIndex] = true;
			continue;
		}

		if( i->qConsumerTokenIndices.empty() )
			continue;

		plan.prefetchInsertAfterTokenIndex = i->qConsumerTokenIndices.back();
		plan.qProducerInsertAfterTokenIndex = i->qConsumerTokenIndices.back();
		unsigned int suffixBlockerTokenIndex = 0;
		if( qProducerCanMoveIntoBranchDelaySlot( *i, indexedTokens, &suffixBlockerTokenIndex ) )
		{
			plan.qProducerInsertAfterTokenIndex = i->branchTokenIndex;
			plan.qProducerInBranchDelaySlot = true;
		}
		else if( suffixBlockerTokenIndex != 0 )
		{
			plan.qProducerBranchDelayBlockedBySuffixDependency = true;
			plan.qProducerBranchDelaySuffixBlockerTokenIndex = suffixBlockerTokenIndex;
		}
		plan.emitsDrain = i->qLiveOut;
		plan.prefetches = i->softwarePipelinePrefetches;
		plan.rotations = i->softwarePipelineRotations;
		plan.suffixStores = i->softwarePipelineSuffixStores;
		plan.drainsSuffixStores = i->canEmitSuffixStoreDrain;
		for( std::vector<VuSoftwarePipelinePrefetch>::const_iterator p = i->softwarePipelinePrefetches.begin();
		     p != i->softwarePipelinePrefetches.end(); ++p )
			plan.prefetchTokenIndices.push_back( p->tokenIndex );
		plan.prologTokenIndices = i->prologTokenIndices;
		plan.mainTokenIndices = i->mainTokenIndices;
		plan.drainTokenIndices = i->drainTokenIndices;
		plan.qConsumerTokenIndices = i->qConsumerTokenIndices;
		plans.push_back( plan );
		plannedLabelIndices[plan.labelTokenIndex] = true;
	}

	const std::vector<VuLoopCandidate> loops = findVuLoopCandidates( tokens );
	for( std::vector<VuLoopCandidate>::const_iterator loop = loops.begin();
	     loop != loops.end(); ++loop )
	{
		if( plannedLabelIndices.find( loop->labelTokenIndex ) != plannedLabelIndices.end() )
			continue;
		VuSoftwarePipelineRewritePlan plan;
		if( buildGenericCyclicPrefixRewritePlan( *loop, indexedTokens, plan ) )
		{
			plans.push_back( plan );
			plannedLabelIndices[plan.labelTokenIndex] = true;
		}
	}

	if( std::getenv( "OPENVCL_DUMP_MULTISTAGE_CANDIDATES" ) != NULL )
	{
		for( std::vector<VuSoftwarePipelineRewritePlan>::const_iterator p = plans.begin();
		     p != plans.end(); ++p )
		{
			std::cerr << "[multistage-plan] loop=" << p->label
			          << " stageCount=" << p->stageCount
			          << " kernelTokens=" << p->kernelTokenIndices.size()
			          << " tokenStageOffsets=" << p->tokenStageOffsets.size()
			          << " rotations=" << p->stageRotationRegisters.size()
			          << "\n";
		}
	}

	// Track 9.G step 6e: surface the kernel-rewrite scaffolding propagated
	// from VuLoopPipelineOpportunity. Diagnostic-only; no consumer yet.
	if( std::getenv( "OPENVCL_DUMP_KERNEL_REWRITE_PROPAGATION" ) != NULL )
	{
		for( std::vector<VuSoftwarePipelineRewritePlan>::const_iterator p = plans.begin();
		     p != plans.end(); ++p )
		{
			std::cerr << "[kernel-rewrite-propagation] loop=" << p->label
			          << " II=" << p->kernelRewriteII
			          << " stageCount=" << p->kernelRewriteStageCount
			          << " conflicts=" << p->kernelRewriteConflicts
			          << " prolog=" << p->kernelRewritePrologTokens.size()
			          << " main=" << p->kernelRewriteMainTokens.size()
			          << " drain=" << p->kernelRewriteDrainTokens.size()
			          << " entryStages=" << p->kernelRewriteEntryStages.size()
			          << "\n";
		}
	}

	// Track 9.G step 8b-1: surface the rename-decision scaffolding.
	// Diagnostic-only; no consumer yet (the 7b emitter still rejects
	// any plan whose renameHints are non-empty).
	if( std::getenv( "OPENVCL_DUMP_KERNEL_RENAME_DECISIONS" ) != NULL )
	{
		for( std::vector<VuSoftwarePipelineRewritePlan>::const_iterator p = plans.begin();
		     p != plans.end(); ++p )
		{
			unsigned int assignedCount = 0;
			for( unsigned int d = 0; d < p->kernelRewriteRenameDecisions.size(); ++d )
				if( p->kernelRewriteRenameDecisions[ d ].assigned )
					++assignedCount;
			std::cerr << "[kernel-rename-decisions] loop=" << p->label
			          << " count=" << p->kernelRewriteRenameDecisions.size()
			          << " assigned=" << assignedCount
			          << "\n";
			if( std::getenv( "OPENVCL_DUMP_KERNEL_RENAME_DECISIONS_DETAIL" ) != NULL )
			{
				for( unsigned int d = 0; d < p->kernelRewriteRenameDecisions.size(); ++d )
				{
					const VuKernelRenameDecision& dec = p->kernelRewriteRenameDecisions[ d ];
					std::cerr << "[kernel-rename-decision] loop=" << p->label
					          << " reg=" << dec.reg
					          << " scratch=" << ( dec.assigned ? dec.scratch : std::string( "<none>" ) )
					          << " assigned=" << ( dec.assigned ? 1 : 0 )
					          << "\n";
				}
			}
		}
	}

	// Track 9.G step 8b-2a: surface the rename MOVE-slot scaffolding.
	// Diagnostic-only; the 7b emitter does not yet consume MOVE slots.
	if( std::getenv( "OPENVCL_DUMP_KERNEL_RENAME_MOVE_SLOTS" ) != NULL )
	{
		for( std::vector<VuSoftwarePipelineRewritePlan>::const_iterator p = plans.begin();
		     p != plans.end(); ++p )
		{
			unsigned int assignedCount = 0;
			for( unsigned int s = 0; s < p->kernelRewriteRenameMoveSlots.size(); ++s )
				if( p->kernelRewriteRenameMoveSlots[ s ].assigned )
					++assignedCount;
			std::cerr << "[kernel-rename-move-slots] loop=" << p->label
			          << " count=" << p->kernelRewriteRenameMoveSlots.size()
			          << " assigned=" << assignedCount
			          << "\n";
			if( std::getenv( "OPENVCL_DUMP_KERNEL_RENAME_MOVE_SLOTS_DETAIL" ) != NULL )
			{
				for( unsigned int s = 0; s < p->kernelRewriteRenameMoveSlots.size(); ++s )
				{
					const VuKernelRenameMoveSlot& mv = p->kernelRewriteRenameMoveSlots[ s ];
					const char* laneName = "?";
					if( mv.lane == 1 ) laneName = "upper";
					else if( mv.lane == 2 ) laneName = "lower";
					else laneName = "none";
					std::cerr << "[kernel-rename-move-slot] loop=" << p->label
					          << " decision=" << mv.decisionIndex
					          << " modSlot=" << mv.modSlot
					          << " lane=" << laneName
					          << " assigned=" << ( mv.assigned ? 1 : 0 )
					          << "\n";
				}
			}
		}
	}

	// Track 9.G step 6f: surface the register-plan + rename-hint scaffolding.
	// Diagnostic-only; no consumer yet.
	if( std::getenv( "OPENVCL_DUMP_KERNEL_REGPLAN_PROPAGATION" ) != NULL )
	{
		for( std::vector<VuSoftwarePipelineRewritePlan>::const_iterator p = plans.begin();
		     p != plans.end(); ++p )
		{
			std::cerr << "[kernel-regplan-propagation] loop=" << p->label
			          << " regs=" << p->kernelRewriteRegCount
			          << " hazards=" << p->kernelRewriteHazards.size()
			          << " waw=" << p->kernelRewriteWawCount
			          << " raw=" << p->kernelRewriteRawCount
			          << " war=" << p->kernelRewriteWarCount
			          << " renameHints=" << p->kernelRewriteRenameHints.size()
			          << "\n";
		}
	}

	// Track 9.G step 6g: surface the envelope scaffolding.
	// Diagnostic-only; no consumer yet.
	if( std::getenv( "OPENVCL_DUMP_KERNEL_ENVELOPE_PROPAGATION" ) != NULL )
	{
		for( std::vector<VuSoftwarePipelineRewritePlan>::const_iterator p = plans.begin();
		     p != plans.end(); ++p )
		{
			std::cerr << "[kernel-envelope-propagation] loop=" << p->label
			          << " kernelTokens=" << p->kernelEnvelopeKernelTokens
			          << " prologueCycles=" << p->kernelEnvelopePrologueCycles
			          << " epilogueCycles=" << p->kernelEnvelopeEpilogueCycles
			          << " conflicts=" << p->kernelEnvelopeConflicts
			          << " prologueStages=" << p->kernelEnvelopePrologueTokenCounts.size()
			          << " epilogueStages=" << p->kernelEnvelopeEpilogueTokenCounts.size()
			          << "\n";
		}
	}

	// Track 9.G step 6h: surface the stageCells VLIW grid scaffolding.
	// Diagnostic-only; no consumer yet. Reports grid size and a count of
	// non-empty lane slots across all cells.
	if( std::getenv( "OPENVCL_DUMP_KERNEL_STAGECELLS_PROPAGATION" ) != NULL )
	{
		for( std::vector<VuSoftwarePipelineRewritePlan>::const_iterator p = plans.begin();
		     p != plans.end(); ++p )
		{
			unsigned int upperCount = 0, lowerCount = 0, fdivCount = 0, efuCount = 0;
			for( std::vector<VuKernelTemplateSlot>::const_iterator c = p->kernelRewriteStageCells.begin();
			     c != p->kernelRewriteStageCells.end(); ++c )
			{
				if( c->upper != VuKernelTemplateSlot::NO_ENTRY ) ++upperCount;
				if( c->lower != VuKernelTemplateSlot::NO_ENTRY ) ++lowerCount;
				if( c->fdiv  != VuKernelTemplateSlot::NO_ENTRY ) ++fdivCount;
				if( c->efu   != VuKernelTemplateSlot::NO_ENTRY ) ++efuCount;
			}
			std::cerr << "[kernel-stagecells-propagation] loop=" << p->label
			          << " cells=" << p->kernelRewriteStageCells.size()
			          << " upper=" << upperCount
			          << " lower=" << lowerCount
			          << " fdiv=" << fdivCount
			          << " efu=" << efuCount
			          << "\n";
		}
	}

	// Track 9.G step 7a: gated-emission eligibility diagnostic.
	// Reports per-plan whether the modulo-placer scaffolding is complete
	// enough for a generic kernel-rewrite emitter to consume directly
	// (no rename support required). No emission path consumes this yet;
	// the helper isVuPlanEligibleForGenericKernelRewrite is the single
	// source of truth.
	if( std::getenv( "OPENVCL_DUMP_GENERIC_EMISSION_ELIGIBILITY" ) != NULL )
	{
		for( std::vector<VuSoftwarePipelineRewritePlan>::const_iterator p = plans.begin();
		     p != plans.end(); ++p )
		{
			const bool eligible = isVuPlanEligibleForGenericKernelRewrite( *p );
			std::cerr << "[generic-emission-eligibility] loop=" << p->label
			          << " eligible=" << ( eligible ? 1 : 0 )
			          << " II=" << p->kernelRewriteII
			          << " stageCount=" << p->kernelRewriteStageCount
			          << " conflicts=" << p->kernelRewriteConflicts
			          << " renameHints=" << p->kernelRewriteRenameHints.size()
			          << " mainTokens=" << p->kernelRewriteMainTokens.size()
			          << "\n";
		}
	}

	// Track 9.G step 8b-2c-2: per-field kernel-rename emission
	// eligibility diagnostic. Reports per-plan whether the rename
	// decisions + move-slots are complete and every mainTokens op
	// touching a decision base is on the splittable allowlist. The
	// blocker count surfaces the number of unsplittable touches when
	// scaffolding is otherwise complete.
	if( std::getenv( "OPENVCL_DUMP_KERNEL_RENAME_EMISSION_ELIGIBILITY" ) != NULL )
	{
		for( std::vector<VuSoftwarePipelineRewritePlan>::const_iterator p = plans.begin();
		     p != plans.end(); ++p )
		{
			const bool eligible = isVuPlanEligibleForKernelRenameEmission( *p, indexedTokens );
			const unsigned int blockers = countKernelRenameEmissionBlockers( *p, indexedTokens );
			const unsigned int softBlockers = countKernelRenameEmissionSoftBlockers( *p, indexedTokens );
			std::cerr << "[kernel-rename-emission-eligibility] loop=" << p->label
			          << " eligible=" << ( eligible ? 1 : 0 )
			          << " II=" << p->kernelRewriteII
			          << " stageCount=" << p->kernelRewriteStageCount
			          << " conflicts=" << p->kernelRewriteConflicts
			          << " decisions=" << p->kernelRewriteRenameDecisions.size()
			          << " moveSlots=" << p->kernelRewriteRenameMoveSlots.size()
			          << " hardBlockers=" << blockers
			          << " softBlockers=" << softBlockers
			          << " mainTokens=" << p->kernelRewriteMainTokens.size()
			          << "\n";
			// Track 9.G step 8b-2d-1: when blockers>0 (i.e. scaffolding
			// is complete but unsplittable ops touch a decision base)
			// also dump the decision targets and blocker op names so
			// the splittable allowlist can be widened surgically.
			if( blockers > 0u || softBlockers > 0u )
			{
				std::cerr << "[kernel-rename-emission-eligibility]   decisions=";
				for( unsigned int d = 0; d < p->kernelRewriteRenameDecisions.size(); ++d )
					std::cerr << ( d ? "," : "" )
					          << p->kernelRewriteRenameDecisions[d].reg;
				std::cerr << "\n";
				if( blockers > 0u )
				{
					const std::vector<std::string> blockerOps =
						describeKernelRenameEmissionBlockers( *p, indexedTokens );
					for( unsigned int b = 0; b < blockerOps.size(); ++b )
						std::cerr << "[kernel-rename-emission-eligibility]   hard blocker op="
						          << blockerOps[b] << "\n";
				}
				if( softBlockers > 0u )
				{
					const std::vector<std::string> softOps =
						describeKernelRenameEmissionSoftBlockers( *p, indexedTokens );
					for( unsigned int b = 0; b < softOps.size(); ++b )
						std::cerr << "[kernel-rename-emission-eligibility]   soft blocker op="
						          << softOps[b] << "\n";
				}
			}
		}
	}

	// Track 9.G-1h-4b-1: per-plan rename-emission expansion factor.
	// For each rename-eligible plan, count how many emitted tokens the
	// MAIN body would produce after splitMultiFieldOpByFieldDecisions
	// + materialize-MOVE injections + tail MOVE block. Compare against
	// the placer grid capacity (II * 4). When `expandedMainTokens` is
	// less than or equal to `II * 4`, sub-step 4b-3's expanded-DDG
	// placer should be able to fit. When greater, the placer needs to
	// either increase II or learn to pack multiple sub-ops per lane.
	if( std::getenv( "OPENVCL_DUMP_KERNEL_RENAME_EMISSION_EXPANSION" ) != NULL )
	{
		for( std::vector<VuSoftwarePipelineRewritePlan>::const_iterator p = plans.begin();
		     p != plans.end(); ++p )
		{
			if( !isVuPlanEligibleForKernelRenameEmission( *p, indexedTokens ) )
				continue;
			unsigned int splitGroups        = 0;
			unsigned int expandedFromSplits = 0;
			unsigned int materializeMOVEs   = 0;
			unsigned int passthrough        = 0;
			unsigned int nopSentinels       = 0;
			unsigned int assignedDecisions  = 0;
			for( unsigned int d = 0; d < p->kernelRewriteRenameDecisions.size(); ++d )
				if( p->kernelRewriteRenameDecisions[d].assigned )
					++assignedDecisions;
			for( unsigned int m = 0; m < p->kernelRewriteMainTokens.size(); ++m )
			{
				const unsigned int idx = p->kernelRewriteMainTokens[m];
				if( idx == VuKernelRewritePlan::NO_TOKEN )
				{
					++nopSentinels;
					continue;
				}
				if( idx >= indexedTokens.size() )
					continue;
				const Token& src = *indexedTokens[idx];
				if( tokenIsKernelRenameMaterializeCandidate( src, *p ) )
				{
					// 4b-8a: count only decisions whose original base is
					// actually read by `src`.
					for( unsigned int d = 0; d < p->kernelRewriteRenameDecisions.size(); ++d )
					{
						if( !p->kernelRewriteRenameDecisions[d].assigned )
							continue;
						if( tokenReadsDecisionBase( src, p->kernelRewriteRenameDecisions[d].reg ) )
							++materializeMOVEs;
					}
					++passthrough; // the unsplit op itself
					continue;
				}
				std::list<Token> split;
				splitMultiFieldOpByFieldDecisions( src, p->kernelRewriteRenameDecisions, split );
				const unsigned int sz = static_cast<unsigned int>( split.size() );
				if( sz > 1u )
				{
					++splitGroups;
					expandedFromSplits += sz;
				}
				else
				{
					++passthrough;
				}
			}
			// 4b-8b: count only decisions whose base is read by a drain
			// token (others' tail MOVEs are skipped).
			unsigned int tailMOVEs = 0;
			for( unsigned int d = 0; d < p->kernelRewriteRenameDecisions.size(); ++d )
			{
				if( !p->kernelRewriteRenameDecisions[d].assigned )
					continue;
				if( drainReadsDecisionBase( *p, indexedTokens, p->kernelRewriteRenameDecisions[d].reg ) )
					++tailMOVEs;
			}
			const unsigned int expandedMainTokens =
				expandedFromSplits + materializeMOVEs + tailMOVEs + passthrough;
			const unsigned int gridCapacity = p->kernelRewriteII * 4u;
			std::cerr << "[kernel-rename-emission-expansion] loop=" << p->label
			          << " II=" << p->kernelRewriteII
			          << " gridCapacity=" << gridCapacity
			          << " mainTokens=" << p->kernelRewriteMainTokens.size()
			          << " nopSentinels=" << nopSentinels
			          << " splitGroups=" << splitGroups
			          << " expandedFromSplits=" << expandedFromSplits
			          << " materializeMOVEs=" << materializeMOVEs
			          << " tailMOVEs=" << tailMOVEs
			          << " passthrough=" << passthrough
			          << " expandedMainTokens=" << expandedMainTokens
			          << " fits=" << ( expandedMainTokens <= gridCapacity ? 1 : 0 )
			          << "\n";
		}
	}

	return plans;
}

bool isVuPlanEligibleForGenericKernelRewrite( const VuSoftwarePipelineRewritePlan& plan )
{
	if( plan.kernelRewriteII == 0u )
		return false;
	if( plan.kernelRewriteStageCount < 2u )
		return false;
	if( plan.kernelRewriteConflicts != 0u )
		return false;
	if( !plan.kernelRewriteRenameHints.empty() )
		return false;
	if( plan.kernelRewriteMainTokens.empty() )
		return false;
	return true;
}

namespace
{
	// Track 9.G step 7b: retarget the BRANCH IMMEDIATE argument of a
	// branch-style token (b/ibne/ibeq/...) to a new label string.
	// Returns true if a BRANCH IMMEDIATE argument was found and updated.
	bool retargetBranchTokenTarget( Token& token, const std::string& newTarget )
	{
		std::list<Token::Argument>& args = token.arguments();
		for( std::list<Token::Argument>::iterator a = args.begin(); a != args.end(); ++a )
		{
			if( ( a->flags() & Token::Argument::BRANCH )
			    && a->type() == Token::Argument::IMMEDIATE )
			{
				a->setImmediate( newTarget );
				return true;
			}
		}
		return false;
	}
}

std::list<Token> applyVuGenericKernelRewritePlans( const std::list<Token>& tokens )
{
	const std::vector<VuSoftwarePipelineRewritePlan> plans = buildVuSoftwarePipelineRewritePlans( tokens );
	return applyVuGenericKernelRewritePlans( tokens, plans );
}

// 9.G-1h-4a-2: 3-argument overload populates outRanges. Implemented
// as a thin wrapper that captures ranges from the shared impl below.
std::list<Token> applyVuGenericKernelRewritePlans( const std::list<Token>& tokens,
                                                   const std::vector<VuSoftwarePipelineRewritePlan>& plans,
                                                   std::vector<VuKernelBlockRange>& outRanges );

std::list<Token> applyVuGenericKernelRewritePlans( const std::list<Token>& tokens,
                                                   const std::vector<VuSoftwarePipelineRewritePlan>& plans )
{
	std::vector<VuKernelBlockRange> discard;
	return applyVuGenericKernelRewritePlans( tokens, plans, discard );
}

std::list<Token> applyVuGenericKernelRewritePlans( const std::list<Token>& tokens,
                                                   const std::vector<VuSoftwarePipelineRewritePlan>& plans,
                                                   std::vector<VuKernelBlockRange>& outRanges )
{
	outRanges.clear();
	std::vector<const Token*> indexedTokens;
	for( std::list<Token>::const_iterator i = tokens.begin(); i != tokens.end(); ++i )
		indexedTokens.push_back( &*i );

	std::map<unsigned int, VuSoftwarePipelineRewritePlan> eligibleByLabelIndex;
	for( std::vector<VuSoftwarePipelineRewritePlan>::const_iterator p = plans.begin(); p != plans.end(); ++p )
	{
		const bool baseEligible = isVuPlanEligibleForGenericKernelRewrite( *p );
		const bool renameEligible = isVuPlanEligibleForKernelRenameEmission( *p, indexedTokens );
		// 8b-2c-3 relaxation: also accept plans whose ONLY base-eligibility
		// failure is non-empty renameHints, provided the rename-emission
		// gate (decisions+moveSlots assigned, all touching ops splittable)
		// holds. All other base predicates (II>0, stageCount>=2,
		// conflicts==0, mainTokens non-empty) are revalidated inside
		// isVuPlanEligibleForKernelRenameEmission.
		if( !baseEligible && !renameEligible )
			continue;
		if( p->labelTokenIndex >= indexedTokens.size() )
			continue;
		if( p->branchTokenIndex >= indexedTokens.size() )
			continue;
		eligibleByLabelIndex[p->labelTokenIndex] = *p;
	}

	if( eligibleByLabelIndex.empty() )
		return tokens;

	std::list<Token> output;
	unsigned int index = 0;
	std::list<Token>::const_iterator i = tokens.begin();
	while( i != tokens.end() )
	{
		std::map<unsigned int, VuSoftwarePipelineRewritePlan>::const_iterator hit =
		    eligibleByLabelIndex.find( index );
		if( hit == eligibleByLabelIndex.end() )
		{
			output.push_back( *i );
			++i;
			++index;
			continue;
		}

		const VuSoftwarePipelineRewritePlan& plan = hit->second;
		if( !tokenCanCarrySoftwarePipelineLabel( *i ) )
		{
			output.push_back( *i );
			++i;
			++index;
			continue;
		}

		const std::string proLabel  = plan.label + "__PRO1";
		const std::string mainLabel = plan.label + "__MAIN_LOOP";
		const std::string epi0Label = plan.label + "__EPI0";
		const std::string epi1Label = plan.label + "__EPI1";

		// 9.G-1h-4a-3b (G-1): the rename-emission path splits multi-
		// field ops and injects per-decision MOVE tokens into the MAIN
		// body, so the surviving body-token count no longer matches
		// the placer grid's non-NOP cell count. The bake-in consumer
		// relies on a positional grid->token mapping, so we only
		// publish the descriptor for non-rename-eligible plans here.
		const bool renameEligible =
		    isVuPlanEligibleForKernelRenameEmission( plan, indexedTokens );

		// 9.G-1h-4a-2: record the MAIN steady-state range. The body
		// lives in [mainLabel, epi0Label) of the rewritten stream.
		// 9.G-1h-4a-3a: also stash the placer grid (II * 4 cells) and II
		// for the bypass consumer. The grid indices are into THIS pass's
		// input `tokens` list.
		// 9.G-1h-4b-5: drop the !renameEligible suppression. We now
		// always publish the placer descriptor. Rename-eligible plans
		// still go through MOVE injection / multi-field split in the
		// emitted MAIN body below, so a bake-in consumer that wants to
		// stay grid-aligned must independently account for the
		// renameEligible flag (publishing the descriptor does not
		// commit the consumer to using it). This unlocks the
		// bake-in path's visibility into rename-eligible loops; the
		// previous !renameEligible gate predated 8b-2c-3's MOVE-tail
		// injection and is no longer the right safety boundary.
		{
			VuKernelBlockRange r;
			r.mainLabel = mainLabel;
			r.endLabel  = epi0Label;
			r.II        = plan.kernelRewriteII;
			r.placerGridMainTokens = plan.kernelRewriteMainTokens;
			// Track 9.G-1h step 4b-7a: also publish the refit-grid +
			// node descriptors. 4b-8c: per-loop rename-profitability
			// gate — only publish when the shadow placer achieved a
			// strictly better II than the original placer
			// (refitII < origII). Loops where the expanded MOVEs
			// inflate the II keep the legacy non-rename emission path
			// (empty refitNodes / refitMainTokens signals
			// buildKernelBakeIns to fall back to placerGridMainTokens).
			// Tolerance is currently 0 (strict improvement required).
			const bool refitProfitable =
			    plan.kernelRewriteRefitII != 0u &&
			    plan.kernelRewriteRefitII < plan.kernelRewriteII;
			if( refitProfitable )
			{
				r.refitNodes      = plan.kernelRewriteRefitNodes;
				r.refitMainTokens = plan.kernelRewriteRefitMainTokens;
			}
			if( std::getenv("OPENVCL_USE_EXPANDED_DDG_PLACER") != NULL )
			{
				std::cerr << "[refit-gate] loop=" << mainLabel
				          << " origII=" << plan.kernelRewriteII
				          << " refitII=" << plan.kernelRewriteRefitII
				          << " decision=" << ( refitProfitable ? "publish" : "reject" )
				          << std::endl;
			}
			outRanges.push_back( r );
		}

		const Token& branchSrc = *indexedTokens[plan.branchTokenIndex];

		// PRO1 label carrier (reuses the original loop label token).
		Token proLabelTok( *i );
		proLabelTok.setLabel( proLabel );
		output.push_back( proLabelTok );

		// Prolog body (strip labels).
		for( std::vector<unsigned int>::const_iterator p = plan.kernelRewritePrologTokens.begin();
		     p != plan.kernelRewritePrologTokens.end(); ++p )
		{
			if( *p < indexedTokens.size() )
				output.push_back( tokenWithoutLabel( *indexedTokens[*p] ) );
		}

		// Prolog branch: copy of the original branch, retargeted to EPI1
		// (skip-main path when the loop induction shows we have only one
		// iteration left after the prolog).
		Token proBranch( branchSrc );
		proBranch.setLabel( "" );
		retargetBranchTokenTarget( proBranch, epi1Label );
		output.push_back( proBranch );

		// MAIN_LOOP label.
		Token mainLabelTok( *i );
		mainLabelTok.setLabel( mainLabel );
		output.push_back( mainLabelTok );

		// 8b-2c-3: if the plan is rename-emission-eligible, split each
		// main token whose destination matches a decision base into
		// per-field clones retargeted to the per-field scratch, and
		// then append a MOVE per decision at the end of the main body
		// (before the branch). The downstream scheduler honours the
		// (modSlot, lane) constraints captured in
		// kernelRewriteRenameMoveSlots when placing those MOVEs.
		// (renameEligible computed above for the descriptor gate.)

		// Main body (strip labels).
		for( std::vector<unsigned int>::const_iterator m = plan.kernelRewriteMainTokens.begin();
		     m != plan.kernelRewriteMainTokens.end(); ++m )
		{
			if( *m >= indexedTokens.size() )
				continue;
			const Token& src = *indexedTokens[*m];
			if( renameEligible )
			{
				// 8b-2d-3: if `src` is a soft blocker — unsplittable,
				// touches a decision base, read-only of that base —
				// materialize the renamed reg from its scratches
				// immediately before emitting `src` unchanged. This
				// keeps stores like `sq VF13, ofs(vi)` correct after
				// the body has computed VF13's lanes into scratches.
				if( tokenIsKernelRenameMaterializeCandidate( src, plan ) )
				{
					for( std::vector<VuKernelRenameDecision>::const_iterator d = plan.kernelRewriteRenameDecisions.begin();
					     d != plan.kernelRewriteRenameDecisions.end(); ++d )
					{
						if( !d->assigned )
							continue;
						// 4b-8a: only materialize decisions whose
						// original base is actually read by this
						// soft-blocker token (kept in lockstep with
						// the shadow synthesis in
						// buildVuKernelExpandedNodes).
						if( !tokenReadsDecisionBase( src, d->reg ) )
							continue;
						output.push_back( makeKernelRenameMoveToken( *i, *d ) );
					}
					output.push_back( tokenWithoutLabel( src ) );
					continue;
				}
				std::list<Token> split;
				splitMultiFieldOpByFieldDecisions( src, plan.kernelRewriteRenameDecisions, split );
				for( std::list<Token>::const_iterator s = split.begin(); s != split.end(); ++s )
					output.push_back( tokenWithoutLabel( *s ) );
			}
			else
			{
				output.push_back( tokenWithoutLabel( src ) );
			}
		}

		// 8b-2c-3: inject per-decision MOVE tokens at the tail of the
		// main body, using the original loop label token as donor for
		// source-position metadata.
		if( renameEligible )
		{
			for( std::vector<VuKernelRenameDecision>::const_iterator d = plan.kernelRewriteRenameDecisions.begin();
			     d != plan.kernelRewriteRenameDecisions.end(); ++d )
			{
				if( !d->assigned )
					continue;
				// 4b-8b: skip tail MOVE if the decision's base is not
				// read by any drain token. In-loop reads are covered by
				// pre-soft-blocker materialize MOVEs (4b-8a).
				if( !drainReadsDecisionBase( plan, indexedTokens, d->reg ) )
					continue;
				output.push_back( makeKernelRenameMoveToken( *i, *d ) );
			}
		}

		// Main branch: copy of the original branch, retargeted to MAIN_LOOP.
		Token mainBranch( branchSrc );
		mainBranch.setLabel( "" );
		retargetBranchTokenTarget( mainBranch, mainLabel );
		output.push_back( mainBranch );

		// EPI0 label + drain body.
		Token epi0LabelTok( *i );
		epi0LabelTok.setLabel( epi0Label );
		output.push_back( epi0LabelTok );

		for( std::vector<unsigned int>::const_iterator d = plan.kernelRewriteDrainTokens.begin();
		     d != plan.kernelRewriteDrainTokens.end(); ++d )
		{
			if( *d < indexedTokens.size() )
				output.push_back( tokenWithoutLabel( *indexedTokens[*d] ) );
		}

		// EPI1 label (skip-main path tail; empty body for now).
		Token epi1LabelTok( *i );
		epi1LabelTok.setLabel( epi1Label );
		output.push_back( epi1LabelTok );

		// Advance the input iterator past the original loop body
		// (label through branch, inclusive).
		while( index <= plan.branchTokenIndex && i != tokens.end() )
		{
			++i;
			++index;
		}
	}

	return output;
}

std::list<Token> applyVuSoftwarePipelinePlans( const std::list<Token>& tokens )
{
	std::vector<const Token*> indexedTokens;
	for( std::list<Token>::const_iterator i = tokens.begin(); i != tokens.end(); ++i )
		indexedTokens.push_back( &*i );

	std::map<unsigned int, VuSoftwarePipelineRewritePlan> plansByLabelIndex;
	const std::vector<VuSoftwarePipelineRewritePlan> plans = buildVuSoftwarePipelineRewritePlans( tokens );
	for( std::vector<VuSoftwarePipelineRewritePlan>::const_iterator i = plans.begin(); i != plans.end(); ++i )
	{
		if( i->labelTokenIndex < indexedTokens.size() && i->branchTokenIndex < indexedTokens.size() )
			plansByLabelIndex[i->labelTokenIndex] = *i;
	}

	if( plansByLabelIndex.empty() )
		return tokens;

	std::list<Token> output;
	unsigned int index = 0;
	std::list<Token>::const_iterator i = tokens.begin();
	while( i != tokens.end() )
	{
		std::map<unsigned int, VuSoftwarePipelineRewritePlan>::const_iterator plan = plansByLabelIndex.find( index );
		if( plan == plansByLabelIndex.end() )
		{
			output.push_back( *i );
			++i;
			++index;
			continue;
		}

		const VuSoftwarePipelineRewritePlan& rewrite = plan->second;
		if( !tokenCanCarrySoftwarePipelineLabel( *i ) )
		{
			output.push_back( *i );
			++i;
			++index;
			continue;
		}

		Token prologLabel( *i );
		prologLabel.setLabel( rewrite.prologLabel );
		output.push_back( prologLabel );

		for( std::vector<unsigned int>::const_iterator p = rewrite.prologTokenIndices.begin();
		     p != rewrite.prologTokenIndices.end(); ++p )
		{
			if( *p < indexedTokens.size() )
			{
				if( rewrite.cyclicPrefixBeforeBranch )
					output.push_back( adjustedMultiQCyclicPrefixToken( *indexedTokens[*p],
					                                                   rewrite.cyclicPrefixRotations ) );
				else
					output.push_back( tokenWithoutLabel( *indexedTokens[*p] ) );
			}
		}

		if( rewrite.drainsSuffixStores )
		{
			if( rewrite.cyclicPrefixBeforeBranch )
			{
				appendCyclicPrefixMainBody( output,
				                            indexedTokens,
				                            rewrite,
				                            true,
				                            true,
				                            true );
			}
			else
				appendTokenRangeWithInsertedPrefetchAndQProducer(
				    output,
				    indexedTokens,
				    rewrite,
				    rewrite.mainTokenIndices.empty() ? rewrite.branchTokenIndex : rewrite.mainTokenIndices.front(),
				    rewrite.branchTokenIndex,
				    true,
				    true );
		}

		Token mainLabel( *i );
		mainLabel.setLabel( rewrite.mainLabel );
		output.push_back( mainLabel );
		if( rewrite.cyclicPrefixBeforeBranch )
			appendRotationMoves( output, *i, rewrite.cyclicPrefixRotations );

		if( rewrite.cyclicPrefixBeforeBranch )
		{
			if( rewrite.drainsSuffixStores )
				appendDelayedSuffixStores( output, indexedTokens, rewrite );
			appendCyclicPrefixMainBody( output,
			                            indexedTokens,
			                            rewrite,
			                            rewrite.drainsSuffixStores,
			                            false,
			                            true );
		}
		else
		{
			if( rewrite.drainsSuffixStores )
				appendDelayedSuffixStores( output, indexedTokens, rewrite );
			appendTokenRangeWithInsertedPrefetchAndQProducer(
			    output,
			    indexedTokens,
			    rewrite,
			    rewrite.mainTokenIndices.empty() ? rewrite.branchTokenIndex : rewrite.mainTokenIndices.front(),
			    rewrite.branchTokenIndex,
			    rewrite.drainsSuffixStores,
			    false );
		}

		while( index <= rewrite.branchTokenIndex && i != tokens.end() )
		{
			++i;
			++index;
		}

		if( rewrite.emitsDrain || rewrite.drainsSuffixStores )
		{
			Token drainLabel( *indexedTokens[rewrite.labelTokenIndex] );
			drainLabel.setLabel( rewrite.drainLabel );
			output.push_back( drainLabel );
			if( rewrite.drainsSuffixStores )
				appendDelayedSuffixStores( output, indexedTokens, rewrite );
			if( rewrite.emitsDrain && rewrite.cyclicPrefixNeedsGuard )
			{
				for( std::vector<unsigned int>::const_iterator d = rewrite.drainTokenIndices.begin();
				     d != rewrite.drainTokenIndices.end(); ++d )
				{
					if( *d < indexedTokens.size() )
						output.push_back( tokenWithoutLabel( *indexedTokens[*d] ) );
				}
			}
			if( rewrite.emitsDrain
			    && !rewrite.cyclicPrefixNeedsGuard
			    && rewrite.qProducerTokenIndex < indexedTokens.size() )
				output.push_back( adjustedQProducerToken( *indexedTokens[rewrite.qProducerTokenIndex],
				                                          rewrite.rotations ) );
		}
	}

	return output;
}

bool advanceVuStoreBaseUpdates( std::list<Token>& tokens )
{
	bool changed = false;

	for( std::list<Token>::iterator update = tokens.begin(); update != tokens.end(); )
	{
		if( update->label().length() != 0
		    || (update->flags() & (Token::BRANCH_DELAY_FILLER | Token::PREORDERED)) )
		{
			++update;
			continue;
		}

		VuLoopInductionUpdate induction;
		if( !describeSelfIntegerImmediateUpdate( *update, 0, induction )
		    || !induction.stepKnown
		    || induction.step == 0
		    || update == tokens.begin() )
		{
			++update;
			continue;
		}

		std::list<Token>::iterator firstStore = update;
		bool foundStore = false;
		std::list<Token>::iterator scan = update;
		while( scan != tokens.begin() )
		{
			--scan;
			if( tokenHasSchedulingBoundaryForStoreBaseAdvance( *scan ) )
				break;

			long offset = 0;
			if( storeUsesBaseWithKnownOffset( *scan, induction.registerName, offset ) )
			{
				(void)offset;
				firstStore = scan;
				foundStore = true;
				continue;
			}

			if( tokenReadsOrWritesRegisterKey( *scan, induction.registerName ) )
				break;
		}

		if( !foundStore )
		{
			++update;
			continue;
		}

		bool canMove = true;
		for( std::list<Token>::iterator i = firstStore; i != update; ++i )
		{
			long offset = 0;
			if( storeUsesBaseWithKnownOffset( *i, induction.registerName, offset ) )
			{
				(void)offset;
				continue;
			}
			if( tokenReadsOrWritesRegisterKey( *i, induction.registerName ) )
			{
				canMove = false;
				break;
			}
		}
		if( !canMove )
		{
			++update;
			continue;
		}

		for( std::list<Token>::iterator i = firstStore; i != update; ++i )
		{
			long offset = 0;
			if( storeUsesBaseWithKnownOffset( *i, induction.registerName, offset ) )
				setIndirectMemoryOffset( *i, induction.registerName, offset - induction.step );
		}

		Token movedUpdate( *update );
		std::list<Token>::iterator next = update;
		++next;
		tokens.erase( update );
		tokens.insert( firstStore, movedUpdate );
		update = next;
		changed = true;
	}

	return changed;
}

bool vuScheduledProgramHasNoCycleRegression( const VuScheduledProgram& original,
                                             const VuScheduledProgram& candidate )
{
	if( candidate.cycleCount > original.cycleCount )
		return false;

	for( std::vector<VuScheduledBasicBlock>::const_iterator c = candidate.blocks.begin();
	     c != candidate.blocks.end(); ++c )
	{
		std::string label;
		if( !scheduledBlockLabel( *c, label ) )
			continue;

		for( std::vector<VuScheduledBasicBlock>::const_iterator o = original.blocks.begin();
		     o != original.blocks.end(); ++o )
		{
			std::string originalLabel;
			if( scheduledBlockLabel( *o, originalLabel ) && originalLabel == label )
			{
				if( c->cycleCount > o->cycleCount )
					return false;
				break;
			}
		}
	}

	return true;
}

std::list<Token> applyVuSoftwarePipelinePlansWithSafeStoreBaseAdvance( const std::list<Token>& tokens )
{
	if( buildVuSoftwarePipelineRewritePlans( tokens ).empty() )
		return tokens;

	std::list<Token> pipelinedTokens = applyVuSoftwarePipelinePlans( tokens );
	std::list<Token> advancedTokens = pipelinedTokens;
	if( !advanceVuStoreBaseUpdates( advancedTokens ) )
		return pipelinedTokens;

	const VuScheduledProgram originalSchedule =
		scheduleVuProgramReadyIssueSlotsWithFlagLiveness( pipelinedTokens );
	const VuScheduledProgram advancedSchedule =
		scheduleVuProgramReadyIssueSlotsWithFlagLiveness( advancedTokens );
	if( vuScheduledProgramHasNoCycleRegression( originalSchedule, advancedSchedule ) )
		return advancedTokens;
	return pipelinedTokens;
}

std::list<Token> scheduleVuTokensPreservingOrder( const std::list<Token>& tokens )
{
	std::list<Token> scheduled;
	std::vector<VuBasicBlock> blocks = buildVuBasicBlocks( tokens );

	for( std::vector<VuBasicBlock>::const_iterator block = blocks.begin(); block != blocks.end(); ++block )
	{
		for( std::vector<const Token*>::const_iterator token = block->tokens.begin(); token != block->tokens.end(); ++token )
			scheduled.push_back( **token );
	}

	return scheduled;
}

std::list<Token> scheduleVuTokensReadySet( const std::list<Token>& tokens,
                                           unsigned int ignoredImplicitWawResources )
{
	const VuScheduledProgram program =
		scheduleVuProgramReadyIssueSlotsInternal( tokens, ignoredImplicitWawResources, false );
	return flattenVuScheduledProgramTokens( program );
}

std::list<Token> scheduleVuTokensReadySetWithFlagLiveness( const std::list<Token>& tokens )
{
	const VuScheduledProgram program =
		scheduleVuProgramReadyIssueSlotsWithFlagLivenessInternal( tokens, false );
	return flattenVuScheduledProgramTokens( program );
}

// Track 9.G step 8b-2c-1: thin wrappers around the anon-namespace
// splitter helpers so unit tests can exercise them without depending
// on the planner.
bool vuOpIsSplittableForKernelRename( const Token& token )
{
	return vuOpIsSplittableForKernelRenameByName( token.name() );
}

void splitMultiFieldOpByFieldDecisions( const Token& token,
                                        const std::vector<VuKernelRenameDecision>& decisions,
                                        std::list<Token>& out )
{
	splitMultiFieldOpByFieldDecisionsImpl( token, decisions, out );
}

// Track 9.G step 8b-2c-2: eligibility gate for the per-field
// kernel-rename emission path. Returns true iff the modulo-placer
// scaffolding is complete enough for the upcoming generic emitter
// (8b-2c-3) to consume per-decision rename + MOVE-slot info without
// further rename support. Specifically:
//
//   - 8b-2b base eligibility (II>0, stageCount>=2, conflicts==0,
//     mainTokens non-empty) — kernelRewriteRenameHints non-emptiness
//     is permitted here because the renames will be resolved via the
//     per-field decisions + move-slots picked in 8b-2a;
//   - decisions and moveSlots non-empty and the same length;
//   - every decision is assigned and every move-slot is assigned;
//   - every mainTokens op that reads or writes any decision base is on
//     the splittable allowlist (so the 8b-2c-1 splitter can clone it
//     per field without changing observable semantics).
//
// Diagnostic-only at this step; no consumer yet. The companion
// helper countKernelRenameEmissionBlockers reports how many mainTokens
// ops would block emission, for the diagnostic env var.
namespace
{
	bool tokenTouchesAnyDecisionBase( const Token& token,
	                                  const std::vector<std::string>& decisionBases )
	{
		for( std::list<Token::Argument>::const_iterator a = token.arguments().begin();
		     a != token.arguments().end(); ++a )
		{
			if( a->type() != Token::Argument::FLOAT_REGISTER )
				continue;
			std::string key;
			if( !vuRegisterKey( *a, key ) )
				continue;
			const std::string base = registerBaseKey( key );
			for( unsigned int b = 0; b < decisionBases.size(); ++b )
				if( decisionBases[b] == base )
					return true;
		}
		return false;
	}

	// Track 9.G step 8b-2d-3: returns true iff `token` writes any
	// FLOAT_REGISTER whose base is on `decisionBases`. Used to
	// distinguish "soft blockers" (unsplittable but read-only on the
	// renamed reg, recoverable by materialize-before-read) from "hard
	// blockers" (unsplittable AND write the renamed reg).
	bool tokenWritesAnyDecisionBase( const Token& token,
	                                 const std::vector<std::string>& decisionBases )
	{
		for( std::list<Token::Argument>::const_iterator a = token.arguments().begin();
		     a != token.arguments().end(); ++a )
		{
			if( a->type() != Token::Argument::FLOAT_REGISTER )
				continue;
			if( !( a->flags() & Token::Argument::WRITE ) )
				continue;
			std::string key;
			if( !vuRegisterKey( *a, key ) )
				continue;
			const std::string base = registerBaseKey( key );
			for( unsigned int b = 0; b < decisionBases.size(); ++b )
				if( decisionBases[b] == base )
					return true;
		}
		return false;
	}

	bool kernelRenameEmissionScaffoldingComplete( const VuSoftwarePipelineRewritePlan& plan )
	{
		if( plan.kernelRewriteII == 0u )                   return false;
		if( plan.kernelRewriteStageCount < 2u )            return false;
		if( plan.kernelRewriteConflicts != 0u )            return false;
		if( plan.kernelRewriteMainTokens.empty() )         return false;
		const std::vector<VuKernelRenameDecision>& decisions = plan.kernelRewriteRenameDecisions;
		const std::vector<VuKernelRenameMoveSlot>&  slots    = plan.kernelRewriteRenameMoveSlots;
		if( decisions.empty() )                            return false;
		if( decisions.size() != slots.size() )             return false;
		for( unsigned int d = 0; d < decisions.size(); ++d )
			if( !decisions[d].assigned ) return false;
		for( unsigned int s = 0; s < slots.size(); ++s )
			if( !slots[s].assigned ) return false;
		return true;
	}

	std::vector<std::string> collectDecisionBases( const VuSoftwarePipelineRewritePlan& plan )
	{
		std::vector<std::string> bases;
		const std::vector<VuKernelRenameDecision>& decisions = plan.kernelRewriteRenameDecisions;
		for( unsigned int d = 0; d < decisions.size(); ++d )
		{
			const std::string base = registerBaseKey( decisions[d].reg );
			bool found = false;
			for( unsigned int b = 0; b < bases.size(); ++b )
				if( bases[b] == base ) { found = true; break; }
			if( !found )
				bases.push_back( base );
		}
		return bases;
	}
}

unsigned int countKernelRenameEmissionBlockers( const VuSoftwarePipelineRewritePlan& plan,
                                                const std::vector<const Token*>& indexedTokens )
{
	if( !kernelRenameEmissionScaffoldingComplete( plan ) )
		return 0u; // not meaningful when scaffolding is incomplete
	const std::vector<std::string> bases = collectDecisionBases( plan );
	unsigned int blockers = 0;
	for( unsigned int m = 0; m < plan.kernelRewriteMainTokens.size(); ++m )
	{
		const unsigned int idx = plan.kernelRewriteMainTokens[m];
		if( idx >= indexedTokens.size() )
			continue;
		const Token& tk = *indexedTokens[idx];
		if( !tokenTouchesAnyDecisionBase( tk, bases ) )
			continue;
		if( vuOpIsSplittableForKernelRename( tk ) )
			continue;
		// Track 9.G step 8b-2d-3: read-only unsplittable touches are
		// "soft blockers" — recoverable by materialize-before-read.
		// Only writes remain "hard blockers".
		if( !tokenWritesAnyDecisionBase( tk, bases ) )
			continue;
		++blockers;
	}
	return blockers;
}

// Track 9.G step 8b-2d-3: returns the number of "soft blocker"
// mainTokens — unsplittable ops that touch a decision base but only
// read it. The emitter materializes the renamed reg from its scratches
// immediately before each such op, then emits the op unchanged.
unsigned int countKernelRenameEmissionSoftBlockers( const VuSoftwarePipelineRewritePlan& plan,
                                                    const std::vector<const Token*>& indexedTokens )
{
	if( !kernelRenameEmissionScaffoldingComplete( plan ) )
		return 0u;
	const std::vector<std::string> bases = collectDecisionBases( plan );
	unsigned int soft = 0;
	for( unsigned int m = 0; m < plan.kernelRewriteMainTokens.size(); ++m )
	{
		const unsigned int idx = plan.kernelRewriteMainTokens[m];
		if( idx >= indexedTokens.size() )
			continue;
		const Token& tk = *indexedTokens[idx];
		if( !tokenTouchesAnyDecisionBase( tk, bases ) )
			continue;
		if( vuOpIsSplittableForKernelRename( tk ) )
			continue;
		if( tokenWritesAnyDecisionBase( tk, bases ) )
			continue;
		++soft;
	}
	return soft;
}

std::vector<std::string> describeKernelRenameEmissionBlockers(
	const VuSoftwarePipelineRewritePlan& plan,
	const std::vector<const Token*>& indexedTokens )
{
	std::vector<std::string> ops;
	if( !kernelRenameEmissionScaffoldingComplete( plan ) )
		return ops;
	const std::vector<std::string> bases = collectDecisionBases( plan );
	for( unsigned int m = 0; m < plan.kernelRewriteMainTokens.size(); ++m )
	{
		const unsigned int idx = plan.kernelRewriteMainTokens[m];
		if( idx >= indexedTokens.size() )
			continue;
		const Token& tk = *indexedTokens[idx];
		if( !tokenTouchesAnyDecisionBase( tk, bases ) )
			continue;
		if( vuOpIsSplittableForKernelRename( tk ) )
			continue;
		// Hard blockers only (writes); soft blockers are surfaced via
		// describeKernelRenameEmissionSoftBlockers.
		if( !tokenWritesAnyDecisionBase( tk, bases ) )
			continue;
		ops.push_back( tk.name() );
	}
	return ops;
}

std::vector<std::string> describeKernelRenameEmissionSoftBlockers(
	const VuSoftwarePipelineRewritePlan& plan,
	const std::vector<const Token*>& indexedTokens )
{
	std::vector<std::string> ops;
	if( !kernelRenameEmissionScaffoldingComplete( plan ) )
		return ops;
	const std::vector<std::string> bases = collectDecisionBases( plan );
	for( unsigned int m = 0; m < plan.kernelRewriteMainTokens.size(); ++m )
	{
		const unsigned int idx = plan.kernelRewriteMainTokens[m];
		if( idx >= indexedTokens.size() )
			continue;
		const Token& tk = *indexedTokens[idx];
		if( !tokenTouchesAnyDecisionBase( tk, bases ) )
			continue;
		if( vuOpIsSplittableForKernelRename( tk ) )
			continue;
		if( tokenWritesAnyDecisionBase( tk, bases ) )
			continue;
		ops.push_back( tk.name() );
	}
	return ops;
}

// Track 9.G step 8b-2d-3: emitter-side predicate. Returns true iff
// `token` is a "soft blocker" against `plan`'s decisions — i.e. it
// touches a decision base, is not on the splittable allowlist, and
// does not write any decision base. The emitter must prepend per-
// decision materialize MOVEs (scratch->original base) before emitting
// such a token unchanged.
bool tokenIsKernelRenameMaterializeCandidate( const Token& token,
                                              const VuSoftwarePipelineRewritePlan& plan )
{
	if( !kernelRenameEmissionScaffoldingComplete( plan ) )
		return false;
	const std::vector<std::string> bases = collectDecisionBases( plan );
	if( !tokenTouchesAnyDecisionBase( token, bases ) )
		return false;
	if( vuOpIsSplittableForKernelRename( token ) )
		return false;
	if( tokenWritesAnyDecisionBase( token, bases ) )
		return false;
	return true;
}

bool isVuPlanEligibleForKernelRenameEmission( const VuSoftwarePipelineRewritePlan& plan,
                                              const std::vector<const Token*>& indexedTokens )
{
	if( !kernelRenameEmissionScaffoldingComplete( plan ) )
		return false;
	return countKernelRenameEmissionBlockers( plan, indexedTokens ) == 0u;
}

}
