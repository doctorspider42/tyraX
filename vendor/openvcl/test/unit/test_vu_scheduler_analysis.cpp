#include "test_harness.h"

#include "../../src/Error.h"
#include "../../src/File.h"
#include "../../src/Line.h"
#include "../../src/Operand.h"
#include "../../src/RegisterAllocator.h"
#include "../../src/Token.h"
#include "../../src/Tokenizer.h"
#include "../../src/VuInstructionInfo.h"
#include "../../src/VuLatencyTracker.h"
#include "../../src/VuSchedulerAnalysis.h"
#include "../../src/VuSchedulingRules.h"
#include "../../src/VuTokenResourceAccess.h"

#include <list>
#include <string>
#include <vector>

namespace
{
    std::list<vcl::Operand> makeOperands()
    {
        std::list<vcl::Operand> ops;
        ops.push_back(vcl::Operand("--barrier", 0, vcl::Operand::PREPROCESSOR | vcl::Operand::FILTERED, ""));
        ops.push_back(vcl::Operand("--cont", 0, vcl::Operand::PREPROCESSOR | vcl::Operand::FILTERED, ""));
        ops.push_back(vcl::Operand("--enter", 0, vcl::Operand::PREPROCESSOR | vcl::Operand::FILTERED, "", vcl::Operand::ENTER));
        ops.push_back(vcl::Operand("--endenter", 0, vcl::Operand::PREPROCESSOR | vcl::Operand::FILTERED, "", vcl::Operand::ENTER));
        ops.push_back(vcl::Operand("--exit", 0, vcl::Operand::PREPROCESSOR | vcl::Operand::FILTERED, "", vcl::Operand::EXIT));
        ops.push_back(vcl::Operand("--endexit", 0, vcl::Operand::PREPROCESSOR | vcl::Operand::FILTERED, "", vcl::Operand::EXIT));
        ops.push_back(vcl::Operand("--LoopCS", 2, vcl::Operand::PREPROCESSOR | vcl::Operand::FILTERED, "imm:integer,imm:integer"));
        ops.push_back(vcl::Operand("--LoopExtra", 1, vcl::Operand::PREPROCESSOR | vcl::Operand::FILTERED, "imm:integer"));

        for (const vcl::VuInstructionInfo* info = vcl::allVuInstructionInfos(); info->mnemonic; ++info)
        {
            ops.push_back(vcl::Operand(info->operandName,
                                       info->arguments,
                                       info->operandFlags,
                                       info->operandPattern,
                                       info->operandUnit,
                                       info->throughput,
                                       info->latency));
        }
        return ops;
    }

    struct ParsedProgram
    {
        ParsedProgram() : operands(makeOperands())
        {
            tokenizer.setOperands(operands);
            tokenizer.setNewSyntax(true);
        }

        bool parse(const std::string& text)
        {
            lines.push_back(vcl::Line(file, static_cast<unsigned int>(lines.size() + 1), static_cast<unsigned int>(lines.size() + 1), text));
            return tokenizer.parse(lines.back());
        }

        static vcl::File file;
        std::list<vcl::Operand> operands;
        std::list<vcl::Line> lines;
        vcl::Tokenizer tokenizer;
    };

    vcl::File ParsedProgram::file("<scheduler-analysis-test>");

    bool hasEdge(const std::vector<vcl::VuDependencyEdge>& edges, unsigned int before, unsigned int after, vcl::VuDependencyKind kind)
    {
        for (std::vector<vcl::VuDependencyEdge>::const_iterator i = edges.begin(); i != edges.end(); ++i)
        {
            if (i->before == before && i->after == after && i->kind == kind)
                return true;
        }
        return false;
    }

    bool hasString(const std::list<std::string>& values, const std::string& value)
    {
        for (std::list<std::string>::const_iterator i = values.begin(); i != values.end(); ++i)
        {
            if (*i == value)
                return true;
        }
        return false;
    }

    const vcl::VuSoftwarePipelineRotation* findRotation(const std::vector<vcl::VuSoftwarePipelineRotation>& rotations,
                                                        const std::string& registerBase)
    {
        for (std::vector<vcl::VuSoftwarePipelineRotation>::const_iterator i = rotations.begin(); i != rotations.end(); ++i)
        {
            if (i->registerBase == registerBase)
                return &*i;
        }
        return NULL;
    }

    const vcl::VuLoopInductionUpdate* findInductionUpdate(const std::vector<vcl::VuLoopInductionUpdate>& updates,
                                                          const std::string& registerName)
    {
        for (std::vector<vcl::VuLoopInductionUpdate>::const_iterator i = updates.begin(); i != updates.end(); ++i)
        {
            if (i->registerName == registerName)
                return &*i;
        }
        return NULL;
    }

    const vcl::VuSoftwarePipelinePrefetch* findPrefetch(const std::vector<vcl::VuSoftwarePipelinePrefetch>& prefetches,
                                                        unsigned int tokenIndex)
    {
        for (std::vector<vcl::VuSoftwarePipelinePrefetch>::const_iterator i = prefetches.begin(); i != prefetches.end(); ++i)
        {
            if (i->tokenIndex == tokenIndex)
                return &*i;
        }
        return NULL;
    }

    const vcl::VuSoftwarePipelineSuffixStore* findSuffixStore(const std::vector<vcl::VuSoftwarePipelineSuffixStore>& stores,
                                                              unsigned int tokenIndex)
    {
        for (std::vector<vcl::VuSoftwarePipelineSuffixStore>::const_iterator i = stores.begin(); i != stores.end(); ++i)
        {
            if (i->tokenIndex == tokenIndex)
                return &*i;
        }
        return NULL;
    }

    const vcl::Register* allocatedFloatRegisterForAlias(const std::list<vcl::Token>& tokens,
                                                        const std::string& alias)
    {
        for (std::list<vcl::Token>::const_iterator token = tokens.begin(); token != tokens.end(); ++token)
        {
            for (std::list<vcl::Token::Argument>::const_iterator arg = token->arguments().begin();
                 arg != token->arguments().end(); ++arg)
            {
                if (arg->type() != vcl::Token::Argument::FLOAT_REGISTER
                    || arg->content() != vcl::Token::Argument::ALIAS
                    || arg->alias() != alias
                    || !arg->dependency()
                    || !arg->dependency()->alias())
                    continue;
                return arg->dependency()->alias()->allocatedRegister();
            }
        }
        return NULL;
    }

    std::string terminatorName(const vcl::VuBasicBlock& block)
    {
        if (!block.terminator || !block.terminator->operand())
            return "";
        return block.terminator->operand()->name();
    }

    bool tokenBranchesTo(const vcl::Token& token, const std::string& target)
    {
        for (std::list<vcl::Token::Argument>::const_iterator i = token.arguments().begin();
             i != token.arguments().end(); ++i)
        {
            if ((i->flags() & vcl::Token::Argument::BRANCH)
                && i->type() == vcl::Token::Argument::IMMEDIATE
                && i->immediate() == target)
                return true;
        }
        return false;
    }
}

TEST_CASE("VuSchedulerAnalysis: basic blocks split on labels and barriers")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("entry:"));
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("ibne vi01, vi02, next"));
    REQUIRE(program.parse("mul.xy vf04, vf05, vf06"));
    REQUIRE(program.parse("next:"));
    REQUIRE(program.parse("xgkick vi01"));
    REQUIRE(program.parse("add.xy vf07, vf08, vf09"));
    REQUIRE(program.parse("--barrier"));
    REQUIRE(program.parse("add.xy vf10, vf11, vf12"));

    std::vector<vcl::VuBasicBlock> blocks = vcl::buildVuBasicBlocks(program.tokenizer.tokens());
    REQUIRE(blocks.size() == 5u);
    CHECK(blocks[0].firstTokenIndex == 0u);
    CHECK(blocks[0].tokens.size() == 3u);
    CHECK(blocks[0].terminatedByBarrier == true);
    CHECK(blocks[0].terminatorKind == vcl::VU_BASIC_BLOCK_TERMINATOR_BRANCH);
    CHECK(blocks[0].terminator != 0);
    CHECK(blocks[1].firstTokenIndex == 3u);
    CHECK(blocks[1].tokens.size() == 1u);
    CHECK(blocks[1].terminatorKind == vcl::VU_BASIC_BLOCK_TERMINATOR_NONE);
    CHECK(blocks[1].terminator == 0);
    CHECK(blocks[2].firstTokenIndex == 4u);
    CHECK(blocks[2].tokens.size() == 2u);
    CHECK(blocks[2].terminatedByBarrier == true);
    CHECK(blocks[2].terminatorKind == vcl::VU_BASIC_BLOCK_TERMINATOR_XGKICK);
    CHECK(blocks[2].terminator != 0);
    CHECK(blocks[3].firstTokenIndex == 6u);
    CHECK(blocks[3].tokens.size() == 2u);
    CHECK(blocks[3].terminatedByBarrier == true);
    CHECK(blocks[3].terminatorKind == vcl::VU_BASIC_BLOCK_TERMINATOR_BOUNDARY);
    CHECK(terminatorName(blocks[3]) == "--barrier");
    CHECK(blocks[4].firstTokenIndex == 8u);
    CHECK(blocks[4].tokens.size() == 1u);
    CHECK(blocks[4].terminatorKind == vcl::VU_BASIC_BLOCK_TERMINATOR_NONE);
    CHECK(blocks[4].terminator == 0);
}

TEST_CASE("VuSchedulerAnalysis: basic blocks classify compiler boundary terminators")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("--cont"));
    REQUIRE(program.parse("--enter"));
    REQUIRE(program.parse("mul.xy vf04, vf05, vf06"));
    REQUIRE(program.parse("--exit"));
    REQUIRE(program.parse("add.xy vf07, vf08, vf09"));

    std::vector<vcl::VuBasicBlock> blocks = vcl::buildVuBasicBlocks(program.tokenizer.tokens());
    REQUIRE(blocks.size() == 4u);
    CHECK(blocks[0].firstTokenIndex == 0u);
    CHECK(blocks[0].tokens.size() == 2u);
    CHECK(blocks[0].terminatorKind == vcl::VU_BASIC_BLOCK_TERMINATOR_BOUNDARY);
    CHECK(terminatorName(blocks[0]) == "--cont");
    CHECK(blocks[1].firstTokenIndex == 2u);
    CHECK(blocks[1].tokens.size() == 1u);
    CHECK(blocks[1].terminatorKind == vcl::VU_BASIC_BLOCK_TERMINATOR_BOUNDARY);
    CHECK(terminatorName(blocks[1]) == "--enter");
    CHECK(blocks[2].firstTokenIndex == 3u);
    CHECK(blocks[2].tokens.size() == 2u);
    CHECK(blocks[2].terminatorKind == vcl::VU_BASIC_BLOCK_TERMINATOR_BOUNDARY);
    CHECK(terminatorName(blocks[2]) == "--exit");
    CHECK(blocks[3].firstTokenIndex == 5u);
    CHECK(blocks[3].tokens.size() == 1u);
    CHECK(blocks[3].terminatorKind == vcl::VU_BASIC_BLOCK_TERMINATOR_NONE);
}

TEST_CASE("VuSchedulerAnalysis: dependency graph uses register and resource descriptors")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("mulw.xyz vf04, vf01, vf05"));
    REQUIRE(program.parse("loi 1.0"));
    REQUIRE(program.parse("addi.xy vf06, vf07, i"));
    REQUIRE(program.parse("sq.xy vf06, 0(vi01)"));
    REQUIRE(program.parse("lq.xy vf08, 0(vi01)"));

    std::vector<vcl::VuBasicBlock> blocks = vcl::buildVuBasicBlocks(program.tokenizer.tokens());
    REQUIRE(blocks.size() == 1u);

    std::vector<vcl::VuDependencyEdge> edges = vcl::buildVuDependencyGraph(blocks[0]);
    CHECK(hasEdge(edges, 0u, 1u, vcl::VU_DEPENDENCY_REGISTER_RAW));
    CHECK(hasEdge(edges, 2u, 3u, vcl::VU_DEPENDENCY_RESOURCE_RAW));
    CHECK(hasEdge(edges, 3u, 4u, vcl::VU_DEPENDENCY_REGISTER_RAW));
    CHECK(hasEdge(edges, 4u, 5u, vcl::VU_DEPENDENCY_MEMORY));
}

TEST_CASE("VuSchedulerAnalysis: loop candidates find LoopCS back-edge loops")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("setup_lid:"));
    REQUIRE(program.parse("iaddiu vi01, vi00, 0"));
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 3"));
    REQUIRE(program.parse("lq.xyz vf01, 0(vi02)"));
    REQUIRE(program.parse("iaddiu vi02, vi02, 3"));
    REQUIRE(program.parse("ibne vi02, vi03, loop_lid"));
    REQUIRE(program.parse("done_lid:"));
    REQUIRE(program.parse("xgkick vi04"));

    std::vector<vcl::VuLoopCandidate> loops = vcl::findVuLoopCandidates(program.tokenizer.tokens());
    REQUIRE(loops.size() == 1u);
    CHECK(loops[0].label == "loop_lid");
    CHECK(loops[0].labelTokenIndex == 2u);
    CHECK(loops[0].branchTokenIndex == 6u);
    CHECK(loops[0].firstBodyTokenIndex == 2u);
    CHECK(loops[0].lastBodyTokenIndex == 6u);
    CHECK(loops[0].hasLoopDirective);
    CHECK(loops[0].loopCsClid == 1u);
    CHECK(loops[0].loopCsMlid == 3u);
    CHECK(loops[0].simpleCountedLoop);
    CHECK(loops[0].memoryLoadCount == 1u);
    CHECK(loops[0].memoryStoreCount == 0u);
    CHECK(!loops[0].hasMemoryPreOrPostIncrement);
    CHECK(!loops[0].hasXgkick);
    CHECK(hasString(loops[0].inductionRegisters, "VI02"));
    const vcl::VuLoopInductionUpdate* vi02Update = findInductionUpdate(loops[0].inductionUpdates, "VI02");
    REQUIRE(vi02Update != NULL);
    CHECK(vi02Update->stepKnown);
    CHECK(vi02Update->step == 3);
    CHECK(vi02Update->tokenIndex == 5u);
    CHECK(hasString(loops[0].loopReadWriteRegisters, "VI02"));
    REQUIRE(loops[0].branchToken != NULL);
    CHECK(vcl::normalizeVuMnemonic(loops[0].branchToken->name()) == "ibne");
    REQUIRE(loops[0].bodyTokens.size() == 5u);
    CHECK(loops[0].bodyTokens.front()->label() == "loop_lid");
    CHECK(vcl::normalizeVuMnemonic(loops[0].bodyTokens.back()->name()) == "ibne");
}

TEST_CASE("VuSchedulerAnalysis: loop candidates ignore forward branches and mark unconditional loops")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("entry_lid:"));
    REQUIRE(program.parse("ibne vi01, vi02, done_lid"));
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 3"));
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("b loop_lid"));
    REQUIRE(program.parse("done_lid:"));
    REQUIRE(program.parse("xgkick vi04"));

    std::vector<vcl::VuLoopCandidate> loops = vcl::findVuLoopCandidates(program.tokenizer.tokens());
    REQUIRE(loops.size() == 1u);
    CHECK(loops[0].label == "loop_lid");
    CHECK(loops[0].hasLoopDirective);
    CHECK(!loops[0].simpleCountedLoop);
    REQUIRE(loops[0].branchToken != NULL);
    CHECK(vcl::normalizeVuMnemonic(loops[0].branchToken->name()) == "b");
}

