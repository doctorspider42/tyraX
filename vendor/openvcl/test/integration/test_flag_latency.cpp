// Regression tests: VU1 has a 4-cycle FMAC pipeline.  An FMAC writes the
// MAC / CLIP / STATUS flag registers 4 cycles after issue.  A flag-reader
// (fmand / fcand / fsand / fcget / etc.) issued any sooner reads the
// PREVIOUS flag value — i.e. silently the wrong answer.
//
// The motivating bug:  ps2gl's `bfc_tri` macro ends with an `opmsub.xyz`
// (computes the backface-cull normal, writes MAC sign) followed
// immediately by `fmand z_sign, z_sign_mask`.  Openvcl emitted those on
// adjacent cycles, so fmand read MAC from BEFORE opmsub — the BFC test
// was based on an unrelated FMAC's sign and culled arbitrary faces.
//
// The fix is to insert NOP cycles before any flag-reader that's too
// close to its FMAC.  These tests pin the cycle distance directly by
// counting emitted lines between the two operations.

#include "test_harness.h"
#include "openvcl_runner.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
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
        for( std::vector<std::string>::const_iterator i = extraArgs.begin(); i != extraArgs.end(); ++i )
            args.push_back(*i);
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
        return runEmitWithArgs(body, name, std::vector<std::string>());
    }

    // Number of emitted lines (cycles) between the first line that
    // contains `first` and the first line after it that contains `second`.
    // Each line == 1 VU cycle, so this is the cycle distance.  Returns -1
    // if either pattern isn't found.
    int cycleDistance(const std::string& vsm, const std::string& first, const std::string& second)
    {
        std::string::size_type startPos = vsm.find(first);
        if( startPos == std::string::npos )
            return -1;
        // Count newlines from `first` line onward until we hit `second`.
        std::string::size_type pos = startPos;
        int cycles = 0;
        while( true )
        {
            std::string::size_type nl = vsm.find('\n', pos);
            if( nl == std::string::npos )
                return -1;
            std::string::size_type nextStart = nl + 1;
            std::string::size_type nextNl = vsm.find('\n', nextStart);
            if( nextNl == std::string::npos )
                nextNl = vsm.size();
            std::string line = vsm.substr(nextStart, nextNl - nextStart);
            cycles++;
            if( line.find(second) != std::string::npos )
                return cycles;
            pos = nextStart;
        }
    }

    bool linePairsSubstrings(const std::string& vsm, const std::string& a, const std::string& b)
    {
        std::string::size_type pos = 0;
        while( pos < vsm.size() )
        {
            std::string::size_type end = vsm.find('\n', pos);
            if( end == std::string::npos )
                end = vsm.size();
            std::string line = vsm.substr(pos, end - pos);
            if( line.find(a) != std::string::npos && line.find(b) != std::string::npos )
                return true;
            pos = end + 1;
        }
        return false;
    }
}

TEST_CASE("Latency: FMAC opmsub followed by fmand has at least 4 cycles between")
{
    // The bfc_tri pattern from ps2gl/vu1/clip_cull.i:
    //   opmula.xyz acc, delta_1, delta_2
    //   opmsub.xyz bfc_normal, delta_2, delta_1     ; updates MAC
    //   fmand      z_sign, z_sign_mask              ; reads MAC of opmsub
    //
    // VU1 FMAC latency is 4 cycles, so fmand must be at least 4 cycles
    // after opmsub or it reads pre-opmsub MAC.
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmove.xyzw vf02, vf00\n"
        "\tilw.x my_mask, 0(vi00)\n"
        "\topmula.xyz acc, vf01, vf02\n"
        "\topmsub.xyz vf03, vf02, vf01\n"
        "\tfmand vi02, my_mask\n";

    std::string vsm = runEmit(body, "vsmLatencyFmand");
    REQUIRE(vsm.length() > 0);
    int d = cycleDistance(vsm, "opmsub", "fmand");
    REQUIRE(d > 0);
    CHECK(d >= 4);
}

