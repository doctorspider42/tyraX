#include "test_harness.h"

#include "../../src/Alias.h"

#include <sstream>

using namespace vcl;

TEST_CASE("Alias: ids are monotonic creation-order keys")
{
    Alias first(Alias::FLOAT);
    Alias second(Alias::INTEGER);
    Alias third(Alias::FLOAT);

    CHECK(first.id() < second.id());
    CHECK(second.id() < third.id());
}

TEST_CASE("Alias: ranges stay sorted and adjacent ranges merge")
{
    Alias alias(Alias::FLOAT);
    alias.addRange(10, 12);
    alias.addRange(1, 3);
    alias.addRange(4, 9);
    alias.addRange(20, 22);

    std::ostringstream output;
    alias.printRanges(output);
    CHECK(output.str() == "ranges: [1-12], [20-22]");
}

TEST_CASE("Alias: sorted intersection distinguishes overlap from adjacency")
{
    Alias alias(Alias::FLOAT);
    alias.addRange(1, 12);
    alias.addRange(20, 22);

    Alias adjacent(Alias::FLOAT);
    adjacent.addRange(13, 19);
    CHECK(!alias.intersects(&adjacent));

    Alias overlapping(Alias::FLOAT);
    overlapping.addRange(18, 20);
    CHECK(alias.intersects(&overlapping));
}
