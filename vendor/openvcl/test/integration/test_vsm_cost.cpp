#include "test_harness.h"
#include "openvcl_runner.h"

#include <cstdlib>
#include <string>
#include <vector>

#ifndef OPENVCL_TEST_VSM_FIXTURES
#error "OPENVCL_TEST_VSM_FIXTURES must point at test/fixtures/vsm_cost"
#endif

namespace
{
    std::string fixturePath(const std::string& name)
    {
        return std::string(OPENVCL_TEST_VSM_FIXTURES) + "/" + name;
    }

    bool contains(const std::string& haystack, const std::string& needle)
    {
        return haystack.find(needle) != std::string::npos;
    }

    unsigned int countOccurrences(const std::string& haystack, const std::string& needle)
    {
        unsigned int count = 0;
        std::string::size_type pos = 0;
        while ((pos = haystack.find(needle, pos)) != std::string::npos)
        {
            ++count;
            pos += needle.size();
        }
        return count;
    }

    int textMetric(const std::string& report, const std::string& name)
    {
        std::string key = name + ": ";
        std::string::size_type pos = report.find(key);
        if( pos == std::string::npos )
            return -1;
        pos += key.size();
        return std::atoi(report.c_str() + pos);
    }

    int jsonMetric(const std::string& report, const std::string& name)
    {
        std::string key = "\"" + name + "\": ";
        std::string::size_type pos = report.find(key);
        if( pos == std::string::npos )
            return -1;
        pos += key.size();
        return std::atoi(report.c_str() + pos);
    }

    ::test::RunResult runCost(const std::string& fixture)
    {
        std::vector<std::string> args;
        args.push_back("--cost");
        args.push_back(fixturePath(fixture));
        return ::test::run_openvcl(args, "");
    }

    ::test::RunResult runCostJson(const std::string& fixture)
    {
        std::vector<std::string> args;
        args.push_back("--cost-json");
        args.push_back(fixturePath(fixture));
        return ::test::run_openvcl(args, "");
    }

    ::test::RunResult runCostCompare(const std::string& baseline, const std::string& candidate)
    {
        std::vector<std::string> args;
        args.push_back("--cost-compare");
        args.push_back(fixturePath(baseline));
        args.push_back(fixturePath(candidate));
        return ::test::run_openvcl(args, "");
    }

    ::test::RunResult runCostCompareJson(const std::string& baseline, const std::string& candidate)
    {
        std::vector<std::string> args;
        args.push_back("--cost-compare-json");
        args.push_back(fixturePath(baseline));
        args.push_back(fixturePath(candidate));
        return ::test::run_openvcl(args, "");
    }

    ::test::RunResult runCostCompareMarkdown(const std::string& baseline, const std::string& candidate)
    {
        std::vector<std::string> args;
        args.push_back("--cost-compare-markdown");
        args.push_back(fixturePath(baseline));
        args.push_back(fixturePath(candidate));
        return ::test::run_openvcl(args, "");
    }

    ::test::RunResult runCostCompareMarkdownLoop(const std::string& baseline, const std::string& candidate, const std::string& loop)
    {
        std::vector<std::string> args;
        args.push_back("--cost-compare-markdown");
        args.push_back(fixturePath(baseline));
        args.push_back("--cost-loop");
        args.push_back(loop);
        args.push_back(fixturePath(candidate));
        return ::test::run_openvcl(args, "");
    }

    ::test::RunResult runCostCompareListMarkdown(const std::string& manifest)
    {
        std::vector<std::string> args;
        args.push_back("--cost-compare-list-markdown");
        return ::test::run_openvcl(args, manifest);
    }

    ::test::RunResult runCostCompareListCheck(const std::string& metric, const std::string& manifest)
    {
        std::vector<std::string> args;
        args.push_back("--cost-compare-list-check");
        args.push_back(metric);
        return ::test::run_openvcl(args, manifest);
    }