TEST_CASE("VuSchedulerAnalysis: pipeline opportunities expose loop-carried Q state")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 3"));
    REQUIRE(program.parse("lq.xyz vf01, 0(vi01)"));
    REQUIRE(program.parse("mul.xyz vf03, vf01, vf02"));
    REQUIRE(program.parse("div q, vf00[w], vf03[w]"));
    REQUIRE(program.parse("mulq.xyz vf03, vf03, q"));
    REQUIRE(program.parse("add.xyz vf05, vf03, vf00"));
    REQUIRE(program.parse("lq.xyz vf06, 2(vi01)"));
    REQUIRE(program.parse("mulq.xyz vf06, vf06, q"));
    REQUIRE(program.parse("sq.xyz vf06, 0(vi02)"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 3"));
    REQUIRE(program.parse("ibne vi01, vi03, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].label == "loop_lid");
    CHECK(opportunities[0].branchTokenIndex == 11u);
    CHECK(opportunities[0].qProducerTokenIndex == 4u);
    CHECK(opportunities[0].firstQConsumerTokenIndex == 5u);
    CHECK(opportunities[0].lastQConsumerTokenIndex == 8u);
    CHECK(opportunities[0].qProducerLatency == 7u);
    CHECK(opportunities[0].qProducerConsumerGapCycles == 0u);
    CHECK(opportunities[0].qProducerConsumerGapDeficitCycles == 7u);
    CHECK(opportunities[0].qProducerInsertionGapCycles == 5u);
    CHECK(opportunities[0].qProducerInsertionGapDeficitCycles == 2u);
    CHECK(opportunities[0].qSchedulingStrategy == vcl::VU_LOOP_Q_SCHEDULE_LOOP_CARRIED);
    CHECK(opportunities[0].sourcePrefixCycles > 0u);
    CHECK(opportunities[0].sourcePrefixCycles < opportunities[0].qProducerLatency);
    CHECK(opportunities[0].sourcePrefixCycles + opportunities[0].sourceSuffixCycles >= opportunities[0].qProducerLatency);
    CHECK(opportunities[0].branchDelaySlots == 1u);
    CHECK(opportunities[0].loopCarriedQGapCycles >= opportunities[0].qProducerLatency);
    CHECK(opportunities[0].simpleCountedLoop);
    CHECK(opportunities[0].hasSingleQProducer);
    CHECK(opportunities[0].requiresPrologEpilog);
    CHECK(opportunities[0].memoryLoadCount == 2u);
    CHECK(opportunities[0].memoryStoreCount == 1u);
    CHECK(!opportunities[0].hasMemoryPreOrPostIncrement);
    CHECK(!opportunities[0].hasXgkick);
    CHECK(hasString(opportunities[0].inductionRegisters, "VI01"));
    const vcl::VuLoopInductionUpdate* vi01Update = findInductionUpdate(opportunities[0].inductionUpdates, "VI01");
    REQUIRE(vi01Update != NULL);
    CHECK(vi01Update->stepKnown);
    CHECK(vi01Update->step == 3);
    CHECK(vi01Update->tokenIndex == 10u);
    CHECK(hasString(opportunities[0].loopReadWriteRegisters, "VI01"));
    CHECK(opportunities[0].requiresLoopCarriedRegisters);
    CHECK(opportunities[0].eligibleSingleQSoftwarePipeline);
    CHECK(opportunities[0].hasSoftwarePipelinePlan);
    CHECK(!opportunities[0].canEmitSoftwarePipeline);
    CHECK(hasString(opportunities[0].softwarePipelineBlockers, "insufficient_q_insertion_gap"));
    CHECK(!hasString(opportunities[0].softwarePipelineBlockers, "requires_register_rotation"));
    CHECK(!hasString(opportunities[0].softwarePipelineBlockers, "multi_instruction_prefetch"));
    CHECK(hasString(opportunities[0].softwarePipelineRotatedRegisters, "VF03"));
    CHECK(!hasString(opportunities[0].softwarePipelineRotatedRegisters, "VF06"));
    const vcl::VuSoftwarePipelinePrefetch* lqPrefetch = findPrefetch(opportunities[0].softwarePipelinePrefetches, 2u);
    REQUIRE(lqPrefetch != NULL);
    CHECK(lqPrefetch->mnemonic == "lq");
    CHECK(lqPrefetch->memoryKind == vcl::VU_MEMORY_LOAD);
    CHECK(lqPrefetch->hasMemoryBase);
    CHECK(lqPrefetch->memoryBaseRegister == "VI01");
    CHECK(lqPrefetch->hasMemoryOffset);
    CHECK(lqPrefetch->memoryOffset == 0);
    CHECK(lqPrefetch->readsInductionRegister);
    CHECK(lqPrefetch->inductionRegister == "VI01");
    CHECK(lqPrefetch->hasNextIterationOffset);
    CHECK(lqPrefetch->nextIterationOffset == 3);
    const vcl::VuSoftwarePipelinePrefetch* mulPrefetch = findPrefetch(opportunities[0].softwarePipelinePrefetches, 3u);
    REQUIRE(mulPrefetch != NULL);
    CHECK(mulPrefetch->memoryKind == vcl::VU_MEMORY_NONE);
    CHECK(!mulPrefetch->readsInductionRegister);
    const vcl::VuSoftwarePipelineRotation* vf03Rotation = findRotation(opportunities[0].softwarePipelineRotations, "VF03");
    REQUIRE(vf03Rotation != NULL);
    CHECK(vf03Rotation->hasScratchRegister);
    CHECK(vf03Rotation->scratchRegister == "VF31");
    CHECK(hasString(vf03Rotation->inputFields, "x"));
    CHECK(hasString(vf03Rotation->inputFields, "y"));
    CHECK(hasString(vf03Rotation->inputFields, "z"));
    CHECK(hasString(vf03Rotation->outputFields, "x"));
    CHECK(hasString(vf03Rotation->outputFields, "y"));
    CHECK(hasString(vf03Rotation->outputFields, "z"));
    CHECK(findRotation(opportunities[0].softwarePipelineRotations, "VF06") == NULL);
    REQUIRE(opportunities[0].qConsumerTokenIndices.size() == 2u);
    CHECK(opportunities[0].qConsumerTokenIndices[0] == 5u);
    CHECK(opportunities[0].qConsumerTokenIndices[1] == 8u);
    REQUIRE(opportunities[0].prologTokenIndices.size() == 3u);
    CHECK(opportunities[0].prologTokenIndices[0] == 2u);
    CHECK(opportunities[0].prologTokenIndices[1] == 3u);
    CHECK(opportunities[0].prologTokenIndices[2] == 4u);
    REQUIRE(opportunities[0].mainTokenIndices.size() == 7u);
    CHECK(opportunities[0].mainTokenIndices[0] == 5u);
    CHECK(opportunities[0].mainTokenIndices[6] == 11u);
    REQUIRE(opportunities[0].drainTokenIndices.size() == 6u);
    CHECK(opportunities[0].drainTokenIndices[0] == 5u);
    CHECK(opportunities[0].drainTokenIndices[5] == 10u);
    CHECK(hasString(opportunities[0].carriedQInputRegisters, "VF03.x"));
    CHECK(hasString(opportunities[0].carriedQInputRegisters, "VF06.x"));
    CHECK(hasString(opportunities[0].carriedQOutputRegisters, "VF03.x"));
    CHECK(hasString(opportunities[0].carriedQOutputRegisters, "VF06.x"));
}

TEST_CASE("VuSchedulerAnalysis: pipeline plans report loops that are safe for generic emission")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("mulq.xyz vf02, vf03, q"));
    REQUIRE(program.parse("add.xyz vf10, vf10, vf00"));
    REQUIRE(program.parse("add.xyz vf11, vf11, vf00"));
    REQUIRE(program.parse("add.xyz vf12, vf12, vf00"));
    REQUIRE(program.parse("add.xyz vf13, vf13, vf00"));
    REQUIRE(program.parse("add.xyz vf14, vf14, vf00"));
    REQUIRE(program.parse("add.xyz vf15, vf15, vf00"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].loopCsClid == 1u);
    CHECK(opportunities[0].loopCsMlid == 1u);
    CHECK(opportunities[0].eligibleSingleQSoftwarePipeline);
    CHECK(opportunities[0].hasSoftwarePipelinePlan);
    CHECK(opportunities[0].canEmitSoftwarePipeline);
    CHECK(opportunities[0].qProducerConsumerGapCycles == 0u);
    CHECK(opportunities[0].qProducerConsumerGapDeficitCycles == 7u);
    CHECK(opportunities[0].loopCarriedQGapCycles >= opportunities[0].qProducerLatency);
    CHECK(opportunities[0].qSchedulingStrategy == vcl::VU_LOOP_Q_SCHEDULE_LOOP_CARRIED);
    CHECK(opportunities[0].softwarePipelineBlockers.empty());
    CHECK(opportunities[0].softwarePipelineRotatedRegisters.empty());
    CHECK(!opportunities[0].requiresLoopCarriedRegisters);
    CHECK(hasString(opportunities[0].inductionRegisters, "VI01"));
}

TEST_CASE("VuSchedulerAnalysis: pipeline plans allow multiple induction registers")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("mulq.xyz vf02, vf03, q"));
    REQUIRE(program.parse("add.xyz vf10, vf10, vf00"));
    REQUIRE(program.parse("add.xyz vf11, vf11, vf00"));
    REQUIRE(program.parse("add.xyz vf12, vf12, vf00"));
    REQUIRE(program.parse("add.xyz vf13, vf13, vf00"));
    REQUIRE(program.parse("add.xyz vf14, vf14, vf00"));
    REQUIRE(program.parse("add.xyz vf15, vf15, vf00"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("iaddiu vi03, vi03, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].canEmitSoftwarePipeline);
    CHECK(hasString(opportunities[0].inductionRegisters, "VI01"));
    CHECK(hasString(opportunities[0].inductionRegisters, "VI03"));
    const vcl::VuLoopInductionUpdate* vi01Update = findInductionUpdate(opportunities[0].inductionUpdates, "VI01");
    const vcl::VuLoopInductionUpdate* vi03Update = findInductionUpdate(opportunities[0].inductionUpdates, "VI03");
    REQUIRE(vi01Update != NULL);
    REQUIRE(vi03Update != NULL);
    CHECK(vi01Update->step == 1);
    CHECK(vi03Update->step == 1);
    CHECK(!hasString(opportunities[0].softwarePipelineBlockers, "requires_single_induction_register"));
    CHECK(!hasString(opportunities[0].softwarePipelineBlockers, "missing_induction_register"));
}

TEST_CASE("VuSchedulerAnalysis: apply software pipeline plans rewrites emittable loops")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("mulq.xyz vf02, vf03, q"));
    REQUIRE(program.parse("add.xyz vf10, vf10, vf00"));
    REQUIRE(program.parse("add.xyz vf11, vf11, vf00"));
    REQUIRE(program.parse("add.xyz vf12, vf12, vf00"));
    REQUIRE(program.parse("add.xyz vf13, vf13, vf00"));
    REQUIRE(program.parse("add.xyz vf14, vf14, vf00"));
    REQUIRE(program.parse("add.xyz vf15, vf15, vf00"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuSoftwarePipelineRewritePlan> plans = vcl::buildVuSoftwarePipelineRewritePlans(program.tokenizer.tokens());
    REQUIRE(plans.size() == 1u);
    CHECK(plans[0].label == "loop_lid");
    CHECK(plans[0].prologLabel == "loop_lid__PROLOG");
    CHECK(plans[0].mainLabel == "loop_lid");
    CHECK(plans[0].drainLabel == "loop_lid__DRAIN");
    CHECK(plans[0].labelTokenIndex == 0u);
    CHECK(plans[0].branchTokenIndex == 11u);
    CHECK(plans[0].qProducerTokenIndex == 2u);
    CHECK(plans[0].prefetchInsertAfterTokenIndex == 3u);
    CHECK(plans[0].qProducerInsertAfterTokenIndex == 11u);
    CHECK(plans[0].qProducerInBranchDelaySlot);
    CHECK(!plans[0].emitsDrain);
    REQUIRE(plans[0].prologTokenIndices.size() == 1u);
    CHECK(plans[0].prologTokenIndices[0] == 2u);
    REQUIRE(plans[0].mainTokenIndices.size() == 9u);
    CHECK(plans[0].mainTokenIndices[0] == 3u);
    CHECK(plans[0].mainTokenIndices[8] == 11u);

    std::list<vcl::Token> transformed = vcl::applyVuSoftwarePipelinePlans(program.tokenizer.tokens());

    unsigned int prologLabels = 0;
    unsigned int mainLabels = 0;
    unsigned int divCount = 0;
    bool divInBranchDelaySlot = false;
    bool previousWasBranch = false;
    for (std::list<vcl::Token>::const_iterator i = transformed.begin(); i != transformed.end(); ++i)
    {
        if (i->label() == "loop_lid__PROLOG")
            ++prologLabels;
        if (i->label() == "loop_lid")
            ++mainLabels;
        const std::string mnemonic = vcl::normalizeVuMnemonic(i->name());
        if (mnemonic == "div")
        {
            ++divCount;
            if (previousWasBranch && (i->flags() & vcl::Token::BRANCH_DELAY_FILLER))
                divInBranchDelaySlot = true;
        }
        previousWasBranch = mnemonic == "ibne";
    }

    CHECK(prologLabels == 1u);
    CHECK(mainLabels == 1u);
    CHECK(divCount == 2u);
    CHECK(divInBranchDelaySlot);
    CHECK(transformed.size() == program.tokenizer.tokens().size() + 1u);
}

TEST_CASE("VuSchedulerAnalysis: software pipeline rewrites label-bearing LoopCS directives")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid: --LoopCS 1, 1"));
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("mulq.xyz vf02, vf03, q"));
    REQUIRE(program.parse("add.xyz vf10, vf10, vf00"));
    REQUIRE(program.parse("add.xyz vf11, vf11, vf00"));
    REQUIRE(program.parse("add.xyz vf12, vf12, vf00"));
    REQUIRE(program.parse("add.xyz vf13, vf13, vf00"));
    REQUIRE(program.parse("add.xyz vf14, vf14, vf00"));
    REQUIRE(program.parse("add.xyz vf15, vf15, vf00"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuSoftwarePipelineRewritePlan> plans =
        vcl::buildVuSoftwarePipelineRewritePlans(program.tokenizer.tokens());
    REQUIRE(plans.size() == 1u);
    CHECK(plans[0].labelTokenIndex == 0u);
    CHECK(plans[0].qProducerTokenIndex == 1u);

    std::list<vcl::Token> transformed =
        vcl::applyVuSoftwarePipelinePlans(program.tokenizer.tokens());

    unsigned int prologLabels = 0;
    unsigned int mainLabels = 0;
    unsigned int loopDirectives = 0;
    unsigned int divCount = 0;
    for (std::list<vcl::Token>::const_iterator i = transformed.begin(); i != transformed.end(); ++i)
    {
        if (i->label() == "loop_lid__PROLOG")
            ++prologLabels;
        if (i->label() == "loop_lid")
            ++mainLabels;
        if (vcl::normalizeVuMnemonic(i->name()) == "--loopcs")
            ++loopDirectives;
        if (vcl::normalizeVuMnemonic(i->name()) == "div")
            ++divCount;
    }

    CHECK(prologLabels == 1u);
    CHECK(mainLabels == 1u);
    CHECK(loopDirectives == 2u);
    CHECK(divCount == 2u);
}

TEST_CASE("VuSchedulerAnalysis: store base updates move before trailing stores")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("sq.xyz vf01, 0(vi03)"));
    REQUIRE(program.parse("add.xyz vf04, vf05, vf06"));
    REQUIRE(program.parse("sq.xyz vf02, 2(vi03)"));
    REQUIRE(program.parse("iaddiu vi03, vi03, 3"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::list<vcl::Token> tokens = program.tokenizer.tokens();
    CHECK(vcl::advanceVuStoreBaseUpdates(tokens));
    REQUIRE(tokens.size() == 5u);

    std::list<vcl::Token>::const_iterator i = tokens.begin();
    REQUIRE(vcl::normalizeVuMnemonic(i->name()) == "iaddiu");

    unsigned int storeCount = 0;
    bool sawOffsetMinusThree = false;
    bool sawOffsetMinusOne = false;
    for (; i != tokens.end(); ++i)
    {
        if (vcl::normalizeVuMnemonic(i->name()) != "sq")
            continue;
        vcl::VuTokenResourceAccess access;
        REQUIRE(vcl::buildVuTokenResourceAccess(*i, access));
        ++storeCount;
        if (access.hasMemoryOffset && access.memoryOffset == -3)
            sawOffsetMinusThree = true;
        if (access.hasMemoryOffset && access.memoryOffset == -1)
            sawOffsetMinusOne = true;
    }

    CHECK(storeCount == 2u);
    CHECK(sawOffsetMinusThree);
    CHECK(sawOffsetMinusOne);
}

TEST_CASE("VuSchedulerAnalysis: store base updates do not cross visible base reads")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("sq.xyz vf01, 0(vi03)"));
    REQUIRE(program.parse("iadd vi05, vi03, vi00"));
    REQUIRE(program.parse("iaddiu vi03, vi03, 3"));

    std::list<vcl::Token> tokens = program.tokenizer.tokens();
    CHECK(!vcl::advanceVuStoreBaseUpdates(tokens));
    REQUIRE(tokens.size() == 3u);
    CHECK(vcl::normalizeVuMnemonic(tokens.front().name()) == "sq");
}

TEST_CASE("VuSchedulerAnalysis: software pipeline helper applies safe store-base advance")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("lq.xyz vf01, 0(vi01)"));
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("mulq.xyz vf02, vf03, q"));
    REQUIRE(program.parse("sq.xyz vf02, 0(vi03)"));
    REQUIRE(program.parse("add.xyz vf10, vf10, vf00"));
    REQUIRE(program.parse("add.xyz vf11, vf11, vf00"));
    REQUIRE(program.parse("add.xyz vf12, vf12, vf00"));
    REQUIRE(program.parse("add.xyz vf13, vf13, vf00"));
    REQUIRE(program.parse("add.xyz vf14, vf14, vf00"));
    REQUIRE(program.parse("add.xyz vf15, vf15, vf00"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("iaddiu vi03, vi03, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::list<vcl::Token> transformed =
        vcl::applyVuSoftwarePipelinePlansWithSafeStoreBaseAdvance(program.tokenizer.tokens());

    bool inMainLoop = false;
    bool sawStoreBaseAdvance = false;
    bool sawAdjustedStore = false;
    for (std::list<vcl::Token>::const_iterator i = transformed.begin(); i != transformed.end(); ++i)
    {
        if (i->label() == "loop_lid")
            inMainLoop = true;
        if (!inMainLoop)
            continue;

        if (vcl::normalizeVuMnemonic(i->name()) == "iaddiu")
        {
            std::list<std::string> writes;
            vcl::collectVuRegisterWriteKeys(*i, writes);
            if (hasString(writes, "VI03"))
                sawStoreBaseAdvance = true;
        }

        vcl::VuTokenResourceAccess access;
        if (!vcl::buildVuTokenResourceAccess(*i, access)
            || access.memoryKind != vcl::VU_MEMORY_STORE
            || !access.hasMemoryBase
            || access.memoryBaseRegister != "VI03")
            continue;

        CHECK(sawStoreBaseAdvance);
        CHECK(access.hasMemoryOffset);
        CHECK(access.memoryOffset == -2);
        sawAdjustedStore = true;
        break;
    }

    CHECK(sawStoreBaseAdvance);
    CHECK(sawAdjustedStore);
}

TEST_CASE("VuSchedulerAnalysis: generic software pipeline skips loops with local Q latency hidden")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("add.xyz vf10, vf10, vf00"));
    REQUIRE(program.parse("add.xyz vf11, vf11, vf00"));
    REQUIRE(program.parse("add.xyz vf12, vf12, vf00"));
    REQUIRE(program.parse("add.xyz vf13, vf13, vf00"));
    REQUIRE(program.parse("add.xyz vf14, vf14, vf00"));
    REQUIRE(program.parse("add.xyz vf15, vf15, vf00"));
    REQUIRE(program.parse("add.xyz vf16, vf16, vf00"));
    REQUIRE(program.parse("mulq.xyz vf02, vf03, q"));
    REQUIRE(program.parse("add.xyz vf20, vf20, vf00"));
    REQUIRE(program.parse("add.xyz vf21, vf21, vf00"));
    REQUIRE(program.parse("add.xyz vf22, vf22, vf00"));
    REQUIRE(program.parse("add.xyz vf23, vf23, vf00"));
    REQUIRE(program.parse("add.xyz vf24, vf24, vf00"));
    REQUIRE(program.parse("add.xyz vf25, vf25, vf00"));
    REQUIRE(program.parse("add.xyz vf26, vf26, vf00"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].eligibleSingleQSoftwarePipeline);
    CHECK(opportunities[0].hasSoftwarePipelinePlan);
    CHECK(!opportunities[0].canEmitSoftwarePipeline);
    CHECK(opportunities[0].qProducerConsumerGapDeficitCycles == 0u);
    CHECK(opportunities[0].qSchedulingStrategy == vcl::VU_LOOP_Q_SCHEDULE_LOCAL);
    CHECK(hasString(opportunities[0].softwarePipelineBlockers, "q_latency_already_local"));

    std::vector<vcl::VuSoftwarePipelineRewritePlan> plans = vcl::buildVuSoftwarePipelineRewritePlans(program.tokenizer.tokens());
    CHECK(plans.empty());
}

TEST_CASE("VuSchedulerAnalysis: generic software pipeline reports insufficient Q latency hiding work")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("mulq.xyz vf02, vf03, q"));
    REQUIRE(program.parse("add.xyz vf10, vf10, vf00"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(!opportunities[0].eligibleSingleQSoftwarePipeline);
    CHECK(!opportunities[0].hasSoftwarePipelinePlan);
    CHECK(!opportunities[0].canEmitSoftwarePipeline);
    CHECK(opportunities[0].qProducerConsumerGapDeficitCycles == 7u);
    CHECK(opportunities[0].loopCarriedQGapCycles < opportunities[0].qProducerLatency);
    CHECK(opportunities[0].qSchedulingStrategy == vcl::VU_LOOP_Q_SCHEDULE_INSUFFICIENT);
    CHECK(hasString(opportunities[0].softwarePipelineBlockers, "insufficient_independent_cycles"));
    CHECK(hasString(opportunities[0].softwarePipelineBlockers, "insufficient_loop_carried_q_gap"));

    std::vector<vcl::VuSoftwarePipelineRewritePlan> plans = vcl::buildVuSoftwarePipelineRewritePlans(program.tokenizer.tokens());
    CHECK(plans.empty());
}

TEST_CASE("VuSchedulerAnalysis: generic software pipeline rewrites next-iteration load prefetches")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("lq.xyz vf01, 0(vi01)"));
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("mulq.xyz vf02, vf03, q"));
    REQUIRE(program.parse("add.xyz vf10, vf10, vf00"));
    REQUIRE(program.parse("add.xyz vf11, vf11, vf00"));
    REQUIRE(program.parse("add.xyz vf12, vf12, vf00"));
    REQUIRE(program.parse("add.xyz vf13, vf13, vf00"));
    REQUIRE(program.parse("add.xyz vf14, vf14, vf00"));
    REQUIRE(program.parse("add.xyz vf15, vf15, vf00"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].eligibleSingleQSoftwarePipeline);
    CHECK(opportunities[0].hasSoftwarePipelinePlan);
    CHECK(opportunities[0].canEmitSoftwarePipeline);
    CHECK(!hasString(opportunities[0].softwarePipelineBlockers, "multi_instruction_prefetch"));
    CHECK(!hasString(opportunities[0].softwarePipelineBlockers, "multi_instruction_prefetch_memory"));
    CHECK(!hasString(opportunities[0].softwarePipelineBlockers, "multi_instruction_prefetch_reads_induction"));
    const vcl::VuSoftwarePipelinePrefetch* lqPrefetch = findPrefetch(opportunities[0].softwarePipelinePrefetches, 2u);
    REQUIRE(lqPrefetch != NULL);
    CHECK(lqPrefetch->hasNextIterationOffset);
    CHECK(lqPrefetch->nextIterationOffset == 1);

    std::vector<vcl::VuSoftwarePipelineRewritePlan> plans = vcl::buildVuSoftwarePipelineRewritePlans(program.tokenizer.tokens());
    REQUIRE(plans.size() == 1u);
    REQUIRE(plans[0].prefetchTokenIndices.size() == 1u);
    CHECK(plans[0].prefetchTokenIndices[0] == 2u);
    CHECK(plans[0].qProducerInsertAfterTokenIndex == 12u);
    CHECK(plans[0].qProducerInBranchDelaySlot);

    std::list<vcl::Token> transformed = vcl::applyVuSoftwarePipelinePlans(program.tokenizer.tokens());
    unsigned int lqOffsetZeroCount = 0;
    unsigned int lqOffsetOneCount = 0;
    unsigned int branchDelayDivCount = 0;
    for (std::list<vcl::Token>::const_iterator i = transformed.begin(); i != transformed.end(); ++i)
    {
        const std::string mnemonic = vcl::normalizeVuMnemonic(i->name());
        if (mnemonic == "div" && (i->flags() & vcl::Token::BRANCH_DELAY_FILLER))
            ++branchDelayDivCount;
        if (mnemonic != "lq")
            continue;
        vcl::VuTokenResourceAccess access;
        REQUIRE(vcl::buildVuTokenResourceAccess(*i, access));
        if (access.hasMemoryOffset && access.memoryOffset == 0)
            ++lqOffsetZeroCount;
        if (access.hasMemoryOffset && access.memoryOffset == 1)
            ++lqOffsetOneCount;
    }
    CHECK(lqOffsetZeroCount == 1u);
    CHECK(lqOffsetOneCount == 1u);
    CHECK(branchDelayDivCount == 1u);
}

TEST_CASE("VuSchedulerAnalysis: generic software pipeline reports suffix store drain candidates")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("lq.xyz vf01, 0(vi01)"));
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("mulq.xyz vf02, vf03, q"));
    REQUIRE(program.parse("sq.xyz vf02, 0(vi03)"));
    REQUIRE(program.parse("add.xyz vf10, vf10, vf00"));
    REQUIRE(program.parse("add.xyz vf11, vf11, vf00"));
    REQUIRE(program.parse("add.xyz vf12, vf12, vf00"));
    REQUIRE(program.parse("add.xyz vf13, vf13, vf00"));
    REQUIRE(program.parse("add.xyz vf14, vf14, vf00"));
    REQUIRE(program.parse("add.xyz vf15, vf15, vf00"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("iaddiu vi03, vi03, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    REQUIRE(opportunities[0].softwarePipelineSuffixStores.size() == 1u);

    const vcl::VuSoftwarePipelineSuffixStore* store =
        findSuffixStore(opportunities[0].softwarePipelineSuffixStores, 5u);
    REQUIRE(store != NULL);
    CHECK(store->mnemonic == "sq");
    CHECK(store->hasMemoryBase);
    CHECK(store->memoryBaseRegister == "VI03");
    CHECK(store->hasMemoryOffset);
    CHECK(store->memoryOffset == 0);
    CHECK(store->usesInductionRegister);
    CHECK(store->inductionRegister == "VI03");
    CHECK(store->hasNextIterationOffset);
    CHECK(store->nextIterationOffset == -1);
    CHECK(store->hasStoredValueRegister);
    CHECK(store->storedValueRegister == "VF02");
    CHECK(hasString(store->storedValueFields, "x"));
    CHECK(hasString(store->storedValueFields, "y"));
    CHECK(hasString(store->storedValueFields, "z"));
    CHECK(!hasString(store->storedValueFields, "w"));
    CHECK(store->drainCandidate);

    std::vector<vcl::VuSoftwarePipelineRewritePlan> plans =
        vcl::buildVuSoftwarePipelineRewritePlans(program.tokenizer.tokens());
    REQUIRE(plans.size() == 1u);
    REQUIRE(plans[0].suffixStores.size() == 1u);
    CHECK(plans[0].suffixStores[0].tokenIndex == store->tokenIndex);
    CHECK(plans[0].suffixStores[0].storedValueRegister == "VF02");
    CHECK(plans[0].suffixStores[0].drainCandidate);
}

TEST_CASE("VuSchedulerAnalysis: generic software pipeline emits delayed suffix store drains")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("lq.xyz vf01, 0(vi01)"));
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("mulq.xyz vf02, vf03, q"));
    REQUIRE(program.parse("sq.xyz vf02, 0(vi03)"));
    REQUIRE(program.parse("add.xyz vf10, vf10, vf00"));
    REQUIRE(program.parse("add.xyz vf11, vf11, vf00"));
    REQUIRE(program.parse("add.xyz vf12, vf12, vf00"));
    REQUIRE(program.parse("add.xyz vf13, vf13, vf00"));
    REQUIRE(program.parse("add.xyz vf14, vf14, vf00"));
    REQUIRE(program.parse("add.xyz vf15, vf15, vf00"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("iaddiu vi03, vi03, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities =
        vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].canEmitSoftwarePipeline);
    CHECK(opportunities[0].hasSuffixStoreDrainPlan);
    CHECK(opportunities[0].canEmitSuffixStoreDrain);
    REQUIRE(opportunities[0].softwarePipelineSuffixStores.size() == 1u);
    CHECK(opportunities[0].softwarePipelineSuffixStores[0].delayedDrain);
    CHECK(!opportunities[0].softwarePipelineSuffixStores[0].requiresValueRotation);

    std::vector<vcl::VuSoftwarePipelineRewritePlan> plans =
        vcl::buildVuSoftwarePipelineRewritePlans(program.tokenizer.tokens());
    REQUIRE(plans.size() == 1u);
    CHECK(plans[0].drainsSuffixStores);

    std::list<vcl::Token> transformed = vcl::applyVuSoftwarePipelinePlans(program.tokenizer.tokens());

    unsigned int prologLabels = 0;
    unsigned int mainLabels = 0;
    unsigned int drainLabels = 0;
    unsigned int storeCount = 0;
    bool sawInvertedPrologBranch = false;
    bool inMainLoop = false;
    bool mainStoreBeforeConsumer = false;
    for (std::list<vcl::Token>::const_iterator i = transformed.begin(); i != transformed.end(); ++i)
    {
        if (i->label() == "loop_lid__PROLOG")
            ++prologLabels;
        if (i->label() == "loop_lid")
            ++mainLabels;
        if (i->label() == "loop_lid__DRAIN")
            ++drainLabels;

        const std::string mnemonic = vcl::normalizeVuMnemonic(i->name());
        if (mnemonic == "ibeq")
        {
            if (tokenBranchesTo(*i, "loop_lid__DRAIN"))
                sawInvertedPrologBranch = true;
        }
        if (i->label() == "loop_lid")
            inMainLoop = true;
        if (inMainLoop && mnemonic == "sq")
        {
            vcl::VuTokenResourceAccess access;
            REQUIRE(vcl::buildVuTokenResourceAccess(*i, access));
            CHECK(access.hasMemoryOffset);
            CHECK(access.memoryOffset == -1);
            ++storeCount;
            mainStoreBeforeConsumer = true;
        }
        if (inMainLoop && mnemonic == "mulq")
            CHECK(mainStoreBeforeConsumer);
    }

    CHECK(prologLabels == 1u);
    CHECK(mainLabels == 1u);
    CHECK(drainLabels == 1u);
    CHECK(storeCount == 2u);
    CHECK(sawInvertedPrologBranch);
}

