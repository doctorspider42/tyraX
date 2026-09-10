#include "VuLatencyTracker.h"

#include "Operand.h"
#include "VuInstructionInfo.h"
#include "VuSchedulingRules.h"
#include "VuTokenResourceAccess.h"

#include <list>

namespace vcl
{

namespace
{
	bool tokenReadsImplicitResource( const Token& token, unsigned int resource )
	{
		VuTokenResourceAccess access;
		return buildVuTokenResourceAccess( token, access )
		    && (access.implicitReads & resource) != 0;
	}

	bool tokenWritesImplicitResource( const Token& token, unsigned int resource )
	{
		VuTokenResourceAccess access;
		return buildVuTokenResourceAccess( token, access )
		    && (access.implicitWrites & resource) != 0;
	}

	// How long after the last CLIP push may this token read the flag? 0 means it does
	// not read it at all.
	//
	// EVERY reader waits. The mask decides how LONG, never whether: a
	// position-independent mask needs no more than the hardware minimum - its own six
	// bits have to have LANDED - while a positional one also needs the window to be in
	// the alignment the source gives it, which is the (never smaller) scheduling
	// figure. Both are 4, so --exempt-full-clip-masks changes nothing today; it used
	// to return false here and remove the wait altogether, and that is the miscompile
	// documented on vuClipReadIsFullWindow.
	int tokenClipReadLatency( const Token& token )
	{
		if( !tokenReadsImplicitResource( token, VU_RESOURCE_CLIP ) )
			return 0;
		if( vuExemptFullClipMasksEnabled() && vuClipReadIsFullWindow( token ) )
			return static_cast<int>( vuClipFlagVisibilityLatency() );
		return static_cast<int>( vuClipFlagSchedulingLatency() );
	}

