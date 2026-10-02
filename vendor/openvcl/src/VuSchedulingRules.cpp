#include "VuSchedulingRules.h"

#include "Expression.h"
#include "Math.h"
#include "Operand.h"
#include "VuInstructionInfo.h"
#include "VuTokenResourceAccess.h"

#include <cstdlib>
#include <sstream>

namespace vcl
{

namespace
{
	bool containsKey( const std::list<std::string>& keys, const std::string& key )
	{
		for( std::list<std::string>::const_iterator i = keys.begin(); i != keys.end(); ++i )
		{
			if( *i == key )
				return true;
		}
		return false;
	}

	bool intersectsKeys( const std::list<std::string>& a, const std::list<std::string>& b )
	{
		for( std::list<std::string>::const_iterator i = a.begin(); i != a.end(); ++i )
		{
			if( containsKey( b, *i ) )
				return true;
		}
		return false;
	}

	bool tokenTouchesImplicitResource( const Token& token, Token::Argument::Type type, bool write )
	{
		unsigned int resource = VU_RESOURCE_NONE;
		switch( type )
		{
			case Token::Argument::I: resource = VU_RESOURCE_I; break;
			case Token::Argument::Q: resource = VU_RESOURCE_Q; break;
			case Token::Argument::P: resource = VU_RESOURCE_P; break;
			case Token::Argument::R: resource = VU_RESOURCE_R; break;
			case Token::Argument::ACCUMULATOR: resource = VU_RESOURCE_ACC; break;
			default: return false;
		}

		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess( token, access ) )
			return false;
		return (write ? access.implicitWrites : access.implicitReads) & resource;
	}

	void implicitResources( const Token& token, unsigned int& reads, unsigned int& writes )
	{
		VuTokenResourceAccess access;
		if( buildVuTokenResourceAccess( token, access ) )
		{
			reads = access.implicitReads;
			writes = access.implicitWrites;
			return;
		}

		reads = VU_RESOURCE_NONE;
		writes = VU_RESOURCE_NONE;
	}

	bool hasImplicitPairDependency( const Token& earlier, const Token& later )
	{
		unsigned int earlierReads = 0;
		unsigned int earlierWrites = 0;
		unsigned int laterReads = 0;
		unsigned int laterWrites = 0;
		implicitResources( earlier, earlierReads, earlierWrites );
		implicitResources( later, laterReads, laterWrites );

		if( earlierWrites & laterReads )
			return true;

		// Two writes of an ACCUMULATING resource are not a hazard: they merge, so
		// there is no order between them to preserve. Without this the status
		// declarations alone would refuse to pair `mul` with `div` - both contribute
		// to the status register - and refuse every other upper/lower row whose two
		// halves happen to touch it. Everything that DOES order accumulating writes
		// goes through a read: a reader on either side is caught by the RAW and WAR
		// tests around this one, and FSSET is declared a reader for that reason.
		if( laterWrites & earlierWrites & ~static_cast<unsigned int>( VU_RESOURCE_ACCUMULATING ) )
			return true;

		return (laterWrites & earlierReads) != 0;
	}

