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
    bool contains(const std::string& haystack, const std::string& needle)
    {
        return haystack.find(needle) != std::string::npos;
    }

    bool appearsBeforeAfter(const std::string& haystack,
                            const std::string& anchor,
                            const std::string& first,
                            const std::string& second)
    {
        const std::string::size_type anchorPos = haystack.find(anchor);
        if (anchorPos == std::string::npos)
            return false;
        const std::string::size_type firstPos = haystack.find(first, anchorPos);
        const std::string::size_type secondPos = haystack.find(second, anchorPos);
        return firstPos != std::string::npos
            && secondPos != std::string::npos
            && firstPos < secondPos;
    }

    int countSubstrings(const std::string& text, const std::string& pattern)
    {
        int count = 0;
        std::string::size_type pos = 0;
        while ((pos = text.find(pattern, pos)) != std::string::npos)
        {
            ++count;
            pos += pattern.size();
        }
        return count;
    }

    int jsonMetric(const std::string& report, const std::string& name)
    {
        std::string key = "\"" + name + "\": ";
        std::string::size_type pos = report.find(key);
        if (pos == std::string::npos)
            return -1;
        pos += key.size();
        return std::atoi(report.c_str() + pos);
    }

    int blockLineCount(const std::string& text, const std::string& label)
    {
        const std::string marker = label + ":\n";
        std::string::size_type begin = text.find(marker);
        if (begin == std::string::npos)
            return -1;
        begin += marker.size();

        int count = 0;
        std::string::size_type lineBegin = begin;
        while (lineBegin < text.size())
        {
            std::string::size_type lineEnd = text.find('\n', lineBegin);
            if (lineEnd == std::string::npos)
                lineEnd = text.size();
            std::string line = text.substr(lineBegin, lineEnd - lineBegin);
            if (!line.empty() && line[0] != ' ' && line[0] != '\t')
                break;
            if (!line.empty())
                ++count;
            if (lineEnd == text.size())
                break;
            lineBegin = lineEnd + 1;
        }
        return count;
    }

    bool blockContains(const std::string& text, const std::string& label, const std::string& needle)
    {
        const std::string marker = label + ":\n";
        std::string::size_type begin = text.find(marker);
        if (begin == std::string::npos)
            return false;
        begin += marker.size();

        std::string::size_type lineBegin = begin;
        while (lineBegin < text.size())
        {
            std::string::size_type lineEnd = text.find('\n', lineBegin);
            if (lineEnd == std::string::npos)
                lineEnd = text.size();
            std::string line = text.substr(lineBegin, lineEnd - lineBegin);
            if (!line.empty() && line[0] != ' ' && line[0] != '\t')
                break;
            if (line.find(needle) != std::string::npos)
                return true;
            if (lineEnd == text.size())
                break;
            lineBegin = lineEnd + 1;
        }
        return false;
    }

    bool nextInstructionLineContains(const std::string& text,
                                     const std::string& anchor,
                                     const std::string& needle)
    {
        std::string::size_type anchorPos = text.find(anchor);
        if (anchorPos == std::string::npos)
            return false;
        std::string::size_type lineEnd = text.find('\n', anchorPos);
        if (lineEnd == std::string::npos)
            return false;

        std::string::size_type lineBegin = lineEnd + 1;
        while (lineBegin < text.size())
        {
            lineEnd = text.find('\n', lineBegin);
            if (lineEnd == std::string::npos)
                lineEnd = text.size();
            std::string line = text.substr(lineBegin, lineEnd - lineBegin);
            if (!line.empty() && line[0] != ';')
                return line.find(needle) != std::string::npos;
            if (lineEnd == text.size())
                break;
            lineBegin = lineEnd + 1;
        }

        return false;
    }

    std::string runEmitWithExtraArgs(const std::string& source, const std::vector<std::string>& extraArgs)
    {
        char tmpl[] = "/tmp/openvcl_pipe_XXXXXX.vsm";
        int fd = mkstemps(tmpl, 4);
        if (fd < 0)
            return std::string();
        close(fd);

        std::vector<std::string> args;
        for (std::vector<std::string>::const_iterator i = extraArgs.begin(); i != extraArgs.end(); ++i)
            args.push_back(*i);
        args.push_back("-o");
        args.push_back(tmpl);
        ::test::RunResult r = ::test::run_openvcl(args, source);
        if (r.exit_code != 0)
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

    std::string runEmit(const std::string& source)
    {
        return runEmitWithExtraArgs(source, std::vector<std::string>());
    }

    std::string runOptimizedEmit(const std::string& source)
    {
        std::vector<std::string> args;
        args.push_back("--enable-known-loop-optimizations");
        return runEmitWithExtraArgs(source, args);
    }

    ::test::RunResult runCostJsonWithLoop(const std::string& vsm, const std::string& loop)
    {
        std::vector<std::string> args;
        args.push_back("--cost-json");
        args.push_back("--cost-loop");
        args.push_back(loop);
        return ::test::run_openvcl(args, vsm);
    }

    void expectGenericPathCompiles(const std::string& source,
                                   const std::string& loopLabel)
    {
        std::string vsm = runEmit(source);
        REQUIRE(vsm.length() > 0);

        CHECK(contains(vsm, loopLabel + ":"));
        CHECK(!contains(vsm, loopLabel + "__MAIN_LOOP:"));
    }

    std::string ps2glNamedXformLoopSource(const std::string& name)
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t.name " + name + "\n"
            "\t--enter\n"
            "\t--endenter\n"
            "\tiaddiu vi03, vi00, 5\n"
            "\tiaddiu vi05, vi00, 17\n"
            "xform_loop_lid:\n"
            "\tnop\n"
            "\tiaddiu vi03, vi03, 3\n"
            "\tibne vi03, vi05, xform_loop_lid\n"
            "done_lid:\n"
            "\t--exit\n"
            "\t--endexit\n";
    }

    std::string simpleGenericSingleQPipelineSource()
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t--enter\n"
            "\t--endenter\n"
            "\tiaddiu vi01, vi00, 0\n"
            "\tiaddiu vi02, vi00, 3\n"
            "loop_lid:\n"
            "\t--LoopCS 1,1\n"
            "\tdiv q, vf00w, vf00w\n"
            "\tmulq.xyz vf02, vf00, q\n"
            "\tadd.xyz vf10, vf00, vf00\n"
            "\tadd.xyz vf11, vf00, vf00\n"
            "\tadd.xyz vf12, vf00, vf00\n"
            "\tadd.xyz vf13, vf00, vf00\n"
            "\tadd.xyz vf14, vf00, vf00\n"
            "\tadd.xyz vf15, vf00, vf00\n"
            "\tiaddiu vi01, vi01, 1\n"
            "\tibne vi01, vi02, loop_lid\n"
            "\t--exit\n"
            "\t--endexit\n";
    }

    std::string genericMemoryPrefetchPipelineSource()
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t--enter\n"
            "\t--endenter\n"
            "\tiaddiu vi01, vi00, 0\n"
            "\tiaddiu vi02, vi00, 3\n"
            "\tmove.xyzw vf03, vf00\n"
            "loop_lid:\n"
            "\t--LoopCS 1,1\n"
            "\tlq.xyzw vf01, 0(vi01)\n"
            "\tdiv q, vf00w, vf01w\n"
            "\tmulq.xyz vf02, vf03, q\n"
            "\tadd.xyz vf10, vf00, vf00\n"
            "\tadd.xyz vf11, vf00, vf00\n"
            "\tadd.xyz vf12, vf00, vf00\n"
            "\tadd.xyz vf13, vf00, vf00\n"
            "\tadd.xyz vf14, vf00, vf00\n"
            "\tadd.xyz vf15, vf00, vf00\n"
            "\tiaddiu vi01, vi01, 1\n"
            "\tibne vi01, vi02, loop_lid\n"
            "\t--exit\n"
            "\t--endexit\n";
    }

    std::string genericQDrainPipelineSource()
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t--enter\n"
            "\t--endenter\n"
            "\tiaddiu vi01, vi00, 0\n"
            "\tiaddiu vi02, vi00, 3\n"
            "\tmove.xyzw vf03, vf00\n"
            "\tmove.xyzw vf05, vf00\n"
            "loop_lid:\n"
            "\t--LoopCS 1,1\n"
            "\tdiv q, vf00w, vf00w\n"
            "\tmulq.xyz vf02, vf03, q\n"
            "\tadd.xyz vf10, vf00, vf00\n"
            "\tadd.xyz vf11, vf00, vf00\n"
            "\tadd.xyz vf12, vf00, vf00\n"
            "\tadd.xyz vf13, vf00, vf00\n"
            "\tadd.xyz vf14, vf00, vf00\n"
            "\tadd.xyz vf15, vf00, vf00\n"
            "\tiaddiu vi01, vi01, 1\n"
            "\tibne vi01, vi02, loop_lid\n"
            "after_lid:\n"
            "\tmulq.xyz vf04, vf05, q\n"
            "\t--exit\n"
            "\t--endexit\n";
    }

    std::string genericRotationPipelineSource()
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t--enter\n"
            "\t--endenter\n"
            "\tiaddiu vi01, vi00, 0\n"
            "\tiaddiu vi02, vi00, 0\n"
            "\tiaddiu vi04, vi00, 3\n"
            "\tmove.xyzw vf02, vf00\n"
            "loop_lid:\n"
            "\t--LoopCS 1,1\n"
            "\tlq.xyzw vf01, 0(vi01)\n"
            "\tmul.xyzw vf03, vf01, vf02\n"
            "\tdiv q, vf00w, vf03w\n"
            "\tmulq.xyz vf03, vf03, q\n"
            "\tsq.xyz vf03, 0(vi02)\n"
            "\tadd.xyz vf10, vf00, vf00\n"
            "\tadd.xyz vf11, vf00, vf00\n"
            "\tadd.xyz vf12, vf00, vf00\n"
            "\tadd.xyz vf13, vf00, vf00\n"
            "\tadd.xyz vf14, vf00, vf00\n"
            "\tiaddiu vi01, vi01, 1\n"
            "\tiaddiu vi02, vi02, 1\n"
            "\tibne vi01, vi04, loop_lid\n"
            "\t--exit\n"
            "\t--endexit\n";
    }

    std::string genericSuffixStoreDrainPipelineSource()
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t--enter\n"
            "\t--endenter\n"
            "\tiaddiu vi01, vi00, 0\n"
            "\tiaddiu vi02, vi00, 0\n"
            "\tiaddiu vi04, vi00, 3\n"
            "\tmove.xyzw vf02, vf00\n"
            "loop_lid:\n"
            "\t--LoopCS 1,1\n"
            "\tlq.xyzw vf01, 0(vi01)\n"
            "\tdiv q, vf00w, vf01w\n"
            "\tmulq.xyz vf03, vf02, q\n"
            "\tsq.xyz vf03, 0(vi02)\n"
            "\tadd.xyz vf10, vf00, vf00\n"
            "\tadd.xyz vf11, vf00, vf00\n"
            "\tadd.xyz vf12, vf00, vf00\n"
            "\tadd.xyz vf13, vf00, vf00\n"
            "\tadd.xyz vf14, vf00, vf00\n"
            "\tiaddiu vi01, vi01, 1\n"
            "\tiaddiu vi02, vi02, 1\n"
            "\tibne vi01, vi04, loop_lid\n"
            "\t--exit\n"
            "\t--endexit\n";
    }

    std::string genericMultiQPipelineSource()
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t--enter\n"
            "\t--endenter\n"
            "\tiaddiu vi01, vi00, 0\n"
            "\tiaddiu vi02, vi00, 3\n"
            "loop_lid:\n"
            "\t--LoopCS 1,1\n"
            "\tdiv q, vf00w, vf00w\n"
            "\tmulq.xyz vf02, vf00, q\n"
            "\tadd.xyz vf10, vf00, vf00\n"
            "\tadd.xyz vf11, vf00, vf00\n"
            "\tdiv q, vf00w, vf00w\n"
            "\tadd.xyz vf12, vf00, vf00\n"
            "\tadd.xyz vf13, vf00, vf00\n"
            "\tmulq.xyz vf05, vf00, q\n"
            "\tadd.xyz vf14, vf00, vf00\n"
            "\tiaddiu vi01, vi01, 1\n"
            "\tibne vi01, vi02, loop_lid\n"
            "\t--exit\n"
            "\t--endexit\n";
    }

    std::string genericMultiQProducerPrefixPipelineSource()
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t--enter\n"
            "\t--endenter\n"
            "\tiaddiu vi01, vi00, 0\n"
            "\tiaddiu vi02, vi00, 3\n"
            "loop_lid:\n"
            "\t--LoopCS 1,1\n"
            "\tdiv q, vf00w, vf00w\n"
            "\tadd.xyz vf10, vf00, vf00\n"
            "\tadd.xyz vf11, vf00, vf00\n"
            "\tadd.xyz vf12, vf00, vf00\n"
            "\tadd.xyz vf13, vf00, vf00\n"
            "\tadd.xyz vf14, vf00, vf00\n"
            "\tadd.xyz vf15, vf00, vf00\n"
            "\tadd.xyz vf16, vf00, vf00\n"
            "\tmulq.xyz vf02, vf00, q\n"
            "\tdiv q, vf00w, vf00w\n"
            "\tadd.xyz vf17, vf00, vf00\n"
            "\tadd.xyz vf18, vf00, vf00\n"
            "\tmulq.xyz vf05, vf00, q\n"
            "\tadd.xyz vf19, vf00, vf00\n"
            "\tiaddiu vi01, vi01, 1\n"
            "\tibne vi01, vi02, loop_lid\n"
            "\t--exit\n"
            "\t--endexit\n";
    }

    std::string genericMultiQSuffixStoreDrainPipelineSource()
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t--enter\n"
            "\t--endenter\n"
            "\tiaddiu vi01, vi00, 0\n"
            "\tiaddiu vi02, vi00, 3\n"
            "\tiaddiu vi03, vi00, 0\n"
            "loop_lid:\n"
            "\t--LoopCS 1,1\n"
            "\tdiv q, vf00w, vf00w\n"
            "\tmulq.xyz vf02, vf00, q\n"
            "\tadd.xyz vf10, vf00, vf00\n"
            "\tadd.xyz vf11, vf00, vf00\n"
            "\tdiv q, vf00w, vf00w\n"
            "\tadd.xyz vf12, vf00, vf00\n"
            "\tadd.xyz vf13, vf00, vf00\n"
            "\tmulq.xyz vf05, vf00, q\n"
            "\tsq.xyz vf05, 0(vi03)\n"
            "\tadd.xyz vf14, vf00, vf00\n"
            "\tiaddiu vi03, vi03, 1\n"
            "\tiaddiu vi03, vi03, 1\n"
            "\tiaddiu vi01, vi01, 1\n"
            "\tibne vi01, vi02, loop_lid\n"
            "\t--exit\n"
            "\t--endexit\n";
    }

    std::string genericLoadedMultiQSuffixStoreDrainPipelineSource()
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t--enter\n"
            "\t--endenter\n"
            "\tiaddiu vi01, vi00, 0\n"
            "\tiaddiu vi02, vi00, 3\n"
            "\tiaddiu vi03, vi00, 0\n"
            "\tiaddiu vi04, vi00, 0\n"
            "loop_lid:\n"
            "\t--LoopCS 1,1\n"
            "\tdiv q, vf00w, vf00w\n"
            "\tmulq.xyz vf02, vf00, q\n"
            "\tadd.xyz vf10, vf00, vf00\n"
            "\tadd.xyz vf11, vf00, vf00\n"
            "\tdiv q, vf00w, vf00w\n"
            "\tadd.xyz vf12, vf00, vf00\n"
            "\tadd.xyz vf13, vf00, vf00\n"
            "\tmulq.xyz vf05, vf00, q\n"
            "\tsq.xyz vf05, 0(vi03)\n"
            "\tlq.xyz vf24, 0(vi04)\n"
            "\tadd.xyz vf05, vf24, vf00\n"
            "\tiaddiu vi04, vi04, 1\n"
            "\tiaddiu vi03, vi03, 1\n"
            "\tiaddiu vi01, vi01, 1\n"
            "\tibne vi01, vi02, loop_lid\n"
            "\t--exit\n"
            "\t--endexit\n";
    }

    std::string genericMultiQGuardedStorePipelineSource()
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t--enter\n"
            "\t--endenter\n"
            "\tiaddiu vi01, vi00, 0\n"
            "\tiaddiu vi02, vi00, 3\n"
            "\tiaddiu vi03, vi00, 0\n"
            "loop_lid:\n"
            "\t--LoopCS 1,1\n"
            "\tadd.xyz vf02, vf00, vf00\n"
            "\tsq.xyz vf02, 0(vi03)\n"
            "\tdiv q, vf00w, vf00w\n"
            "\tmulq.xyz vf03, vf00, q\n"
            "\tadd.xyz vf20, vf00, vf00\n"
            "\tadd.xyz vf21, vf00, vf00\n"
            "\tdiv q, vf00w, vf00w\n"
            "\tmulq.xyz vf06, vf00, q\n"
            "\tadd.xyz vf22, vf00, vf00\n"
            "\tiaddiu vi01, vi01, 1\n"
            "\tiaddiu vi03, vi03, 1\n"
            "\tibne vi01, vi02, loop_lid\n"
            "\t--exit\n"
            "\t--endexit\n";
    }

    std::string fastNoLightsPipelineSource()
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t.name vsmFastNoLights\n"
            "\t--enter\n"
            "\t--endenter\n"
            "\tiaddiu vi01, vi00, 0\n"
            "\tiaddiu vi02, vi00, 0\n"
            "\tiaddiu vi03, vi00, 0\n"
            "\tiaddiu vi05, vi00, 9\n"
            "\tmove.xyzw vf01, vf00\n"
            "\tmove.xyzw vf02, vf00\n"
            "\tmove.xyzw vf03, vf00\n"
            "\tmove.xyzw vf04, vf00\n"
            "\tmove.xyzw vf05, vf00\n"
            "\tmove.xyzw vf06, vf00\n"
            "\tmove.xyzw vf07, vf00\n"
            "\tmove.xyzw vf08, vf00\n"
            "\tmove.xyzw vf09, vf00\n"
            "\tmove.xyzw vf10, vf00\n"
            "xform_loop_lid:\n"
            "\t--LoopCS 1,3\n"
            "\tlq.xyz vf04, 0(vi02)\n"
            "\tmulax acc, vf05, vf04\n"
            "\tmadday acc, vf01, vf04\n"
            "\tmaddaz acc, vf02, vf04\n"
            "\tmaddw vf07, vf03, vf00\n"
            "\tdiv q, vf00w, vf07w\n"
            "\tmulq.xyz vf07, vf07, q\n"
            "\tftoi4.xyz vf08, vf07\n"
            "\tilw.w vi04, 0(vi02)\n"
            "\tiaddiu vi06, vi04, 0x7fff\n"
            "\tmfir.w vf08, vi06\n"
            "\tsq vf08, 2(vi03)\n"
            "\tsq vf06, 1(vi03)\n"
            "\tlq.xyz vf09, 2(vi02)\n"
            "\tmulq.xyz vf10, vf09, q\n"
            "\tsq.xyz vf10, 0(vi03)\n"
            "\tiaddiu vi02, vi02, 3\n"
            "\tiaddiu vi03, vi03, 3\n"
            "\tibne vi02, vi05, xform_loop_lid\n"
            "done_lid:\n"
            "\txgkick vi01\n"
            "\t--exit\n"
            "\t--endexit\n";
    }

    std::string fastLitPipelineSource()
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t.name vsmFast\n"
            "\t--enter\n"
            "\t--endenter\n"
            "\tiaddiu vi01, vi00, 0\n"
            "\tiaddiu vi02, vi00, 0\n"
            "\tiaddiu vi03, vi00, 0\n"
            "\tiaddiu vi05, vi00, 9\n"
            "\tmove.xyzw vf01, vf00\n"
            "\tmove.xyzw vf02, vf00\n"
            "\tmove.xyzw vf03, vf00\n"
            "\tmove.xyzw vf04, vf00\n"
            "\tmove.xyzw vf05, vf00\n"
            "\tmove.xyzw vf06, vf00\n"
            "\tmove.xyzw vf07, vf00\n"
            "\tmove.xyzw vf08, vf00\n"
            "\tmove.xyzw vf09, vf00\n"
            "\tmove.xyzw vf10, vf00\n"
            "\tmove.xyzw vf11, vf00\n"
            "\tmove.xyzw vf12, vf00\n"
            "\tmove.xyzw vf13, vf00\n"
            "\tmove.xyzw vf14, vf00\n"
            "\tmove.xyzw vf15, vf00\n"
            "\tmove.xyzw vf16, vf00\n"
            "\tmove.xyzw vf17, vf00\n"
            "\tmove.xyzw vf18, vf00\n"
            "\tmove.xyzw vf19, vf00\n"
            "\tmove.xyzw vf20, vf00\n"
            "\tmove.xyzw vf21, vf00\n"
            "xform_loop_lid:\n"
            "\t--LoopCS 1,3\n"
            "\tlq.xyz vf04, 0(vi02)\n"
            "\tlq.xyz vf10, 1(vi02)\n"
            "\tilw.w vi04, 0(vi02)\n"
            "\tlq.xyz vf20, 2(vi02)\n"
            "\tiaddiu vi02, vi02, 3\n"
            "\tmulax acc, vf18, vf04\n"
            "\tmadday acc, vf01, vf04\n"
            "\tmaddaz acc, vf02, vf04\n"
            "\tmaddw vf08, vf03, vf00\n"
            "\tmulax.xyz acc, vf11, vf10\n"
            "\tmadday.xyz acc, vf14, vf10\n"
            "\tmaddz.xyz vf13, vf16, vf10\n"
            "\tdiv q, vf00w, vf08w\n"
            "\tmax.xyz vf19, vf13, vf00\n"
            "\tmulax.xyz acc, vf12, vf19\n"
            "\tmadday.xyz acc, vf15, vf19\n"
            "\tmaddz.xyz vf05, vf17, vf19\n"
            "\tmulq.xyz vf08, vf08, q\n"
            "\tmulq.xyz vf21, vf20, q\n"
            "\tadd.xyz vf05, vf05, vf07\n"
            "\tftoi4.xyz vf09, vf08\n"
            "\tsq.xyz vf21, 0(vi03)\n"
            "\tminiw.xyz vf05, vf05, vf06w\n"
            "\tiaddiu vi06, vi04, 0x7fff\n"
            "\tmfir.w vf09, vi06\n"
            "\tsq vf09, 2(vi03)\n"
            "\tsq vf05, 1(vi03)\n"
            "\tiaddiu vi03, vi03, 3\n"
            "\tibne vi02, vi05, xform_loop_lid\n"
            "done_lid:\n"
            "\txgkick vi01\n"
            "\t--exit\n"
            "\t--endexit\n";
    }

    std::string sceiPipelineSource()
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t.name vsmSCEI\n"
            "\t--enter\n"
            "\t--endenter\n"
            "\tiaddiu vi01, vi00, 0\n"
            "\tiaddiu vi03, vi00, 0\n"
            "\tiaddiu vi04, vi00, 0\n"
            "\tiaddiu vi05, vi00, 9\n"
            "\tmove.xyzw vf01, vf00\n"
            "\tmove.xyzw vf02, vf00\n"
            "\tmove.xyzw vf03, vf00\n"
            "\tmove.xyzw vf04, vf00\n"
            "\tmove.xyzw vf05, vf00\n"
            "\tmove.xyzw vf06, vf00\n"
            "\tmove.xyzw vf07, vf00\n"
            "\tmove.xyzw vf08, vf00\n"
            "\tmove.xyzw vf09, vf00\n"
            "\tmove.xyzw vf10, vf00\n"
            "\tmove.xyzw vf11, vf00\n"
            "\tmove.xyzw vf12, vf00\n"
            "\tmove.xyzw vf13, vf00\n"
            "\tmove.xyzw vf14, vf00\n"
            "\tmove.xyzw vf15, vf00\n"
            "\tmove.xyzw vf16, vf00\n"
            "\tmove.xyzw vf17, vf00\n"
            "\tmove.xyzw vf18, vf00\n"
            "\tmove.xyzw vf19, vf00\n"
            "\tmove.xyzw vf20, vf00\n"
            "\tmove.xyzw vf21, vf00\n"
            "\tmove.xyzw vf22, vf00\n"
            "\tmove.xyzw vf23, vf00\n"
            "xform_loop_lid:\n"
            "\t--LoopCS 1,3\n"
            "\tlq.xyz vf09, 0(vi03)\n"
            "\tlq.xyz vf19, 1(vi03)\n"
            "\tilw.w vi06, 0(vi03)\n"
            "\tlq.xyz vf22, 2(vi03)\n"
            "\tiaddiu vi03, vi03, 3\n"
            "\tmulax acc, vf01, vf09\n"
            "\tmadday acc, vf02, vf09\n"
            "\tmaddaz acc, vf03, vf09\n"
            "\tmaddw vf10, vf04, vf00\n"
            "\tmulax.xyz acc, vf11, vf19\n"
            "\tmadday.xyz acc, vf14, vf19\n"
            "\tmaddz.xyz vf20, vf16, vf19\n"
            "\tdiv q, vf00w, vf10w\n"
            "\tmulq.xyz vf10, vf10, q\n"
            "\tmax.xyz vf21, vf20, vf00\n"
            "\tmulq.xyz vf23, vf22, q\n"
            "\tadd.xyz vf13, vf10, vf06\n"
            "\tmul.xyz vf18, vf10, vf08\n"
            "\tmulax.xyz acc, vf12, vf21\n"
            "\tmadday.xyz acc, vf15, vf21\n"
            "\tmaddz.xyz vf05, vf17, vf21\n"
            "\tftoi4.xyz vf13, vf13\n"
            "\tclipw.xyz vf18, vf08w\n"
            "\tadd.xyz vf05, vf05, vf07\n"
            "\tfcand vi01, 0x003ffff\n"
            "\tior vi07, vi01, vi06\n"
            "\tiaddiu vi07, vi07, 0x7fff\n"
            "\tmfir.w vf13, vi07\n"
            "\tminiw.xyz vf05, vf05, vf06w\n"
            "\tsq.xyz vf23, 0(vi04)\n"
            "\tsq vf13, 2(vi04)\n"
            "\tsq vf05, 1(vi04)\n"
            "\tiaddiu vi04, vi04, 3\n"
            "\tibne vi03, vi05, xform_loop_lid\n"
            "done_lid:\n"
            "\txgkick vi01\n"
            "\t--exit\n"
            "\t--endexit\n";
    }

    std::string finalColorPipelineSource()
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t.name vsmFinalColor\n"
            "\t--enter\n"
            "\t--endenter\n"
            "\tiaddiu vi02, vi00, 9\n"
            "\tiaddiu vi03, vi00, 0\n"
            "\tmove.xyzw vf08, vf00\n"
            "\tloi 255.0\n"
            "final_loop_lid:\n"
            "\t--LoopCS 1,3\n"
            "\t--LoopExtra 1\n"
            "\tlq.xyz vf08, 1(vi03)\n"
            "\tminii.xyz vf08, vf08, i\n"
            "\tftoi0.xyz vf08, vf08\n"
            "\tsq vf08, 1(vi03)\n"
            "\tiaddiu vi03, vi03, 3\n"
            "\tibne vi03, vi02, final_loop_lid\n"
            "done_lid:\n"
            "\t--exit\n"
            "\t--endexit\n";
    }

    std::string linearXformPipelineSource()
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t.name vsmGeneralLinear\n"
            "\t--enter\n"
            "\t--endenter\n"
            "\tiaddiu vi03, vi00, 0\n"
            "\tiaddiu vi04, vi00, 9\n"
            "\tiaddiu vi02, vi00, 0\n"
            "\tiaddiu vi05, vi00, 0\n"
            "\tiaddiu vi06, vi00, 0\n"
            "\tiaddiu vi13, vi00, 0\n"
            "\tmove.xyzw vf01, vf00\n"
            "\tmove.xyzw vf02, vf00\n"
            "\tmove.xyzw vf03, vf00\n"
            "\tmove.xyzw vf04, vf00\n"
            "\tmove.xyzw vf05, vf00\n"
            "\tmove.xyzw vf06, vf00\n"
            "\tmove.xyzw vf07, vf00\n"
            "\tmove.xyzw vf08, vf00\n"
            "\tmove.xyzw vf09, vf00\n"
            "\tmove.xyzw vf10, vf00\n"
            "\tmove.xyzw vf11, vf00\n"
            "\tmove.xyzw vf12, vf00\n"
            "\tmove.xyzw vf13, vf00\n"
            "\tmove.xyzw vf14, vf00\n"
            "\tmove.xyzw vf15, vf00\n"
            "\tmove.xyzw vf16, vf00\n"
            "\tmove.xyzw vf17, vf00\n"
            "\tmove.xyzw vf18, vf00\n"
            "xform_loop_lid:\n"
            "\t--LoopCS 1,3\n"
            "\tlq.xyz vf11, 0(vi13)\n"
            "\tmulax acc, vf01, vf11\n"
            "\tmadday acc, vf02, vf11\n"
            "\tmaddaz acc, vf03, vf11\n"
            "\tmaddw vf12, vf04, vf00\n"
            "\tdiv q, vf00w, vf12w\n"
            "\tmulq.xyz vf12, vf12, q\n"
            "\tadd.xyz vf13, vf12, vf05\n"
            "\tftoi4.xyz vf13, vf13\n"
            "\tilw.w vi07, 0(vi13)\n"
            "\tsub.xyz vf14, vf09, vf12\n"
            "\topmula.xyz acc, vf14, vf06\n"
            "\topmsub.xyz vf15, vf06, vf14\n"
            "\tfmand vi08, vi06\n"
            "\tisub vi08, vi08, vi05\n"
            "\tiand vi08, vi08, vi06\n"
            "\tisub vi05, vi06, vi05\n"
            "\tiand vi10, vi07, vi06\n"
            "\tior vi05, vi05, vi10\n"
            "\tmulw.xyz vf06, vf14, vf10w\n"
            "\tmulw.xyz vf09, vf12, vf00w\n"
            "\tmul.xyz vf16, vf12, vf07\n"
            "\tclipw.xyz vf16, vf07w\n"
            "\tfcand vi01, 0x003ffff\n"
            "\tiand vi01, vi01, vi02\n"
            "\tior vi09, vi01, vi08\n"
            "\tior vi09, vi09, vi07\n"
            "\tiaddiu vi09, vi09, 0x7fff\n"
            "\tmfir.w vf13, vi09\n"
            "\tsq vf13, 2(vi03)\n"
            "\tsq.xyz vf08, 1(vi03)\n"
            "\tlq.xyz vf17, 2(vi13)\n"
            "\tmulq.xyz vf18, vf17, q\n"
            "\tsq.xyz vf18, 0(vi03)\n"
            "\tiaddiu vi13, vi13, 3\n"
            "\tiaddiu vi03, vi03, 3\n"
            "\tibne vi13, vi04, xform_loop_lid\n"
            "done_lid:\n"
            "\t--exit\n"
            "\t--endexit\n";
    }

    std::string dirLightNoSpecPipelineSource()
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t.name vsmDirLight\n"
            "\t--enter\n"
            "\t--endenter\n"
            "\tiaddiu vi03, vi00, 0\n"
            "\tiaddiu vi04, vi00, 15\n"
            "\tiaddiu vi13, vi00, 0\n"
            "\tmove.xyzw vf06, vf00\n"
            "\tmove.xyzw vf08, vf00\n"
            "\tmove.xyzw vf09, vf00\n"
            "\tmove.xyzw vf10, vf00\n"
            "\tmove.xyzw vf11, vf00\n"
            "\tmove.xyzw vf12, vf00\n"
            "\tmove.xyzw vf13, vf00\n"
            "\tmove.xyzw vf14, vf00\n"
            "\tmove.xyzw vf15, vf00\n"
            "\tmove.xyzw vf16, vf00\n"
            "\tmove.xyzw vf17, vf00\n"
            "\tmove.xyzw vf18, vf00\n"
            "dir_light_vert_loop_lid:\n"
            "\t--LoopCS 1,3\n"
            "\tlq.xyz vf12, 1(vi13)\n"
            "\tlq.xyz vf17, 1(vi03)\n"
            "\tmul.xyz vf14, vf13, vf12\n"
            "\tadday.z acc, vf14, vf14y\n"
            "\tmaddx.z vf14, vf09, vf14x\n"
            "\tmaxx.z vf14, vf14, vf00x\n"
            "\tmulz.xyz vf15, vf11, vf14z\n"
            "\tmula.xyz acc, vf15, vf08\n"
            "\tmadd.xyz vf16, vf10, vf06\n"
            "\tadd.xyz vf18, vf17, vf16\n"
            "\tsq.xyz vf18, 1(vi03)\n"
            "\tiaddiu vi13, vi13, 3\n"
            "\tibne vi13, vi04, dir_light_vert_loop_lid\n"
            "\tiaddiu vi03, vi03, 3\n"
            "done_lid:\n"
            "\t--exit\n"
            "\t--endexit\n";
    }

    std::string dirLightSpecPipelineSource()
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t.name vsmDirLightSpec\n"
            "\t--enter\n"
            "\t--endenter\n"
            "\tiaddiu vi03, vi00, 0\n"
            "\tiaddiu vi04, vi00, 15\n"
            "\tiaddiu vi13, vi00, 0\n"
            "\tmove.xyzw vf06, vf00\n"
            "\tmove.xyzw vf08, vf00\n"
            "\tmove.xyzw vf12, vf00\n"
            "\tmove.xyzw vf13, vf00\n"
            "\tmove.xyzw vf14, vf00\n"
            "\tmove.xyzw vf15, vf00\n"
            "\tmove.xyzw vf16, vf00\n"
            "\tmove.xyzw vf17, vf00\n"
            "\tmove.xyzw vf18, vf00\n"
            "\tmove.xyzw vf19, vf00\n"
            "\tmove.xyzw vf20, vf00\n"
            "\tmove.xyzw vf21, vf00\n"
            "\tmove.xyzw vf22, vf00\n"
            "\tmove.xyzw vf28, vf00\n"
            "\tmove.xyzw vf29, vf00\n"
            "\tmove.xyzw vf30, vf00\n"
            "\tmaxw.xyzw vf11, vf00, vf00w\n"
            "dir_light_vert_loop_lid:\n"
            "\t--LoopCS 1,3\n"
            "\tlq.xyz vf14, 1(vi13)\n"
            "\tmul.xyz vf17, vf16, vf14\n"
            "\tadday.z acc, vf17, vf17y\n"
            "\tmaddx.z vf17, vf11, vf17x\n"
            "\tmaxx.z vf17, vf17, vf00x\n"
            "\tmulz.xyz vf18, vf13, vf17z\n"
            "\tmula.xyz acc, vf18, vf08\n"
            "\tmul.xyz vf20, vf19, vf14\n"
            "\tmr32.xyw vf20, vf20\n"
            "\taddax.w acc, vf20, vf20x\n"
            "\tmaddy.w vf21, vf00, vf20y\n"
            "\tmaxx.w vf22, vf21, vf00x\n"
            "\tmul.w vf22, vf22, vf22\n"
            "\tmul.w vf22, vf22, vf22\n"
            "\tmul.w vf22, vf22, vf22\n"
            "\tmul.w vf22, vf22, vf22\n"
            "\tmul.w vf22, vf22, vf22\n"
            "\tmaddaw.xyz acc, vf15, vf22w\n"
            "\tmadd.xyz vf28, vf12, vf06\n"
            "\tlq.xyz vf29, 1(vi03)\n"
            "\tadd.xyz vf30, vf29, vf28\n"
            "\tsq.xyz vf30, 1(vi03)\n"
            "\tiaddiu vi13, vi13, 3\n"
            "\tiaddiu vi03, vi03, 3\n"
            "\tibne vi13, vi04, dir_light_vert_loop_lid\n"
            "done_lid:\n"
            "\t--exit\n"
            "\t--endexit\n";
    }

    std::string dirLightSpecIndexedPipelineSource()
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t.name vsmDirLightSpecIndexed\n"
            "\t--enter\n"
            "\t--endenter\n"
            "\tiaddiu vi04, vi00, 0\n"
            "\tiaddiu vi05, vi00, 0\n"
            "\tiaddiu vi06, vi00, 15\n"
            "\tmove.xyzw vf05, vf00\n"
            "\tmove.xyzw vf06, vf00\n"
            "\tmove.xyzw vf09, vf00\n"
            "\tmove.xyzw vf10, vf00\n"
            "\tmove.xyzw vf11, vf00\n"
            "\tmove.xyzw vf12, vf00\n"
            "\tmove.xyzw vf13, vf00\n"
            "\tmove.xyzw vf14, vf00\n"
            "\tmove.xyzw vf15, vf00\n"
            "\tmove.xyzw vf16, vf00\n"
            "\tmove.xyzw vf17, vf00\n"
            "\tmove.xyzw vf18, vf00\n"
            "\tmove.xyzw vf19, vf00\n"
            "\tmove.xyzw vf20, vf00\n"
            "\tmove.xyzw vf21, vf00\n"
            "\tmove.xyzw vf22, vf00\n"
            "\tmove.xyzw vf23, vf00\n"
            "\tmove.xyzw vf24, vf00\n"
            "\tmove.xyzw vf25, vf00\n"
            "\tmove.xyzw vf26, vf00\n"
            "\tmove.xyzw vf27, vf00\n"
            "\tmove.xyzw vf28, vf00\n"
            "dir_light_vert_loop_lid:\n"
            "\t--LoopCS 1,3\n"
            "\tlq.xyz vf12, 1(vi04)\n"
            "\tmul.xyz vf15, vf14, vf12\n"
            "\tadday.z acc, vf15, vf15y\n"
            "\tmaddx.z vf15, vf09, vf15x\n"
            "\tmaxx.z vf15, vf15, vf00x\n"
            "\tmulz.xyz vf16, vf11, vf15z\n"
            "\tmula.xyz acc, vf16, vf06\n"
            "\tmul.xyz vf18, vf17, vf12\n"
            "\tmr32.xyw vf18, vf18\n"
            "\taddax.w acc, vf18, vf18x\n"
            "\tmaddy.w vf19, vf00, vf18y\n"
            "\tmaxx.w vf20, vf19, vf00x\n"
            "\tmul.w vf21, vf20, vf20\n"
            "\tmul.w vf22, vf21, vf21\n"
            "\tmul.w vf23, vf22, vf22\n"
            "\tmul.w vf24, vf23, vf23\n"
            "\tmul.w vf25, vf24, vf24\n"
            "\tmaddaw.xyz acc, vf13, vf25w\n"
            "\tmadd.xyz vf26, vf10, vf05\n"
            "\tlq.xyz vf27, 0(vi05)\n"
            "\tadd.xyz vf28, vf27, vf26\n"
            "\tsqi.xyz vf28, (vi05++)\n"
            "\tiaddiu vi04, vi04, 3\n"
            "\tibne vi04, vi06, dir_light_vert_loop_lid\n"
            "done_lid:\n"
            "\t--exit\n"
            "\t--endexit\n";
    }

    std::string dirLightSpecPvDiffPipelineSource()
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t.name vsmDirLightSpecPvDiff\n"
            "\t--enter\n"
            "\t--endenter\n"
            "\tiaddiu vi03, vi00, 0\n"
            "\tiaddiu vi04, vi00, 20\n"
            "\tiaddiu vi13, vi00, 0\n"
            "\tmove.xyzw vf06, vf00\n"
            "\tmove.xyzw vf09, vf00\n"
            "\tmove.xyzw vf10, vf00\n"
            "\tmove.xyzw vf11, vf00\n"
            "\tmove.xyzw vf12, vf00\n"
            "\tmove.xyzw vf13, vf00\n"
            "\tmove.xyzw vf14, vf00\n"
            "\tmove.xyzw vf15, vf00\n"
            "\tmove.xyzw vf16, vf00\n"
            "\tmove.xyzw vf17, vf00\n"
            "\tmove.xyzw vf18, vf00\n"
            "\tmove.xyzw vf19, vf00\n"
            "\tmove.xyzw vf20, vf00\n"
            "\tmove.xyzw vf21, vf00\n"
            "\tmove.xyzw vf22, vf00\n"
            "\tmove.xyzw vf23, vf00\n"
            "\tmove.xyzw vf24, vf00\n"
            "\tmove.xyzw vf25, vf00\n"
            "\tmove.xyzw vf26, vf00\n"
            "\tmove.xyzw vf27, vf00\n"
            "\tmove.xyzw vf28, vf00\n"
            "\tmove.xyzw vf29, vf00\n"
            "\tmove.xyzw vf30, vf00\n"
            "\tmaxw.xyzw vf10, vf00, vf00w\n"
            "dir_light_vert_loop_lid:\n"
            "\t--LoopCS 1,3\n"
            "\tlq.xyz vf13, 1(vi13)\n"
            "\tlq.xyz vf16, 3(vi13)\n"
            "\tmul.xyz vf17, vf15, vf13\n"
            "\tadday.z acc, vf17, vf17y\n"
            "\tmaddx.z vf17, vf10, vf17x\n"
            "\tmaxx.z vf17, vf17, vf00x\n"
            "\tmulz.xyz vf19, vf12, vf17z\n"
            "\tmula.xyz acc, vf19, vf16\n"
            "\tmul.xyz vf20, vf18, vf13\n"
            "\tmr32.xyw vf20, vf20\n"
            "\taddax.w acc, vf20, vf20x\n"
            "\tmaddy.w vf21, vf00, vf20y\n"
            "\tmaxx.w vf22, vf21, vf00x\n"
            "\tmul.w vf23, vf22, vf22\n"
            "\tmul.w vf24, vf23, vf23\n"
            "\tmul.w vf25, vf24, vf24\n"
            "\tmul.w vf26, vf25, vf25\n"
            "\tmul.w vf27, vf26, vf26\n"
            "\tmaddaw.xyz acc, vf14, vf27w\n"
            "\tmadd.xyz vf28, vf11, vf06\n"
            "\tlq.xyz vf29, 1(vi03)\n"
            "\tadd.xyz vf30, vf29, vf28\n"
            "\tsq.xyz vf30, 1(vi03)\n"
            "\tiaddiu vi13, vi13, 4\n"
            "\tiaddiu vi03, vi03, 3\n"
            "\tibne vi13, vi04, dir_light_vert_loop_lid\n"
            "done_lid:\n"
            "\t--exit\n"
            "\t--endexit\n";
    }

    std::string ptLightNoSpecPipelineSource()
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t.name vsmPtLight\n"
            "\t--enter\n"
            "\t--endenter\n"
            "\tiaddiu vi03, vi00, 0\n"
            "\tiaddiu vi04, vi00, 15\n"
            "\tiaddiu vi13, vi00, 0\n"
            "\tmove.xyzw vf06, vf00\n"
            "\tmove.xyzw vf08, vf00\n"
            "\tmove.xyzw vf09, vf00\n"
            "\tmove.xyzw vf10, vf00\n"
            "\tmove.xyzw vf11, vf00\n"
            "\tmove.xyzw vf12, vf00\n"
            "\tmove.xyzw vf13, vf00\n"
            "\tmove.xyzw vf14, vf00\n"
            "\tmove.xyzw vf15, vf00\n"
            "\tmove.xyzw vf16, vf00\n"
            "\tmove.xyzw vf17, vf00\n"
            "\tmove.xyzw vf18, vf00\n"
            "\tmove.xyzw vf19, vf00\n"
            "\tmove.xyzw vf20, vf00\n"
            "\tmove.xyzw vf21, vf00\n"
            "\tmove.xyzw vf22, vf00\n"
            "\tmove.xyzw vf23, vf00\n"
            "\tmove.xyzw vf24, vf00\n"
            "\tmove.xyzw vf25, vf00\n"
            "\tmove.xyzw vf26, vf00\n"
            "\tmove.xyzw vf27, vf00\n"
            "pt_light_vert_loop_lid:\n"
            "\t--LoopCS 1,3\n"
            "\t--LoopExtra 5\n"
            "\tlq.xyz vf14, 1(vi13)\n"
            "\tlq.xyz vf15, 0(vi13)\n"
            "\tsub.xyz vf16, vf13, vf15\n"
            "\tmul.xyz vf18, vf16, vf16\n"
            "\tadday.z acc, vf18, vf18y\n"
            "\tmaddx.z vf18, vf09, vf18x\n"
            "\tsqrt q, vf18z\n"
            "\taddw.x vf18, vf00, vf00w\n"
            "\taddq.y vf18, vf00, q\n"
            "\tdiv q, vf00w, vf18y\n"
            "\tmulq.xyz vf17, vf16, q\n"
            "\tmul.xyz vf19, vf18, vf12\n"
            "\tmulax.w acc, vf00, vf19x\n"
            "\tmadday.w acc, vf00, vf19y\n"
            "\tmaddz.w vf18, vf00, vf19z\n"
            "\tmul.xyz vf20, vf17, vf14\n"
            "\tmulax.w acc, vf00, vf20x\n"
            "\tmadday.w acc, vf00, vf20y\n"
            "\tmaddz.w vf21, vf00, vf20z\n"
            "\tmaxx.w vf22, vf21, vf00x\n"
            "\tmulw.xyz vf23, vf11, vf22w\n"
            "\tmula.xyz acc, vf23, vf08\n"
            "\tmadd.xyz vf24, vf10, vf06\n"
            "\tdiv q, vf00w, vf18w\n"
            "\tmulq.xyz vf25, vf24, q\n"
            "\tlq.xyz vf26, 1(vi03)\n"
            "\tadd.xyz vf27, vf26, vf25\n"
            "\tsq.xyz vf27, 1(vi03)\n"
            "\tiaddiu vi13, vi13, 3\n"
            "\tiaddiu vi03, vi03, 3\n"
            "\tibne vi13, vi04, pt_light_vert_loop_lid\n"
            "done_lid:\n"
            "\t--exit\n"
            "\t--endexit\n";
    }

    std::string ptLightSpecPipelineSource()
    {
        return
            "\t.init_vf_all\n"
            "\t.init_vi_all\n"
            "\t.name vsmPtLightSpec\n"
            "\t--enter\n"
            "\t--endenter\n"
            "\tiaddiu vi03, vi00, 0\n"
            "\tiaddiu vi04, vi00, 15\n"
            "\tiaddiu vi13, vi00, 0\n"
            "\tmove.xyzw vf06, vf00\n"
            "\tmove.xyzw vf08, vf00\n"
            "\tmove.xyzw vf10, vf00\n"
            "\tmove.xyzw vf11, vf00\n"
            "\tmove.xyzw vf12, vf00\n"
            "\tmove.xyzw vf13, vf00\n"
            "\tmove.xyzw vf14, vf00\n"
            "\tmove.xyzw vf15, vf00\n"
            "\tmove.xyzw vf16, vf00\n"
            "\tmove.xyzw vf17, vf00\n"
            "\tmove.xyzw vf18, vf00\n"
            "\tmove.xyzw vf19, vf00\n"
            "\tmove.xyzw vf20, vf00\n"
            "\tmove.xyzw vf21, vf00\n"
            "\tmove.xyzw vf22, vf00\n"
            "\tmaxw.xyzw vf11, vf00, vf00w\n"
            "pt_light_vert_loop_lid:\n"
            "\t--LoopCS 1,3\n"
            "\t--LoopExtra 5\n"
            "\tlq.xyz vf17, 1(vi13)\n"
            "\tlq.xyz vf18, 0(vi13)\n"
            "\tsub.xyz vf20, vf16, vf18\n"
            "\tmul.xyz vf22, vf20, vf20\n"
            "\tadday.z acc, vf22, vf22y\n"
            "\tmaddx.z vf22, vf11, vf22x\n"
            "\tsqrt q, vf22z\n"
            "\taddw.x vf22, vf00, vf00w\n"
            "\taddq.y vf22, vf00, q\n"
            "\tdiv q, vf00w, vf22y\n"
            "\tmulq.xyz vf18, vf20, q\n"
            "\tmul.xyz vf20, vf22, vf14\n"
            "\tmulax.w acc, vf00, vf20x\n"
            "\tmadday.w acc, vf00, vf20y\n"
            "\tmaddz.w vf22, vf00, vf20z\n"
            "\tadd.xyz vf19, vf10, vf18\n"
            "\tmul.xyz vf20, vf18, vf17\n"
            "\tesadd p, vf19\n"
            "\tmulax.w acc, vf00, vf20x\n"
            "\tmadday.w acc, vf00, vf20y\n"
            "\tmaddz.w vf21, vf00, vf20z\n"
            "\tmfp.w vf19, p\n"
            "\tersqrt p, vf19w\n"
            "\tmaxx.w vf20, vf21, vf00x\n"
            "\tmfp.w vf19, p\n"
            "\tmulw.xyz vf21, vf13, vf20w\n"
            "\tmulw.xyz vf19, vf19, vf19w\n"
            "\tmula.xyz acc, vf21, vf08\n"
            "\tmul.xyz vf18, vf19, vf17\n"
            "\tmulax.w acc, vf00, vf18x\n"
            "\tmadday.w acc, vf00, vf18y\n"
            "\tmaddz.w vf17, vf00, vf18z\n"
            "\tmaxx.w vf18, vf17, vf00x\n"
            "\tmul.w vf17, vf18, vf18\n"
            "\tmul.w vf18, vf17, vf17\n"
            "\tmul.w vf17, vf18, vf18\n"
            "\tmul.w vf18, vf17, vf17\n"
            "\tmul.w vf17, vf18, vf18\n"
            "\tmaddaw.xyz acc, vf15, vf17w\n"
            "\tmadd.xyz vf17, vf12, vf06\n"
            "\tdiv q, vf00w, vf22w\n"
            "\tmulq.xyz vf18, vf17, q\n"
            "\tlq.xyz vf17, 1(vi03)\n"
            "\tadd.xyz vf20, vf17, vf18\n"
            "\tsq.xyz vf20, 1(vi03)\n"
            "\tiaddiu vi13, vi13, 3\n"
            "\tiaddiu vi03, vi03, 3\n"
            "\tibne vi13, vi04, pt_light_vert_loop_lid\n"
            "done_lid:\n"
            "\t--exit\n"
            "\t--endexit\n";
    }

    std::string ptLightSpecPvDiffPipelineSource()
    {
        std::string source = ptLightSpecPipelineSource();
        std::string::size_type pos = source.find("\tlq.xyz vf17, 1(vi13)\n");
        if (pos != std::string::npos)
            source.insert(pos + std::string("\tlq.xyz vf17, 1(vi13)\n").size(),
                          "\tlq.xyz vf08, 3(vi13)\n");
        while ((pos = source.find("\tiaddiu vi13, vi13, 3")) != std::string::npos)
            source.replace(pos, std::string("\tiaddiu vi13, vi13, 3").size(),
                           "\tiaddiu vi13, vi13, 4");
        pos = source.find("\tiaddiu vi04, vi00, 15");
        if (pos != std::string::npos)
            source.replace(pos, std::string("\tiaddiu vi04, vi00, 15").size(),
                           "\tiaddiu vi04, vi00, 20");
        return source;
    }
}