    ::test::RunResult runCostCompareListCheckWithLoop(const std::string& metric,
                                                      const std::string& loop,
                                                      const std::string& manifest)
    {
        std::vector<std::string> args;
        args.push_back("--cost-compare-list-check");
        args.push_back(metric);
        args.push_back("--cost-loop");
        args.push_back(loop);
        return ::test::run_openvcl(args, manifest);
    }

    ::test::RunResult runCostLoop(const std::string& fixture, const std::string& loop)
    {
        std::vector<std::string> args;
        args.push_back("--cost");
        args.push_back("--cost-loop");
        args.push_back(loop);
        args.push_back(fixturePath(fixture));
        return ::test::run_openvcl(args, "");
    }

    ::test::RunResult runCostLoopStdin(const std::string& source, const std::string& loop)
    {
        std::vector<std::string> args;
        args.push_back("--cost");
        args.push_back("--cost-loop");
        args.push_back(loop);
        return ::test::run_openvcl(args, source);
    }

    ::test::RunResult runCostJsonLoopStdin(const std::string& source, const std::string& loop)
    {
        std::vector<std::string> args;
        args.push_back("--cost-json");
        args.push_back("--cost-loop");
        args.push_back(loop);
        return ::test::run_openvcl(args, source);
    }

    ::test::RunResult runCostLoopPresetStdin(const std::string& source, const std::string& preset)
    {
        std::vector<std::string> args;
        args.push_back("--cost");
        args.push_back("--cost-loop-preset");
        args.push_back(preset);
        return ::test::run_openvcl(args, source);
    }
}

TEST_CASE("VSM cost CLI: fixture file reports precomputed scheduled cost")
{
    ::test::RunResult r = runCost("simple_scheduled.vsm");
    REQUIRE(r.exit_code == 0);
    CHECK(contains(r.stdout_data, "VSM cost report"));
    CHECK(textMetric(r.stdout_data, "static_cycles") == 5);
    CHECK(textMetric(r.stdout_data, "instructions") == 5);
    CHECK(textMetric(r.stdout_data, "upper_instructions") == 1);
    CHECK(textMetric(r.stdout_data, "lower_instructions") == 4);
    CHECK(textMetric(r.stdout_data, "paired_cycles") == 1);
    CHECK(textMetric(r.stdout_data, "single_lower_cycles") == 3);
    CHECK(textMetric(r.stdout_data, "nop_only_cycles") == 1);
    CHECK(textMetric(r.stdout_data, "nop_slots") == 5);
    CHECK(textMetric(r.stdout_data, "branch_cycles") == 1);
    CHECK(textMetric(r.stdout_data, "waitq_cycles") == 1);
    CHECK(textMetric(r.stdout_data, "e_bit_cycles") == 1);
    CHECK(textMetric(r.stdout_data, "operation_latency_cycles") == 10);
    CHECK(textMetric(r.stdout_data, "long_latency_ops") == 3);
    CHECK(textMetric(r.stdout_data, "long_latency_cycles") == 5);
    CHECK(textMetric(r.stdout_data, "max_op_latency") == 3);
    CHECK(contains(r.stdout_data, "entry_lid: cycles=5"));
    CHECK(contains(r.stdout_data, "nop_slots=5"));
}

TEST_CASE("VSM cost CLI: JSON fixture report is suitable for comparisons")
{
    ::test::RunResult r = runCostJson("simple_scheduled.vsm");
    REQUIRE(r.exit_code == 0);
    CHECK(jsonMetric(r.stdout_data, "static_cycles") == 5);
    CHECK(jsonMetric(r.stdout_data, "instructions") == 5);
    CHECK(jsonMetric(r.stdout_data, "paired_cycles") == 1);
    CHECK(jsonMetric(r.stdout_data, "operation_latency_cycles") == 10);
    CHECK(jsonMetric(r.stdout_data, "long_latency_cycles") == 5);
    CHECK(contains(r.stdout_data, "\"label_order\": [\"entry_lid\"]"));
    CHECK(contains(r.stdout_data, "\"cost_by_label\""));
    CHECK(contains(r.stdout_data, "\"entry_lid\": {\"canonical_label\": \"entry_lid\", \"affine_role\": \"base\""));
    CHECK(contains(r.stdout_data, "\"label\": \"entry_lid\""));
    CHECK(contains(r.stdout_data, "\"nop_slots\": 5"));
}