	bool memoryBaseRegisterKey( const Token& token, std::string& key )
	{
		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess( token, access ) || !access.hasMemoryBase )
			return false;
		key = access.memoryBaseRegister;
		return true;
	}

	bool memoryOffset( const Token& token, long& offset )
	{
		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess( token, access ) || !access.hasMemoryOffset )
			return false;
		offset = access.memoryOffset;
		return true;
	}

	bool plainMemoryAccessesAreDistinct( const Token& a, const Token& b )
	{
		std::string movedBase;
		std::string crossedBase;
		if( !memoryBaseRegisterKey(a, movedBase) || !memoryBaseRegisterKey(b, crossedBase) )
			return false;

		if( movedBase != crossedBase )
			return true;

		long movedOffset = 0;
		long crossedOffset = 0;
		if( !memoryOffset(a, movedOffset) || !memoryOffset(b, crossedOffset) )
			return false;

		return movedOffset != crossedOffset;
	}

	bool plainLoadCanMoveBeforePlainStore( const Token& moved, const Token& crossed )
	{
		if( !isVuPlainMemoryLoad(moved) || !isVuPlainMemoryStore(crossed) )
			return false;

		return plainMemoryAccessesAreDistinct(moved, crossed);
	}

	bool plainStoreCanMoveBeforePlainMemory( const Token& moved, const Token& crossed )
	{
		if( !isVuPlainMemoryStore(moved) )
			return false;
		if( !isVuPlainMemoryStore(crossed) && !isVuPlainMemoryLoad(crossed) )
			return false;

		return plainMemoryAccessesAreDistinct(moved, crossed);
	}

	bool hasMemoryOrControlSideEffect( const Token& token );

	bool plainStoreCanMoveBeforeComputation( const Token& moved, const Token& crossed )
	{
		if( !isVuPlainMemoryStore(moved) )
			return false;
		return !hasMemoryOrControlSideEffect(crossed);
	}

	bool computationCanMoveBeforePlainStore( const Token& moved, const Token& crossed )
	{
		if( !isVuPlainMemoryStore(crossed) )
			return false;
		if( isVuPlainMemoryLoad(moved) )
			return false;
		return !hasMemoryOrControlSideEffect(moved);
	}

	bool hasMemoryOrControlSideEffect( const Token& token )
	{
		if( !token.operand() )
			return true;
		if( token.flags() & Token::PREORDERED )
			return true;

		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess( token, access ) )
			return true;
		if( access.branchDelaySlots > 0 )
			return true;
		if( access.memoryFlags & (VU_MEMORY_FLAG_PREDEC | VU_MEMORY_FLAG_POSTINC) )
			return true;
		return access.memoryKind == VU_MEMORY_STORE
		    || access.memoryKind == VU_MEMORY_XGKICK;
	}

	bool registerKey( const Token::Argument& arg, std::string& key )
	{
		return vuRegisterKey( arg, key );
	}

	bool isIntegerImmediateAdd( const Token& token, std::string& dstReg, std::string& srcReg, long& immediate )
	{
		if( !token.operand() )
			return false;
		if( token.flags() & (Token::PREORDERED | Token::E | Token::D | Token::T) )
			return false;

		const std::string name = lowerVuTokenName(token);
		if( name != "iaddiu" )
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

		if( !registerKey(*dst, dstReg) || !registerKey(*src, srcReg) )
			return false;

		Expression e;
		e.setCustomOperators( Math::mathOperators() );
		if( !e.process( (*imm).immediate() ) || !e.solve() )
			return false;

		immediate = static_cast<long>(e.result());
		return true;
	}

	bool isSelfIntegerImmediateAdd( const Token& token, std::string& reg, long& immediate )
	{
		if( token.label().length() != 0 )
			return false;

		std::string srcReg;
		if( !isIntegerImmediateAdd(token, reg, srcReg, immediate) )
			return false;
		return reg == srcReg;
	}

	bool setIntegerImmediate( Token& token, long immediate )
	{
		std::list<Token::Argument>& args = token.arguments();
		if( args.size() != 3 )
			return false;
		std::list<Token::Argument>::iterator i = args.begin();
		++i;
		++i;
		std::stringstream s;
		s << immediate;
		(*i).setImmediate(s.str());
		return true;
	}
}

bool isVuEmittableInstruction( const Token& token )
{
	if( !token.operand() )
		return false;
	if( token.flags() & Token::IGNORED )
		return false;
	if( token.flags() & Token::BRANCH_DELAY_FILLER )
		return false;
	if( !(token.flags() & Token::PROCESSED) && !(token.operand()->flags() & Operand::PREPROCESSOR) )
		return false;
	if( token.operand()->flags() & Operand::PREPROCESSOR )
		return false;
	if( token.operand()->flags() & Operand::FILTERED )
		return false;
	if( token.operand()->unit() == Operand::ENTER )
		return false;
	if( token.operand()->unit() == Operand::EXIT )
		return false;
	return true;
}

std::string lowerVuTokenName( const Token& token )
{
	std::string name;
	if( token.operand() )
		name = token.operand()->name();
	for( std::string::iterator i = name.begin(); i != name.end(); ++i )
	{
		if( *i >= 'A' && *i <= 'Z' )
			*i = char(*i - 'A' + 'a');
	}
	return name;
}

bool isVuMtir( const Token& token )
{
	return lowerVuTokenName(token) == "mtir";
}

bool isVuFtoiConversion( const std::string& name )
{
	return name == "ftoi0" || name == "ftoi4"
	    || name == "ftoi12" || name == "ftoi15";
}

bool isVuLoadToFtoiBypassProducer( const std::string& name )
{
	const VuInstructionInfo* info = findVuInstructionInfo( name );
	return info && (info->bypassFlags & VU_BYPASS_LOAD_TO_FTOI) != 0;
}

bool isVuMinii( const std::string& name )
{
	return name == "minii";
}

bool isVuLoadToMiniiBypassProducer( const std::string& name )
{
	const VuInstructionInfo* info = findVuInstructionInfo( name );
	return info && (info->bypassFlags & VU_BYPASS_LOAD_TO_MINII) != 0;
}

// The MAC flag register and the status register are two different registers, and
// this used to answer for both: `fsand`/`fseq`/`fsor` were listed here as MAC
// readers while the instruction table declared them as reading nothing at all, so
// the compiler gave two different answers to "does this read MAC?" depending on
// which of the two it asked - and VuLatencyTracker asked the table, the one that
// said no. The table now declares VU_RESOURCE_STATUS and this list answers only
// about MAC; every caller that wanted "is this a flag reader" asks both.
bool isVuMacReader( const std::string& name )
{
	return name == "fmand" || name == "fmeq" || name == "fmor"
	    || name == "FMAND" || name == "FMEQ" || name == "FMOR";
}

bool isVuStatusReader( const std::string& name )
{
	return name == "fsand" || name == "fseq" || name == "fsor"
	    || name == "FSAND" || name == "FSEQ" || name == "FSOR";
}

bool isVuMacOrStatusReader( const std::string& name )
{
	return isVuMacReader( name ) || isVuStatusReader( name );
}

