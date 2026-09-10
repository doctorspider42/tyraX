// Tests for dual-pipe pairing in CodeGenerator.
//
// Each test feeds a tiny VCL snippet to ./openvcl and asserts on the
// shape of the emitted VSM line(s):
//
//   - "paired" means upper-pipe instruction and lower-pipe instruction
//     appear on the SAME line (no NOP between them).
//   - "not paired" means they appear on separate lines with a NOP on
//     the unused pipe.
//
// We grep the emitted VSM and assert presence/absence of "nop" on the
// relevant pipe.  Each test stresses one hazard rule.

#include "test_harness.h"
#include "openvcl_runner.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <vector>

namespace
{
    std::string skeleton(const std::string& name, const std::string& body)
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t.name " + name + "\n"
            "\t--enter\n"
            "\t--endenter\n"
            + body +
            "\t--exit\n"
            "\t--endexit\n";
    }

    // Run openvcl, capturing the EMITTED VSM into the returned string.
    // openvcl's `-o -` doesn't actually write to stdout (it creates a
    // literal file named `-`) so we use a temp file per test.
    std::string runEmitWithArgs(const std::string& body,
                                const std::string& name,
                                const std::vector<std::string>& extraArgs)
    {
        char tmpl[] = "/tmp/openvcl_test_XXXXXX.vsm";
        int fd = mkstemps(tmpl, 4);
        if( fd < 0 )
            return std::string();
        close(fd);

        std::vector<std::string> args;
        for( std::vector<std::string>::const_iterator arg = extraArgs.begin(); arg != extraArgs.end(); ++arg )
            args.push_back(*arg);
        args.push_back("-o");
        args.push_back(tmpl);
        ::test::RunResult r = ::test::run_openvcl(args, skeleton(name, body));
        if( r.exit_code != 0 )
        {
            std::remove(tmpl);
            return std::string();
        }
        std::ifstream f(tmpl);
        std::stringstream ss;
        ss << f.rdbuf();
        std::remove(tmpl);
        return ss.str();
    }

    std::string runEmit(const std::string& body, const std::string& name)
    {
        std::vector<std::string> args;
        return runEmitWithArgs(body, name, args);
    }

    // Find the first VSM line that mentions `pattern`.
    std::string find_line(const std::string& vsm, const std::string& pattern)
    {
        std::string::size_type pos = vsm.find(pattern);
        if( pos == std::string::npos )
            return std::string();
        std::string::size_type begin = vsm.rfind('\n', pos);
        std::string::size_type end = vsm.find('\n', pos);
        if( begin == std::string::npos ) begin = 0; else begin++;
        if( end == std::string::npos ) end = vsm.size();
        return vsm.substr(begin, end - begin);
    }
}

// --- Sanity: a known-safe pair should actually pair ----------------
//
// Two unrelated lq.xyz operations: an upper-pipe op that has no
// dependency on a lower-pipe op should pair into one line.

// True if any line in `vsm` contains both `a` and `b` substrings.
static bool linePairsSubstrings( const std::string& vsm, const std::string& a, const std::string& b )
{
    std::string::size_type pos = 0;
    while( pos < vsm.size() )
    {
        std::string::size_type end = vsm.find('\n', pos);
        if( end == std::string::npos ) end = vsm.size();
        std::string line = vsm.substr(pos, end - pos);
        if( line.find(a) != std::string::npos && line.find(b) != std::string::npos )
            return true;
        pos = end + 1;
    }
    return false;
}

static int lineIndex( const std::string& vsm, const std::string& pattern )
{
    std::string::size_type pos = 0;
    int lineNo = 0;
    while( pos < vsm.size() )
    {
        std::string::size_type end = vsm.find('\n', pos);
        if( end == std::string::npos ) end = vsm.size();
        std::string line = vsm.substr(pos, end - pos);
        if( line.find(pattern) != std::string::npos )
            return lineNo;
        pos = end + 1;
        lineNo++;
    }
    return -1;
}

static int lastLineIndex( const std::string& vsm, const std::string& pattern )
{
    std::string::size_type pos = 0;
    int lineNo = 0;
    int found = -1;
    while( pos < vsm.size() )
    {
        std::string::size_type end = vsm.find('\n', pos);
        if( end == std::string::npos ) end = vsm.size();
        std::string line = vsm.substr(pos, end - pos);
        if( line.find(pattern) != std::string::npos )
            found = lineNo;
        pos = end + 1;
        lineNo++;
    }
    return found;
}

static int countSubstrings( const std::string& text, const std::string& pattern )
{
    int count = 0;
    std::string::size_type pos = 0;
    while( (pos = text.find(pattern, pos)) != std::string::npos )
    {
        ++count;
        pos += pattern.size();
    }
    return count;
}

TEST_CASE("Pairing: independent upper+lower ops pair into one cycle")
{
    // mulax (upper) + lq.xyz (lower) - no shared register.  These should
    // pair into a single emitted line because they have no data flow
    // between them and sit on different pipes.
    const std::string body =
        "\tmulax acc, vf00, vf00x\n"
        "\tlq.xyz vf09, 0(vi00)\n";
    std::string vsm = runEmit(body, "vsmPairIndependent");
    REQUIRE(vsm.length() > 0);
    CHECK(linePairsSubstrings(vsm, "mulax", "lq.xyz"));
}

TEST_CASE("Pairing: strict schedule slots honor ready-set pair tags")
{
    const std::string body =
        "\tadd.xy vf01, vf00, vf00\n"
        "\tmul.xy vf04, vf01, vf00\n"
        "\tiaddiu vi01, vi00, 1\n";
    std::vector<std::string> args;
    args.push_back("--strict-schedule-slots");

    std::string vsm = runEmitWithArgs(body, "vsmStrictScheduleSlotsPair", args);
    REQUIRE(vsm.length() > 0);
    CHECK(linePairsSubstrings(vsm, "add.xy", "iaddiu"));
}

TEST_CASE("Scheduling: strict schedule slots emit typed Q wait padding across labels")
{
    const std::string body =
        "\tdiv q, vf00w, vf00w\n"
        "after_div_lid:\n"
        "\tmulq.xyz vf02, vf00, q\n";
    std::vector<std::string> args;
    args.push_back("--strict-schedule-slots");

    std::string vsm = runEmitWithArgs(body, "vsmStrictQPaddingAcrossLabel", args);
    REQUIRE(vsm.length() > 0);
    const int divLine = lineIndex(vsm, "div q, VF00w, VF00w");
    const int waitLine = lineIndex(vsm, "waitq");
    const int mulqLine = lineIndex(vsm, "mulq.xyz VF02, VF00, q");
    REQUIRE(divLine >= 0);
    CHECK(divLine < waitLine);
    CHECK(waitLine < mulqLine);
}

TEST_CASE("Scheduling: strict schedule slots omit unreachable exit after terminal branch")
{
    const std::string body =
        "loop_lid:\n"
        "\tb loop_lid\n";
    std::vector<std::string> args;
    args.push_back("--strict-schedule-slots");

    std::string vsm = runEmitWithArgs(body, "vsmStrictTerminalBranch", args);
    REQUIRE(vsm.length() > 0);
    CHECK(countSubstrings(vsm, "nop[E]") == 0);
}