TEST_CASE("Software pipeline: fast_nolights transform loop emits a 12-cycle steady state")
{
    std::string vsm = runOptimizedEmit(fastNoLightsPipelineSource());
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "xform_loop_lid__ENTRY_POINT:"));
    CHECK(contains(vsm, "xform_loop_lid__PRO1:"));
    CHECK(contains(vsm, "xform_loop_lid__MAIN_LOOP:"));
    CHECK(contains(vsm, "xform_loop_lid__EPI0:"));
    CHECK(contains(vsm, "xform_loop_lid__EPI1:"));
    CHECK(contains(vsm, "ibne VI02, VI05, xform_loop_lid__MAIN_LOOP"));
    CHECK(contains(vsm, "mulq.xyz VF10, VF10, q          div q, VF00w, VF07w"));
    CHECK(countSubstrings(vsm, "waitq") == 0);
}

TEST_CASE("Software pipeline: generic path emits safe single-Q loop prologs")
{
    std::vector<std::string> args;
    args.push_back("--enable-generic-software-pipelining");
    std::string vsm = runEmitWithExtraArgs(simpleGenericSingleQPipelineSource(), args);
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "loop_lid__PROLOG:"));
    CHECK(contains(vsm, "loop_lid:"));
    CHECK(contains(vsm, "ibne VI01, VI02, loop_lid"));
    CHECK(countSubstrings(vsm, "div q, VF00w, VF00w") == 2);
    CHECK(!contains(vsm, "loop_lid__MAIN_LOOP:"));
}