bool isVuClipReader( const std::string& name )
{
	return name == "fcand" || name == "fceq" || name == "fcor" || name == "fcget"
	    || name == "FCAND" || name == "FCEQ" || name == "FCOR" || name == "FCGET";
}

bool isVuClipw( const std::string& name )
{
	if( name.size() < 5 )
		return false;
	return ( name.compare(0, 5, "clipw") == 0 )
	    || ( name.compare(0, 5, "CLIPw") == 0 )
	    || ( name.compare(0, 5, "CLIP" ) == 0 && (name[4] == 'w' || name[4] == 'W') );
}

bool vuTokenHasInstructionFlag( const Token& token, unsigned int flag )
{
	VuTokenResourceAccess access;
	return buildVuTokenResourceAccess( token, access )
	    && (access.instructionFlags & flag) != 0;
}

unsigned int vuTokenBranchDelaySlots( const Token& token )
{
	VuTokenResourceAccess access;
	if( !buildVuTokenResourceAccess( token, access ) )
		return 0;
	return access.branchDelaySlots;
}

bool isVuTerminalUnconditionalBranch( const Token& token )
{
	return vuTokenHasInstructionFlag( token, VU_INSTR_UNCONDITIONAL_BRANCH )
	    && !vuTokenHasInstructionFlag( token, VU_INSTR_LINK_BRANCH );
}

bool vuTokenReadsQ( const Token& token )
{
	return tokenTouchesImplicitResource(token, Token::Argument::Q, false);
}

bool vuTokenWritesQ( const Token& token )
{
	return tokenTouchesImplicitResource(token, Token::Argument::Q, true);
}

bool vuTokenReadsP( const Token& token )
{
	return tokenTouchesImplicitResource(token, Token::Argument::P, false);
}

bool vuTokenWritesP( const Token& token )
{
	return tokenTouchesImplicitResource(token, Token::Argument::P, true);
}

bool vuTokenReadsRegister( const Token& token, const std::string& key )
{
	std::list<std::string> reads;
	collectVuRegisterReadKeys(token, reads);
	return containsKey(reads, key);
}

void collectVuRegisterReadKeys( const Token& token, std::list<std::string>& reads )
{
	VuTokenResourceAccess access;
	if( !buildVuTokenResourceAccess( token, access ) )
		return;
	reads.insert( reads.end(), access.registerReads.begin(), access.registerReads.end() );
}

void collectVuRegisterWriteKeys( const Token& token, std::list<std::string>& writes )
{
	VuTokenResourceAccess access;
	if( !buildVuTokenResourceAccess( token, access ) )
		return;
	writes.insert( writes.end(), access.registerWrites.begin(), access.registerWrites.end() );
}

bool isVuMoveAsUpperMaxCandidate( const Token& token )
{
	if( !token.operand() )
		return false;
	if( token.flags() & (Token::PREORDERED | Token::E | Token::D | Token::T) )
		return false;
	if( lowerVuTokenName(token) != "move" )
		return false;

	unsigned int fields = token.fields();
	if( !vuUpperMoveWithWEnabled() && ( fields == 0 || (fields & Token::W) ) )
		return false;

	const std::list<Token::Argument>& args = token.arguments();
	if( args.size() != 2 )
		return false;

	std::list<Token::Argument>::const_iterator src = args.begin();
	if( src->type() != Token::Argument::FLOAT_REGISTER
	    || (src->flags() & (Token::Argument::INDIRECT
	                      | Token::Argument::PREDEC
	                      | Token::Argument::POSTINC)) )
		return false;
	++src;
	return src->type() == Token::Argument::FLOAT_REGISTER
	    && !(src->flags() & (Token::Argument::INDIRECT
	                       | Token::Argument::PREDEC
	                       | Token::Argument::POSTINC));
}

bool isVuZeroMoveFromVf00( const Token& token )
{
	if( !isVuMoveAsUpperMaxCandidate( token ) )
		return false;

	std::list<Token::Argument>::const_iterator src = token.arguments().begin();
	++src;
	return src->content() == Token::Argument::REGISTER
	    && src->regNumber() == 0;
}

bool vuTokenListReadsMac( const std::list<Token>& tokens )
{
	for( std::list<Token>::const_iterator i = tokens.begin(); i != tokens.end(); ++i )
	{
		if( i->operand() && isVuMacReader(i->operand()->name()) )
			return true;
	}
	return false;
}

bool vuTokenListReadsStatus( const std::list<Token>& tokens )
{
	for( std::list<Token>::const_iterator i = tokens.begin(); i != tokens.end(); ++i )
	{
		if( i->operand() && isVuStatusReader(i->operand()->name()) )
			return true;
	}
	return false;
}

