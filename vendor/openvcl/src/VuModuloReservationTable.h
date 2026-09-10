// VuModuloReservationTable: resource model for iterative modulo scheduling.
//
// Track 9.G step 4b — Modulo Reservation Table scaffolding.
//
// Models the VU's issue resources as a set of lanes of length II (the
// candidate initiation interval). Each cycle slot is taken modulo II, so a
// multi-cycle FDIV/EFU reservation can wrap around the table boundary.
//
// Lanes:
//   - Upper issue lane (1 instr/cycle, occupancy = 1 cycle)
//   - Lower issue lane (1 instr/cycle, occupancy = 1 cycle)
//   - FDIV pipe        (1 in-flight at a time; throughput = DIV/SQRT=7,
//                       RSQRT=13, WAITQ=1)
//   - EFU pipe         (1 in-flight at a time; throughput per instruction)
//
// A reservation records the token index that occupies the slot to keep the
// dump output deterministic when introspecting failed placements.
//
// Diagnostic-only data structure for now: planner/emission do not consume
// it. See VuSchedulerAnalysis.cpp OPENVCL_DUMP_LOOP_MRT block.

#ifndef OPENVCL_VU_MODULO_RESERVATION_TABLE_H
#define OPENVCL_VU_MODULO_RESERVATION_TABLE_H

#include <vector>

namespace vcl
{

class ModuloReservationTable
{
public:
    enum { SLOT_FREE = static_cast< unsigned int >( -1 ) };

    explicit ModuloReservationTable( unsigned int initiationInterval );

    unsigned int initiationInterval() const { return m_ii; }

    // Single-cycle lanes (Upper / Lower issue slots).
    bool canReserveUpper( unsigned int cycle ) const;
    bool reserveUpper( unsigned int cycle, unsigned int tokenIndex );

    bool canReserveLower( unsigned int cycle ) const;
    bool reserveLower( unsigned int cycle, unsigned int tokenIndex );

    // Multi-cycle pipes (FDIV / EFU). 'duration' is the throughput cost from
    // VuInstructionInfo (e.g. DIV=7, RSQRT=13). The reservation wraps modulo
    // II if it would cross the table boundary.
    bool canReserveFdiv( unsigned int cycle, unsigned int duration ) const;
    bool reserveFdiv( unsigned int cycle, unsigned int duration, unsigned int tokenIndex );

    bool canReserveEfu( unsigned int cycle, unsigned int duration ) const;
    bool reserveEfu( unsigned int cycle, unsigned int duration, unsigned int tokenIndex );

    // Track 9.G step 4g: release a reservation so the placer can evict an
    // already-placed instruction during backtracking. Single-cycle lanes
    // ignore 'duration'; multi-cycle pipes free the same span the matching
    // reserveX call claimed.
    void releaseUpper( unsigned int cycle );
    void releaseLower( unsigned int cycle );
    void releaseFdiv( unsigned int cycle, unsigned int duration );
    void releaseEfu( unsigned int cycle, unsigned int duration );

    // Lookup (returns SLOT_FREE if unoccupied).
    unsigned int upperAt( unsigned int cycle ) const;
    unsigned int lowerAt( unsigned int cycle ) const;
    unsigned int fdivAt( unsigned int cycle ) const;
    unsigned int efuAt( unsigned int cycle ) const;

    // Occupancy counters (number of slots used in each lane).
    unsigned int upperOccupancy() const { return m_upperOccupancy; }
    unsigned int lowerOccupancy() const { return m_lowerOccupancy; }
    unsigned int fdivOccupancy() const { return m_fdivOccupancy; }
    unsigned int efuOccupancy()  const { return m_efuOccupancy;  }

private:
    bool laneIsClear( const std::vector< unsigned int >& lane,
                      unsigned int cycle,
                      unsigned int duration ) const;
    void markLane( std::vector< unsigned int >& lane,
                   unsigned int cycle,
                   unsigned int duration,
                   unsigned int tokenIndex,
                   unsigned int& occupancyCounter );

    void clearLane( std::vector< unsigned int >& lane,
                    unsigned int cycle,
                    unsigned int duration,
                    unsigned int& occupancyCounter );

    unsigned int m_ii;
    std::vector< unsigned int > m_upper;
    std::vector< unsigned int > m_lower;
    std::vector< unsigned int > m_fdiv;
    std::vector< unsigned int > m_efu;
    unsigned int m_upperOccupancy;
    unsigned int m_lowerOccupancy;
    unsigned int m_fdivOccupancy;
    unsigned int m_efuOccupancy;
};

} // namespace vcl

#endif