TEST_CASE("VuSchedulerAnalysis: delayed suffix store drains rotate overwritten values")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("lq.xyz vf01, 0(vi01)"));
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("mulq.xyz vf02, vf03, q"));
    REQUIRE(program.parse("sq.xyz vf02, 0(vi03)"));
    REQUIRE(program.parse("add.xyz vf02, vf10, vf00"));
    REQUIRE(program.parse("add.xyz vf11, vf11, vf00"));
    REQUIRE(program.parse("add.xyz vf12, vf12, vf00"));
    REQUIRE(program.parse("add.xyz vf13, vf13, vf00"));
    REQUIRE(program.parse("add.xyz vf14, vf14, vf00"));
    REQUIRE(program.parse("add.xyz vf15, vf15, vf00"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("iaddiu vi03, vi03, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities =
        vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].canEmitSuffixStoreDrain);
    REQUIRE(opportunities[0].softwarePipelineSuffixStores.size() == 1u);
    CHECK(opportunities[0].softwarePipelineSuffixStores[0].requiresValueRotation);
    CHECK(opportunities[0].softwarePipelineSuffixStores[0].hasValueScratchRegister);
    CHECK(opportunities[0].softwarePipelineSuffixStores[0].rotateValueAtStore);

    std::list<vcl::Token> transformed = vcl::applyVuSoftwarePipelinePlans(program.tokenizer.tokens());
    bool sawValueMove = false;
    bool sawAdjustedStore = false;
    for (std::list<vcl::Token>::const_iterator i = transformed.begin(); i != transformed.end(); ++i)
    {
        vcl::VuTokenResourceAccess access;
        if (!vcl::buildVuTokenResourceAccess(*i, access))
            continue;
        const std::string mnemonic = vcl::normalizeVuMnemonic(i->name());
        if (mnemonic == "move"
            && hasString(access.registerReads, "VF02.x")
            && hasString(access.registerWrites, "VF31.x"))
            sawValueMove = true;
        if (mnemonic == "sq"
            && hasString(access.registerReads, "VF31.x")
            && access.hasMemoryOffset
            && access.memoryOffset == -1)
            sawAdjustedStore = true;
    }

    CHECK(sawValueMove);
    CHECK(sawAdjustedStore);
}

TEST_CASE("VuSchedulerAnalysis: generic software pipeline prefers Q producers over ordinary branch-delay suffix fillers")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("lq.xyz vf01, 0(vi01)"));
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("mulq.xyz vf02, vf03, q"));
    REQUIRE(program.parse("add.xyz vf10, vf10, vf00"));
    REQUIRE(program.parse("add.xyz vf11, vf11, vf00"));
    REQUIRE(program.parse("add.xyz vf12, vf12, vf00"));
    REQUIRE(program.parse("add.xyz vf13, vf13, vf00"));
    REQUIRE(program.parse("add.xyz vf14, vf14, vf00"));
    REQUIRE(program.parse("add.xyz vf15, vf15, vf00"));
    REQUIRE(program.parse("iaddiu vi03, vi03, 1"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuSoftwarePipelineRewritePlan> plans = vcl::buildVuSoftwarePipelineRewritePlans(program.tokenizer.tokens());
    REQUIRE(plans.size() == 1u);
    CHECK(plans[0].qProducerInsertAfterTokenIndex == 13u);
    CHECK(plans[0].qProducerInBranchDelaySlot);
    CHECK(!plans[0].qProducerBranchDelayBlockedBySuffixDependency);
    CHECK(plans[0].qProducerBranchDelaySuffixBlockerTokenIndex == 0u);

    std::list<vcl::Token> transformed = vcl::applyVuSoftwarePipelinePlans(program.tokenizer.tokens());
    bool sawOrdinarySuffixBeforeBranch = false;
    bool sawBranch = false;
    bool sawBranchDelayDiv = false;
    for (std::list<vcl::Token>::const_iterator i = transformed.begin(); i != transformed.end(); ++i)
    {
        const std::string mnemonic = vcl::normalizeVuMnemonic(i->name());
        if (!sawBranch && mnemonic == "iaddiu")
        {
            vcl::VuTokenResourceAccess access;
            REQUIRE(vcl::buildVuTokenResourceAccess(*i, access));
            if (hasString(access.registerWrites, "VI03"))
                sawOrdinarySuffixBeforeBranch = true;
        }
        if (mnemonic == "ibne")
            sawBranch = true;
        if (sawBranch && mnemonic == "div" && (i->flags() & vcl::Token::BRANCH_DELAY_FILLER))
            sawBranchDelayDiv = true;
    }
    CHECK(sawOrdinarySuffixBeforeBranch);
    CHECK(sawBranchDelayDiv);
}

TEST_CASE("VuSchedulerAnalysis: generic software pipeline reports suffix dependencies that block Q branch-delay placement")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("lq.xyz vf01, 0(vi01)"));
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("mulq.xyz vf02, vf03, q"));
    REQUIRE(program.parse("add.w vf01, vf04, vf00"));
    REQUIRE(program.parse("add.xyz vf10, vf10, vf00"));
    REQUIRE(program.parse("add.xyz vf11, vf11, vf00"));
    REQUIRE(program.parse("add.xyz vf12, vf12, vf00"));
    REQUIRE(program.parse("add.xyz vf13, vf13, vf00"));
    REQUIRE(program.parse("add.xyz vf14, vf14, vf00"));
    REQUIRE(program.parse("add.xyz vf15, vf15, vf00"));
    REQUIRE(program.parse("iaddiu vi03, vi03, 1"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuSoftwarePipelineRewritePlan> plans = vcl::buildVuSoftwarePipelineRewritePlans(program.tokenizer.tokens());
    REQUIRE(plans.size() == 1u);
    CHECK(plans[0].qProducerInsertAfterTokenIndex == 4u);
    CHECK(!plans[0].qProducerInBranchDelaySlot);
    CHECK(plans[0].qProducerBranchDelayBlockedBySuffixDependency);
    CHECK(plans[0].qProducerBranchDelaySuffixBlockerTokenIndex == 5u);
}

TEST_CASE("VuSchedulerAnalysis: generic software pipeline rewrites simple rotated registers")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("lq.xyz vf01, 0(vi01)"));
    REQUIRE(program.parse("mul.xyz vf03, vf01, vf02"));
    REQUIRE(program.parse("div q, vf00[w], vf03[w]"));
    REQUIRE(program.parse("mulq.xyz vf03, vf03, q"));
    REQUIRE(program.parse("sq.xyz vf03, 0(vi02)"));
    REQUIRE(program.parse("add.xyz vf10, vf10, vf00"));
    REQUIRE(program.parse("add.xyz vf11, vf11, vf00"));
    REQUIRE(program.parse("add.xyz vf12, vf12, vf00"));
    REQUIRE(program.parse("add.xyz vf13, vf13, vf00"));
    REQUIRE(program.parse("add.xyz vf14, vf14, vf00"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("ibne vi01, vi04, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].eligibleSingleQSoftwarePipeline);
    CHECK(opportunities[0].hasSoftwarePipelinePlan);
    CHECK(opportunities[0].canEmitSoftwarePipeline);
    CHECK(!hasString(opportunities[0].softwarePipelineBlockers, "requires_register_rotation"));
    const vcl::VuSoftwarePipelineRotation* vf03Rotation = findRotation(opportunities[0].softwarePipelineRotations, "VF03");
    REQUIRE(vf03Rotation != NULL);
    CHECK(vf03Rotation->hasScratchRegister);
    CHECK(vf03Rotation->scratchRegister == "VF31");

    std::list<vcl::Token> transformed = vcl::applyVuSoftwarePipelinePlans(program.tokenizer.tokens());
    unsigned int mulWritesScratch = 0;
    unsigned int divReadsScratch = 0;
    unsigned int branchDelayDivCount = 0;
    unsigned int moveScratchToOriginal = 0;
    for (std::list<vcl::Token>::const_iterator i = transformed.begin(); i != transformed.end(); ++i)
    {
        if (!i->operand() || i->operand()->isPreprocessor())
            continue;
        vcl::VuTokenResourceAccess access;
        REQUIRE(vcl::buildVuTokenResourceAccess(*i, access));
        const std::string mnemonic = vcl::normalizeVuMnemonic(i->name());
        if (mnemonic == "mul" && hasString(access.registerWrites, "VF31.x"))
            ++mulWritesScratch;
        if (mnemonic == "div" && (i->flags() & vcl::Token::BRANCH_DELAY_FILLER))
            ++branchDelayDivCount;
        if (mnemonic == "div" && hasString(access.registerReads, "VF31.w"))
            ++divReadsScratch;
        if (mnemonic == "move"
            && hasString(access.registerReads, "VF31.x")
            && hasString(access.registerWrites, "VF03.x"))
            ++moveScratchToOriginal;
    }

    CHECK(mulWritesScratch == 1u);
    CHECK(divReadsScratch == 1u);
    CHECK(branchDelayDivCount == 1u);
    CHECK(moveScratchToOriginal == 1u);
}

TEST_CASE("VuSchedulerAnalysis: generic software pipeline rewrites multiple rotated Q consumers")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("lq.xy vf03, 0(vi01)"));
    REQUIRE(program.parse("lq.xy vf05, 1(vi01)"));
    REQUIRE(program.parse("div q, vf00[w], vf00[w]"));
    REQUIRE(program.parse("mulq.xy vf03, vf03, q"));
    REQUIRE(program.parse("mulq.xy vf05, vf05, q"));
    REQUIRE(program.parse("add.xy vf20, vf21, vf22"));
    REQUIRE(program.parse("sub.xy vf23, vf24, vf25"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 2"));
    REQUIRE(program.parse("iaddiu vi02, vi02, 2"));
    REQUIRE(program.parse("ibne vi01, vi03, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].eligibleSingleQSoftwarePipeline);
    CHECK(opportunities[0].hasSoftwarePipelinePlan);
    CHECK(opportunities[0].canEmitSoftwarePipeline);
    CHECK(!hasString(opportunities[0].softwarePipelineBlockers, "requires_register_rotation"));
    CHECK(opportunities[0].softwarePipelineRotations.size() == 2u);
    CHECK(findRotation(opportunities[0].softwarePipelineRotations, "VF03") != NULL);
    CHECK(findRotation(opportunities[0].softwarePipelineRotations, "VF05") != NULL);

    std::list<vcl::Token> transformed = vcl::applyVuSoftwarePipelinePlans(program.tokenizer.tokens());
    unsigned int moveCount = 0;
    for (std::list<vcl::Token>::const_iterator i = transformed.begin(); i != transformed.end(); ++i)
    {
        if (vcl::normalizeVuMnemonic(i->name()) == "move")
            ++moveCount;
    }
    CHECK(moveCount == 2u);
}

TEST_CASE("VuSchedulerAnalysis: generic software pipeline matches alias rotation bases")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("lq.xy temp_a, 0(vi01)"));
    REQUIRE(program.parse("div q, vf00[w], vf00[w]"));
    REQUIRE(program.parse("mulq.xy temp_a, temp_a, q"));
    REQUIRE(program.parse("add.xy vf20, vf21, vf22"));
    REQUIRE(program.parse("sub.xy vf23, vf24, vf25"));
    REQUIRE(program.parse("max.xy vf26, vf27, vf28"));
    REQUIRE(program.parse("mini.xy vf29, vf30, vf31"));
    REQUIRE(program.parse("add.xy vf16, vf17, vf18"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].eligibleSingleQSoftwarePipeline);
    CHECK(opportunities[0].hasSoftwarePipelinePlan);
    CHECK(opportunities[0].canEmitSoftwarePipeline);
    CHECK(!hasString(opportunities[0].softwarePipelineBlockers, "requires_register_rotation"));
    const vcl::VuSoftwarePipelineRotation* rotation = findRotation(opportunities[0].softwarePipelineRotations, "temp_a");
    REQUIRE(rotation != NULL);
    CHECK(rotation->hasScratchRegister);

    std::list<vcl::Token> transformed = vcl::applyVuSoftwarePipelinePlans(program.tokenizer.tokens());
    bool sawAliasRestoreMove = false;
    bool sawInvalidVf00RestoreMove = false;
    for (std::list<vcl::Token>::const_iterator i = transformed.begin(); i != transformed.end(); ++i)
    {
        if (vcl::normalizeVuMnemonic(i->name()) != "move")
            continue;

        vcl::VuTokenResourceAccess access;
        REQUIRE(vcl::buildVuTokenResourceAccess(*i, access));
        if (hasString(access.registerReads, rotation->scratchRegister + ".x")
            && hasString(access.registerWrites, "temp_a.x"))
            sawAliasRestoreMove = true;
        if (hasString(access.registerReads, rotation->scratchRegister + ".x")
            && hasString(access.registerWrites, "VF00.x"))
            sawInvalidVf00RestoreMove = true;
    }

    CHECK(sawAliasRestoreMove);
    CHECK(!sawInvalidVf00RestoreMove);
}

TEST_CASE("VuSchedulerAnalysis: generic software pipeline ignores non-prefetched Q consumer rotations")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("lq.xy vf03, 0(vi01)"));
    REQUIRE(program.parse("div q, vf00[w], vf00[w]"));
    REQUIRE(program.parse("mulq.xy vf03, vf03, q"));
    REQUIRE(program.parse("lq.xy vf05, 1(vi01)"));
    REQUIRE(program.parse("mulq.xy vf05, vf05, q"));
    REQUIRE(program.parse("add.xy vf20, vf21, vf22"));
    REQUIRE(program.parse("sub.xy vf23, vf24, vf25"));
    REQUIRE(program.parse("max.xy vf26, vf27, vf28"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 2"));
    REQUIRE(program.parse("iaddiu vi02, vi02, 2"));
    REQUIRE(program.parse("ibne vi01, vi03, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].eligibleSingleQSoftwarePipeline);
    CHECK(opportunities[0].hasSoftwarePipelinePlan);
    CHECK(opportunities[0].canEmitSoftwarePipeline);
    CHECK(opportunities[0].softwarePipelineRotations.size() == 1u);
    CHECK(findRotation(opportunities[0].softwarePipelineRotations, "VF03") != NULL);
    CHECK(findRotation(opportunities[0].softwarePipelineRotations, "VF05") == NULL);
}

TEST_CASE("VuSchedulerAnalysis: multi-instruction prefetch reports suffix clobber blockers")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("lq.xyz vf01, 0(vi01)"));
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("mulq.xyz vf02, vf03, q"));
    REQUIRE(program.parse("add.xyz vf10, vf01, vf00"));
    REQUIRE(program.parse("add.xyz vf11, vf11, vf00"));
    REQUIRE(program.parse("add.xyz vf12, vf12, vf00"));
    REQUIRE(program.parse("add.xyz vf13, vf13, vf00"));
    REQUIRE(program.parse("add.xyz vf14, vf14, vf00"));
    REQUIRE(program.parse("add.xyz vf15, vf15, vf00"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].eligibleSingleQSoftwarePipeline);
    CHECK(opportunities[0].hasSoftwarePipelinePlan);
    CHECK(!opportunities[0].canEmitSoftwarePipeline);
    CHECK(hasString(opportunities[0].softwarePipelineBlockers, "multi_instruction_prefetch"));
    CHECK(hasString(opportunities[0].softwarePipelineBlockers, "multi_instruction_prefetch_memory"));
    CHECK(hasString(opportunities[0].softwarePipelineBlockers, "multi_instruction_prefetch_reads_induction"));
    CHECK(hasString(opportunities[0].softwarePipelineBlockers, "prefetch_clobbers_suffix"));
}

TEST_CASE("VuSchedulerAnalysis: generic software pipeline rotates suffix store values before clobbering prefetches")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("lq.xyz vf01, 0(vi01)"));
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("mulq.xyz vf02, vf03, q"));
    REQUIRE(program.parse("sq.xyz vf01, 0(vi03)"));
    REQUIRE(program.parse("add.xyz vf10, vf10, vf00"));
    REQUIRE(program.parse("add.xyz vf11, vf11, vf00"));
    REQUIRE(program.parse("add.xyz vf12, vf12, vf00"));
    REQUIRE(program.parse("add.xyz vf13, vf13, vf00"));
    REQUIRE(program.parse("add.xyz vf14, vf14, vf00"));
    REQUIRE(program.parse("add.xyz vf15, vf15, vf00"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("iaddiu vi03, vi03, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].canEmitSoftwarePipeline);
    CHECK(!hasString(opportunities[0].softwarePipelineBlockers, "prefetch_clobbers_suffix"));
    REQUIRE(opportunities[0].softwarePipelineSuffixStores.size() == 1u);
    CHECK(opportunities[0].softwarePipelineSuffixStores[0].requiresValueRotation);
    CHECK(opportunities[0].softwarePipelineSuffixStores[0].hasValueScratchRegister);
    CHECK(opportunities[0].softwarePipelineSuffixStores[0].valueScratchRegister == "VF31");

    std::list<vcl::Token> transformed = vcl::applyVuSoftwarePipelinePlans(program.tokenizer.tokens());

    bool sawValueMove = false;
    bool sawAdjustedStore = false;
    bool sawPrefetchAfterValueMove = false;
    for (std::list<vcl::Token>::const_iterator i = transformed.begin(); i != transformed.end(); ++i)
    {
        const std::string mnemonic = vcl::normalizeVuMnemonic(i->name());
        vcl::VuTokenResourceAccess access;
        if (!vcl::buildVuTokenResourceAccess(*i, access))
            continue;

        if (mnemonic == "move"
            && hasString(access.registerReads, "VF01.x")
            && hasString(access.registerWrites, "VF31.x"))
            sawValueMove = true;
        if (sawValueMove
            && mnemonic == "lq"
            && hasString(access.registerWrites, "VF01.x"))
            sawPrefetchAfterValueMove = true;
        if (mnemonic == "sq"
            && hasString(access.registerReads, "VF31.x")
            && access.hasMemoryBase
            && access.memoryBaseRegister == "VI03")
            sawAdjustedStore = true;
    }

    CHECK(sawValueMove);
    CHECK(sawPrefetchAfterValueMove);
    CHECK(sawAdjustedStore);
}

TEST_CASE("VuSchedulerAnalysis: generic software pipeline emits simple Q live-out drain")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("mulq.xyz vf02, vf03, q"));
    REQUIRE(program.parse("add.xyz vf10, vf10, vf00"));
    REQUIRE(program.parse("add.xyz vf11, vf11, vf00"));
    REQUIRE(program.parse("add.xyz vf12, vf12, vf00"));
    REQUIRE(program.parse("add.xyz vf13, vf13, vf00"));
    REQUIRE(program.parse("add.xyz vf14, vf14, vf00"));
    REQUIRE(program.parse("add.xyz vf15, vf15, vf00"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));
    REQUIRE(program.parse("after_lid:"));
    REQUIRE(program.parse("mulq.xyz vf04, vf05, q"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].hasSoftwarePipelinePlan);
    CHECK(opportunities[0].qLiveOut);
    CHECK(opportunities[0].canEmitSoftwarePipeline);
    CHECK(!hasString(opportunities[0].softwarePipelineBlockers, "q_live_out"));

    std::vector<vcl::VuSoftwarePipelineRewritePlan> plans = vcl::buildVuSoftwarePipelineRewritePlans(program.tokenizer.tokens());
    REQUIRE(plans.size() == 1u);
    CHECK(plans[0].emitsDrain);

    std::list<vcl::Token> transformed = vcl::applyVuSoftwarePipelinePlans(program.tokenizer.tokens());

    unsigned int prologLabels = 0;
    unsigned int drainLabels = 0;
    unsigned int divCount = 0;
    for (std::list<vcl::Token>::const_iterator i = transformed.begin(); i != transformed.end(); ++i)
    {
        if (i->label() == "loop_lid__PROLOG")
            ++prologLabels;
        if (i->label() == "loop_lid__DRAIN")
            ++drainLabels;
        if (vcl::normalizeVuMnemonic(i->name()) == "div")
            ++divCount;
    }

    CHECK(prologLabels == 1u);
    CHECK(drainLabels == 1u);
    CHECK(divCount == 3u);
    CHECK(transformed.size() == program.tokenizer.tokens().size() + 3u);
}

TEST_CASE("VuSchedulerAnalysis: generic software pipeline blocks non-drainable Q live-out loops")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("lq.xyz vf01, 0(vi01)"));
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("mulq.xyz vf02, vf03, q"));
    REQUIRE(program.parse("add.xyz vf10, vf10, vf00"));
    REQUIRE(program.parse("add.xyz vf11, vf11, vf00"));
    REQUIRE(program.parse("add.xyz vf12, vf12, vf00"));
    REQUIRE(program.parse("add.xyz vf13, vf13, vf00"));
    REQUIRE(program.parse("add.xyz vf14, vf14, vf00"));
    REQUIRE(program.parse("add.xyz vf15, vf15, vf00"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));
    REQUIRE(program.parse("after_lid:"));
    REQUIRE(program.parse("mulq.xyz vf04, vf05, q"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].hasSoftwarePipelinePlan);
    CHECK(opportunities[0].qLiveOut);
    CHECK(!opportunities[0].canEmitSoftwarePipeline);
    CHECK(hasString(opportunities[0].softwarePipelineBlockers, "q_live_out"));

    std::vector<vcl::VuSoftwarePipelineRewritePlan> plans = vcl::buildVuSoftwarePipelineRewritePlans(program.tokenizer.tokens());
    CHECK(plans.empty());
}