TEST_CASE("VSM cost CLI: comparison reports baseline, candidate, and deltas")
{
    ::test::RunResult r = runCostCompare("simple_scheduled.vsm", "sce_padded_columns.vsm");
    REQUIRE(r.exit_code == 0);
    CHECK(contains(r.stdout_data, "VSM cost comparison"));
    CHECK(contains(r.stdout_data, "baseline: " + fixturePath("simple_scheduled.vsm")));
    CHECK(contains(r.stdout_data, "candidate: " + fixturePath("sce_padded_columns.vsm")));
    CHECK(contains(r.stdout_data, "static_cycles: baseline=5 candidate=2 delta=-3"));
    CHECK(contains(r.stdout_data, "estimated_total_cycles: baseline=5 candidate=2 delta=-3"));
    CHECK(contains(r.stdout_data, "instructions: baseline=5 candidate=3 delta=-2"));
    CHECK(contains(r.stdout_data, "nop_slots: baseline=5 candidate=1 delta=-4"));
    CHECK(contains(r.stdout_data, "top_weighted_estimated_blocks:"));
    CHECK(contains(r.stdout_data, "entry_lid: baseline_weighted_estimated_cycles=5 candidate_weighted_estimated_cycles=0 delta=-5"));
    CHECK(contains(r.stdout_data, "sce_style_lid: baseline_weighted_estimated_cycles=0 candidate_weighted_estimated_cycles=2 delta=2"));
    CHECK(contains(r.stdout_data, "top_weighted_idle_blocks:"));
    CHECK(contains(r.stdout_data, "entry_lid: baseline_weighted_nop_slots=5 candidate_weighted_nop_slots=0 delta=-5"));
    CHECK(contains(r.stdout_data, "top_weighted_wait_blocks:"));
    CHECK(contains(r.stdout_data, "(no block deltas)"));
}

TEST_CASE("VSM cost CLI: JSON comparison is suitable for scheduler before-after reports")
{
    ::test::RunResult r = runCostCompareJson("simple_scheduled.vsm", "sce_padded_columns.vsm");
    REQUIRE(r.exit_code == 0);
    CHECK(contains(r.stdout_data, "\"baseline\": {\"input\": \"" + fixturePath("simple_scheduled.vsm") + "\""));
    CHECK(contains(r.stdout_data, "\"candidate\": {\"input\": \"" + fixturePath("sce_padded_columns.vsm") + "\""));
    CHECK(contains(r.stdout_data, "\"delta\": {\"static_cycles\": -3"));
    CHECK(contains(r.stdout_data, "\"estimated_total_cycles\": -3"));
    CHECK(contains(r.stdout_data, "\"nop_slots\": -4"));
    CHECK(contains(r.stdout_data, "\"top_weighted_estimated_blocks\""));
    CHECK(contains(r.stdout_data, "\"label\": \"entry_lid\", \"baseline_weighted_estimated_cycles\": 5, \"candidate_weighted_estimated_cycles\": 0, \"delta_weighted_estimated_cycles\": -5"));
    CHECK(contains(r.stdout_data, "\"label_comparisons\""));
    CHECK(contains(r.stdout_data, "\"label\": \"entry_lid\", \"baseline\": {\"label\": \"entry_lid\""));
    CHECK(contains(r.stdout_data, "\"candidate\": {\"label\": \"entry_lid\""));
    CHECK(contains(r.stdout_data, "\"top_weighted_idle_blocks\""));
    CHECK(contains(r.stdout_data, "\"label\": \"entry_lid\", \"baseline_weighted_nop_slots\": 5, \"candidate_weighted_nop_slots\": 0, \"delta_weighted_nop_slots\": -5"));
    CHECK(contains(r.stdout_data, "\"top_weighted_wait_blocks\""));
}

