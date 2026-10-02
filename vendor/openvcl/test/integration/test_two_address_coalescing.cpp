// Regression tests: the allocator must coalesce two-address ops where
// the destination and source carry the same source-level VCL name.
//
// The motivating bug:  ps2gl's general_quad.vcl runs a per-light loop
//
//     dir_light_loop:
//         ... use num_dir_lights ...
//         isubiu num_dir_lights, num_dir_lights, 1
//         ibgtz  num_dir_lights, dir_light_loop
//
// Before the fix, openvcl spawned a *new* alias on each write to
// num_dir_lights and the allocator was free to put it on a different
// physical VI — typically `isubiu VI02, VI01, 1`.  The branch then
// reads VI01 (the old, never-decremented value), so the loop runs
// forever.  xgkick at the end of the shader is never reached, no
// PATH1 GIF transfer is emitted, and the cube in box.elf renders
// invisible on the GS.
//
// The fix records a `sameNamePredecessor` link on each newly-created
// write alias and steers the allocator to reuse the predecessor's
// register when no conflict prevents it.  These tests pin the
// behaviour at the VSM level.

#include "test_harness.h"
#include "openvcl_runner.h"

#include <cctype>
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

    std::string runEmit(const std::string& body, const std::string& name)
    {
        char tmpl[] = "/tmp/openvcl_test_XXXXXX.vsm";
        int fd = mkstemps(tmpl, 4);
        if( fd < 0 )
            return std::string();
        close(fd);

        std::vector<std::string> args;
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

    // Parse the i-th occurrence (0-indexed) of `mnemonic` and pull out
    // up to three comma-separated operands.  VSM lines look like:
    //
    //   "    nop                             isubiu VI01, VI01, 1"
    //
    // We start at the mnemonic, skip past its optional field suffix
    // (.x / .xyzw / .xyz / etc.), then split the remainder on commas
    // up to end-of-line.  Returns the operand strings stripped of
    // surrounding whitespace; missing slots come back empty.
    struct Operands
    {
        std::string dst;
        std::string src1;
        std::string src2;
    };

    std::string strip(const std::string& s)
    {
        std::string::size_type a = 0;
        while( a < s.size() && (s[a] == ' ' || s[a] == '\t') ) ++a;
        std::string::size_type b = s.size();
        while( b > a && (s[b - 1] == ' ' || s[b - 1] == '\t') ) --b;
        return s.substr(a, b - a);
    }

    Operands parseNth(const std::string& vsm, const std::string& mnemonic, int n)
    {
        Operands ops;
        std::string::size_type pos = 0;
        for( int i = 0; i <= n; ++i )
        {
            pos = vsm.find(mnemonic, pos);
            if( pos == std::string::npos )
                return ops;
            if( i < n )
                pos += mnemonic.size();
        }
        // Skip past mnemonic and any .suffix.
        std::string::size_type i = pos + mnemonic.size();
        while( i < vsm.size() && (vsm[i] == '.' || std::isalnum((unsigned char)vsm[i])) )
            ++i;
        // End at newline.
        std::string::size_type eol = vsm.find('\n', i);
        if( eol == std::string::npos )
            eol = vsm.size();
        std::string rest = vsm.substr(i, eol - i);
        // Split on commas.
        std::vector<std::string> parts;
        std::string::size_type a = 0;
        while( a <= rest.size() )
        {
            std::string::size_type b = rest.find(',', a);
            if( b == std::string::npos )
                b = rest.size();
            parts.push_back(strip(rest.substr(a, b - a)));
            if( b == rest.size() )
                break;
            a = b + 1;
        }
        if( !parts.empty() ) ops.dst = parts[0];
        if( parts.size() >= 2 ) ops.src1 = parts[1];
        if( parts.size() >= 3 ) ops.src2 = parts[2];
        return ops;
    }

    // Convenience: just the destination register of the first occurrence.
    std::string firstDest(const std::string& vsm, const std::string& mnemonic)
    {
        return parseNth(vsm, mnemonic, 0).dst;
    }
}