TEST_CASE("Scheduling: strict software-pipelined store stays before loop branch")
{
    const std::string body =
        "\tloi 255.0\n"
        "\tiaddiu vi03, vi00, 0\n"
        "\tiaddiu vi02, vi00, 9\n"
        "final_loop_lid:\n"
        "\t--LoopCS 1,3\n"
        "\t--LoopExtra 1\n"
        "\tlq.xyz vf08, 1(vi03)\n"
        "\tminii.xyz vf08, vf08, i\n"
        "\tftoi0.xyz vf08, vf08\n"
        "\tsq.xyz vf08, 1(vi03)\n"
        "\tiaddiu vi03, vi03, 3\n"
        "\tibne vi03, vi02, final_loop_lid\n";
    std::vector<std::string> args;
    args.push_back("--strict-schedule-slots");

    std::string vsm = runEmitWithArgs(body, "vsmStrictBranchDelayFillerProducer", args);
    REQUIRE(vsm.length() > 0);
    CHECK(!linePairsSubstrings(vsm, "ftoi0.xyz VF08, VF08", "ibne VI03, VI02, final_loop_lid"));
    const int branchLine = lineIndex(vsm, "ibne VI03, VI02, final_loop_lid");
    const int storeLine = lineIndex(vsm, "sq.xyz VF08, -2(VI03)");
    REQUIRE(branchLine >= 0);
    REQUIRE(storeLine >= 0);
    CHECK(storeLine < branchLine);
}

TEST_CASE("Pairing: strict schedule slots pair safe barrier tails")
{
    std::vector<std::string> args;
    args.push_back("--strict-schedule-slots");

    const std::string branchBody =
        "\tadd.xyz vf01, vf00, vf00\n"
        "\tb done_lid\n"
        "done_lid:\n";
    std::string branchVsm = runEmitWithArgs(branchBody, "vsmStrictBranchTailPair", args);
    REQUIRE(branchVsm.length() > 0);
    CHECK(linePairsSubstrings(branchVsm, "add.xyz", "b done_lid"));

    const std::string xgkickBody =
        "\tadd.xyz vf02, vf00, vf00\n"
        "\txgkick vi00\n";
    std::string xgkickVsm = runEmitWithArgs(xgkickBody, "vsmStrictXgkickTailPair", args);
    REQUIRE(xgkickVsm.length() > 0);
    CHECK(linePairsSubstrings(xgkickVsm, "add.xyz", "xgkick"));
}

TEST_CASE("Pairing: later LOI does not pair with current I reader")
{
	const std::string body =
		"\tloi 1.0\n"
		"\tmuli.xyz vf01, vf00, i\n"
		"\tloi 2.0\n"
		"\taddi.xyz vf02, vf00, i\n";
	std::string vsm = runEmit(body, "vsmNoPairLaterLoiWithIReader");
	REQUIRE(vsm.length() > 0);
	CHECK(!linePairsSubstrings(vsm, "muli.xyz", "loi 0x40000000"));
	CHECK(!linePairsSubstrings(vsm, "muli.xyz", "loi 0x3f800000"));
}

TEST_CASE("Pairing: LOI producer does not pair with its I reader")
{
    const std::string body =
        "\tloi 1.0\n"
        "\tmuli.xyz vf01, vf00, i\n";
    std::string vsm = runEmit(body, "vsmNoPairLoiProducerWithIReader");
    REQUIRE(vsm.length() > 0);
    CHECK(!linePairsSubstrings(vsm, "muli.xyz", "loi 0x3f800000"));
}

TEST_CASE("Register allocation: emitted VSM is stable across repeated runs")
{
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmove.xyzw vf02, vf00\n"
        "\tmove.xyzw vf03, vf00\n"
        "\tmove.xyzw vf04, vf00\n"
        "\tmove.xyzw vf05, vf00\n"
        "\tmove.xyzw vf06, vf00\n"
        "\tmove.xyzw vf07, vf00\n"
        "\tmove.xyzw vf08, vf00\n"
        "\tmul.xyz vf09, vf01, vf02\n"
        "\tadd.xyz vf10, vf03, vf04\n"
        "\tsub.xyz vf11, vf05, vf06\n"
        "\tmax.xyz vf12, vf07, vf08\n"
        "\tadd.xyz vf13, vf09, vf10\n"
        "\tmul.xyz vf14, vf11, vf12\n"
        "\tadd.xyz vf15, vf13, vf14\n"
        "\tsq.xyz vf15, 0(vi00)\n";

    std::string first = runEmit(body, "vsmStableAllocationA");
    std::string second = runEmit(body, "vsmStableAllocationA");
    REQUIRE(first.length() > 0);
    REQUIRE(second.length() > 0);
    CHECK(first == second);
}

TEST_CASE("Pairing: independent upper op can pair with adjacent plain store")
{
    // Pairing an adjacent store does not reorder memory; it only fills the
    // upper slot of the same issue cycle.
    const std::string body =
        "\tadd.xyz vf02, vf00, vf00\n"
        "\tsq.xyz vf00, 0(vi00)\n";
    std::string vsm = runEmit(body, "vsmPairPlainStore");
    REQUIRE(vsm.length() > 0);
    CHECK(linePairsSubstrings(vsm, "add.xyz", "sq.xyz"));
}

TEST_CASE("Pairing: later independent upper op can pair with plain store")
{
    const std::string body =
        "\tsq.xyz vf00, 0(vi00)\n"
        "\tiaddiu vi02, vi00, 1\n"
        "\tadd.xyz vf02, vf00, vf00\n";
    std::string vsm = runEmit(body, "vsmPairLaterUpperWithStore");
    REQUIRE(vsm.length() > 0);
    CHECK(linePairsSubstrings(vsm, "add.xyz", "sq.xyz"));
}

TEST_CASE("Pairing: store pairing can skip an unpaired lower candidate")
{
    const std::string body =
        "\tsq.xyz vf00, 0(vi00)\n"
        "\tlq.xyz vf01, 0(vi00)\n"
        "\tadd.xyz vf02, vf00, vf00\n";
    std::string vsm = runEmit(body, "vsmPairStoreSkipsLowerCandidate");
    REQUIRE(vsm.length() > 0);
    CHECK(linePairsSubstrings(vsm, "add.xyz", "sq.xyz"));
}

TEST_CASE("Pairing: store reading the upper result is not paired")
{
    const std::string body =
        "\tadd.xyz vf01, vf00, vf00\n"
        "\tsq.xyz vf01, 0(vi00)\n";
    std::string vsm = runEmit(body, "vsmNoPairStoreReadsUpper");
    REQUIRE(vsm.length() > 0);
    CHECK(!linePairsSubstrings(vsm, "add.xyz", "sq.xyz"));
}

TEST_CASE("Pairing: post-increment store remains unpaired")
{
    const std::string body =
        "\tadd.xyz vf02, vf00, vf00\n"
        "\tsqi.xyz vf00, (vi00++)\n";
    std::string vsm = runEmit(body, "vsmNoPairPostIncStore");
    REQUIRE(vsm.length() > 0);
    CHECK(!linePairsSubstrings(vsm, "add.xyz", "sqi.xyz"));
}

TEST_CASE("Pairing: independent upper op can pair with following xgkick")
{
    const std::string body =
        "\tadd.xyz vf02, vf00, vf00\n"
        "\txgkick vi00\n";
    std::string vsm = runEmit(body, "vsmPairXgkick");
    REQUIRE(vsm.length() > 0);
    CHECK(linePairsSubstrings(vsm, "add.xyz", "xgkick"));
}

TEST_CASE("Pairing: xgkick does not pull a following upper op before it")
{
    const std::string body =
        "\txgkick vi00\n"
        "\tadd.xyz vf02, vf00, vf00\n";
    std::string vsm = runEmit(body, "vsmNoPairAfterXgkick");
    REQUIRE(vsm.length() > 0);
    CHECK(!linePairsSubstrings(vsm, "add.xyz", "xgkick"));
}

