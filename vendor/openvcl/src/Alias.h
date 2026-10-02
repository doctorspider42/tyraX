#ifndef __OPENVCL_ALIAS_H__
#define __OPENVCL_ALIAS_H__

/*
 * Alias.h
 *
 * Copyright (C) 2004 Jesper Svennevid, Daniel Collin
 *
 * Licensed under the AFL v2.0. See the file LICENSE included with this
 * distribution for licensing terms.
 *
 */

#include "Register.h"

#include <list>
#include <iostream>
#include <string>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace vcl
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class Alias
{

public:

	enum Type
	{
		FLOAT,
		INTEGER
	};

	struct Range
	{
		unsigned int m_start;
		unsigned int m_stop;
	};

	Alias( Type type );

	void setAllocatedRegister( const Register* allocated );
	const Register* allocatedRegister() const;

	unsigned int id() const;
	Type type() const;

	void addRange( unsigned int start, unsigned int stop );
	void merge( Alias* alias );
	bool intersects( Alias* alias );
	bool hasRangeOverlapping( unsigned int start, unsigned int stop ) const;
	bool hasRangeStartingBefore( unsigned int line ) const;
	void printRanges( std::ostream& os ) const;

	// Lowest start / highest stop over all ranges.  False when the alias has
	// no range at all (nothing ever referenced it).
	bool rangeExtent( unsigned int& start, unsigned int& stop ) const;

	// Drop everything outside [start,stop] and shorten the ranges that
	// straddle the boundary.  Only ever removes liveness, never adds it.
	void clipRanges( unsigned int start, unsigned int stop );

	// Throw the range list away, to be rebuilt with addRange.  Only for a caller
	// that has computed the alias's liveness itself and can defend it.
	void clearRanges();

	// Two-address hint.  When the parser emits an instruction like
	// `isubiu x, x, 1`, openvcl creates two Alias objects for `x` — the
	// read picks up the existing alias, the write spawns a fresh one.
	// Without coordination the allocator can put them on different VI
	// regs, so the counter is decremented in a stale register, the
	// loop never reaches zero, and xgkick is never fired.  We record
	// the prior (read) alias on the new (write) alias so the allocator
	// can prefer its allocated register first.
	void setSameNamePredecessor( Alias* predecessor );
	Alias* sameNamePredecessor() const;

	// Diagnostics only: the source-level name this alias was created for.
	// An Alias is otherwise anonymous, which leaves --show-reg-alloc unable
	// to say WHICH value ended up sharing a register with which.
	void setDebugName( const std::string& name );
	const std::string& debugName() const;

private:

	Type m_type;

	unsigned int m_id;
	const Register* m_allocatedRegister;
	Alias* m_sameNamePredecessor;
	std::string m_debugName;
	std::list<Range> m_ranges;

	static unsigned int s_nextId;

};

#include "Alias.inl"

}

#endif
