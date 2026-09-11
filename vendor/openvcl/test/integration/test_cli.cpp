#include "test_harness.h"
#include "openvcl_runner.h"

#include "OpenVclVersion.h"

#include <string>
#include <vector>

namespace
{
    bool contains(const std::string& haystack, const std::string& needle)
    {
        return haystack.find(needle) != std::string::npos;
    }

    std::string pipelineInput()
    {
        return
            "loop_lid:\n"
            "--LoopCS 1, 3\n"
            "lq.xyz vf01, 0(vi01)\n"
            "mul.xyz vf03, vf01, vf02\n"
            "div q, vf00[w], vf03[w]\n"
            "mulq.xyz vf03, vf03, q\n"
            "add.xyz vf05, vf03, vf00\n"
            "lq.xyz vf06, 2(vi01)\n"
            "mulq.xyz vf06, vf06, q\n"
            "sq.xyz vf06, 0(vi02)\n"
            "iaddiu vi01, vi01, 3\n"
            "ibne vi01, vi03, loop_lid\n";
    }

    std::string scheduleInput()
    {
        return
            "add.xy vf01, vf02, vf03\n"
            "mul.xy vf04, vf01, vf05\n"
            "iaddiu vi01, vi00, 1\n";
    }

    std::string safePipelineInput()
    {
        return
            "loop_lid:\n"
            "--LoopCS 1, 1\n"
            "div q, vf00[w], vf01[w]\n"
            "mulq.xyz vf02, vf03, q\n"
            "add.xyz vf10, vf10, vf00\n"
            "add.xyz vf11, vf11, vf00\n"
            "add.xyz vf12, vf12, vf00\n"
            "add.xyz vf13, vf13, vf00\n"
            "add.xyz vf14, vf14, vf00\n"
            "add.xyz vf15, vf15, vf00\n"
            "iaddiu vi01, vi01, 1\n"
            "ibne vi01, vi02, loop_lid\n";
    }
}

TEST_CASE("CLI: --version prints the shared OpenVCL version")
{
    std::vector<std::string> args;
    args.push_back("--version");

    ::test::RunResult r = ::test::run_openvcl(args, "");
    REQUIRE(r.exit_code == 0);
    CHECK(r.stderr_data.empty());
    CHECK(r.stdout_data == std::string("OpenVCL Version ") + vcl::OPENVCL_VERSION + "\n");
}

TEST_CASE("CLI: loop pipeline text dump exposes Q software-pipeline candidates")
{
    std::vector<std::string> args;
    args.push_back("-n");
    args.push_back("--dump-loop-pipeline-info");

    ::test::RunResult r = ::test::run_openvcl(args, pipelineInput());
    REQUIRE(r.exit_code == 0);
    CHECK(r.stderr_data.empty());
    CHECK(contains(r.stdout_data, "OpenVCL VU loop pipeline opportunities"));
    CHECK(contains(r.stdout_data, "loop_lid q_producer_token=4"));
    CHECK(contains(r.stdout_data, "q_producer_tokens=4"));
    CHECK(contains(r.stdout_data, "q_stages=4->5,8(latency=7,gap=0,deficit=7,loop_gap=8,insert_gap=5,insert_deficit=2,strategy=loop_carried)"));
    CHECK(contains(r.stdout_data, "last_q_consumer_token=8"));
    CHECK(contains(r.stdout_data, "q_latency=7"));
    CHECK(contains(r.stdout_data, "q_producer_consumer_gap_cycles=0"));
    CHECK(contains(r.stdout_data, "q_producer_consumer_gap_deficit_cycles=7"));
    CHECK(contains(r.stdout_data, "loop_carried_q_gap_cycles=8"));
    CHECK(contains(r.stdout_data, "q_producer_insertion_gap_cycles=5"));
    CHECK(contains(r.stdout_data, "q_producer_insertion_gap_deficit_cycles=2"));
    CHECK(contains(r.stdout_data, "q_scheduling_strategy=loop_carried"));
    CHECK(contains(r.stdout_data, "loop_cs_clid=1"));
    CHECK(contains(r.stdout_data, "loop_cs_mlid=3"));
    CHECK(contains(r.stdout_data, "requires_prolog_epilog=yes"));
    CHECK(contains(r.stdout_data, "eligible_single_q_pipeline=yes"));
    CHECK(contains(r.stdout_data, "pipeline_plan=yes"));
    CHECK(contains(r.stdout_data, "pipeline_emittable=no"));
    CHECK(contains(r.stdout_data, "rewrite_plan=no"));
    CHECK(contains(r.stdout_data, "prolog_tokens=2|3|4"));
    CHECK(contains(r.stdout_data, "main_tokens=5|6|7|8|9|10|11"));
    CHECK(contains(r.stdout_data, "drain_tokens=5|6|7|8|9|10"));
    CHECK(contains(r.stdout_data, "blockers=insufficient_q_insertion_gap"));
    CHECK(contains(r.stdout_data, "rotated_registers=VF03"));
    CHECK(contains(r.stdout_data, "rotation_descriptors=VF03->VF31(in=x|y|z,out=x|y|z)"));
    CHECK(contains(r.stdout_data, "prefetch_descriptors=2:lq(load,base=VI01,offset=+0,reads_induction=yes:VI01,next_offset=+3)|3:mul(none,reads_induction=no)"));
    CHECK(contains(r.stdout_data, "memory_loads=2"));
    CHECK(contains(r.stdout_data, "memory_stores=1"));
    CHECK(contains(r.stdout_data, "induction_registers=VI01"));
    CHECK(contains(r.stdout_data, "induction_updates=VI01:+3@10"));
    CHECK(contains(r.stdout_data, "carried_q_inputs=VF03.x"));
}

