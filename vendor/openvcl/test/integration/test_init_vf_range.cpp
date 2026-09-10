// Regression test: .init_vf with a register range is accepted
//
// The historical roadmap documented that .init_vf with "vf01-vf04"
// errored out because the tokenizer didn't understand the 'range'
// modifier.  The range modifier is now implemented end-to-end:
// extractRegister parses "vfXX-vfYY", stores it as a Content::RANGE
// argument, and the Tokenizer's .init_vf consumer expands the range
// into the same availableFloats bits the comma-list form sets.
//
// This test pins the working behavior.  An "inverted" range
// (lower > upper) is also expected to be rejected.

#include "test_harness.h"
#include "openvcl_runner.h"

TEST_CASE("openvcl: .init_vf with a register range is accepted")
{
    // Note: code statements live in the body between --endenter and
    // --exit, not inside --enter/--endenter (that block only accepts
    // input-parameter directives like in_vf / in_vi).
    const std::string input =
        "\t.init_vf vf01-vf04\n"
        "\t.init_vi_all\n"
        "\t.name vsmInitVfRangeTest\n"
        "\t--enter\n"
        "\t--endenter\n"
        "\tnop\n"
        "\t--exit\n"
        "\t--endexit\n";

    std::vector<std::string> args;
    args.push_back("-o");
    args.push_back("-"); // write to stdout

    ::test::RunResult r = ::test::run_openvcl(args, input);

    CHECK(r.exit_code == 0);
    CHECK(r.stderr_data.find("Invalid argument") == std::string::npos);
    CHECK(r.stdout_data.find(".vu") != std::string::npos);
}

TEST_CASE("openvcl: .init_vf rejects an inverted register range")
{
    // "vf05-vf02" is malformed -- lower bound exceeds upper.  The
    // range parser must reject it cleanly rather than silently
    // expand into an empty (or wraparound) set of register bits.
    const std::string input =
        "\t.init_vf vf05-vf02\n"
        "\t.init_vi_all\n"
        "\t.name vsmBadRange\n"
        "\t--enter\n"
        "\t--endenter\n"
        "\tnop\n"
        "\t--exit\n"
        "\t--endexit\n";

    std::vector<std::string> args;
    args.push_back("-o");
    args.push_back("-");

    ::test::RunResult r = ::test::run_openvcl(args, input);

    CHECK(r.exit_code != 0);
    CHECK(r.stderr_data.find("Invalid argument") != std::string::npos);
}