TEST_CASE("Pairing: vf00 xyz move can emit as upper zero op when MAC is dead")
{
    const std::string body =
        "\tmove.xyz vf01, vf00\n"
        "\tlq.xyz vf02, 0(vi00)\n";
    std::string vsm = runEmit(body, "vsmPairUpperZeroMove");
    REQUIRE(vsm.length() > 0);
    CHECK(linePairsSubstrings(vsm, "max.xyz", "lq.xyz"));
    CHECK(find_line(vsm, "move.xyz").empty());
}

TEST_CASE("Pairing: vf00 move stays lower when MAC flags are read")
{
    const std::string body =
        "\tmove.xyz vf01, vf00\n"
        "\tlq.xyz vf02, 0(vi00)\n"
        "\tfmand vi01, vi00\n";
    std::string vsm = runEmit(body, "vsmKeepMoveWhenMacRead");
    REQUIRE(vsm.length() > 0);
    CHECK(!linePairsSubstrings(vsm, "max.xyz", "lq.xyz"));
    CHECK(!find_line(vsm, "move.xyz").empty());
}

TEST_CASE("Pairing: vf00 move after final MAC reader can emit as upper zero op")
{
    const std::string body =
        "\tfmand vi01, vi00\n"
        "\tmove.xyz vf01, vf00\n"
        "\tlq.xyz vf02, 0(vi00)\n";
    std::string vsm = runEmit(body, "vsmPairUpperZeroMoveAfterMacRead");
    REQUIRE(vsm.length() > 0);
    CHECK(linePairsSubstrings(vsm, "max.xyz", "lq.xyz"));
    CHECK(find_line(vsm, "move.xyz").empty());
}

TEST_CASE("Pairing: nonzero xyz move can emit as upper max when MAC is dead")
{
    const std::string body =
        "\tlq.xyz vf02, 0(vi00)\n"
        "\tmove.xyz vf01, vf02\n"
        "\tiaddiu vi01, vi00, 1\n";
    std::string vsm = runEmit(body, "vsmPairUpperMove");
    REQUIRE(vsm.length() > 0);
    CHECK(vsm.find("max.xyz VF01, VF02, VF02") != std::string::npos);
    CHECK(vsm.find("move.xyz VF01, VF02") == std::string::npos);
}

TEST_CASE("Pairing: nonzero move stays lower when MAC flags are read")
{
    const std::string body =
        "\tlq.xyz vf02, 0(vi00)\n"
        "\tmove.xyz vf01, vf02\n"
        "\tiaddiu vi01, vi00, 1\n"
        "\tfmand vi02, vi00\n";
    std::string vsm = runEmit(body, "vsmKeepNonzeroMoveWhenMacRead");
    REQUIRE(vsm.length() > 0);
    CHECK(vsm.find("max.xyz VF01, VF02, VF02") == std::string::npos);
    CHECK(vsm.find("move.xyz VF01, VF02") != std::string::npos);
}

TEST_CASE("Pairing: suffix after final MAC reader can cross dead MAC WAW")
{
    const std::string body =
        "\tfmand vi01, vi00\n"
        "\tlq.xyz vf02, 0(vi00)\n"
        "\tadd.xyz vf03, vf02, vf00\n"
        "\tmul.xyz vf04, vf00, vf00\n";
    std::string vsm = runEmit(body, "vsmPairAfterFinalMacReader");
    REQUIRE(vsm.length() > 0);
    CHECK(linePairsSubstrings(vsm, "mul.xyz", "lq.xyz"));
}

TEST_CASE("Pairing: prefix before final MAC reader keeps MAC WAW order")
{
    const std::string body =
        "\tlq.xyz vf02, 0(vi00)\n"
        "\tadd.xyz vf03, vf02, vf00\n"
        "\tmul.xyz vf04, vf00, vf00\n"
        "\tfmand vi01, vi00\n";
    std::string vsm = runEmit(body, "vsmNoPairBeforeFinalMacReader");
    REQUIRE(vsm.length() > 0);
    CHECK(!linePairsSubstrings(vsm, "mul.xyz", "lq.xyz"));
}

TEST_CASE("Scheduling: branch emission reuses existing hazard nop before branch")
{
    const std::string body =
        "\tilw.x vi01, 0(vi00)\n"
        "\tibeq vi01, vi00, done_lid\n"
        "\tiaddiu vi02, vi00, 1\n"
        "done_lid:\n";
    std::string vsm = runEmit(body, "vsmBranchReuseHazardNop");
    REQUIRE(vsm.length() > 0);

    int loadLine = lineIndex(vsm, "ilw.x");
    int branchLine = lineIndex(vsm, "ibeq");
    REQUIRE(loadLine >= 0);
    REQUIRE(branchLine >= 0);
    CHECK(branchLine - loadLine == 5);
}

TEST_CASE("Pairing: adjacent upper instruction can pair with following direct branch")
{
    const std::string body =
        "\tadd.xyz vf01, vf00, vf00\n"
        "\tb done_lid\n"
        "done_lid:\n";
    std::string vsm = runEmit(body, "vsmPairUpperWithBranch");
    REQUIRE(vsm.length() > 0);

    CHECK(linePairsSubstrings(vsm, "add.xyz", "b done_lid"));
    CHECK(countSubstrings(vsm, "nop                             nop") >= 1);
}

TEST_CASE("Pairing: branch does not pull upper instruction from after control flow")
{
    const std::string body =
        "\tb done_lid\n"
        "\tadd.xyz vf01, vf00, vf00\n"
        "done_lid:\n";
    std::string vsm = runEmit(body, "vsmBranchDoesNotPullUpper");
    REQUIRE(vsm.length() > 0);

    CHECK(!linePairsSubstrings(vsm, "add.xyz", "b done_lid"));
}

TEST_CASE("Scheduling: independent integer op fills previous branch delay slot")
{
    const std::string body =
        "\tiaddiu vi01, vi00, 1\n"
        "\tiaddiu vi02, vi00, 2\n"
        "\tiaddiu vi03, vi00, 3\n"
        "\tibne vi01, vi02, done_lid\n"
        "done_lid:\n";
    std::string vsm = runEmit(body, "vsmIntegerBranchDelayFiller");
    REQUIRE(vsm.length() > 0);

    int branchLine = lineIndex(vsm, "ibne");
    int bumpLine = lineIndex(vsm, "iaddiu VI03");
    REQUIRE(branchLine >= 0);
    REQUIRE(bumpLine >= 0);
    CHECK(bumpLine == branchLine + 1);
}

TEST_CASE("Scheduling: strict schedule slots preserve filled branch delay slots")
{
    const std::string body =
        "\tiaddiu vi01, vi00, 1\n"
        "\tiaddiu vi02, vi00, 2\n"
        "\tiaddiu vi03, vi00, 3\n"
        "\tibne vi01, vi02, done_lid\n"
        "done_lid:\n";
    std::vector<std::string> args;
    args.push_back("--strict-schedule-slots");

    std::string vsm = runEmitWithArgs(body, "vsmStrictIntegerBranchDelayFiller", args);
    REQUIRE(vsm.length() > 0);

    int branchLine = lineIndex(vsm, "ibne");
    int bumpLine = lineIndex(vsm, "iaddiu VI03");
    REQUIRE(branchLine >= 0);
    REQUIRE(bumpLine >= 0);
    CHECK(bumpLine == branchLine + 1);
}

TEST_CASE("Scheduling: branch delay filler cannot write branch condition")
{
    const std::string body =
        "\tiaddiu vi02, vi00, 2\n"
        "\tiaddiu vi01, vi00, 1\n"
        "\tibne vi01, vi02, done_lid\n"
        "done_lid:\n";
    std::string vsm = runEmit(body, "vsmNoConditionBranchDelayFiller");
    REQUIRE(vsm.length() > 0);

    int bumpLine = lineIndex(vsm, "iaddiu VI01");
    int branchLine = lineIndex(vsm, "ibne");
    REQUIRE(bumpLine >= 0);
    REQUIRE(branchLine >= 0);
    CHECK(bumpLine < branchLine);
}