TEST_CASE("CLI: loop pipeline JSON dump is stable enough for scheduler tooling")
{
    std::vector<std::string> args;
    args.push_back("-n");
    args.push_back("--dump-loop-pipeline-info-json");

    ::test::RunResult r = ::test::run_openvcl(args, pipelineInput());
    REQUIRE(r.exit_code == 0);
    CHECK(r.stderr_data.empty());
    CHECK(contains(r.stdout_data, "\"loop_pipeline_opportunities\": ["));
    CHECK(contains(r.stdout_data, "\"label\": \"loop_lid\""));
    CHECK(contains(r.stdout_data, "\"q_producer_token_index\": 4"));
    CHECK(contains(r.stdout_data, "\"q_producer_token_indices\": [4]"));
    CHECK(contains(r.stdout_data, "\"q_stages\": [{\"producer_token_index\": 4, \"consumer_token_indices\": [5, 8], \"producer_latency\": 7"));
    CHECK(contains(r.stdout_data, "\"loop_carried_q_gap_cycles\": 8, \"producer_insertion_gap_cycles\": 5, \"producer_insertion_gap_deficit_cycles\": 2, \"q_scheduling_strategy\": \"loop_carried\""));
    CHECK(contains(r.stdout_data, "\"q_producer_latency\": 7"));
    CHECK(contains(r.stdout_data, "\"q_producer_consumer_gap_cycles\": 0"));
    CHECK(contains(r.stdout_data, "\"q_producer_consumer_gap_deficit_cycles\": 7"));
    CHECK(contains(r.stdout_data, "\"loop_carried_q_gap_cycles\": 8"));
    CHECK(contains(r.stdout_data, "\"q_producer_insertion_gap_cycles\": 5"));
    CHECK(contains(r.stdout_data, "\"q_producer_insertion_gap_deficit_cycles\": 2"));
    CHECK(contains(r.stdout_data, "\"q_scheduling_strategy\": \"loop_carried\""));
    CHECK(contains(r.stdout_data, "\"loop_cs_clid\": 1"));
    CHECK(contains(r.stdout_data, "\"loop_cs_mlid\": 3"));
    CHECK(contains(r.stdout_data, "\"requires_loop_carried_registers\": true"));
    CHECK(contains(r.stdout_data, "\"memory_loads\": 2"));
    CHECK(contains(r.stdout_data, "\"memory_stores\": 1"));
    CHECK(contains(r.stdout_data, "\"induction_registers\": [\"VI01\"]"));
    CHECK(contains(r.stdout_data, "\"induction_updates\": [{\"register\": \"VI01\", \"mnemonic\": \"iaddiu\", \"immediate\": \"3\", \"step_known\": true, \"step\": 3, \"token_index\": 10}]"));
    CHECK(contains(r.stdout_data, "\"eligible_single_q_pipeline\": true"));
    CHECK(contains(r.stdout_data, "\"pipeline_plan\": {"));
    CHECK(contains(r.stdout_data, "\"available\": true"));
    CHECK(contains(r.stdout_data, "\"emittable\": false"));
    CHECK(contains(r.stdout_data, "\"blockers\": [\"insufficient_q_insertion_gap\"]"));
    CHECK(contains(r.stdout_data, "\"rotated_registers\": [\"VF03\"]"));
    CHECK(contains(r.stdout_data, "\"rotation_descriptors\": [{\"register\": \"VF03\", \"scratch_available\": true, \"scratch_register\": \"VF31\", \"input_fields\": [\"x\", \"y\", \"z\"], \"output_fields\": [\"x\", \"y\", \"z\"]}"));
    CHECK(contains(r.stdout_data, "\"prefetch_descriptors\": [{\"token_index\": 2, \"mnemonic\": \"lq\", \"memory\": \"load\", \"memory_base\": \"VI01\", \"memory_offset_known\": true, \"memory_offset\": 0, \"reads_induction_register\": true, \"induction_register\": \"VI01\", \"next_iteration_offset_known\": true, \"next_iteration_offset\": 3}"));
    CHECK(contains(r.stdout_data, "\"suffix_store_descriptors\": [{\"token_index\": 9, \"mnemonic\": \"sq\", \"memory_base\": \"VI02\", \"memory_offset_known\": true, \"memory_offset\": 0, \"uses_induction_register\": false, \"induction_register\": \"\", \"next_iteration_offset_known\": false, \"next_iteration_offset\": 0, \"stored_value_register\": \"VF06\", \"stored_value_fields\": [\"x\", \"y\", \"z\"], \"value_rotation_required\": false, \"value_scratch_register\": \"\", \"drain_candidate\": false, \"delayed_drain\": false, \"rotate_value_before_prefetch\": false, \"rotate_value_at_store\": false}]"));
    CHECK(contains(r.stdout_data, "\"suffix_store_drain_plan\": {"));
    CHECK(contains(r.stdout_data, "\"prolog_token_indices\": [2, 3, 4]"));
    CHECK(contains(r.stdout_data, "\"main_token_indices\": [5, 6, 7, 8, 9, 10, 11]"));
    CHECK(contains(r.stdout_data, "\"drain_token_indices\": [5, 6, 7, 8, 9, 10]"));
    CHECK(contains(r.stdout_data, "\"rewrite_plan\": {"));
    CHECK(contains(r.stdout_data, "\"available\": false"));
    CHECK(contains(r.stdout_data, "\"carried_q_input_registers\": [\"VF03.x\""));
}