TEST_CASE("Two-address: isubiu x,x,1 in a loop is in-place")
{
    // The minimal reproduction of the box.elf hang.  `counter` is loaded
    // before the loop, decremented inside, and tested by ibgtz.  If the
    // allocator does NOT coalesce the isubiu's dest with its src, the
    // branch reads a stale register and the loop never terminates on
    // the VU.
    //
    // We verify three things in lockstep:
    //   1. isubiu is in-place: dst == src
    //   2. The initial load (ilw) writes the same physical register
    //   3. The branch (ibgtz) reads the same physical register
    // Together this proves the entire counter chain lives on one VI.
    const std::string body =
        "\tilw.x counter, 0(vi00)\n"
        "loop:\n"
        "\tisubiu counter, counter, 1\n"
        "\tibgtz counter, loop\n";

    std::string vsm = runEmit(body, "vsmTwoAddrLoop");
    REQUIRE(vsm.length() > 0);

    Operands sub = parseNth(vsm, "isubiu", 0);
    REQUIRE(sub.dst.length() > 0);
    REQUIRE(sub.src1.length() > 0);
    CHECK(sub.dst == sub.src1);

    std::string loadDst = firstDest(vsm, "ilw");
    REQUIRE(loadDst.length() > 0);
    CHECK(loadDst == sub.dst);

    // ibgtz <reg>, <label> — read register is the dst slot in our parser.
    Operands br = parseNth(vsm, "ibgtz", 0);
    REQUIRE(br.dst.length() > 0);
    CHECK(br.dst == sub.dst);
}

TEST_CASE("Two-address: isubiu x,x,1 straight-line stays in-place")
{
    // Same op without a loop — still expected to be in-place because the
    // VCL source name `counter` is reused as both src and dst.  This is
    // the simpler version that pins the basic mechanism independent of
    // any branch handling.
    const std::string body =
        "\tilw.x counter, 0(vi00)\n"
        "\tisubiu counter, counter, 1\n"
        "\tiaddiu sink, counter, 0\n";

    std::string vsm = runEmit(body, "vsmTwoAddrStraight");
    REQUIRE(vsm.length() > 0);

    Operands sub = parseNth(vsm, "isubiu", 0);
    REQUIRE(sub.dst.length() > 0);
    REQUIRE(sub.src1.length() > 0);
    CHECK(sub.dst == sub.src1);
}

TEST_CASE("Two-address: iaddiu y,y,k coalesces the same way")
{
    // The fix is mnemonic-agnostic — it's driven by the alias-name match,
    // not by the specific opcode.  Re-verify with iaddiu to catch any
    // accidental opcode coupling.
    const std::string body =
        "\tilw.x tick, 0(vi00)\n"
        "loop:\n"
        "\tiaddiu tick, tick, 1\n"
        "\tibgtz tick, loop\n";

    std::string vsm = runEmit(body, "vsmTwoAddrIaddiu");
    REQUIRE(vsm.length() > 0);

    Operands add = parseNth(vsm, "iaddiu", 0);
    REQUIRE(add.dst.length() > 0);
    REQUIRE(add.src1.length() > 0);
    CHECK(add.dst == add.src1);
}

TEST_CASE("Two-address: independent counters get independent registers")
{
    // Two unrelated VCL names (`outer`, `inner`) must NOT be coalesced
    // — the fix only applies when the source-level name matches.  This
    // guards against an overzealous coalescer collapsing distinct
    // values onto one register.
    const std::string body =
        "\tilw.x outer, 0(vi00)\n"
        "\tilw.y inner, 0(vi00)\n"
        "outerloop:\n"
        "\tisubiu inner, inner, 1\n"
        "\tibgtz inner, outerloop\n"
        "\tisubiu outer, outer, 1\n"
        "\tibgtz outer, outerloop\n";

    std::string vsm = runEmit(body, "vsmTwoCounters");
    REQUIRE(vsm.length() > 0);

    Operands innerSub = parseNth(vsm, "isubiu", 0);
    Operands outerSub = parseNth(vsm, "isubiu", 1);
    REQUIRE(innerSub.dst.length() > 0);
    REQUIRE(outerSub.dst.length() > 0);

    // Each isubiu is in-place on its own counter.
    CHECK(innerSub.dst == innerSub.src1);
    CHECK(outerSub.dst == outerSub.src1);
    // The two counters live on DIFFERENT registers.
    CHECK(innerSub.dst != outerSub.dst);
}

