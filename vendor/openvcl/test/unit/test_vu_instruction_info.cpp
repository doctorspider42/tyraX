#include "test_harness.h"

#include "../../src/VuInstructionInfo.h"

namespace
{
    const vcl::VuInstructionInfo* findOperandInfo(const char* name, unsigned int flags)
    {
        for (const vcl::VuInstructionInfo* info = vcl::allVuInstructionInfos(); info->mnemonic; ++info)
        {
            if (std::string(info->operandName) == name && info->operandFlags == flags)
                return info;
        }
        return 0;
    }
}

TEST_CASE("VuInstructionInfo: normalizes field and broadcast mnemonics")
{
    CHECK(vcl::normalizeVuMnemonic("mulw.xyz") == "mul");
    CHECK(vcl::normalizeVuMnemonic("maxx.w") == "max");
    CHECK(vcl::normalizeVuMnemonic("clipw.xyz") == "clipw");
    CHECK(vcl::normalizeVuMnemonic("nop[E]") == "nop");
}

TEST_CASE("VuInstructionInfo: opcode enum owns mnemonic strings")
{
    CHECK(std::string(vcl::vuInstructionMnemonic(vcl::VU_OP_LQ)) == "lq");
    CHECK(std::string(vcl::vuInstructionMnemonic(vcl::VU_OP_MULQ)) == "mulq");
    CHECK(std::string(vcl::vuInstructionMnemonic(vcl::VU_OP_WAITQ)) == "waitq");
    CHECK(vcl::vuInstructionOpcodeForMnemonic("lq") == vcl::VU_OP_LQ);
    CHECK(vcl::vuInstructionOpcodeForMnemonic("MULQ") == vcl::VU_OP_MULQ);
    CHECK(vcl::vuInstructionOpcodeForMnemonic("unknown") == vcl::VU_OP_INVALID);

    const vcl::VuInstructionInfo* lq = vcl::findVuInstructionInfo("lq");
    REQUIRE(lq != 0);
    CHECK(lq->opcode == vcl::VU_OP_LQ);
    CHECK(lq->mnemonic == vcl::vuInstructionMnemonic(vcl::VU_OP_LQ));
}

TEST_CASE("VuInstructionInfo: exposes Q and P producer costs")
{
    const vcl::VuInstructionInfo* div = vcl::findVuInstructionInfo("div");
    REQUIRE(div != 0);
    CHECK(div->pipe == vcl::VU_PIPE_LOWER);
    CHECK(div->unit == vcl::VU_EXEC_FDIV);
    CHECK(div->throughput == 7);
    CHECK(div->latency == 7);
    CHECK((div->flags & vcl::VU_INSTR_WRITES_Q) != 0);

    const vcl::VuInstructionInfo* ersqrt = vcl::findVuInstructionInfo("ersqrt");
    REQUIRE(ersqrt != 0);
    CHECK(ersqrt->unit == vcl::VU_EXEC_EFU);
    CHECK(ersqrt->throughput == 17);
    CHECK(ersqrt->latency == 18);
    CHECK((ersqrt->flags & vcl::VU_INSTR_WRITES_P) != 0);
}

TEST_CASE("VuInstructionInfo: MFP consumes P but does not produce P")
{
    const vcl::VuInstructionInfo* mfp = vcl::findVuInstructionInfo("mfp");
    REQUIRE(mfp != 0);
    CHECK(mfp->pipe == vcl::VU_PIPE_LOWER);
    CHECK(mfp->unit == vcl::VU_EXEC_EFU);
    CHECK(mfp->throughput == 1);
    CHECK(mfp->latency == 4);
    CHECK((mfp->flags & vcl::VU_INSTR_WRITES_P) == 0);
}

