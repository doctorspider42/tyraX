#include "test_harness.h"

#include "../../src/Error.h"
#include "../../src/File.h"
#include "../../src/Line.h"
#include "../../src/Operand.h"
#include "../../src/Token.h"
#include "../../src/Tokenizer.h"
#include "../../src/VuInstructionInfo.h"
#include "../../src/VuTokenResourceAccess.h"

#include <list>
#include <string>

namespace
{
    std::list<vcl::Operand> makeVuOperands()
    {
        std::list<vcl::Operand> ops;
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

    bool accessFor(const std::string& text, vcl::VuTokenResourceAccess& access)
    {
        static vcl::File f("<resource-test>");
        vcl::Tokenizer tokenizer;
        std::list<vcl::Operand> ops = makeVuOperands();
        tokenizer.setOperands(ops);
        tokenizer.setNewSyntax(true);
        vcl::Line line(f, 1, 1, text);
        vcl::Error::ResetErrorCount();
        if (!tokenizer.parse(line))
            return false;
        if (tokenizer.tokens().size() != 1u)
            return false;
        return vcl::buildVuTokenResourceAccess(tokenizer.tokens().front(), access);
    }

    bool hasKey(const std::list<std::string>& keys, const std::string& key)
    {
        for (std::list<std::string>::const_iterator i = keys.begin(); i != keys.end(); ++i)
        {
            if (*i == key)
                return true;
        }
        return false;
    }
}

TEST_CASE("VuTokenResourceAccess: broadcast VF reads use the broadcast component")
{
    vcl::VuTokenResourceAccess access;
    REQUIRE(accessFor("mulw.xyz vf01, vf02, vf03", access));
    CHECK(hasKey(access.registerWrites, "VF01.x"));
    CHECK(hasKey(access.registerWrites, "VF01.y"));
    CHECK(hasKey(access.registerWrites, "VF01.z"));
    CHECK(!hasKey(access.registerWrites, "VF01.w"));
    CHECK(hasKey(access.registerReads, "VF03.w"));
    CHECK(!hasKey(access.registerReads, "VF03.x"));
}

TEST_CASE("VuTokenResourceAccess: unresolved aliases keep symbolic resource keys")
{
    vcl::VuTokenResourceAccess access;
    REQUIRE(accessFor("mul.xyz dst, src, vf02", access));
    CHECK(hasKey(access.registerWrites, "dst.x"));
    CHECK(hasKey(access.registerWrites, "dst.y"));
    CHECK(hasKey(access.registerWrites, "dst.z"));
    CHECK(hasKey(access.registerReads, "src.x"));
    CHECK(hasKey(access.registerReads, "src.y"));
    CHECK(hasKey(access.registerReads, "src.z"));
    CHECK(hasKey(access.registerReads, "VF02.x"));
}

TEST_CASE("VuTokenResourceAccess: special registers and waits come from metadata and operands")
{
    vcl::VuTokenResourceAccess loiAccess;
    REQUIRE(accessFor("loi 1.0", loiAccess));
    CHECK((loiAccess.implicitWrites & vcl::VU_RESOURCE_I) != 0);

    vcl::VuTokenResourceAccess addiAccess;
    REQUIRE(accessFor("addi.xy vf01, vf02, i", addiAccess));
    CHECK((addiAccess.implicitReads & vcl::VU_RESOURCE_I) != 0);

    vcl::VuTokenResourceAccess divAccess;
    REQUIRE(accessFor("div q, vf01[x], vf02[x]", divAccess));
    CHECK((divAccess.implicitWrites & vcl::VU_RESOURCE_Q) != 0);

    vcl::VuTokenResourceAccess addqAccess;
    REQUIRE(accessFor("addq vf01, vf02, q", addqAccess));
    CHECK((addqAccess.implicitReads & vcl::VU_RESOURCE_Q) != 0);

    vcl::VuTokenResourceAccess maddAccess;
    REQUIRE(accessFor("maddw.xyz vf01, vf02, vf03", maddAccess));
    CHECK((maddAccess.implicitReads & vcl::VU_RESOURCE_ACC) != 0);

    vcl::VuTokenResourceAccess maddaAccess;
    REQUIRE(accessFor("madday acc, vf02, vf03", maddaAccess));
    CHECK((maddaAccess.implicitReads & vcl::VU_RESOURCE_ACC) != 0);
    CHECK((maddaAccess.implicitWrites & vcl::VU_RESOURCE_ACC) != 0);
}

TEST_CASE("VuTokenResourceAccess: MAC/CLIP, memory, branch, and bypass metadata are visible")
{
    vcl::VuTokenResourceAccess clipAccess;
    REQUIRE(accessFor("clipw.xyz vf01, vf02[w]", clipAccess));
    CHECK((clipAccess.implicitWrites & vcl::VU_RESOURCE_MAC) != 0);
    CHECK((clipAccess.implicitWrites & vcl::VU_RESOURCE_CLIP) != 0);

    vcl::VuTokenResourceAccess fcandAccess;
    REQUIRE(accessFor("fcand vi01, 0x3ffff", fcandAccess));
    CHECK((fcandAccess.implicitReads & vcl::VU_RESOURCE_CLIP) != 0);

    vcl::VuTokenResourceAccess lqiAccess;
    REQUIRE(accessFor("lqi.xyzw vf04, (vi01++)", lqiAccess));
    CHECK(lqiAccess.memoryKind == vcl::VU_MEMORY_LOAD);
    CHECK((lqiAccess.memoryFlags & vcl::VU_MEMORY_FLAG_POSTINC) != 0);
    CHECK(lqiAccess.hasMemoryBase == true);
    CHECK(lqiAccess.memoryBaseRegister == "VI01");

    vcl::VuTokenResourceAccess lqAccess;
    REQUIRE(accessFor("lq.xyzw vf04, 4(vi02)", lqAccess));
    CHECK(lqAccess.memoryKind == vcl::VU_MEMORY_LOAD);
    CHECK((lqAccess.bypassFlags & vcl::VU_BYPASS_LOAD_TO_FTOI) != 0);
    CHECK((lqAccess.bypassFlags & vcl::VU_BYPASS_LOAD_TO_MINII) != 0);
    CHECK(lqAccess.hasMemoryBase == true);
    CHECK(lqAccess.memoryBaseRegister == "VI02");
    CHECK(lqAccess.hasMemoryOffset == true);
    CHECK(lqAccess.memoryOffset == 4);

    vcl::VuTokenResourceAccess sqAccess;
    REQUIRE(accessFor("sq.xy vf06, 12(vi03)", sqAccess));
    CHECK(sqAccess.memoryKind == vcl::VU_MEMORY_STORE);
    CHECK(sqAccess.hasMemoryBase == true);
    CHECK(sqAccess.memoryBaseRegister == "VI03");
    CHECK(sqAccess.hasMemoryOffset == true);
    CHECK(sqAccess.memoryOffset == 12);

    vcl::VuTokenResourceAccess branchAccess;
    REQUIRE(accessFor("ibne vi01, vi02, target", branchAccess));
    CHECK((branchAccess.instructionFlags & vcl::VU_INSTR_BRANCH) != 0);
    CHECK(branchAccess.branchDelaySlots == 1);

    vcl::VuTokenResourceAccess bAccess;
    REQUIRE(accessFor("b target", bAccess));
    CHECK((bAccess.instructionFlags & vcl::VU_INSTR_UNCONDITIONAL_BRANCH) != 0);
    CHECK((bAccess.instructionFlags & vcl::VU_INSTR_LINK_BRANCH) == 0);

    vcl::VuTokenResourceAccess jalrAccess;
    REQUIRE(accessFor("jalr vi15, vi01", jalrAccess));
    CHECK((jalrAccess.instructionFlags & vcl::VU_INSTR_LINK_BRANCH) != 0);
    CHECK((jalrAccess.instructionFlags & vcl::VU_INSTR_REGISTER_BRANCH) != 0);

    vcl::VuTokenResourceAccess ftoiAccess;
    REQUIRE(accessFor("ftoi4 vf01, vf02", ftoiAccess));
    CHECK((ftoiAccess.bypassFlags & vcl::VU_BYPASS_FTOI_TO_MTIR) != 0);
}