TEST_CASE("Software pipeline: strict schedule slots preserve scheduled branch pairs and delay fillers")
{
    std::vector<std::string> args;
    args.push_back("--strict-schedule-slots");
    std::string vsm = runEmitWithExtraArgs(simpleGenericSingleQPipelineSource(), args);
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "mulq.xyz VF02, VF00, q          ibne VI01, VI02, loop_lid"));
    CHECK(contains(vsm, "nop                             div q, VF00w, VF00w"));
    CHECK(countSubstrings(vsm, "div q, VF00w, VF00w") == 2);
}

TEST_CASE("Software pipeline: generic default uses typed schedule-slot emission")
{
    std::vector<std::string> args;
    args.push_back("--strict-schedule-slots");

    std::string generic = runEmit(simpleGenericSingleQPipelineSource());
    std::string strict = runEmitWithExtraArgs(simpleGenericSingleQPipelineSource(), args);
    REQUIRE(generic.length() > 0);
    REQUIRE(strict.length() > 0);

    CHECK(generic == strict);
}

TEST_CASE("Software pipeline: generic path emits adjusted memory prefetches")
{
    std::vector<std::string> args;
    args.push_back("--enable-generic-software-pipelining");
    std::string vsm = runEmitWithExtraArgs(genericMemoryPrefetchPipelineSource(), args);
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "loop_lid__PROLOG:"));
    CHECK(contains(vsm, "loop_lid:"));
    CHECK(contains(vsm, "lq VF01, 0(VI01)"));
    CHECK(contains(vsm, "lq VF01, 1(VI01)"));
    CHECK(countSubstrings(vsm, "div q, VF00w, VF01w") == 2);
}

