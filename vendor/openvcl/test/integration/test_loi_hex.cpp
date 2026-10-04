// Regression test for the LOI float-immediate fix in commit bc41a56.
//
// Before bc41a56, openvcl emitted floating-point immediates for LOI as
// decimal scientific notation, which dvp-as could not consume.  The fix
// added a 'raw' modifier on the LOI operand spec plus an IEEE-754 hex
// emission path in CodeGenerator::immediateArg.
//
// This test pins the working behavior end-to-end: a few sample float
// values must appear in the .vsm output as their exact IEEE-754 hex
// representations.  If anyone re-introduces the decimal-output bug,
// these tests trip immediately.

#include "test_harness.h"
#include "openvcl_runner.h"

namespace
{
    // Minimal VCL frame with one LOI <value> instruction in the body.
    std::string loi_program(const std::string& value)
    {
        return std::string()
            + "\t.init_vf_all\n"
            + "\t.init_vi_all\n"
            + "\t.name vsmLoiTest\n"
            + "\t--enter\n"
            + "\t--endenter\n"
            + "\tloi " + value + "\n"
            + "\tnop\n"
            + "\t--exit\n"
            + "\t--endexit\n";
    }
}

TEST_CASE("openvcl: LOI emits IEEE-754 hex for 1.0")
{
    // No -o flag means openvcl writes to stdout (treating `-o -` as
    // a filename would create a file literally named `-`).
    std::vector<std::string> args;
    ::test::RunResult r = ::test::run_openvcl(args, loi_program("1.0"));

    CHECK(r.exit_code == 0);
    CHECK(r.stdout_data.find("loi 0x3f800000") != std::string::npos);
}

TEST_CASE("openvcl: LOI emits IEEE-754 hex for 0.0")
{
    // No -o flag means openvcl writes to stdout (treating `-o -` as
    // a filename would create a file literally named `-`).
    std::vector<std::string> args;
    ::test::RunResult r = ::test::run_openvcl(args, loi_program("0.0"));

    CHECK(r.exit_code == 0);
    CHECK(r.stdout_data.find("loi 0x00000000") != std::string::npos);
}

TEST_CASE("openvcl: LOI emits IEEE-754 hex for -1.0")
{
    // No -o flag means openvcl writes to stdout (treating `-o -` as
    // a filename would create a file literally named `-`).
    std::vector<std::string> args;
    ::test::RunResult r = ::test::run_openvcl(args, loi_program("-1.0"));

    CHECK(r.exit_code == 0);
    CHECK(r.stdout_data.find("loi 0xbf800000") != std::string::npos);
}

TEST_CASE("openvcl: LOI emits IEEE-754 hex for 2.5")
{
    // No -o flag means openvcl writes to stdout (treating `-o -` as
    // a filename would create a file literally named `-`).
    std::vector<std::string> args;
    ::test::RunResult r = ::test::run_openvcl(args, loi_program("2.5"));

    CHECK(r.exit_code == 0);
    CHECK(r.stdout_data.find("loi 0x40200000") != std::string::npos);
}

TEST_CASE("openvcl: LOI must NOT emit decimal scientific notation")
{
    // Even if hex emission has a bug, decimal scientific (e.g. "1e+00")
    // or plain decimals like "1.5" must never appear in the output.
    // This catches reintroductions of the original bug.
    // No -o flag means openvcl writes to stdout (treating `-o -` as
    // a filename would create a file literally named `-`).
    std::vector<std::string> args;
    ::test::RunResult r = ::test::run_openvcl(args, loi_program("1.5"));

    CHECK(r.exit_code == 0);
    CHECK(r.stdout_data.find("loi 0x3fc00000") != std::string::npos);
    // Negative checks: scientific notation and bare decimal would be bugs.
    CHECK(r.stdout_data.find("loi 1.5")  == std::string::npos);
    CHECK(r.stdout_data.find("loi 1e")   == std::string::npos);
}