unsigned int vuDeclaredHardwareResource( const Token& token )
{
	if( !token.operand() )
		return VU_RESOURCE_NONE;
	if( !(token.operand()->flags() & Operand::PREPROCESSOR) )
		return VU_RESOURCE_NONE;
	const std::string& n = token.operand()->name();
	if( n == "out_hw_acc" || n == "in_hw_acc" )       return VU_RESOURCE_ACC;
	if( n == "out_hw_i" || n == "in_hw_i" )           return VU_RESOURCE_I;
	if( n == "out_hw_q" || n == "in_hw_q" )           return VU_RESOURCE_Q;
	if( n == "out_hw_p" || n == "in_hw_p" )           return VU_RESOURCE_P;
	if( n == "out_hw_r" || n == "in_hw_r" )           return VU_RESOURCE_R;
	if( n == "out_hw_clip" || n == "in_hw_clip" )     return VU_RESOURCE_CLIP;
	// This used to answer VU_RESOURCE_MAC, which was the third copy of the
	// status/MAC conflation: the status register is its own resource now.
	if( n == "out_hw_status" || n == "in_hw_status" ) return VU_RESOURCE_STATUS;
	return VU_RESOURCE_NONE;
}

bool vuTokenListReadsClip( const std::list<Token>& tokens )
{
	for( std::list<Token>::const_iterator i = tokens.begin(); i != tokens.end(); ++i )
	{
		if( i->operand() && isVuClipReader(i->operand()->name()) )
			return true;
	}
	return false;
}

bool isVuPlainMemoryStore( const Token& token )
{
	VuTokenResourceAccess access;
	if( !buildVuTokenResourceAccess( token, access ) )
		return false;
	return access.memoryKind == VU_MEMORY_STORE
	    && (access.memoryFlags & (VU_MEMORY_FLAG_PREDEC | VU_MEMORY_FLAG_POSTINC)) == 0;
}

bool isVuPlainMemoryLoad( const Token& token )
{
	VuTokenResourceAccess access;
	if( !buildVuTokenResourceAccess( token, access ) )
		return false;
	return access.memoryKind == VU_MEMORY_LOAD
	    && (access.memoryFlags & (VU_MEMORY_FLAG_PREDEC | VU_MEMORY_FLAG_POSTINC)) == 0;
}

bool isVuXgkick( const Token& token )
{
	VuTokenResourceAccess access;
	return buildVuTokenResourceAccess( token, access )
	    && access.memoryKind == VU_MEMORY_XGKICK;
}

bool isVuMemoryOrderingAccess( const Token& token )
{
	VuTokenResourceAccess access;
	if( !buildVuTokenResourceAccess( token, access ) )
		return false;
	return access.memoryKind == VU_MEMORY_STORE
	    || access.memoryKind == VU_MEMORY_XGKICK
	    || access.memoryFlags != VU_MEMORY_FLAG_NONE;
}

bool isVuBoundaryOperand( const Token& token )
{
	if( !token.operand() )
		return false;

	const std::string& name = token.operand()->name();
	return name == "--barrier"
	    || name == "--cont"
	    || name == "--enter"
	    || name == "--endenter"
	    || name == "--exit"
	    || name == "--endexit";
}

bool isVuSchedulingBarrier( const Token& token )
{
	if( token.flags() & (Token::PREORDERED | Token::BRANCH_DELAY_FILLER) )
		return true;
	if( isVuBoundaryOperand( token ) )
		return true;

	VuTokenResourceAccess access;
	if( !buildVuTokenResourceAccess( token, access ) )
		return false;

	return access.branchDelaySlots > 0
	    || access.memoryKind == VU_MEMORY_XGKICK;
}

bool isVuReadyScheduleCandidate( const Token& token )
{
	if( token.label().length() != 0 )
		return false;
	if( !token.operand() )
		return false;
	if( token.operand()->isPreprocessor() )
		return false;
	if( token.flags() & (Token::PREORDERED | Token::IGNORED | Token::E | Token::D | Token::T) )
		return false;
	if( token.flags() & Token::BRANCH_DELAY_FILLER )
		return false;
	if( !token.operand()->isUpperExecutionPath() && !token.operand()->isLowerExecutionPath() )
		return false;

	VuTokenResourceAccess access;
	if( !buildVuTokenResourceAccess( token, access ) )
		return false;
	if( access.branchDelaySlots > 0 )
		return false;
	if( access.instructionFlags & (VU_INSTR_WAIT_Q | VU_INSTR_WAIT_P | VU_INSTR_BRANCH) )
		return false;
	if( access.memoryFlags != VU_MEMORY_FLAG_NONE )
		return false;
	if( access.memoryKind != VU_MEMORY_NONE
	    && access.memoryKind != VU_MEMORY_LOAD
	    && access.memoryKind != VU_MEMORY_STORE )
		return false;
	// Readers of the MAC/CLIP flags are excluded from scheduling by default, which
	// breaks the scheduling segment at every one of them - in a vertex pipeline
	// that is once per vertex, at the fcand after each clipw - and the scheduler
	// can only hide a latency with work inside its current segment, so those
	// breaks are where the nop padding comes from.
	//
	// The exclusion is not needed for correctness: buildVuDependencyGraph models
	// ACC/MAC/CLIP as precise resources, and addPreciseImplicitFlagDependencies
	// orders every writer of a flag against the reader it finds. The mask handed
	// to it only has to say whether the flag is read AFTER the range, which is why
	// the caller derives it from the segment's last token rather than per token.
	//
	// It stays behind --schedule-flag-readers because it does change what the
	// known-loop-optimisation path decides: with a cheaper unpipelined estimate,
	// that path stops software-pipelining the loops its own tests pin down. Off by
	// default, nothing moves for anyone who does not ask.
	if( !vuScheduleFlagReadersEnabled()
	    && (access.implicitReads & (VU_RESOURCE_MAC | VU_RESOURCE_CLIP | VU_RESOURCE_STATUS)) )
		return false;

	return true;
}

