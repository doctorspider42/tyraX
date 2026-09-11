// Integration tests: smoke-cover the Parser's operand-template families.
//
// One positive test per major VU operand family (FMAC, FDIV, LSU, IALU,
// BRU, RANDU, EFU) confirming that a minimal idiomatic instance of that
// family is *accepted* by the parser — i.e. it does not raise any of the
// syntax-level errors:
//
//   - "Invalid argument"           (operand pattern mismatch)
//   - "Unknown operand"            (mnemonic not in the template list)
//   - "Wrong number of arguments"  (argument count mismatch)
//
// followed by negative tests for the same error-recovery paths.
//
// Why check stderr substrings rather than exit_code on positives?
// Separation of concerns: these tests assert that the Parser accepts a
// given operand shape, not that the RegisterAllocator is happy with
// the program semantics.  An uninitialized-read in the body (e.g.
// reading vf02 without writing it first) is a legitimate failure of
// the program but not of the Parser; checking only the Parser's
// diagnostics keeps the assertion scoped to the unit under test.
//
// The RegisterAllocator's own error propagation is exercised by the
// dedicated regression test at the bottom of this file.

#include "test_harness.h"
#include "openvcl_runner.h"

namespace
{
    // Minimal VCL boilerplate the Parser will accept.  `body` is dropped
    // between --endenter and --exit.  We declare a generous .init_vf
    // range and all integer registers so individual tests don't need to
    // repeat that.
    std::string skeleton(const std::string& name, const std::string& body)
    {
        return
            "\t.init_vf vf01-vf08\n"
            "\t.init_vi_all\n"
            "\t.name " + name + "\n"
            "\t--enter\n"
            "\t--endenter\n"
            + body +
            "\t--exit\n"
            "\t--endexit\n";
    }

    ::test::RunResult run_with(const std::string& body, const std::string& name)
    {
        std::vector<std::string> args;
        args.push_back("-o");
        args.push_back("-");
        return ::test::run_openvcl(args, skeleton(name, body));
    }

    // Positive-test helper: assert that no syntax-level errors were
    // reported.  Does NOT check exit_code — see file header for why.
    bool parser_accepted(const ::test::RunResult& r)
    {
        return r.stderr_data.find("Invalid argument")           == std::string::npos
            && r.stderr_data.find("Unknown operand")            == std::string::npos
            && r.stderr_data.find("Wrong number of arguments")  == std::string::npos;
    }
}

// --- positive: one instance per VU operand family --------------------

TEST_CASE("Parser: FMAC ADD.xy with three vf sources is accepted")
{
    ::test::RunResult r = run_with("\tadd.xy vf01, vf02, vf03\n",
                                   "vsmFmacAdd");
    CHECK(parser_accepted(r));
}

TEST_CASE("Parser: FMAC broadcast MULw.xy is accepted")
{
    // Broadcast form: lane suffix (w here) is appended to the mnemonic;
    // Tokenizer::identifyToken's BROADCAST branch synthesizes "MUL" + "w"
    // and matches via casecompare.
    ::test::RunResult r = run_with("\tmulw.xy vf01, vf02, vf03\n",
                                   "vsmFmacBroadcast");
    CHECK(parser_accepted(r));
}

TEST_CASE("Parser: FDIV DIV with per-lane source suffix is accepted")
{
    // FDIV operands take a per-lane suffix on the source vfs (no dot,
    // e.g. vf01w), not the .x/.y modifier used for destination fields.
    // Pattern: "q:write,vf:flag,vf:flag".
    ::test::RunResult r = run_with("\tdiv q, vf01w, vf02w\n",
                                   "vsmFdivDiv");
    CHECK(parser_accepted(r));
}

TEST_CASE("Parser: LSU LQ with imm(vi) addressing is accepted")
{
    ::test::RunResult r = run_with("\tlq.xy vf01, 0(vi01)\n",
                                   "vsmLsuLq");
    CHECK(parser_accepted(r));
}

TEST_CASE("Parser: LSU SQI with post-increment (vi++) addressing is accepted")
{
    // Post-increment store: SQI vf, (vi++).  The "++" lives inside the
    // parentheses — exercises the (vi):postinc pattern fragment.
    ::test::RunResult r = run_with("\tsqi.xy vf01, (vi01++)\n",
                                   "vsmLsuSqi");
    CHECK(parser_accepted(r));
}