TEST_CASE("Software pipeline: generic path emits simple Q live-out drains")
{
    std::vector<std::string> args;
    args.push_back("--enable-generic-software-pipelining");
    std::string vsm = runEmitWithExtraArgs(genericQDrainPipelineSource(), args);
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "loop_lid__PROLOG:"));
    CHECK(contains(vsm, "loop_lid__DRAIN:"));
    CHECK(contains(vsm, "after_lid:"));
    CHECK(countSubstrings(vsm, "div q, VF00w, VF00w") == 3);
}

TEST_CASE("Software pipeline: generic path emits simple rotated register prefetches")
{
    std::vector<std::string> args;
    args.push_back("--enable-generic-software-pipelining");
    std::string vsm = runEmitWithExtraArgs(genericRotationPipelineSource(), args);
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "loop_lid__PROLOG:"));
    CHECK(contains(vsm, "mul.xyzw VF31, VF01, VF02"));
    CHECK(contains(vsm, "div q, VF00w, VF31w"));
    CHECK(contains(vsm, "move.xyz VF03, VF31")
          || contains(vsm, "max.xyz VF03, VF31, VF31"));
}

TEST_CASE("Software pipeline: generic path emits delayed suffix store drains")
{
    std::vector<std::string> args;
    args.push_back("--enable-generic-software-pipelining");
    std::string vsm = runEmitWithExtraArgs(genericSuffixStoreDrainPipelineSource(), args);
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "loop_lid__PROLOG:"));
    CHECK(contains(vsm, "loop_lid__DRAIN:"));
    CHECK(contains(vsm, "ibeq VI01, VI04, loop_lid__DRAIN"));
    CHECK(contains(vsm, "sq.xyz VF03, -1(VI02)"));
    CHECK(contains(vsm, "sq.xyz VF03, -2(VI02)"));
}

