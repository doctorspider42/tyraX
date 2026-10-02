// Regression tests for VCL continuation lifetime.
//
// Code before `--cont` runs once when the renderer is loaded; later `Mscnt`
// resumes after the [E] instruction.  Values initialized before `--cont` and
// read by the continuation must therefore keep their registers for the whole
// continuation loop, even after their last static read in one pass.

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

    std::string strip(const std::string& s)
    {
        std::string::size_type a = 0;
        while( a < s.size() && (s[a] == ' ' || s[a] == '\t') ) ++a;
        std::string::size_type b = s.size();
        while( b > a && (s[b - 1] == ' ' || s[b - 1] == '\t') ) --b;
        return s.substr(a, b - a);
    }

    std::string nthDest(const std::string& vsm, const std::string& mnemonic, int n)
    {
        std::string::size_type pos = 0;
        for( int i = 0; i <= n; ++i )
        {
            pos = vsm.find(mnemonic, pos);
            if( pos == std::string::npos )
                return std::string();
            if( i < n )
                pos += mnemonic.size();
        }

        std::string::size_type i = pos + mnemonic.size();
        while( i < vsm.size() && (vsm[i] == '.' || std::isalnum((unsigned char)vsm[i])) )
            ++i;
        std::string::size_type comma = vsm.find(',', i);
        std::string::size_type eol = vsm.find('\n', i);
        if( comma == std::string::npos || (eol != std::string::npos && comma > eol) )
            return std::string();
        return strip(vsm.substr(i, comma - i));
    }
}

TEST_CASE("Continuation: pre-cont live alias is not reused inside looping body")
{
    const std::string body =
        "\tlq keep, 0(vi00)\n"
        "\t--cont\n"
        "main_lid:\n"
        "\tadd.xyz sink0, keep, vf00\n"
        "\tlq temp, 1(vi00)\n"
        "\tadd.xyz sink1, temp, vf00\n"
        "\tb main_lid\n";

    std::string vsm = runEmit(body, "vsmContLive");
    REQUIRE(vsm.length() > 0);

    std::string keepReg = nthDest(vsm, "lq", 0);
    std::string tempReg = nthDest(vsm, "lq", 1);
    REQUIRE(keepReg.length() > 0);
    REQUIRE(tempReg.length() > 0);
    CHECK(keepReg != tempReg);
}

TEST_CASE("LoopCS: reused temporary register is not clobbered before its store")
{
    const std::string body =
        "\tiaddiu ptr, vi00, 0\n"
        "\tiaddiu last, vi00, 4\n"
        "loop_lid:\n"
        "\t--LoopCS 1,3\n"
        "\tlq.xyz first, 0(ptr)\n"
        "\tsq.xyz first, 0(ptr)\n"
        "\tlq.xyz second, 1(ptr)\n"
        "\tsq.xyz second, 1(ptr)\n"
        "\tiaddiu ptr, ptr, 1\n"
        "\tibne ptr, last, loop_lid\n";

    std::string vsm = runEmit(body, "vsmLoopCsLifetime");
    REQUIRE(vsm.length() > 0);

    std::string firstReg = nthDest(vsm, "lq", 0);
    std::string secondReg = nthDest(vsm, "lq", 1);
    REQUIRE(firstReg.length() > 0);
    REQUIRE(secondReg.length() > 0);
    if( firstReg == secondReg )
    {
        // Loop-local values do not have to occupy registers for the entire loop.
        // Reusing one is correct when scheduling preserves the last read of the
        // first value before the second load overwrites it.
        std::string::size_type firstLoad = vsm.find("lq.xyz");
        std::string::size_type firstStore = vsm.find("sq.xyz", firstLoad);
        std::string::size_type secondLoad = vsm.find("lq.xyz", firstLoad + 1);
        REQUIRE(firstStore != std::string::npos);
        REQUIRE(secondLoad != std::string::npos);
        CHECK(firstStore < secondLoad);
    }
}