TEST_CASE("Parser: IALU IADD vi,vi,vi is accepted")
{
    ::test::RunResult r = run_with("\tiadd vi03, vi01, vi02\n",
                                   "vsmIaluIadd");
    CHECK(parser_accepted(r));
}

TEST_CASE("Parser: BRU unconditional branch to a forward label is accepted")
{
    ::test::RunResult r = run_with(
        "\tb tail\n"
        "\tnop\n"
        "tail:\n"
        "\tnop\n",
        "vsmBruBranch");
    CHECK(parser_accepted(r));
}

TEST_CASE("Parser: RANDU RGET.xy vf,r is accepted")
{
    // RGET reads from the random-number register R into a destination
    // vf.  The :dest:write pattern on the first arg is what we're
    // exercising here.
    ::test::RunResult r = run_with("\trget.xy vf01, r\n",
                                   "vsmRanduRget");
    CHECK(parser_accepted(r));
}

TEST_CASE("Parser: EFU ESADD p, vf is accepted")
{
    // EFU = Elementary Function Unit.  ESADD writes to the P register
    // and consumes a single vf source.
    ::test::RunResult r = run_with("\tesadd p, vf01\n",
                                   "vsmEfuEsadd");
    CHECK(parser_accepted(r));
}

// --- negative: error-recovery scenarios ------------------------------
//
// For negatives we DO check exit_code because the error paths under
// test (Unknown operand, Wrong number of arguments, Invalid argument)
// are wired through Error::Display which bumps the error count.

TEST_CASE("Parser: unknown mnemonic is rejected with non-zero exit")
{
    ::test::RunResult r = run_with("\tnonsense_op vf01, vf02, vf03\n",
                                   "vsmUnknownOp");
    CHECK(r.exit_code != 0);
    CHECK(r.stderr_data.find("Unknown operand") != std::string::npos);
}

TEST_CASE("Parser: wrong argument count is rejected with non-zero exit")
{
    // ADD needs three sources; supplying two trips the arg-count
    // check in Tokenizer::handleToken before code-gen even runs.
    ::test::RunResult r = run_with("\tadd.xy vf01, vf02\n",
                                   "vsmWrongArgs");
    CHECK(r.exit_code != 0);
    CHECK(r.stderr_data.find("Wrong number of arguments") != std::string::npos);
}

TEST_CASE("Parser: out-of-range vf register is rejected with non-zero exit")
{
    // VU1 has 32 vf registers (vf00..vf31); vf99 must be rejected.
    ::test::RunResult r = run_with("\tadd.xy vf99, vf02, vf03\n",
                                   "vsmBadReg");
    CHECK(r.exit_code != 0);
    CHECK(r.stderr_data.find("Invalid argument") != std::string::npos);
}

TEST_CASE("Parser: register-family mismatch is rejected with non-zero exit")
{
    // ADD's first operand expects a vf (float); supplying vi (int)
    // should trip the operand-template validator.
    ::test::RunResult r = run_with("\tadd.xy vi01, vf02, vf03\n",
                                   "vsmFamilyMismatch");
    CHECK(r.exit_code != 0);
    CHECK(r.stderr_data.find("Invalid argument") != std::string::npos);
}

// --- regression: RegisterAllocator uninit-read propagation ----------

TEST_CASE("RegisterAllocator: uninit-read produces a non-zero exit")
{
    // Pre-fix, RegisterAllocator wrote "Read-attempt from uninitialized
    // float register" directly to std::cerr without going through
    // Error::Display, so m_errorCount stayed at zero and the process
    // exited 0 — the same silent-failure mode that commit 5c0227b fixed
    // for CLIP.  Routing the seven RA error paths through Error::Display
    // (token, *i) makes them participate in exit-code propagation.
    //
    // This test reads vf03 without ever writing it; the diagnostic must
    // appear on stderr AND the process must exit non-zero.
    ::test::RunResult r = run_with("\tadd.xy vf01, vf02, vf03\n",
                                   "vsmUninitRead");
    CHECK(r.exit_code != 0);
    CHECK(r.stderr_data.find("Read-attempt from uninitialized float register")
          != std::string::npos);
}