TEST_CASE("Scheduling: unconditional standalone branch does not add a pre-branch bubble")
{
    const std::string body =
        "\tloi 1.0\n"
        "\tb done_lid\n"
        "done_lid:\n";
    std::string vsm = runEmit(body, "vsmNoPreBranchBubble");
    REQUIRE(vsm.length() > 0);

    int loiLine = lineIndex(vsm, "loi");
    int branchLine = lineIndex(vsm, "b done_lid");
    REQUIRE(loiLine >= 0);
    REQUIRE(branchLine >= 0);
    CHECK(branchLine == loiLine + 1);
}

TEST_CASE("Scheduling: conditional standalone branch keeps a pre-branch bubble")
{
    const std::string body =
        "\tloi 1.0\n"
        "\tibne vi00, vi00, done_lid\n"
        "done_lid:\n";
    std::string vsm = runEmit(body, "vsmConditionalPreBranchBubble");
    REQUIRE(vsm.length() > 0);

    int loiLine = lineIndex(vsm, "loi");
    int branchLine = lineIndex(vsm, "ibne");
    REQUIRE(loiLine >= 0);
    REQUIRE(branchLine >= 0);
    CHECK(branchLine == loiLine + 2);
}

TEST_CASE("Scheduling: pre-increment store can fill branch delay slot with adjusted offset")
{
    const std::string body =
        "\tadd.xyzw vf01, vf00, vf00\n"
        "\tiaddiu vi04, vi00, 9\n"
        "\tiaddiu vi02, vi00, 15\n"
        "\tsq vf01, 1(vi04)\n"
        "\tiaddiu vi04, vi04, 3\n"
        "\tibne vi04, vi02, done_lid\n"
        "done_lid:\n";
    std::string vsm = runEmit(body, "vsmPreIncrementStoreDelay");
    REQUIRE(vsm.length() > 0);

    int branchLine = lineIndex(vsm, "ibne");
    int storeLine = lineIndex(vsm, "sq VF01, -2(VI04)");
    int bumpLine = lineIndex(vsm, "iaddiu VI04, VI04, 3");
    REQUIRE(branchLine >= 0);
    REQUIRE(storeLine >= 0);
    REQUIRE(bumpLine >= 0);
    CHECK(bumpLine < branchLine);
    CHECK(storeLine == branchLine + 1);
}

TEST_CASE("Scheduling: pre-increment store stays before branch when branch ignores increment")
{
    const std::string body =
        "\tadd.xyzw vf01, vf00, vf00\n"
        "\tiaddiu vi04, vi00, 9\n"
        "\tiaddiu vi02, vi00, 15\n"
        "\tiaddiu vi03, vi00, 3\n"
        "\tsq vf01, 1(vi04)\n"
        "\tiaddiu vi04, vi04, 3\n"
        "\tibne vi03, vi02, done_lid\n"
        "done_lid:\n";
    std::string vsm = runEmit(body, "vsmNoPreIncrementStoreDelay");
    REQUIRE(vsm.length() > 0);

    int branchLine = lineIndex(vsm, "ibne");
    int adjustedStoreLine = lineIndex(vsm, "sq VF01, -2(VI04)");
    int originalStoreLine = lineIndex(vsm, "sq VF01, 1(VI04)");
    REQUIRE(branchLine >= 0);
    REQUIRE(originalStoreLine >= 0);
    CHECK(adjustedStoreLine < 0);
    CHECK(originalStoreLine < branchLine);
}

TEST_CASE("Scheduling: independent store can fill branch delay after loop increment")
{
    const std::string body =
        "\tiaddiu vi01, vi00, 0\n"
        "\tiaddiu vi02, vi00, 4\n"
        "\tiaddiu vi03, vi00, 12\n"
        "\tiaddiu vi04, vi00, 0x20\n"
        "loop_lid:\n"
        "\tisw.w vi04, 3(vi03)\n"
        "\tiaddiu vi01, vi01, 1\n"
        "\tibne vi01, vi02, loop_lid\n";
    std::string vsm = runEmit(body, "vsmIndependentStoreDelay");
    REQUIRE(vsm.length() > 0);

    int branchLine = lineIndex(vsm, "ibne");
    int storeLine = lineIndex(vsm, "isw.w VI04, 3(VI03)");
    int bumpLine = lineIndex(vsm, "iaddiu VI01, VI01, 1");
    REQUIRE(branchLine >= 0);
    REQUIRE(storeLine >= 0);
    REQUIRE(bumpLine >= 0);
    CHECK(bumpLine < branchLine);
    CHECK(storeLine == branchLine + 1);
}

TEST_CASE("Scheduling: store using loop increment register stays before branch")
{
    const std::string body =
        "\tiaddiu vi01, vi00, 0\n"
        "\tiaddiu vi02, vi00, 4\n"
        "\tiaddiu vi03, vi00, 12\n"
        "loop_lid:\n"
        "\tisw.w vi01, 3(vi03)\n"
        "\tiaddiu vi01, vi01, 1\n"
        "\tibne vi01, vi02, loop_lid\n";
    std::string vsm = runEmit(body, "vsmNoIndependentStoreDelay");
    REQUIRE(vsm.length() > 0);

    int branchLine = lineIndex(vsm, "ibne");
    int storeLine = lineIndex(vsm, "isw.w VI01, 3(VI03)");
    REQUIRE(branchLine >= 0);
    REQUIRE(storeLine >= 0);
    CHECK(storeLine < branchLine);
}

TEST_CASE("Scheduling: dead fallthrough integer op can fill forward branch delay")
{
    const std::string body =
        "\tiaddiu vi01, vi00, 1\n"
        "\tiaddiu vi02, vi00, 1\n"
        "\tibeq vi01, vi02, done_lid\n"
        "\tiand vi01, vi02, vi00\n"
        "done_lid:\n"
        "\tiaddiu vi01, vi00, 7\n";
    std::string vsm = runEmit(body, "vsmDeadFallthroughDelay");
    REQUIRE(vsm.length() > 0);

    int branchLine = lineIndex(vsm, "ibeq");
    int fillerLine = lineIndex(vsm, "iand VI01");
    REQUIRE(branchLine >= 0);
    REQUIRE(fillerLine >= 0);
    CHECK(fillerLine == branchLine + 1);
}

TEST_CASE("Scheduling: live fallthrough integer op stays after branch delay")
{
    const std::string body =
        "\tiaddiu vi01, vi00, 1\n"
        "\tiaddiu vi02, vi00, 1\n"
        "\tibeq vi01, vi02, done_lid\n"
        "\tiand vi01, vi02, vi00\n"
        "done_lid:\n"
        "\tiadd vi04, vi01, vi00\n";
    std::string vsm = runEmit(body, "vsmLiveFallthroughNoDelay");
    REQUIRE(vsm.length() > 0);

    int branchLine = lineIndex(vsm, "ibeq");
    int fillerLine = lineIndex(vsm, "iand VI01");
    REQUIRE(branchLine >= 0);
    REQUIRE(fillerLine >= 0);
    CHECK(fillerLine > branchLine + 1);
}

TEST_CASE("Scheduling: terminal unconditional branch omits unreachable auto-exit footer")
{
    const std::string body =
        "loop_lid:\n"
        "\tiaddiu vi01, vi00, 1\n"
        "\t--cont\n"
        "\tb loop_lid\n";
    std::string vsm = runEmit(body, "vsmTerminalBranchNoAutoExit");
    REQUIRE(vsm.length() > 0);

    CHECK(lineIndex(vsm, "b loop_lid") >= 0);
    CHECK(countSubstrings(vsm, "nop[E]") == 1);
}