TEST_CASE("VuSchedulerAnalysis: pipeline opportunities expose all Q producers")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("mulq.xyz vf02, vf03, q"));
    REQUIRE(program.parse("add.xyz vf10, vf10, vf00"));
    REQUIRE(program.parse("div q, vf00[w], vf04[w]"));
    REQUIRE(program.parse("mulq.xyz vf05, vf06, q"));
    REQUIRE(program.parse("add.xyz vf11, vf11, vf00"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(!opportunities[0].hasSingleQProducer);
    CHECK(opportunities[0].qProducerTokenIndex == 5u);
    REQUIRE(opportunities[0].qProducerTokenIndices.size() == 2u);
    CHECK(opportunities[0].qProducerTokenIndices[0] == 2u);
    CHECK(opportunities[0].qProducerTokenIndices[1] == 5u);
    REQUIRE(opportunities[0].qStages.size() == 2u);
    CHECK(opportunities[0].qStages[0].qProducerTokenIndex == 2u);
    REQUIRE(opportunities[0].qStages[0].qConsumerTokenIndices.size() == 1u);
    CHECK(opportunities[0].qStages[0].qConsumerTokenIndices[0] == 3u);
    CHECK(opportunities[0].qStages[0].qProducerLatency == 7u);
    CHECK(opportunities[0].qStages[0].qProducerConsumerGapCycles == 0u);
    CHECK(opportunities[0].qStages[0].qProducerConsumerGapDeficitCycles == 7u);
    CHECK(opportunities[0].qStages[0].loopCarriedQGapCycles == 6u);
    CHECK(opportunities[0].qStages[0].qProducerInsertionGapCycles == 6u);
    CHECK(opportunities[0].qStages[0].qProducerInsertionGapDeficitCycles == 1u);
    CHECK(opportunities[0].qStages[0].qSchedulingStrategy == vcl::VU_LOOP_Q_SCHEDULE_INSUFFICIENT);
    CHECK(opportunities[0].qStages[1].qProducerTokenIndex == 5u);
    REQUIRE(opportunities[0].qStages[1].qConsumerTokenIndices.size() == 1u);
    CHECK(opportunities[0].qStages[1].qConsumerTokenIndices[0] == 6u);
    CHECK(opportunities[0].qStages[1].qProducerLatency == 7u);
    CHECK(opportunities[0].qStages[1].qProducerConsumerGapCycles == 0u);
    CHECK(opportunities[0].qStages[1].qProducerConsumerGapDeficitCycles == 7u);
    CHECK(opportunities[0].qStages[1].loopCarriedQGapCycles == 6u);
    CHECK(opportunities[0].qStages[1].qProducerInsertionGapCycles == 6u);
    CHECK(opportunities[0].qStages[1].qProducerInsertionGapDeficitCycles == 1u);
    CHECK(opportunities[0].qStages[1].qSchedulingStrategy == vcl::VU_LOOP_Q_SCHEDULE_INSUFFICIENT);
    CHECK(opportunities[0].firstQConsumerTokenIndex == 3u);
    CHECK(opportunities[0].lastQConsumerTokenIndex == 6u);
    REQUIRE(opportunities[0].qConsumerTokenIndices.size() == 2u);
    CHECK(opportunities[0].qConsumerTokenIndices[0] == 3u);
    CHECK(opportunities[0].qConsumerTokenIndices[1] == 6u);
    CHECK(hasString(opportunities[0].softwarePipelineBlockers, "multiple_q_producers"));
}

TEST_CASE("VuSchedulerAnalysis: multi-Q stages contribute carried register sets")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("add.xyz vf20, vf21, vf22"));
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("mulq.xyz vf20, vf20, q"));
    REQUIRE(program.parse("add.xyz vf30, vf20, vf00"));
    REQUIRE(program.parse("add.xyz vf24, vf25, vf26"));
    REQUIRE(program.parse("div q, vf00[w], vf04[w]"));
    REQUIRE(program.parse("mulq.xyz vf24, vf24, q"));
    REQUIRE(program.parse("add.xyz vf31, vf24, vf00"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    REQUIRE(opportunities[0].qStages.size() == 2u);
    CHECK(hasString(opportunities[0].carriedQInputRegisters, "VF20.x"));
    CHECK(hasString(opportunities[0].carriedQInputRegisters, "VF24.x"));
    CHECK(hasString(opportunities[0].carriedQOutputRegisters, "VF20.x"));
    CHECK(hasString(opportunities[0].carriedQOutputRegisters, "VF24.x"));
}

TEST_CASE("VuSchedulerAnalysis: multi-Q software pipeline can use producer-only cyclic prefixes")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("mulq.xyz vf02, vf03, q"));
    REQUIRE(program.parse("add.xyz vf10, vf10, vf00"));
    REQUIRE(program.parse("add.xyz vf11, vf11, vf00"));
    REQUIRE(program.parse("div q, vf00[w], vf04[w]"));
    REQUIRE(program.parse("add.xyz vf12, vf12, vf00"));
    REQUIRE(program.parse("add.xyz vf13, vf13, vf00"));
    REQUIRE(program.parse("mulq.xyz vf05, vf06, q"));
    REQUIRE(program.parse("add.xyz vf14, vf14, vf00"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities =
        vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(!opportunities[0].hasSingleQProducer);
    CHECK(opportunities[0].hasMultiQSoftwarePipelinePlan);
    CHECK(opportunities[0].eligibleMultiQSoftwarePipeline);
    CHECK(opportunities[0].canEmitMultiQSoftwarePipeline);
    CHECK(opportunities[0].multiQSoftwarePipelineBlockers.empty());
    REQUIRE(opportunities[0].multiQPrologTokenIndices.size() == 1u);
    CHECK(opportunities[0].multiQPrologTokenIndices[0] == 2u);
    REQUIRE(opportunities[0].multiQMainTokenIndices.size() == 10u);
    CHECK(opportunities[0].multiQMainTokenIndices.front() == 3u);
    CHECK(opportunities[0].multiQMainTokenIndices.back() == 12u);
    REQUIRE(opportunities[0].multiQCyclicPrefixTokenIndices.size() == 1u);
    CHECK(opportunities[0].multiQCyclicPrefixTokenIndices[0] == 2u);
    CHECK(opportunities[0].multiQCyclicPrefixInsertBeforeTokenIndex == 10u);

    std::vector<vcl::VuSoftwarePipelineRewritePlan> plans =
        vcl::buildVuSoftwarePipelineRewritePlans(program.tokenizer.tokens());
    REQUIRE(plans.size() == 1u);
    CHECK(plans[0].cyclicPrefixBeforeBranch);
    CHECK(plans[0].cyclicPrefixTokenIndices == opportunities[0].multiQCyclicPrefixTokenIndices);

    std::list<vcl::Token> transformed = vcl::applyVuSoftwarePipelinePlans(program.tokenizer.tokens());
    unsigned int prologLabels = 0;
    unsigned int mainLabels = 0;
    unsigned int divCount = 0;
    unsigned int mulqCount = 0;
    bool sawStageOneDivAfterStageTwoConsumer = false;
    bool sawStageTwoConsumer = false;
    for (std::list<vcl::Token>::const_iterator i = transformed.begin(); i != transformed.end(); ++i)
    {
        if (i->label() == "loop_lid__PROLOG")
            ++prologLabels;
        if (i->label() == "loop_lid")
            ++mainLabels;

        const std::string mnemonic = vcl::normalizeVuMnemonic(i->name());
        if (mnemonic == "div")
        {
            ++divCount;
            vcl::VuTokenResourceAccess access;
            REQUIRE(vcl::buildVuTokenResourceAccess(*i, access));
            if (sawStageTwoConsumer && hasString(access.registerReads, "VF01.w"))
                sawStageOneDivAfterStageTwoConsumer = true;
        }
        if (mnemonic == "mulq")
        {
            ++mulqCount;
            vcl::VuTokenResourceAccess access;
            REQUIRE(vcl::buildVuTokenResourceAccess(*i, access));
            if (hasString(access.registerWrites, "VF05.x"))
                sawStageTwoConsumer = true;
        }
        if (mnemonic == "ibne")
            CHECK(sawStageOneDivAfterStageTwoConsumer);
    }

    CHECK(prologLabels == 1u);
    CHECK(mainLabels == 1u);
    CHECK(divCount == 3u);
    CHECK(mulqCount == 2u);
    CHECK(sawStageOneDivAfterStageTwoConsumer);
}

TEST_CASE("VuSchedulerAnalysis: multi-Q software pipeline can rotate producer-side prefixes")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("add.xyz vf10, vf10, vf00"));
    REQUIRE(program.parse("add.xyz vf11, vf11, vf00"));
    REQUIRE(program.parse("add.xyz vf12, vf12, vf00"));
    REQUIRE(program.parse("add.xyz vf13, vf13, vf00"));
    REQUIRE(program.parse("add.xyz vf14, vf14, vf00"));
    REQUIRE(program.parse("add.xyz vf15, vf15, vf00"));
    REQUIRE(program.parse("add.xyz vf16, vf16, vf00"));
    REQUIRE(program.parse("mulq.xyz vf02, vf03, q"));
    REQUIRE(program.parse("div q, vf00[w], vf04[w]"));
    REQUIRE(program.parse("add.xyz vf17, vf17, vf00"));
    REQUIRE(program.parse("add.xyz vf18, vf18, vf00"));
    REQUIRE(program.parse("mulq.xyz vf05, vf06, q"));
    REQUIRE(program.parse("add.xyz vf19, vf19, vf00"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities =
        vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(!opportunities[0].hasSingleQProducer);
    CHECK(opportunities[0].hasMultiQSoftwarePipelinePlan);
    CHECK(opportunities[0].eligibleMultiQSoftwarePipeline);
    CHECK(opportunities[0].canEmitMultiQSoftwarePipeline);
    CHECK(opportunities[0].multiQSoftwarePipelineBlockers.empty());
    REQUIRE(opportunities[0].multiQPrologTokenIndices.size() == 8u);
    CHECK(opportunities[0].multiQPrologTokenIndices.front() == 2u);
    CHECK(opportunities[0].multiQPrologTokenIndices.back() == 9u);
    REQUIRE(opportunities[0].multiQMainTokenIndices.size() == 8u);
    CHECK(opportunities[0].multiQMainTokenIndices.front() == 10u);
    CHECK(opportunities[0].multiQMainTokenIndices.back() == 17u);
    CHECK(opportunities[0].multiQCyclicPrefixTokenIndices == opportunities[0].multiQPrologTokenIndices);
    CHECK(opportunities[0].multiQCyclicPrefixInsertBeforeTokenIndex == 16u);

    std::list<vcl::Token> transformed = vcl::applyVuSoftwarePipelinePlans(program.tokenizer.tokens());
    unsigned int divCount = 0;
    unsigned int mulqCount = 0;
    bool sawStageTwoConsumer = false;
    bool sawProducerPrefixBeforeBranch = false;
    for (std::list<vcl::Token>::const_iterator i = transformed.begin(); i != transformed.end(); ++i)
    {
        const std::string mnemonic = vcl::normalizeVuMnemonic(i->name());
        if (mnemonic == "div")
        {
            ++divCount;
            vcl::VuTokenResourceAccess access;
            REQUIRE(vcl::buildVuTokenResourceAccess(*i, access));
            if (sawStageTwoConsumer && hasString(access.registerReads, "VF01.w"))
                sawProducerPrefixBeforeBranch = true;
        }
        if (mnemonic == "mulq")
        {
            ++mulqCount;
            vcl::VuTokenResourceAccess access;
            REQUIRE(vcl::buildVuTokenResourceAccess(*i, access));
            if (hasString(access.registerWrites, "VF05.x"))
                sawStageTwoConsumer = true;
            if (sawProducerPrefixBeforeBranch)
                CHECK(!hasString(access.registerWrites, "VF02.x"));
        }
    }

    CHECK(divCount == 3u);
    CHECK(mulqCount == 2u);
    CHECK(sawProducerPrefixBeforeBranch);
}

TEST_CASE("VuSchedulerAnalysis: multi-Q cyclic prefixes can guard plain store side effects")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("add.xyz vf02, vf00, vf00"));
    REQUIRE(program.parse("sq.xyz vf02, 0(vi03)"));
    REQUIRE(program.parse("div q, vf00[w], vf00[w]"));
    REQUIRE(program.parse("mulq.xyz vf03, vf00, q"));
    REQUIRE(program.parse("add.xyz vf20, vf00, vf00"));
    REQUIRE(program.parse("add.xyz vf21, vf00, vf00"));
    REQUIRE(program.parse("div q, vf00[w], vf00[w]"));
    REQUIRE(program.parse("mulq.xyz vf06, vf00, q"));
    REQUIRE(program.parse("add.xyz vf22, vf00, vf00"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("iaddiu vi03, vi03, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities =
        vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].hasMultiQSoftwarePipelinePlan);
    CHECK(opportunities[0].canEmitMultiQSoftwarePipeline);
    CHECK(opportunities[0].multiQCyclicPrefixNeedsGuard);
    CHECK(opportunities[0].multiQSoftwarePipelineBlockers.empty());
    REQUIRE(!opportunities[0].drainTokenIndices.empty());
    CHECK(opportunities[0].drainTokenIndices.back() < opportunities[0].branchTokenIndex);

    std::vector<vcl::VuSoftwarePipelineRewritePlan> plans =
        vcl::buildVuSoftwarePipelineRewritePlans(program.tokenizer.tokens());
    REQUIRE(plans.size() == 1u);
    CHECK(plans[0].cyclicPrefixBeforeBranch);
    CHECK(plans[0].cyclicPrefixNeedsGuard);
    CHECK(plans[0].emitsDrain);
    CHECK(plans[0].drainTokenIndices == opportunities[0].drainTokenIndices);

    std::list<vcl::Token> transformed =
        vcl::applyVuSoftwarePipelinePlans(program.tokenizer.tokens());

    bool inMain = false;
    bool inDrain = false;
    bool sawGuardBranch = false;
    bool sawGuardedStore = false;
    bool sawLoopBranchAfterGuardedStore = false;
    bool sawDrainIndexUpdate = false;
    bool sawDivAfterDrain = false;
    unsigned int storeCount = 0;
    for (std::list<vcl::Token>::const_iterator i = transformed.begin(); i != transformed.end(); ++i)
    {
        if (i->label() == "loop_lid")
            inMain = true;
        if (i->label() == "loop_lid__DRAIN")
        {
            inMain = false;
            inDrain = true;
        }

        const std::string mnemonic = vcl::normalizeVuMnemonic(i->name());
        if (inMain && mnemonic == "ibeq" && tokenBranchesTo(*i, "loop_lid__DRAIN"))
            sawGuardBranch = true;
        if (inMain && sawGuardBranch && mnemonic == "sq")
        {
            ++storeCount;
            sawGuardedStore = true;
        }
        if (inMain && sawGuardedStore && mnemonic == "ibne" && tokenBranchesTo(*i, "loop_lid"))
            sawLoopBranchAfterGuardedStore = true;
        if (inDrain && mnemonic == "iaddiu")
            sawDrainIndexUpdate = true;
        if (inDrain && mnemonic == "div")
            sawDivAfterDrain = true;
    }

    CHECK(sawGuardBranch);
    CHECK(sawGuardedStore);
    CHECK(sawLoopBranchAfterGuardedStore);
    CHECK(sawDrainIndexUpdate);
    CHECK(!sawDivAfterDrain);
    CHECK(storeCount == 1u);
}

TEST_CASE("VuSchedulerAnalysis: loaded multi-Q suffix drains rotate values across the prolog boundary")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("div q, vf00[w], vf00[w]"));
    REQUIRE(program.parse("mulq.xyz vf02, vf00, q"));
    REQUIRE(program.parse("add.xyz vf10, vf00, vf00"));
    REQUIRE(program.parse("add.xyz vf11, vf00, vf00"));
    REQUIRE(program.parse("div q, vf00[w], vf00[w]"));
    REQUIRE(program.parse("add.xyz vf12, vf00, vf00"));
    REQUIRE(program.parse("add.xyz vf13, vf00, vf00"));
    REQUIRE(program.parse("mulq.xyz vf05, vf00, q"));
    REQUIRE(program.parse("sq.xyz vf05, 0(vi03)"));
    REQUIRE(program.parse("lq.xyz vf24, 0(vi04)"));
    REQUIRE(program.parse("add.xyz vf05, vf24, vf00"));
    REQUIRE(program.parse("iaddiu vi04, vi04, 1"));
    REQUIRE(program.parse("iaddiu vi03, vi03, 1"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    for (std::list<vcl::Token>::iterator i = program.tokenizer.tokens().begin();
         i != program.tokenizer.tokens().end(); ++i)
    {
        if (i->operand() && !i->operand()->isPreprocessor())
            i->setFlags(i->flags() | vcl::Token::PROCESSED);
    }

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities =
        vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].memoryLoadCount == 1u);
    CHECK(opportunities[0].hasMultiQSoftwarePipelinePlan);
    CHECK(opportunities[0].canEmitMultiQSoftwarePipeline);
    CHECK(opportunities[0].hasSuffixStoreDrainPlan);
    CHECK(opportunities[0].canEmitSuffixStoreDrain);
    CHECK(!hasString(opportunities[0].suffixStoreDrainBlockers,
                     "loaded_multi_q_boundary_rotation"));
    REQUIRE(opportunities[0].softwarePipelineSuffixStores.size() == 1u);
    CHECK(opportunities[0].softwarePipelineSuffixStores[0].delayedDrain);
    CHECK(opportunities[0].softwarePipelineSuffixStores[0].requiresValueRotation);
    CHECK(opportunities[0].softwarePipelineSuffixStores[0].hasValueScratchRegister);

    std::vector<vcl::VuSoftwarePipelineRewritePlan> plans =
        vcl::buildVuSoftwarePipelineRewritePlans(program.tokenizer.tokens());
    REQUIRE(plans.size() == 1u);
    CHECK(plans[0].drainsSuffixStores);

    std::list<vcl::Token> transformed =
        vcl::applyVuSoftwarePipelinePlans(program.tokenizer.tokens());

    bool sawProlog = false;
    bool sawMain = false;
    bool sawDrain = false;
    bool sawInvertedBranch = false;
    bool sawBoundaryQProducer = false;
    bool sawMainDelayedStore = false;
    bool sawDrainDelayedStore = false;
    for (std::list<vcl::Token>::const_iterator i = transformed.begin(); i != transformed.end(); ++i)
    {
        if (i->label() == "loop_lid__PROLOG")
            sawProlog = true;
        if (i->label() == "loop_lid")
            sawMain = true;
        if (i->label() == "loop_lid__DRAIN")
        {
            sawMain = false;
            sawDrain = true;
        }

        const std::string mnemonic = vcl::normalizeVuMnemonic(i->name());
        if (mnemonic == "ibeq" && tokenBranchesTo(*i, "loop_lid__DRAIN"))
        {
            sawInvertedBranch = true;
            std::list<vcl::Token>::const_iterator next = i;
            ++next;
            REQUIRE(next != transformed.end());
            if ((next->flags() & vcl::Token::BRANCH_DELAY_FILLER) != 0)
            {
                CHECK(vcl::normalizeVuMnemonic(next->name()) == "div");
                sawBoundaryQProducer = true;
            }
        }
        if (sawMain && mnemonic == "sq")
            sawMainDelayedStore = true;
        if (sawDrain && mnemonic == "sq")
            sawDrainDelayedStore = true;
    }

    CHECK(sawProlog);
    CHECK(sawInvertedBranch);
    CHECK(sawBoundaryQProducer);
    CHECK(sawMainDelayedStore);
    CHECK(sawDrain);
    CHECK(sawDrainDelayedStore);
}

TEST_CASE("VuSchedulerAnalysis: multi-Q suffix drains keep same-base load and store streams ordered")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("lq.xyz vf24, 0(vi03)"));
    REQUIRE(program.parse("div q, vf00[w], vf00[w]"));
    REQUIRE(program.parse("mulq.xyz vf02, vf00, q"));
    REQUIRE(program.parse("add.xyz vf10, vf00, vf00"));
    REQUIRE(program.parse("add.xyz vf11, vf00, vf00"));
    REQUIRE(program.parse("div q, vf00[w], vf00[w]"));
    REQUIRE(program.parse("add.xyz vf12, vf00, vf00"));
    REQUIRE(program.parse("add.xyz vf13, vf00, vf00"));
    REQUIRE(program.parse("mulq.xyz vf05, vf00, q"));
    REQUIRE(program.parse("sq.xyz vf05, 0(vi03)"));
    REQUIRE(program.parse("iaddiu vi03, vi03, 1"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities =
        vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].memoryLoadCount == 1u);
    CHECK(!opportunities[0].canEmitMultiQSoftwarePipeline);
    CHECK(hasString(opportunities[0].multiQSoftwarePipelineBlockers,
                    "read_write_memory_stream_conflict"));
    CHECK(opportunities[0].hasSuffixStoreDrainPlan);
    CHECK(!opportunities[0].canEmitSuffixStoreDrain);
    CHECK(hasString(opportunities[0].suffixStoreDrainBlockers,
                    "read_write_memory_stream_conflict"));
    CHECK(!hasString(opportunities[0].suffixStoreDrainBlockers,
                     "loaded_multi_q_boundary_rotation"));

    for (std::vector<vcl::VuSoftwarePipelineSuffixStore>::const_iterator i =
             opportunities[0].softwarePipelineSuffixStores.begin();
         i != opportunities[0].softwarePipelineSuffixStores.end(); ++i)
        CHECK(!i->delayedDrain);
}

TEST_CASE("VuSchedulerAnalysis: loaded multi-Q multi-store drains wait for value rotation")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("div q, vf00[w], vf00[w]"));
    REQUIRE(program.parse("mulq.xyz vf02, vf00, q"));
    REQUIRE(program.parse("add.xyz vf10, vf00, vf00"));
    REQUIRE(program.parse("add.xyz vf11, vf00, vf00"));
    REQUIRE(program.parse("div q, vf00[w], vf00[w]"));
    REQUIRE(program.parse("add.xyz vf12, vf00, vf00"));
    REQUIRE(program.parse("add.xyz vf13, vf00, vf00"));
    REQUIRE(program.parse("mulq.xyz vf05, vf00, q"));
    REQUIRE(program.parse("sq.xyz vf05, 0(vi03)"));
    REQUIRE(program.parse("sq.xyz vf02, 1(vi03)"));
    REQUIRE(program.parse("lq.xyz vf24, 0(vi04)"));
    REQUIRE(program.parse("add.xyz vf05, vf24, vf00"));
    REQUIRE(program.parse("iaddiu vi04, vi04, 1"));
    REQUIRE(program.parse("iaddiu vi03, vi03, 2"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    for (std::list<vcl::Token>::iterator i = program.tokenizer.tokens().begin();
         i != program.tokenizer.tokens().end(); ++i)
    {
        if (i->operand() && !i->operand()->isPreprocessor())
            i->setFlags(i->flags() | vcl::Token::PROCESSED);
    }

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities =
        vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].hasSuffixStoreDrainPlan);
    CHECK(!opportunities[0].canEmitSuffixStoreDrain);
    CHECK(!opportunities[0].canEmitMultiQSoftwarePipeline);
    CHECK(hasString(opportunities[0].suffixStoreDrainBlockers,
                    "multi_store_loaded_suffix_stream"));
    CHECK(hasString(opportunities[0].multiQSoftwarePipelineBlockers,
                    "multi_store_loaded_suffix_stream"));

    for (std::vector<vcl::VuSoftwarePipelineSuffixStore>::const_iterator i =
             opportunities[0].softwarePipelineSuffixStores.begin();
         i != opportunities[0].softwarePipelineSuffixStores.end(); ++i)
        CHECK(!i->delayedDrain);
}