TEST_CASE("VSM cost CLI: Markdown comparison emits a report-table row")
{
    ::test::RunResult r = runCostCompareMarkdown("simple_scheduled.vsm", "sce_padded_columns.vsm");
    REQUIRE(r.exit_code == 0);
    CHECK(contains(r.stdout_data, "| baseline | candidate | baseline static | candidate static | static delta |"));
    CHECK(contains(r.stdout_data, "baseline weighted estimated"));
    CHECK(contains(r.stdout_data, "| " + fixturePath("simple_scheduled.vsm") + " | " + fixturePath("sce_padded_columns.vsm") + " |"));
    CHECK(contains(r.stdout_data, " | 5 | 2 | -3 | 5 | 2 | -3 |"));
    CHECK(contains(r.stdout_data, " | 1 | 1 | 0 |"));
    CHECK(contains(r.stdout_data, " | 0.40x |"));
}

TEST_CASE("VSM cost CLI: Markdown comparison exposes loop-weighted totals")
{
    ::test::RunResult r = runCostCompareMarkdownLoop("simple_scheduled.vsm", "sce_padded_columns.vsm", "entry_lid=4");
    REQUIRE(r.exit_code == 0);
    CHECK(contains(r.stdout_data, "baseline weighted static"));
    CHECK(contains(r.stdout_data, "candidate weighted estimated"));
    CHECK(contains(r.stdout_data, "baseline affine estimated"));
    CHECK(contains(r.stdout_data, " | 20 | 2 | -18 | 20 | 2 | -18 | 0.10x |"));
    CHECK(contains(r.stdout_data, " | 0 + 5n | 2 + 0n | +2 | -5 |"));
}

TEST_CASE("VSM cost CLI: Markdown comparison list emits one table with multiple rows")
{
    const std::string manifest =
        "# baseline candidate\n"
        + fixturePath("simple_scheduled.vsm") + " " + fixturePath("sce_padded_columns.vsm") + "\n"
        + fixturePath("sce_padded_columns.vsm") + " " + fixturePath("simple_scheduled.vsm") + "\n";

    ::test::RunResult r = runCostCompareListMarkdown(manifest);
    REQUIRE(r.exit_code == 0);
    CHECK(countOccurrences(r.stdout_data, "| baseline | candidate | baseline static | candidate static | static delta |") == 1u);
    CHECK(contains(r.stdout_data, "| " + fixturePath("simple_scheduled.vsm") + " | " + fixturePath("sce_padded_columns.vsm") + " | 5 | 2 | -3 |"));
    CHECK(contains(r.stdout_data, "| " + fixturePath("sce_padded_columns.vsm") + " | " + fixturePath("simple_scheduled.vsm") + " | 2 | 5 | +3 |"));
}

TEST_CASE("VSM cost CLI: malformed Markdown comparison list is rejected")
{
    ::test::RunResult r = runCostCompareListMarkdown(fixturePath("simple_scheduled.vsm") + "\n");
    CHECK(r.exit_code != 0);
    CHECK(contains(r.stderr_data, "Invalid cost comparison list entry at line 1"));
}

TEST_CASE("VSM cost CLI: comparison list check accepts weighted loop cost improvements")
{
    const std::string manifest =
        fixturePath("simple_scheduled.vsm") + " " + fixturePath("sce_padded_columns.vsm") + "\n";

    ::test::RunResult r = runCostCompareListCheckWithLoop("weighted-estimated", "entry_lid=4", manifest);
    CHECK(r.exit_code == 0);
    CHECK(r.stderr_data.empty());
}