TEST_CASE("Pairing: independent upper op can pair with FDIV")
{
    // FDIV writes Q on the lower pipe.  With deferred waitq handling, an
    // unrelated upper-pipe op can issue in the same cycle safely.
    const std::string body =
        "\tadd.xyz vf01, vf00, vf00\n"
        "\tdiv q, vf00w, vf00w\n";
    std::string vsm = runEmit(body, "vsmPairFdiv");
    REQUIRE(vsm.length() > 0);
    CHECK(linePairsSubstrings(vsm, "add.xyz", "div"));
}

TEST_CASE("Pairing: independent upper op can pair with EFU")
{
    // EFU writes P on the lower pipe.  The dependency checker keeps P hazards
    // apart, but independent upper work should still fill the paired slot.
    const std::string body =
        "\tadd.xyz vf01, vf00, vf00\n"
        "\teleng p, vf00\n";
    std::string vsm = runEmit(body, "vsmPairEfu");
    REQUIRE(vsm.length() > 0);
    CHECK(linePairsSubstrings(vsm, "add.xyz", "eleng"));
}

TEST_CASE("Pairing: disjoint VF field writes can pair")
{
    const std::string body =
        "\tftoi4.xyz vf01, vf00\n"
        "\tmfir.w vf01, vi00\n";
    std::string vsm = runEmit(body, "vsmPairDisjointVfWrites");
    REQUIRE(vsm.length() > 0);
    CHECK(linePairsSubstrings(vsm, "ftoi4.xyz", "mfir.w"));
}

TEST_CASE("Pairing: overlapping VF field writes stay unpaired")
{
    const std::string body =
        "\tftoi4.xyz vf01, vf00\n"
        "\tmfir.x vf01, vi00\n";
    std::string vsm = runEmit(body, "vsmNoPairOverlappingVfWrites");
    REQUIRE(vsm.length() > 0);
    CHECK(!linePairsSubstrings(vsm, "ftoi4.xyz", "mfir.x"));
}

TEST_CASE("Scheduling: disjoint VF field write can move before an xyz reader")
{
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmul.xyz vf02, vf01, vf00\n"
        "\tmfir.w vf01, vi00\n";
    std::string vsm = runEmit(body, "vsmMoveDisjointVfWriteRead");
    REQUIRE(vsm.length() > 0);
    CHECK(lineIndex(vsm, "mfir.w") < lineIndex(vsm, "mul.xyz"));
}

TEST_CASE("Scheduling: overlapping VF field write cannot move before a reader")
{
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmul.xyz vf02, vf01, vf00\n"
        "\tmfir.x vf01, vi00\n";
    std::string vsm = runEmit(body, "vsmNoMoveOverlappingVfWriteRead");
    REQUIRE(vsm.length() > 0);
    CHECK(lineIndex(vsm, "mul.xyz") < lineIndex(vsm, "mfir.x"));
}

TEST_CASE("Pairing: disjoint VF field write does not stall an xyz reader")
{
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tiaddiu vi02, vi00, 1\n"
        "\tiaddiu vi03, vi00, 2\n"
        "\tiaddiu vi04, vi00, 3\n"
        "\tiaddiu vi05, vi00, 4\n"
        "\tiaddiu vi06, vi00, 5\n"
        "field_write_lid:\n"
        "\tmfir.w vf01, vi00\n"
        "\tmul.xyz vf02, vf01, vf00\n";
    std::string vsm = runEmit(body, "vsmFieldWriteDoesNotStallReader");
    REQUIRE(vsm.length() > 0);
    CHECK(linePairsSubstrings(vsm, "mul.xyz", "mfir.w"));
}

TEST_CASE("Scheduling: disjoint VF field write can cross an xyz reader")
{
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmove.xyzw vf09, vf00\n"
        "\tadd.xyz vf02, vf09, vf00\n"
        "\tmul.xyz vf03, vf01, vf00\n"
        "\tmfir.w vf01, vi00\n";
    std::string vsm = runEmit(body, "vsmMoveFieldWriteCrossesReader");
    REQUIRE(vsm.length() > 0);
    CHECK(lineIndex(vsm, "mfir.w") < lineIndex(vsm, "add.xyz"));
}

TEST_CASE("Scheduling: overlapping VF field write cannot cross a reader")
{
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmove.xyzw vf09, vf00\n"
        "\tadd.xyz vf02, vf09, vf00\n"
        "\tmul.xyz vf03, vf01, vf00\n"
        "\tmfir.x vf01, vi00\n";
    std::string vsm = runEmit(body, "vsmNoMoveFieldWriteCrossesReader");
    REQUIRE(vsm.length() > 0);
    CHECK(lineIndex(vsm, "mul.xyz") < lineIndex(vsm, "mfir.x"));
}

TEST_CASE("Scheduling: implicit broadcast reads the broadcast component")
{
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmulw.xyz vf02, vf01, vf01\n"
        "\tmfir.w vf01, vi00\n";
    std::string vsm = runEmit(body, "vsmImplicitBroadcastBlocksWWrite");
    REQUIRE(vsm.length() > 0);
    CHECK(lineIndex(vsm, "mulw.xyz") < lineIndex(vsm, "mfir.w"));
}

TEST_CASE("Scheduling: implicit broadcast waits for MFP W producer")
{
    const std::string body =
        "\tadd.xyz vf01, vf00, vf00\n"
        "\tesadd p, vf01\n"
        "\tmfp.w vf01, p\n"
        "\tersqrt p, vf01w\n"
        "\tmfp.w vf01, p\n"
        "\tmulw.xyz vf01, vf01, vf01\n";
    std::string vsm = runEmit(body, "vsmImplicitBroadcastWaitsForMfpW");
    REQUIRE(vsm.length() > 0);

    int mulwLine = lineIndex(vsm, "mulw.xyz");
    int firstMfpLine = lineIndex(vsm, "mfp.w");
    int lastMfpLine = lastLineIndex(vsm, "mfp.w");
    REQUIRE(mulwLine >= 0);
    REQUIRE(firstMfpLine >= 0);
    REQUIRE(lastMfpLine >= 0);
    CHECK(firstMfpLine < mulwLine);
    CHECK(lastMfpLine < mulwLine);
}

TEST_CASE("Pairing: independent non-adjacent lower op can fill current upper slot")
{
    // The iaddiu is separated from mulax by another upper-pipe op.  It can
    // safely move upward and pair with mulax because it has no dependency on
    // the crossed FMAC.
    const std::string body =
        "\tmulax acc, vf00, vf00x\n"
        "\tmadday acc, vf00, vf00y\n"
        "\tiaddiu vi01, vi00, 1\n";
    std::string vsm = runEmit(body, "vsmPairLookaheadIndependent");
    REQUIRE(vsm.length() > 0);
    CHECK(linePairsSubstrings(vsm, "mulax", "iaddiu"));
}

TEST_CASE("Pairing: distant independent lower op can fill current upper slot")
{
    // ps2gl setup blocks often have many same-pipe FMACs before the next
    // independent lower op.  The bounded lookahead should be wide enough to
    // find that lower op while dependency checks still decide what can cross.
    const std::string body =
        "\tmulax acc, vf00, vf00x\n"
        "\tmadday acc, vf00, vf00y\n"
        "\tmaddaz acc, vf00, vf00z\n"
        "\tmaddw vf01, vf00, vf00w\n"
        "\tmulax acc, vf00, vf00x\n"
        "\tmadday acc, vf00, vf00y\n"
        "\tmaddaz acc, vf00, vf00z\n"
        "\tmaddw vf02, vf00, vf00w\n"
        "\tmulax acc, vf00, vf00x\n"
        "\tmadday acc, vf00, vf00y\n"
        "\tiaddiu vi01, vi00, 1\n";
    std::string vsm = runEmit(body, "vsmPairLookaheadDistantIndependent");
    REQUIRE(vsm.length() > 0);
    CHECK(linePairsSubstrings(vsm, "mulax", "iaddiu VI01"));
}