TEST_CASE("VuInstructionInfo: multiply-add families read ACC")
{
    const vcl::VuInstructionInfo* madd = vcl::findVuInstructionInfo("madd");
    REQUIRE(madd != 0);
    CHECK((madd->implicitReads & vcl::VU_RESOURCE_ACC) != 0);
    CHECK((madd->implicitWrites & vcl::VU_RESOURCE_MAC) != 0);

    const vcl::VuInstructionInfo* madda = vcl::findVuInstructionInfo("madda");
    REQUIRE(madda != 0);
    CHECK((madda->implicitReads & vcl::VU_RESOURCE_ACC) != 0);
    CHECK((madda->implicitWrites & vcl::VU_RESOURCE_ACC) != 0);
    CHECK((madda->implicitWrites & vcl::VU_RESOURCE_MAC) != 0);

    const vcl::VuInstructionInfo* msubq = vcl::findVuInstructionInfo("msubq");
    REQUIRE(msubq != 0);
    CHECK((msubq->implicitReads & vcl::VU_RESOURCE_ACC) != 0);
    CHECK((msubq->implicitReads & vcl::VU_RESOURCE_Q) != 0);
}

TEST_CASE("VuInstructionInfo: wait and branch metadata is explicit")
{
    const vcl::VuInstructionInfo* waitq = vcl::findVuInstructionInfo("waitq");
    REQUIRE(waitq != 0);
    CHECK((waitq->flags & vcl::VU_INSTR_WAIT_Q) != 0);

    const vcl::VuInstructionInfo* waitp = vcl::findVuInstructionInfo("waitp");
    REQUIRE(waitp != 0);
    CHECK((waitp->flags & vcl::VU_INSTR_WAIT_P) != 0);

    const vcl::VuInstructionInfo* branch = vcl::findVuInstructionInfo("ibne");
    REQUIRE(branch != 0);
    CHECK(branch->unit == vcl::VU_EXEC_BRU);
    CHECK(branch->latency == 2);
    CHECK((branch->flags & vcl::VU_INSTR_BRANCH) != 0);
    CHECK(branch->branchDelaySlots == 1);

    const vcl::VuInstructionInfo* unconditional = vcl::findVuInstructionInfo("b");
    REQUIRE(unconditional != 0);
    CHECK((unconditional->flags & vcl::VU_INSTR_BRANCH) != 0);
    CHECK((unconditional->flags & vcl::VU_INSTR_UNCONDITIONAL_BRANCH) != 0);
    CHECK((unconditional->flags & vcl::VU_INSTR_LINK_BRANCH) == 0);

    const vcl::VuInstructionInfo* jalr = vcl::findVuInstructionInfo("jalr");
    REQUIRE(jalr != 0);
    CHECK((jalr->flags & vcl::VU_INSTR_BRANCH) != 0);
    CHECK((jalr->flags & vcl::VU_INSTR_LINK_BRANCH) != 0);
    CHECK((jalr->flags & vcl::VU_INSTR_REGISTER_BRANCH) != 0);
}

TEST_CASE("VuInstructionInfo: iterator exposes parser metadata")
{
    unsigned int count = 0;
    for (const vcl::VuInstructionInfo* info = vcl::allVuInstructionInfos(); info->mnemonic; ++info)
    {
        ++count;
        CHECK(info->operandName != 0);
        CHECK(info->operandPattern != 0);
        CHECK(info->operandUnit != vcl::Operand::ENTER);
        CHECK(info->operandUnit != vcl::Operand::EXIT);
        CHECK((info->operandFlags & vcl::Operand::PREPROCESSOR) == 0);
    }

    CHECK(count > 80);
}

TEST_CASE("VuInstructionInfo: parser variants retain exact operand metadata")
{
    const vcl::VuInstructionInfo* add = findOperandInfo("ADD", vcl::Operand::UPPER | vcl::Operand::DEST);
    REQUIRE(add != 0);
    CHECK(std::string(add->operandPattern) == "vf:dest:write,vf:dest,vf:dest");
    CHECK(add->operandUnit == vcl::Operand::FMAC);
    CHECK(add->throughput == 1);
    CHECK(add->latency == 3);

    const vcl::VuInstructionInfo* addBroadcast = findOperandInfo("ADD", vcl::Operand::UPPER | vcl::Operand::BROADCAST);
    REQUIRE(addBroadcast != 0);
    CHECK(std::string(addBroadcast->operandPattern) == "vf:dest:write,vf:dest,vf:bc");

    const vcl::VuInstructionInfo* clipw = vcl::findVuInstructionInfo("clipw");
    REQUIRE(clipw != 0);
    CHECK(std::string(clipw->operandPattern) == "vf:dest,vf:wcomp");
    CHECK((clipw->implicitWrites & vcl::VU_RESOURCE_MAC) != 0);
    CHECK((clipw->implicitWrites & vcl::VU_RESOURCE_CLIP) != 0);
}

