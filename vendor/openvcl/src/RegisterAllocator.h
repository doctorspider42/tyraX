#ifndef __OPENVCL_REGISTERALLOCATOR_H__
#define __OPENVCL_REGISTERALLOCATOR_H__

/*
 * RegisterAllocator.h
 *
 * Copyright (C) 2004 Jesper Svennevid, Daniel Collin
 *
 * Licensed under the AFL v2.0. See the file LICENSE included with this
 * distribution for licensing terms.
 *
 */
        
#ifndef __OPENVCL_REGISTER_H__
#include "Register.h"
#endif

#ifndef __OPENVCL_TOKEN_H__
#include "Token.h"
#endif

#ifndef __OPENVCL_BranchState_H__
#include "BranchState.h"
#endif

#include <map>
#include <set>
#include <vector>
#include <sstream>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace vcl
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class RegisterAllocator
{

public:

	RegisterAllocator();
	~RegisterAllocator();

	void setAvailableFloats( unsigned int floats );
	void setAvailableIntegers( unsigned int integers );

	bool process( std::list<Token>& tokens );

	// Put every accumulated allocation result back to its constructed state, so
	// the same allocator can be run a second time over a restored token list.
	// Only Parser::allocateRegisters() needs this, for the
	// --sink-loads-past-branches retry.  The BranchStates and Aliases the failed
	// attempt built are dropped rather than freed, which is what the failure
	// path already did before returning.
	void reset();

	// `replacement` is the alias `alias` has just been merged INTO, when there is
	// one.  A merge is a rename, not a deletion, so the two-address chain has to
	// be rewritten rather than cut - see the body.
	void releaseAlias( Alias* alias, Alias* replacement = NULL );
	Alias* obtainAlias( Alias::Type type );

	const Register* floatRegister( unsigned int regNumber ) const;
	const Register* integerRegister( unsigned int regNumber ) const;

	const std::string& name();

	void setDynamicThreshold( unsigned int threshold );
	void setShowRegisterInfo( bool show );

protected:

private:

	struct AliasOrder
	{
		bool operator()( const Alias* a, const Alias* b ) const
		{
			return a->id() < b->id();
		}
	};

	typedef std::map<Alias*, Alias*, AliasOrder> AliasMap;

	enum State
	{
		OUTSIDE,
		ENTER,
		CODE,
		EXIT
	};

	void setState( State state );
	State state() const;

	bool collectLabels( std::list<Token>::iterator start, std::list<Token>::iterator end );
	bool processBranchState( BranchState* state, std::list<Token>::iterator end );

	bool processCommonDirective( Token& token );

	bool processAliases();

	// Install `predecessor` as `alias`'s two-address chain predecessor, unless
	// that would make the chain a cycle - the same defence BranchState uses when
	// it builds the edge in the first place.  Returns whether the edge was made.
	static bool trySetSameNamePredecessor( Alias* alias, Alias* predecessor );

	// Walks all emittable tokens and records, for each physical register,
	// the line numbers where it appears as a *literal* (non-alias) operand.
	// Used to keep the allocator from assigning an alias to a physical
	// register that is hardcoded by some instruction in the alias's live
	// range — e.g. `fcand vi01, ...` clobbering an unrelated alias that
	// happens to share VI01.  Synthesized as preallocated aliases inserted
	// into m_aliases so the existing interference check picks them up.
	void collectLiteralRegisterUsage( std::list<Token>& tokens );

	// --drop-dead-writes.  Delete every token whose only effect is a value the
	// program never reads.  Runs FIRST, before --sink-loads, so the sink pass and
	// the allocator both see the smaller program, and renumbers the timeline from
	// list position for the same reason the sink pass does.  Iterated to a fixed
	// point: dropping `add.z k0, vf00, i` is what makes the `loi` above it dead.
	// Returns the number of tokens removed.
	unsigned int dropDeadRegisterWrites( std::list<Token>& tokens );

	// --sink-loads.  Move each load down the token list to just before the value
	// it loads is first read, or as far as a legal move goes.  Runs FIRST, before
	// anything reads a line number, and renumbers the timeline from list position
	// when it moved something, because after a splice the source line number is
	// no longer an ordering.  Returns the number of loads moved.
	unsigned int sinkLoadsToFirstUse( std::list<Token>& tokens );

	// Where the given load may legally be re-inserted: the position just before
	// the first token that either reads what it loaded or that it may not cross.
	std::list<Token>::iterator sinkTargetForLoad( std::list<Token>::iterator load,
	                                              std::list<Token>::iterator end ) const;

	// --sink-loads-into-loops needs to know, for a candidate load, whether its
	// address survives a trip round the loop it is about to be moved into. Both
	// are program-wide facts, collected once per pass: every integer register
	// key any token writes, and the base register of every store (with a flag
	// for a store whose base could not be read, which forbids the motion
	// outright).
	std::set<std::string> m_sinkIntegerWrites;
	std::map<std::string, std::set<long> > m_sinkStoreOffsets;
	std::set<std::string> m_sinkStoreVagueBases;
	std::set<std::string> m_sinkLoopHeaders;
	std::map<std::string, unsigned int> m_sinkNameWrites;
	bool m_sinkStoreBaseUnknown;

	// --sink-loads-past-branches needs one program-wide fact per label: how many
	// branches in the whole program name it. The scan counts the ones it walks
	// over, and the two numbers agreeing is what proves no OTHER path enters the
	// span the load is being moved across. Counted per label rather than per
	// position because the pass splices tokens as it goes, which invalidates
	// indices but never the count - a load is not a branch.
	std::map<std::string, unsigned int> m_sinkLabelBranchCount;
	bool m_sinkIndirectBranch;

	// --trim-uncarried-ranges.  Shrink a merged alias back to the extent of its
	// own accesses when it provably dies at its last use inside one loop
	// iteration.  Runs before the extension passes so those can still re-add
	// whatever liveness they genuinely require.
	void trimUncarriedLoopLocalRanges( std::list<Token>& tokens );

	// --split-dead-float-ranges.  Rename float alias occurrences so each
	// independent value of a source name gets its own Alias, and so its own
	// register.  Runs before anything reads a line number or builds a branch
	// state: it is a pure rewrite of Token::Argument::alias() and the rest of
	// the compiler simply sees more names.  Returns the number of renamed
	// occurrences.  See VuSchedulingRules.h for why this is the pass that
	// reaches openvcl's FMAC stalls.
	unsigned int splitDeadFloatRanges( std::list<Token>& tokens );

	// The register a free-list scan should hand out.  With
	// --split-dead-float-ranges the lowest-numbered free register is the wrong
	// answer: two webs of one split name have disjoint ranges by construction,
	// so first fit puts them straight back on one register and the split buys
	// nothing.  Score each acceptable candidate by how far the nearest range
	// already on it sits from `dest`, and take the furthest.
	const Register* preferSpreadRegister( const std::vector<Alias*>& group,
	                                      const std::vector<const Register*>& candidates ) const;

	// --coalesce-float-writes.  Tie a float write to the alias holding the same
	// source-level name's previous value when that value is dead from here on,
	// so the two-address chain pre-pass hands both one register.  Runs last, on
	// final ranges, because the deadness test needs them.
	void coalesceSameNameFloatWrites( std::list<Token>& tokens );

	void extendContinuationLiveRanges( std::list<Token>& tokens );
	void extendLoopDirectiveLiveRanges( std::list<Token>& tokens );
	void extendMultiQStageLiveRanges( std::list<Token>& tokens );
	bool loopTargetHasLoopDirective( std::list<Token>::iterator target, std::list<Token>::iterator end ) const;
	void extendLoopDirectiveRange( std::list<Token>& tokens, unsigned int loopStart, unsigned int loopEnd );
	void tieCarriedWritesToLiveInAliases( std::list<Token>& tokens, unsigned int loopStart, unsigned int loopEnd,
	                                      const std::set<Alias*>& liveInAliases );
	void extendMultiQStageRange( std::list<Token>& tokens, unsigned int loopStart, unsigned int loopEnd );

	bool updateDynamicTracker( const Token* src );

	bool setupLocks( std::map< std::string, unsigned int >& inputs, std::map< unsigned int, std::vector<std::string> >& locks, std::map< std::string, Register*>& regs, std::map<std::string, BranchState::State>& aliases, Register* rarray, unsigned int max );
	bool releaseLocks( std::map< unsigned int, std::vector<std::string> >& locks, std::map< std::string, Register*>& regs, unsigned int line );
	void releaseLocks( std::map< unsigned int, std::vector<std::string> >& locks, std::map< std::string, Register*>& regs );
	const Register* allocateRegister( const std::string& name, std::map<std::string,Register*>& regs, std::map < unsigned int, std::vector<std::string> >& locks, std::map<std::string, BranchState::State>& aliases, Register* rarray, unsigned int max );

	std::map< std::string, std::list<Token>::iterator > m_labels;
	std::list< BranchState* > m_states;

//	void generateReadErrorReport( const Token::Argument& arg, const Token& token );

	State m_currState;
	Register m_floats[32];
	Register m_integers[16];	

	std::string m_name;

	std::map< const Token*, unsigned int > m_dynamicTracker;
	unsigned int m_dynamicThreshold;
	bool m_showRegisterInfo;

	AliasMap m_aliases;

	// Aliases whose sameNamePredecessor edge --coalesce-float-writes added, so
	// the edges can be withdrawn and allocation retried if they cost more than
	// they save.
	std::vector<Alias*> m_coalescedWrites;
};

#include "RegisterAllocator.inl"

}

#endif