TEST_CASE("VuSchedulerAnalysis: generic cyclic prefixes can rotate no-Q counted loops")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("--enter"));
    REQUIRE(program.parse("--endenter"));
    REQUIRE(program.parse("loi 1.0"));
    REQUIRE(program.parse("move.xyz vf08, vf00"));
    REQUIRE(program.parse("iaddiu vi04, vi00, 0"));
    REQUIRE(program.parse("iaddiu vi02, vi00, 9"));
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 3"));
    REQUIRE(program.parse("lq.xyz vf01, 1(vi04)"));
    REQUIRE(program.parse("minii.xyz vf01, vf01, i"));
    REQUIRE(program.parse("ftoi0.xyz vf02, vf01"));
    REQUIRE(program.parse("mul.xyz vf03, vf02, vf02"));
    REQUIRE(program.parse("mul.xyz vf05, vf03, vf03"));
    REQUIRE(program.parse("mul.xyz vf06, vf05, vf05"));
    REQUIRE(program.parse("add.xyz vf07, vf06, vf08"));
    REQUIRE(program.parse("iaddiu vi04, vi04, 3"));
    REQUIRE(program.parse("ibne vi04, vi02, loop_lid"));
    REQUIRE(program.parse("--exit"));
    REQUIRE(program.parse("--endexit"));

    vcl::RegisterAllocator allocator;
    allocator.setAvailableFloats(0xffffffffu);
    allocator.setAvailableIntegers(0xffffu);
    REQUIRE(allocator.process(program.tokenizer.tokens()));

    std::vector<vcl::VuSoftwarePipelineRewritePlan> plans =
        vcl::buildVuSoftwarePipelineRewritePlans(program.tokenizer.tokens());
    REQUIRE(plans.size() == 1u);
    CHECK(plans[0].cyclicPrefixBeforeBranch);
    CHECK(plans[0].qProducerTokenIndex == vcl::VU_SCHEDULED_TOKEN_INDEX_NONE);
    CHECK(!plans[0].prologTokenIndices.empty());
    CHECK(!plans[0].mainTokenIndices.empty());
    CHECK(!plans[0].cyclicPrefixTokenIndices.empty());
    CHECK(!plans[0].cyclicPrefixRotations.empty());
    CHECK(plans[0].cyclicPrefixInsertBeforeTokenIndex <= plans[0].branchTokenIndex);

    std::list<vcl::Token> transformed =
        vcl::applyVuSoftwarePipelinePlans(program.tokenizer.tokens());

    bool sawProlog = false;
    bool sawMain = false;
    bool sawRotationMove = false;
    bool sawClonedLoadAfterMain = false;
    for (std::list<vcl::Token>::const_iterator i = transformed.begin(); i != transformed.end(); ++i)
    {
        if (i->label() == "loop_lid__PROLOG")
            sawProlog = true;
        if (i->label() == "loop_lid")
            sawMain = true;
        if (!sawMain)
            continue;

        if (vcl::normalizeVuMnemonic(i->name()) == "move")
            sawRotationMove = true;

        vcl::VuTokenResourceAccess access;
        if (vcl::buildVuTokenResourceAccess(*i, access)
            && access.memoryKind == vcl::VU_MEMORY_LOAD
            && access.hasMemoryBase
            && access.memoryBaseRegister == "VI04")
            sawClonedLoadAfterMain = true;
    }

    CHECK(sawProlog);
    CHECK(sawMain);
    CHECK(sawRotationMove);
    CHECK(sawClonedLoadAfterMain);
}

TEST_CASE("VuSchedulerAnalysis: no-Q cyclic prefixes keep LoopExtra suffix stores")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loi 255.0"));
    REQUIRE(program.parse("iaddiu vi03, vi00, 0"));
    REQUIRE(program.parse("iaddiu vi02, vi00, 9"));
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 3"));
    REQUIRE(program.parse("--LoopExtra 1"));
    REQUIRE(program.parse("lq.xyz vf08, 1(vi03)"));
    REQUIRE(program.parse("minii.xyz vf08, vf08, i"));
    REQUIRE(program.parse("ftoi0.xyz vf08, vf08"));
    REQUIRE(program.parse("sq.xyz vf08, 1(vi03)"));
    REQUIRE(program.parse("iaddiu vi03, vi03, 3"));
    REQUIRE(program.parse("ibne vi03, vi02, loop_lid"));

    std::vector<vcl::VuSoftwarePipelineRewritePlan> plans =
        vcl::buildVuSoftwarePipelineRewritePlans(program.tokenizer.tokens());
    REQUIRE(plans.size() == 1u);
    CHECK(plans[0].cyclicPrefixBeforeBranch);
    CHECK(!plans[0].cyclicPrefixTokenIndices.empty());

    std::list<vcl::Token> transformed =
        vcl::applyVuSoftwarePipelinePlans(program.tokenizer.tokens());

    unsigned int storeCount = 0;
    bool sawMain = false;
    bool sawMainStore = false;
    for (std::list<vcl::Token>::const_iterator i = transformed.begin(); i != transformed.end(); ++i)
    {
        if (i->label() == "loop_lid")
            sawMain = true;

        vcl::VuTokenResourceAccess access;
        if (!vcl::buildVuTokenResourceAccess(*i, access)
            || access.memoryKind != vcl::VU_MEMORY_STORE)
            continue;

        ++storeCount;
        if (sawMain)
            sawMainStore = true;
    }

    CHECK(storeCount == 1u);
    CHECK(sawMainStore);
}

TEST_CASE("VuSchedulerAnalysis: no-Q cyclic prefixes can fill value-chain latency")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("lq.xyz vf01, 0(vi01)"));
    REQUIRE(program.parse("add.xyz vf02, vf01, vf00"));
    REQUIRE(program.parse("mul.w vf02, vf02, vf02"));
    REQUIRE(program.parse("mul.w vf02, vf02, vf02"));
    REQUIRE(program.parse("mul.w vf02, vf02, vf02"));
    REQUIRE(program.parse("mul.w vf02, vf02, vf02"));
    REQUIRE(program.parse("maddaw.xyz acc, vf03, vf02"));
    REQUIRE(program.parse("madd.xyz vf04, vf05, vf06"));
    REQUIRE(program.parse("sq.xyz vf04, 0(vi02)"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("iaddiu vi02, vi02, 1"));
    REQUIRE(program.parse("ibne vi01, vi03, loop_lid"));

    std::vector<vcl::VuSoftwarePipelineRewritePlan> plans =
        vcl::buildVuSoftwarePipelineRewritePlans(program.tokenizer.tokens());
    REQUIRE(plans.size() == 1u);
    CHECK(plans[0].cyclicPrefixBeforeBranch);
    CHECK(!plans[0].cyclicPrefixRotations.empty());
    CHECK(plans[0].cyclicPrefixInsertBeforeTokenIndex < plans[0].branchTokenIndex);

    std::list<vcl::Token> transformed =
        vcl::applyVuSoftwarePipelinePlans(program.tokenizer.tokens());

    bool inMain = false;
    bool sawClonedLoadBeforeFirstMul = false;
    bool sawFirstMainMul = false;
    for (std::list<vcl::Token>::const_iterator i = transformed.begin(); i != transformed.end(); ++i)
    {
        if (i->label() == "loop_lid")
            inMain = true;
        if (!inMain)
            continue;

        const std::string mnemonic = vcl::normalizeVuMnemonic(i->name());
        if (mnemonic == "mul")
            sawFirstMainMul = true;
        vcl::VuTokenResourceAccess access;
        if (!sawFirstMainMul
            && vcl::buildVuTokenResourceAccess(*i, access)
            && access.memoryKind == vcl::VU_MEMORY_LOAD
            && access.hasMemoryBase
            && access.memoryBaseRegister == "VI01")
            sawClonedLoadBeforeFirstMul = true;
    }

    CHECK(sawClonedLoadBeforeFirstMul);
}

TEST_CASE("VuSchedulerAnalysis: multi-Q software pipeline blocks cyclic prefixes that read clobbered suffix values")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("add.xyz vf02, vf10, vf00"));
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("mulq.xyz vf02, vf02, q"));
    REQUIRE(program.parse("div q, vf00[w], vf04[w]"));
    REQUIRE(program.parse("mulq.xyz vf05, vf06, q"));
    REQUIRE(program.parse("add.xyz vf10, vf11, vf00"));
    REQUIRE(program.parse("add.xyz vf20, vf20, vf00"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities =
        vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].hasMultiQSoftwarePipelinePlan);
    CHECK(!opportunities[0].canEmitMultiQSoftwarePipeline);
    CHECK(hasString(opportunities[0].multiQSoftwarePipelineBlockers,
                    "cyclic_prefix_reads_suffix_clobber"));

    std::vector<vcl::VuSoftwarePipelineRewritePlan> plans =
        vcl::buildVuSoftwarePipelineRewritePlans(program.tokenizer.tokens());
    CHECK(plans.empty());
}

TEST_CASE("VuSchedulerAnalysis: multi-Q software pipeline blocks unguardable cyclic prefix stores")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("sqi.xyz vf02, (vi03++)"));
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("mulq.xyz vf02, vf03, q"));
    REQUIRE(program.parse("div q, vf00[w], vf04[w]"));
    REQUIRE(program.parse("mulq.xyz vf05, vf06, q"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities =
        vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(!opportunities[0].canEmitMultiQSoftwarePipeline);
    CHECK(hasString(opportunities[0].multiQSoftwarePipelineBlockers,
                    "cyclic_prefix_side_effect"));
}

TEST_CASE("VuSchedulerAnalysis: multi-Q software pipeline blocks rotated internal labels")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("mulq.xyz vf02, vf03, q"));
    REQUIRE(program.parse("inside_lid:"));
    REQUIRE(program.parse("add.xyz vf10, vf10, vf00"));
    REQUIRE(program.parse("div q, vf00[w], vf04[w]"));
    REQUIRE(program.parse("mulq.xyz vf05, vf06, q"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 1"));
    REQUIRE(program.parse("ibne vi01, vi02, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities =
        vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(!opportunities[0].canEmitMultiQSoftwarePipeline);
    CHECK(hasString(opportunities[0].multiQSoftwarePipelineBlockers,
                    "cyclic_prefix_or_main_label"));
}

TEST_CASE("VuSchedulerAnalysis: pipeline opportunities require Q consumers")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 3"));
    REQUIRE(program.parse("lq.xyz vf01, 0(vi01)"));
    REQUIRE(program.parse("mul.xyz vf03, vf01, vf02"));
    REQUIRE(program.parse("div q, vf00[w], vf03[w]"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 3"));
    REQUIRE(program.parse("ibne vi01, vi03, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities = vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    CHECK(opportunities.empty());
}

TEST_CASE("VuSchedulerAnalysis: dependency graph keeps ACC multiply-add chains ordered")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("mulax acc, vf01, vf02"));
    REQUIRE(program.parse("madday acc, vf03, vf04"));
    REQUIRE(program.parse("maddw vf05, vf06, vf07"));

    std::vector<vcl::VuBasicBlock> blocks = vcl::buildVuBasicBlocks(program.tokenizer.tokens());
    REQUIRE(blocks.size() == 1u);

    std::vector<vcl::VuDependencyEdge> edges = vcl::buildVuDependencyGraph(blocks[0]);
    CHECK(hasEdge(edges, 0u, 1u, vcl::VU_DEPENDENCY_RESOURCE_RAW));
    CHECK(hasEdge(edges, 1u, 2u, vcl::VU_DEPENDENCY_RESOURCE_RAW));
}

TEST_CASE("VuSchedulerAnalysis: dependency graph keeps only the live ACC writer before ACC reads")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("mulax acc, vf01, vf02"));
    REQUIRE(program.parse("maddw vf03, vf04, vf05"));
    REQUIRE(program.parse("mulax acc, vf06, vf07"));
    REQUIRE(program.parse("maddw vf08, vf09, vf10"));

    std::vector<vcl::VuBasicBlock> blocks = vcl::buildVuBasicBlocks(program.tokenizer.tokens());
    REQUIRE(blocks.size() == 1u);

    std::vector<vcl::VuDependencyEdge> edges = vcl::buildVuDependencyGraph(blocks[0]);
    CHECK(hasEdge(edges, 0u, 1u, vcl::VU_DEPENDENCY_RESOURCE_RAW));
    CHECK(hasEdge(edges, 2u, 3u, vcl::VU_DEPENDENCY_RESOURCE_RAW));
    CHECK(!hasEdge(edges, 0u, 3u, vcl::VU_DEPENDENCY_RESOURCE_RAW));
    CHECK(hasEdge(edges, 1u, 2u, vcl::VU_DEPENDENCY_RESOURCE_WAR));
}

TEST_CASE("VuSchedulerAnalysis: dependency graph can ignore dead MAC WAW edges")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("mul.xy vf04, vf05, vf06"));

    std::vector<vcl::VuBasicBlock> blocks = vcl::buildVuBasicBlocks(program.tokenizer.tokens());
    REQUIRE(blocks.size() == 1u);

    std::vector<vcl::VuDependencyEdge> conservativeEdges = vcl::buildVuDependencyGraph(blocks[0]);
    CHECK(hasEdge(conservativeEdges, 0u, 1u, vcl::VU_DEPENDENCY_RESOURCE_WAW));

    std::vector<vcl::VuDependencyEdge> deadMacEdges = vcl::buildVuDependencyGraph(blocks[0], vcl::VU_RESOURCE_MAC);
    CHECK(!hasEdge(deadMacEdges, 0u, 1u, vcl::VU_DEPENDENCY_RESOURCE_WAW));
}

TEST_CASE("VuSchedulerAnalysis: dependency graph keeps only final live MAC writer before flag reads")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("mul.xy vf04, vf05, vf06"));
    REQUIRE(program.parse("sub.xy vf07, vf08, vf09"));
    REQUIRE(program.parse("fmand vi01, vi02"));

    std::vector<vcl::VuBasicBlock> blocks = vcl::buildVuBasicBlocks(program.tokenizer.tokens());
    REQUIRE(blocks.size() == 1u);

    std::vector<vcl::VuDependencyEdge> edges = vcl::buildVuDependencyGraph(blocks[0]);
    CHECK(!hasEdge(edges, 0u, 1u, vcl::VU_DEPENDENCY_RESOURCE_WAW));
    CHECK(hasEdge(edges, 0u, 2u, vcl::VU_DEPENDENCY_RESOURCE_WAW));
    CHECK(hasEdge(edges, 1u, 2u, vcl::VU_DEPENDENCY_RESOURCE_WAW));
    CHECK(!hasEdge(edges, 0u, 3u, vcl::VU_DEPENDENCY_RESOURCE_RAW));
    CHECK(!hasEdge(edges, 1u, 3u, vcl::VU_DEPENDENCY_RESOURCE_RAW));
    CHECK(hasEdge(edges, 2u, 3u, vcl::VU_DEPENDENCY_RESOURCE_RAW));
}

TEST_CASE("VuSchedulerAnalysis: ready scheduler can hoist overwritten MAC writers across waiting Q consumers")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("div q, vf00[w], vf01[w]"));
    REQUIRE(program.parse("mulq.xyz vf02, vf01, q"));
    REQUIRE(program.parse("mulax acc, vf03, vf04"));
    REQUIRE(program.parse("madday acc, vf05, vf06"));
    REQUIRE(program.parse("maddw vf07, vf08, vf09"));
    REQUIRE(program.parse("opmula.xyz acc, vf10, vf11"));
    REQUIRE(program.parse("opmsub.xyz vf12, vf11, vf10"));
    REQUIRE(program.parse("fmand vi01, vi02"));

    std::list<vcl::Token> scheduled =
        vcl::scheduleVuTokensReadySetWithFlagLiveness(program.tokenizer.tokens());

    unsigned int index = 0;
    unsigned int mulqIndex = 100u;
    unsigned int mulaIndex = 100u;
    for (std::list<vcl::Token>::const_iterator i = scheduled.begin(); i != scheduled.end(); ++i, ++index)
    {
        const std::string mnemonic = vcl::normalizeVuMnemonic(i->name());
        if (mnemonic == "mulq")
            mulqIndex = index;
        if (mnemonic == "mula")
            mulaIndex = index;
    }

    REQUIRE(mulqIndex != 100u);
    REQUIRE(mulaIndex != 100u);
    CHECK(mulaIndex < mulqIndex);
}

TEST_CASE("RegisterAllocator: multi-Q loop keeps Q stage aliases distinct for scheduling")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("--enter"));
    REQUIRE(program.parse("--endenter"));
    REQUIRE(program.parse("iaddiu ptr, vi00, 0"));
    REQUIRE(program.parse("iaddiu limit, vi00, 2"));
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 1"));
    REQUIRE(program.parse("lq.xyz source0, 0(ptr)"));
    REQUIRE(program.parse("lq.xyz tex0, 1(ptr)"));
    REQUIRE(program.parse("mulax acc, vf00, source0"));
    REQUIRE(program.parse("maddw stage0, vf00, vf00"));
    REQUIRE(program.parse("div q, vf00[w], stage0[w]"));
    REQUIRE(program.parse("mulq.xyz out0, tex0, q"));
    REQUIRE(program.parse("lq.xyz source1, 2(ptr)"));
    REQUIRE(program.parse("mulax acc, vf00, source1"));
    REQUIRE(program.parse("maddw stage1, vf00, vf00"));
    REQUIRE(program.parse("div q, vf00[w], stage1[w]"));
    REQUIRE(program.parse("mulq.xyz out1, stage1, q"));
    REQUIRE(program.parse("iaddiu ptr, ptr, 1"));
    REQUIRE(program.parse("ibne ptr, limit, loop_lid"));
    REQUIRE(program.parse("--exit"));
    REQUIRE(program.parse("--endexit"));

    vcl::RegisterAllocator allocator;
    allocator.setAvailableFloats(0xffffffffu);
    allocator.setAvailableIntegers(0xffffu);
    REQUIRE(allocator.process(program.tokenizer.tokens()));

    const vcl::Register* tex0 = allocatedFloatRegisterForAlias(program.tokenizer.tokens(), "tex0");
    const vcl::Register* stage1 = allocatedFloatRegisterForAlias(program.tokenizer.tokens(), "stage1");
    REQUIRE(tex0 != NULL);
    REQUIRE(stage1 != NULL);
    CHECK(tex0 != stage1);
}

TEST_CASE("VuSchedulerAnalysis: dependency graph keeps CLIP WAW unless CLIP is dead")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("clipw.xyz vf01, vf02[w]"));
    REQUIRE(program.parse("clipw.xyz vf03, vf04[w]"));

    std::vector<vcl::VuBasicBlock> blocks = vcl::buildVuBasicBlocks(program.tokenizer.tokens());
    REQUIRE(blocks.size() == 1u);

    std::vector<vcl::VuDependencyEdge> macOnlyIgnored = vcl::buildVuDependencyGraph(blocks[0], vcl::VU_RESOURCE_MAC);
    CHECK(hasEdge(macOnlyIgnored, 0u, 1u, vcl::VU_DEPENDENCY_RESOURCE_WAW));

    std::vector<vcl::VuDependencyEdge> allFlagsIgnored = vcl::buildVuDependencyGraph(blocks[0], vcl::VU_RESOURCE_MAC | vcl::VU_RESOURCE_CLIP);
    CHECK(!hasEdge(allFlagsIgnored, 0u, 1u, vcl::VU_DEPENDENCY_RESOURCE_WAW));
}

TEST_CASE("VuSchedulerAnalysis: dependency graph compares plain memory descriptors")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("sq.xy vf01, 0(vi01)"));
    REQUIRE(program.parse("lq.xy vf02, 4(vi01)"));
    REQUIRE(program.parse("lq.xy vf03, 0(vi01)"));

    std::vector<vcl::VuBasicBlock> blocks = vcl::buildVuBasicBlocks(program.tokenizer.tokens());
    REQUIRE(blocks.size() == 1u);

    std::vector<vcl::VuDependencyEdge> edges = vcl::buildVuDependencyGraph(blocks[0]);
    CHECK(!hasEdge(edges, 0u, 1u, vcl::VU_DEPENDENCY_MEMORY));
    CHECK(hasEdge(edges, 0u, 2u, vcl::VU_DEPENDENCY_MEMORY));
}

TEST_CASE("VuSchedulerAnalysis: preserve-order scheduler keeps block order intact")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("entry:"));
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("ibne vi01, vi02, next"));
    REQUIRE(program.parse("mul.xy vf04, vf05, vf06"));
    REQUIRE(program.parse("next:"));
    REQUIRE(program.parse("xgkick vi01"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensPreservingOrder(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == program.tokenizer.tokens().size());

    std::list<vcl::Token>::const_iterator a = program.tokenizer.tokens().begin();
    std::list<vcl::Token>::const_iterator b = scheduled.begin();
    for (; a != program.tokenizer.tokens().end(); ++a, ++b)
    {
        CHECK(a->name() == b->name());
        CHECK(a->label() == b->label());
        CHECK(a->arguments().size() == b->arguments().size());
    }
}

TEST_CASE("VuLatencyTracker: shared readiness model tracks registers Q and bypasses")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("mul.xy vf04, vf01, vf05"));
    REQUIRE(program.parse("div q, vf06[w], vf07[w]"));
    REQUIRE(program.parse("mulq.xy vf08, vf09, q"));
    REQUIRE(program.parse("ftoi0.xy vf10, vf00"));
    REQUIRE(program.parse("mtir vi01, vf10[x]"));

    std::vector<const vcl::Token*> tokens;
    for (std::list<vcl::Token>::const_iterator i = program.tokenizer.tokens().begin();
         i != program.tokenizer.tokens().end(); ++i)
        tokens.push_back(&*i);
    REQUIRE(tokens.size() == 6u);

    vcl::VuLatencyTracker tracker;
    tracker.recordWrites(*tokens[0], 0);
    CHECK(tracker.readHazardDelay(*tokens[1], NULL, 1) > 0);
    CHECK(tracker.readHazardDelay(*tokens[1], NULL, 5) == 0);

    tracker.reset();
    tracker.recordWrites(*tokens[2], 0);
    CHECK(tracker.qReadyCycle() == 8);
    CHECK(tracker.readHazardDelay(*tokens[3], NULL, 1) > 0);
    CHECK(tracker.readHazardDelay(*tokens[3], NULL, 8) == 0);

    tracker.reset();
    tracker.recordWrites(*tokens[4], 0);
    CHECK(tracker.readHazardDelay(*tokens[5], NULL, 1) == 0);
}

TEST_CASE("VuSchedulerAnalysis: ready-set scheduler pulls long Q producers forward")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("div q, vf04[w], vf05[w]"));
    REQUIRE(program.parse("mul.xy vf06, vf07, vf08"));
    REQUIRE(program.parse("waitq"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensReadySet(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == program.tokenizer.tokens().size());

    std::list<vcl::Token>::const_iterator i = scheduled.begin();
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "div");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "add");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "mul");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "waitq");
}

TEST_CASE("VuSchedulerAnalysis: ready issue slots expose latency padding cycles")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("mul.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("add.xy vf04, vf01, vf05"));

    std::vector<vcl::VuBasicBlock> blocks = vcl::buildVuBasicBlocks(program.tokenizer.tokens());
    REQUIRE(blocks.size() == 1u);

    std::vector<vcl::VuScheduledIssueSlot> slots = vcl::scheduleVuBasicBlockReadyIssueSlots(blocks[0]);
    REQUIRE(slots.size() == 5u);
    CHECK(slots[0].firstToken != NULL);
    CHECK(!slots[0].padding);
    CHECK(slots[0].paddingKind == vcl::VU_SCHEDULED_PADDING_NONE);
    CHECK(slots[0].issueCycle == 0u);
    CHECK(slots[0].cycleCount == 1u);
    for (unsigned int i = 1; i < 4; ++i)
    {
        CHECK(slots[i].firstToken == NULL);
        CHECK(slots[i].secondToken == NULL);
        CHECK(slots[i].padding);
        CHECK(slots[i].paddingKind == vcl::VU_SCHEDULED_PADDING_NOP);
        CHECK(slots[i].issueCycle == i);
        CHECK(slots[i].cycleCount == 1u);
    }
    REQUIRE(slots[4].firstToken != NULL);
    CHECK(vcl::normalizeVuMnemonic(slots[4].firstToken->name()) == "add");
    CHECK(!slots[4].padding);
    CHECK(slots[4].paddingKind == vcl::VU_SCHEDULED_PADDING_NONE);
    CHECK(slots[4].issueCycle == 4u);
    CHECK(slots[4].cycleCount == 1u);
}