namespace
{
	bool g_scheduleFlagReaders = false;
	bool g_fmacInterlock = false;
	bool g_pairBestOfCycles = false;
	unsigned int g_flagVisibilityLatency = 4;
	unsigned int g_clipFlagVisibilityLatency = 4;
	unsigned int g_clipFlagSchedulingLatency = 4;
	bool g_exemptFullClipMasks = false;
	bool g_clipExemptionBestOf = false;
	unsigned int g_integerLoadReadyCycles = 0;
	bool g_emitDelayFillers = false;
	bool g_branchInterlock = false;
	bool g_loopLivenessAlways = false;
	bool g_upperMoveWithW = false;
	bool g_coalesceFloatWrites = false;
	bool g_trimUncarriedRanges = false;
	bool g_splitDeadFloatRanges = false;
	bool g_spreadFloatRegisters = false;
	bool g_spreadFloatRegistersWebsOnly = false;
	bool g_sinkLoads = false;
	bool g_sinkLoadsAcrossStores = false;
	bool g_sinkLoadsIntoLoops = false;
	bool g_sinkLoadsPastBranches = false;
	bool g_dropDeadWrites = false;
	bool g_showPairMisses = false;
	bool g_pairBestOfTwo = false;
	bool g_pairBestOfMany = false;
	bool g_branchBubbleOnDependency = false;
}

void setVuScheduleFlagReadersEnabled( bool enabled )
{
	g_scheduleFlagReaders = enabled;
}

bool vuScheduleFlagReadersEnabled()
{
	return g_scheduleFlagReaders;
}

void setVuFmacInterlockEnabled( bool enabled )
{
	g_fmacInterlock = enabled;
}

bool vuFmacInterlockEnabled()
{
	return g_fmacInterlock;
}

void setVuPairBestOfCyclesEnabled( bool enabled )
{
	g_pairBestOfCycles = enabled;
}

bool vuPairBestOfCyclesEnabled()
{
	return g_pairBestOfCycles;
}

void setVuFlagVisibilityLatency( unsigned int cycles )
{
	g_flagVisibilityLatency = cycles > 0 ? cycles : 1;
}

unsigned int vuFlagVisibilityLatency()
{
	return g_flagVisibilityLatency;
}

// The CLIP flag is tracked separately from the MAC flags, because the two are not
// interchangeable. MAC flags summarise the last FMAC; the CLIP flag register is a
// 24-bit shift window into which every CLIP pushes six new bits. Reading it early
// therefore does not return a partly-settled answer - it returns a DIFFERENT
// vertex's answer, with the mask silently selecting the wrong window position. A
// full-window test (0x3FFFF, "is anything outside") survives that; a single-bit
// positional test does not, and that is what a Sutherland-Hodgman edge loop does.
void setVuClipFlagVisibilityLatency( unsigned int cycles )
{
	g_clipFlagVisibilityLatency = cycles > 0 ? cycles : 1;
}

unsigned int vuClipFlagVisibilityLatency()
{
	return g_clipFlagVisibilityLatency;
}

// The scheduler and the emitter need different numbers for the same wait. The emitter
// enforces the hardware minimum in EMITTED ROWS. The scheduler works in cycles, where
// an interlocked wait is free, so giving it the bare minimum leaves it no reason to
// move work into the gap - and every row the emitter then has to pad is a wasted
// instruction. A larger scheduling figure asks it to spread the flag reader further
// out, which it can only do by filling the space with work that was going to be
// emitted anyway.
void setVuClipFlagSchedulingLatency( unsigned int cycles )
{
	g_clipFlagSchedulingLatency = cycles > 0 ? cycles : 1;
}

unsigned int vuClipFlagSchedulingLatency()
{
	return g_clipFlagSchedulingLatency;
}

bool vuClipReadIsFullWindow( const Token& token )
{
	// The mask is the reader's immediate argument. Only the whole-window values are
	// position-independent: 0x3FFFF is the eighteen bits three vertices occupy and
	// 0xFFFFFF the whole register, and both mean "is anything outside". A mask that
	// names particular entries - one bit, or several OR-ed - is reading positions.
	//
	// POSITION-INDEPENDENT IS NOT LATENCY-INDEPENDENT, and reading this predicate as
	// if it were is the third miscompile of this family. "Which entry the bits sit in
	// does not matter" is a statement about WHERE a judgement is, not about WHETHER it
	// has arrived; 0x3FFFF is three entries wide, so a read taken before its own CLIP
	// has landed does not get a partly-settled answer, it gets the window shifted by a
	// whole vertex - the previous vertex's verdict, wearing this one's name. Both
	// callers therefore make every reader wait, and consult this only to choose
	// between the hardware minimum and the larger scheduling figure.
	//
	// An fcget takes no mask at all; it copies the register, so every position of it
	// matters and it is never full-window by this test (the loop below finds no
	// immediate and falls through to false).
	for( std::list<Token::Argument>::const_iterator a = token.arguments().begin();
	     a != token.arguments().end(); ++a )
	{
		if( a->type() != Token::Argument::IMMEDIATE )
			continue;
		const std::string& text = a->immediate();
		if( text.empty() )
			continue;
		const unsigned long mask = std::strtoul( text.c_str(), NULL, 0 );
		return mask == 0 || mask == 0x3FFFFul || mask == 0xFFFFFFul;
	}
	return false;      // no mask to judge by: assume the position matters
}