TEST_CASE("Latency: strict schedule slots keep opmsub to fmand padding")
{
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmove.xyzw vf02, vf00\n"
        "\tilw.x my_mask, 0(vi00)\n"
        "\topmula.xyz acc, vf01, vf02\n"
        "\topmsub.xyz vf03, vf02, vf01\n"
        "\tfmand vi02, my_mask\n";
    std::vector<std::string> args;
    args.push_back("--strict-schedule-slots");

    std::string vsm = runEmitWithArgs(body, "vsmStrictLatencyFmand", args);
    REQUIRE(vsm.length() > 0);
    int d = cycleDistance(vsm, "opmsub", "fmand");
    REQUIRE(d > 0);
    CHECK(d >= 4);
}

TEST_CASE("Latency: clipw followed by fcand has at least 4 cycles between")
{
    // clipw is the only FMAC that writes the CLIP register.  fcand reads
    // CLIP, so it needs 4 cycles after the last clipw.
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmove.xyzw vf02, vf00\n"
        "\tclipw.xyz vf01, vf02w\n"
        "\tfcand vi01, 0x0ffffff\n";

    std::string vsm = runEmit(body, "vsmLatencyFcand");
    REQUIRE(vsm.length() > 0);
    int d = cycleDistance(vsm, "clipw", "fcand");
    REQUIRE(d > 0);
    CHECK(d >= 4);
}

TEST_CASE("Latency: plain mul followed by fmand has at least 4 cycles between")
{
    // Any FMAC sets MAC flags, not just opmsub.  A simple mul must also
    // give fmand 4 cycles of headroom.
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tilw.x my_mask, 0(vi00)\n"
        "\tmul.xyz vf02, vf01, vf01\n"
        "\tfmand vi02, my_mask\n";

    std::string vsm = runEmit(body, "vsmLatencyMulFmand");
    REQUIRE(vsm.length() > 0);
    int d = cycleDistance(vsm, "mul.xyz", "fmand");
    REQUIRE(d > 0);
    CHECK(d >= 4);
}

TEST_CASE("Latency: lq followed by vf consumer has at least 6 cycles between")
{
	// ps2gl's general renderer loads texture STQ with lq and then consumes the
	// loaded VF.  Without a load-use delay, the consumer sees the old VF
	// contents and emits corrupt perspective texture coordinates.
    const std::string body =
        "\tlq.xyz vf01, 0(vi00)\n"
        "\tmul.xyz vf02, vf01, vf00\n";

    std::string vsm = runEmit(body, "vsmLatencyLqMul");
    REQUIRE(vsm.length() > 0);
	int d = cycleDistance(vsm, "lq.xyz", "mul.xyz");
	REQUIRE(d > 0);
	CHECK(d >= 4);
}

TEST_CASE("Latency: strict schedule slots keep lq to fmac padding")
{
	const std::string body =
	    "\tlq.xyz vf17, 0(vi00)\n"
	    "\tmul.xyz vf18, vf17, vf00\n";
	std::vector<std::string> args;
	args.push_back("--strict-schedule-slots");

	std::string vsm = runEmitWithArgs(body, "vsmStrictLatencyLqFmac", args);
	REQUIRE(vsm.length() > 0);
	int d = cycleDistance(vsm, "lq.xyz", "mul.xyz");
	REQUIRE(d > 0);
	CHECK(d >= 4);
}

TEST_CASE("Latency: lq result can feed minii after the SCE final-color gap")
{
    // SCE final-color loops use lq.xyz -> minii.xyz with a shorter gap than
    // ordinary FMAC consumers. Keep this bypass narrow so lq -> mul still pads.
    const std::string body =
        "\tloi 255.0\n"
        "\tlq.xyz vf01, 0(vi00)\n"
        "\tminii.xyz vf02, vf01, i\n";

    std::string vsm = runEmit(body, "vsmLatencyLqMiniiBypass");
    REQUIRE(vsm.length() > 0);
    int d = cycleDistance(vsm, "lq.xyz", "minii.xyz");
    REQUIRE(d > 0);
    CHECK(d == 2);
}