TEST_CASE("Pairing: lookahead spans beyond 48 same-pipe candidates")
{
    std::string body = "\tmulax acc, vf00, vf00x\n";
    for( int i = 0; i < 60; ++i )
        body += "\tmadday acc, vf00, vf00y\n";
    body += "\tiaddiu vi01, vi00, 1\n";

    std::string vsm = runEmit(body, "vsmPairLookaheadBeyond48");
    REQUIRE(vsm.length() > 0);
    CHECK(linePairsSubstrings(vsm, "mulax", "iaddiu VI01"));
}

TEST_CASE("Pairing: non-adjacent candidate is not moved across a register conflict")
{
    // The later lq writes VF01.  The intervening madday reads VF01, so the
    // scheduler must not hoist lq above madday just to pair it with mulax.
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmove.xyzw vf02, vf00\n"
        "\tmove.xyzw vf03, vf00\n"
        "\tmove.xyzw vf04, vf00\n"
        "\tmulax acc, vf03, vf04x\n"
        "\tmadday acc, vf01, vf02y\n"
        "\tlq.xyz vf01, 0(vi00)\n";
    std::string vsm = runEmit(body, "vsmPairLookaheadConflict");
    REQUIRE(vsm.length() > 0);
    CHECK(!linePairsSubstrings(vsm, "mulax", "lq.xyz"));
}

TEST_CASE("Pairing: non-adjacent candidate is not moved before it is latency-ready")
{
    // The mtir reads VF09 produced by the earlier lq.  The intervening FMAC
    // can usefully cover part of that latency, so hoisting mtir upward would
    // create extra NOP padding even though there is no direct dependency with
    // madday.
    const std::string body =
        "\tlq.xyz vf09, 0(vi00)\n"
        "\tmulax acc, vf00, vf00x\n"
        "\tmadday acc, vf00, vf00y\n"
        "\tmtir vi01, vf09x\n";
    std::string vsm = runEmit(body, "vsmPairLookaheadLatencyReady");
    REQUIRE(vsm.length() > 0);
    CHECK(!linePairsSubstrings(vsm, "madday", "mtir"));
}

TEST_CASE("Scheduling: independent later op fills current register-latency wait")
{
    // mulax must wait for the two moves.  The later lq is independent and
    // ready, so the scheduler can issue it during that wait instead of
    // burning a pure NOP cycle.
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmove.xyzw vf02, vf00\n"
        "\tmulax acc, vf01, vf02x\n"
        "\tlq.xyz vf09, 0(vi00)\n";
    std::string vsm = runEmit(body, "vsmScheduleLatencyFiller");
    REQUIRE(vsm.length() > 0);

    int lqLine = lineIndex(vsm, "lq.xyz");
    int mulLine = lineIndex(vsm, "mulax");
    REQUIRE(lqLine >= 0);
    REQUIRE(mulLine >= 0);
    CHECK(lqLine < mulLine);
}

TEST_CASE("Scheduling: latency-gap filler can pair with another ready op")
{
    // mtir must wait for VF09 from the lqi.  The lqi's post-increment keeps it
    // from pairing with the later FMAC directly; both later instructions are
    // ready and independent of mtir, so they should occupy a single paired
    // cycle in the otherwise empty latency gap.
    const std::string body =
        "\tlqi.xyz vf09, (vi00++)\n"
        "\tmtir vi01, vf09x\n"
        "\tmulax acc, vf00, vf00x\n"
        "\tiaddiu vi02, vi00, 1\n";
    std::string vsm = runEmit(body, "vsmSchedulePairedLatencyFiller");
    REQUIRE(vsm.length() > 0);

    CHECK(linePairsSubstrings(vsm, "mulax", "iaddiu"));

    int pairLine = lineIndex(vsm, "mulax");
    int mtirLine = lineIndex(vsm, "mtir");
    REQUIRE(pairLine >= 0);
    REQUIRE(mtirLine >= 0);
    CHECK(pairLine < mtirLine);
}

TEST_CASE("Scheduling: independent FDIV producer can fill a register-latency gap")
{
    // FDIV writes Q, but Q is tracked as an implicit dependency.  When the
    // producer is independent of the waiting FMAC, it can cover the load
    // latency instead of acting as a hard scheduling barrier.
    const std::string body =
        "\tlq vf09, 0(vi00)\n"
        "\tmulax acc, vf09, vf00x\n"
        "\tdiv q, vf00w, vf00w\n"
        "\tmulq.xyz vf03, vf00, q\n";
    std::string vsm = runEmit(body, "vsmScheduleFdivLatencyFiller");
    REQUIRE(vsm.length() > 0);

    int divLine = lineIndex(vsm, "div");
    int mulLine = lineIndex(vsm, "mulax");
    int mulqLine = lineIndex(vsm, "mulq");
    REQUIRE(divLine >= 0);
    REQUIRE(mulLine >= 0);
    REQUIRE(mulqLine >= 0);
    CHECK(divLine < mulLine);
    CHECK(divLine < mulqLine);
}

TEST_CASE("Scheduling: independent upper op can pair with deferred waitp")
{
    const std::string body =
        "\tesadd p, vf00\n"
        "\t--barrier\n"
        "\tmfp.w vf01, p\n"
        "\tadd.xyz vf02, vf00, vf00\n";
    std::string vsm = runEmit(body, "vsmScheduleUpperWaitpFiller");
    REQUIRE(vsm.length() > 0);

    CHECK(linePairsSubstrings(vsm, "add.xyz", "waitp"));

    int addLine = lineIndex(vsm, "add.xyz");
    int mfpLine = lineIndex(vsm, "mfp.w");
    REQUIRE(addLine >= 0);
    REQUIRE(mfpLine >= 0);
    CHECK(addLine < mfpLine);
}

TEST_CASE("Scheduling: distant ready op fills current register-latency wait")
{
    // The first several candidates also read the pending load result, so they
    // are not ready.  A later independent integer op should still be found and
    // moved into the gap instead of burning a pure NOP.
    const std::string body =
        "\tlq.xyz vf09, 0(vi00)\n"
        "\tmtir vi01, vf09x\n"
        "\tadd.xyz vf10, vf09, vf00\n"
        "\tadd.xyz vf11, vf09, vf00\n"
        "\tadd.xyz vf12, vf09, vf00\n"
        "\tadd.xyz vf13, vf09, vf00\n"
        "\tadd.xyz vf14, vf09, vf00\n"
        "\tadd.xyz vf15, vf09, vf00\n"
        "\tadd.xyz vf16, vf09, vf00\n"
        "\tadd.xyz vf17, vf09, vf00\n"
        "\tadd.xyz vf18, vf09, vf00\n"
        "\tiaddiu vi02, vi00, 1\n";
    std::string vsm = runEmit(body, "vsmScheduleDistantLatencyFiller");
    REQUIRE(vsm.length() > 0);

    int fillerLine = lineIndex(vsm, "iaddiu VI02");
    int mtirLine = lineIndex(vsm, "mtir");
    REQUIRE(fillerLine >= 0);
    REQUIRE(mtirLine >= 0);
    CHECK(fillerLine < mtirLine);
}