// Does this status reader depend on WHICH contributor ran last, or only on the set
// of them? The status register is two registers in one: bits 0..5 (Z S U O I D)
// describe the last operation and are replaced by every FMAC, bits 6..11
// (ZS SS US OS IS DS) accumulate until FSSET clears them.
//
// A reader whose mask names only sticky bits - `fsand VI01,0x0C00` asks "did
// anything divide by zero or go invalid since the last fsset" - gets the same
// answer whatever order the contributors ran in, so it needs nothing but "all of
// them are behind me". A reader that names a non-sticky bit is asking about the
// instruction immediately in front of it and the last writer has to stay last.
//
// This is the same shape as vuClipReadIsFullWindow: a mask the source already
// carries, telling the scheduler how much ordering the read actually needs. No
// mask to judge by (or one that cannot be parsed) means the position matters.
bool vuStatusReadNeedsLastWriter( const Token& token )
{
	const unsigned long VU_STATUS_NON_STICKY_BITS = 0x3Ful;

	for( std::list<Token::Argument>::const_iterator a = token.arguments().begin();
	     a != token.arguments().end(); ++a )
	{
		if( a->type() != Token::Argument::IMMEDIATE )
			continue;
		const std::string& text = a->immediate();
		if( text.empty() )
			continue;
		char* endptr = NULL;
		const unsigned long mask = std::strtoul( text.c_str(), &endptr, 0 );
		if( endptr == text.c_str() || (endptr && *endptr != '\0') )
			return true;                 // not a plain literal: assume it matters
		return (mask & VU_STATUS_NON_STICKY_BITS) != 0;
	}
	return true;
}

// --exempt-full-clip-masks. NARROWED, and inert at the shipped latencies.
//
// It used to mean "a full-window reader need not wait at all", matching an exemption
// the emitter had made since padForClipFlagWindow was written. Both were wrong in the
// same way - see vuClipReadIsFullWindow - and both are gone. What is left is the only
// part of the exemption that was ever sound: a position-independent reader needs no
// more than vuClipFlagVisibilityLatency(), the wait for its own six bits to land,
// while a positional one also needs vuClipFlagSchedulingLatency(), the spread that
// keeps the rest of the window where source order put it. Those two are both 4 today,
// so the flag changes no byte; it stays because they are separately calibrated and
// are not required to stay equal.
void setVuExemptFullClipMasksEnabled( bool enabled )
{
	g_exemptFullClipMasks = enabled;
}

bool vuExemptFullClipMasksEnabled()
{
	return g_exemptFullClipMasks;
}

// --clip-exemption-best-of. Taking the exemption is not free downstream: the freed
// reader issues early, the list scheduler then sees a different ready set for every
// row after it, and on two of the ten resident programs that came out one row worse
// even though the reader's own segment did not. Rather than argue about which way is
// right, schedule the whole program both ways and keep the shorter one.
void setVuClipExemptionBestOfEnabled( bool enabled )
{
	g_clipExemptionBestOf = enabled;
}

bool vuClipExemptionBestOfEnabled()
{
	return g_clipExemptionBestOf;
}

bool vuTokenListHasFullWindowClipReader( const std::list<Token>& tokens )
{
	// The two arms of the best-of differ only where a full-window CLIP reader exists;
	// with none in the program the second schedule is provably the first one, so it is
	// not worth its compile time. 44 of the 70 programs this fork compiles take this
	// exit; the other 26 pay for a second pass and 10 of them keep its result.
	for( std::list<Token>::const_iterator i = tokens.begin(); i != tokens.end(); ++i )
	{
		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess( *i, access ) )
			continue;
		if( (access.implicitReads & VU_RESOURCE_CLIP) == 0 )
			continue;
		if( vuClipReadIsFullWindow( *i ) )
			return true;
	}
	return false;
}

void setVuIntegerLoadReadyCycles( unsigned int cycles )
{
	g_integerLoadReadyCycles = cycles;
}

unsigned int vuIntegerLoadReadyCycles()
{
	return g_integerLoadReadyCycles;
}

void setVuEmitDelayFillersEnabled( bool enabled )
{
	g_emitDelayFillers = enabled;
}

bool vuEmitDelayFillersEnabled()
{
	return g_emitDelayFillers;
}

void setVuBranchBubbleOnDependencyEnabled( bool enabled )
{
	g_branchBubbleOnDependency = enabled;
}