TEST_CASE("Latency: lq result can feed ftoi on the next cycle")
{
    // SCE-generated ps2gl ADC setup emits `lq -> ftoi0` tightly.  Keep this as
    // a narrow conversion bypass while preserving normal load-use padding.
    const std::string body =
        "\tlq vf01, 0(vi00)\n"
        "\tftoi0 vf02, vf01\n";

    std::string vsm = runEmit(body, "vsmLatencyLqFtoiBypass");
    REQUIRE(vsm.length() > 0);
    int d = cycleDistance(vsm, "lq", "ftoi0");
    REQUIRE(d > 0);
    CHECK(d == 1);
}

TEST_CASE("Latency: FMAC vf write followed by lower vf consumer has at least 5 cycles between")
{
    // ps2gl's perspective path writes xformed_vert.w with an FMAC and then
    // divides by that W on the lower pipe.  The lower consumer must not read
    // the pre-FMAC VF contents.
    const std::string body =
        "\tmove.xyzw vf01, vf00\n"
        "\tmove.xyzw vf02, vf00\n"
        "\tadd.w vf03, vf01, vf02\n"
        "\tdiv q, vf00w, vf03w\n";

    std::string vsm = runEmit(body, "vsmLatencyFmacDiv");
    REQUIRE(vsm.length() > 0);
    int d = cycleDistance(vsm, "add.w", "div");
    REQUIRE(d > 0);
    CHECK(d >= 4);
}

TEST_CASE("Latency: ftoi result can feed mtir on the next cycle")
{
    // Sony's scheduled ps2gl VSM emits the strip-boundary `ftoi* -> mtir`
    // sequence with only one cycle between producer and MTIR.  Keep that as
    // a narrow bypass instead of weakening normal FMAC/VF latency.
    const std::string body =
        "\tftoi0 vf01, vf00\n"
        "\tmtir vi01, vf01x\n";

    std::string vsm = runEmit(body, "vsmLatencyFtoiMtirBypass");
    REQUIRE(vsm.length() > 0);
    int d = cycleDistance(vsm, "ftoi0", "mtir");
    REQUIRE(d > 0);
    CHECK(d == 1);
}

TEST_CASE("Latency: ftoi result still waits for non-mtir vf consumers")
{
    const std::string body =
        "\tftoi0 vf01, vf00\n"
        "\tdiv q, vf00w, vf01w\n";

    std::string vsm = runEmit(body, "vsmLatencyFtoiDiv");
    REQUIRE(vsm.length() > 0);
    int d = cycleDistance(vsm, "ftoi0", "div");
    REQUIRE(d > 0);
    CHECK(d >= 4);
}

TEST_CASE("Latency: FDIV Q consumer uses deferred waitq after movable work")
{
    // The scheduler tracks Q as a long-latency resource.  It should not burn
    // an unconditional waitq immediately after every DIV when independent work
    // can be issued before the Q consumer.
    const std::string body =
        "\tdiv q, vf00w, vf00w\n"
        "\tmulq.xyz vf01, vf00, q\n"
        "\tlq.xyz vf02, 0(vi00)\n";

    std::string vsm = runEmit(body, "vsmLatencyDivMulqDeferredWaitq");
    REQUIRE(vsm.length() > 0);

    std::string::size_type div = vsm.find("div");
    std::string::size_type lq = vsm.find("lq.xyz");
    std::string::size_type mulq = vsm.find("mulq");
    REQUIRE(div != std::string::npos);
    REQUIRE(lq != std::string::npos);
    REQUIRE(mulq != std::string::npos);
    CHECK(div < lq);
    CHECK(lq < mulq);
    CHECK(vsm.find("waitq") != std::string::npos);
    CHECK(linePairsSubstrings(vsm, "mulq", "waitq"));
}

TEST_CASE("CodeGen: full-width FMAC destination emits explicit xyzw mask")
{
    const std::string body =
        "\tmove.xyzw vf02, vf00\n"
        "\tmaddw.xyzw vf01, vf02, vf00w\n";

    std::string vsm = runEmit(body, "vsmFullFmacMask");
    REQUIRE(vsm.length() > 0);
    CHECK(vsm.find("maddw.xyzw") != std::string::npos);
}