TEST_CASE("VSM cost CLI: comparison list check rejects slower weighted loop cost candidates")
{
    const std::string manifest =
        fixturePath("sce_padded_columns.vsm") + " " + fixturePath("simple_scheduled.vsm") + "\n";

    ::test::RunResult r = runCostCompareListCheckWithLoop("weighted-estimated", "entry_lid=4", manifest);
    CHECK(r.exit_code != 0);
    CHECK(contains(r.stderr_data, "Cost check failed for " + fixturePath("simple_scheduled.vsm")));
    CHECK(contains(r.stderr_data, "metric=weighted-estimated baseline=2 candidate=20"));
    CHECK(contains(r.stderr_data, "Cost comparison list check failed for 1 shader pair(s)"));
}

TEST_CASE("VSM cost CLI: comparison list check rejects unknown metrics")
{
    ::test::RunResult r = runCostCompareListCheck("aggregate-magic", "");
    CHECK(r.exit_code != 0);
    CHECK(contains(r.stderr_data, "Unknown cost comparison check metric"));
}

TEST_CASE("VSM cost CLI: loop repeat weights static block cost")
{
    ::test::RunResult r = runCostLoop("simple_scheduled.vsm", "entry_lid=4");
    REQUIRE(r.exit_code == 0);
    CHECK(textMetric(r.stdout_data, "static_cycles") == 5);
    CHECK(textMetric(r.stdout_data, "weighted_static_cycles") == 20);
    CHECK(textMetric(r.stdout_data, "weighted_instructions") == 20);
    CHECK(textMetric(r.stdout_data, "weighted_paired_cycles") == 4);
    CHECK(textMetric(r.stdout_data, "affine_static_base_cycles") == 0);
    CHECK(textMetric(r.stdout_data, "affine_static_loop_cycles") == 5);
    CHECK(textMetric(r.stdout_data, "affine_estimated_base_cycles") == 0);
    CHECK(textMetric(r.stdout_data, "affine_estimated_loop_cycles") == 5);
    CHECK(contains(r.stdout_data, "affine_estimated_cycles: 0 + 5n"));
    CHECK(contains(r.stdout_data, "entry_lid: cycles=5 repeat=4 weighted_cycles=20"));
    CHECK(contains(r.stdout_data, "top_weighted_blocks:"));
    CHECK(contains(r.stdout_data, "entry_lid: weighted_cycles=20 cycles=5 repeat=4"));
    CHECK(contains(r.stdout_data, "top_weighted_estimated_blocks:"));
    CHECK(contains(r.stdout_data, "entry_lid: weighted_estimated_cycles=20 estimated_cycles=5 repeat=4"));
    CHECK(contains(r.stdout_data, "weighted_nop_slots=20"));
    CHECK(contains(r.stdout_data, "top_weighted_idle_blocks:"));
    CHECK(contains(r.stdout_data, "entry_lid: weighted_nop_slots=20 nop_slots=5 repeat=4"));
}