bool vuBranchBubbleOnDependencyEnabled()
{
	return g_branchBubbleOnDependency;
}

void setVuLoopLivenessAlwaysEnabled( bool enabled )
{
	g_loopLivenessAlways = enabled;
}

bool vuLoopLivenessAlwaysEnabled()
{
	return g_loopLivenessAlways;
}

// A move covering w, and a move with no field suffix at all (fields() == 0, which means
// all four), may be emitted as `max dst,src,src` in the UPPER pipe. MAX of a value with
// itself is that value on every component, and SCE's own output does this 25 times across
// the engine's five clip programs. Off by default: upstream refuses these deliberately and
// has a test that says so.
void setVuUpperMoveWithWEnabled( bool enabled )
{
	g_upperMoveWithW = enabled;
}

bool vuUpperMoveWithWEnabled()
{
	return g_upperMoveWithW;
}

void setVuCoalesceFloatWritesEnabled( bool enabled )
{
	g_coalesceFloatWrites = enabled;
}

bool vuCoalesceFloatWritesEnabled()
{
	return g_coalesceFloatWrites;
}

void setVuSplitDeadFloatRangesEnabled( bool enabled )
{
	g_splitDeadFloatRanges = enabled;
}

bool vuSplitDeadFloatRangesEnabled()
{
	return g_splitDeadFloatRanges;
}

void setVuSpreadFloatRegistersEnabled( bool enabled )
{
	g_spreadFloatRegisters = enabled;
}

bool vuSpreadFloatRegistersEnabled()
{
	return g_spreadFloatRegisters;
}

void setVuSpreadFloatRegistersWebsOnly( bool enabled )
{
	g_spreadFloatRegistersWebsOnly = enabled;
}

bool vuSpreadFloatRegistersWebsOnly()
{
	return g_spreadFloatRegistersWebsOnly;
}

void setVuTrimUncarriedRangesEnabled( bool enabled )
{
	g_trimUncarriedRanges = enabled;
}

bool vuTrimUncarriedRangesEnabled()
{
	return g_trimUncarriedRanges;
}

void setVuSinkLoadsEnabled( bool enabled )
{
	g_sinkLoads = enabled;
}

bool vuSinkLoadsEnabled()
{
	return g_sinkLoads;
}

void setVuSinkLoadsAcrossStoresEnabled( bool enabled )
{
	g_sinkLoadsAcrossStores = enabled;
}

bool vuSinkLoadsAcrossStoresEnabled()
{
	return g_sinkLoadsAcrossStores;
}

void setVuSinkLoadsIntoLoopsEnabled( bool enabled )
{
	g_sinkLoadsIntoLoops = enabled;
}

bool vuSinkLoadsIntoLoopsEnabled()
{
	return g_sinkLoadsIntoLoops;
}

void setVuSinkLoadsPastBranchesEnabled( bool enabled )
{
	g_sinkLoadsPastBranches = enabled;
}

bool vuSinkLoadsPastBranchesEnabled()
{
	return g_sinkLoadsPastBranches;
}

// --drop-dead-writes: delete an instruction whose only effect is a value nothing
// ever reads. Off by default because it deletes code the author wrote, and an
// author who writes a value the program never reads may be describing an
// interface this compiler cannot see.
void setVuDropDeadWritesEnabled( bool enabled )
{
	g_dropDeadWrites = enabled;
}

bool vuDropDeadWritesEnabled()
{
	return g_dropDeadWrites;
}

// Diagnostics only: report why a row went out with one slot used.
void setVuShowPairMissesEnabled( bool enabled )
{
	g_showPairMisses = enabled;
}

bool vuShowPairMissesEnabled()
{
	return g_showPairMisses;
}

// Schedule a segment both ways - with and without the unpaired-row re-pick - and keep the
// shorter result. Off by default: it reorders a cyclic prefix that a software-pipelining
// test pins down, and nobody outside this engine asked for the trade.
void setVuPairBestOfTwoEnabled( bool enabled )
{
	g_pairBestOfTwo = enabled;
}

bool vuPairBestOfTwoEnabled()
{
	return g_pairBestOfTwo;
}

// --pair-best-of-many: the same trade as --pair-best-of-two over a table of ready-list
// heuristics instead of two, compared on emitted words rather than on issue-slot count.
// Off by default for the same reason, and it supersedes --pair-best-of-two when both are
// given, the table's first two entries being exactly that pair.
void setVuPairBestOfManyEnabled( bool enabled )
{
	g_pairBestOfMany = enabled;
}

bool vuPairBestOfManyEnabled()
{
	return g_pairBestOfMany;
}

void setVuBranchInterlockEnabled( bool enabled )
{
	g_branchInterlock = enabled;
}

bool vuBranchInterlockEnabled()
{
	return g_branchInterlock;
}

bool isVuLowerPipe( const Token& token )
{
	return token.operand() && token.operand()->isLowerExecutionPath();
}

bool isVuLongLatencyProducer( const Token& token )
{
	VuTokenResourceAccess access;
	return buildVuTokenResourceAccess( token, access )
	    && (access.instructionFlags & (VU_INSTR_WRITES_Q | VU_INSTR_WRITES_P)) != 0;
}

