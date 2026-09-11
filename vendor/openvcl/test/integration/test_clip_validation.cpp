// Regression test: CLIP rejects an obviously malformed operand
//
// The historical roadmap documented that CLIP operands were present
// but not properly validated.  In practice the parser was already
// emitting "Invalid argument" diagnostics for them; the missing piece
// was Error::HasErrors() bubbling up into the exit code so callers
// could actually see the failure.  Both halves are now in place.
//
// This test pins the working behavior end-to-end:  a malformed CLIP
// must produce a non-zero exit AND a stderr diagnostic that mentions
// "Invalid argument" with the offending operand.

#include "test_harness.h"
#include "openvcl_runner.h"

TEST_CASE("openvcl: CLIP rejects an obviously malformed operand")
{

    // CLIP takes a vector source and the W field of another vector.
    // Passing a non-vector operand on the second slot is clearly invalid
    // and should produce a diagnostic — but openvcl currently accepts it.
    //
    // The leading tabs matter: openvcl's tokenizer rejects directives that
    // start in column 0 with "Invalid characters", which would mask the
    // CLIP diagnostic we're actually testing for.
    const std::string input =
        "\t.init_vf_all\n"
        "\t.init_vi_all\n"
        "\t.name vsmClipTest\n"
        "\t--enter\n"
        "\tin_vf src vf01\n"
        "\t--endenter\n"
        "\tclip.xyz VF01, 0\n"   // 0 is not a valid VFw operand for CLIP
        "\t--exit\n"
        "\t--endexit\n";

    std::vector<std::string> args;
    args.push_back("-o");
    args.push_back("-");

    ::test::RunResult r = ::test::run_openvcl(args, input);

    // Non-zero exit means the error did propagate (Error::HasErrors fix).
    CHECK(r.exit_code != 0);
    // Diagnostic must name the offending instruction so users can find the
    // problem line.  openvcl prints lowercase mnemonic in the source slice.
    CHECK(r.stderr_data.find("Invalid argument") != std::string::npos);
    CHECK(r.stderr_data.find("clip") != std::string::npos);
}
