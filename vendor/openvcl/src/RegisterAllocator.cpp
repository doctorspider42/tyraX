/*
 * RegisterAllocator.cpp
 *
 * Copyright (C) 2004 Jesper Svennevid, Daniel Collin
 *
 * Licensed under the AFL v2.0. See the file LICENSE included with this
 * distribution for licensing terms.
 *
 */


#include "RegisterAllocator.h"
#include "VuSchedulingRules.h"
#include "BranchState.h"
#include "Error.h"
#include "VuTokenResourceAccess.h"

#include <iostream>
#include <iomanip>
#include <map>
#include <set>
#include <vector>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace vcl
{

namespace
{
	// An alias splitDeadFloatRanges created, told apart by the `~generation`
	// suffix it renames into.  `~` cannot occur in a VCL identifier, so the test
	// can never mistake a source name for a web.
	bool aliasIsSplitWeb( const Alias* alias )
	{
		return alias != NULL && alias->debugName().find( '~' ) != std::string::npos;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RegisterAllocator::RegisterAllocator()
{
	unsigned int i;

	for( i = 0; i < sizeof(m_floats)/sizeof(Register); i++ )
	{
		std::stringstream s;
		s << "VF" << std::setw(2) << std::setfill('0') << i;
		m_floats[i].setName( s.str() );
	}

	for( i = 0; i < sizeof(m_integers)/sizeof(Register); i++ )
	{
		std::stringstream s;
		s << "VI" << std::setw(2) << std::setfill('0') << i;
		m_integers[i].setName( s.str() );
	}

	m_dynamicThreshold = 16;
	m_showRegisterInfo = false;
	m_sinkStoreBaseUnknown = false;
	m_sinkIndirectBranch = false;
	m_currState = OUTSIDE;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RegisterAllocator::~RegisterAllocator()
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RegisterAllocator::reset()
{
	m_labels.clear();
	m_states.clear();
	m_aliases.clear();
	m_coalescedWrites.clear();
	m_dynamicTracker.clear();
	m_name.clear();
	m_currState = OUTSIDE;

	for( unsigned int i = 0; i < 32; ++i )
		m_floats[i].setBusy( false );
	for( unsigned int i = 0; i < 16; ++i )
		m_integers[i].setBusy( false );

	m_sinkIntegerWrites.clear();
	m_sinkStoreOffsets.clear();
	m_sinkStoreVagueBases.clear();
	m_sinkLoopHeaders.clear();
	m_sinkNameWrites.clear();
	m_sinkLabelBranchCount.clear();
	m_sinkStoreBaseUnknown = false;
	m_sinkIndirectBranch = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RegisterAllocator::setAvailableFloats( unsigned int floats )
{
	for( unsigned int i = 1; i < 32; i++ )
		m_floats[i].setAvailable( ( floats & (1<<i) ) != 0 );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RegisterAllocator::setAvailableIntegers( unsigned int integers  )
{
	for( unsigned int i = 1; i < 16; i++ )
		m_integers[i].setAvailable( ( integers & (1<<i) ) != 0 );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RegisterAllocator::process( std::list<Token>& tokens )
{
	BranchState* branchState = NULL;

	// Before the sink pass and before anything reads a line number: deleting a
	// token is the only thing cheaper than scheduling it well, and every later
	// pass - sinking, allocation, the scheduler - is better off not seeing it.
	if( vuDropDeadWritesEnabled() )
	{
		const unsigned int dropped = dropDeadRegisterWrites( tokens );
		if( m_showRegisterInfo )
			std::cerr << "--drop-dead-writes removed " << dropped << " tokens" << std::endl;
	}

	// Before the sink pass, and while a name still means what the source meant
	// by it: splitting renames alias occurrences, and sinking decides where a
	// load goes by which alias reads it.
	if( vuSplitDeadFloatRangesEnabled() )
	{
		const unsigned int split = splitDeadFloatRanges( tokens );
		if( m_showRegisterInfo )
			std::cerr << "--split-dead-float-ranges renamed " << split << " occurrences" << std::endl;
	}

	// First, before anything reads a line number: this is the one pass that
	// reorders the token list, and it rewrites the timeline when it does.
	if( vuSinkLoadsEnabled() )
	{
		const unsigned int sunk = sinkLoadsToFirstUse( tokens );
		if( m_showRegisterInfo )
			std::cerr << "--sink-loads moved " << sunk << " loads" << std::endl;
	}

	if( !collectLabels( tokens.begin(), tokens.end() ) )
		return false;

	for( std::list<Token>::iterator i = tokens.begin(), next = tokens.begin(); i != tokens.end(); i = next )
	{
		next++;

		switch( state() )
		{
			case OUTSIDE:
			{
				if( !(*i).operand() )
					continue;

				if( !((*i).operand()->flags() & Operand::PREPROCESSOR) )
				{
					Error::Display( Error( "Code not allowed outside entry point", *i ) );
					return false;
				}

				if( "--enter" == (*i).operand()->name() )
				{
					next = i;

					branchState = new BranchState( *this );
					assert( branchState );

					setState( ENTER );
				}
				if( ("--exit" == (*i).operand()->name()) || ("--exitm" == (*i).operand()->name()) )
				{
					next = i;
					setState( EXIT );
				}
				else
				{
				}
			}
			break;

			case ENTER:
			{
				if( !(*i).operand() )
					continue;

				if( (*i).operand()->unit() != Operand::ENTER )
				{
					Error::Display( Error( "Invalid operand inside --enter/--endenter block", *i ) );
					delete branchState;
					return false;
				}

				(*i).setFlags( (*i).flags() | Token::IGNORED );

				if( "in_vf" == (*i).operand()->name() )
				{
					if( (*i).arguments().empty() )
					{
						Error::Display( Error( "Missing argument to in_vf", *i ) );
						return false;
					}
					const Token::Argument& arg = *((*i).arguments().begin());
					branchState->setFloatInput( arg.immediate(), arg.regNumber() );
				}
				else if( "in_vi" == (*i).operand()->name() )
				{
					if( (*i).arguments().empty() )
					{
						Error::Display( Error( "Missing argument to in_vi", *i ) );
						return false;
					}
					const Token::Argument& arg = *((*i).arguments().begin());
					branchState->setIntegerInput( arg.immediate(), arg.regNumber() );
				}
				else if( "in_hw_acc" == (*i).operand()->name() )
					branchState->writeAccumulator( Token::X|Token::Y|Token::Z|Token::W );
				else if( "in_hw_i" == (*i).operand()->name() )
					branchState->writeI();
				else if( "in_hw_p" == (*i).operand()->name() )
					branchState->writeP();
				else if( "in_hw_q" == (*i).operand()->name() )
					branchState->writeQ();
				else if( "in_hw_r" == (*i).operand()->name() )
					branchState->writeR();
				else if( "--endenter" == (*i).operand()->name() )
				{
					m_states.push_back( branchState );
					setState( CODE );
				}
			}
			break;

			case CODE:
			{
				if( !branchState )
				{
					Error::Display( Error( "Internal error: missing branch state before CODE block" ) );
					return false;
				}
				// locate end of codeblock
				for( ; next != tokens.end(); next++ )
				{
					if( !(*next).operand() )
						continue;

					if( !((*next).operand()->flags() & Operand::PREPROCESSOR) )
						continue;

					if( ((*next).operand()->unit() == Operand::EXIT) || ((*next).operand()->unit() == Operand::ENTER) )
						break;

					if( !processCommonDirective( (*next) ) )
						return false;
				}

				branchState->setCurrent( i );
				branchState->setExitPoint( next );
				branchState->pushTraces( &*i, true );

				setState( EXIT );
			}
			break;

			case EXIT:
			{
				if( !(*i).operand() )
					continue;

				if( (*i).operand()->unit() != Operand::EXIT )
				{
					Error::Display( Error( "Invalid operand inside --exit/--endexit block", *i ) );
					return false;
				}

				(*i).setFlags( (*i).flags() | Token::IGNORED );

				if( "out_vf" == (*i).operand()->name() )
				{
					if( (*i).arguments().empty() )
					{
						Error::Display( Error( "Missing argument to out_vf", *i ) );
						return false;
					}
					const Token::Argument& arg = *((*i).arguments().begin());

					if( branchState )
						branchState->setFloatOutput( arg.immediate(), arg.regNumber() );
				}
				else if( "out_vi" == (*i).operand()->name() )
				{
					if( (*i).arguments().empty() )
					{
						Error::Display( Error( "Missing argument to out_vi", *i ) );
						return false;
					}
					const Token::Argument& arg = *((*i).arguments().begin());
					if( branchState )
						branchState->setIntegerOutput( arg.immediate(), arg.regNumber() );
				}
				else if( "--endexit" == (*i).operand()->name() )
				{
					setState( OUTSIDE );
				}
			}
			break;
		}

		if( !processCommonDirective( (*i) ) )
			return false;
	}

	std::list<BranchState*>::iterator j;
	for( j = m_states.begin(); j != m_states.end(); j++ )
	{
		if( !(*j) )
		{
			Error::Display( Error( "Internal error: null branch state before processing" ) );
			return false;
		}
		if( !processBranchState( *j, tokens.end() ) )
			return false;
	}

	// Before the extension passes, not after: trimming is allowed to undo the
	// branch-state analysis's blanket stretch, but it must not be able to undo a
	// liveness requirement one of the extensions below discovers.
	if( vuTrimUncarriedRangesEnabled() )
		trimUncarriedLoopLocalRanges( tokens );

	collectLiteralRegisterUsage( tokens );
	extendContinuationLiveRanges( tokens );
	extendLoopDirectiveLiveRanges( tokens );
	extendMultiQStageLiveRanges( tokens );

	// After every extension: the deadness test below reads final ranges.
	if( vuCoalesceFloatWritesEnabled() )
		coalesceSameNameFloatWrites( tokens );

	if( m_aliases.size() > 0 )
	{
		// Coalescing is a preference, not a requirement, and the chain pre-pass
		// enforces it as a requirement: a chain must find ONE register free over
		// the union of every member's range, and a long chain placed early can
		// leave a later one with nothing. That turned vu0_rt_kernel - which
		// allocates fine without the flag - into a failure. So allocate with the
		// coalescing edges, and if that runs out, throw the edges away and
		// allocate again. The flag can then only ever add programs that compile.
		std::map<Alias*, const Register*> preallocated;
		if( !m_coalescedWrites.empty() )
		{
			for( AliasMap::iterator i = m_aliases.begin(); i != m_aliases.end(); ++i )
				preallocated[ i->first ] = i->first->allocatedRegister();
		}

		bool allocated = processAliases();

		if( !allocated && !m_coalescedWrites.empty() )
		{
			for( std::map<Alias*, const Register*>::iterator i = preallocated.begin();
			     i != preallocated.end(); ++i )
				i->first->setAllocatedRegister( i->second );
			for( unsigned int i = 0; i < m_coalescedWrites.size(); ++i )
				m_coalescedWrites[i]->setSameNamePredecessor( NULL );
			m_coalescedWrites.clear();

			if( m_showRegisterInfo )
				std::cerr << "Retrying allocation without --coalesce-float-writes" << std::endl;

			allocated = processAliases();
		}

		if( !allocated )
		{
			Error::Display( Error( "Register allocation ran out of registers" ) );
			return false;
		}
	}

	while( !m_states.empty() )
	{
		delete m_states.back();
		m_states.pop_back();
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RegisterAllocator::processBranchState( BranchState* state, std::list<Token>::iterator end )
{
	std::list<Token>::iterator curr, next;
	for( ; state->current() != end; state->setCurrent( curr ) )
	{
		curr = state->current();
		Token& token = *curr;
		curr++;

		if( !token.operand() )
			continue;

		// if we've reached a new entry or exit-point, abort branch
		if( (token.operand()->unit() == Operand::EXIT) || (token.operand()->unit() == Operand::ENTER) )
			break;

		token.setFlags( token.flags() | Token::PROCESSED );

		for( std::list<Token::Argument>::reverse_iterator i = token.arguments().rbegin(); i != token.arguments().rend(); i++ )
		{
			switch( (*i).type() )
			{
				case Token::Argument::FLOAT_REGISTER:
				{
					if( (*i).flags() & Token::Argument::WRITE )
						state->writeFloat( (*i) );
					else
					{
						if( !state->readFloat( (*i) ) )
						{
							Error::Display( Error( "Read-attempt from uninitialized float register", token, *i ) );
							return false;
						}
					}
				}
				break;

				case Token::Argument::INTEGER_REGISTER:
				{
					if( (*i).flags() & Token::Argument::WRITE )
						state->writeInteger( (*i) );
					else
					{
						if( !state->readInteger( (*i) ) )
						{
							Error::Display( Error( "Read-attempt from uninitialized integer register", token, *i ) );
							return false;
						}
					}
				}
				break;

				case Token::Argument::ACCUMULATOR:
				{
					if( (*i).flags() & Token::Argument::WRITE )
						state->writeAccumulator( (*i).fields() );
					else
					{
						if( !state->readAccumulator( (*i).fields() ) )
						{
							Error::Display( Error( "Read-attempt from uninitialized accumulator", token, *i ) );
							return false;
						}
					}
				}
				break;

				case Token::Argument::Q:
				{
					if( (*i).flags() & Token::Argument::WRITE )
						state->writeQ();
					else
					{
						if( !state->readQ() )
						{
							Error::Display( Error( "Read-attempt from uninitialized Q register", token, *i ) );
							return false;
						}
					}
				}
				break;

				case Token::Argument::P:
				{
					if( (*i).flags() & Token::Argument::WRITE )
						state->writeP();
					else
					{
						if( !state->readP() )
						{
							Error::Display( Error( "Read-attempt from uninitialized P register", token, *i ) );
							return false;
						}
					}
				}
				break;

				case Token::Argument::R:
				{
					if( (*i).flags() & Token::Argument::WRITE )
						state->writeR();
					else
					{
						if( !state->readR() )
						{
							Error::Display( Error( "Read-attempt from uninitialized R register", token, *i ) );
							return false;
						}
					}
				}
				break;

				case Token::Argument::I:
				{
					if( (*i).flags() & Token::Argument::WRITE )
						state->writeI();
					else
					{
						if( !state->readI() )
						{
							Error::Display( Error( "Read-attempt from uninitialized I register", token, *i ) );
							return false;
						}
					}
				}
				break;

				case Token::Argument::IMMEDIATE:
				{
					if( token.operand()->flags() & Operand::IWRITE )
					{
						// special case for LOI
						state->writeI();
					}
				}
				break;

				default: break;
			}
		}

		if( token.operand()->unit() == Operand::BRU )
		{
			// branch instruction

			std::list<Token::Argument>::const_iterator branchDest = token.arguments().end();
			std::list<Token::Argument>::const_iterator branchStore = token.arguments().end();

			std::list<Token::Argument>::const_iterator i;
			for( i = token.arguments().begin(); i != token.arguments().end(); i++ )
			{
				if( (*i).flags() & Token::Argument::BRANCH )
					branchDest = i;

				if( (*i).flags() & Token::Argument::ADDRESS )
					branchStore = i;
			}

			if( token.arguments().end() == branchDest )
			{
				Error::Display( Error( "Invalid branch", token ) );
				return false;
			}

			if( token.arguments().end() != branchStore )
				state->storeAddress( *branchStore );

			std::list<Token>::iterator target;

			if( token.operand()->flags() & Operand::DYNAMIC )
			{
				if( !state->address( *branchDest, target, m_labels ) )
					continue;

				unsigned int targetLine = (*target).lineNumber();
				unsigned int currentLine = (*curr).lineNumber();
				bool isBackwardBranch = targetLine < currentLine;

				if( state->isBranchTaken( &*target ) )
				{
					if( isBackwardBranch )
					{
						// Backward branch (loop back-edge) already processed - extend ranges again and stop
						state->extendLiveRanges( targetLine, currentLine );
					}
					break;
				}

				// First time seeing this branch - extend ranges now if it's a backward branch
				if( isBackwardBranch )
				{
					state->extendLiveRanges( targetLine, currentLine );
				}

				if( !updateDynamicTracker( &*state->current() ) )
					break;

				state->storeBranch( &*target );

				BranchState* newState = new BranchState( *state );
				assert( newState );

				newState->pushTraces( &*curr, false );
				newState->pushTraces( &*target, true );
				newState->setCurrent( target );

				m_states.push_back( newState );
			}
			else
			{
				if( !state->address( *branchDest, target, m_labels ) )
					break;

				if( state->isBranchTaken( &*target ) )
				{
					// Backward branch (loop back-edge) - extend live ranges to cover loop body
					unsigned int targetLine = (*target).lineNumber();
					unsigned int currentLine = (*curr).lineNumber();
					if( targetLine < currentLine )
						state->extendLiveRanges( targetLine, currentLine );
					break;
				}

				state->storeBranch( &*target );

				state->pushTraces( &*curr, false );
				state->pushTraces( &*target, true );

				curr = target;
			}
		}
	}

	if( state->current() == state->exitPoint() )
	{
		if( !state->applyRegisterOutputs() )
		{
			Error::Display( Error( "Output register conflict", *(state->exitPoint()) ) );
			return false;
		}
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RegisterAllocator::collectLabels( std::list<Token>::iterator start, std::list<Token>::iterator end )
{
	m_labels.clear();

	for( std::list<Token>::iterator i = start; i != end; i++ )
	{
		if( (*i).label().length() )
		{
			std::map<std::string,std::list<Token>::iterator>::iterator j = m_labels.find( (*i).label() );

			if( j != m_labels.end() )
			{
				Error::Display( Error( "Duplicate label '" + (*i).label() + "'", *i ) );
				return false;
			}

			m_labels[ (*i).label() ] = i;
		}
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RegisterAllocator::processAliases()
{
	// allocate registers that do not overlap usage

	// Count total aliases by type
	int totalFloatAliases = 0;
	int totalIntAliases = 0;
	int preallocatedFloats = 0;
	int preallocatedInts = 0;

	for( AliasMap::iterator i = m_aliases.begin(); i != m_aliases.end(); ++i )
	{
		if( i->first->type() == Alias::FLOAT )
		{
			totalFloatAliases++;
			if( i->first->allocatedRegister() )
				preallocatedFloats++;
		}
		else
		{
			totalIntAliases++;
			if( i->first->allocatedRegister() )
				preallocatedInts++;
		}
	}

	// Count available registers
	int availableFloats = 0;
	int availableInts = 0;
	for( int i = 0; i < 32; i++ )
		if( m_floats[i].available() )
			availableFloats++;
	for( int i = 0; i < 16; i++ )
		if( m_integers[i].available() )
			availableInts++;

	if( m_showRegisterInfo )
	{
		std::cerr << "\n=== Register Allocation Debug ===" << std::endl;
		std::cerr << "Float registers: " << availableFloats << " available, "
		          << (totalFloatAliases - preallocatedFloats) << " needed (total aliases: "
		          << totalFloatAliases << ", preallocated: " << preallocatedFloats << ")" << std::endl;
		std::cerr << "Integer registers: " << availableInts << " available, "
		          << (totalIntAliases - preallocatedInts) << " needed (total aliases: "
		          << totalIntAliases << ", preallocated: " << preallocatedInts << ")" << std::endl;

		// Print all alias ranges for debugging
		std::cerr << "\n=== Alias Ranges ===" << std::endl;
		for( AliasMap::iterator i = m_aliases.begin(); i != m_aliases.end(); ++i )
		{
			Alias* alias = i->first;
			std::cerr << (alias->type() == Alias::FLOAT ? "FLOAT" : "INT") << " "
			          << (alias->debugName().empty() ? "?" : alias->debugName())
			          << " #" << alias->id() << ": ";
			if( alias->allocatedRegister() )
				std::cerr << "[prealloc: " << alias->allocatedRegister()->name() << "] ";
			alias->printRanges( std::cerr );
			std::cerr << std::endl;
		}
		std::cerr << "====================\n" << std::endl;
	}

	// Pre-pass: allocate every two-address chain ATOMICALLY (root +
	// all its known successors) before any singleton aliases run the
	// main loop.  Treating chain members one-at-a-time isn't enough,
	// because an unrelated singleton alias whose live range fits in
	// the same register can snipe it before the successor reaches the
	// main loop — leaving the successor with no choice but a different
	// register, which is exactly the bug we're trying to prevent (the
	// `isubiu` dest then disagrees with its source, the loop counter
	// never decrements, and xgkick never fires).
	//
	// For each unallocated root (no sameNamePredecessor), find every
	// alias whose chain root is this root, then pick a register that
	// no non-chain allocated alias intersects on ANY chain member's
	// range.  Assign that register to all chain members at once.
	for( AliasMap::iterator i = m_aliases.begin(); i != m_aliases.end(); ++i )
	{
		Alias* root = i->first;
		if( root->allocatedRegister() )
			continue;
		if( root->sameNamePredecessor() )
			continue;

		// Collect chain members: walk every alias's predecessor chain
		// (with a hard hop cap as a cycle defence) and gather the ones
		// that terminate at `root`.  This is O(N²) but N is small.
		std::vector<Alias*> chain;
		chain.push_back( root );
		for( AliasMap::iterator k = m_aliases.begin(); k != m_aliases.end(); ++k )
		{
			Alias* other = k->first;
			if( other == root ) continue;
			if( other->type() != root->type() ) continue;
			Alias* p = other;
			for( int hop = 0; hop < 32 && p->sameNamePredecessor(); ++hop )
				p = p->sameNamePredecessor();
			if( p == root )
				chain.push_back( other );
		}

		unsigned int maxLimit = root->type() == Alias::FLOAT ? 32 : 16;
		// With --split-dead-float-ranges, first fit is the wrong answer here for
		// the same reason as in the main loop below - and this pre-pass places
		// almost everything, because --coalesce-float-writes makes almost every
		// alias a chain root or a chain member.  A spread policy that skipped
		// this loop would never run.
		// FLOAT only.  The split pass renames float aliases, so only the float
		// file has webs to spread; spreading the sixteen-wide integer file buys no
		// interleave and burns the registers that hold loop counters and DMA base
		// pointers.  Left on for integers it was the cause of most of the
		// fallbacks below - `Failed to allocate INTEGER register for alias
		// srcBase` in vu_script3_td_cl, with the float side fitting fine - and
		// turning it off is better on all three corpora AND on modelled cycles.
		bool chainHasWeb = false;
		for( unsigned int m = 0; !chainHasWeb && m < chain.size(); ++m )
			chainHasWeb = aliasIsSplitWeb( chain[m] );
		const bool spreadChain = vuSpreadFloatRegistersEnabled()
		                         && root->type() == Alias::FLOAT
		                         && ( !vuSpreadFloatRegistersWebsOnly() || chainHasWeb );
		std::vector<const Register*> chainAcceptable;
		for( unsigned int j = 0; j < maxLimit; ++j )
		{
			const Register* candidate = (root->type() == Alias::FLOAT) ? &m_floats[j] : &m_integers[j];
			if( !candidate->available() )
				continue;

			// `candidate` must not collide with any already-allocated
			// non-chain alias on ANY chain member's live range.
			bool conflict = false;
			for( AliasMap::iterator k = m_aliases.begin(); !conflict && k != m_aliases.end(); ++k )
			{
				Alias* src = k->first;
				if( !src->allocatedRegister() ) continue;
				if( src->type() != root->type() ) continue;
				if( src->allocatedRegister() != candidate ) continue;
				// Chain members don't conflict with each other on the
				// shared register — that's the whole point.
				bool inChain = false;
				for( unsigned int m = 0; m < chain.size(); ++m )
					if( chain[m] == src ) { inChain = true; break; }
				if( inChain ) continue;
				for( unsigned int m = 0; m < chain.size(); ++m )
				{
					if( chain[m]->intersects( src ) )
					{
						conflict = true;
						break;
					}
				}
			}

			if( !conflict )
			{
				if( !spreadChain )
				{
					for( unsigned int m = 0; m < chain.size(); ++m )
						chain[m]->setAllocatedRegister( candidate );
					break;
				}
				chainAcceptable.push_back( candidate );
			}
		}

		if( spreadChain && !chainAcceptable.empty() )
		{
			const Register* chosen = preferSpreadRegister( chain, chainAcceptable );
			for( unsigned int m = 0; m < chain.size(); ++m )
				chain[m]->setAllocatedRegister( chosen );
		}
	}

	for( AliasMap::iterator i = m_aliases.begin(); i != m_aliases.end(); ++i )
	{
		Alias* dest = i->first;

		// preallocated register
		if( dest->allocatedRegister() )
			continue;

		unsigned int maxLimit = dest->type() == Alias::FLOAT ? 32 : 16;

		// Two-address coalescing: if `dest` was created by a write that
		// reuses an existing source-level alias name (e.g. the dest of
		// `isubiu x, x, 1`), try the predecessor's register FIRST.  The
		// predecessor may itself be unallocated yet; walk the chain to
		// find the earliest allocated ancestor.  Falls through to the
		// regular j=0..maxLimit scan if no predecessor / preferred reg
		// is available or it conflicts.
		const Register* preferred = NULL;
		// Walk to the root, with a hard cap to defeat any residual cycle
		// the branch-state analysis might have introduced via loop re-
		// processing.  16 is well above any plausible chain depth in
		// real shaders.
		Alias* chainRoot = dest;
		for( int hop = 0; hop < 16 && chainRoot->sameNamePredecessor(); ++hop )
			chainRoot = chainRoot->sameNamePredecessor();
		if( chainRoot != dest && chainRoot->allocatedRegister() )
			preferred = chainRoot->allocatedRegister();
		if( preferred )
		{
			// Run the normal conflict check against `preferred`.  If it
			// holds, assign immediately and skip the j-loop.
			bool conflict = false;
			for( AliasMap::iterator k = m_aliases.begin(); k != m_aliases.end(); ++k )
			{
				Alias* src = k->first;
				if( !src->allocatedRegister() ) continue;
				if( src->type() != dest->type() ) continue;
				if( src->allocatedRegister() != preferred ) continue;
				if( src == dest ) continue;
				// Same-name predecessor on the chain doesn't count as a
				// conflict: that's the whole point of coalescing.
				// Bounded by the alias count, which no acyclic chain can reach, so
				// this walk gives the same answer as an unbounded one on every
				// input that is well formed and merely stops on one that is not.
				// It is the loop that hung on --loop-liveness-always: every other
				// walk of this chain in the file already carries a cap and this one
				// did not, so a ring built by the pass above spun here forever.
				bool isAncestor = false;
				const size_t ancestorCap = m_aliases.size() + 1;
				size_t ancestorHops = 0;
				for( Alias* p = dest->sameNamePredecessor(); p && ancestorHops < ancestorCap;
				     p = p->sameNamePredecessor(), ++ancestorHops )
				{
					if( p == src ) { isAncestor = true; break; }
				}
				if( isAncestor ) continue;
				if( !dest->intersects( src ) ) continue;
				conflict = true;
				break;
			}
			if( !conflict )
			{
				dest->setAllocatedRegister( preferred );
				continue;
			}
		}

		// With --split-dead-float-ranges the lowest-numbered acceptable
		// register is the wrong answer, so every acceptable one is collected
		// and preferSpreadRegister picks.  Without the flag the vector is
		// short-circuited on the first hit and the behaviour is unchanged,
		// byte for byte.
		const bool spread = vuSpreadFloatRegistersEnabled()
		                    && dest->type() == Alias::FLOAT
		                    && ( !vuSpreadFloatRegistersWebsOnly() || aliasIsSplitWeb( dest ) );
		std::vector<const Register*> acceptable;

		for( unsigned int j = 0; j < maxLimit; ++j )
		{
			const Register* candidate = (dest->type() == Alias::FLOAT) ? &m_floats[j] : &m_integers[j];

			if( !candidate->available() )
				continue;

			for( AliasMap::iterator k = m_aliases.begin(); k != m_aliases.end(); ++k )
			{
				Alias* src = k->first;

				if( !src->allocatedRegister() )
					continue;

				if( src->type() != dest->type() )
					continue;

				if( src->allocatedRegister() != candidate )
					continue;

				if( !dest->intersects( src ) )
					continue;

				candidate = NULL;
				break;
			}

			if( candidate )
			{
				if( !spread )
				{
					dest->setAllocatedRegister( candidate );
					break;
				}
				acceptable.push_back( candidate );
			}
		}

		if( spread && !acceptable.empty() )
		{
			std::vector<Alias*> group;
			group.push_back( dest );
			dest->setAllocatedRegister( preferSpreadRegister( group, acceptable ) );
		}

		if( !dest->allocatedRegister() )
		{
			if( m_showRegisterInfo )
			{
				std::cerr << "Failed to allocate " << (dest->type() == Alias::FLOAT ? "FLOAT" : "INTEGER")
				          << " register for alias " << (dest->debugName().empty() ? "?" : dest->debugName())
				          << " #" << dest->id() << std::endl;
			}
			return false;
		}
		//assert( dest->allocatedRegister() );
	}

	if( m_showRegisterInfo )
	{
		std::cerr << std::endl << "=== Final assignment ===" << std::endl;
		for( AliasMap::iterator i = m_aliases.begin(); i != m_aliases.end(); ++i )
		{
			Alias* alias = i->first;
			std::cerr << alias->allocatedRegister()->name() << " <- "
			          << (alias->debugName().empty() ? "?" : alias->debugName())
			          << " #" << alias->id() << "  ";
			alias->printRanges( std::cerr );
			std::cerr << std::endl;
		}
		std::cerr << "====================" << std::endl << std::endl;
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace
{
	void extendContinuationBlockLiveRanges( std::list<Token>::iterator blockStart, std::list<Token>::iterator blockEnd )
	{
		for( std::list<Token>::iterator cont = blockStart; cont != blockEnd; ++cont )
		{
			if( !cont->operand() || cont->operand()->name() != "--cont" )
				continue;

			const unsigned int contLine = cont->lineNumber();
			unsigned int blockEndLine = contLine;
			std::set<Alias*> writtenBeforeCont;
			std::set<Alias*> liveAcrossCont;

			for( std::list<Token>::iterator t = blockStart; t != blockEnd; ++t )
			{
				if( t->lineNumber() > blockEndLine )
					blockEndLine = t->lineNumber();

				for( std::list<Token::Argument>::const_iterator a = t->arguments().begin(); a != t->arguments().end(); ++a )
				{
					if( a->content() != Token::Argument::ALIAS || !a->dependency() || !a->dependency()->alias() )
						continue;

					Alias* alias = a->dependency()->alias();
					if( t->lineNumber() < contLine && (a->flags() & Token::Argument::WRITE) )
						writtenBeforeCont.insert( alias );
					else if( t->lineNumber() > contLine
					         && !(a->flags() & Token::Argument::WRITE)
					         && writtenBeforeCont.find( alias ) != writtenBeforeCont.end() )
						liveAcrossCont.insert( alias );
				}
			}

			for( std::set<Alias*>::iterator a = liveAcrossCont.begin(); a != liveAcrossCont.end(); ++a )
				(*a)->addRange( contLine, blockEndLine );
		}
	}
}

void RegisterAllocator::extendContinuationLiveRanges( std::list<Token>& tokens )
{
	std::list<Token>::iterator blockStart = tokens.end();
	for( std::list<Token>::iterator i = tokens.begin(); i != tokens.end(); ++i )
	{
		if( !i->operand() )
			continue;

		if( i->operand()->name() == "--endenter" )
		{
			blockStart = i;
			++blockStart;
			continue;
		}

		const bool startsNewBlock = (i->operand()->unit() == Operand::ENTER);
		const bool startsExitBlock = (i->operand()->unit() == Operand::EXIT);
		if( blockStart == tokens.end() || (!startsNewBlock && !startsExitBlock) )
			continue;

		std::list<Token>::iterator blockEnd = i;
		extendContinuationBlockLiveRanges( blockStart, blockEnd );

		blockStart = tokens.end();
		if( startsNewBlock )
		{
			blockStart = i;
			++blockStart;
		}
	}

	if( blockStart != tokens.end() )
		extendContinuationBlockLiveRanges( blockStart, tokens.end() );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RegisterAllocator::loopTargetHasLoopDirective( std::list<Token>::iterator target, std::list<Token>::iterator end ) const
{
	if( target == end )
		return false;

	if( target->operand() && target->operand()->name() == "--LoopCS" )
		return true;

	std::list<Token>::iterator next = target;
	++next;
	if( next != end && next->operand() && next->operand()->name() == "--LoopCS" )
		return true;

	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RegisterAllocator::extendLoopDirectiveRange( std::list<Token>& tokens, unsigned int loopStart, unsigned int loopEnd )
{
	// Which aliases does this loop body touch, and is the FIRST touch a read?
	// Only a read-first alias is live across the back edge - the next iteration
	// depends on what the previous one left in it. An alias written before it is read
	// inside the body is a temporary and its own range already covers it; stretching
	// those over the whole loop is what runs the allocator out of registers.
	//
	// Integers are collected on the same terms as floats. Tying their carried writes is
	// not enough on its own: an integer whose live range stops at its last use inside
	// the body has its register handed to another name for the rest of the loop, so the
	// next iteration reads whatever that name left. The range has to cover the back edge
	// too - the file is only 16 registers, so this is where a program that is already
	// tight will fail loudly instead of quietly computing the wrong thing.
	// Two sets, because the two things this function does have different costs.
	// liveInAliases pays for a live range stretched over the whole body and is what
	// runs a tight program out of registers, so it stays exactly as it was: whole
	// aliases, first touch decides. carriedAliases only decides which in-loop WRITES
	// get tied to the alias the readers at the top hold, which lengthens no range -
	// it merges two - so it can afford to ask the sharper question below.
	std::set<Alias*> aliases;
	std::set<Alias*> liveInAliases;
	std::set<Alias*> carriedAliases;
	std::set<Alias*> touched;
	// The sharper question is per FIELD, because a write need not be a definition.
	// `mul.w carry, ...` names one field; the other three still hold what the
	// previous iteration left there, so a later `carry[z]` is carried across the
	// back edge even though the body wrote `carry` first. Reading that as "defined
	// in the loop" is what puts the update at the bottom in a register the reader at
	// the top never looks at. Integers have no fields and vuReadFieldMask answers
	// xyzw for them, so they keep exactly the old meaning: any read before a write.
	std::map<std::string, unsigned int> writtenFields;
	for( std::list<Token>::iterator t = tokens.begin(); t != tokens.end(); ++t )
	{
		if( t->lineNumber() < loopStart || t->lineNumber() > loopEnd )
			continue;

		std::set<Alias*> readHere;
		std::set<Alias*> tokenAliases;
		for( std::list<Token::Argument>::const_iterator a = t->arguments().begin(); a != t->arguments().end(); ++a )
		{
			if( a->content() != Token::Argument::ALIAS || !a->dependency() || !a->dependency()->alias() )
				continue;
			Alias* alias = a->dependency()->alias();
			aliases.insert( alias );
			tokenAliases.insert( alias );
			if( !(a->flags() & Token::Argument::WRITE) )
			{
				readHere.insert( alias );
				if( !a->alias().empty()
				    && (vuReadFieldMask( *t, *a ) & ~writtenFields[ a->alias() ]) != 0 )
					carriedAliases.insert( alias );
			}
		}

		// Reads are scored against the mask BEFORE this token's own writes join it,
		// so `add a, a, b` reads what the previous iteration left - the same reason
		// the read wins over the write below.
		for( std::list<Token::Argument>::const_iterator a = t->arguments().begin(); a != t->arguments().end(); ++a )
		{
			if( !(a->flags() & Token::Argument::WRITE) )
				continue;
			if( a->content() != Token::Argument::ALIAS || a->alias().empty() )
				continue;
			writtenFields[ a->alias() ] |= vuWriteFieldMask( *t, *a );
		}

		// One token can both read and write the same alias (`add a, a, b`), and that
		// still reads what the previous iteration left, so the read wins.
		for( std::set<Alias*>::iterator i = tokenAliases.begin(); i != tokenAliases.end(); ++i )
		{
			if( touched.find( *i ) != touched.end() )
				continue;
			touched.insert( *i );
			if( readHere.find( *i ) != readHere.end() )
				liveInAliases.insert( *i );
		}
	}
	// Never fewer than before: whatever first-touch liveness already called carried
	// stays carried, and the field walk only adds to it.
	for( std::set<Alias*>::iterator i = liveInAliases.begin(); i != liveInAliases.end(); ++i )
		carriedAliases.insert( *i );
	// ONLY THE READ-FIRST NAMES ARE LIVE ACROSS THE BACK EDGE. A name written
	// before it is read inside the body is a temporary whose own range already
	// covers it, and stretching those over the whole loop is what used to run the
	// allocator out of registers. That narrowing was --loop-liveness-always's
	// alone; the default path kept the wider set and an early return below to
	// catch the overflow the wider set caused.
	//
	// SKIPPING THE EXTENSION IS NOT A LICENCE TO EMIT WRONG CODE, and that early
	// return did exactly that. It compared the number of aliases with a range
	// OVERLAPPING the loop against the register file - a set it does not extend:
	// what gets addRange() below is this read-first set, while the count included
	// every short-lived temporary in the body. So it fired on loops whose
	// extension was free, and when it fired it dropped the extension for the
	// carried names that needed it. A value written at the bottom of the body and
	// read at the top then had its register handed to a temporary, and every
	// iteration after the first read what the temporary left.
	//
	// Measured, on a `--LoopCS` loop with 41 float aliases overlapping it and one
	// carried name: with the early return the emitted program diverges from its
	// own source under both pb-dag and pd-cond; without it the same program is
	// clean AND still allocates. The same loop with two fewer temporaries never
	// reached the guard and was correct all along. See the regression cases
	// loop_pressure_carry and loop_pressure_carry_ok.
	//
	// When the extension genuinely does not fit, failing loudly is the only honest
	// answer - which is what --loop-liveness-always has always done here.
	aliases = liveInAliases;

	for( std::set<Alias*>::iterator a = aliases.begin(); a != aliases.end(); ++a )
		(*a)->addRange( loopStart, loopEnd );

	tieCarriedWritesToLiveInAliases( tokens, loopStart, loopEnd, carriedAliases );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RegisterAllocator::tieCarriedWritesToLiveInAliases( std::list<Token>& tokens,
                                                         unsigned int loopStart, unsigned int loopEnd,
                                                         const std::set<Alias*>& liveInAliases )
{
	// A name read before it is written inside the body is carried across the back
	// edge, and the readers at the top of the loop hold ONE alias for it. But a
	// fresh Alias is spawned for every write, so the update at the bottom of the
	// loop is a different alias - free to land in a different register. The write
	// then goes nowhere the reader looks: the next iteration sees what the previous
	// one saw, forever.
	//
	// Worse, those tail aliases get a live range one line long - the line of the
	// write itself - because nothing downstream reads them. Two such ranges do not
	// intersect, so the allocator may legally put two different carried names in one
	// register, where the second write destroys the first.
	//
	// Both follow from the same omission, so both have one fix: tie every in-loop
	// write of a carried name to the alias its readers use. openvcl already has the
	// machinery - the two-address chain, which exists so `isubiu x, x, 1` writes the
	// register it read - and the chain pre-pass then hands the whole chain a single
	// register.
	std::map<std::string, Alias*> carriedByName;
	for( std::list<Token>::iterator t = tokens.begin(); t != tokens.end(); ++t )
	{
		if( t->lineNumber() < loopStart || t->lineNumber() > loopEnd )
			continue;
		for( std::list<Token::Argument>::const_iterator a = t->arguments().begin(); a != t->arguments().end(); ++a )
		{
			if( a->content() != Token::Argument::ALIAS || !a->dependency() || !a->dependency()->alias() )
				continue;
			if( a->alias().empty() )
				continue;
			Alias* alias = a->dependency()->alias();
			if( liveInAliases.find( alias ) == liveInAliases.end() )
				continue;
			if( carriedByName.find( a->alias() ) == carriedByName.end() )
				carriedByName[ a->alias() ] = alias;
		}
	}

	for( std::list<Token>::iterator t = tokens.begin(); t != tokens.end(); ++t )
	{
		if( t->lineNumber() < loopStart || t->lineNumber() > loopEnd )
			continue;
		for( std::list<Token::Argument>::const_iterator a = t->arguments().begin(); a != t->arguments().end(); ++a )
		{
			if( !(a->flags() & Token::Argument::WRITE) )
				continue;
			if( a->content() != Token::Argument::ALIAS || !a->dependency() || !a->dependency()->alias() )
				continue;
			Alias* written = a->dependency()->alias();
			std::map<std::string, Alias*>::iterator carried = carriedByName.find( a->alias() );
			if( carried == carriedByName.end() )
				continue;
			if( written == carried->second )
				continue;
			// An existing chain already says where this write must live; a carried
			// name cannot claim it too without contradicting the earlier link.
			if( written->sameNamePredecessor() )
				continue;
			// Through trySetSameNamePredecessor, not the raw setter, because this
			// runs ONCE PER BACK EDGE and the ranges of two back edges need not
			// nest. Two overlapping loops disagree about which alias of a name is
			// the one its readers hold - each picks the first in ITS OWN range -
			// so loop A can tie X to Y and loop B tie Y back to X. The chain is
			// then a ring with no root, and processAliases walks it forever.
			// Refusing the closing edge costs nothing: an edge that closes a ring
			// runs between two aliases that are ALREADY on one chain, which is the
			// whole of what tying them was for - they reach one root and the
			// pre-pass hands the ring one register either way.
			trySetSameNamePredecessor( written, carried->second );
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RegisterAllocator::extendLoopDirectiveLiveRanges( std::list<Token>& tokens )
{
	for( std::list<Token>::iterator branch = tokens.begin(); branch != tokens.end(); ++branch )
	{
		if( !branch->operand() || branch->operand()->unit() != Operand::BRU )
			continue;

		std::list<Token::Argument>::const_iterator branchDest = branch->arguments().end();
		for( std::list<Token::Argument>::const_iterator a = branch->arguments().begin(); a != branch->arguments().end(); ++a )
		{
			if( a->flags() & Token::Argument::BRANCH )
			{
				branchDest = a;
				break;
			}
		}
		if( branchDest == branch->arguments().end() || branchDest->type() != Token::Argument::IMMEDIATE )
			continue;

		std::map< std::string, std::list<Token>::iterator >::iterator label = m_labels.find( branchDest->immediate() );
		if( label == m_labels.end() )
			continue;

		std::list<Token>::iterator target = label->second;
		if( target->lineNumber() >= branch->lineNumber() )
			continue;
		// A back edge is a back edge whether or not the source marked it with a --loop
		// directive: a value written at the bottom and read at the top is live across
		// it either way. Requiring the directive means a plain `label: ... ibne label`
		// loop - which is what hand-written VU code looks like - gets no extension at
		// all, and the allocator then hands one register to two live names.
		// See --loop-liveness-always.
		if( !vuLoopLivenessAlwaysEnabled()
		    && !loopTargetHasLoopDirective( target, tokens.end() ) )
			continue;

		extendLoopDirectiveRange( tokens, target->lineNumber(), branch->lineNumber() );
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace
{
	struct MultiQStage
	{
		MultiQStage()
		{
			producerLine = 0;
		}

		unsigned int producerLine;
		std::vector<unsigned int> consumerLines;
	};

	bool tokenTouchesQ( const Token& token, bool& readsQ, bool& writesQ )
	{
		readsQ = false;
		writesQ = false;
		for( std::list<Token::Argument>::const_iterator a = token.arguments().begin();
		     a != token.arguments().end(); ++a )
		{
			if( a->type() != Token::Argument::Q )
				continue;
			if( a->flags() & Token::Argument::WRITE )
				writesQ = true;
			else
				readsQ = true;
		}
		return readsQ || writesQ;
	}

	void collectFloatAliasesFromToken( const Token& token, std::set<Alias*>& aliases )
	{
		for( std::list<Token::Argument>::const_iterator a = token.arguments().begin();
		     a != token.arguments().end(); ++a )
		{
			if( a->type() != Token::Argument::FLOAT_REGISTER )
				continue;
			if( a->content() != Token::Argument::ALIAS || !a->dependency() || !a->dependency()->alias() )
				continue;
			Alias* alias = a->dependency()->alias();
			if( alias->type() == Alias::FLOAT )
				aliases.insert( alias );
		}
	}

	bool stringListContains( const std::list<std::string>& values, const std::string& value )
	{
		for( std::list<std::string>::const_iterator i = values.begin(); i != values.end(); ++i )
		{
			if( *i == value )
				return true;
		}
		return false;
	}

	bool stringListsIntersect( const std::list<std::string>& a, const std::list<std::string>& b )
	{
		for( std::list<std::string>::const_iterator i = a.begin(); i != a.end(); ++i )
		{
			if( stringListContains( b, *i ) )
				return true;
		}
		return false;
	}

	void addUniqueStrings( std::list<std::string>& dest, const std::list<std::string>& src )
	{
		for( std::list<std::string>::const_iterator i = src.begin(); i != src.end(); ++i )
		{
			if( !stringListContains( dest, *i ) )
				dest.push_back( *i );
		}
	}

	void addAliasRange( const std::set<Alias*>& aliases, unsigned int beginLine, unsigned int endLine )
	{
		if( beginLine > endLine )
			return;
		for( std::set<Alias*>::const_iterator a = aliases.begin(); a != aliases.end(); ++a )
			(*a)->addRange( beginLine, endLine );
	}

	void collectMultiQStages( std::list<Token>& tokens,
	                          unsigned int loopStart,
	                          unsigned int loopEnd,
	                          std::vector<MultiQStage>& stages,
	                          unsigned int& qReads,
	                          unsigned int& qWrites )
	{
		qReads = 0;
		qWrites = 0;
		for( std::list<Token>::iterator t = tokens.begin(); t != tokens.end(); ++t )
		{
			if( t->lineNumber() < loopStart || t->lineNumber() > loopEnd )
				continue;

			bool readsQ = false;
			bool writesQ = false;
			if( !tokenTouchesQ( *t, readsQ, writesQ ) )
				continue;

			if( writesQ )
			{
				MultiQStage stage;
				stage.producerLine = t->lineNumber();
				stages.push_back( stage );
				++qWrites;
			}

			if( readsQ )
			{
				++qReads;
				if( !stages.empty() )
					stages.back().consumerLines.push_back( t->lineNumber() );
			}
		}
	}

	void collectMultiQProducerDependencySliceAliases( std::list<Token>& tokens,
	                                                  unsigned int beginLine,
	                                                  unsigned int producerLine,
	                                                  std::set<Alias*>& aliases )
	{
		std::vector<Token*> rangeTokens;
		for( std::list<Token>::iterator t = tokens.begin(); t != tokens.end(); ++t )
		{
			if( t->lineNumber() < beginLine || t->lineNumber() > producerLine )
				continue;
			rangeTokens.push_back( &*t );
		}
		if( rangeTokens.empty() )
			return;

		VuTokenResourceAccess producerAccess;
		if( !buildVuTokenResourceAccess( *rangeTokens.back(), producerAccess ) )
			return;

		std::list<std::string> neededRegisters = producerAccess.registerReads;
		unsigned int neededResources = producerAccess.implicitReads;
		collectFloatAliasesFromToken( *rangeTokens.back(), aliases );

		for( unsigned int reverse = static_cast<unsigned int>( rangeTokens.size() ); reverse > 0; --reverse )
		{
			Token& token = *rangeTokens[reverse - 1];
			VuTokenResourceAccess access;
			if( !buildVuTokenResourceAccess( token, access ) )
				continue;

			const bool feedsNeededRegister = stringListsIntersect( access.registerWrites, neededRegisters );
			const bool feedsNeededResource = (access.implicitWrites & neededResources) != 0;
			if( !feedsNeededRegister && !feedsNeededResource )
				continue;

			collectFloatAliasesFromToken( token, aliases );
			addUniqueStrings( neededRegisters, access.registerReads );
			neededResources |= access.implicitReads;
		}
	}

	void extendAdjacentMultiQStageAliases( std::list<Token>& tokens,
	                                       const std::vector<MultiQStage>& stages,
	                                       unsigned int loopEnd )
	{
		if( stages.size() < 2 )
			return;

		for( unsigned int stage = 1; stage < stages.size(); ++stage )
		{
			if( stages[stage - 1].consumerLines.empty() )
				continue;

			const unsigned int previousProducerLine = stages[stage - 1].producerLine;
			const unsigned int previousLastConsumerLine = stages[stage - 1].consumerLines.back();
			const unsigned int currentProducerLine = stages[stage].producerLine;
			if( previousProducerLine == 0
			    || currentProducerLine == 0
			    || previousLastConsumerLine >= currentProducerLine )
				continue;

			std::set<Alias*> aliases;
			collectMultiQProducerDependencySliceAliases( tokens,
			                                             previousLastConsumerLine + 1,
			                                             currentProducerLine,
			                                             aliases );
			addAliasRange( aliases, previousProducerLine, currentProducerLine );
		}
		(void)loopEnd;
	}
}

void RegisterAllocator::extendMultiQStageRange( std::list<Token>& tokens, unsigned int loopStart, unsigned int loopEnd )
{
	std::set<Alias*> qStageAliases;
	std::vector<MultiQStage> stages;
	unsigned int qReads = 0;
	unsigned int qWrites = 0;

	collectMultiQStages( tokens, loopStart, loopEnd, stages, qReads, qWrites );
	if( qWrites > 1 && qReads > 0 )
		extendAdjacentMultiQStageAliases( tokens, stages, loopEnd );

	for( std::list<Token>::iterator t = tokens.begin(); t != tokens.end(); ++t )
	{
		if( t->lineNumber() < loopStart || t->lineNumber() > loopEnd )
			continue;

		bool readsQ = false;
		bool writesQ = false;
		if( !tokenTouchesQ( *t, readsQ, writesQ ) )
			continue;

		collectFloatAliasesFromToken( *t, qStageAliases );
	}

	if( qWrites <= 1 || qReads == 0 || qStageAliases.empty() )
		return;

	unsigned int availableFloats = 0;
	for( unsigned int i = 0; i < 32; ++i )
	{
		if( m_floats[i].available() )
			++availableFloats;
	}

	std::set<Alias*> overlappingAliases;
	for( AliasMap::iterator i = m_aliases.begin(); i != m_aliases.end(); ++i )
	{
		Alias* alias = i->first;
		if( alias->type() != Alias::FLOAT )
			continue;
		if( qStageAliases.find( alias ) != qStageAliases.end() || alias->hasRangeOverlapping( loopStart, loopEnd ) )
			overlappingAliases.insert( alias );
	}

	// Skipping the extension is not a licence to emit wrong code: a value live
	// across the back edge whose range was not extended gets its register handed
	// to another name. --loop-liveness-always keeps the extension and lets
	// allocation fail loudly instead.
	if( !vuLoopLivenessAlwaysEnabled()
	    && overlappingAliases.size() > availableFloats )
		return;

	for( std::set<Alias*>::iterator a = qStageAliases.begin(); a != qStageAliases.end(); ++a )
		(*a)->addRange( loopStart, loopEnd );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RegisterAllocator::extendMultiQStageLiveRanges( std::list<Token>& tokens )
{
	for( std::list<Token>::iterator branch = tokens.begin(); branch != tokens.end(); ++branch )
	{
		if( !branch->operand() || branch->operand()->unit() != Operand::BRU )
			continue;

		std::list<Token::Argument>::const_iterator branchDest = branch->arguments().end();
		for( std::list<Token::Argument>::const_iterator a = branch->arguments().begin(); a != branch->arguments().end(); ++a )
		{
			if( a->flags() & Token::Argument::BRANCH )
			{
				branchDest = a;
				break;
			}
		}
		if( branchDest == branch->arguments().end() || branchDest->type() != Token::Argument::IMMEDIATE )
			continue;

		std::map< std::string, std::list<Token>::iterator >::iterator label = m_labels.find( branchDest->immediate() );
		if( label == m_labels.end() )
			continue;

		std::list<Token>::iterator target = label->second;
		if( target->lineNumber() >= branch->lineNumber() )
			continue;
		if( !loopTargetHasLoopDirective( target, tokens.end() ) )
			continue;

		extendMultiQStageRange( tokens, target->lineNumber(), branch->lineNumber() );
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RegisterAllocator::collectLiteralRegisterUsage( std::list<Token>& tokens )
{
	// One synthetic alias per (type, register) pair, with one range per
	// usage line PLUS one range per definition-to-use span.  Inserted into
	// m_aliases pre-allocated to its physical register so the existing conflict
	// check in processAliases naturally keeps user aliases off it.
	//
	// The span is what makes this a live range rather than a set of points, and
	// it is not decoration.  VI01 is the only legal destination of fcand/fcor/
	// fceq, so a clip judgement has to be read back out of VI01, and between the
	// two the register holds a value.  Recorded as two points that range list
	// merges only while they are ADJACENT - addRange treats a one-line gap as
	// contiguous - so as long as the flag is read on the next line the two
	// spellings agree and nothing here changes.  Open one row between them, which
	// --branch-interlock does by inserting the bubble the hardware needs, and the
	// point spelling says VI01 is free in the middle: --sink-loads then drops a
	// load whose value is dead into that row, the allocator has already given it
	// VI01 because a dead value's range is one line long and does not intersect
	// either point, and the ibne below tests the load.
	//
	// A span is only ever added from a write to a LATER read, so a register that
	// is only ever read (VI00, and anything the caller passes in) keeps exactly
	// the ranges it had.  Reads on a path the write does not reach make the span
	// too long rather than too short, which can cost a register but cannot lose
	// a value.
	Alias* floats[32];
	Alias* integers[16];
	unsigned int integerWrittenAt[16];
	for( unsigned int i = 0; i < 32; i++ ) floats[i]   = NULL;
	for( unsigned int i = 0; i < 16; i++ ) { integers[i] = NULL; integerWrittenAt[i] = 0; }

	for( std::list<Token>::iterator it = tokens.begin(); it != tokens.end(); ++it )
	{
		if( !it->operand() )
			continue;
		// Preprocessor directives (--enter, in_vf, .name, etc.) don't emit
		// hardware ops — their register references shouldn't pin physical
		// registers across the whole program.
		if( it->operand()->flags() & Operand::PREPROCESSOR )
			continue;
		if( it->flags() & Token::IGNORED )
			continue;

		const unsigned int line = it->lineNumber();

		// Reads before writes, so `iaddiu VI01, VI01, 2` closes the span it was
		// standing in before it opens the next one - the same "the read wins"
		// convention the branch-state analysis uses for aliases.
		for( int pass = 0; pass < 2; ++pass )
		for( std::list<Token::Argument>::const_iterator a = it->arguments().begin(); a != it->arguments().end(); ++a )
		{
			if( a->content() != Token::Argument::REGISTER )
				continue;
			const bool write = (a->flags() & Token::Argument::WRITE) != 0;
			if( write != (pass == 1) )
				continue;

			if( a->type() == Token::Argument::FLOAT_REGISTER )
			{
				// Float side intentionally disabled — adding synthetic
				// VF aliases (even just for vf00, the zero register)
				// shifts the allocator's choices and ends up splitting
				// `vert_xform` between two VF sets across the [E]
				// halt boundary (load into VF05..VF08, use in
				// xform_loop as VF01..VF04 = garbage).  The integer-
				// side fix below is the one that matters for the
				// originally-motivating `do_clipping` / VI01 bug.
				(void)a;
				continue;
			}
			else if( a->type() == Token::Argument::INTEGER_REGISTER )
			{
				int r = a->regNumber();
				if( r < 0 || r >= 16 )
					continue;
				if( !integers[r] )
				{
					integers[r] = new Alias( Alias::INTEGER );
					integers[r]->setAllocatedRegister( &m_integers[r] );
					m_aliases[ integers[r] ] = integers[r];
				}
				integers[r]->addRange( line, line );
				if( write )
					integerWrittenAt[r] = line;
				else if( integerWrittenAt[r] && integerWrittenAt[r] < line )
					integers[r]->addRange( integerWrittenAt[r], line );
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace
{
	// One reference to an alias: which line, which components, and whether it
	// was the destination.  Sources are recorded before the destination of the
	// same token, which is the order the hardware sees them in.
	struct AliasAccess
	{
		AliasAccess() : m_line(0), m_fields(0), m_write(false) {}
		AliasAccess( unsigned int line, unsigned int fields, bool write )
			: m_line(line), m_fields(fields), m_write(write) {}

		unsigned int m_line;
		unsigned int m_fields;
		bool m_write;
	};

	// Which components an operand really touches.  A float source written
	// without a selector is read under the instruction's destination mask - a
	// `mini.xyz d,s,t` never looks at s.w - and openvcl leaves such an operand's
	// field mask at zero, meaning "whatever the instruction says". Reading that
	// zero as "all four" would make every masked read look like a read of an
	// undefined w. An integer register has no components at all, so it is always
	// whole.
	unsigned int accessFields( const Token& token, const Token::Argument& argument )
	{
		const unsigned int all = Token::X | Token::Y | Token::Z | Token::W;
		if( argument.type() == Token::Argument::INTEGER_REGISTER )
			return all;
		if( argument.fields() )
			return argument.fields();
		return token.fields() ? token.fields() : all;
	}

	bool tokenIsEmittable( const Token& token )
	{
		if( !token.operand() )
			return false;
		if( token.operand()->flags() & Operand::PREPROCESSOR )
			return false;
		if( token.flags() & Token::IGNORED )
			return false;
		return true;
	}

	// Every backward branch in the program, as [target line, branch line].
	void collectBackEdges( std::list<Token>& tokens,
	                       const std::map< std::string, std::list<Token>::iterator >& labels,
	                       std::vector< std::pair<unsigned int, unsigned int> >& loops )
	{
		for( std::list<Token>::iterator branch = tokens.begin(); branch != tokens.end(); ++branch )
		{
			if( !branch->operand() || branch->operand()->unit() != Operand::BRU )
				continue;

			std::list<Token::Argument>::const_iterator dest = branch->arguments().end();
			for( std::list<Token::Argument>::const_iterator a = branch->arguments().begin();
			     a != branch->arguments().end(); ++a )
			{
				if( a->flags() & Token::Argument::BRANCH )
				{
					dest = a;
					break;
				}
			}
			if( dest == branch->arguments().end() || dest->type() != Token::Argument::IMMEDIATE )
				continue;

			std::map< std::string, std::list<Token>::iterator >::const_iterator label =
				labels.find( dest->immediate() );
			if( label == labels.end() )
				continue;

			if( label->second->lineNumber() >= branch->lineNumber() )
				continue;

			loops.push_back( std::make_pair( label->second->lineNumber(), branch->lineNumber() ) );
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace
{
	// "vertex1.x" -> "vertex1".  buildVuTokenResourceAccess reports a float
	// operand one component at a time; the sink pass cares about the value.
	std::string sinkRegisterBase( const std::string& key )
	{
		const std::string::size_type dot = key.rfind( '.' );
		if( std::string::npos == dot )
			return key;
		return key.substr( 0, dot );
	}

	// A load with no side effect beyond its destination: plain `lq`, not `lqi`
	// or `lqd`, which also write the address register, and not one whose
	// destination is a hardcoded VF register (nothing to relieve there).
	bool tokenIsSinkableLoad( const Token& token, std::string& dest, std::string& base,
	                          bool& hasOffset, long& offset )
	{
		hasOffset = false;
		offset = 0;
		if( !token.operand() )
			return false;
		if( token.operand()->flags() & Operand::PREPROCESSOR )
			return false;
		if( token.flags() & (Token::PREORDERED | Token::E | Token::D | Token::T
		                     | Token::IGNORED | Token::BRANCH_DELAY_FILLER) )
			return false;
		if( !token.label().empty() )
			return false;

		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess( token, access ) )
			return false;
		if( access.memoryKind != VU_MEMORY_LOAD )
			return false;
		if( access.memoryFlags & (VU_MEMORY_FLAG_PREDEC | VU_MEMORY_FLAG_POSTINC) )
			return false;
		if( access.implicitWrites != VU_RESOURCE_NONE || access.implicitReads != VU_RESOURCE_NONE )
			return false;
		if( !access.hasMemoryBase )
			return false;
		if( access.registerWrites.empty() )
			return false;

		dest.clear();
		for( std::list<std::string>::const_iterator i = access.registerWrites.begin();
		     i != access.registerWrites.end(); ++i )
		{
			const std::string b = sinkRegisterBase( *i );
			if( dest.empty() )
				dest = b;
			else if( dest != b )
				return false;
		}

		// Only an alias is worth moving, and only an alias can be reasoned about
		// here - a literal VFxx is already pinned to its register.
		for( std::list<Token::Argument>::const_iterator a = token.arguments().begin();
		     a != token.arguments().end(); ++a )
		{
			if( !(a->flags() & Token::Argument::WRITE) )
				continue;
			if( a->content() != Token::Argument::ALIAS )
				return false;
		}

		base = access.memoryBaseRegister;
		hasOffset = access.hasMemoryOffset;
		offset = access.memoryOffset;
		return true;
	}

	// The label a branch names, for the one shape --sink-loads-past-branches
	// reasons about: a single immediate target and nothing written. That is
	// every conditional branch and a plain `b`. `bal` writes its link register
	// and `jr` has no immediate target at all; neither is a forward edge inside
	// a straight-line span, and both leave through the empty string.
	std::string sinkBranchTargetLabel( const Token& token )
	{
		std::string target;
		for( std::list<Token::Argument>::const_iterator a = token.arguments().begin();
		     a != token.arguments().end(); ++a )
		{
			if( a->flags() & Token::Argument::WRITE )
				return std::string();
			if( !(a->flags() & Token::Argument::BRANCH) )
				continue;
			if( a->type() != Token::Argument::IMMEDIATE || a->immediate().empty() )
				return std::string();
			if( !target.empty() )
				return std::string();
			target = a->immediate();
		}
		return target;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace
{
	const unsigned int ALL_FIELDS = Token::X | Token::Y | Token::Z | Token::W;

	// Every value the token reads and writes, keyed by name, with the field mask
	// for each. Built from the arguments rather than from the flattened
	// per-component key lists buildVuTokenResourceAccess produces, because this
	// pass has to know whether a key came from a float - where a trailing ".x" is
	// a component - or from an integer, where it could only be part of a name.
	// `aliasOnly` comes back false as soon as one destination is a literal VFxx:
	// a register the author named by number may be an interface with something
	// this compiler cannot see, and is never a candidate for deletion.
	void collectDeadWriteAccess( const Token& token,
	                             std::map<std::string, unsigned int>& reads,
	                             std::map<std::string, unsigned int>& writes,
	                             bool& aliasOnly )
	{
		for( std::list<Token::Argument>::const_iterator a = token.arguments().begin();
		     a != token.arguments().end(); ++a )
		{
			std::string key;
			if( !vuRegisterKey( *a, key ) )
				continue;

			const bool isFloat = ( a->type() == Token::Argument::FLOAT_REGISTER );

			if( a->flags() & Token::Argument::WRITE )
			{
				if( a->content() != Token::Argument::ALIAS )
					aliasOnly = false;
				writes[key] |= isFloat ? vuWriteFieldMask( token, *a ) : ALL_FIELDS;
			}
			else
			{
				reads[key] |= isFloat ? vuReadFieldMask( token, *a ) : ALL_FIELDS;
			}
		}
	}

	// Can this token be considered for deletion at all? Everything with an effect
	// the destination register does not describe is out: stores and xgkick, the
	// auto-incrementing loads, branches and anything with a delay slot, a token
	// carrying a label (its branches would lose their target), and every token a
	// previous pass pinned.
	bool tokenIsDeadWriteCandidate( const Token& token, VuTokenResourceAccess& access )
	{
		if( !token.operand() )
			return false;
		if( token.operand()->flags() & Operand::PREPROCESSOR )
			return false;
		if( token.operand()->unit() == Operand::ENTER
		    || token.operand()->unit() == Operand::EXIT
		    || token.operand()->unit() == Operand::BRU )
			return false;
		if( token.flags() & (Token::PREORDERED | Token::E | Token::D | Token::T
		                     | Token::IGNORED | Token::BRANCH_DELAY_FILLER
		                     | Token::KERNEL_BLOCK_BEGIN | Token::KERNEL_BLOCK_END) )
			return false;
		if( !token.label().empty() )
			return false;
		if( !buildVuTokenResourceAccess( token, access ) )
			return false;
		if( access.memoryKind == VU_MEMORY_STORE || access.memoryKind == VU_MEMORY_XGKICK )
			return false;
		if( access.memoryFlags & (VU_MEMORY_FLAG_PREDEC | VU_MEMORY_FLAG_POSTINC) )
			return false;
		if( access.branchDelaySlots != 0 )
			return false;
		return true;
	}

	// Is this token a CLIP *push* rather than a CLIP overwrite? CLIP/CLIPw/CLIPLw
	// shift six new judgement bits into a 24-bit window and move the previous
	// three entries up; FCSET replaces the whole register. Matched on the
	// mnemonic because that is what survives to this point - the opcode table has
	// no "is a shift" bit and adding one would touch every entry.
	bool tokenIsClipPush( const Token& token )
	{
		if( !token.operand() )
			return false;
		std::string name = token.operand()->name();
		if( name.size() < 4 )
			return false;
		for( std::string::size_type i = 0; i < 4; ++i )
			name[i] = static_cast<char>( tolower( static_cast<unsigned char>( name[i] ) ) );
		return name.compare( 0, 4, "clip" ) == 0;
	}

	// The CLIP window is four 6-bit entries deep, so a judgement pushed here is
	// still readable through the next three pushes and gone after the fourth.
	const unsigned int VU_CLIP_WINDOW_ENTRIES = 4;

	// Which fields of ACC a token writes. ACC is four independent 32-bit lanes and
	// an FMAC destination mask selects among them, so `mula.x acc` and
	// `mula.yzw acc` write disjoint halves of it and neither retires the other.
	// vuWriteFieldMask() cannot answer this: it reports the whole register for
	// anything that is not a FLOAT_REGISTER argument, and ACC is its own argument
	// type. Same precedence it uses - the token's destination mask first, then the
	// argument's own, then all four.
	unsigned int accumulatorWriteFieldMask( const Token& token )
	{
		unsigned int fields = token.fields();
		if( fields == 0 )
		{
			for( std::list<Token::Argument>::const_iterator a = token.arguments().begin();
			     a != token.arguments().end(); ++a )
			{
				if( a->type() != Token::Argument::ACCUMULATOR )
					continue;
				if( !(a->flags() & Token::Argument::WRITE) )
					continue;
				fields = a->fields();
				break;
			}
		}
		return fields == 0 ? ALL_FIELDS : fields;
	}

	// Does anything observe this token's write to `resources`? Walks forward from
	// it: a reader says yes, another writer of the same resource says no, and
	// anything the walk cannot follow - a label, a branch, a preprocessor
	// directive, the end of the program - says yes, because the allocator has no
	// dominance information and a wrong answer here is a miscompile.
	//
	// The caller has already masked `resources` down to what the program reads at
	// all, which is what makes this useful: in a program with no MAC reader every
	// FMAC's flag write drops out before the walk starts, and the walk only has
	// to be right about the resources somebody really does read.
	//
	// CLIP is the one resource where "another writer says no" is WRONG, and it
	// is not behind a flag because it is not an option: a `clipw` does not
	// overwrite the previous judgement, it shifts it up one 6-bit entry, where a
	// positional reader (`fcand VI01,0x3CA3CA` names all four entries) still
	// sees it. Only the fourth subsequent push retires it; anything else that
	// writes CLIP (FCSET) really does replace the register and kills it at once.
	//
	// This shipped for one commit as --clip-window-liveness and was folded in
	// here, because a --drop-dead-writes build without it deletes four of the
	// seven `clipw` in every stapip_clip_* program, and the "entirely inside,
	// skip clipping" branch then reads the previous triangle's judgements - a
	// miscompile no pixel or GIF-packet comparison can see.
	//
	// ACC is the third resource where "another writer says no" needs a
	// qualification, and it is a narrower one than CLIP's: ACC really is replaced
	// by a later write, but only in the FIELDS that write names. `mula.x acc`
	// followed by `mula.yzw acc` and then one `madd` reading all four had the FIRST
	// deleted, because the walk saw an ACC writer and never asked which lanes it
	// covered. `pendingAccFields` is that question - the kill only counts once the
	// later writes between here and the next reader cover every field this one
	// wrote.
	bool implicitWriteIsObservable( std::list<Token>::const_iterator token,
	                                std::list<Token>::const_iterator end,
	                                unsigned int resources,
	                                unsigned int accFields )
	{
		unsigned int pending = resources;
		unsigned int pendingAccFields = accFields;
		unsigned int clipPushes = 0;
		for( ++token; token != end && pending != VU_RESOURCE_NONE; ++token )
		{
			if( !token->operand() )
				continue;
			if( token->operand()->flags() & Operand::PREPROCESSOR )
				return true;
			if( !token->label().empty() )
				return true;

			VuTokenResourceAccess access;
			if( !buildVuTokenResourceAccess( *token, access ) )
				return true;
			if( access.implicitReads & pending )
				return true;
			if( token->operand()->unit() == Operand::BRU )
				return true;

			unsigned int kills = access.implicitWrites;
			// An ACCUMULATING write never retires an earlier one: two FMACs both OR
			// into the status register's sticky half, so the first one's contribution
			// is still there for a reader after the second. The only thing that does
			// retire it is FSSET, and FSSET is declared as READING the resource too,
			// so the `implicitReads & pending` test above has already returned true
			// for it by the time control gets here.
			kills &= ~static_cast<unsigned int>( VU_RESOURCE_ACCUMULATING );
			if( (kills & VU_RESOURCE_CLIP) && tokenIsClipPush( *token ) )
			{
				// A push only retires the entry once it has shifted out of the
				// window; until then the old judgement is still readable.
				if( ++clipPushes < VU_CLIP_WINDOW_ENTRIES )
					kills &= ~VU_RESOURCE_CLIP;
			}
			if( kills & VU_RESOURCE_ACC )
			{
				// Only the lanes this write covers are retired. What is left over is
				// still the earlier write's, and still readable.
				pendingAccFields &= ~accumulatorWriteFieldMask( *token );
				if( pendingAccFields != 0 )
					kills &= ~VU_RESOURCE_ACC;
			}
			pending &= ~kills;
		}
		return pending != VU_RESOURCE_NONE;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

unsigned int RegisterAllocator::dropDeadRegisterWrites( std::list<Token>& tokens )
{
	// The VU authoring layer emits a whole constant vector when the program reads
	// two of its components, and a `lq` for every quadword the description names
	// whether or not the body touches it. SCE's vcl drops both; openvcl carried
	// them all the way to micro memory, which is where its generated code was
	// losing to SCE's - measured over the 45 generated TyraX programs, openvcl
	// emitted 312 more instructions than SCE at the same rows-per-instruction
	// density, and 246 more rows.
	//
	// The analysis is deliberately the weakest one that finds them: a value is
	// live if ANY token anywhere reads that component, with no control flow in
	// it at all. Nothing here reasons about a write being dead on the path it is
	// on while live on another - that needs dominance the allocator does not
	// have - so a value read once, anywhere, keeps every write to it.
	unsigned int removed = 0;

	// A fixed point, because deleting the reader is what makes the producer dead.
	// The bound is a guard, not a schedule: each round deletes at least one token
	// or stops, so it cannot spin.
	for( unsigned int round = 0; round < 32; ++round )
	{
		std::map<std::string, unsigned int> readFields;
		std::set<std::string> protectedNames;
		unsigned int resourcesRead = VU_RESOURCE_NONE;

		for( std::list<Token>::const_iterator t = tokens.begin(); t != tokens.end(); ++t )
		{
			if( !t->operand() )
				continue;

			// A name mentioned by in_vf/out_vf, and any other string a directive
			// carries, is part of the program's interface. Collected as raw text
			// because that is how the directives hold it - out_vf's argument is an
			// immediate whose spelling is the alias name, not a register operand.
			if( t->operand()->flags() & Operand::PREPROCESSOR )
			{
				resourcesRead |= vuDeclaredHardwareResource( *t );
				for( std::list<Token::Argument>::const_iterator a = t->arguments().begin();
				     a != t->arguments().end(); ++a )
				{
					if( !a->immediate().empty() ) protectedNames.insert( a->immediate() );
					if( !a->alias().empty() )     protectedNames.insert( a->alias() );
					if( !a->text().empty() )      protectedNames.insert( a->text() );
				}
				continue;
			}

			std::map<std::string, unsigned int> reads, writes;
			bool aliasOnly = true;
			collectDeadWriteAccess( *t, reads, writes, aliasOnly );
			for( std::map<std::string, unsigned int>::const_iterator r = reads.begin();
			     r != reads.end(); ++r )
				readFields[r->first] |= r->second;

			VuTokenResourceAccess access;
			if( buildVuTokenResourceAccess( *t, access ) )
				resourcesRead |= access.implicitReads;
		}

		unsigned int roundRemoved = 0;
		for( std::list<Token>::iterator t = tokens.begin(); t != tokens.end(); )
		{
			VuTokenResourceAccess access;
			if( !tokenIsDeadWriteCandidate( *t, access ) )
			{
				++t;
				continue;
			}

			std::map<std::string, unsigned int> reads, writes;
			bool aliasOnly = true;
			collectDeadWriteAccess( *t, reads, writes, aliasOnly );

			// A token that writes no register and no resource is a nop, a waitq or
			// something else placed for its timing. Not this pass's business.
			if( writes.empty() && access.implicitWrites == VU_RESOURCE_NONE )
			{
				++t;
				continue;
			}

			bool dead = aliasOnly;
			for( std::map<std::string, unsigned int>::const_iterator w = writes.begin();
			     dead && w != writes.end(); ++w )
			{
				if( protectedNames.find( w->first ) != protectedNames.end() )
					dead = false;
				else
				{
					const std::map<std::string, unsigned int>::const_iterator r =
						readFields.find( w->first );
					if( r != readFields.end() && (r->second & w->second) != 0 )
						dead = false;
				}
			}

			if( dead )
			{
				const unsigned int live = access.implicitWrites & resourcesRead;
				if( live != VU_RESOURCE_NONE
				    && implicitWriteIsObservable( t, tokens.end(), live,
				                                  accumulatorWriteFieldMask( *t ) ) )
					dead = false;
			}

			if( !dead )
			{
				++t;
				continue;
			}

			t = tokens.erase( t );
			++roundRemoved;
		}

		if( roundRemoved == 0 )
			break;
		removed += roundRemoved;
	}

	if( removed )
	{
		// Same reason the sink pass renumbers: the allocator's timeline is the
		// token's line number, and after an erase it is no longer an ordering.
		// The Line each token was parsed from is untouched, so diagnostics still
		// point at the source the user wrote.
		unsigned int line = 1;
		for( std::list<Token>::iterator i = tokens.begin(); i != tokens.end(); ++i )
			i->setLineNumber( line++ );
	}

	return removed;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::list<Token>::iterator RegisterAllocator::sinkTargetForLoad( std::list<Token>::iterator load,
                                                                 std::list<Token>::iterator end ) const
{
	std::string dest;
	std::string base;
	std::list<Token>::iterator target = load;
	++target;

	bool hasOffset = false;
	long offset = 0;
	if( !tokenIsSinkableLoad( *load, dest, base, hasOffset, offset ) )
		return target;

	// Where the load stood before it crossed the first label or branch, kept so
	// the whole excursion can be abandoned when it turns out not to reach the
	// value's first reader. Both --sink-loads-into-loops and
	// --sink-loads-past-branches only pay off on a completed motion: one is
	// rematerialization charged per loop iteration, the other gives up the
	// scheduler's room inside a two-arm block, and neither buys a register back
	// unless the load ends up in front of its reader.
	std::list<Token>::iterator beforeExcursion = target;
	bool tookExcursion = false;
	bool crossedHeader = false;

	// --sink-loads-past-branches. The load may be carried over a branch as long
	// as the place it lands is reached on EVERY path out of where it started -
	// which needs two things of the span walked over, and nothing else:
	//
	//   * no path out of the span skips the landing point. Every branch walked
	//     over names a label, and the load may only land once that label has
	//     itself been walked over; while one is outstanding there is a jump
	//     over the current position and no legal home here. `pendingTargets`
	//     holds the outstanding ones. A branch back to a label already walked
	//     over is a loop and is refused outright.
	//   * no path INTO the span skips the load. A label is only transparent
	//     once every branch in the whole program that names it has been walked
	//     over; then the paths joining there all came from the load. That is
	//     what `m_sinkLabelBranchCount` is counted for.
	//
	// Together those are post-dominance, restricted to a forward span, which is
	// the only shape that occurs here: the generated header block picks its
	// destination address on one arm or the other and both arms rejoin at
	// `setDestAddr:` before the first `sq` reads a GIF tag.
	//
	// A branch excursion is given back the same way a label excursion is: unless
	// the walk gets all the way to the value's first reader, the load returns to
	// where it stood before the first branch, and the result is what the flag off
	// would have produced. Stopping halfway across a two-arm block frees no
	// register and only lands the load somewhere the scheduler has less room -
	// keeping those partial moves cost 2 words each on ten of the 45 generated
	// programs and bought nothing on any of them.
	const bool mayCrossBranch = vuSinkLoadsPastBranchesEnabled() && !m_sinkIndirectBranch;
	std::map<std::string, unsigned int> seenBranchesTo;
	std::set<std::string> pendingTargets;
	std::set<std::string> passedLabels;

	// The furthest position walked to that no outstanding jump passes over. With
	// the flag off nothing is ever outstanding and this is just `target`.
	std::list<Token>::iterator safeTarget = target;

	// May this load be moved into the loop that starts at the next label? Two
	// things have to hold across the back edge. The address must be the same
	// next time round, so nothing anywhere may write the base register. And the
	// load must be the only thing that ever writes the value, because re-running
	// it every iteration throws away any other write - an accumulator seeded
	// from memory above the loop would be reset each time. Tested here rather
	// than at the label so a program where it does not hold behaves exactly as
	// it does with the flag off.
	std::map<std::string, unsigned int>::const_iterator writes = m_sinkNameWrites.find( dest );
	bool mayCrossLabel = vuSinkLoadsIntoLoopsEnabled()
		&& !base.empty()
		&& hasOffset
		&& m_sinkIntegerWrites.find( base ) == m_sinkIntegerWrites.end()
		&& !m_sinkStoreBaseUnknown
		&& m_sinkStoreVagueBases.find( base ) == m_sinkStoreVagueBases.end()
		&& writes != m_sinkNameWrites.end() && writes->second == 1;

	// Same base register, different constant offset: a different quadword, and
	// that one IS provable. vu_script3_tce_cl spills its clipped triangle to
	// 956(VI00) while every constant it loads sits at 8..21(VI00), so without
	// this the whole preamble is pinned by a store that cannot reach it.
	if( mayCrossLabel )
	{
		std::map<std::string, std::set<long> >::const_iterator s = m_sinkStoreOffsets.find( base );
		if( s != m_sinkStoreOffsets.end() && s->second.find( offset ) != s->second.end() )
			mayCrossLabel = false;
	}

	for( std::list<Token>::iterator i = target; i != end; ++i )
	{
		// A label starts another basic block: past it the load sits on a
		// different path, and - the case that matters here - inside a loop it
		// was outside of, where it would run once per iteration.
		if( !i->label().empty() )
		{
			// A pure join: every branch in the program that names this label has
			// already been walked over, so the only way to be standing here is to
			// have come through the load. Nothing is being entered and nothing is
			// re-run, so this costs nothing and is not an excursion to give back.
			// A label no branch names at all falls in here too, count zero.
			unsigned int named = 0;
			std::map<std::string, unsigned int>::const_iterator total =
				m_sinkLabelBranchCount.find( i->label() );
			if( total != m_sinkLabelBranchCount.end() )
				named = total->second;
			unsigned int walked = 0;
			std::map<std::string, unsigned int>::const_iterator s =
				seenBranchesTo.find( i->label() );
			if( s != seenBranchesTo.end() )
				walked = s->second;

			if( !mayCrossBranch || named == 0 || walked != named )
			{
				// Only the first loop header may be crossed: the load then runs once
				// per iteration of THAT loop, and letting it fall into the inner
				// per-vertex loop as well would pay for the register in the hottest
				// place there is. Labels that are not branched back to cost nothing
				// and do not count.
				const bool header = m_sinkLoopHeaders.find( i->label() ) != m_sinkLoopHeaders.end();
				if( !mayCrossLabel || (header && crossedHeader) )
					break;
				if( !tookExcursion )
					beforeExcursion = safeTarget;
				tookExcursion = true;
				if( header )
					crossedHeader = true;
			}

			pendingTargets.erase( i->label() );
			passedLabels.insert( i->label() );

			// A label-only token carries no operand, so let it through here
			// rather than fall into the "not a VU instruction" wall below.
			if( !i->operand() )
			{
				target = i;
				++target;
				if( pendingTargets.empty() )
					safeTarget = target;
				continue;
			}
		}

		// Anything the allocator does not model as a plain VU instruction is a
		// wall: --cont and --exit are block boundaries, raw .vsm passthrough
		// keeps its position by definition, and [E]/[D]/[T] mark the end.
		if( !i->operand() )
			break;
		if( i->operand()->flags() & Operand::PREPROCESSOR )
			break;
		if( i->flags() & (Token::PREORDERED | Token::E | Token::D | Token::T | Token::IGNORED) )
			break;
		if( i->operand()->unit() == Operand::BRU )
		{
			if( !mayCrossBranch )
				break;
			// One immediate target and no register written: a conditional branch
			// or a plain `b`. `bal` and `jr` leave through the empty string.
			const std::string branchTarget = sinkBranchTargetLabel( *i );
			if( branchTarget.empty() )
				break;
			// Backwards, into a loop whose body the load would then run once per
			// iteration of - a different trade, and --sink-loads-into-loops makes
			// it at the loop header where the conditions for it are checked.
			if( passedLabels.find( branchTarget ) != passedLabels.end() )
				break;
			seenBranchesTo[ branchTarget ] += 1;
			pendingTargets.insert( branchTarget );
			if( !tookExcursion )
				beforeExcursion = safeTarget;
			tookExcursion = true;
		}

		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess( *i, access ) )
			break;

		// The reader we are sinking towards, or a redefinition - either way the
		// load has to stay in front of it.
		bool touchesDest = false;
		for( std::list<std::string>::const_iterator r = access.registerReads.begin();
		     !touchesDest && r != access.registerReads.end(); ++r )
			touchesDest = (sinkRegisterBase( *r ) == dest);
		for( std::list<std::string>::const_iterator w = access.registerWrites.begin();
		     !touchesDest && w != access.registerWrites.end(); ++w )
			touchesDest = (sinkRegisterBase( *w ) == dest);
		if( touchesDest )
		{
			// Landing in front of a token that carries a label is not the same
			// as landing in front of the token: a branch to that label jumps
			// straight past the load. And an excursion past a label is only
			// worth its per-iteration cost if it got all the way to the reader,
			// which this is.
			if( tookExcursion && !i->label().empty() )
				return beforeExcursion;
			// A jump over this point is still outstanding, so the reader is on
			// one arm and not the other, and this is not a home for the load.
			if( !pendingTargets.empty() )
				return beforeExcursion;
			return target;
		}

		// Memory ordering. A load that crosses the store that wrote its own
		// quadword reads the wrong value, and nothing here can prove that a
		// store through some other base register misses what we are about to
		// read, so by default any store stops the load. That costs real ground -
		// the generated loop bodies write the previous vertex's ADC bit halfway
		// down and every later load piles up behind it - which is what
		// --sink-loads-across-stores buys back, on the same different-base-means-
		// different-quadword assumption SCE's vcl makes.
		if( access.memoryKind == VU_MEMORY_XGKICK )
			break;
		if( access.memoryKind == VU_MEMORY_STORE )
		{
			if( !vuSinkLoadsAcrossStoresEnabled() )
				break;
			if( !access.hasMemoryBase || access.memoryBaseRegister == base )
				break;
		}

		// The address has to still say the same thing when the load runs.
		bool clobbersBase = false;
		for( std::list<std::string>::const_iterator w = access.registerWrites.begin();
		     !clobbersBase && w != access.registerWrites.end(); ++w )
			clobbersBase = (sinkRegisterBase( *w ) == base);
		if( clobbersBase )
			break;

		target = i;
		++target;
		if( pendingTargets.empty() )
			safeTarget = target;
	}

	// Fell out on a barrier or on the end of the program rather than on a
	// reader. Whatever ground was gained past the label is not worth paying for
	// once per iteration, so give it back.
	return tookExcursion ? beforeExcursion : safeTarget;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

unsigned int RegisterAllocator::sinkLoadsToFirstUse( std::list<Token>& tokens )
{
	// openvcl allocates over source order, and the VU authoring layer emits its
	// loop bodies the way a human reads them: load everything the iteration
	// needs, then use it. Six loaded quadwords are therefore live from the top
	// of the body even though the code transforms them one at a time, and six
	// registers is about the margin these programs are missing. SCE's vcl has no
	// such problem because its scheduler places a load next to its reader:
	// measured on its output for vu_script3_d, one VF register carries vertex1,
	// then vertex2, then vertex3, reloaded each time.
	//
	// The token really moves. Recording the load as "sinkable", leaving it where
	// it is and starting the range at the first read would be unsound - between
	// the load and the read the register is not reserved, so another value can
	// be given it and the loaded one is gone by the time it is read.
	unsigned int moved = 0;

	// Program-wide facts --sink-loads-into-loops needs before it may move a load
	// across a loop header: an address register that something writes is not the
	// same address next time round, and a store through the same base is the one
	// store we know can hit what the load reads.
	m_sinkIntegerWrites.clear();
	m_sinkStoreOffsets.clear();
	m_sinkStoreVagueBases.clear();
	m_sinkLoopHeaders.clear();
	m_sinkNameWrites.clear();
	m_sinkStoreBaseUnknown = false;
	m_sinkLabelBranchCount.clear();
	m_sinkIndirectBranch = false;

	// Which labels are the target of a backward branch, i.e. which ones a load
	// would be moved INSIDE rather than merely past. Computed from list order,
	// which is still the source order at this point.
	{
		std::map<std::string, unsigned int> labelIndex;
		unsigned int index = 0;
		for( std::list<Token>::iterator t = tokens.begin(); t != tokens.end(); ++t, ++index )
			if( !t->label().empty() )
				labelIndex[ t->label() ] = index;

		index = 0;
		for( std::list<Token>::iterator t = tokens.begin(); t != tokens.end(); ++t, ++index )
		{
			if( !t->operand() || t->operand()->unit() != Operand::BRU )
				continue;
			// How many branches name each label, over the whole program, and
			// whether any branch goes somewhere this cannot say. One `jr` and
			// every label in the program is potentially entered from a place the
			// scan will never see, which is the one thing
			// --sink-loads-past-branches must not be wrong about, so it gives up
			// on the program entirely.
			bool named = false;
			for( std::list<Token::Argument>::const_iterator a = t->arguments().begin();
			     a != t->arguments().end(); ++a )
			{
				if( !(a->flags() & Token::Argument::BRANCH) || a->type() != Token::Argument::IMMEDIATE )
					continue;
				std::map<std::string, unsigned int>::const_iterator l = labelIndex.find( a->immediate() );
				if( l == labelIndex.end() )
					continue;
				named = true;
				m_sinkLabelBranchCount[ a->immediate() ] += 1;
				if( l->second <= index )
					m_sinkLoopHeaders.insert( a->immediate() );
			}
			if( !named )
				m_sinkIndirectBranch = true;
		}
	}
	for( std::list<Token>::iterator t = tokens.begin(); t != tokens.end(); ++t )
	{
		VuTokenResourceAccess access;
		if( !t->operand() || (t->operand()->flags() & Operand::PREPROCESSOR) )
			continue;
		if( !buildVuTokenResourceAccess( *t, access ) )
			continue;
		std::set<std::string> written;
		for( std::list<std::string>::const_iterator w = access.registerWrites.begin();
		     w != access.registerWrites.end(); ++w )
			written.insert( sinkRegisterBase( *w ) );
		for( std::set<std::string>::const_iterator w = written.begin(); w != written.end(); ++w )
		{
			m_sinkIntegerWrites.insert( *w );
			m_sinkNameWrites[ *w ] += 1;   // per TOKEN, not per component
		}
		// Stores only. xgkick READS the packet and hands it to the GIF - it
		// writes no VU memory - and its register operand is not an indirect
		// address, so counting it here only ever produced "some store has an
		// address I cannot read", which switched the whole pass off.
		if( access.memoryKind == VU_MEMORY_STORE )
		{
			if( !access.hasMemoryBase )
				m_sinkStoreBaseUnknown = true;
			else if( access.hasMemoryOffset )
				m_sinkStoreOffsets[ access.memoryBaseRegister ].insert( access.memoryOffset );
			else
				m_sinkStoreVagueBases.insert( access.memoryBaseRegister );
		}
	}

	// Every token, considered once, in source order. Walking the live list
	// instead would revisit what the pass just moved: two loads that both stop
	// at the same barrier take turns being last and leapfrog each other for
	// ever. A list iterator survives a splice, so collecting them up front is
	// enough - and going forwards keeps two loads that land against the same
	// barrier in the order they were written.
	std::vector< std::list<Token>::iterator > order;
	for( std::list<Token>::iterator i = tokens.begin(); i != tokens.end(); ++i )
		order.push_back( i );

	for( unsigned int k = 0; k < order.size(); ++k )
	{
		const std::list<Token>::iterator load = order[k];

		std::list<Token>::iterator next = load;
		++next;

		const std::list<Token>::iterator target = sinkTargetForLoad( load, tokens.end() );
		if( target != next )
		{
			tokens.splice( target, tokens, load );
			moved++;
		}
	}

	if( moved )
	{
		// The timeline the whole allocator runs on is the token's line number,
		// and after a splice that number is no longer an ordering. Re-derive it
		// from list position, once, so the two cannot disagree again. The Line
		// the token was parsed from is untouched, so every diagnostic still
		// points at the source the user wrote.
		unsigned int line = 1;
		for( std::list<Token>::iterator i = tokens.begin(); i != tokens.end(); ++i )
			i->setLineNumber( line++ );
	}

	return moved;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RegisterAllocator::trimUncarriedLoopLocalRanges( std::list<Token>& tokens )
{
	// The branch-state analysis merges every alias a source-level name ever had
	// into one, and the trace machinery then stretches the survivor from the
	// enclosing loop's entry point to the last line it walked. For a value that
	// is recomputed from scratch every iteration - a transformed vertex, a
	// per-vertex colour - that turns a range twenty lines long into one covering
	// the whole loop body. Three of those cost three registers for nothing, and
	// on a three-vertex batch that is the difference between fitting in 31 VF
	// registers and not.
	//
	// A range may be pulled back to its own accesses only when the value dies at
	// its last use, and the three conditions that establish that are all
	// necessary:
	//
	//   * every component read is written by an EARLIER access, so the iteration
	//     does not depend on what the previous one left behind;
	//   * every access sits inside the same loops, so nothing outside a loop
	//     defines a value read inside it (which WOULD have to survive the back
	//     edge, and is what the extension passes exist to cover);
	//   * no branch lies between the first and last access, so the access list
	//     read in line order is the order the value actually sees. Without this
	//     a definition on one arm of a conditional reads as covering both.
	std::vector< std::pair<unsigned int, unsigned int> > loops;
	collectBackEdges( tokens, m_labels, loops );

	std::map< Alias*, std::vector<AliasAccess> > accesses;
	std::vector<unsigned int> branchLines;

	for( std::list<Token>::iterator t = tokens.begin(); t != tokens.end(); ++t )
	{
		if( !tokenIsEmittable( *t ) )
			continue;

		const unsigned int line = t->lineNumber();

		if( t->operand()->unit() == Operand::BRU )
			branchLines.push_back( line );

		// Sources first, destination last - a token that reads and writes the
		// same name reads the previous value.
		for( int pass = 0; pass < 2; ++pass )
		{
			for( std::list<Token::Argument>::const_iterator a = t->arguments().begin();
			     a != t->arguments().end(); ++a )
			{
				const bool write = (a->flags() & Token::Argument::WRITE) != 0;
				if( write != (pass == 1) )
					continue;
				if( a->content() != Token::Argument::ALIAS || !a->dependency() || !a->dependency()->alias() )
					continue;
				accesses[ a->dependency()->alias() ].push_back(
					AliasAccess( line, accessFields( *t, *a ), write ) );
			}
		}
	}

	for( std::map< Alias*, std::vector<AliasAccess> >::iterator i = accesses.begin();
	     i != accesses.end(); ++i )
	{
		Alias* alias = i->first;
		const std::vector<AliasAccess>& acc = i->second;

		// An input or output register is pinned to a physical register by the
		// --enter/--exit contract; its liveness is not ours to shorten.
		if( alias->allocatedRegister() )
			continue;
		if( acc.empty() )
			continue;

		unsigned int lo = 0;
		unsigned int hi = 0;
		if( !alias->rangeExtent( lo, hi ) )
			continue;

		const unsigned int firstLine = acc.front().m_line;
		const unsigned int lastLine = acc.back().m_line;
		if( firstLine > lastLine )
			continue;
		(void)lo;
		(void)hi;

		// Condition 1: no component is read before it is written.
		unsigned int defined = 0;
		bool carried = false;
		for( std::vector<AliasAccess>::const_iterator a = acc.begin(); a != acc.end(); ++a )
		{
			if( a->m_write )
			{
				defined |= a->m_fields;
				continue;
			}
			if( a->m_fields & ~defined )
			{
				carried = true;
				break;
			}
		}
		if( carried )
			continue;

		// Condition 2: every loop either contains the whole span or misses it.
		bool crossesLoop = false;
		for( unsigned int l = 0; !crossesLoop && l < loops.size(); ++l )
		{
			const unsigned int start = loops[l].first;
			const unsigned int stop = loops[l].second;
			const bool overlaps = !(lastLine < start || stop < firstLine);
			const bool contained = firstLine >= start && lastLine <= stop;
			if( overlaps && !contained )
				crossesLoop = true;
		}
		if( crossesLoop )
			continue;

		// Condition 3: straight-line control flow between the two ends.
		bool branchInside = false;
		for( unsigned int b = 0; !branchInside && b < branchLines.size(); ++b )
		{
			if( branchLines[b] > firstLine && branchLines[b] < lastLine )
				branchInside = true;
		}
		if( branchInside )
			continue;

		// The precise thing: one interval per definition, from the defining line
		// to that definition's last reader, per component. A scalar scratch name
		// reused once per vertex is a merged alias with ONE range spanning the
		// whole batch (vu_script3_d's `vuS1` is [86-229]); its three generations
		// are independent, and the register is free in the holes between them.
		// Nothing here invents liveness: every interval starts at a write this
		// alias performs and ends at a read of that write.
		std::vector< std::pair<unsigned int, unsigned int> > intervals;
		for( unsigned int component = 0; component < 4; ++component )
		{
			const unsigned int mask = 1u << component;
			bool open = false;
			unsigned int defLine = 0;
			unsigned int lastUse = 0;

			for( std::vector<AliasAccess>::const_iterator a = acc.begin(); a != acc.end(); ++a )
			{
				if( !(a->m_fields & mask) )
					continue;

				if( a->m_write )
				{
					if( open )
						intervals.push_back( std::make_pair( defLine, lastUse ) );
					open = true;
					defLine = a->m_line;
					lastUse = a->m_line;
				}
				else if( open )
					lastUse = a->m_line;
			}

			if( open )
				intervals.push_back( std::make_pair( defLine, lastUse ) );
		}

		if( intervals.empty() )
		{
			alias->clipRanges( firstLine, lastLine );
			continue;
		}

		alias->clearRanges();
		for( unsigned int n = 0; n < intervals.size(); ++n )
			alias->addRange( intervals[n].first, intervals[n].second );
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace
{
	// One occurrence of a float alias name in the token list.
	struct FloatAliasAccess
	{
		Token::Argument* argument;
		unsigned int slot;
		unsigned int mask;
		bool write;
	};
}

// --split-dead-float-ranges.  See VuSchedulingRules.h.
//
// The safety argument, because this pass is exactly how a scheduler comes to
// emit a program that assembles, fits, and computes the wrong thing:
//
//  * A REGION is a maximal run of emittable instructions with no label, no
//    branch and no directive inside.  Control flow cannot enter a region except
//    at its top and cannot leave it except at its bottom, so a linear scan of a
//    region is exact - no CFG needed, and none of the conservatism a wrong CFG
//    would hide.
//  * A name is only considered when EVERY access to it in the whole program is
//    inside ONE region.  A name accessed nowhere else cannot be live-out (a
//    later read would be an access), and cannot be live-in either unless the
//    region's own first access reads it - which is checked separately and
//    disqualifies the name.  So the value neither enters nor leaves, including
//    across a back edge that re-enters the region every iteration.
//  * A SPLIT POINT is a write whose field mask covers every field of the name
//    that is still to be read, and which does not itself read the name.  The
//    first condition makes it a full kill: nothing of the old value survives, so
//    a fresh register may start here and no masked write ever inherits fields
//    from a register that no longer holds them.  The second excludes two-address
//    self-updates (`sub.x vuS2, vuS2, k0[z]`), where the edge crossed is a true
//    RAW and splitting would only widen liveness by one row for no gain.
//  * Names bound to physical registers by --enter/--exit (in_vf / out_vf) are
//    never renamed: their register is the program's ABI.
unsigned int RegisterAllocator::splitDeadFloatRanges( std::list<Token>& tokens )
{
	// Names pinned to a physical register by the entry / exit blocks.
	std::set<std::string> pinned;
	for( std::list<Token>::const_iterator t = tokens.begin(); t != tokens.end(); ++t )
	{
		if( !t->operand() )
			continue;
		if( t->operand()->unit() != Operand::ENTER && t->operand()->unit() != Operand::EXIT )
			continue;
		for( std::list<Token::Argument>::const_iterator a = t->arguments().begin();
		     a != t->arguments().end(); ++a )
		{
			if( !a->immediate().empty() )
				pinned.insert( a->immediate() );
			if( !a->alias().empty() )
				pinned.insert( a->alias() );
		}
	}

	// Slots: the emittable instructions in list order, each tagged with the
	// region it belongs to.  A label, a directive and anything not emittable
	// break the region BEFORE the token; a branch breaks it after.
	std::vector<Token*> slots;
	std::vector<unsigned int> slotRegion;
	unsigned int region = 0;
	for( std::list<Token>::iterator t = tokens.begin(); t != tokens.end(); ++t )
	{
		if( !t->label().empty() || !tokenIsEmittable( *t ) )
			++region;
		if( !tokenIsEmittable( *t ) )
			continue;
		slots.push_back( &*t );
		slotRegion.push_back( region );
		if( t->operand()->unit() == Operand::BRU )
			++region;
	}

	// Every float-alias occurrence, grouped by name, in slot order.
	std::map< std::string, std::vector<FloatAliasAccess> > byName;
	for( unsigned int s = 0; s < slots.size(); ++s )
	{
		Token& token = *slots[s];
		for( std::list<Token::Argument>::iterator a = token.arguments().begin();
		     a != token.arguments().end(); ++a )
		{
			if( a->type() != Token::Argument::FLOAT_REGISTER )
				continue;
			if( a->content() != Token::Argument::ALIAS )
				continue;
			if( a->alias().empty() )
				continue;

			FloatAliasAccess access;
			access.argument = &*a;
			access.slot = s;
			access.write = ( a->flags() & Token::Argument::WRITE ) != 0;
			access.mask = access.write ? vuWriteFieldMask( token, *a )
			                           : vuReadFieldMask( token, *a );
			byName[ a->alias() ].push_back( access );
		}
	}

	unsigned int renamed = 0;

	for( std::map< std::string, std::vector<FloatAliasAccess> >::iterator n = byName.begin();
	     n != byName.end(); ++n )
	{
		const std::string& name = n->first;
		std::vector<FloatAliasAccess>& access = n->second;

		if( pinned.find( name ) != pinned.end() )
			continue;
		if( access.size() < 2 )
			continue;

		// One region, or nothing doing.
		const unsigned int home = slotRegion[ access[0].slot ];
		bool oneRegion = true;
		for( unsigned int i = 1; oneRegion && i < access.size(); ++i )
			if( slotRegion[ access[i].slot ] != home )
				oneRegion = false;
		if( !oneRegion )
			continue;

		// Backward scan.  `live` is the set of fields that will still be read
		// before something writes them.
		unsigned int live = 0;
		std::vector<unsigned int> kills;
		int i = (int)access.size() - 1;
		while( i >= 0 )
		{
			const unsigned int slot = access[i].slot;
			unsigned int writeMask = 0;
			unsigned int readMask = 0;
			int j = i;
			while( j >= 0 && access[j].slot == slot )
			{
				if( access[j].write )
					writeMask |= access[j].mask;
				else
					readMask |= access[j].mask;
				--j;
			}
			if( writeMask )
			{
				if( live != 0 && ( live & ~writeMask ) == 0 && readMask == 0 )
					kills.push_back( slot );
				live &= ~writeMask;
			}
			live |= readMask;
			i = j;
		}

		// live != 0 here means a field is read before any write covers it: the
		// name is live-in, so the value crosses the region boundary and the
		// whole argument above collapses.  Refuse.
		if( live != 0 || kills.empty() )
			continue;

		// Forward rewrite.  kills came out of a backward scan, so it is
		// descending; walk it from the back.
		unsigned int generation = 0;
		int nextKill = (int)kills.size() - 1;
		for( unsigned int k = 0; k < access.size(); ++k )
		{
			while( nextKill >= 0 && access[k].slot == kills[nextKill] )
			{
				++generation;
				--nextKill;
			}
			if( generation == 0 )
				continue;
			std::stringstream renamedTo;
			renamedTo << name << "~" << generation;
			access[k].argument->setAlias( renamedTo.str() );
			++renamed;
		}
	}

	return renamed;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Among the registers the conflict test already accepted, the one whose nearest
// occupied neighbour is furthest from `dest`.  A register with nothing on it at
// all wins outright; ties keep the lowest number, so the choice is a function of
// the ranges and not of allocation order.
//
// The `score < 0` shortcut - an empty register wins outright - is what buys the
// interleave and also what makes the spread expensive: the first thirty-one
// values each take a fresh register.  Capping it was measured and rejected.  A
// cap of four rows applied to all seventy programs costs 3491 modelled cycles
// and grows every corpus; the fix for the programs that run out is to narrow
// WHICH aliases are spread, not how far.  See vuSpreadFloatRegistersWebsOnly.
const Register* RegisterAllocator::preferSpreadRegister( const std::vector<Alias*>& group,
                                                         const std::vector<const Register*>& candidates ) const
{
	if( candidates.empty() )
		return NULL;
	if( candidates.size() == 1 )
		return candidates[0];
	if( group.empty() )
		return candidates[0];

	// The group's extent: a two-address chain shares one register over the
	// union of its members' ranges, so that union is what the gap is measured
	// from.
	unsigned int lo = 0;
	unsigned int hi = 0;
	bool haveExtent = false;
	for( unsigned int g = 0; g < group.size(); ++g )
	{
		unsigned int glo = 0;
		unsigned int ghi = 0;
		if( !group[g]->rangeExtent( glo, ghi ) )
			continue;
		if( !haveExtent || glo < lo ) lo = glo;
		if( !haveExtent || ghi > hi ) hi = ghi;
		haveExtent = true;
	}
	if( !haveExtent )
		return candidates[0];

	const Register* best = candidates[0];
	long bestScore = -1;

	for( unsigned int c = 0; c < candidates.size(); ++c )
	{
		const Register* candidate = candidates[c];
		long score = -1;

		for( AliasMap::const_iterator k = m_aliases.begin(); k != m_aliases.end(); ++k )
		{
			Alias* src = k->first;
			bool inGroup = false;
			for( unsigned int g = 0; !inGroup && g < group.size(); ++g )
				if( group[g] == src ) inGroup = true;
			if( inGroup )
				continue;
			if( src->allocatedRegister() != candidate )
				continue;
			unsigned int slo = 0;
			unsigned int shi = 0;
			if( !src->rangeExtent( slo, shi ) )
				continue;

			long gap;
			if( shi < lo )
				gap = (long)lo - (long)shi;
			else if( slo > hi )
				gap = (long)slo - (long)hi;
			else
				gap = 0;                      // overlapping: the conflict test
			                                  // let it through on ranges with
			                                  // holes, so score it worst.
			if( score < 0 || gap < score )
				score = gap;
		}

		if( score < 0 )
			return candidate;                 // untouched register, take it

		if( score > bestScore )
		{
			bestScore = score;
			best = candidate;
		}
	}

	return best;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RegisterAllocator::coalesceSameNameFloatWrites( std::list<Token>& tokens )
{
	// `mul.x fogAccum, fogAccum, fogParams[z]` is one value, but openvcl gives
	// the read one Alias and the write another, and the two ranges touch on the
	// shared line, so they interfere and the chain alternates between two
	// registers. The integer side already fixes this - it had to, or `isubiu x,
	// x, 1` decremented a register nobody read - via setSameNamePredecessor and
	// the atomic chain pre-pass in processAliases. This is the same edge for
	// floats, with one extra precondition the integer path gets for free: the
	// predecessor must be dead from the write on. If it is still live afterwards
	// the two values coexist, and sharing a register would destroy one of them.
	std::map<std::string, Alias*> lastByName;

	for( std::list<Token>::iterator t = tokens.begin(); t != tokens.end(); ++t )
	{
		if( !tokenIsEmittable( *t ) )
			continue;

		const unsigned int line = t->lineNumber();

		for( int pass = 0; pass < 2; ++pass )
		{
			for( std::list<Token::Argument>::iterator a = t->arguments().begin();
			     a != t->arguments().end(); ++a )
			{
				const bool write = (a->flags() & Token::Argument::WRITE) != 0;
				if( write != (pass == 1) )
					continue;
				if( a->type() != Token::Argument::FLOAT_REGISTER )
					continue;
				if( a->content() != Token::Argument::ALIAS || !a->dependency() || !a->dependency()->alias() )
					continue;

				Alias* alias = a->dependency()->alias();
				if( alias->type() != Alias::FLOAT )
					continue;

				if( !write )
				{
					lastByName[ a->alias() ] = alias;
					continue;
				}

				std::map<std::string, Alias*>::iterator previous = lastByName.find( a->alias() );
				if( previous != lastByName.end()
				    && previous->second != alias
				    && !alias->sameNamePredecessor()
				    && !alias->allocatedRegister()
				    && !previous->second->allocatedRegister() )
				{
					unsigned int lo = 0;
					unsigned int hi = 0;
					if( previous->second->rangeExtent( lo, hi ) && hi <= line )
					{
						// A chain must stay a chain: refuse an edge that would
						// close a cycle, the same defence BranchState uses.
						bool wouldCycle = false;
						Alias* p = previous->second;
						for( int hop = 0; hop < 32 && p; ++hop, p = p->sameNamePredecessor() )
						{
							if( p == alias ) { wouldCycle = true; break; }
						}
						if( !wouldCycle )
						{
							alias->setSameNamePredecessor( previous->second );
							m_coalescedWrites.push_back( alias );
						}
					}
				}

				lastByName[ a->alias() ] = alias;
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Alias* RegisterAllocator::obtainAlias( Alias::Type type )
{
	Alias* alias = new Alias( type );

	m_aliases[ alias ] = alias;

	return alias;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RegisterAllocator::trySetSameNamePredecessor( Alias* alias, Alias* predecessor )
{
	if( !alias || !predecessor || alias == predecessor )
		return false;
	if( alias->type() != predecessor->type() )
		return false;

	// A chain must stay a chain, and the check has to be COMPLETE rather than
	// deep-enough-looking: a fixed depth of 16 answers "no cycle" for any ring
	// that would close further up than that, and the walks it protects then never
	// terminate. Floyd rather than a depth, because this is a static member with
	// no alias count to bound itself by - and it needs no bound: the hare meets
	// the tortoise on a ring, so the walk ends whether or not the chain it is
	// handed is already one. A chain that IS already a ring gets nothing added to
	// it either.
	Alias* slow = predecessor;
	Alias* fast = predecessor;
	while( slow )
	{
		if( slow == alias )
			return false;
		slow = slow->sameNamePredecessor();
		fast = fast ? fast->sameNamePredecessor() : NULL;
		fast = fast ? fast->sameNamePredecessor() : NULL;
		if( fast && fast == slow )
			return false;
	}

	alias->setSameNamePredecessor( predecessor );
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RegisterAllocator::releaseAlias( Alias* alias, Alias* replacement )
{
	AliasMap::iterator i = m_aliases.find( alias );
	assert( i != m_aliases.end() );

	// A merge is a RENAME, not a deletion.  Dependency::depend has just rewritten
	// every dependency that named `alias` to name `replacement`, so the
	// two-address chain - the edges that make `iaddi k0, k0, -1` write the
	// register it read - has to be rewritten the same way.  Clearing those edges
	// instead, which is what this did, is the bug it looks like a fix for: on a
	// loop whose counter is decremented at a JOIN, the write of the counter is
	// merged with a later write of the same name, the edge from the live-in dies
	// with the absorbed alias, and the decrement is allocated to a register the
	// loop's `ibgtz` at the bottom is the only reader of.  The counter then
	// recomputes initial-1 every iteration and the trip count is wrong.
	if( replacement && replacement != alias )
	{
		// The survivor inherits the absorbed alias's own predecessor when it has
		// none: the absorbed alias's place in the chain is now the survivor's.
		if( !replacement->sameNamePredecessor() )
			trySetSameNamePredecessor( replacement, alias->sameNamePredecessor() );

		for( AliasMap::iterator k = m_aliases.begin(); k != m_aliases.end(); ++k )
		{
			Alias* other = k->first;
			if( other == alias || other->sameNamePredecessor() != alias )
				continue;

			// Rename first.  When the merge folds a LATER definition of the same
			// name onto an earlier one the rename closes a cycle - every member of
			// it is then the same value and belongs in one register, which a linear
			// chain says by splicing the dying node out rather than by cutting the
			// list at it.
			if( !trySetSameNamePredecessor( other, replacement ) )
				trySetSameNamePredecessor( other, alias->sameNamePredecessor() );
		}
	}

	// Anything still pointing at `alias` is about to hold a dangling pointer;
	// clear those edges so processAliases doesn't walk into freed memory.
	// (Without this guard openvcl segfaults when branch-state merges release one
	// half of a previously-recorded two-address pair.)
	for( AliasMap::iterator k = m_aliases.begin(); k != m_aliases.end(); ++k )
	{
		Alias* other = k->first;
		if( other == alias )
			continue;
		if( other->sameNamePredecessor() == alias )
			other->setSameNamePredecessor( NULL );
	}

	m_aliases.erase( i );

	delete alias;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RegisterAllocator::processCommonDirective( Token& token )
{
	if( !token.operand() )
		return true;

	if( token.operand()->name() == ".name" )
	{
		m_name = token.arguments().begin()->immediate();
		token.setFlags( token.flags() | Token::IGNORED );
		return true;
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RegisterAllocator::updateDynamicTracker( const Token* src )
{
	std::map< const Token*, unsigned int >::iterator i = m_dynamicTracker.find( src );

	if( m_dynamicTracker.end() == i )
	{
		m_dynamicTracker[ src ] = 1;
		return true;
	}

	if( i->second > m_dynamicThreshold )
		return false;

	m_dynamicTracker[ src ] = i->second+1;
	return true;
}


}