TEST_CASE("Scheduling: latency filler spans beyond 48 unready candidates")
{
    std::string body =
        "\tlq.xyz vf09, 0(vi00)\n"
        "\tmtir vi01, vf09x\n";
    for( int i = 0; i < 60; ++i )
        body += "\tadd.xyz vf10, vf09, vf00\n";
    body += "\tiaddiu vi02, vi00, 1\n";

    std::string vsm = runEmit(body, "vsmScheduleLookaheadBeyond48");
    REQUIRE(vsm.length() > 0);

    int fillerLine = lineIndex(vsm, "iaddiu VI02");
    int mtirLine = lineIndex(vsm, "mtir");
    REQUIRE(fillerLine >= 0);
    REQUIRE(mtirLine >= 0);
    CHECK(fillerLine < mtirLine);
}

TEST_CASE("Scheduling: latency filler may load from a different base before a plain store")
{
    // The Q consumer must wait for DIV.  A later input load using VI03 can
    // cover that wait even though a plain output store using VI04 sits before
    // it in source order.
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmove.xyzw vf02, vf00\n"
        "\tmove.xyzw vf03, vf00\n"
        "\tmove.xyzw vf10, vf00\n"
        "\tiaddiu vi03, vi00, 0\n"
        "\tiaddiu vi04, vi00, 4\n"
        "\tdiv q, vf01w, vf02w\n"
        "\tmulq.xyz vf03, vf03, q\n"
        "\tsq.xyz vf10, 0(vi04)\n"
        "\tlq.xyz vf20, 0(vi03)\n";
    std::string vsm = runEmit(body, "vsmScheduleLoadAcrossStore");
    REQUIRE(vsm.length() > 0);

    int lqLine = lineIndex(vsm, "lq.xyz VF20");
    int mulqLine = lineIndex(vsm, "mulq");
    REQUIRE(lqLine >= 0);
    REQUIRE(mulqLine >= 0);
    CHECK(lqLine < mulqLine);
}

TEST_CASE("Scheduling: latency filler does not move a load before a same-base store")
{
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmove.xyzw vf02, vf00\n"
        "\tmove.xyzw vf03, vf00\n"
        "\tmove.xyzw vf10, vf00\n"
        "\tiaddiu vi03, vi00, 0\n"
        "\tdiv q, vf01w, vf02w\n"
        "\tmulq.xyz vf03, vf03, q\n"
        "\tsq.xyz vf10, 0(vi03)\n"
        "\tlq.xyz vf20, 0(vi03)\n";
    std::string vsm = runEmit(body, "vsmScheduleNoLoadAcrossSameBaseStore");
    REQUIRE(vsm.length() > 0);

    int sqLine = lineIndex(vsm, "sq.xyz");
    int lqLine = lineIndex(vsm, "lq.xyz VF20");
    REQUIRE(sqLine >= 0);
    REQUIRE(lqLine >= 0);
    CHECK(sqLine < lqLine);
}

TEST_CASE("Scheduling: latency filler may move a load before a different-offset same-base store")
{
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmove.xyzw vf02, vf00\n"
        "\tmove.xyzw vf03, vf00\n"
        "\tmove.xyzw vf10, vf00\n"
        "\tiaddiu vi03, vi00, 0\n"
        "\tdiv q, vf01w, vf02w\n"
        "\tmulq.xyz vf03, vf03, q\n"
        "\tsq.xyz vf10, 0(vi03)\n"
        "\tlq.xyz vf20, 2(vi03)\n";
    std::string vsm = runEmit(body, "vsmScheduleLoadAcrossDifferentOffsetStore");
    REQUIRE(vsm.length() > 0);

    int lqLine = lineIndex(vsm, "lq.xyz VF20");
    int mulqLine = lineIndex(vsm, "mulq");
    REQUIRE(lqLine >= 0);
    REQUIRE(mulqLine >= 0);
    CHECK(lqLine < mulqLine);
}

TEST_CASE("Scheduling: latency filler may move independent integer compute before a plain store")
{
    // The iaddiu is pure integer work and does not feed the store, so it can
    // move above that store to cover the lq->mtir latency.
    const std::string body =
        "\tmove.xyzw vf10, vf00\n"
        "\tiaddiu vi04, vi00, 4\n"
        "\tlq.xyz vf09, 0(vi00)\n"
        "\tmtir vi01, vf09x\n"
        "\tsq.xyz vf10, 0(vi04)\n"
        "\tiaddiu vi02, vi00, 1\n";
    std::string vsm = runEmit(body, "vsmScheduleComputeAcrossStore");
    REQUIRE(vsm.length() > 0);

    int addLine = lineIndex(vsm, "iaddiu VI02");
    int mtirLine = lineIndex(vsm, "mtir");
    REQUIRE(addLine >= 0);
    REQUIRE(mtirLine >= 0);
    CHECK(addLine < mtirLine);
}

TEST_CASE("Scheduling: latency filler may move integer compute before a waiting plain store")
{
    const std::string body =
        "\tiaddiu vi04, vi00, 4\n"
        "\tmfir.x vf01, vi00\n"
        "\tsq.x vf01, 0(vi04)\n"
        "\tiaddiu vi02, vi00, 1\n";
    std::string vsm = runEmit(body, "vsmScheduleComputeBeforeWaitingStore");
    REQUIRE(vsm.length() > 0);

    int addLine = lineIndex(vsm, "iaddiu VI02");
    int storeLine = lineIndex(vsm, "sq.x");
    REQUIRE(addLine >= 0);
    REQUIRE(storeLine >= 0);
    CHECK(addLine < storeLine);
}

TEST_CASE("Scheduling: latency filler can skip an unmovable candidate")
{
    const std::string body =
        "\tiaddiu vi04, vi00, 4\n"
        "\tmfir.x vf01, vi00\n"
        "\tsq.x vf01, 0(vi04)\n"
        "\tlq.x vf02, 0(vi04)\n"
        "\tiaddiu vi02, vi00, 1\n";
    std::string vsm = runEmit(body, "vsmScheduleSkipsUnmovableCandidate");
    REQUIRE(vsm.length() > 0);

    int addLine = lineIndex(vsm, "iaddiu VI02");
    int storeLine = lineIndex(vsm, "sq.x");
    REQUIRE(addLine >= 0);
    REQUIRE(storeLine >= 0);
    CHECK(addLine < storeLine);
}

TEST_CASE("Scheduling: latency filler may move a distinct-address store before a plain load")
{
    const std::string body =
        "\tlq vf09, 0(vi00)\n"
        "\tmtir vi01, vf09x\n"
        "\tlq.xyz vf20, 0(vi01)\n"
        "\tsq.xyz vf00, 8(vi00)\n";
    std::string vsm = runEmit(body, "vsmScheduleStoreAcrossLoad");
    REQUIRE(vsm.length() > 0);

    int storeLine = lineIndex(vsm, "sq.xyz");
    int mtirLine = lineIndex(vsm, "mtir");
    REQUIRE(storeLine >= 0);
    REQUIRE(mtirLine >= 0);
    CHECK(storeLine < mtirLine);
}

TEST_CASE("Scheduling: latency filler does not move a store before a same-address store")
{
    const std::string body =
        "\tmove.xyzw vf10, vf00\n"
        "\tiaddiu vi04, vi00, 4\n"
        "\tlq vf09, 0(vi00)\n"
        "\tmtir vi01, vf09x\n"
        "\tsq vf09, 0(vi04)\n"
        "\tsq vf10, 0(vi04)\n";
    std::string vsm = runEmit(body, "vsmScheduleNoStoreAcrossSameAddressStore");
    REQUIRE(vsm.length() > 0);

    int firstStore = lineIndex(vsm, "sq VF09");
    int secondStore = lineIndex(vsm, "sq VF10");
    REQUIRE(firstStore >= 0);
    REQUIRE(secondStore >= 0);
    CHECK(firstStore < secondStore);
}

