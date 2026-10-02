#ifndef __OPENVCL_VULATENCYTRACKER_H__
#define __OPENVCL_VULATENCYTRACKER_H__

/*
 * VuLatencyTracker.h
 *
 * Shared VU readiness model used by code emission and scheduler analysis.
 */

#include "Token.h"

#include <map>
#include <string>

namespace vcl
{

class VuLatencyTracker
{
public:
	VuLatencyTracker();

	void reset();
	int readHazardDelay( const Token& token, const Token* partner, int currentCycle ) const;

	// The part of readHazardDelay() that the hardware will NOT resolve on its own,
	// i.e. what has to be spent as emitted instruction words rather than left to
	// the VU's own stall.
	//
	// The FMAC pipeline interlocks: an instruction whose source is still in flight
	// simply stalls, so padding a VF-to-VF latency costs micro memory and buys
	// nothing. Everything else here does need the words - the integer pipeline
	// feeding a branch or another integer op, the MAC/CLIP flags (not interlocked)
	// and Q/P. That split is exactly what SCE's vcl does: with its code-size
	// reduction pass disabled (-C) stapip_clip_c grows from 257 to 327
	// instructions, the extra 70 being nop/nop rows (88 vs 18), and the 18 it
	// keeps sit in front of branches, fcand and Q readers.
	int manualReadHazardDelay( const Token& token, const Token* partner, int currentCycle ) const;
	void recordWrites( const Token& token, int issueCycle, bool forceMacFlagWrite = false );

	int qReadyCycle() const;
	int pReadyCycle() const;

	// EVERY CYCLE IN THIS TRACKER IS MEASURED ON THE FALL-THROUGH TIMELINE.
	// A block reached by a forward branch is entered EARLIER than that timeline
	// says - the branch and its delay slot are the only rows between them,
	// while the timeline counted every row the branch jumped over. An
	// outstanding division is therefore less complete than qReadyCycle()
	// claims, by exactly that difference, and there is no hardware interlock on
	// Q or P to cover the gap: a `mulq` that issues early reads the PREVIOUS
	// quotient and says nothing.
	//
	// Only Q and P are pushed. The FMAC pipeline interlocks on VF registers and
	// --branch-interlock covers the integer results a branch reads, so those
	// waits are the hardware's to take on whichever path it arrives by. The
	// flags are neither, and are left alone here deliberately: the CLIP window
	// is positional and its cross-edge liveness is handled in the scheduler,
	// so moving it here would be a second, untested answer to the same
	// question.
	void delayPipelinedResultsBy( int cycles );

private:
	int readHazardDelayImpl( const Token& token,
	                         const Token* partner,
	                         int currentCycle,
	                         bool skipInterlockedRegisters ) const;

	int m_qReadyCycle;
	int m_pReadyCycle;
	int m_lastFMACCycle;
	int m_lastStatusCycle;
	int m_lastClipwCycle;
	std::map<std::string, int> m_registerReadyCycle;
	std::map<std::string, std::string> m_registerProducerMnemonic;
};

}

#endif