TEST_CASE("VuSchedulerAnalysis: ready issue slots classify Q and P wait padding")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram qProgram;
    REQUIRE(qProgram.parse("div q, vf01[w], vf02[w]"));
    REQUIRE(qProgram.parse("mulq.xy vf03, vf04, q"));

    std::vector<vcl::VuBasicBlock> qBlocks = vcl::buildVuBasicBlocks(qProgram.tokenizer.tokens());
    REQUIRE(qBlocks.size() == 1u);
    std::vector<vcl::VuScheduledIssueSlot> qSlots = vcl::scheduleVuBasicBlockReadyIssueSlots(qBlocks[0]);
    REQUIRE(qSlots.size() == 2u);
    CHECK(!qSlots[1].padding);
    CHECK(qSlots[1].paddingKind == vcl::VU_SCHEDULED_PADDING_WAITQ);
    CHECK(qSlots[1].issueCycle == 1u);
    CHECK(qSlots[1].cycleCount == 8u);
    REQUIRE(qSlots[1].upperToken != NULL);
    CHECK(vcl::normalizeVuMnemonic(qSlots[1].upperToken->name()) == "mulq");

    vcl::Error::ResetErrorCount();
    ParsedProgram pProgram;
    REQUIRE(pProgram.parse("esin p, vf01[x]"));
    REQUIRE(pProgram.parse("mfp.xy vf02, p"));

    std::vector<vcl::VuBasicBlock> pBlocks = vcl::buildVuBasicBlocks(pProgram.tokenizer.tokens());
    REQUIRE(pBlocks.size() == 1u);
    std::vector<vcl::VuScheduledIssueSlot> pSlots = vcl::scheduleVuBasicBlockReadyIssueSlots(pBlocks[0]);
    REQUIRE(pSlots.size() == 3u);
    CHECK(pSlots[1].padding);
    CHECK(pSlots[1].paddingKind == vcl::VU_SCHEDULED_PADDING_WAITP);
    CHECK(pSlots[1].issueCycle == 1u);
    CHECK(pSlots[1].cycleCount == 29u);
    REQUIRE(pSlots[2].firstToken != NULL);
    CHECK(pSlots[2].issueCycle == 30u);
    CHECK(vcl::normalizeVuMnemonic(pSlots[2].firstToken->name()) == "mfp");
}

TEST_CASE("VuSchedulerAnalysis: ready-set scheduler keeps dependencies and barriers ordered")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("mul.xy vf04, vf01, vf05"));
    REQUIRE(program.parse("loi 1.0"));
    REQUIRE(program.parse("addi.xy vf06, vf07, i"));
    REQUIRE(program.parse("sq.xy vf06, 0(vi01)"));
    REQUIRE(program.parse("div q, vf08[w], vf09[w]"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensReadySet(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == program.tokenizer.tokens().size());

    std::vector<std::string> names;
    for( std::list<vcl::Token>::const_iterator i = scheduled.begin(); i != scheduled.end(); ++i )
        names.push_back(vcl::normalizeVuMnemonic(i->name()));

    REQUIRE(names.size() == 6u);

    unsigned int addPos = 0;
    unsigned int mulPos = 0;
    unsigned int loiPos = 0;
    unsigned int addiPos = 0;
    unsigned int sqPos = 0;
    unsigned int divPos = 0;
    for( unsigned int i = 0; i < names.size(); ++i )
    {
        if( names[i] == "add" )
            addPos = i;
        else if( names[i] == "mul" )
            mulPos = i;
        else if( names[i] == "loi" )
            loiPos = i;
        else if( names[i] == "addi" )
            addiPos = i;
        else if( names[i] == "sq" )
            sqPos = i;
        else if( names[i] == "div" )
            divPos = i;
    }

    CHECK(addPos < mulPos);
    CHECK(loiPos < addiPos);
    CHECK(addiPos < sqPos);
    CHECK(divPos < sqPos);
}

TEST_CASE("VuSchedulerAnalysis: ready-set scheduler pulls plain loads before independent arithmetic")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("lq.xy vf04, 0(vi01)"));
    REQUIRE(program.parse("mul.xy vf05, vf04, vf06"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensReadySet(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == program.tokenizer.tokens().size());

    std::list<vcl::Token>::const_iterator i = scheduled.begin();
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "lq");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "add");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "mul");
}

TEST_CASE("VuSchedulerAnalysis: ready-set scheduler fills register latency with independent work")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("mul.xy vf04, vf01, vf05"));
    REQUIRE(program.parse("iaddiu vi01, vi00, 1"));
    REQUIRE(program.parse("iaddiu vi02, vi00, 2"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensReadySet(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == program.tokenizer.tokens().size());

    std::list<vcl::Token>::const_iterator i = scheduled.begin();
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "add");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "iaddiu");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "iaddiu");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "mul");
}

TEST_CASE("VuSchedulerAnalysis: ready-set scheduler keeps independent dual-pipe partners adjacent")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("mul.xy vf04, vf01, vf05"));
    REQUIRE(program.parse("iaddiu vi01, vi00, 1"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensReadySet(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == program.tokenizer.tokens().size());

    std::list<vcl::Token>::const_iterator i = scheduled.begin();
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "add");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "iaddiu");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "mul");
}

TEST_CASE("VuSchedulerAnalysis: issue slots expose generic dual-pipe pair choices")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("mul.xy vf04, vf01, vf05"));
    REQUIRE(program.parse("iaddiu vi01, vi00, 1"));

    std::vector<vcl::VuBasicBlock> blocks = vcl::buildVuBasicBlocks(program.tokenizer.tokens());
    REQUIRE(blocks.size() == 1u);

    std::vector<vcl::VuScheduledIssueSlot> slots = vcl::scheduleVuBasicBlockReadyIssueSlots(blocks[0]);
    REQUIRE(slots.size() == 5u);
    REQUIRE(slots[0].firstToken != 0);
    REQUIRE(slots[0].secondToken != 0);
    CHECK(vcl::normalizeVuMnemonic(slots[0].firstToken->name()) == "add");
    CHECK(vcl::normalizeVuMnemonic(slots[0].secondToken->name()) == "iaddiu");
    REQUIRE(slots[0].upperToken != 0);
    REQUIRE(slots[0].lowerToken != 0);
    CHECK(vcl::normalizeVuMnemonic(slots[0].upperToken->name()) == "add");
    CHECK(vcl::normalizeVuMnemonic(slots[0].lowerToken->name()) == "iaddiu");
    CHECK(slots[0].firstTokenIndex == 0u);
    CHECK(slots[0].secondTokenIndex == 2u);
    CHECK(slots[0].upperTokenIndex == 0u);
    CHECK(slots[0].lowerTokenIndex == 2u);

    for (unsigned int i = 1; i < 4; ++i)
    {
        CHECK(slots[i].padding);
        CHECK(slots[i].paddingKind == vcl::VU_SCHEDULED_PADDING_NOP);
        CHECK(slots[i].issueCycle == i);
        CHECK(slots[i].cycleCount == 1u);
    }

    REQUIRE(slots[4].firstToken != 0);
    CHECK(slots[4].secondToken == 0);
    CHECK(vcl::normalizeVuMnemonic(slots[4].firstToken->name()) == "mul");
    REQUIRE(slots[4].upperToken != 0);
    CHECK(slots[4].lowerToken == 0);
    CHECK(vcl::normalizeVuMnemonic(slots[4].upperToken->name()) == "mul");
}

TEST_CASE("VuSchedulerAnalysis: ready-set scheduler tags explicit issue-slot pairs")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("mul.xy vf04, vf01, vf05"));
    REQUIRE(program.parse("iaddiu vi01, vi00, 1"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensReadySet(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == 3u);

    std::list<vcl::Token>::const_iterator i = scheduled.begin();
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "add");
    CHECK((i->flags() & vcl::Token::SCHEDULED_PAIR_FIRST) != 0);
    CHECK((i->flags() & vcl::Token::SCHEDULED_PAIR_SECOND) == 0);
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "iaddiu");
    CHECK((i->flags() & vcl::Token::SCHEDULED_PAIR_FIRST) == 0);
    CHECK((i->flags() & vcl::Token::SCHEDULED_PAIR_SECOND) != 0);
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "mul");
    CHECK((i->flags() & vcl::Token::SCHEDULED_PAIR_FIRST) == 0);
    CHECK((i->flags() & vcl::Token::SCHEDULED_PAIR_SECOND) == 0);
}

TEST_CASE("VuSchedulerAnalysis: non-flag ready-set program exposes issue slots")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("mul.xy vf04, vf01, vf05"));
    REQUIRE(program.parse("iaddiu vi01, vi00, 1"));

    vcl::VuScheduledProgram scheduled =
        vcl::scheduleVuProgramReadyIssueSlots(program.tokenizer.tokens());
    REQUIRE(scheduled.blocks.size() == 1u);
    REQUIRE(scheduled.blocks[0].issueSlots.size() == 5u);
    CHECK(scheduled.blocks[0].firstIssueCycle == 0u);
    CHECK(scheduled.blocks[0].cycleCount == 5u);
    REQUIRE(scheduled.blocks[0].issueSlots[0].firstToken != NULL);
    REQUIRE(scheduled.blocks[0].issueSlots[0].secondToken != NULL);
    CHECK(vcl::normalizeVuMnemonic(scheduled.blocks[0].issueSlots[0].firstToken->name()) == "add");
    CHECK(vcl::normalizeVuMnemonic(scheduled.blocks[0].issueSlots[0].secondToken->name()) == "iaddiu");
    CHECK(scheduled.blocks[0].issueSlots[0].firstTokenIndex == 0u);
    CHECK(scheduled.blocks[0].issueSlots[0].secondTokenIndex == 2u);
}

TEST_CASE("VuSchedulerAnalysis: ready-set program pairs barrier tails when safe")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xyz vf01, vf00, vf00"));
    REQUIRE(program.parse("b done_lid"));
    REQUIRE(program.parse("done_lid:"));
    REQUIRE(program.parse("add.xyz vf02, vf00, vf00"));
    REQUIRE(program.parse("xgkick vi00"));

    vcl::VuScheduledProgram scheduled =
        vcl::scheduleVuProgramReadyIssueSlotsWithFlagLiveness(program.tokenizer.tokens());
    REQUIRE(scheduled.blocks.size() == 2u);
    REQUIRE(scheduled.blocks[0].issueSlots.size() == 1u);
    CHECK(vcl::normalizeVuMnemonic(scheduled.blocks[0].issueSlots[0].upperToken->name()) == "add");
    CHECK(vcl::normalizeVuMnemonic(scheduled.blocks[0].issueSlots[0].lowerToken->name()) == "b");
    CHECK(scheduled.blocks[0].issueSlots[0].firstTokenIndex == 0u);
    CHECK(scheduled.blocks[0].issueSlots[0].secondTokenIndex == 1u);

    REQUIRE(scheduled.blocks[1].issueSlots.size() == 2u);
    CHECK(vcl::normalizeVuMnemonic(scheduled.blocks[1].issueSlots[1].upperToken->name()) == "add");
    CHECK(vcl::normalizeVuMnemonic(scheduled.blocks[1].issueSlots[1].lowerToken->name()) == "xgkick");
    CHECK(scheduled.blocks[1].issueSlots[1].firstTokenIndex == 3u);
    CHECK(scheduled.blocks[1].issueSlots[1].secondTokenIndex == 4u);
}

TEST_CASE("VuSchedulerAnalysis: ready-set program can pair branch tails before delay fillers")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xyz vf01, vf00, vf00"));
    REQUIRE(program.parse("b done_lid"));
    REQUIRE(program.parse("iaddiu vi01, vi00, 1"));
    REQUIRE(program.parse("done_lid:"));

    std::list<vcl::Token>::iterator filler = program.tokenizer.tokens().begin();
    ++filler;
    ++filler;
    filler->setFlags(filler->flags() | vcl::Token::BRANCH_DELAY_FILLER);

    vcl::VuScheduledProgram scheduled =
        vcl::scheduleVuProgramReadyIssueSlotsWithFlagLiveness(program.tokenizer.tokens());
    REQUIRE(scheduled.blocks.size() >= 2u);
    REQUIRE(scheduled.blocks[0].issueSlots.size() == 1u);
    CHECK(vcl::normalizeVuMnemonic(scheduled.blocks[0].issueSlots[0].upperToken->name()) == "add");
    CHECK(vcl::normalizeVuMnemonic(scheduled.blocks[0].issueSlots[0].lowerToken->name()) == "b");
    CHECK(scheduled.blocks[0].issueSlots[0].firstTokenIndex == 0u);
    CHECK(scheduled.blocks[0].issueSlots[0].secondTokenIndex == 1u);
    REQUIRE(scheduled.blocks[1].issueSlots.size() >= 1u);
    CHECK(scheduled.blocks[1].issueSlots[0].firstTokenIndex == 2u);
    CHECK(scheduled.blocks[1].issueSlots[0].secondTokenIndex == vcl::VU_SCHEDULED_TOKEN_INDEX_NONE);
}

TEST_CASE("VuSchedulerAnalysis: ready-set scheduler prefers longer dependency chains")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("iaddiu vi10, vi00, 1"));
    REQUIRE(program.parse("iaddiu vi01, vi00, 1"));
    REQUIRE(program.parse("iaddiu vi04, vi01, 1"));
    REQUIRE(program.parse("iaddiu vi06, vi04, 1"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensReadySet(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == program.tokenizer.tokens().size());

    std::list<vcl::Token>::const_iterator i = scheduled.begin();
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "iaddiu");
    CHECK(i->arguments().begin()->regNumber() == 1u);
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "iaddiu");
    CHECK(i->arguments().begin()->regNumber() == 4u);
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "iaddiu");
    CHECK(i->arguments().begin()->regNumber() == 10u);
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "iaddiu");
    CHECK(i->arguments().begin()->regNumber() == 6u);
}

TEST_CASE("VuSchedulerAnalysis: ready-set scheduler weights dependency chains by latency")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("iaddiu vi10, vi00, 1"));
    REQUIRE(program.parse("iaddiu vi11, vi10, 1"));
    REQUIRE(program.parse("iaddiu vi12, vi11, 1"));
    REQUIRE(program.parse("ftoi0.xy vf01, vf00"));
    REQUIRE(program.parse("add.xy vf02, vf01, vf00"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensReadySet(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == program.tokenizer.tokens().size());

    std::list<vcl::Token>::const_iterator i = scheduled.begin();
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "ftoi0");
}

TEST_CASE("VuSchedulerAnalysis: ready-set scheduler moves distinct-address loads before stores")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("sq.xy vf01, 0(vi01)"));
    REQUIRE(program.parse("lq.xy vf02, 4(vi01)"));
    REQUIRE(program.parse("mul.xy vf03, vf02, vf04"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensReadySet(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == program.tokenizer.tokens().size());

    std::list<vcl::Token>::const_iterator i = scheduled.begin();
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "lq");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "sq");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "mul");
}

TEST_CASE("VuSchedulerAnalysis: ready-set scheduler keeps same-address loads behind stores")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("sq.xy vf01, 0(vi01)"));
    REQUIRE(program.parse("lq.xy vf02, 0(vi01)"));
    REQUIRE(program.parse("mul.xy vf03, vf02, vf04"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensReadySet(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == program.tokenizer.tokens().size());

    std::list<vcl::Token>::const_iterator i = scheduled.begin();
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "sq");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "lq");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "mul");
}

TEST_CASE("VuSchedulerAnalysis: flag liveness keeps MAC WAW before the last reader")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("ftoi0.xy vf10, vf00"));
    REQUIRE(program.parse("mul.xy vf11, vf10, vf00"));
    REQUIRE(program.parse("fmand vi01, vi02"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensReadySetWithFlagLiveness(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == program.tokenizer.tokens().size());

    std::list<vcl::Token>::const_iterator i = scheduled.begin();
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "add");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "ftoi0");
}

TEST_CASE("VuSchedulerAnalysis: flag liveness ignores MAC WAW after the last reader")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("fmand vi01, vi02"));
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("ftoi0.xy vf10, vf00"));
    REQUIRE(program.parse("mul.xy vf11, vf10, vf00"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensReadySetWithFlagLiveness(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == program.tokenizer.tokens().size());

    std::list<vcl::Token>::const_iterator i = scheduled.begin();
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "fmand");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "ftoi0");
}

TEST_CASE("VuSchedulerAnalysis: flag-liveness issue slots match ready-set scheduling")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("fmand vi01, vi02"));
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("ftoi0.xy vf10, vf00"));
    REQUIRE(program.parse("mul.xy vf11, vf10, vf00"));

    std::vector< std::vector<vcl::VuScheduledIssueSlot> > blockSlots =
        vcl::scheduleVuBasicBlocksReadyIssueSlotsWithFlagLiveness(program.tokenizer.tokens());
    REQUIRE(blockSlots.size() == 1u);
    REQUIRE(blockSlots[0].size() >= 2u);
    REQUIRE(blockSlots[0][0].firstToken != NULL);
    CHECK(vcl::normalizeVuMnemonic(blockSlots[0][0].firstToken->name()) == "fmand");
    CHECK(blockSlots[0][0].ignoredImplicitWawResources == vcl::VU_RESOURCE_NONE);
    REQUIRE(blockSlots[0][1].firstToken != NULL);
    CHECK(vcl::normalizeVuMnemonic(blockSlots[0][1].firstToken->name()) == "ftoi0");
    CHECK((blockSlots[0][1].ignoredImplicitWawResources & vcl::VU_RESOURCE_MAC) != 0u);
    CHECK((blockSlots[0][1].ignoredImplicitWawResources & vcl::VU_RESOURCE_CLIP) != 0u);
}

TEST_CASE("VuSchedulerAnalysis: scheduled program exposes block cycle ranges")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("entry_lid:"));
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("mul.xy vf04, vf01, vf05"));
    REQUIRE(program.parse("next_lid:"));
    REQUIRE(program.parse("iaddiu vi01, vi00, 1"));

    vcl::VuScheduledProgram scheduled =
        vcl::scheduleVuProgramReadyIssueSlotsWithFlagLiveness(program.tokenizer.tokens());
    REQUIRE(scheduled.blocks.size() == 2u);
    CHECK(scheduled.blocks[0].block.firstTokenIndex == 0u);
    CHECK(scheduled.blocks[0].firstIssueCycle == 0u);
    CHECK(scheduled.blocks[0].cycleCount == 6u);
    CHECK(scheduled.blocks[1].block.firstTokenIndex == 3u);
    CHECK(scheduled.blocks[1].firstIssueCycle == 6u);
    CHECK(scheduled.blocks[1].cycleCount == 2u);
    CHECK(scheduled.cycleCount == 8u);
    REQUIRE(scheduled.blocks[0].issueSlots.size() >= 2u);
    CHECK(scheduled.blocks[0].issueSlots[0].firstTokenIndex == 0u);
    CHECK(scheduled.blocks[0].issueSlots[0].secondTokenIndex == vcl::VU_SCHEDULED_TOKEN_INDEX_NONE);
    CHECK(scheduled.blocks[0].issueSlots[1].firstTokenIndex == 1u);
    REQUIRE(scheduled.blocks[1].issueSlots.size() >= 1u);
    CHECK(scheduled.blocks[1].issueSlots[0].firstTokenIndex == 3u);
}

TEST_CASE("VuSchedulerAnalysis: scheduled program carries Q latency across label blocks")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("div q, vf01[w], vf02[w]"));
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("mulq.xy vf03, vf04, q"));

    vcl::VuScheduledProgram scheduled =
        vcl::scheduleVuProgramReadyIssueSlotsWithFlagLiveness(program.tokenizer.tokens());
    REQUIRE(scheduled.blocks.size() == 2u);
    REQUIRE(scheduled.blocks[1].issueSlots.size() >= 3u);
    CHECK(scheduled.blocks[1].issueSlots[0].firstTokenIndex == 1u);
    CHECK(scheduled.blocks[1].issueSlots[1].padding);
    CHECK(scheduled.blocks[1].issueSlots[1].paddingKind == vcl::VU_SCHEDULED_PADDING_WAITQ);
    REQUIRE(scheduled.blocks[1].issueSlots[2].firstToken != NULL);
    CHECK(vcl::normalizeVuMnemonic(scheduled.blocks[1].issueSlots[2].firstToken->name()) == "mulq");
}

TEST_CASE("VuSchedulerAnalysis: scheduled program flattens to scheduled token order")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("entry_lid:"));
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("mul.xy vf04, vf01, vf05"));
    REQUIRE(program.parse("iaddiu vi01, vi00, 1"));
    REQUIRE(program.parse("next_lid:"));

    vcl::VuScheduledProgram scheduled =
        vcl::scheduleVuProgramReadyIssueSlotsWithFlagLiveness(program.tokenizer.tokens());
    std::list<vcl::Token> flattened = vcl::flattenVuScheduledProgramTokens(scheduled);
    REQUIRE(flattened.size() == 5u);

    std::list<vcl::Token>::const_iterator i = flattened.begin();
    REQUIRE(i != flattened.end());
    CHECK(i->label() == "entry_lid");
    CHECK(i->operand() == NULL);

    ++i;
    REQUIRE(i != flattened.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "add");
    CHECK((i->flags() & vcl::Token::SCHEDULED_PAIR_FIRST) != 0);
    CHECK((i->flags() & vcl::Token::SCHEDULED_PAIR_SECOND) == 0);

    ++i;
    REQUIRE(i != flattened.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "iaddiu");
    CHECK((i->flags() & vcl::Token::SCHEDULED_PAIR_FIRST) == 0);
    CHECK((i->flags() & vcl::Token::SCHEDULED_PAIR_SECOND) != 0);

    ++i;
    REQUIRE(i != flattened.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "mul");
    CHECK((i->flags() & vcl::Token::SCHEDULED_PAIR_FIRST) == 0);
    CHECK((i->flags() & vcl::Token::SCHEDULED_PAIR_SECOND) == 0);

    ++i;
    REQUIRE(i != flattened.end());
    CHECK(i->label() == "next_lid");
    CHECK(i->operand() == NULL);
}

TEST_CASE("VuSchedulerAnalysis: remaining flag WAW helper reports dead MAC and CLIP flags")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("add.xy vf01, vf02, vf03"));
    REQUIRE(program.parse("clipw.xyz vf04, vf05[w]"));
    REQUIRE(program.parse("fmand vi01, vi02"));

    std::list<vcl::Token>::const_iterator begin = program.tokenizer.tokens().begin();
    CHECK((vcl::vuIgnoredFlagWawResourcesForRemaining(begin, program.tokenizer.tokens().end()) & vcl::VU_RESOURCE_MAC) == 0u);
    CHECK((vcl::vuIgnoredFlagWawResourcesForRemaining(begin, program.tokenizer.tokens().end()) & vcl::VU_RESOURCE_CLIP) != 0u);

    ++begin;
    ++begin;
    ++begin;
    CHECK((vcl::vuIgnoredFlagWawResourcesForRemaining(begin, program.tokenizer.tokens().end()) & vcl::VU_RESOURCE_MAC) != 0u);
    CHECK((vcl::vuIgnoredFlagWawResourcesForRemaining(begin, program.tokenizer.tokens().end()) & vcl::VU_RESOURCE_CLIP) != 0u);
}