TEST_CASE("Software pipeline: generic final color suffix store stays inside the loop")
{
    std::string vsm = runEmit(finalColorPipelineSource());
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "final_loop_lid__PROLOG:"));
    CHECK(contains(vsm, "final_loop_lid:"));
    CHECK(appearsBeforeAfter(vsm,
                             "final_loop_lid__PROLOG:",
                             "ftoi0.xyz VF31, VF31",
                             "final_loop_lid:"));
    CHECK(countSubstrings(vsm, "sq VF08") == 1);
    CHECK(appearsBeforeAfter(vsm,
                             "final_loop_lid:",
                             "sq VF08",
                             "ibne VI03, VI02, final_loop_lid"));
}

TEST_CASE("Software pipeline: generic path emits multi-Q cyclic prefixes")
{
    std::vector<std::string> args;
    args.push_back("--enable-generic-software-pipelining");
    std::string vsm = runEmitWithExtraArgs(genericMultiQPipelineSource(), args);
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "loop_lid__PROLOG:"));
    CHECK(contains(vsm, "loop_lid:"));
    CHECK(contains(vsm, "ibne VI01, VI02, loop_lid"));
    CHECK(countSubstrings(vsm, "div q, VF00w, VF00w") == 3);
    CHECK(countSubstrings(vsm, "mulq.xyz VF02, VF00, q") == 1);
    CHECK(countSubstrings(vsm, "mulq.xyz VF05, VF00, q") == 1);
    CHECK(appearsBeforeAfter(vsm,
                             "loop_lid:",
                             "add.xyz VF14, VF00, VF00",
                             "mulq.xyz VF02, VF00, q"));
    CHECK(appearsBeforeAfter(vsm,
                             "loop_lid:",
                             "mulq.xyz VF02, VF00, q",
                             "ibne VI01, VI02, loop_lid"));
}