bool isVuLatencyLoad( const Token& token )
{
	VuTokenResourceAccess access;
	return buildVuTokenResourceAccess( token, access )
	    && access.memoryKind == VU_MEMORY_LOAD;
}

bool vuTokensHaveDataDependency( const Token& a, const Token& b )
{
	VuTokenResourceAccess aAccess;
	VuTokenResourceAccess bAccess;
	if( !buildVuTokenResourceAccess( a, aAccess ) || !buildVuTokenResourceAccess( b, bAccess ) )
		return false;

	return intersectsKeys( aAccess.registerWrites, bAccess.registerReads )
	    || intersectsKeys( aAccess.registerWrites, bAccess.registerWrites );
}

bool vuTokenCanMoveBefore( const Token& moved,
                           const Token& crossed,
                           unsigned int ignoredImplicitWawResources )
{
	if( hasMemoryOrControlSideEffect(moved) || hasMemoryOrControlSideEffect(crossed) )
	{
		if( !plainLoadCanMoveBeforePlainStore(moved, crossed)
		    && !plainStoreCanMoveBeforePlainMemory(moved, crossed)
		    && !plainStoreCanMoveBeforeComputation(moved, crossed)
		    && !computationCanMoveBeforePlainStore(moved, crossed) )
			return false;
	}

	unsigned int movedReadsImplicit = 0, movedWritesImplicit = 0;
	unsigned int crossedReadsImplicit = 0, crossedWritesImplicit = 0;
	implicitResources(moved, movedReadsImplicit, movedWritesImplicit);
	implicitResources(crossed, crossedReadsImplicit, crossedWritesImplicit);
	if( movedWritesImplicit & crossedReadsImplicit )
		return false;
	if( crossedWritesImplicit & movedReadsImplicit )
		return false;
	// Accumulating writes commute, so a WAW between two of them is not a barrier
	// here either - see hasImplicitPairDependency. The reads are what order them,
	// and both read tests are above this line.
	if( (movedWritesImplicit & crossedWritesImplicit
	     & ~ignoredImplicitWawResources
	     & ~static_cast<unsigned int>( VU_RESOURCE_ACCUMULATING )) != 0 )
		return false;

	if( vuTokensHaveDataDependency(moved, crossed) || vuTokensHaveDataDependency(crossed, moved) )
		return false;

	return true;
}

bool vuTokenRangeCanBeCrossed( const Token& first, const Token& last )
{
	return !hasMemoryOrControlSideEffect(first) && !hasMemoryOrControlSideEffect(last);
}

bool vuTokenPairResourcesAreIndependent( const Token& a,
                                         const Token& b,
                                         bool aWritesMac,
                                         bool bWritesMac )
{
	if( !a.operand() || !b.operand() )
		return false;

	VuTokenResourceAccess aAccess;
	VuTokenResourceAccess bAccess;
	buildVuTokenResourceAccess( a, aAccess );
	buildVuTokenResourceAccess( b, bAccess );
	if( aAccess.branchDelaySlots > 0 )
		return false;
	if( bAccess.branchDelaySlots > 0
	    && (bAccess.instructionFlags & (VU_INSTR_LINK_BRANCH | VU_INSTR_REGISTER_BRANCH)) )
		return false;
	if( (aAccess.instructionFlags & (VU_INSTR_WAIT_Q | VU_INSTR_WAIT_P))
	    || (bAccess.instructionFlags & (VU_INSTR_WAIT_Q | VU_INSTR_WAIT_P)) )
		return false;

	if( aWritesMac && isVuMacReader(b.operand()->name()) )
		return false;
	if( bWritesMac && isVuMacReader(a.operand()->name()) )
		return false;
	if( isVuClipw(a.operand()->name()) && isVuClipReader(b.operand()->name()) )
		return false;
	if( isVuClipw(b.operand()->name()) && isVuClipReader(a.operand()->name()) )
		return false;

	if( hasImplicitPairDependency(a, b) )
		return false;

	if( vuTokensHaveDataDependency(a, b) )
		return false;
	if( vuTokensHaveDataDependency(b, a) )
		return false;

	return true;
}

void coalesceAdjacentVuIntegerAdds( std::list<Token>& tokens )
{
	for( std::list<Token>::iterator i = tokens.begin(); i != tokens.end(); )
	{
		std::list<Token>::iterator next = i;
		++next;
		if( next == tokens.end() )
			break;

		std::string dstReg;
		std::string srcReg;
		std::string nextReg;
		long immediate = 0;
		long nextImmediate = 0;
		if( !isIntegerImmediateAdd(*i, dstReg, srcReg, immediate)
		    || !isSelfIntegerImmediateAdd(*next, nextReg, nextImmediate)
		    || dstReg != nextReg )
		{
			++i;
			continue;
		}

		const long combined = immediate + nextImmediate;
		if( combined < -32768 || combined > 32767 || combined == 0 )
		{
			++i;
			continue;
		}

		if( setIntegerImmediate(*i, combined) )
			tokens.erase(next);
		else
			++i;
	}
}

}
