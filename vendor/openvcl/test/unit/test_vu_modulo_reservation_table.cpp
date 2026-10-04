// Unit tests for the Modulo Reservation Table (Track 9.G step 4b).

#include "test_harness.h"

#include "../../src/VuModuloReservationTable.h"

using namespace vcl;

TEST_CASE("MRT: constructor sets II and clears all lanes")
{
    ModuloReservationTable mrt( 8 );
    CHECK(mrt.initiationInterval() == 8u);
    CHECK(mrt.upperOccupancy() == 0u);
    CHECK(mrt.lowerOccupancy() == 0u);
    CHECK(mrt.fdivOccupancy() == 0u);
    CHECK(mrt.efuOccupancy()  == 0u);

    for( unsigned int c = 0; c < 8; ++c )
    {
        CHECK(mrt.upperAt(c) == (unsigned int)ModuloReservationTable::SLOT_FREE);
        CHECK(mrt.lowerAt(c) == (unsigned int)ModuloReservationTable::SLOT_FREE);
        CHECK(mrt.fdivAt(c)  == (unsigned int)ModuloReservationTable::SLOT_FREE);
        CHECK(mrt.efuAt(c)   == (unsigned int)ModuloReservationTable::SLOT_FREE);
    }
}

TEST_CASE("MRT: zero II is clamped to 1")
{
    ModuloReservationTable mrt( 0 );
    CHECK(mrt.initiationInterval() == 1u);
    CHECK(mrt.reserveUpper(0, 42));
    CHECK(!mrt.canReserveUpper(0));
    // Cycle 1 wraps to slot 0 -- still busy.
    CHECK(!mrt.canReserveUpper(1));
}

TEST_CASE("MRT: Upper/Lower single-cycle reservation and collision")
{
    ModuloReservationTable mrt( 4 );

    CHECK(mrt.canReserveUpper(2));
    CHECK(mrt.reserveUpper(2, 100));
    CHECK(mrt.upperOccupancy() == 1u);
    CHECK(mrt.upperAt(2) == 100u);

    // Collision: same slot rejected.
    CHECK(!mrt.canReserveUpper(2));
    CHECK(!mrt.reserveUpper(2, 200));
    CHECK(mrt.upperOccupancy() == 1u);

    // Modulo wrap: cycle 6 maps to slot 2, also rejected.
    CHECK(!mrt.canReserveUpper(6));

    // Lower lane is independent.
    CHECK(mrt.canReserveLower(2));
    CHECK(mrt.reserveLower(2, 101));
    CHECK(mrt.lowerOccupancy() == 1u);

    // Other Upper slots remain free.
    CHECK(mrt.canReserveUpper(0));
    CHECK(mrt.canReserveUpper(1));
    CHECK(mrt.canReserveUpper(3));
}

TEST_CASE("MRT: FDIV multi-cycle reservation with modulo wrap")
{
    // II=8, DIV throughput=7: reserves 7 contiguous slots; wraps if needed.
    ModuloReservationTable mrt( 8 );

    CHECK(mrt.canReserveFdiv(0, 7));
    CHECK(mrt.reserveFdiv(0, 7, 50));
    CHECK(mrt.fdivOccupancy() == 7u);
    for( unsigned int k = 0; k < 7; ++k )
        CHECK(mrt.fdivAt(k) == 50u);
    CHECK(mrt.fdivAt(7) == (unsigned int)ModuloReservationTable::SLOT_FREE);

    // Cannot reserve another 7-cycle FDIV anywhere: only slot 7 is free.
    for( unsigned int c = 0; c < 8; ++c )
        CHECK(!mrt.canReserveFdiv(c, 7));

    // A 1-cycle FDIV (e.g. WAITQ) at slot 7 is fine.
    CHECK(mrt.canReserveFdiv(7, 1));
    CHECK(mrt.reserveFdiv(7, 1, 51));
    CHECK(mrt.fdivOccupancy() == 8u);

    // Now FDIV pipe is fully busy.
    CHECK(!mrt.canReserveFdiv(0, 1));
}

TEST_CASE("MRT: FDIV wrap-around across table boundary")
{
    // II=8, place a 3-cycle FDIV starting at cycle 6: slots 6,7,0.
    ModuloReservationTable mrt( 8 );

    CHECK(mrt.canReserveFdiv(6, 3));
    CHECK(mrt.reserveFdiv(6, 3, 77));
    CHECK(mrt.fdivOccupancy() == 3u);
    CHECK(mrt.fdivAt(6) == 77u);
    CHECK(mrt.fdivAt(7) == 77u);
    CHECK(mrt.fdivAt(0) == 77u);
    CHECK(mrt.fdivAt(1) == (unsigned int)ModuloReservationTable::SLOT_FREE);

    // A new FDIV touching any of slots 0,6,7 is rejected.
    CHECK(!mrt.canReserveFdiv(7, 1));
    CHECK(!mrt.canReserveFdiv(0, 1));
    CHECK(!mrt.canReserveFdiv(6, 1));
    // Slot 5 is free, but a 2-cycle reservation at 5 would overlap slot 6.
    CHECK(mrt.canReserveFdiv(5, 1));
    CHECK(!mrt.canReserveFdiv(5, 2));
}

TEST_CASE("MRT: EFU lane is independent from FDIV lane")
{
    ModuloReservationTable mrt( 6 );

    CHECK(mrt.reserveFdiv(0, 4, 10));
    // EFU at the same cycles is unaffected.
    CHECK(mrt.canReserveEfu(0, 4));
    CHECK(mrt.reserveEfu(0, 4, 11));
    CHECK(mrt.fdivOccupancy() == 4u);
    CHECK(mrt.efuOccupancy()  == 4u);
    CHECK(mrt.fdivAt(2) == 10u);
    CHECK(mrt.efuAt(2)  == 11u);
}

TEST_CASE("MRT: duration capped at II prevents self-collision arithmetic")
{
    // Duration > II is silently capped at II (one full sweep). This makes
    // an oversized reservation either fully succeed (table empty) or fully
    // fail (any prior occupancy), rather than wrapping forever.
    ModuloReservationTable mrt( 4 );
    CHECK(mrt.canReserveFdiv(0, 100));
    CHECK(mrt.reserveFdiv(0, 100, 9));
    CHECK(mrt.fdivOccupancy() == 4u);
    for( unsigned int c = 0; c < 4; ++c )
        CHECK(mrt.fdivAt(c) == 9u);
}