	int bypassLatencyReduction( const std::string& mnemonic, int fallback )
	{
		const VuInstructionInfo* info = findVuInstructionInfo( mnemonic );
		return info ? static_cast<int>( info->latency ) : fallback;
	}
}

VuLatencyTracker::VuLatencyTracker()
{
	reset();
}

void VuLatencyTracker::reset()
{
	m_qReadyCycle = -10;
	m_pReadyCycle = -10;
	m_lastFMACCycle = -10;
	m_lastStatusCycle = -10;
	m_lastClipwCycle = -10;
	m_registerReadyCycle.clear();
	m_registerProducerMnemonic.clear();
}

int VuLatencyTracker::readHazardDelay( const Token& token,
                                       const Token* partner,
                                       int currentCycle ) const
{
	return readHazardDelayImpl( token, partner, currentCycle, false );
}

int VuLatencyTracker::manualReadHazardDelay( const Token& token,
                                             const Token* partner,
                                             int currentCycle ) const
{
	return readHazardDelayImpl( token, partner, currentCycle, true );
}

namespace
{
	// Register keys are the allocated register names, "VF00".."VF31" /
	// "VI00".."VI15" (see vuRegisterKey).
	// Producers whose result a BRANCH may read without a padding word: the
	// hardware stalls for these on its own. Integer loads and the flag readers
	// (which write an integer register) qualify; an ordinary integer op does not
	// - see the file comment for the SCE output this is calibrated against.
	bool isBranchInterlockedProducer( const std::string& mnemonic )
	{
		return mnemonic.compare( 0, 3, "ilw" ) == 0
		    || mnemonic.compare( 0, 2, "lq" ) == 0
		    || isVuClipReader( mnemonic )
		    || isVuMacOrStatusReader( mnemonic );
	}
	bool isFloatRegisterKey( const std::string& key )
	{
		return key.length() >= 2
		    && (key[0] == 'V' || key[0] == 'v')
		    && (key[1] == 'F' || key[1] == 'f');
	}
}

int VuLatencyTracker::readHazardDelayImpl( const Token& token,
                                           const Token* partner,
                                           int currentCycle,
                                           bool skipInterlockedRegisters ) const
{
	std::list<std::string> reads;
	collectVuRegisterReadKeys( token, reads );
	if( partner )
		collectVuRegisterReadKeys( *partner, reads );

	bool readsQ = vuTokenReadsQ( token );
	bool readsP = vuTokenReadsP( token );
	if( partner )
	{
		readsQ = readsQ || vuTokenReadsQ( *partner );
		readsP = readsP || vuTokenReadsP( *partner );
	}

	int needed = 0;
	for( std::list<std::string>::const_iterator i = reads.begin(); i != reads.end(); ++i )
	{
		// The FMAC pipeline interlocks on VF registers: the hardware stalls by
		// itself, so this wait must not be paid for in instruction words.
		if( skipInterlockedRegisters && isFloatRegisterKey( *i ) )
			continue;
		// Same distinction for the integer file, but it depends on the CONSUMER:
		// a branch reading a load result or a flag-reader result stalls in
		// hardware, so those words are not ours to spend either.
		if( skipInterlockedRegisters && vuBranchInterlockEnabled()
			&& vuTokenHasInstructionFlag( token, VU_INSTR_BRANCH ) )
		{
			std::map<std::string, std::string>::const_iterator branchProducer =
			    m_registerProducerMnemonic.find( *i );
			if( branchProducer != m_registerProducerMnemonic.end()
			    && isBranchInterlockedProducer( branchProducer->second ) )
				continue;
		}

		std::map<std::string, int>::const_iterator ready = m_registerReadyCycle.find( *i );
		if( ready == m_registerReadyCycle.end() )
			continue;

		int readyCycle = ready->second;
		std::map<std::string, std::string>::const_iterator producer =
		    m_registerProducerMnemonic.find( *i );
		if( producer != m_registerProducerMnemonic.end()
		    && isVuFtoiConversion( producer->second )
		    && ( (isVuMtir( token ) && vuTokenReadsRegister( token, *i ))
		         || (partner && isVuMtir( *partner ) && vuTokenReadsRegister( *partner, *i )) ) )
			readyCycle -= bypassLatencyReduction( producer->second, 4 );
		if( producer != m_registerProducerMnemonic.end()
		    && isVuLoadToFtoiBypassProducer( producer->second )
		    && ( (isVuFtoiConversion( lowerVuTokenName( token ) ) && vuTokenReadsRegister( token, *i ))
		         || (partner && isVuFtoiConversion( lowerVuTokenName( *partner ) )
		             && vuTokenReadsRegister( *partner, *i )) ) )
			readyCycle -= bypassLatencyReduction( producer->second, 4 );
		if( producer != m_registerProducerMnemonic.end()
		    && isVuLoadToMiniiBypassProducer( producer->second )
		    && ( (isVuMinii( lowerVuTokenName( token ) ) && vuTokenReadsRegister( token, *i ))
		         || (partner && isVuMinii( lowerVuTokenName( *partner ) )
		             && vuTokenReadsRegister( *partner, *i )) ) )
			readyCycle -= 2;

		const int gap = readyCycle - currentCycle;
		if( gap > needed )
			needed = gap;
	}

	if( readsQ )
	{
		const int gap = m_qReadyCycle - currentCycle;
		if( gap > needed )
			needed = gap;
	}
	if( readsP )
	{
		const int gap = m_pReadyCycle - currentCycle;
		if( gap > needed )
			needed = gap;
	}

	const int flagCycle = currentCycle + needed;
	bool readsMac = tokenReadsImplicitResource( token, VU_RESOURCE_MAC );
	bool readsStatus = tokenReadsImplicitResource( token, VU_RESOURCE_STATUS );
	int clipLatency = tokenClipReadLatency( token );
	if( partner && partner->operand() )
	{
		readsMac = readsMac || tokenReadsImplicitResource( *partner, VU_RESOURCE_MAC );
		readsStatus = readsStatus || tokenReadsImplicitResource( *partner, VU_RESOURCE_STATUS );
		const int partnerClipLatency = tokenClipReadLatency( *partner );
		if( partnerClipLatency > clipLatency )
			clipLatency = partnerClipLatency;
	}

	// How long after its producer a flag may be read. 4 by default;
	// --sce-flag-latency calibrates it to the 1 that SCE's vcl emits (see
	// vuFlagVisibilityLatency).
	const int flagLatency = static_cast<int>( vuFlagVisibilityLatency() );

	int flagDelay = 0;
	if( readsMac )
	{
		const int gap = flagCycle - m_lastFMACCycle;
		if( flagLatency - gap > flagDelay )
			flagDelay = flagLatency - gap;
	}
	// The status flags become visible on the same schedule as the MAC flags, and
	// they have a producer of their own: DIV/SQRT/RSQRT set the D and I bits without
	// touching MAC, so m_lastFMACCycle is not the cycle to measure from.
	if( readsStatus )
	{
		const int gap = flagCycle - m_lastStatusCycle;
		if( flagLatency - gap > flagDelay )
			flagDelay = flagLatency - gap;
	}
	if( clipLatency > 0 )
	{
		// Not flagLatency: the CLIP window is positional, so --sce-latencies must
		// not shorten this wait (see setVuClipFlagVisibilityLatency).
		const int gap = flagCycle - m_lastClipwCycle;
		if( clipLatency - gap > flagDelay )
			flagDelay = clipLatency - gap;
	}
	if( flagDelay > 0 )
		needed += flagDelay;

	return needed;
}

void VuLatencyTracker::recordWrites( const Token& token, int issueCycle, bool forceMacFlagWrite )
{
	if( forceMacFlagWrite || tokenWritesImplicitResource( token, VU_RESOURCE_MAC ) )
		m_lastFMACCycle = issueCycle;
	if( forceMacFlagWrite || tokenWritesImplicitResource( token, VU_RESOURCE_STATUS ) )
		m_lastStatusCycle = issueCycle;
	if( tokenWritesImplicitResource( token, VU_RESOURCE_CLIP ) )
		m_lastClipwCycle = issueCycle;

	if( !token.operand() || token.operand()->latency() <= 1 )
		return;

	int readyCycle = issueCycle + static_cast<int>( token.operand()->latency() ) + 1;

	// An integer load lands sooner than latency+1 says. The table gives ILW a
	// latency of 4, so this formula makes its result readable at issue+5, while
	// SCE's vcl reads one at issue+3 - measured as the minimum ilw -> integer-op
	// distance over the 25 microprograms of a real engine (85 samples), against
	// openvcl's own minimum of 5 on the same corpus. Those two extra cycles are
	// paid in nop words at every load-use pair. --sce-latencies calibrates it.
	if( vuIntegerLoadReadyCycles() > 0 )
	{
		VuTokenResourceAccess access;
		if( buildVuTokenResourceAccess( token, access )
		    && access.memoryKind == VU_MEMORY_LOAD
		    && access.memoryFlags == VU_MEMORY_FLAG_NONE )
		{
			const int calibrated = issueCycle + static_cast<int>( vuIntegerLoadReadyCycles() );
			if( calibrated < readyCycle )
				readyCycle = calibrated;
		}
	}

	std::list<std::string> writes;
	collectVuRegisterWriteKeys( token, writes );
	for( std::list<std::string>::const_iterator i = writes.begin(); i != writes.end(); ++i )
	{
		m_registerReadyCycle[*i] = readyCycle;
		m_registerProducerMnemonic[*i] = lowerVuTokenName( token );
	}

	if( vuTokenWritesQ( token ) )
		m_qReadyCycle = readyCycle;
	if( vuTokenWritesP( token ) )
		m_pReadyCycle = readyCycle;
}

void VuLatencyTracker::delayPipelinedResultsBy( int cycles )
{
	if( cycles <= 0 )
		return;
	// reset() parks both at -10 to mean "nothing is in flight", and pushing THAT
	// forward would invent a producer out of nothing: after a 60-cycle shift a
	// -10 becomes 50, and every Q reader below would wait for a division that
	// was never issued. A recorded readyCycle is issueCycle + latency + 1 with
	// issueCycle >= 0 and latency >= 2, so it is never negative, and the sign is
	// a sound test for "was anything recorded".
	if( m_qReadyCycle >= 0 )
		m_qReadyCycle += cycles;
	if( m_pReadyCycle >= 0 )
		m_pReadyCycle += cycles;
}

int VuLatencyTracker::qReadyCycle() const
{
	return m_qReadyCycle;
}

int VuLatencyTracker::pReadyCycle() const
{
	return m_pReadyCycle;
}

}
