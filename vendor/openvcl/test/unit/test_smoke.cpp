// Smoke test: confirms the harness links, the registry mechanism works,
// and a basic openvcl object can be constructed from a test.

#include "test_harness.h"

#include "../../src/Line.h"
#include "../../src/File.h"

using namespace vcl;

TEST_CASE("Smoke: harness links and runs")
{
    CHECK(true);
}

TEST_CASE("Smoke: Line stores content verbatim")
{
    File f;
    Line line(f, 42, 7, "addi.xy VF05, VF00, i");
    CHECK(line.number() == 42u);
    CHECK(line.originalNumber() == 7u);
    CHECK(line.content() == "addi.xy VF05, VF00, i");
}