TEST_CASE("VuSchedulerAnalysis: ready-set scheduler can move Q producers before independent plain stores")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("sq.xy vf01, 0(vi01)"));
    REQUIRE(program.parse("div q, vf02[w], vf03[w]"));
    REQUIRE(program.parse("add.xy vf04, vf05, vf06"));
    REQUIRE(program.parse("mulq.xy vf07, vf08, q"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensReadySet(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == program.tokenizer.tokens().size());

    std::list<vcl::Token>::const_iterator i = scheduled.begin();
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "div");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "add");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "sq");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "mulq");
}

TEST_CASE("VuSchedulerAnalysis: ready-set scheduler fills Q latency before Q consumers")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("div q, vf01[w], vf02[w]"));
    REQUIRE(program.parse("mulq.xy vf03, vf04, q"));
    REQUIRE(program.parse("iaddiu vi01, vi00, 1"));
    REQUIRE(program.parse("iaddiu vi02, vi00, 2"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensReadySet(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == program.tokenizer.tokens().size());

    std::list<vcl::Token>::const_iterator i = scheduled.begin();
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "div");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "iaddiu");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "iaddiu");
    ++i;
    REQUIRE(i != scheduled.end());
    CHECK(vcl::normalizeVuMnemonic(i->name()) == "mulq");
}

TEST_CASE("VuSchedulerAnalysis: latency-blocked Q consumers do not outrank ready independent work")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("div q, vf01[w], vf02[w]"));
    REQUIRE(program.parse("mulq.xy vf03, vf04, q"));
    REQUIRE(program.parse("add.xy vf05, vf03, vf06"));
    REQUIRE(program.parse("add.xy vf07, vf05, vf08"));
    REQUIRE(program.parse("lq.xy vf09, 0(vi01)"));
    REQUIRE(program.parse("lq.xy vf10, 1(vi01)"));
    REQUIRE(program.parse("iaddiu vi02, vi02, 1"));

    std::list<vcl::Token> scheduled = vcl::scheduleVuTokensReadySet(program.tokenizer.tokens());
    REQUIRE(scheduled.size() == program.tokenizer.tokens().size());

    std::vector<std::string> names;
    for (std::list<vcl::Token>::const_iterator i = scheduled.begin(); i != scheduled.end(); ++i)
        names.push_back(vcl::normalizeVuMnemonic(i->name()));

    REQUIRE(names.size() == 7u);
    CHECK(names[0] == "div");
    CHECK(names[1] == "lq");
    CHECK(names[2] == "lq");
    CHECK(names[3] == "iaddiu");
    CHECK(names[4] == "mulq");
}

TEST_CASE("VuSchedulerAnalysis: wait slots can carry the waiting upper instruction")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("div q, vf01[w], vf02[w]"));
    REQUIRE(program.parse("mulq.xy vf03, vf04, q"));

    vcl::VuScheduledProgram scheduled =
        vcl::scheduleVuProgramReadyIssueSlotsWithFlagLiveness(program.tokenizer.tokens());
    REQUIRE(scheduled.blocks.size() == 1u);
    REQUIRE(scheduled.blocks[0].issueSlots.size() >= 2u);

    const vcl::VuScheduledIssueSlot& waitSlot = scheduled.blocks[0].issueSlots[1];
    REQUIRE(waitSlot.upperToken != NULL);
    CHECK(vcl::normalizeVuMnemonic(waitSlot.upperToken->name()) == "mulq");
    CHECK(waitSlot.paddingKind == vcl::VU_SCHEDULED_PADDING_WAITQ);
    CHECK(waitSlot.cycleCount > 1u);
}

TEST_CASE("VuSchedulerAnalysis: wait slots can carry independent upper work")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("esadd p, vf00"));
    REQUIRE(program.parse("--barrier"));
    REQUIRE(program.parse("mfp.w vf01, p"));
    REQUIRE(program.parse("add.xyz vf02, vf00, vf00"));

    vcl::VuScheduledProgram scheduled =
        vcl::scheduleVuProgramReadyIssueSlotsWithFlagLiveness(program.tokenizer.tokens());
    REQUIRE(scheduled.blocks.size() >= 2u);
    REQUIRE(scheduled.blocks[1].issueSlots.size() >= 2u);

    const vcl::VuScheduledIssueSlot& waitSlot = scheduled.blocks[1].issueSlots[0];
    REQUIRE(waitSlot.upperToken != NULL);
    CHECK(vcl::normalizeVuMnemonic(waitSlot.upperToken->name()) == "add");
    CHECK(waitSlot.paddingKind == vcl::VU_SCHEDULED_PADDING_WAITP);
    CHECK(waitSlot.cycleCount > 1u);

    const vcl::VuScheduledIssueSlot& mfpSlot = scheduled.blocks[1].issueSlots[1];
    REQUIRE(mfpSlot.lowerToken != NULL);
    CHECK(vcl::normalizeVuMnemonic(mfpSlot.lowerToken->name()) == "mfp");
}

// Track 9.E (multi-stage SWP) step 1: assert all opportunities default to
// stageCount==1 and the new collections are empty (no behavior change yet).
TEST_CASE("VuSchedulerAnalysis: multistage schema defaults to stageCount=1")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 3"));
    REQUIRE(program.parse("lq.xyz vf01, 0(vi01)"));
    REQUIRE(program.parse("mul.xyz vf03, vf01, vf02"));
    REQUIRE(program.parse("div q, vf00[w], vf03[w]"));
    REQUIRE(program.parse("mulq.xyz vf03, vf03, q"));
    REQUIRE(program.parse("add.xyz vf05, vf03, vf00"));
    REQUIRE(program.parse("lq.xyz vf06, 2(vi01)"));
    REQUIRE(program.parse("mulq.xyz vf06, vf06, q"));
    REQUIRE(program.parse("sq.xyz vf06, 0(vi02)"));
    REQUIRE(program.parse("iaddiu vi01, vi01, 3"));
    REQUIRE(program.parse("ibne vi01, vi03, loop_lid"));

    std::vector<vcl::VuLoopPipelineOpportunity> opportunities =
        vcl::findVuLoopPipelineOpportunities(program.tokenizer.tokens());
    REQUIRE(opportunities.size() == 1u);
    CHECK(opportunities[0].stageCount == 1u);
    CHECK(opportunities[0].kernelTokenIndices.empty());
    CHECK(opportunities[0].tokenStageOffsets.empty());
    CHECK(opportunities[0].stageRotationRegisters.empty());

    std::vector<vcl::VuSoftwarePipelineRewritePlan> plans =
        vcl::buildVuSoftwarePipelineRewritePlans(program.tokenizer.tokens());
    for (std::vector<vcl::VuSoftwarePipelineRewritePlan>::const_iterator p = plans.begin();
         p != plans.end(); ++p) {
        CHECK(p->stageCount == 1u);
        CHECK(p->kernelTokenIndices.empty());
        CHECK(p->tokenStageOffsets.empty());
        CHECK(p->stageRotationRegisters.empty());
    }
}

// Track 9.G step 7a: generic-kernel-rewrite emission eligibility helper.
TEST_CASE("VuSchedulerAnalysis: default rewrite plan is not eligible for generic kernel rewrite")
{
    vcl::VuSoftwarePipelineRewritePlan plan;
    CHECK(!vcl::isVuPlanEligibleForGenericKernelRewrite(plan));
}

TEST_CASE("VuSchedulerAnalysis: rewrite plan with II>=1, stageCount>=2, no conflicts, no hints, non-empty MAIN is eligible")
{
    vcl::VuSoftwarePipelineRewritePlan plan;
    plan.kernelRewriteII = 4u;
    plan.kernelRewriteStageCount = 2u;
    plan.kernelRewriteConflicts = 0u;
    plan.kernelRewriteMainTokens.push_back(0u);
    CHECK(vcl::isVuPlanEligibleForGenericKernelRewrite(plan));
}

TEST_CASE("VuSchedulerAnalysis: any conflict disqualifies the plan from generic kernel rewrite")
{
    vcl::VuSoftwarePipelineRewritePlan plan;
    plan.kernelRewriteII = 4u;
    plan.kernelRewriteStageCount = 2u;
    plan.kernelRewriteConflicts = 1u;
    plan.kernelRewriteMainTokens.push_back(0u);
    CHECK(!vcl::isVuPlanEligibleForGenericKernelRewrite(plan));
}

TEST_CASE("VuSchedulerAnalysis: any rename hint disqualifies the plan from generic kernel rewrite")
{
    vcl::VuSoftwarePipelineRewritePlan plan;
    plan.kernelRewriteII = 4u;
    plan.kernelRewriteStageCount = 2u;
    plan.kernelRewriteConflicts = 0u;
    plan.kernelRewriteMainTokens.push_back(0u);
    vcl::VuKernelRenameHint hint;
    hint.reg = "VF12"; hint.entry = 0; hint.stage = 0; hint.kind = 1;
    plan.kernelRewriteRenameHints.push_back(hint);
    CHECK(!vcl::isVuPlanEligibleForGenericKernelRewrite(plan));
}

TEST_CASE("VuSchedulerAnalysis: single-stage layout is not eligible for generic kernel rewrite")
{
    vcl::VuSoftwarePipelineRewritePlan plan;
    plan.kernelRewriteII = 4u;
    plan.kernelRewriteStageCount = 1u;
    plan.kernelRewriteMainTokens.push_back(0u);
    CHECK(!vcl::isVuPlanEligibleForGenericKernelRewrite(plan));
}

// Track 9.G step 7b: SCE-style multi-stage emitter — synthetic-plan coverage.
TEST_CASE("VuSchedulerAnalysis: generic kernel rewrite emits PRO1/MAIN_LOOP/EPI0/EPI1 labels for an eligible synthetic plan")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("setup_lid:"));
    REQUIRE(program.parse("iaddiu vi01, vi00, 0"));
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("--LoopCS 1, 3"));
    REQUIRE(program.parse("lq.xyz vf01, 0(vi02)"));
    REQUIRE(program.parse("iaddiu vi02, vi02, 3"));
    REQUIRE(program.parse("ibne vi02, vi03, loop_lid"));
    REQUIRE(program.parse("done_lid:"));

    // Construct a synthetic plan with the layout the helper considers eligible:
    // II=2, stageCount=2, no conflicts, no rename hints, non-empty MAIN.
    vcl::VuSoftwarePipelineRewritePlan plan;
    plan.label = "loop_lid";
    plan.labelTokenIndex = 2u;   // "loop_lid:" pure-label token
    plan.branchTokenIndex = 6u;  // "ibne vi02, vi03, loop_lid"
    plan.kernelRewriteII = 2u;
    plan.kernelRewriteStageCount = 2u;
    plan.kernelRewriteConflicts = 0u;
    plan.kernelRewritePrologTokens.push_back(4u);   // lq
    plan.kernelRewriteMainTokens.push_back(4u);     // lq
    plan.kernelRewriteMainTokens.push_back(5u);     // iaddiu
    plan.kernelRewriteDrainTokens.push_back(5u);    // iaddiu
    REQUIRE(vcl::isVuPlanEligibleForGenericKernelRewrite(plan));

    std::vector<vcl::VuSoftwarePipelineRewritePlan> plans;
    plans.push_back(plan);

    std::list<vcl::Token> output =
        vcl::applyVuGenericKernelRewritePlans(program.tokenizer.tokens(), plans);

    std::vector<std::string> labelsInOrder;
    std::vector<std::string> branchTargets;
    for (std::list<vcl::Token>::const_iterator t = output.begin(); t != output.end(); ++t) {
        if (t->label().length() != 0u)
            labelsInOrder.push_back(t->label());
        for (std::list<vcl::Token::Argument>::const_iterator a = t->arguments().begin();
             a != t->arguments().end(); ++a) {
            if ((a->flags() & vcl::Token::Argument::BRANCH)
                && a->type() == vcl::Token::Argument::IMMEDIATE) {
                branchTargets.push_back(a->immediate());
            }
        }
    }

    // Labels must appear in SCE order: setup_lid, loop_lid__PRO1, loop_lid__MAIN_LOOP,
    // loop_lid__EPI0, loop_lid__EPI1, done_lid.
    REQUIRE(labelsInOrder.size() >= 6u);
    CHECK(labelsInOrder[0] == "setup_lid");
    CHECK(labelsInOrder[1] == "loop_lid__PRO1");
    CHECK(labelsInOrder[2] == "loop_lid__MAIN_LOOP");
    CHECK(labelsInOrder[3] == "loop_lid__EPI0");
    CHECK(labelsInOrder[4] == "loop_lid__EPI1");
    CHECK(labelsInOrder[5] == "done_lid");

    // Two emitted branches: prolog branch -> __EPI1, main branch -> __MAIN_LOOP.
    REQUIRE(branchTargets.size() == 2u);
    CHECK(branchTargets[0] == "loop_lid__EPI1");
    CHECK(branchTargets[1] == "loop_lid__MAIN_LOOP");
}

TEST_CASE("VuSchedulerAnalysis: generic kernel rewrite leaves tokens unchanged when no plan is eligible")
{
    vcl::Error::ResetErrorCount();
    ParsedProgram program;
    REQUIRE(program.parse("setup_lid:"));
    REQUIRE(program.parse("loop_lid:"));
    REQUIRE(program.parse("lq.xyz vf01, 0(vi02)"));
    REQUIRE(program.parse("iaddiu vi02, vi02, 3"));
    REQUIRE(program.parse("ibne vi02, vi03, loop_lid"));

    // Ineligible plan (renameHints non-empty disqualifies it).
    vcl::VuSoftwarePipelineRewritePlan plan;
    plan.label = "loop_lid";
    plan.labelTokenIndex = 1u;
    plan.branchTokenIndex = 4u;
    plan.kernelRewriteII = 2u;
    plan.kernelRewriteStageCount = 2u;
    plan.kernelRewriteMainTokens.push_back(2u);
    vcl::VuKernelRenameHint hint;
    hint.reg = "VF01"; hint.entry = 0; hint.stage = 0; hint.kind = 1;
    plan.kernelRewriteRenameHints.push_back(hint);
    REQUIRE(!vcl::isVuPlanEligibleForGenericKernelRewrite(plan));

    std::vector<vcl::VuSoftwarePipelineRewritePlan> plans;
    plans.push_back(plan);

    std::list<vcl::Token> output =
        vcl::applyVuGenericKernelRewritePlans(program.tokenizer.tokens(), plans);

    CHECK(output.size() == program.tokenizer.tokens().size());
}

// Track 9.G step 6e: VuKernelRewritePlan scaffolding fields default to zero/empty.
TEST_CASE("VuSchedulerAnalysis: kernel-rewrite scaffolding defaults are zero/empty")
{
    vcl::VuLoopPipelineOpportunity opp;
    CHECK(opp.kernelRewriteII == 0u);
    CHECK(opp.kernelRewriteStageCount == 0u);
    CHECK(opp.kernelRewriteConflicts == 0u);
    CHECK(opp.kernelRewritePrologTokens.empty());
    CHECK(opp.kernelRewriteMainTokens.empty());
    CHECK(opp.kernelRewriteDrainTokens.empty());
    CHECK(opp.kernelRewriteEntryStages.empty());

    vcl::VuSoftwarePipelineRewritePlan plan;
    CHECK(plan.kernelRewriteII == 0u);
    CHECK(plan.kernelRewriteStageCount == 0u);
    CHECK(plan.kernelRewriteConflicts == 0u);
    CHECK(plan.kernelRewritePrologTokens.empty());
    CHECK(plan.kernelRewriteMainTokens.empty());
    CHECK(plan.kernelRewriteDrainTokens.empty());
    CHECK(plan.kernelRewriteEntryStages.empty());
}

// Track 9.G step 6f: register-rewrite scaffolding fields default to zero/empty.
TEST_CASE("VuSchedulerAnalysis: register-rewrite scaffolding defaults are zero/empty")
{
    vcl::VuLoopPipelineOpportunity opp;
    CHECK(opp.kernelRewriteRegCount == 0u);
    CHECK(opp.kernelRewriteWawCount == 0u);
    CHECK(opp.kernelRewriteRawCount == 0u);
    CHECK(opp.kernelRewriteWarCount == 0u);
    CHECK(opp.kernelRewriteHazards.empty());
    CHECK(opp.kernelRewriteRenameHints.empty());

    vcl::VuSoftwarePipelineRewritePlan plan;
    CHECK(plan.kernelRewriteRegCount == 0u);
    CHECK(plan.kernelRewriteWawCount == 0u);
    CHECK(plan.kernelRewriteRawCount == 0u);
    CHECK(plan.kernelRewriteWarCount == 0u);
    CHECK(plan.kernelRewriteHazards.empty());
    CHECK(plan.kernelRewriteRenameHints.empty());
}

// Track 9.G step 6g: kernel-envelope scaffolding fields default to zero/empty.
TEST_CASE("VuSchedulerAnalysis: kernel-envelope scaffolding defaults are zero/empty")
{
    vcl::VuLoopPipelineOpportunity opp;
    CHECK(opp.kernelEnvelopeKernelTokens == 0u);
    CHECK(opp.kernelEnvelopePrologueCycles == 0u);
    CHECK(opp.kernelEnvelopeEpilogueCycles == 0u);
    CHECK(opp.kernelEnvelopeConflicts == 0u);
    CHECK(opp.kernelEnvelopePrologueTokenCounts.empty());
    CHECK(opp.kernelEnvelopeEpilogueTokenCounts.empty());

    vcl::VuSoftwarePipelineRewritePlan plan;
    CHECK(plan.kernelEnvelopeKernelTokens == 0u);
    CHECK(plan.kernelEnvelopePrologueCycles == 0u);
    CHECK(plan.kernelEnvelopeEpilogueCycles == 0u);
    CHECK(plan.kernelEnvelopeConflicts == 0u);
    CHECK(plan.kernelEnvelopePrologueTokenCounts.empty());
    CHECK(plan.kernelEnvelopeEpilogueTokenCounts.empty());
}

// Track 9.G step 6h: stageCells scaffolding default is empty.
TEST_CASE("VuSchedulerAnalysis: stageCells scaffolding defaults to empty")
{
    vcl::VuLoopPipelineOpportunity opp;
    CHECK(opp.kernelRewriteStageCells.empty());

    vcl::VuSoftwarePipelineRewritePlan plan;
    CHECK(plan.kernelRewriteStageCells.empty());
}

// ---------------------------------------------------------------------------
// Track 9.G step 8b-2c-1: per-field op-splitter for kernel rename.
// ---------------------------------------------------------------------------

namespace
{
    std::string stripOpSuffix(const std::string& name)
    {
        std::string s = name;
        std::string::size_type bracket = s.find('[');
        if (bracket != std::string::npos) s = s.substr(0, bracket);
        std::string::size_type dot = s.find('.');
        if (dot != std::string::npos) s = s.substr(0, dot);
        return s;
    }
    const vcl::Token* findFirstOpToken(const std::list<vcl::Token>& tokens,
                                       const std::string& opName)
    {
        for (std::list<vcl::Token>::const_iterator i = tokens.begin(); i != tokens.end(); ++i)
        {
            if (stripOpSuffix(i->name()) == opName)
                return &*i;
        }
        return NULL;
    }

    int destRegNumber(const vcl::Token& token)
    {
        for (std::list<vcl::Token::Argument>::const_iterator a = token.arguments().begin();
             a != token.arguments().end(); ++a)
        {
            if (a->type() != vcl::Token::Argument::FLOAT_REGISTER) continue;
            if ((a->flags() & vcl::Token::Argument::DEST) == 0) continue;
            if ((a->flags() & vcl::Token::Argument::WRITE) == 0) continue;
            return a->regNumber();
        }
        return -1;
    }

    unsigned int destFields(const vcl::Token& token)
    {
        for (std::list<vcl::Token::Argument>::const_iterator a = token.arguments().begin();
             a != token.arguments().end(); ++a)
        {
            if (a->type() != vcl::Token::Argument::FLOAT_REGISTER) continue;
            if ((a->flags() & vcl::Token::Argument::DEST) == 0) continue;
            if ((a->flags() & vcl::Token::Argument::WRITE) == 0) continue;
            return a->fields();
        }
        return 0;
    }
}

TEST_CASE("VuSchedulerAnalysis 8b-2c-1: vuOpIsSplittableForKernelRename allowlist")
{
    ParsedProgram program;
    REQUIRE(program.parse("add.xyzw vf13, vf13, vf14"));
    REQUIRE(program.parse("sub.xy vf13, vf13, vf14"));
    REQUIRE(program.parse("muli.xyz vf13, vf13, i"));
    REQUIRE(program.parse("maddw.xyzw vf13, vf13, vf14"));
    REQUIRE(program.parse("mini.xyzw vf13, vf13, vf14"));

    REQUIRE(program.parse("opmula.xyz acc, vf13, vf14"));
    // 8b-2d-2: move/ftoi*/itof*/abs/mfir are now per-field-safe and
    // appear on the splittable allowlist; mr32 (lane rotation) is not.
    REQUIRE(program.parse("move.xyzw vf13, vf14"));
    REQUIRE(program.parse("ftoi4.xyzw vf13, vf14"));
    REQUIRE(program.parse("itof12.xyz vf13, vf14"));
    REQUIRE(program.parse("abs.xyzw vf13, vf14"));
    REQUIRE(program.parse("mfir.w vf13, vi01"));
    REQUIRE(program.parse("mr32.xyzw vf13, vf14"));

    const std::list<vcl::Token>& tokens = program.tokenizer.tokens();
    const vcl::Token* add = findFirstOpToken(tokens, "add");
    const vcl::Token* sub = findFirstOpToken(tokens, "sub");
    const vcl::Token* muli = findFirstOpToken(tokens, "muli");
    const vcl::Token* maddw = findFirstOpToken(tokens, "maddw");
    const vcl::Token* mini = findFirstOpToken(tokens, "mini");
    const vcl::Token* opmula = findFirstOpToken(tokens, "opmula");
    const vcl::Token* move = findFirstOpToken(tokens, "move");
    const vcl::Token* ftoi4 = findFirstOpToken(tokens, "ftoi4");
    const vcl::Token* itof12 = findFirstOpToken(tokens, "itof12");
    const vcl::Token* absOp = findFirstOpToken(tokens, "abs");
    const vcl::Token* mfir = findFirstOpToken(tokens, "mfir");
    const vcl::Token* mr32 = findFirstOpToken(tokens, "mr32");
    REQUIRE(add != NULL);
    REQUIRE(sub != NULL);
    REQUIRE(muli != NULL);
    REQUIRE(maddw != NULL);
    REQUIRE(mini != NULL);
    REQUIRE(opmula != NULL);
    REQUIRE(move != NULL);
    REQUIRE(ftoi4 != NULL);
    REQUIRE(itof12 != NULL);
    REQUIRE(absOp != NULL);
    REQUIRE(mfir != NULL);
    REQUIRE(mr32 != NULL);

    CHECK(vcl::vuOpIsSplittableForKernelRename(*add));
    CHECK(vcl::vuOpIsSplittableForKernelRename(*sub));
    CHECK(vcl::vuOpIsSplittableForKernelRename(*muli));
    CHECK(vcl::vuOpIsSplittableForKernelRename(*maddw));
    CHECK(vcl::vuOpIsSplittableForKernelRename(*mini));
    CHECK(vcl::vuOpIsSplittableForKernelRename(*move));
    CHECK(vcl::vuOpIsSplittableForKernelRename(*ftoi4));
    CHECK(vcl::vuOpIsSplittableForKernelRename(*itof12));
    CHECK(vcl::vuOpIsSplittableForKernelRename(*absOp));
    CHECK(vcl::vuOpIsSplittableForKernelRename(*mfir));

    CHECK(!vcl::vuOpIsSplittableForKernelRename(*opmula));
    CHECK(!vcl::vuOpIsSplittableForKernelRename(*mr32));
}