TEST_CASE("VSM cost CLI: affine loop cost reports setup plus per-vertex term")
{
    const std::string source =
        "\t.vu\n"
        "init_lid:\n"
        "                    nop                             iaddiu VI01, VI00, 1\n"
        "loop_lid:\n"
        "                    add.xyz VF01, VF02, VF03        iaddiu VI01, VI01, -1\n"
        "done_lid:\n"
        "                    nop                             xgkick VI01\n";

    ::test::RunResult text = runCostLoopStdin(source, "loop_lid=10");
    REQUIRE(text.exit_code == 0);
    CHECK(textMetric(text.stdout_data, "static_cycles") == 3);
    CHECK(textMetric(text.stdout_data, "estimated_total_cycles") == 3);
    CHECK(textMetric(text.stdout_data, "weighted_static_cycles") == 12);
    CHECK(textMetric(text.stdout_data, "weighted_estimated_total_cycles") == 12);
    CHECK(textMetric(text.stdout_data, "affine_static_base_cycles") == 2);
    CHECK(textMetric(text.stdout_data, "affine_static_loop_cycles") == 1);
    CHECK(textMetric(text.stdout_data, "affine_estimated_base_cycles") == 2);
    CHECK(textMetric(text.stdout_data, "affine_estimated_loop_cycles") == 1);
    CHECK(contains(text.stdout_data, "affine_static_cycles: 2 + 1n"));
    CHECK(contains(text.stdout_data, "affine_estimated_cycles: 2 + 1n"));

    ::test::RunResult json = runCostJsonLoopStdin(source, "loop_lid=10");
    REQUIRE(json.exit_code == 0);
    CHECK(jsonMetric(json.stdout_data, "affine_static_base_cycles") == 2);
    CHECK(jsonMetric(json.stdout_data, "affine_static_loop_cycles") == 1);
    CHECK(jsonMetric(json.stdout_data, "affine_estimated_base_cycles") == 2);
    CHECK(jsonMetric(json.stdout_data, "affine_estimated_loop_cycles") == 1);
    CHECK(contains(json.stdout_data, "\"affine_estimated_cycles\": \"2 + 1n\""));
    CHECK(contains(json.stdout_data, "\"label_order\": [\"init_lid\", \"loop_lid\", \"done_lid\"]"));
    CHECK(contains(json.stdout_data, "\"init_lid\": {\"canonical_label\": \"init_lid\", \"affine_role\": \"base\", \"repeat\": 1, \"static_cycles\": 1"));
    CHECK(contains(json.stdout_data, "\"loop_lid\": {\"canonical_label\": \"loop_lid\", \"affine_role\": \"loop\", \"repeat\": 10, \"static_cycles\": 1"));
    CHECK(contains(json.stdout_data, "\"done_lid\": {\"canonical_label\": \"done_lid\", \"affine_role\": \"base\", \"repeat\": 1, \"static_cycles\": 1"));
}

TEST_CASE("VSM cost CLI: ps2gl loop preset weights known hot labels")
{
    const std::string source =
        "\t.vu\n"
        "xform_loop_lid:\n"
        "                    add.xyz VF01, VF02, VF03        iaddiu VI01, VI00, 1\n"
        "                    nop                             nop\n";

    ::test::RunResult r = runCostLoopPresetStdin(source, "ps2gl");
    REQUIRE(r.exit_code == 0);
    CHECK(textMetric(r.stdout_data, "static_cycles") == 2);
    CHECK(textMetric(r.stdout_data, "weighted_static_cycles") == 200);
    CHECK(contains(r.stdout_data, "xform_loop_lid: cycles=2 repeat=100 weighted_cycles=200"));
}

TEST_CASE("VSM cost CLI: ps2gl loop preset weights SCE optimized main-loop labels")
{
    const std::string source =
        "\t.vu\n"
        "EXPL_vu1_general_quad_pp4_vcl_xform_loop_lid__MAIN_LOOP:\n"
        "                    add.xyz VF01, VF02, VF03        iaddiu VI01, VI00, 1\n"
        "                    nop                             nop\n";

    ::test::RunResult r = runCostLoopPresetStdin(source, "ps2gl");
    REQUIRE(r.exit_code == 0);
    CHECK(textMetric(r.stdout_data, "static_cycles") == 2);
    CHECK(textMetric(r.stdout_data, "weighted_static_cycles") == 200);
    CHECK(contains(r.stdout_data, "EXPL_vu1_general_quad_pp4_vcl_xform_loop_lid__MAIN_LOOP: cycles=2 repeat=100 weighted_cycles=200"));
}