TEST_CASE("Software pipeline: generic path emits multi-Q producer-side prefixes")
{
    std::vector<std::string> args;
    args.push_back("--enable-generic-software-pipelining");
    std::string vsm = runEmitWithExtraArgs(genericMultiQProducerPrefixPipelineSource(), args);
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "loop_lid__PROLOG:"));
    CHECK(contains(vsm, "loop_lid:"));
    CHECK(contains(vsm, "ibne VI01, VI02, loop_lid"));
    CHECK(countSubstrings(vsm, "div q, VF00w, VF00w") == 3);
    CHECK(countSubstrings(vsm, "mulq.xyz VF02, VF00, q") == 1);
    CHECK(countSubstrings(vsm, "mulq.xyz VF05, VF00, q") == 1);
    CHECK(appearsBeforeAfter(vsm,
                             "loop_lid:",
                             "add.xyz VF19, VF00, VF00",
                             "add.xyz VF16, VF00, VF00"));
    CHECK(appearsBeforeAfter(vsm,
                             "loop_lid:",
                             "ibne VI01, VI02, loop_lid",
                             "add.xyz VF16, VF00, VF00"));
}

TEST_CASE("Software pipeline: generic multi-Q prefixes emit delayed suffix store drains")
{
    std::vector<std::string> args;
    args.push_back("--enable-generic-software-pipelining");
    std::string vsm = runEmitWithExtraArgs(genericMultiQSuffixStoreDrainPipelineSource(), args);
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "loop_lid__PROLOG:"));
    CHECK(contains(vsm, "loop_lid__DRAIN:"));
    CHECK(contains(vsm, "ibeq VI01, VI02, loop_lid__DRAIN"));
    CHECK(appearsBeforeAfter(vsm,
                             "loop_lid:",
                             "sq.xyz VF05, -4(VI03)",
                             "ibne VI01, VI02, loop_lid"));
    CHECK(blockContains(vsm, "loop_lid__DRAIN", "sq.xyz VF05, -2(VI03)"));
}

