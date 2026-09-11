/*
 * Alias.cpp
 *
 * Copyright (C) 2004 Jesper Svennevid, Daniel Collin
 *
 * Licensed under the AFL v2.0. See the file LICENSE included with this
 * distribution for licensing terms.
 *
 */

#include "Alias.h"

#include <assert.h>

#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace vcl
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

unsigned int Alias::s_nextId = 1;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace
{
	bool rangesHaveGap( unsigned int leftStop, unsigned int rightStart )
	{
		return leftStop < rightStart && (rightStart - leftStop) > 1;
	}

	bool rangesOverlap( const Alias::Range& a, const Alias::Range& b )
	{
		return !(a.m_stop < b.m_start || b.m_stop < a.m_start);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Alias::addRange( unsigned int start, unsigned int stop )
{
	assert( start <= stop );

	Range merged;
	merged.m_start = start;
	merged.m_stop = stop;

	std::list<Range>::iterator insertAt = m_ranges.begin();
	while( insertAt != m_ranges.end() )
	{
		if( rangesHaveGap( merged.m_stop, insertAt->m_start ) )
			break;
		if( rangesHaveGap( insertAt->m_stop, merged.m_start ) )
		{
			++insertAt;
			continue;
		}

		merged.m_start = min( merged.m_start, insertAt->m_start );
		merged.m_stop = max( merged.m_stop, insertAt->m_stop );
		insertAt = m_ranges.erase( insertAt );
	}

	m_ranges.insert( insertAt, merged );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Alias::merge( Alias* alias )
{
	if( alias == this )
		return;

	assert( !(alias->allocatedRegister() && allocatedRegister()) || (alias->allocatedRegister() == allocatedRegister()) );

	if( alias->allocatedRegister() )
		setAllocatedRegister( alias->allocatedRegister() );	

	for( std::list<Range>::iterator i = alias->m_ranges.begin(); i != alias->m_ranges.end(); ++i )
		addRange( (*i).m_start, (*i).m_stop );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Alias::intersects( Alias* alias )
{
	for( std::list<Range>::iterator i = m_ranges.begin(); i != m_ranges.end(); ++i )
	{
		Range& r1 = *i;

		for( std::list<Range>::iterator j = alias->m_ranges.begin(); j != alias->m_ranges.end(); ++j )
		{
			Range& r2 = *j;
			if( r2.m_start > r1.m_stop )
				break;
			if( r1.m_start > r2.m_stop )
				continue;
			if( rangesOverlap( r1, r2 ) )
				return true;
		}
	}

	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Alias::hasRangeOverlapping( unsigned int start, unsigned int stop ) const
{
	// Check if any of this alias's ranges overlap with [start, stop]
	for( std::list<Range>::const_iterator i = m_ranges.begin(); i != m_ranges.end(); ++i )
	{
		const Range& r = *i;
		// Check if [r.m_start, r.m_stop] overlaps with [start, stop]
		if(
			(((start >= r.m_start) && (start <= r.m_stop)) || ((stop >= r.m_start) && (stop <= r.m_stop))) ||
			(((r.m_start >= start) && (r.m_start <= stop)) || ((r.m_stop >= start) && (r.m_stop <= stop)))
		)
		{
			return true;
		}
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Alias::hasRangeStartingBefore( unsigned int line ) const
{
	// Check if any of this alias's ranges start before the given line
	for( std::list<Range>::const_iterator i = m_ranges.begin(); i != m_ranges.end(); ++i )
	{
		if( i->m_start < line )
		{
			return true;
		}
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Alias::rangeExtent( unsigned int& start, unsigned int& stop ) const
{
	if( m_ranges.empty() )
		return false;

	std::list<Range>::const_iterator i = m_ranges.begin();
	start = i->m_start;
	stop = i->m_stop;
	for( ++i; i != m_ranges.end(); ++i )
	{
		start = min( start, i->m_start );
		stop = max( stop, i->m_stop );
	}
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Alias::clipRanges( unsigned int start, unsigned int stop )
{
	if( start > stop )
		return;

	std::list<Range>::iterator i = m_ranges.begin();
	while( i != m_ranges.end() )
	{
		if( i->m_stop < start || i->m_start > stop )
		{
			i = m_ranges.erase( i );
			continue;
		}
		i->m_start = max( i->m_start, start );
		i->m_stop = min( i->m_stop, stop );
		++i;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Alias::clearRanges()
{
	m_ranges.clear();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Alias::printRanges( std::ostream& os ) const
{
	os << "ranges: ";
	for( std::list<Range>::const_iterator i = m_ranges.begin(); i != m_ranges.end(); ++i )
	{
		if( i != m_ranges.begin() )
			os << ", ";
		os << "[" << i->m_start << "-" << i->m_stop << "]";
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

}