TEST_CASE("VuSchedulerAnalysis 8b-2c-1: splitMultiFieldOpByFieldDecisions passes through unsplittable ops")
{
    ParsedProgram program;
    REQUIRE(program.parse("mr32.xyzw vf13, vf14"));
    const vcl::Token* mr32 = findFirstOpToken(program.tokenizer.tokens(), "mr32");
    REQUIRE(mr32 != NULL);

    std::vector<vcl::VuKernelRenameDecision> decisions;
    vcl::VuKernelRenameDecision d;
    d.reg = "VF13.w"; d.scratch = "VF31"; d.assigned = true;
    decisions.push_back(d);

    std::list<vcl::Token> out;
    vcl::splitMultiFieldOpByFieldDecisions(*mr32, decisions, out);
    CHECK(out.size() == 1u);
    CHECK(stripOpSuffix(out.front().name()) == "mr32");
    CHECK(destRegNumber(out.front()) == 13);
}

TEST_CASE("VuSchedulerAnalysis 8b-2c-1: splitMultiFieldOpByFieldDecisions passes through when no decision matches dest base")
{
    ParsedProgram program;
    REQUIRE(program.parse("add.xyzw vf20, vf20, vf14"));
    const vcl::Token* add = findFirstOpToken(program.tokenizer.tokens(), "add");
    REQUIRE(add != NULL);

    std::vector<vcl::VuKernelRenameDecision> decisions;
    vcl::VuKernelRenameDecision d;
    d.reg = "VF13.w"; d.scratch = "VF31"; d.assigned = true;
    decisions.push_back(d);

    std::list<vcl::Token> out;
    vcl::splitMultiFieldOpByFieldDecisions(*add, decisions, out);
    CHECK(out.size() == 1u);
    CHECK(destRegNumber(out.front()) == 20);
    CHECK(destFields(out.front()) == (vcl::Token::X | vcl::Token::Y | vcl::Token::Z | vcl::Token::W));
}

TEST_CASE("VuSchedulerAnalysis 8b-2c-1: splitMultiFieldOpByFieldDecisions full-mask split across four per-field decisions")
{
    ParsedProgram program;
    REQUIRE(program.parse("add.xyzw vf13, vf13, vf14"));
    const vcl::Token* add = findFirstOpToken(program.tokenizer.tokens(), "add");
    REQUIRE(add != NULL);

    std::vector<vcl::VuKernelRenameDecision> decisions;
    const char* fields[4] = { "x", "y", "z", "w" };
    const char* scratches[4] = { "VF30", "VF29", "VF28", "VF31" };
    for (unsigned int i = 0; i < 4; ++i)
    {
        vcl::VuKernelRenameDecision d;
        d.reg = std::string("VF13.") + fields[i];
        d.scratch = scratches[i];
        d.assigned = true;
        decisions.push_back(d);
    }

    std::list<vcl::Token> out;
    vcl::splitMultiFieldOpByFieldDecisions(*add, decisions, out);
    CHECK(out.size() == 4u);

    const unsigned int bits[4] = { vcl::Token::X, vcl::Token::Y, vcl::Token::Z, vcl::Token::W };
    const int expectedRegs[4] = { 30, 29, 28, 31 };
    unsigned int idx = 0;
    for (std::list<vcl::Token>::const_iterator i = out.begin(); i != out.end(); ++i, ++idx)
    {
        CHECK(stripOpSuffix(i->name()) == "add");
        CHECK(destFields(*i) == bits[idx]);
        CHECK(destRegNumber(*i) == expectedRegs[idx]);
    }
}

TEST_CASE("VuSchedulerAnalysis 8b-2c-1: splitMultiFieldOpByFieldDecisions partial overlap emits split clones plus residual")
{
    ParsedProgram program;
    REQUIRE(program.parse("add.xyzw vf13, vf13, vf14"));
    const vcl::Token* add = findFirstOpToken(program.tokenizer.tokens(), "add");
    REQUIRE(add != NULL);

    std::vector<vcl::VuKernelRenameDecision> decisions;
    {
        vcl::VuKernelRenameDecision d;
        d.reg = "VF13.w"; d.scratch = "VF31"; d.assigned = true;
        decisions.push_back(d);
    }
    {
        vcl::VuKernelRenameDecision d;
        d.reg = "VF13.x"; d.scratch = "VF30"; d.assigned = true;
        decisions.push_back(d);
    }

    std::list<vcl::Token> out;
    vcl::splitMultiFieldOpByFieldDecisions(*add, decisions, out);
    // Two single-field renames (.x -> vf30, .w -> vf31) + one residual (.yz on vf13).
    CHECK(out.size() == 3u);

    std::list<vcl::Token>::const_iterator it = out.begin();
    CHECK(it->fields() == vcl::Token::X);
    CHECK(destRegNumber(*it) == 30);
    ++it;
    CHECK(it->fields() == vcl::Token::W);
    CHECK(destRegNumber(*it) == 31);
    ++it;
    CHECK(it->fields() == (vcl::Token::Y | vcl::Token::Z));
    CHECK(destFields(*it) == (vcl::Token::Y | vcl::Token::Z));
    CHECK(destRegNumber(*it) == 13);
}

TEST_CASE("VuSchedulerAnalysis 8b-2c-1: splitMultiFieldOpByFieldDecisions ignores unassigned decisions")
{
    ParsedProgram program;
    REQUIRE(program.parse("add.xyzw vf13, vf13, vf14"));
    const vcl::Token* add = findFirstOpToken(program.tokenizer.tokens(), "add");
    REQUIRE(add != NULL);

    std::vector<vcl::VuKernelRenameDecision> decisions;
    vcl::VuKernelRenameDecision d;
    d.reg = "VF13.w"; d.scratch = ""; d.assigned = false;
    decisions.push_back(d);

    std::list<vcl::Token> out;
    vcl::splitMultiFieldOpByFieldDecisions(*add, decisions, out);
    CHECK(out.size() == 1u);
    CHECK(destRegNumber(out.front()) == 13);
    CHECK(destFields(out.front()) == (vcl::Token::X | vcl::Token::Y | vcl::Token::Z | vcl::Token::W));
}

TEST_CASE("VuSchedulerAnalysis 8b-2c-1: splitMultiFieldOpByFieldDecisions decision without dot defaults to all fields")
{
    ParsedProgram program;
    REQUIRE(program.parse("add.xyzw vf13, vf13, vf14"));
    const vcl::Token* add = findFirstOpToken(program.tokenizer.tokens(), "add");
    REQUIRE(add != NULL);

    std::vector<vcl::VuKernelRenameDecision> decisions;
    vcl::VuKernelRenameDecision d;
    d.reg = "VF13"; d.scratch = "VF31"; d.assigned = true;
    decisions.push_back(d);

    std::list<vcl::Token> out;
    vcl::splitMultiFieldOpByFieldDecisions(*add, decisions, out);
    // Single decision covers all four fields -> four single-field clones, all to vf31.
    CHECK(out.size() == 4u);
    const unsigned int bits[4] = { vcl::Token::X, vcl::Token::Y, vcl::Token::Z, vcl::Token::W };
    unsigned int idx = 0;
    for (std::list<vcl::Token>::const_iterator i = out.begin(); i != out.end(); ++i, ++idx)
    {
        CHECK(i->fields() == bits[idx]);
        CHECK(destRegNumber(*i) == 31);
    }
}

TEST_CASE("VuSchedulerAnalysis 8b-2e: splitMultiFieldOpByFieldDecisions clears multi-bit source-arg field annotations")
{
    // Repro for the dvp-as syntax bug surfaced once 8b-2d-2/8b-2d-3 made
    // more loops eligible: the parser stamps the opcode-level mask
    // (e.g. .xyz) onto FLOAT_REGISTER source args too. The per-field
    // splitter must clear those multi-bit annotations on the clones so
    // the printer renders `VF07` instead of `VF07xyz` (no dot).
    ParsedProgram program;
    REQUIRE(program.parse("ftoi4.xyz vf08, vf07"));
    const vcl::Token* op = findFirstOpToken(program.tokenizer.tokens(), "ftoi4");
    REQUIRE(op != NULL);

    std::vector<vcl::VuKernelRenameDecision> decisions;
    vcl::VuKernelRenameDecision d;
    d.reg = "VF08"; d.scratch = "VF31"; d.assigned = true;
    decisions.push_back(d);

    std::list<vcl::Token> out;
    vcl::splitMultiFieldOpByFieldDecisions(*op, decisions, out);
    // Three single-field clones for x, y, z.
    REQUIRE(out.size() == 3u);

    for (std::list<vcl::Token>::const_iterator i = out.begin(); i != out.end(); ++i)
    {
        for (std::list<vcl::Token::Argument>::const_iterator a = i->arguments().begin();
             a != i->arguments().end(); ++a)
        {
            if (a->type() != vcl::Token::Argument::FLOAT_REGISTER) continue;
            const bool isDestWrite =
                ((a->flags() & vcl::Token::Argument::DEST) != 0) &&
                ((a->flags() & vcl::Token::Argument::WRITE) != 0);
            if (isDestWrite) continue;
            const unsigned int af = a->fields();
            const bool singleBit = (af != 0) && ((af & (af - 1)) == 0);
            // Source args must carry zero or a single-bit broadcast.
            CHECK((af == 0 || singleBit));
        }
    }
}

// ---------------------------------------------------------------------------
// Track 9.G step 8b-2c-2: eligibility gate for per-field kernel-rename
// emission. Pure-analysis predicate (no emission yet); MD5-invariant.
// ---------------------------------------------------------------------------

namespace
{
    // Build an indexedTokens vector from a parsed program's token list,
    // preserving source order. Returns the vector by value; callers must
    // keep the ParsedProgram alive for the pointer lifetime.
    std::vector<const vcl::Token*> buildIndexedTokens(const std::list<vcl::Token>& tokens)
    {
        std::vector<const vcl::Token*> out;
        for (std::list<vcl::Token>::const_iterator i = tokens.begin(); i != tokens.end(); ++i)
            out.push_back(&*i);
        return out;
    }

    // Construct an 8b-2b-style scaffolded plan with one decision +
    // one move-slot, both assigned. mainTokens covers indices [0, n).
    vcl::VuSoftwarePipelineRewritePlan makeScaffoldedPlan(unsigned int mainTokenCount,
                                                          const std::string& renamedBase,
                                                          const std::string& scratch)
    {
        vcl::VuSoftwarePipelineRewritePlan plan;
        plan.kernelRewriteII = 4u;
        plan.kernelRewriteStageCount = 2u;
        plan.kernelRewriteConflicts = 0u;
        for (unsigned int m = 0; m < mainTokenCount; ++m)
            plan.kernelRewriteMainTokens.push_back(m);
        vcl::VuKernelRenameDecision d;
        d.reg = renamedBase; d.scratch = scratch; d.assigned = true;
        plan.kernelRewriteRenameDecisions.push_back(d);
        vcl::VuKernelRenameMoveSlot s;
        s.decisionIndex = 0u; s.modSlot = 0u; s.lane = 1; s.assigned = true;
        plan.kernelRewriteRenameMoveSlots.push_back(s);
        return plan;
    }
}

TEST_CASE("VuSchedulerAnalysis 8b-2c-2: default plan is not eligible for kernel-rename emission")
{
    vcl::VuSoftwarePipelineRewritePlan plan;
    std::vector<const vcl::Token*> empty;
    CHECK(!vcl::isVuPlanEligibleForKernelRenameEmission(plan, empty));
    CHECK(vcl::countKernelRenameEmissionBlockers(plan, empty) == 0u);
}

TEST_CASE("VuSchedulerAnalysis 8b-2c-2: plan with assigned decision+slot and splittable body is eligible")
{
    ParsedProgram program;
    REQUIRE(program.parse("add.xyzw vf13, vf13, vf14"));
    REQUIRE(program.parse("sub.xy vf13, vf13, vf14"));
    const std::vector<const vcl::Token*> idx = buildIndexedTokens(program.tokenizer.tokens());

    vcl::VuSoftwarePipelineRewritePlan plan =
        makeScaffoldedPlan(static_cast<unsigned int>(idx.size()), "VF13", "VF31");
    CHECK(vcl::isVuPlanEligibleForKernelRenameEmission(plan, idx));
    CHECK(vcl::countKernelRenameEmissionBlockers(plan, idx) == 0u);
}

TEST_CASE("VuSchedulerAnalysis 8b-2c-2: unsplittable op writing decision base hard-blocks eligibility")
{
    ParsedProgram program;
    REQUIRE(program.parse("add.xyzw vf13, vf13, vf14"));
    // mr32 writes the whole quad atomically (cross-field rotate) — on the
    // unsplittable list per 8b-2c-1; here it writes VF13 so it is a hard
    // blocker under 8b-2d-3 semantics (writes a decision base).
    REQUIRE(program.parse("mr32.xyzw vf13, vf20"));
    const std::vector<const vcl::Token*> idx = buildIndexedTokens(program.tokenizer.tokens());

    vcl::VuSoftwarePipelineRewritePlan plan =
        makeScaffoldedPlan(static_cast<unsigned int>(idx.size()), "VF13", "VF31");
    CHECK(!vcl::isVuPlanEligibleForKernelRenameEmission(plan, idx));
    CHECK(vcl::countKernelRenameEmissionBlockers(plan, idx) == 1u);
}

TEST_CASE("VuSchedulerAnalysis 8b-2c-2: unsplittable op not touching any decision base is fine")
{
    ParsedProgram program;
    REQUIRE(program.parse("add.xyzw vf13, vf13, vf14"));
    // opmula on unrelated registers — must not block eligibility.
    REQUIRE(program.parse("opmula.xyz ACC, vf20, vf21"));
    const std::vector<const vcl::Token*> idx = buildIndexedTokens(program.tokenizer.tokens());

    vcl::VuSoftwarePipelineRewritePlan plan =
        makeScaffoldedPlan(static_cast<unsigned int>(idx.size()), "VF13", "VF31");
    CHECK(vcl::isVuPlanEligibleForKernelRenameEmission(plan, idx));
    CHECK(vcl::countKernelRenameEmissionBlockers(plan, idx) == 0u);
}

TEST_CASE("VuSchedulerAnalysis 8b-2c-2: unassigned decision disqualifies the plan")
{
    ParsedProgram program;
    REQUIRE(program.parse("add.xyzw vf13, vf13, vf14"));
    const std::vector<const vcl::Token*> idx = buildIndexedTokens(program.tokenizer.tokens());

    vcl::VuSoftwarePipelineRewritePlan plan =
        makeScaffoldedPlan(static_cast<unsigned int>(idx.size()), "VF13", "VF31");
    plan.kernelRewriteRenameDecisions[0].assigned = false;
    CHECK(!vcl::isVuPlanEligibleForKernelRenameEmission(plan, idx));
    // Scaffolding incomplete -> blocker count returns 0 (predicate already rejected).
    CHECK(vcl::countKernelRenameEmissionBlockers(plan, idx) == 0u);
}

TEST_CASE("VuSchedulerAnalysis 8b-2c-2: unassigned move-slot disqualifies the plan")
{
    ParsedProgram program;
    REQUIRE(program.parse("add.xyzw vf13, vf13, vf14"));
    const std::vector<const vcl::Token*> idx = buildIndexedTokens(program.tokenizer.tokens());

    vcl::VuSoftwarePipelineRewritePlan plan =
        makeScaffoldedPlan(static_cast<unsigned int>(idx.size()), "VF13", "VF31");
    plan.kernelRewriteRenameMoveSlots[0].assigned = false;
    CHECK(!vcl::isVuPlanEligibleForKernelRenameEmission(plan, idx));
    CHECK(vcl::countKernelRenameEmissionBlockers(plan, idx) == 0u);
}

TEST_CASE("VuSchedulerAnalysis 8b-2c-2: mismatched decisions/moveSlots sizes disqualify the plan")
{
    ParsedProgram program;
    REQUIRE(program.parse("add.xyzw vf13, vf13, vf14"));
    const std::vector<const vcl::Token*> idx = buildIndexedTokens(program.tokenizer.tokens());

    vcl::VuSoftwarePipelineRewritePlan plan =
        makeScaffoldedPlan(static_cast<unsigned int>(idx.size()), "VF13", "VF31");
    // Add a second decision without a matching move-slot.
    vcl::VuKernelRenameDecision d2;
    d2.reg = "VF14.w"; d2.scratch = "VF30"; d2.assigned = true;
    plan.kernelRewriteRenameDecisions.push_back(d2);
    CHECK(!vcl::isVuPlanEligibleForKernelRenameEmission(plan, idx));
}

TEST_CASE("VuSchedulerAnalysis 8b-2c-2: empty decisions vector disqualifies the plan even with valid base scaffolding")
{
    ParsedProgram program;
    REQUIRE(program.parse("add.xyzw vf13, vf13, vf14"));
    const std::vector<const vcl::Token*> idx = buildIndexedTokens(program.tokenizer.tokens());

    vcl::VuSoftwarePipelineRewritePlan plan;
    plan.kernelRewriteII = 4u;
    plan.kernelRewriteStageCount = 2u;
    plan.kernelRewriteMainTokens.push_back(0u);
    // No decisions / no move-slots: kernel-rename emission is meaningless.
    CHECK(!vcl::isVuPlanEligibleForKernelRenameEmission(plan, idx));
}

TEST_CASE("VuSchedulerAnalysis 8b-2c-2: per-field decision (VF13.w) matches base VF13 for splittable check")
{
    ParsedProgram program;
    REQUIRE(program.parse("add.xyzw vf13, vf13, vf14"));
    const std::vector<const vcl::Token*> idx = buildIndexedTokens(program.tokenizer.tokens());

    vcl::VuSoftwarePipelineRewritePlan plan =
        makeScaffoldedPlan(static_cast<unsigned int>(idx.size()), "VF13.w", "VF31");
    CHECK(vcl::isVuPlanEligibleForKernelRenameEmission(plan, idx));
}

TEST_CASE("VuSchedulerAnalysis 8b-2c-2: kernelRewriteRenameHints non-empty does NOT disqualify (rename is the point)")
{
    ParsedProgram program;
    REQUIRE(program.parse("add.xyzw vf13, vf13, vf14"));
    const std::vector<const vcl::Token*> idx = buildIndexedTokens(program.tokenizer.tokens());

    vcl::VuSoftwarePipelineRewritePlan plan =
        makeScaffoldedPlan(static_cast<unsigned int>(idx.size()), "VF13", "VF31");
    vcl::VuKernelRenameHint hint;
    hint.reg = "VF13"; hint.entry = 0; hint.stage = 0; hint.kind = 1;
    plan.kernelRewriteRenameHints.push_back(hint);
    CHECK(vcl::isVuPlanEligibleForKernelRenameEmission(plan, idx));
}

// Track 9.G step 8b-2d-3: hard vs soft blocker classification.
TEST_CASE("VuSchedulerAnalysis 8b-2d-3: read-only unsplittable touch is soft, NOT hard")
{
    ParsedProgram program;
    REQUIRE(program.parse("add.xyzw vf13, vf13, vf14"));
    // sq writes memory, reads VF13 — unsplittable but read-only on the
    // decision base. Under 8b-2d-3 this is a SOFT blocker, handled by
    // materialize-before-read.
    REQUIRE(program.parse("sq vf13, 0(vi05)"));
    const std::vector<const vcl::Token*> idx = buildIndexedTokens(program.tokenizer.tokens());

    vcl::VuSoftwarePipelineRewritePlan plan =
        makeScaffoldedPlan(static_cast<unsigned int>(idx.size()), "VF13", "VF31");
    CHECK(vcl::isVuPlanEligibleForKernelRenameEmission(plan, idx));
    CHECK(vcl::countKernelRenameEmissionBlockers(plan, idx) == 0u);
    CHECK(vcl::countKernelRenameEmissionSoftBlockers(plan, idx) == 1u);
    const std::vector<std::string> soft =
        vcl::describeKernelRenameEmissionSoftBlockers(plan, idx);
    REQUIRE(soft.size() == 1u);
    CHECK(soft[0] == "sq");
}

TEST_CASE("VuSchedulerAnalysis 8b-2d-3: writing unsplittable touch is hard, NOT soft")
{
    ParsedProgram program;
    REQUIRE(program.parse("add.xyzw vf13, vf13, vf14"));
    REQUIRE(program.parse("mr32.xyzw vf13, vf20"));
    const std::vector<const vcl::Token*> idx = buildIndexedTokens(program.tokenizer.tokens());

    vcl::VuSoftwarePipelineRewritePlan plan =
        makeScaffoldedPlan(static_cast<unsigned int>(idx.size()), "VF13", "VF31");
    CHECK(!vcl::isVuPlanEligibleForKernelRenameEmission(plan, idx));
    CHECK(vcl::countKernelRenameEmissionBlockers(plan, idx) == 1u);
    CHECK(vcl::countKernelRenameEmissionSoftBlockers(plan, idx) == 0u);
}

TEST_CASE("VuSchedulerAnalysis 8b-2d-3: tokenIsKernelRenameMaterializeCandidate identifies soft-blocker tokens")
{
    ParsedProgram program;
    REQUIRE(program.parse("add.xyzw vf13, vf13, vf14"));        // splittable -> not a candidate
    REQUIRE(program.parse("sq vf13, 0(vi05)"));                 // soft blocker -> candidate
    REQUIRE(program.parse("mr32.xyzw vf13, vf20"));             // hard blocker -> not a candidate
    REQUIRE(program.parse("sq vf09, 0(vi05)"));                 // doesn't touch decision base -> not a candidate
    const std::vector<const vcl::Token*> idx = buildIndexedTokens(program.tokenizer.tokens());

    vcl::VuSoftwarePipelineRewritePlan plan =
        makeScaffoldedPlan(static_cast<unsigned int>(idx.size()), "VF13", "VF31");
    REQUIRE(idx.size() == 4u);
    CHECK(!vcl::tokenIsKernelRenameMaterializeCandidate(*idx[0], plan));
    CHECK( vcl::tokenIsKernelRenameMaterializeCandidate(*idx[1], plan));
    CHECK(!vcl::tokenIsKernelRenameMaterializeCandidate(*idx[2], plan));
    CHECK(!vcl::tokenIsKernelRenameMaterializeCandidate(*idx[3], plan));
}