TEST_CASE("Two-address: chained writes keep the initial root register")
{
    // The general_quad case decrements a counter several times across
    // a basic block.  Each successive write should chain back to the
    // root alias and prefer its register.  At least the first chain
    // hop must hold — that's the cycle that drives the loop branch.
    const std::string body =
        "\tilw.x counter, 0(vi00)\n"
        "\tisubiu counter, counter, 1\n"
        "\tisubiu counter, counter, 1\n"
        "\tiaddiu sink, counter, 0\n";

    std::string vsm = runEmit(body, "vsmTwoAddrChain");
    REQUIRE(vsm.length() > 0);

    Operands first = parseNth(vsm, "isubiu", 0);
    REQUIRE(first.dst.length() > 0);
    // The root and the first hop must match — this is what the loop
    // back-edge would re-read in real code.
    std::string loadDst = firstDest(vsm, "ilw");
    REQUIRE(loadDst.length() > 0);
    CHECK(first.dst == loadDst);
    CHECK(first.dst == first.src1);
}

TEST_CASE("Two-address: predecessor reg occupied by literal-write -> fall through")
{
    // If the coalescer's preferred register is unavailable because some
    // other live alias already owns it, the allocator must fall back to
    // any free register — NOT crash, NOT pick a conflicting register.
    //
    // We construct this by mixing a counter loop with an unrelated load
    // into VI01 (forced by referencing vi01 explicitly) so the literal
    // VI01 write would conflict with a coalesced counter.  The test
    // only asserts that emission succeeds and the isubiu is still
    // self-consistent (dst == src1) on whichever reg the fallback
    // picks.
    const std::string body =
        "\tilw.x counter, 0(vi00)\n"
        "loop:\n"
        "\tisubiu counter, counter, 1\n"
        "\tiaddiu vi01, vi00, 5\n"
        "\tibgtz counter, loop\n";

    std::string vsm = runEmit(body, "vsmTwoAddrFallback");
    REQUIRE(vsm.length() > 0);

    Operands sub = parseNth(vsm, "isubiu", 0);
    REQUIRE(sub.dst.length() > 0);
    REQUIRE(sub.src1.length() > 0);
    CHECK(sub.dst == sub.src1);
}

TEST_CASE("Two-address: nested branches don't trigger releaseAlias-after-free")
{
    // Branch state cloning historically caused a use-after-free when an
    // alias holding a `sameNamePredecessor` pointer was released while
    // its successor was still live.  The fix clears dangling predecessor
    // edges inside releaseAlias.  This test exercises that path by
    // building a branchy program with multiple writes to one name; the
    // assertion is simply that openvcl exits cleanly and emits VSM,
    // i.e. doesn't segfault.
    const std::string body =
        "\tilw.x counter, 0(vi00)\n"
        "\tilw.y flag, 0(vi00)\n"
        "outerloop:\n"
        "\tibgtz flag, skip\n"
        "\tisubiu counter, counter, 1\n"
        "skip:\n"
        "\tisubiu counter, counter, 1\n"
        "\tibgtz counter, outerloop\n";

    std::string vsm = runEmit(body, "vsmTwoAddrBranchy");
    REQUIRE(vsm.length() > 0);

    Operands sub = parseNth(vsm, "isubiu", 0);
    REQUIRE(sub.dst.length() > 0);
    REQUIRE(sub.src1.length() > 0);
    CHECK(sub.dst == sub.src1);
}