TEST_CASE("CLI: loop pipeline dumps expose emitted rewrite plans")
{
    std::vector<std::string> args;
    args.push_back("-n");
    args.push_back("--dump-loop-pipeline-info");

    ::test::RunResult r = ::test::run_openvcl(args, safePipelineInput());
    REQUIRE(r.exit_code == 0);
    CHECK(r.stderr_data.empty());
    CHECK(contains(r.stdout_data, "pipeline_emittable=yes"));
    CHECK(contains(r.stdout_data, "rewrite_plan=yes"));
    CHECK(contains(r.stdout_data, "rewrite_prolog_label=loop_lid__PROLOG"));
    CHECK(contains(r.stdout_data, "rewrite_main_label=loop_lid"));
    CHECK(contains(r.stdout_data, "rewrite_insert_prefetch_after=3"));
    CHECK(contains(r.stdout_data, "rewrite_insert_q_after=11"));
    CHECK(contains(r.stdout_data, "rewrite_q_in_branch_delay=yes"));
}

TEST_CASE("CLI: loop pipeline JSON dumps expose emitted rewrite plans")
{
    std::vector<std::string> args;
    args.push_back("-n");
    args.push_back("--dump-loop-pipeline-info-json");

    ::test::RunResult r = ::test::run_openvcl(args, safePipelineInput());
    REQUIRE(r.exit_code == 0);
    CHECK(r.stderr_data.empty());
    CHECK(contains(r.stdout_data, "\"emittable\": true"));
    CHECK(contains(r.stdout_data, "\"rewrite_plan\": {"));
    CHECK(contains(r.stdout_data, "\"available\": true"));
    CHECK(contains(r.stdout_data, "\"prolog_label\": \"loop_lid__PROLOG\""));
    CHECK(contains(r.stdout_data, "\"main_label\": \"loop_lid\""));
    CHECK(contains(r.stdout_data, "\"prefetch_insert_after_token_index\": 3"));
    CHECK(contains(r.stdout_data, "\"q_producer_insert_after_token_index\": 11"));
    CHECK(contains(r.stdout_data, "\"q_producer_in_branch_delay_slot\": true"));
    CHECK(contains(r.stdout_data, "\"suffix_store_descriptors\": []"));
}