TEST_CASE("Scheduling: latency filler does not move store base update before that store")
{
    const std::string body =
        "\tmove.xyzw vf10, vf00\n"
        "\tiaddiu vi04, vi00, 4\n"
        "\tlq.xyz vf09, 0(vi00)\n"
        "\tmtir vi01, vf09x\n"
        "\tsq.xyz vf10, 0(vi04)\n"
        "\tiaddiu vi04, vi04, 1\n";
    std::string vsm = runEmit(body, "vsmScheduleNoStoreProducerAcrossStore");
    REQUIRE(vsm.length() > 0);

    int storeLine = lineIndex(vsm, "sq.xyz");
    int addLine = lineIndex(vsm, "iaddiu VI04, VI04");
    REQUIRE(storeLine >= 0);
    REQUIRE(addLine >= 0);
    CHECK(storeLine < addLine);
}

TEST_CASE("Scheduling: adjacent iaddiu pointer bump chain coalesces")
{
    const std::string body =
        "\tiaddiu vi03, vi00, 0\n"
        "\tiaddiu vi03, vi03, 3\n"
        "\tiaddiu vi03, vi03, 3\n"
        "\tiaddiu vi03, vi03, 3\n";
    std::string vsm = runEmit(body, "vsmCoalesceSelfIaddiu");
    REQUIRE(vsm.length() > 0);

    CHECK(lineIndex(vsm, "iaddiu VI03, VI00, 9") >= 0);
    CHECK(lineIndex(vsm, "iaddiu VI03, VI03, 3") < 0);
}

TEST_CASE("Scheduling: adjacent iaddiu followed by self bump coalesces")
{
    const std::string body =
        "\tiaddiu vi03, vi00, 5\n"
        "\tiaddiu vi03, vi03, 3\n";
    std::string vsm = runEmit(body, "vsmCoalesceIaddiuChain");
    REQUIRE(vsm.length() > 0);

    CHECK(lineIndex(vsm, "iaddiu VI03, VI00, 8") >= 0);
    CHECK(lineIndex(vsm, "iaddiu VI03, VI03, 3") < 0);
}

TEST_CASE("Scheduling: overwriting an already-read register does not create a WAR stall")
{
    // Source operands are latched when the older FMAC issues, so a later
    // instruction may overwrite VF02 without waiting for all older reads of
    // VF02 to retire.  The true hazard is RAW from an older write to a later
    // read, which is covered by the latency tests.
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmove.xyzw vf02, vf00\n"
        "\tmove.xyzw vf03, vf00\n"
        "\tmove.xyzw vf04, vf00\n"
        "\tmove.xyzw vf05, vf00\n"
        "\tmulax acc, vf01, vf02x\n"
        "\tmadday acc, vf03, vf02y\n"
        "\tmaddaz acc, vf04, vf02z\n"
        "\tmaddw vf02, vf05, vf00w\n";
    std::string vsm = runEmit(body, "vsmScheduleNoRegisterWarStall");
    REQUIRE(vsm.length() > 0);

    int lastRead = lineIndex(vsm, "maddaz");
    int overwrite = lineIndex(vsm, "maddw");
    REQUIRE(lastRead >= 0);
    REQUIRE(overwrite >= 0);
    CHECK(overwrite == lastRead + 1);
}

TEST_CASE("Pairing: LOI is not paired with the next FMAC that reads I")
{
    // loi writes I.  An immediately-following FMAC that reads I must
    // NOT share a cycle with the loi or it will see the stale I.
    // Operand::IWRITE is the only signal that loi writes I — the
    // immediate Argument doesn't carry the WRITE flag.
    const std::string body =
        "\tloi 0x44fff000\n"
        "\taddi.xy vf01, vf00, i\n";
    std::string vsm = runEmit(body, "vsmPairLoiI");
    REQUIRE(vsm.length() > 0);
    CHECK(!linePairsSubstrings(vsm, "addi.xy", "loi"));
}

TEST_CASE("Pairing: FMAC reading I is not paired with following LOI")
{
	// Pairing an I-consuming FMAC with the next LOI changes the logo lighting
	// path: the upper pipe observes the new I value on hardware.
	const std::string body =
		"\tloi 0x42000000\n"
		"\taddi.xy vf01, vf00, i\n"
		"\tloi 0x43000000\n";
	std::string vsm = runEmit(body, "vsmNoPairFmacIToLoi");
	REQUIRE(vsm.length() > 0);
	CHECK(!linePairsSubstrings(vsm, "addi.xy", "loi 0x43000000"));
	CHECK(!linePairsSubstrings(vsm, "addi.xy", "loi 0x42000000"));
}

TEST_CASE("Pairing: FMAC opmsub is not paired with fmand reading MAC")
{
    // Every FMAC implicitly writes the MAC flag register with 4-cycle
    // latency.  Pairing opmsub with the next fmand makes fmand see
    // stale flags from before opmsub - breaks back-face cull etc.
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmove.xyzw vf02, vf00\n"
        "\tmove.xyzw vf03, vf00\n"
        "\topmula.xyz acc, vf01, vf02\n"
        "\topmsub.xyz vf04, vf02, vf01\n"
        "\tfmand vi01, vi00\n";
    std::string vsm = runEmit(body, "vsmPairOpmsubFmand");
    REQUIRE(vsm.length() > 0);
    CHECK(!linePairsSubstrings(vsm, "opmsub", "fmand"));
}

TEST_CASE("Pairing: FMAC clipw is not paired with fcand reading CLIP")
{
    // clipw writes the CLIP register with FMAC latency.  fcand reads
    // CLIP and must see the post-clipw value.  Pairing them is a
    // stale-read hazard, just like the MAC case above.
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmove.xyzw vf02, vf00\n"
        "\tclipw.xyz vf01, vf02w\n"
        "\tfcand vi01, 0xffffff\n";
    std::string vsm = runEmit(body, "vsmPairClipwFcand");
    REQUIRE(vsm.length() > 0);
    CHECK(!linePairsSubstrings(vsm, "clipw", "fcand"));
}

TEST_CASE("Pairing: non-clip FMAC can pair with latency-ready fcand")
{
    // fcand reads the CLIP flags, not MAC.  Once the earlier clipw result
    // is ready, an unrelated FMAC that only writes MAC can occupy the upper
    // pipe in the same cycle.
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmove.xyzw vf02, vf00\n"
        "\tmove.xyzw vf03, vf00\n"
        "\tclipw.xyz vf01, vf00w\n"
        "\tsub.xyz vf04, vf01, vf02\n"
        "\tsub.xyz vf05, vf03, vf02\n"
        "\tmulw.xyz vf10, vf04, vf00w\n"
        "\tfcand vi01, 0xffffff\n";
    std::string vsm = runEmit(body, "vsmPairFmacFcand");
    REQUIRE(vsm.length() > 0);
    CHECK(linePairsSubstrings(vsm, "mulw", "fcand"));
}

TEST_CASE("Pairing: FMAC mulq reading Q is not paired with FDIV writing Q")
{
    // FDIV is excluded from pairing wholesale (Unit==FDIV check) but
    // we also want the conservative resource-conflict path to catch
    // it: any token that writes Q can't share a cycle with one that
    // reads Q, regardless of which pipe.
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmove.xyzw vf02, vf00\n"
        "\tdiv q, vf01w, vf02w\n"
        "\tmulq.xyz vf03, vf00, q\n";
    std::string vsm = runEmit(body, "vsmPairDivMulq");
    REQUIRE(vsm.length() > 0);
    CHECK(!linePairsSubstrings(vsm, "div", "mulq"));
}