TEST_CASE("VuInstructionInfo: memory, special register, and bypass metadata are explicit")
{
    const vcl::VuInstructionInfo* loi = vcl::findVuInstructionInfo("loi");
    REQUIRE(loi != 0);
    CHECK((loi->operandFlags & vcl::Operand::IWRITE) != 0);
    CHECK((loi->implicitWrites & vcl::VU_RESOURCE_I) != 0);

    const vcl::VuInstructionInfo* lqi = vcl::findVuInstructionInfo("lqi");
    REQUIRE(lqi != 0);
    CHECK(lqi->memoryKind == vcl::VU_MEMORY_LOAD);
    CHECK((lqi->memoryFlags & vcl::VU_MEMORY_FLAG_POSTINC) != 0);
    CHECK((lqi->bypassFlags & vcl::VU_BYPASS_LOAD_TO_FTOI) != 0);
    CHECK((lqi->bypassFlags & vcl::VU_BYPASS_LOAD_TO_MINII) != 0);

    const vcl::VuInstructionInfo* sqd = vcl::findVuInstructionInfo("sqd");
    REQUIRE(sqd != 0);
    CHECK(sqd->memoryKind == vcl::VU_MEMORY_STORE);
    CHECK((sqd->memoryFlags & vcl::VU_MEMORY_FLAG_PREDEC) != 0);

    const vcl::VuInstructionInfo* xgkick = vcl::findVuInstructionInfo("xgkick");
    REQUIRE(xgkick != 0);
    CHECK(xgkick->memoryKind == vcl::VU_MEMORY_XGKICK);
    CHECK((xgkick->flags & vcl::VU_INSTR_XGKICK) != 0);

    const vcl::VuInstructionInfo* ftoi4 = vcl::findVuInstructionInfo("ftoi4");
    REQUIRE(ftoi4 != 0);
    CHECK((ftoi4->bypassFlags & vcl::VU_BYPASS_FTOI_TO_MTIR) != 0);
}

TEST_CASE("VuInstructionInfo: exposes readable parameter summaries and descriptions")
{
    const vcl::VuInstructionInfo* add = findOperandInfo("ADD", vcl::Operand::UPPER | vcl::Operand::DEST);
    REQUIRE(add != 0);
    CHECK(vcl::vuInstructionParameterSummary(*add) == "vf[dest,write], vf[dest], vf[dest]");
    CHECK(std::string(vcl::vuInstructionDescription(*add)) == "Add vector fields.");

    const vcl::VuInstructionInfo* addBroadcast = findOperandInfo("ADD", vcl::Operand::UPPER | vcl::Operand::BROADCAST);
    REQUIRE(addBroadcast != 0);
    CHECK(vcl::vuInstructionParameterSummary(*addBroadcast) == "vf[dest,write], vf[dest], vf[bc]");

    const vcl::VuInstructionInfo* div = vcl::findVuInstructionInfo("div");
    REQUIRE(div != 0);
    CHECK(vcl::vuInstructionParameterSummary(*div) == "q[write], vf[component], vf[component]");
    CHECK(std::string(vcl::vuInstructionDescription(*div)) == "Start a Q scalar divide/square-root operation.");

    const vcl::VuInstructionInfo* lq = vcl::findVuInstructionInfo("lq");
    REQUIRE(lq != 0);
    CHECK(vcl::vuInstructionParameterSummary(*lq) == "vf[dest,write], (vi)|imm(vi)");
    CHECK(std::string(vcl::vuInstructionDescription(*lq)) == "Load data from VU memory into a vector or integer register.");

    const vcl::VuInstructionInfo* ibne = vcl::findVuInstructionInfo("ibne");
    REQUIRE(ibne != 0);
    CHECK(vcl::vuInstructionParameterSummary(*ibne) == "vi, vi, label");
    CHECK(std::string(vcl::vuInstructionDescription(*ibne)) == "Branch after one delay slot when the integer condition is met.");
}