TEST_CASE("Software pipeline: loaded generic multi-Q drains prime the prolog branch boundary")
{
    std::vector<std::string> args;
    args.push_back("--enable-generic-software-pipelining");
    std::string vsm = runEmitWithExtraArgs(genericLoadedMultiQSuffixStoreDrainPipelineSource(), args);
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "loop_lid__PROLOG:"));
    CHECK(contains(vsm, "loop_lid__DRAIN:"));
    CHECK(contains(vsm, "ibeq VI01, VI02, loop_lid__DRAIN"));
    CHECK(nextInstructionLineContains(vsm,
                                      "ibeq VI01, VI02, loop_lid__DRAIN",
                                      "div q, VF00w, VF00w"));
    CHECK(appearsBeforeAfter(vsm,
                             "loop_lid:",
                             "sq.xyz",
                             "ibne VI01, VI02, loop_lid"));
    CHECK(blockContains(vsm, "loop_lid__DRAIN", "sq.xyz"));
}

TEST_CASE("Software pipeline: generic path guards cloned multi-Q store prefixes")
{
    std::vector<std::string> args;
    args.push_back("--enable-generic-software-pipelining");
    std::string vsm = runEmitWithExtraArgs(genericMultiQGuardedStorePipelineSource(), args);
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "loop_lid__PROLOG:"));
    CHECK(contains(vsm, "loop_lid__DRAIN:"));
    CHECK(contains(vsm, "ibeq VI01, VI02, loop_lid__DRAIN"));
    CHECK(appearsBeforeAfter(vsm,
                             "loop_lid:",
                             "ibeq VI01, VI02, loop_lid__DRAIN",
                             "sq.xyz VF31, 0(VI03)"));
    CHECK(appearsBeforeAfter(vsm,
                             "loop_lid:",
                             "sq.xyz VF31, 0(VI03)",
                             "ibne VI01, VI02, loop_lid"));
    CHECK(!blockContains(vsm, "loop_lid__DRAIN", "sq.xyz VF31, 0(VI03)"));
}

TEST_CASE("Software pipeline: generic software-pipelining is default and can be disabled")
{
    std::string vsm = runEmit(simpleGenericSingleQPipelineSource());
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "loop_lid__PROLOG:"));
    CHECK(contains(vsm, "loop_lid:"));
    CHECK(contains(vsm, "ibne VI01, VI02, loop_lid"));
    CHECK(countSubstrings(vsm, "div q, VF00w, VF00w") == 2);

    std::vector<std::string> args;
    args.push_back("--disable-generic-software-pipelining");
    vsm = runEmitWithExtraArgs(simpleGenericSingleQPipelineSource(), args);
    REQUIRE(vsm.length() > 0);

    CHECK(!contains(vsm, "loop_lid__PROLOG:"));
    CHECK(contains(vsm, "loop_lid:"));
    CHECK(contains(vsm, "ibne VI01, VI02, loop_lid"));
    CHECK(countSubstrings(vsm, "div q, VF00w, VF00w") == 1);
}

TEST_CASE("Software pipeline: generic compiler path is the default")
{
    expectGenericPathCompiles(fastNoLightsPipelineSource(), "xform_loop_lid");
    expectGenericPathCompiles(fastLitPipelineSource(), "xform_loop_lid");
    expectGenericPathCompiles(sceiPipelineSource(), "xform_loop_lid");
    expectGenericPathCompiles(finalColorPipelineSource(), "final_loop_lid");
    expectGenericPathCompiles(linearXformPipelineSource(), "xform_loop_lid");
    expectGenericPathCompiles(dirLightNoSpecPipelineSource(), "dir_light_vert_loop_lid");
    expectGenericPathCompiles(dirLightSpecPipelineSource(), "dir_light_vert_loop_lid");
    expectGenericPathCompiles(dirLightSpecIndexedPipelineSource(), "dir_light_vert_loop_lid");
    expectGenericPathCompiles(dirLightSpecPvDiffPipelineSource(), "dir_light_vert_loop_lid");
    expectGenericPathCompiles(ptLightNoSpecPipelineSource(), "pt_light_vert_loop_lid");
    expectGenericPathCompiles(ptLightSpecPipelineSource(), "pt_light_vert_loop_lid");
    expectGenericPathCompiles(ptLightSpecPvDiffPipelineSource(), "pt_light_vert_loop_lid");

    expectGenericPathCompiles(ps2glNamedXformLoopSource("vsmGeneralNoSpecQuad"), "xform_loop_lid");
    expectGenericPathCompiles(ps2glNamedXformLoopSource("vsmGeneralNoSpecTri"), "xform_loop_lid");
    expectGenericPathCompiles(ps2glNamedXformLoopSource("vsmGeneralTri"), "xform_loop_lid");
    expectGenericPathCompiles(ps2glNamedXformLoopSource("vsmGeneralPVDiffTri"), "xform_loop_lid");
    expectGenericPathCompiles(ps2glNamedXformLoopSource("vsmIndexed"), "xform_loop_lid");
}

TEST_CASE("Software pipeline: known-loop emitters remain cost references for generic path")
{
    std::string optimized = runOptimizedEmit(fastNoLightsPipelineSource());
    REQUIRE(optimized.length() > 0);

    std::vector<std::string> genericArgs;
    genericArgs.push_back("--disable-known-loop-optimizations");
    std::string generic = runEmitWithExtraArgs(fastNoLightsPipelineSource(), genericArgs);
    REQUIRE(generic.length() > 0);

    REQUIRE(contains(optimized, "xform_loop_lid__MAIN_LOOP:"));
    REQUIRE(contains(generic, "xform_loop_lid:"));

    ::test::RunResult optimizedCost = runCostJsonWithLoop(optimized, "xform_loop_lid__MAIN_LOOP=10");
    ::test::RunResult genericCost = runCostJsonWithLoop(generic, "xform_loop_lid=10");
    REQUIRE(optimizedCost.exit_code == 0);
    REQUIRE(genericCost.exit_code == 0);

    CHECK(jsonMetric(optimizedCost.stdout_data, "affine_estimated_loop_cycles") == 12);
    CHECK(jsonMetric(genericCost.stdout_data, "affine_estimated_loop_cycles") > 12);
    CHECK(contains(optimizedCost.stdout_data, "\"affine_estimated_cycles\":"));
    CHECK(contains(genericCost.stdout_data, "\"affine_estimated_cycles\":"));
}

TEST_CASE("Software pipeline: fast lit transform loop emits a 16-cycle steady state")
{
    std::string vsm = runOptimizedEmit(fastLitPipelineSource());
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "xform_loop_lid__ENTRY_POINT:"));
    CHECK(contains(vsm, "xform_loop_lid__PRO1:"));
    CHECK(contains(vsm, "xform_loop_lid__MAIN_LOOP:"));
    CHECK(contains(vsm, "xform_loop_lid__EPI0:"));
    CHECK(contains(vsm, "xform_loop_lid__EPI1:"));
    CHECK(contains(vsm, "ibne VI02, VI05, xform_loop_lid__MAIN_LOOP"));
    CHECK(contains(vsm, "maddz.xyz VF23, VF17, VF19z     lq.xyz VF10, 1(VI02)"));
    CHECK(contains(vsm, "mulq.xyz VF25, VF24, q          mfir.w VF09, VI06"));
}

TEST_CASE("Software pipeline: SCEI transform loop emits a 19-cycle steady state")
{
    std::string vsm = runOptimizedEmit(sceiPipelineSource());
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "xform_loop_lid__ENTRY_POINT:"));
    CHECK(contains(vsm, "xform_loop_lid__PRO1:"));
    CHECK(contains(vsm, "xform_loop_lid__MAIN_LOOP:"));
    CHECK(contains(vsm, "xform_loop_lid__EPI0:"));
    CHECK(contains(vsm, "xform_loop_lid__EPI1:"));
    CHECK(contains(vsm, "ibne VI03, VI05, xform_loop_lid__MAIN_LOOP"));
    CHECK(contains(vsm, "clipw.xyz VF18xyz, VF08w        div q, VF00w, VF10w"));
    CHECK(contains(vsm, "mulq.xyz VF24, VF10, q          iaddiu VI07, VI07, 0x7fff"));
}

