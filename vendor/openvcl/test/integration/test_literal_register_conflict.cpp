// Regression tests: the allocator must not assign an alias to a literal
// (hardware-named) register that is explicitly written by an instruction
// whose lifetime overlaps the alias.
//
// The motivating bug:  ps2gl's general_quad.vcl declares an alias
// `do_clipping` loaded via `ilw.w do_clipping, ...`, then later in the
// per-vertex loop runs `fcand vi01, 0x0ffffff` (writes literal vi01)
// followed by `iand vi01, vi01, do_clipping`.  Openvcl's allocator was
// only considering alias-vs-alias range overlap, ignoring the literal
// vi01 write by fcand.  It happily put `do_clipping` in VI01, so fcand
// silently destroyed `do_clipping`, the iand collapsed to vi01 & vi01
// (a no-op), and clipping-disabled couldn't actually disable clipping.

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

    // Find the destination register named after `mnemonic` in the FIRST
    // emitted line that contains `mnemonic`.  Lines look like:
    //   "    ilw.w VI01, 76(VI00)" or
    //   "    iand  VI03, VI01, VI03  nop"
    // Returns "" if not found.
    std::string destReg(const std::string& vsm, const std::string& mnemonic)
    {
        std::string::size_type pos = vsm.find(mnemonic);
        if( pos == std::string::npos )
            return std::string();
        // Skip past mnemonic + any field suffix (.w / .xyz / etc.) + whitespace.
        std::string::size_type i = pos + mnemonic.size();
        while( i < vsm.size() && (vsm[i] == '.' || isalnum((unsigned char)vsm[i])) )
            i++;
        while( i < vsm.size() && (vsm[i] == ' ' || vsm[i] == '\t') )
            i++;
        // Read register token until comma/space.
        std::string::size_type start = i;
        while( i < vsm.size() && vsm[i] != ',' && vsm[i] != ' ' && vsm[i] != '\t' && vsm[i] != '\n' )
            i++;
        return vsm.substr(start, i - start);
    }
}

TEST_CASE("Allocator: alias live over fcand must not be assigned VI01")
{
    // do_clipping is loaded BEFORE fcand and read AFTER fcand.  fcand
    // writes literal VI01, so any alias spanning the fcand cannot live
    // in VI01.  Without this protection, openvcl would pick VI01 for
    // do_clipping because VI01 is the lowest-numbered free integer
    // register and no OTHER alias is competing for it.
    const std::string body =
        "\tilw.w do_clipping, 76(vi00)\n"
        "\tfcand vi01, 0x0ffffff\n"
        "\tiand vi01, vi01, do_clipping\n";

    std::string vsm = runEmit(body, "vsmAllocatorVI01");
    REQUIRE(vsm.length() > 0);

    // ilw.w writes do_clipping.  Whatever physical reg that's allocated
    // to had better NOT be VI01.
    std::string dest = destReg(vsm, "ilw");
    REQUIRE(dest.length() > 0);
    CHECK(dest != "VI01");
}

TEST_CASE("Allocator: alias live over fmand must not be assigned VI01")
{
    // fmand has the same VI01 destination constraint as fcand on VU1.
    // The hazard is identical: an alias whose live range crosses fmand
    // cannot share VI01 with it.
    const std::string body =
        "\tilw.w my_mask, 76(vi00)\n"
        "\tfmand vi01, vi00\n"
        "\tiand vi01, vi01, my_mask\n";

    std::string vsm = runEmit(body, "vsmAllocatorVI01Fmand");
    REQUIRE(vsm.length() > 0);

    std::string dest = destReg(vsm, "ilw");
    REQUIRE(dest.length() > 0);
    CHECK(dest != "VI01");
}

TEST_CASE("Allocator: alias live over fcget must not be assigned VI01")
{
    // fcget also targets VI01 specifically.
    const std::string body =
        "\tilw.w my_alias, 76(vi00)\n"
        "\tfcget vi01\n"
        "\tiand vi01, vi01, my_alias\n";

    std::string vsm = runEmit(body, "vsmAllocatorVI01Fcget");
    REQUIRE(vsm.length() > 0);

    std::string dest = destReg(vsm, "ilw");
    REQUIRE(dest.length() > 0);
    CHECK(dest != "VI01");
}