TEST_CASE("VSM cost CLI: ps2gl loop preset weights OpenVCL software-pipelined main loops")
{
    const std::string source =
        "\t.vu\n"
        "xform_loop_lid__MAIN_LOOP:\n"
        "                    add.xyz VF01, VF02, VF03        iaddiu VI01, VI00, 1\n"
        "                    nop                             nop\n";

    ::test::RunResult r = runCostLoopPresetStdin(source, "ps2gl");
    REQUIRE(r.exit_code == 0);
    CHECK(textMetric(r.stdout_data, "static_cycles") == 2);
    CHECK(textMetric(r.stdout_data, "weighted_static_cycles") == 200);
    CHECK(contains(r.stdout_data, "xform_loop_lid__MAIN_LOOP: cycles=2 repeat=100 weighted_cycles=200"));
}

TEST_CASE("VSM cost CLI: ps2gl loop preset weights SCE fast-family transform loops")
{
    const std::string source =
        "\t.vu\n"
        "EXPL_vu1_fast_pp4_vcl_adcLoop_done_lid__MAIN_LOOP:\n"
        "                    add.xyz VF01, VF02, VF03        iaddiu VI01, VI00, 1\n"
        "                    nop                             nop\n";

    ::test::RunResult r = runCostLoopPresetStdin(source, "ps2gl");
    REQUIRE(r.exit_code == 0);
    CHECK(textMetric(r.stdout_data, "static_cycles") == 2);
    CHECK(textMetric(r.stdout_data, "weighted_static_cycles") == 200);
    CHECK(contains(r.stdout_data, "EXPL_vu1_fast_pp4_vcl_adcLoop_done_lid__MAIN_LOOP: cycles=2 repeat=100 weighted_cycles=200"));
}

TEST_CASE("VSM cost CLI: unknown loop preset is rejected")
{
    ::test::RunResult r = runCostLoopPresetStdin("\t.vu\n", "unknown");
    CHECK(r.exit_code != 0);
}

TEST_CASE("VSM cost CLI: fixture accepts SCE-style padded VSM output")
{
    ::test::RunResult r = runCost("sce_padded_columns.vsm");
    REQUIRE(r.exit_code == 0);
    CHECK(textMetric(r.stdout_data, "static_cycles") == 2);
    CHECK(textMetric(r.stdout_data, "instructions") == 3);
    CHECK(textMetric(r.stdout_data, "upper_instructions") == 1);
    CHECK(textMetric(r.stdout_data, "lower_instructions") == 2);
    CHECK(textMetric(r.stdout_data, "paired_cycles") == 1);
    CHECK(textMetric(r.stdout_data, "operation_latency_cycles") == 7);
    CHECK(textMetric(r.stdout_data, "long_latency_ops") == 2);
    CHECK(contains(r.stdout_data, "sce_style_lid: cycles=2"));
}

TEST_CASE("VSM cost CLI: fixture proves long-latency operations affect weighted cost")
{
    ::test::RunResult r = runCost("long_latency_weighted.vsm");
    REQUIRE(r.exit_code == 0);
    CHECK(textMetric(r.stdout_data, "static_cycles") == 4);
    CHECK(textMetric(r.stdout_data, "instructions") == 7);
    CHECK(textMetric(r.stdout_data, "paired_cycles") == 3);
    CHECK(textMetric(r.stdout_data, "waitq_cycles") == 1);
    CHECK(textMetric(r.stdout_data, "waitp_cycles") == 1);
    CHECK(textMetric(r.stdout_data, "fdiv_ops") == 1);
    CHECK(textMetric(r.stdout_data, "efu_ops") == 1);
    CHECK(textMetric(r.stdout_data, "operation_latency_cycles") == 47);
    CHECK(textMetric(r.stdout_data, "long_latency_ops") == 5);
    CHECK(textMetric(r.stdout_data, "long_latency_cycles") == 40);
    CHECK(textMetric(r.stdout_data, "max_op_latency") == 29);
    CHECK(contains(r.stdout_data, "weighted_lid: cycles=4"));
}