TEST_CASE("Software pipeline: final color loop keeps the original output register w lane")
{
    std::string vsm = runOptimizedEmit(finalColorPipelineSource());
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "final_loop_lid__ENTRY_POINT:"));
    CHECK(contains(vsm, "final_loop_lid__PRO1:"));
    CHECK(contains(vsm, "final_loop_lid__MAIN_LOOP:"));
    CHECK(contains(vsm, "final_loop_lid__EPI0:"));
    CHECK(contains(vsm, "final_loop_lid__EPI1:"));
    CHECK(contains(vsm, "ibne VI03, VI02, final_loop_lid__MAIN_LOOP"));
    CHECK(contains(vsm, "lq.xyz VF25, 1(VI03)"));
    CHECK(contains(vsm, "sq VF08, -8(VI03)"));
    CHECK(contains(vsm, "ftoi0.xyz VF08, VF24"));
    CHECK(!contains(vsm, "sq VF25, -8(VI03)"));
}

TEST_CASE("Software pipeline: linear transform loop computes next flags before looping")
{
    std::string vsm = runOptimizedEmit(linearXformPipelineSource());
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "xform_loop_lid__ENTRY_POINT:"));
    CHECK(contains(vsm, "xform_loop_lid__PRO1:"));
    CHECK(contains(vsm, "xform_loop_lid__MAIN_LOOP:"));
    CHECK(contains(vsm, "xform_loop_lid__EPI0:"));
    CHECK(contains(vsm, "xform_loop_lid__EXIT_POINT:"));
    CHECK(contains(vsm, "ibne VI13, VI04, xform_loop_lid__MAIN_LOOP"));
    CHECK(contains(vsm, "lq.xyz VF18, 2(VI13)"));
    CHECK(contains(vsm, "fcand VI01, 0x003ffff"));
}

TEST_CASE("Software pipeline: linear transform loop pipelines when clip scratch aliases strip flip")
{
    std::string source = linearXformPipelineSource();
    const std::string stripFlip = "\tiand vi10, vi07, vi06\n";
    const std::string stripMerge = "\tior vi05, vi05, vi10\n";
    std::string::size_type pos = source.find(stripFlip);
    REQUIRE(pos != std::string::npos);
    source.replace(pos, stripFlip.length(), "\tiand vi01, vi07, vi06\n");
    pos = source.find(stripMerge);
    REQUIRE(pos != std::string::npos);
    source.replace(pos, stripMerge.length(), "\tior vi05, vi05, vi01\n");

    std::string vsm = runOptimizedEmit(source);
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "xform_loop_lid__PRO1:"));
    CHECK(contains(vsm, "xform_loop_lid__MAIN_LOOP:"));
    CHECK(contains(vsm, "xform_loop_lid__EPI0:"));
    CHECK(contains(vsm, "ibne VI13, VI04, xform_loop_lid__MAIN_LOOP"));
    CHECK(!contains(vsm, "iand VI01, VI07, VI06"));
    CHECK(contains(vsm, "iand VI09, VI07, VI06"));
    CHECK(contains(vsm, "ior VI05, VI05, VI09"));
    CHECK(contains(vsm, "fcand VI01, 0x003ffff"));
}

TEST_CASE("Software pipeline: safe ps2gl primitive transform loops keep SCE-sized steady states")
{
    struct Case
    {
        const char* name;
        int expectedMainLines;
    };

    const Case cases[] =
    {
        { "vsmGeneralNoSpecQuad", 47 },
        { "vsmGeneralNoSpecTri", 36 },
        { "vsmGeneralTri", 36 },
        { "vsmGeneralPVDiffTri", 36 },
        { "vsmIndexed", 38 }
    };

    for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        std::string vsm = runOptimizedEmit(ps2glNamedXformLoopSource(cases[i].name));
        REQUIRE(vsm.length() > 0);
        CHECK(contains(vsm, "xform_loop_lid__ENTRY_POINT:"));
        CHECK(contains(vsm, "xform_loop_lid__MAIN_LOOP:"));
        CHECK(blockLineCount(vsm, "xform_loop_lid__MAIN_LOOP") == cases[i].expectedMainLines);
        if (std::string(cases[i].name) == "vsmGeneralNoSpecQuad")
        {
            CHECK(contains(vsm, "xform_loop_lid__FALLBACK_SHORT:"));
            CHECK(contains(vsm, "isubiu VI01, VI01, 384"));
            CHECK(contains(vsm, "                    nop                             iadd VI08, VI07, VI00"));
        }
    }
}

TEST_CASE("Software pipeline: no-spec directional light loop emits an 8-cycle steady state")
{
    std::string vsm = runOptimizedEmit(dirLightNoSpecPipelineSource());
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "dir_light_vert_loop_lid__ENTRY_POINT:"));
    CHECK(contains(vsm, "dir_light_vert_loop_lid__MAIN_LOOP:"));
    CHECK(contains(vsm, "dir_light_vert_loop_lid__SCALAR_FALLBACK:"));
    CHECK(contains(vsm, "ibne VI13, VI04, dir_light_vert_loop_lid__MAIN_LOOP"));
    CHECK(contains(vsm, "sq.xyz VF18, -2(VI03)"));
}

TEST_CASE("Software pipeline: plain specular directional light loop pipelines shared W power chains")
{
    std::string vsm = runOptimizedEmit(dirLightSpecPipelineSource());
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "dir_light_vert_loop_lid__SPEC_ENTRY_POINT:"));
    CHECK(contains(vsm, "dir_light_vert_loop_lid__MAIN_LOOP:"));
    CHECK(contains(vsm, "ibne VI13, VI04, dir_light_vert_loop_lid__MAIN_LOOP"));
    CHECK(contains(vsm, "maxx.z VF28, VF29, VF00x"));
    CHECK(contains(vsm, "mulz.xyz VF27, VF13, VF28z"));
    CHECK(contains(vsm, "maddaw.xyz ACC, VF15, VF22w"));
    CHECK(contains(vsm, "sq.xyz VF20, -2(VI03)"));
}

TEST_CASE("Software pipeline: indexed specular directional light loop keeps postincrement stores")
{
    std::string vsm = runOptimizedEmit(dirLightSpecIndexedPipelineSource());
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "dir_light_vert_loop_lid__SPEC_ENTRY_POINT:"));
    CHECK(contains(vsm, "dir_light_vert_loop_lid__MAIN_LOOP:"));
    CHECK(contains(vsm, "ibne VI04, VI06, dir_light_vert_loop_lid__MAIN_LOOP"));
    CHECK(contains(vsm, "lq.xyz VF27, 0(VI05)"));
    CHECK(contains(vsm, "sqi.xyz VF28, (VI05++)"));
}

TEST_CASE("Software pipeline: pv-diff specular directional light loop keeps per-vertex material diffuse")
{
    std::string vsm = runOptimizedEmit(dirLightSpecPvDiffPipelineSource());
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "dir_light_vert_loop_lid__SPEC_ENTRY_POINT:"));
    CHECK(contains(vsm, "dir_light_vert_loop_lid__MAIN_LOOP:"));
    CHECK(contains(vsm, "ibne VI13, VI04, dir_light_vert_loop_lid__MAIN_LOOP"));
    CHECK(contains(vsm, "iaddiu VI13, VI13, 4"));
    CHECK(contains(vsm, "lq.xyz VF19, -1(VI13)"));
    CHECK(contains(vsm, "lq.xyz VF19, -5(VI13)"));
}

TEST_CASE("Software pipeline: no-spec point light loop emits a 26-cycle steady state")
{
    std::string vsm = runOptimizedEmit(ptLightNoSpecPipelineSource());
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "pt_light_vert_loop_lid__ENTRY_POINT:"));
    CHECK(contains(vsm, "pt_light_vert_loop_lid__PRO1:"));
    CHECK(contains(vsm, "pt_light_vert_loop_lid__MAIN_LOOP:"));
    CHECK(contains(vsm, "pt_light_vert_loop_lid__SCALAR_FALLBACK:"));
    CHECK(contains(vsm, "ibne VI13, VI04, pt_light_vert_loop_lid__MAIN_LOOP"));
    CHECK(contains(vsm, "sq.xyz VF19, -8(VI03)"));
}

TEST_CASE("Software pipeline: plain specular point light loop pipelines Q/P and W power chains")
{
    std::string vsm = runOptimizedEmit(ptLightSpecPipelineSource());
    REQUIRE(vsm.length() > 0);

    CHECK(contains(vsm, "pt_light_vert_loop_lid__SPEC_ENTRY_POINT:"));
    CHECK(contains(vsm, "pt_light_vert_loop_lid__MAIN_LOOP:"));
    CHECK(contains(vsm, "ibne VI13, VI04, pt_light_vert_loop_lid__MAIN_LOOP"));
    CHECK(contains(vsm, "maddaw.xyz ACC, VF15, VF29w"));
    CHECK(contains(vsm, "sq.xyz VF20, -2(VI03)"));
}

TEST_CASE("Software pipeline: pv-diff specular point light loop stays scalar until W power chain is safe")
{
    std::string vsm = runOptimizedEmit(ptLightSpecPvDiffPipelineSource());
    REQUIRE(vsm.length() > 0);

    CHECK(!contains(vsm, "pt_light_vert_loop_lid__SPEC_ENTRY_POINT:"));
    CHECK(!contains(vsm, "pt_light_vert_loop_lid__MAIN_LOOP:"));
    CHECK(contains(vsm, "pt_light_vert_loop_lid:"));
    CHECK(contains(vsm, "ibne VI13, VI04, pt_light_vert_loop_lid"));
    CHECK(contains(vsm, "iaddiu VI13, VI13, 4"));
    CHECK(contains(vsm, "maddaw.xyz ACC"));
}