TEST_CASE("CLI: schedule text dump exposes ready-set issue slots")
{
    std::vector<std::string> args;
    args.push_back("-n");
    args.push_back("--dump-schedule-info");

    ::test::RunResult r = ::test::run_openvcl(args, scheduleInput());
    REQUIRE(r.exit_code == 0);
    CHECK(r.stderr_data.empty());
    CHECK(contains(r.stdout_data, "OpenVCL VU ready scheduler issue slots"));
    CHECK(contains(r.stdout_data, "program_cycle_count=5"));
    CHECK(contains(r.stdout_data, "block 0 first_token=0 terminator=none first_issue_cycle=0 cycle_count=5 slots=5"));
    CHECK(contains(r.stdout_data, "slot 0 issue_cycle=0 program_issue_cycle=0 cycle_count=1 first=0:add second=2:iaddiu upper=0:add lower=2:iaddiu padding=no padding_kind=none"));
    CHECK(contains(r.stdout_data, "slot 1 issue_cycle=1 program_issue_cycle=1 cycle_count=1 first=- second=- upper=- lower=- padding=yes padding_kind=nop"));
    CHECK(contains(r.stdout_data, "ignored_waw_resources=mac|clip"));
    CHECK(contains(r.stdout_data, "slot 4 issue_cycle=4 program_issue_cycle=4 cycle_count=1 first=1:mul second=- upper=1:mul lower=- padding=no padding_kind=none"));
}

TEST_CASE("CLI: schedule text dump honors generic software-pipeline rewrites")
{
    std::vector<std::string> args;
    args.push_back("-n");
    args.push_back("--enable-generic-software-pipelining");
    args.push_back("--dump-schedule-info");

    ::test::RunResult r = ::test::run_openvcl(args, safePipelineInput());
    REQUIRE(r.exit_code == 0);
    CHECK(r.stderr_data.empty());
    CHECK(contains(r.stdout_data, "loop_lid__PROLOG:"));
    CHECK(contains(r.stdout_data, "loop_lid:"));
    CHECK(contains(r.stdout_data, "div[branch_delay]"));
}

TEST_CASE("CLI: schedule JSON dump is stable enough for generic emission tooling")
{
    std::vector<std::string> args;
    args.push_back("-n");
    args.push_back("--dump-schedule-info-json");

    ::test::RunResult r = ::test::run_openvcl(args, scheduleInput());
    REQUIRE(r.exit_code == 0);
    CHECK(r.stderr_data.empty());
    CHECK(contains(r.stdout_data, "\"program_cycle_count\": 5"));
    CHECK(contains(r.stdout_data, "\"scheduled_blocks\": ["));
    CHECK(contains(r.stdout_data, "\"block_index\": 0"));
    CHECK(contains(r.stdout_data, "\"first_token_index\": 0"));
    CHECK(contains(r.stdout_data, "\"terminator\": \"none\""));
    CHECK(contains(r.stdout_data, "\"first_issue_cycle\": 0"));
    CHECK(contains(r.stdout_data, "\"program_issue_cycle\": 0"));
    CHECK(contains(r.stdout_data, "\"issue_cycle\": 0"));
    CHECK(contains(r.stdout_data, "\"cycle_count\": 1"));
    CHECK(contains(r.stdout_data, "\"second_token_index\": 2"));
    CHECK(contains(r.stdout_data, "\"upper\": \"add\""));
    CHECK(contains(r.stdout_data, "\"lower\": \"iaddiu\""));
    CHECK(contains(r.stdout_data, "\"padding\": false"));
    CHECK(contains(r.stdout_data, "\"padding_kind\": \"none\""));
    CHECK(contains(r.stdout_data, "\"padding_kind\": \"nop\""));
    CHECK(contains(r.stdout_data, "\"ignored_waw_resources\": [\"mac\", \"clip\"]"));
}

TEST_CASE("CLI: schedule JSON dump honors generic software-pipeline rewrites")
{
    std::vector<std::string> args;
    args.push_back("-n");
    args.push_back("--enable-generic-software-pipelining");
    args.push_back("--dump-schedule-info-json");

    ::test::RunResult r = ::test::run_openvcl(args, safePipelineInput());
    REQUIRE(r.exit_code == 0);
    CHECK(r.stderr_data.empty());
    CHECK(contains(r.stdout_data, "\"first\": \"loop_lid__PROLOG:\""));
    CHECK(contains(r.stdout_data, "\"first\": \"loop_lid:\""));
    CHECK(contains(r.stdout_data, "\"first\": \"div[branch_delay]\""));
}