TEST_CASE("VSM cost CLI: stdin wait program reports estimated stalls")
{
    const std::string source =
        "\t.vu\n"
        "wait_lid:\n"
        "                    nop                             div q, VF01w, VF02w\n"
        "                    nop                             waitq\n"
        "                    nop                             esadd p, VF03\n"
        "                    nop                             waitp\n";

    ::test::RunResult r = runCostLoopStdin(source, "wait_lid=3");
    REQUIRE(r.exit_code == 0);
    CHECK(textMetric(r.stdout_data, "static_cycles") == 4);
    CHECK(textMetric(r.stdout_data, "estimated_total_cycles") == 20);
    CHECK(textMetric(r.stdout_data, "wait_stall_cycles") == 16);
    CHECK(textMetric(r.stdout_data, "waitq_stall_cycles") == 6);
    CHECK(textMetric(r.stdout_data, "waitp_stall_cycles") == 10);
    CHECK(textMetric(r.stdout_data, "weighted_estimated_total_cycles") == 60);
    CHECK(textMetric(r.stdout_data, "weighted_wait_stall_cycles") == 48);
    CHECK(contains(r.stdout_data, "top_weighted_estimated_blocks:"));
    CHECK(contains(r.stdout_data, "wait_lid: weighted_estimated_cycles=60 estimated_cycles=20 repeat=3"));
    CHECK(contains(r.stdout_data, "top_weighted_wait_blocks:"));
    CHECK(contains(r.stdout_data, "wait_lid: weighted_wait_stall=48 wait_stall=16 repeat=3"));
}

TEST_CASE("VSM cost CLI: stdin EFU producer throughput reports issue stalls")
{
    const std::string source =
        "\t.vu\n"
        "efu_lid:\n"
        "                    nop                             esadd p, VF01\n"
        "                    nop                             esadd p, VF02\n"
        "                    nop                             waitp\n"
        "                    nop                             mfp.w VF03, p\n";

    ::test::RunResult r = runCostLoopStdin(source, "efu_lid=2");
    REQUIRE(r.exit_code == 0);
    CHECK(textMetric(r.stdout_data, "static_cycles") == 4);
    CHECK(textMetric(r.stdout_data, "estimated_total_cycles") == 23);
    CHECK(textMetric(r.stdout_data, "issue_stall_cycles") == 9);
    CHECK(textMetric(r.stdout_data, "efu_issue_stall_cycles") == 9);
    CHECK(textMetric(r.stdout_data, "wait_stall_cycles") == 10);
    CHECK(textMetric(r.stdout_data, "weighted_estimated_total_cycles") == 46);
    CHECK(textMetric(r.stdout_data, "weighted_issue_stall_cycles") == 18);
    CHECK(contains(r.stdout_data, "efu_lid: cycles=4 repeat=2 weighted_cycles=8"));
    CHECK(contains(r.stdout_data, "issue_stall=9 fdiv_issue_stall=0 efu_issue_stall=9"));
}

TEST_CASE("VSM cost CLI: stdin MFP does not report a new P producer stall")
{
    const std::string source =
        "\t.vu\n"
        "mfp_lid:\n"
        "                    nop                             esadd p, VF01\n"
        "                    nop                             waitp\n"
        "                    nop                             mfp.w VF02, p\n"
        "                    nop                             waitp\n";

    ::test::RunResult r = runCostLoopStdin(source, "mfp_lid=3");
    REQUIRE(r.exit_code == 0);
    CHECK(textMetric(r.stdout_data, "static_cycles") == 4);
    CHECK(textMetric(r.stdout_data, "estimated_total_cycles") == 14);
    CHECK(textMetric(r.stdout_data, "issue_stall_cycles") == 0);
    CHECK(textMetric(r.stdout_data, "wait_stall_cycles") == 10);
    CHECK(textMetric(r.stdout_data, "weighted_estimated_total_cycles") == 42);
    CHECK(contains(r.stdout_data, "mfp_lid: cycles=4 repeat=3 weighted_cycles=12"));
    CHECK(contains(r.stdout_data, "wait_stall=10 waitq_stall=0 waitp_stall=10 estimated_cycles=14"));
}
