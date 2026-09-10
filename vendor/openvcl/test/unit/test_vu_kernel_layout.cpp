// Tests for VuKernelLayout: structured handoff between modulo placer and
// a future stage-aware emitter (Track 9.G step 5a).

#include "test_harness.h"

#include "../../src/VuKernelLayout.h"

#include <algorithm>
#include <cstdio>
#include <set>

using namespace vcl;

TEST_CASE("VuKernelLayout: default-constructed state is empty")
{
    VuKernelLayout layout;
    CHECK(layout.II == 0u);
    CHECK(layout.miiStart == 0u);
    CHECK(layout.bumps == 0u);
    CHECK(layout.stageCount == 0u);
    CHECK(layout.feasible == false);
    CHECK(layout.entries.empty());
}

TEST_CASE("VuKernelLayoutEntry: defaults match a pipe=none placeholder")
{
    VuKernelLayoutEntry ent;
    CHECK(ent.nodeIndex == 0u);
    CHECK(ent.tokenIndex == 0u);
    CHECK(ent.slot == 0u);
    CHECK(ent.stage == 0u);
    CHECK(ent.modSlot == 0u);
    CHECK(ent.pipe == 0);
    CHECK(ent.duration == 1u);
}

TEST_CASE("VuKernelLayout: comparator sorts by (stage, modSlot, nodeIndex)")
{
    VuKernelLayoutEntry a;
    a.stage = 1; a.modSlot = 5; a.nodeIndex = 7;
    VuKernelLayoutEntry b;
    b.stage = 0; b.modSlot = 9; b.nodeIndex = 0;
    VuKernelLayoutEntry c;
    c.stage = 1; c.modSlot = 2; c.nodeIndex = 4;
    VuKernelLayoutEntry d;
    d.stage = 1; d.modSlot = 5; d.nodeIndex = 1;

    std::vector<VuKernelLayoutEntry> v;
    v.push_back(a);
    v.push_back(b);
    v.push_back(c);
    v.push_back(d);
    std::sort(v.begin(), v.end(), vuKernelLayoutEntryLess);

    // b (stage 0) first.
    CHECK(v[0].stage == 0u);
    CHECK(v[0].modSlot == 9u);
    // c (stage 1, modSlot 2) next.
    CHECK(v[1].stage == 1u);
    CHECK(v[1].modSlot == 2u);
    // d (stage 1, modSlot 5, nodeIndex 1) before a (nodeIndex 7).
    CHECK(v[2].stage == 1u);
    CHECK(v[2].modSlot == 5u);
    CHECK(v[2].nodeIndex == 1u);
    CHECK(v[3].nodeIndex == 7u);
}

TEST_CASE("VuKernelLayout: II/miiStart/bumps consistent with feasible round")
{
    VuKernelLayout layout;
    layout.II         = 36;
    layout.miiStart   = 36;
    layout.bumps      = 0;
    layout.stageCount = 2;
    layout.feasible   = true;
    CHECK(layout.II == layout.miiStart + layout.bumps);
    CHECK(layout.stageCount >= 1u);
    CHECK(layout.feasible);
}

TEST_CASE("VuKernelTemplate: empty layout produces empty template")
{
    VuKernelLayout layout;
    VuKernelTemplate t;
    buildVuKernelTemplate(layout, t);
    CHECK(t.II == 0u);
    CHECK(t.slots.empty());
    CHECK(t.conflicts == 0u);
}

TEST_CASE("VuKernelTemplate: conflict-free pairing of upper and lower")
{
    VuKernelLayout layout;
    layout.II = 4;
    VuKernelLayoutEntry u0; u0.nodeIndex = 0; u0.modSlot = 0; u0.pipe = 1;
    VuKernelLayoutEntry l0; l0.nodeIndex = 1; l0.modSlot = 0; l0.pipe = 2;
    VuKernelLayoutEntry u2; u2.nodeIndex = 2; u2.modSlot = 2; u2.pipe = 1;
    VuKernelLayoutEntry f3; f3.nodeIndex = 3; f3.modSlot = 3; f3.pipe = 3;
    layout.entries.push_back(u0);
    layout.entries.push_back(l0);
    layout.entries.push_back(u2);
    layout.entries.push_back(f3);

    VuKernelTemplate t;
    buildVuKernelTemplate(layout, t);
    CHECK(t.II == 4u);
    CHECK(t.slots.size() == 4u);
    CHECK(t.conflicts == 0u);
    CHECK(t.slots[0].upper == 0);
    CHECK(t.slots[0].lower == 1);
    CHECK(t.slots[2].upper == 2);
    CHECK(t.slots[2].lower == VuKernelTemplateSlot::NO_ENTRY);
    CHECK(t.slots[3].fdiv  == 3);
    CHECK(t.slots[1].upper == VuKernelTemplateSlot::NO_ENTRY);
    CHECK(t.slots[1].lower == VuKernelTemplateSlot::NO_ENTRY);
}

TEST_CASE("VuKernelTemplate: same-lane collision at modSlot is counted")
{
    VuKernelLayout layout;
    layout.II = 2;
    VuKernelLayoutEntry a; a.nodeIndex = 0; a.modSlot = 1; a.pipe = 1;
    VuKernelLayoutEntry b; b.nodeIndex = 1; b.modSlot = 1; b.pipe = 1;
    layout.entries.push_back(a);
    layout.entries.push_back(b);

    VuKernelTemplate t;
    buildVuKernelTemplate(layout, t);
    CHECK(t.II == 2u);
    CHECK(t.conflicts == 1u);
    // Later entry overwrites earlier (diagnostic behavior).
    CHECK(t.slots[1].upper == 1);
}

TEST_CASE("VuKernelTemplate: pipe=0 (none) entries are ignored")
{
    VuKernelLayout layout;
    layout.II = 2;
    VuKernelLayoutEntry n; n.nodeIndex = 0; n.modSlot = 0; n.pipe = 0;
    layout.entries.push_back(n);
    VuKernelTemplate t;
    buildVuKernelTemplate(layout, t);
    CHECK(t.conflicts == 0u);
    CHECK(t.slots[0].upper == VuKernelTemplateSlot::NO_ENTRY);
    CHECK(t.slots[0].lower == VuKernelTemplateSlot::NO_ENTRY);
}

TEST_CASE("VuKernelEnvelope: single-stage layout has no prologue/epilogue")
{
    VuKernelLayout layout;
    layout.II = 4;
    layout.stageCount = 1;
    VuKernelLayoutEntry e; e.modSlot = 0; e.stage = 0; e.pipe = 1;
    layout.entries.push_back(e);
    VuKernelEnvelope env;
    buildVuKernelEnvelope(layout, env);
    CHECK(env.stageCount == 1u);
    CHECK(env.kernelTokens == 1u);
    CHECK(env.prologueCycles == 0u);
    CHECK(env.epilogueCycles == 0u);
    CHECK(env.prologueTokenCounts.empty());
    CHECK(env.epilogueTokenCounts.empty());
}

TEST_CASE("VuKernelEnvelope: 2-stage layout has 1 prologue + 1 epilogue copy")
{
    // 3 entries at stage 0, 2 entries at stage 1.
    VuKernelLayout layout;
    layout.II = 4;
    layout.stageCount = 2;
    for (unsigned int i = 0; i < 3; ++i) {
        VuKernelLayoutEntry e; e.stage = 0; e.modSlot = i; e.pipe = 1;
        layout.entries.push_back(e);
    }
    for (unsigned int i = 0; i < 2; ++i) {
        VuKernelLayoutEntry e; e.stage = 1; e.modSlot = i; e.pipe = 2;
        layout.entries.push_back(e);
    }
    VuKernelEnvelope env;
    buildVuKernelEnvelope(layout, env);
    CHECK(env.II == 4u);
    CHECK(env.stageCount == 2u);
    CHECK(env.kernelTokens == 5u);
    CHECK(env.prologueCycles == 4u);
    CHECK(env.epilogueCycles == 4u);
    CHECK(env.prologueTokenCounts.size() == 1u);
    CHECK(env.epilogueTokenCounts.size() == 1u);
    // Prologue copy 0 (only) issues stage-0 entries: 3.
    CHECK(env.prologueTokenCounts[0] == 3u);
    // Epilogue copy 1 (only) issues stage-1 entries: 2.
    CHECK(env.epilogueTokenCounts[0] == 2u);
}

TEST_CASE("VuKernelEnvelope: 3-stage layout has 2 prologue + 2 epilogue copies")
{
    VuKernelLayout layout;
    layout.II = 2;
    layout.stageCount = 3;
    // 1 entry per stage.
    for (unsigned int s = 0; s < 3; ++s) {
        VuKernelLayoutEntry e; e.stage = s; e.modSlot = 0; e.pipe = 1;
        layout.entries.push_back(e);
    }
    VuKernelEnvelope env;
    buildVuKernelEnvelope(layout, env);
    CHECK(env.prologueTokenCounts.size() == 2u);
    CHECK(env.epilogueTokenCounts.size() == 2u);
    // Prologue copy 0 issues stages [0,0]: 1 token.
    CHECK(env.prologueTokenCounts[0] == 1u);
    // Prologue copy 1 issues stages [0,1]: 2 tokens.
    CHECK(env.prologueTokenCounts[1] == 2u);
    // Epilogue copy 1 issues stages [1,2]: 2 tokens.
    CHECK(env.epilogueTokenCounts[0] == 2u);
    // Epilogue copy 2 issues stages [2,2]: 1 token.
    CHECK(env.epilogueTokenCounts[1] == 1u);
}

TEST_CASE("VuKernelEnvelope: per-row slot tables populated for 2-stage layout")
{
    VuKernelLayout layout;
    layout.II = 2;
    layout.stageCount = 2;
    // stage 0: upper@modSlot 0, lower@modSlot 1
    { VuKernelLayoutEntry e; e.stage = 0; e.modSlot = 0; e.pipe = 1; layout.entries.push_back(e); }
    { VuKernelLayoutEntry e; e.stage = 0; e.modSlot = 1; e.pipe = 2; layout.entries.push_back(e); }
    // stage 1: upper@modSlot 1
    { VuKernelLayoutEntry e; e.stage = 1; e.modSlot = 1; e.pipe = 1; layout.entries.push_back(e); }

    VuKernelEnvelope env;
    buildVuKernelEnvelope(layout, env);
    CHECK(env.conflicts == 0u);
    CHECK(env.prologueRows.size() == 2u);
    CHECK(env.epilogueRows.size() == 2u);
    // Prologue copy 0 = stage 0 only.
    CHECK(env.prologueRows[0].upper == 0);                              // entry 0
    CHECK(env.prologueRows[0].lower == VuKernelTemplateSlot::NO_ENTRY);
    CHECK(env.prologueRows[1].upper == VuKernelTemplateSlot::NO_ENTRY); // entry 2 is stage 1, not active in prologue copy 0
    CHECK(env.prologueRows[1].lower == 1);                              // entry 1
    // Epilogue copy 1 = stage 1 only.
    CHECK(env.epilogueRows[0].upper == VuKernelTemplateSlot::NO_ENTRY);
    CHECK(env.epilogueRows[1].upper == 2);                              // entry 2
    CHECK(env.epilogueRows[1].lower == VuKernelTemplateSlot::NO_ENTRY); // entry 1 is stage 0, drained already
}

TEST_CASE("VuKernelEnvelope: per-row tables grow with stageCount")
{
    VuKernelLayout layout;
    layout.II = 1;
    layout.stageCount = 3;
    // Same modSlot/pipe across all stages is a degenerate case used here
    // purely to exercise sizing and conflict detection.
    for (unsigned int s = 0; s < 3; ++s) {
        VuKernelLayoutEntry e; e.stage = s; e.modSlot = 0; e.pipe = 1;
        layout.entries.push_back(e);
    }
    VuKernelEnvelope env;
    buildVuKernelEnvelope(layout, env);
    CHECK(env.prologueRows.size() == 2u); // (stageCount-1) * II = 2*1
    CHECK(env.epilogueRows.size() == 2u);
    // Stage 0 and stage 1 both fight for upper@modSlot 0 in prologue copy 1.
    // Stage 1 and stage 2 both fight for upper@modSlot 0 in epilogue copy 1.
    CHECK(env.conflicts >= 1u);
}

TEST_CASE("VuKernelRegisterPlan: same-stage shared register is not a hazard")
{
    VuKernelLayout layout;
    layout.II = 2; layout.stageCount = 1;
    { VuKernelLayoutEntry e; e.stage = 0; layout.entries.push_back(e); }
    { VuKernelLayoutEntry e; e.stage = 0; layout.entries.push_back(e); }
    std::vector<VuKernelEntryRegisters> regs(2);
    regs[0].writes.push_back("VF10");
    regs[1].reads.push_back("VF10");
    VuKernelRegisterPlan plan;
    buildVuKernelRegisterPlan(layout, regs, plan);
    CHECK(plan.regCount == 1u);
    CHECK(plan.hazards.empty());
    CHECK(plan.wawCount == 0u);
    CHECK(plan.rawCount == 0u);
    CHECK(plan.warCount == 0u);
}

TEST_CASE("VuKernelRegisterPlan: cross-stage WAW and RAW counted")
{
    VuKernelLayout layout;
    layout.II = 2; layout.stageCount = 2;
    { VuKernelLayoutEntry e; e.stage = 0; layout.entries.push_back(e); } // 0: writes VF20
    { VuKernelLayoutEntry e; e.stage = 1; layout.entries.push_back(e); } // 1: writes+reads VF20
    std::vector<VuKernelEntryRegisters> regs(2);
    regs[0].writes.push_back("VF20");
    regs[1].writes.push_back("VF20");
    regs[1].reads.push_back("VF20");
    VuKernelRegisterPlan plan;
    buildVuKernelRegisterPlan(layout, regs, plan);
    // Pairs on VF20: (0W,1W) WAW + (0W,1R) RAW = 2 hazards.
    CHECK(plan.regCount == 1u);
    CHECK(plan.hazards.size() == 2u);
    CHECK(plan.wawCount == 1u);
    CHECK(plan.rawCount == 1u);
    CHECK(plan.warCount == 0u);
}

TEST_CASE("VuKernelRegisterPlan: read-only across stages is not a hazard")
{
    VuKernelLayout layout;
    layout.II = 2; layout.stageCount = 2;
    { VuKernelLayoutEntry e; e.stage = 0; layout.entries.push_back(e); }
    { VuKernelLayoutEntry e; e.stage = 1; layout.entries.push_back(e); }
    std::vector<VuKernelEntryRegisters> regs(2);
    regs[0].reads.push_back("VF05");
    regs[1].reads.push_back("VF05");
    VuKernelRegisterPlan plan;
    buildVuKernelRegisterPlan(layout, regs, plan);
    CHECK(plan.regCount == 1u);
    CHECK(plan.hazards.empty());
}

TEST_CASE("VuKernelRegisterPlan: WAR counted when reader is in earlier stage")
{
    VuKernelLayout layout;
    layout.II = 2; layout.stageCount = 2;
    { VuKernelLayoutEntry e; e.stage = 0; layout.entries.push_back(e); }
    { VuKernelLayoutEntry e; e.stage = 1; layout.entries.push_back(e); }
    std::vector<VuKernelEntryRegisters> regs(2);
    regs[0].reads.push_back("VF07");
    regs[1].writes.push_back("VF07");
    VuKernelRegisterPlan plan;
    buildVuKernelRegisterPlan(layout, regs, plan);
    CHECK(plan.regCount == 1u);
    CHECK(plan.hazards.size() == 1u);
    CHECK(plan.warCount == 1u);
    CHECK(plan.wawCount == 0u);
    CHECK(plan.rawCount == 0u);
}

// ---------------------------------------------------------------------------
// Track 9.G step 6a — rewrite-plan synthesis tests.
// ---------------------------------------------------------------------------

namespace
{
    VuKernelLayoutEntry makeRewriteEntry(unsigned tokenIdx, unsigned stage,
                                          unsigned modSlot, int pipe)
    {
        VuKernelLayoutEntry e;
        e.tokenIndex = tokenIdx;
        e.stage      = stage;
        e.modSlot    = modSlot;
        e.slot       = stage * 2u + modSlot;
        e.pipe       = pipe;
        e.duration   = 1;
        return e;
    }
}

TEST_CASE("VuKernelRewritePlan: empty layout yields empty plan")
{
    VuKernelLayout layout;
    VuKernelEnvelope env;
    buildVuKernelEnvelope(layout, env);
    VuKernelRewritePlan plan;
    buildVuKernelRewritePlan(layout, env, plan);
    CHECK(plan.II == 0u);
    CHECK(plan.stageCount == 0u);
    CHECK(plan.prologTokens.empty());
    CHECK(plan.mainTokens.empty());
    CHECK(plan.drainTokens.empty());
}

TEST_CASE("VuKernelRewritePlan: single-stage layout produces main only")
{
    // II=2, stageCount=1. Two cycles, one upper + one lower per cycle.
    VuKernelLayout layout;
    layout.II = 2; layout.stageCount = 1; layout.feasible = true;
    layout.entries.push_back(makeRewriteEntry(100, 0, 0, 1)); // U
    layout.entries.push_back(makeRewriteEntry(101, 0, 0, 2)); // L
    layout.entries.push_back(makeRewriteEntry(102, 0, 1, 1)); // U
    layout.entries.push_back(makeRewriteEntry(103, 0, 1, 2)); // L
    VuKernelEnvelope env;
    buildVuKernelEnvelope(layout, env);
    VuKernelRewritePlan plan;
    buildVuKernelRewritePlan(layout, env, plan);
    CHECK(plan.II == 2u);
    CHECK(plan.stageCount == 1u);
    CHECK(plan.prologTokens.empty());
    CHECK(plan.drainTokens.empty());
    REQUIRE(plan.mainTokens.size() == 8u); // II * 4
    // cycle 0: upper=100, lower=101, fdiv=-, efu=-
    CHECK(plan.mainTokens[0] == 100u);
    CHECK(plan.mainTokens[1] == 101u);
    CHECK(plan.mainTokens[2] == VuKernelRewritePlan::NO_TOKEN);
    CHECK(plan.mainTokens[3] == VuKernelRewritePlan::NO_TOKEN);
    // cycle 1
    CHECK(plan.mainTokens[4] == 102u);
    CHECK(plan.mainTokens[5] == 103u);
}

TEST_CASE("VuKernelRewritePlan: two-stage layout produces prolog, main, drain")
{
    // II=2, stageCount=2.
    // Stage 0: token 10 @modSlot 0 (upper), token 11 @modSlot 1 (upper)
    // Stage 1: token 20 @modSlot 0 (lower), token 21 @modSlot 1 (lower)
    VuKernelLayout layout;
    layout.II = 2; layout.stageCount = 2; layout.feasible = true;
    layout.entries.push_back(makeRewriteEntry(10, 0, 0, 1));
    layout.entries.push_back(makeRewriteEntry(11, 0, 1, 1));
    layout.entries.push_back(makeRewriteEntry(20, 1, 0, 2));
    layout.entries.push_back(makeRewriteEntry(21, 1, 1, 2));
    VuKernelEnvelope env;
    buildVuKernelEnvelope(layout, env);
    VuKernelRewritePlan plan;
    buildVuKernelRewritePlan(layout, env, plan);
    CHECK(plan.II == 2u);
    CHECK(plan.stageCount == 2u);
    // Each section: (stageCount-1)=1 copy * II=2 cycles * 4 lanes = 8 for prolog/drain; 8 for main.
    REQUIRE(plan.prologTokens.size() == 8u);
    REQUIRE(plan.mainTokens.size() == 8u);
    REQUIRE(plan.drainTokens.size() == 8u);
    // Prolog copy 0: only stage 0 entries are live.
    CHECK(plan.prologTokens[0] == 10u);                                  // cycle 0 upper
    CHECK(plan.prologTokens[1] == VuKernelRewritePlan::NO_TOKEN);        // cycle 0 lower
    CHECK(plan.prologTokens[4] == 11u);                                  // cycle 1 upper
    CHECK(plan.prologTokens[5] == VuKernelRewritePlan::NO_TOKEN);        // cycle 1 lower
    // Main: both stages overlapped — upper has stage-0 token, lower has stage-1 token.
    CHECK(plan.mainTokens[0] == 10u);
    CHECK(plan.mainTokens[1] == 20u);
    CHECK(plan.mainTokens[4] == 11u);
    CHECK(plan.mainTokens[5] == 21u);
    // Drain copy 1: only stage 1 entries are live.
    CHECK(plan.drainTokens[0] == VuKernelRewritePlan::NO_TOKEN);
    CHECK(plan.drainTokens[1] == 20u);
    CHECK(plan.drainTokens[4] == VuKernelRewritePlan::NO_TOKEN);
    CHECK(plan.drainTokens[5] == 21u);
}

TEST_CASE("VuKernelRewritePlan: two-stage layout exposes entryStages + stageCells")
{
    // Same setup as the two-stage prolog/main/drain test above.
    VuKernelLayout layout;
    layout.II = 2; layout.stageCount = 2; layout.feasible = true;
    layout.entries.push_back(makeRewriteEntry(10, 0, 0, 1));
    layout.entries.push_back(makeRewriteEntry(11, 0, 1, 1));
    layout.entries.push_back(makeRewriteEntry(20, 1, 0, 2));
    layout.entries.push_back(makeRewriteEntry(21, 1, 1, 2));
    VuKernelEnvelope env;
    buildVuKernelEnvelope(layout, env);
    VuKernelRewritePlan plan;
    buildVuKernelRewritePlan(layout, env, plan);
    REQUIRE(plan.entryStages.size() == 4u);
    CHECK(plan.entryStages[0] == 0u);
    CHECK(plan.entryStages[1] == 0u);
    CHECK(plan.entryStages[2] == 1u);
    CHECK(plan.entryStages[3] == 1u);
    REQUIRE(plan.stageCells.size() == 4u); // stageCount(2) * II(2)
    // (stage 0, modSlot 0): upper = entry 0
    CHECK(plan.stageCells[0].upper == 0);
    CHECK(plan.stageCells[0].lower == VuKernelTemplateSlot::NO_ENTRY);
    // (stage 0, modSlot 1): upper = entry 1
    CHECK(plan.stageCells[1].upper == 1);
    // (stage 1, modSlot 0): lower = entry 2
    CHECK(plan.stageCells[2].lower == 2);
    CHECK(plan.stageCells[2].upper == VuKernelTemplateSlot::NO_ENTRY);
    // (stage 1, modSlot 1): lower = entry 3
    CHECK(plan.stageCells[3].lower == 3);
    CHECK(plan.conflicts == 0u);
}

TEST_CASE("VuKernelRenameHints: each hazard yields two hints, deduplicated")
{
    // Set up a regplan with two hazards on VF12:
    //   - (entry 0 W, entry 1 R) — RAW
    //   - (entry 0 W, entry 2 R) — RAW
    // entry 0 contributes a W-hint that must dedup across the two hazards.
    VuKernelRegisterPlan plan;
    plan.regCount = 1;
    plan.rawCount = 2;
    {
        VuKernelRegisterHazard h;
        h.reg = "VF12";
        h.entryA = 0; h.stageA = 0; h.kindA = 1;
        h.entryB = 1; h.stageB = 1; h.kindB = 0;
        plan.hazards.push_back(h);
    }
    {
        VuKernelRegisterHazard h;
        h.reg = "VF12";
        h.entryA = 0; h.stageA = 0; h.kindA = 1;
        h.entryB = 2; h.stageB = 1; h.kindB = 0;
        plan.hazards.push_back(h);
    }
    std::vector<VuKernelRenameHint> hints;
    buildVuKernelRenameHints(plan, hints);
    // 3 unique: (entry 0, W), (entry 1, R), (entry 2, R).
    REQUIRE(hints.size() == 3u);
    unsigned writeCount = 0, readCount = 0;
    for (unsigned i = 0; i < hints.size(); ++i)
    {
        CHECK(hints[i].reg == "VF12");
        if (hints[i].kind == 1) ++writeCount;
        else ++readCount;
    }
    CHECK(writeCount == 1u);
    CHECK(readCount == 2u);
}

// Track 9.G step 8b-1: rename-decision allocator.
TEST_CASE("VuKernelRenameDecisions: one decision per unique hint reg")
{
    std::vector<VuKernelRenameHint> hints;
    {
        VuKernelRenameHint h; h.reg = "VF12"; h.entry = 0; h.stage = 0; h.kind = 1;
        hints.push_back(h);
    }
    {
        VuKernelRenameHint h; h.reg = "VF12"; h.entry = 1; h.stage = 1; h.kind = 0;
        hints.push_back(h);
    }
    {
        VuKernelRenameHint h; h.reg = "VF20"; h.entry = 2; h.stage = 0; h.kind = 1;
        hints.push_back(h);
    }
    std::vector<std::string> usedBases;
    usedBases.push_back("VF12");
    usedBases.push_back("VF20");
    std::vector<VuKernelRenameDecision> decisions;
    buildVuKernelRenameDecisions(hints, usedBases, decisions);
    REQUIRE(decisions.size() == 2u);
    CHECK(decisions[0].reg == "VF12");
    CHECK(decisions[0].assigned);
    CHECK(decisions[1].reg == "VF20");
    CHECK(decisions[1].assigned);
    CHECK(decisions[0].scratch != decisions[1].scratch);
}

TEST_CASE("VuKernelRenameDecisions: scratches avoid used bases and each other")
{
    std::vector<VuKernelRenameHint> hints;
    {
        VuKernelRenameHint h; h.reg = "VF02"; h.entry = 0; h.stage = 0; h.kind = 1;
        hints.push_back(h);
    }
    {
        VuKernelRenameHint h; h.reg = "VF03"; h.entry = 1; h.stage = 0; h.kind = 1;
        hints.push_back(h);
    }
    // Reserve top of VF range (31, 30) and the renamed bases.
    std::vector<std::string> usedBases;
    usedBases.push_back("VF02");
    usedBases.push_back("VF03");
    usedBases.push_back("VF31");
    usedBases.push_back("VF30");
    usedBases.push_back("VF29.xyz");  // field-suffixed key must still reserve VF29
    std::vector<VuKernelRenameDecision> decisions;
    buildVuKernelRenameDecisions(hints, usedBases, decisions);
    REQUIRE(decisions.size() == 2u);
    CHECK(decisions[0].assigned);
    CHECK(decisions[1].assigned);
    // Both scratches must not collide with any reserved base.
    const char* reserved[] = { "VF02", "VF03", "VF29", "VF30", "VF31" };
    for (unsigned r = 0; r < sizeof(reserved)/sizeof(reserved[0]); ++r)
    {
        CHECK(decisions[0].scratch != reserved[r]);
        CHECK(decisions[1].scratch != reserved[r]);
    }
    CHECK(decisions[0].scratch != decisions[1].scratch);
}

TEST_CASE("VuKernelRenameDecisions: budget exhaustion marks tail decisions unassigned")
{
    // Build hints for 31 unique regs (VF02..VF32-equivalent) so the allocator
    // runs out of VF31..VF02 free slots. We use synthetic reg names that are
    // not in usedBases so collision only comes from the scratch pool itself.
    std::vector<VuKernelRenameHint> hints;
    for (unsigned int i = 0; i < 31u; ++i)
    {
        VuKernelRenameHint h;
        char buf[16];
        // Synthetic non-VF names so they cannot collide with the scratch scan.
        std::sprintf(buf, "X%02u", i);
        h.reg = buf; h.entry = i; h.stage = 0; h.kind = 1;
        hints.push_back(h);
    }
    std::vector<std::string> usedBases;  // empty
    std::vector<VuKernelRenameDecision> decisions;
    buildVuKernelRenameDecisions(hints, usedBases, decisions);
    REQUIRE(decisions.size() == 31u);
    // VF02..VF31 = 30 candidates; first 30 assigned, 31st must be unassigned.
    unsigned assignedCount = 0;
    for (unsigned d = 0; d < decisions.size(); ++d)
        if (decisions[d].assigned) ++assignedCount;
    CHECK(assignedCount == 30u);
    CHECK(!decisions.back().assigned);
    CHECK(decisions.back().scratch == "");
}

// ---- Track 9.G step 8b-2a: buildVuKernelRenameMoveSlots ----

namespace {
VuKernelEntryRegisters makeMoveEntryReads(const char* reg)
{
    VuKernelEntryRegisters er;
    er.reads.push_back(reg);
    return er;
}
VuKernelLayoutEntry makeMoveLayoutEntry(unsigned nodeIndex, unsigned modSlot)
{
    VuKernelLayoutEntry e;
    e.nodeIndex  = nodeIndex;
    e.tokenIndex = nodeIndex;
    e.slot       = modSlot;
    e.stage      = 0;
    e.modSlot    = modSlot;
    e.pipe       = 1;
    e.duration   = 1;
    return e;
}
} // namespace

TEST_CASE("buildVuKernelRenameMoveSlots: empty decisions yields no slots")
{
    std::vector<VuKernelRenameDecision> decisions;
    VuKernelLayout layout; layout.II = 4; layout.stageCount = 2;
    std::vector<VuKernelEntryRegisters> entryRegs;
    VuKernelTemplate tmpl; tmpl.II = 4; tmpl.slots.resize(4);
    std::vector<VuKernelRenameMoveSlot> slots;
    buildVuKernelRenameMoveSlots(decisions, layout, entryRegs, tmpl, slots);
    CHECK(slots.empty());
}

TEST_CASE("buildVuKernelRenameMoveSlots: prefers upper lane and avoids consumer modSlot")
{
    VuKernelRenameDecision d;
    d.reg = "VF20"; d.scratch = "VF31"; d.assigned = true;
    std::vector<VuKernelRenameDecision> decisions; decisions.push_back(d);

    VuKernelLayout layout;
    layout.II = 4; layout.stageCount = 1; layout.feasible = true;
    // One consumer entry reading VF20 at modSlot 2.
    layout.entries.push_back(makeMoveLayoutEntry(0, 2));
    std::vector<VuKernelEntryRegisters> entryRegs;
    entryRegs.push_back(makeMoveEntryReads("VF20.xyzw"));

    VuKernelTemplate tmpl; tmpl.II = 4; tmpl.slots.resize(4);
    // All slots empty; should pick slot 0, upper lane.
    std::vector<VuKernelRenameMoveSlot> slots;
    buildVuKernelRenameMoveSlots(decisions, layout, entryRegs, tmpl, slots);
    REQUIRE(slots.size() == 1u);
    CHECK(slots[0].assigned);
    CHECK(slots[0].lane == 1);
    CHECK(slots[0].modSlot != 2u);
}

TEST_CASE("buildVuKernelRenameMoveSlots: falls back to lower when upper is full")
{
    VuKernelRenameDecision d;
    d.reg = "VF20"; d.scratch = "VF31"; d.assigned = true;
    std::vector<VuKernelRenameDecision> decisions; decisions.push_back(d);

    VuKernelLayout layout;
    layout.II = 2; layout.stageCount = 1; layout.feasible = true;
    std::vector<VuKernelEntryRegisters> entryRegs;

    VuKernelTemplate tmpl; tmpl.II = 2; tmpl.slots.resize(2);
    // Both upper lanes occupied; both lower lanes free.
    tmpl.slots[0].upper = 100;
    tmpl.slots[1].upper = 101;
    std::vector<VuKernelRenameMoveSlot> slots;
    buildVuKernelRenameMoveSlots(decisions, layout, entryRegs, tmpl, slots);
    REQUIRE(slots.size() == 1u);
    CHECK(slots[0].assigned);
    CHECK(slots[0].lane == 2);
}

TEST_CASE("buildVuKernelRenameMoveSlots: unassigned when all slots forbidden or occupied")
{
    VuKernelRenameDecision d;
    d.reg = "VF20"; d.scratch = "VF31"; d.assigned = true;
    std::vector<VuKernelRenameDecision> decisions; decisions.push_back(d);

    VuKernelLayout layout;
    layout.II = 2; layout.stageCount = 1; layout.feasible = true;
    // Consumer reads VF20 at both modSlots → both forbidden.
    layout.entries.push_back(makeMoveLayoutEntry(0, 0));
    layout.entries.push_back(makeMoveLayoutEntry(1, 1));
    std::vector<VuKernelEntryRegisters> entryRegs;
    entryRegs.push_back(makeMoveEntryReads("VF20.xyzw"));
    entryRegs.push_back(makeMoveEntryReads("VF20.xyzw"));

    VuKernelTemplate tmpl; tmpl.II = 2; tmpl.slots.resize(2);
    std::vector<VuKernelRenameMoveSlot> slots;
    buildVuKernelRenameMoveSlots(decisions, layout, entryRegs, tmpl, slots);
    REQUIRE(slots.size() == 1u);
    CHECK(!slots[0].assigned);
    CHECK(slots[0].lane == -1);
}

TEST_CASE("buildVuKernelRenameMoveSlots: propagates !assigned decisions")
{
    VuKernelRenameDecision d; d.reg = "VF20"; d.assigned = false;
    std::vector<VuKernelRenameDecision> decisions; decisions.push_back(d);

    VuKernelLayout layout; layout.II = 4; layout.stageCount = 1;
    std::vector<VuKernelEntryRegisters> entryRegs;
    VuKernelTemplate tmpl; tmpl.II = 4; tmpl.slots.resize(4);
    std::vector<VuKernelRenameMoveSlot> slots;
    buildVuKernelRenameMoveSlots(decisions, layout, entryRegs, tmpl, slots);
    REQUIRE(slots.size() == 1u);
    CHECK(!slots[0].assigned);
}

TEST_CASE("buildVuKernelRenameMoveSlots: multiple decisions claim distinct cells")
{
    std::vector<VuKernelRenameDecision> decisions;
    for (unsigned i = 0; i < 3; ++i) {
        VuKernelRenameDecision d;
        char buf[8]; std::sprintf(buf, "VF%u", 20u + i);
        d.reg = buf; d.scratch = "VF31"; d.assigned = true;
        decisions.push_back(d);
    }
    VuKernelLayout layout; layout.II = 4; layout.stageCount = 1; layout.feasible = true;
    std::vector<VuKernelEntryRegisters> entryRegs;
    VuKernelTemplate tmpl; tmpl.II = 4; tmpl.slots.resize(4);
    // All cells free; expect three distinct (modSlot, lane=upper) cells.
    std::vector<VuKernelRenameMoveSlot> slots;
    buildVuKernelRenameMoveSlots(decisions, layout, entryRegs, tmpl, slots);
    REQUIRE(slots.size() == 3u);
    CHECK(slots[0].assigned); CHECK(slots[1].assigned); CHECK(slots[2].assigned);
    CHECK(slots[0].lane == 1); CHECK(slots[1].lane == 1); CHECK(slots[2].lane == 1);
    CHECK(slots[0].modSlot != slots[1].modSlot);
    CHECK(slots[0].modSlot != slots[2].modSlot);
    CHECK(slots[1].modSlot != slots[2].modSlot);
}

// Track 9.G step 6d: layout-coverage self-validation.
// The synthesized MAIN section of VuKernelRewritePlan must reproduce the
// full multiset of layout token indices (no drops, no duplicates).
TEST_CASE("VuKernelRewritePlan: main section reproduces layout multiset (single stage)")
{
    VuKernelLayout layout;
    layout.II = 3; layout.stageCount = 1; layout.feasible = true;
    layout.entries.push_back(makeRewriteEntry(100, 0, 0, 1));
    layout.entries.push_back(makeRewriteEntry(101, 0, 1, 2));
    layout.entries.push_back(makeRewriteEntry(102, 0, 2, 3));
    layout.entries.push_back(makeRewriteEntry(103, 0, 2, 4));
    VuKernelEnvelope env;
    buildVuKernelEnvelope(layout, env);
    VuKernelRewritePlan plan;
    buildVuKernelRewritePlan(layout, env, plan);
    std::multiset<unsigned int> layoutTokens;
    for (unsigned i = 0; i < layout.entries.size(); ++i)
        layoutTokens.insert(layout.entries[i].tokenIndex);
    std::multiset<unsigned int> mainTokens;
    for (unsigned i = 0; i < plan.mainTokens.size(); ++i)
        if (plan.mainTokens[i] != VuKernelRewritePlan::NO_TOKEN)
            mainTokens.insert(plan.mainTokens[i]);
    CHECK(mainTokens == layoutTokens);
    CHECK(plan.prologTokens.empty());
    CHECK(plan.drainTokens.empty());
}

TEST_CASE("VuKernelRewritePlan: main section reproduces layout multiset (two stages)")
{
    VuKernelLayout layout;
    layout.II = 2; layout.stageCount = 2; layout.feasible = true;
    layout.entries.push_back(makeRewriteEntry(10, 0, 0, 1));
    layout.entries.push_back(makeRewriteEntry(11, 0, 1, 1));
    layout.entries.push_back(makeRewriteEntry(20, 1, 0, 2));
    layout.entries.push_back(makeRewriteEntry(21, 1, 1, 2));
    VuKernelEnvelope env;
    buildVuKernelEnvelope(layout, env);
    VuKernelRewritePlan plan;
    buildVuKernelRewritePlan(layout, env, plan);
    std::multiset<unsigned int> layoutTokens;
    for (unsigned i = 0; i < layout.entries.size(); ++i)
        layoutTokens.insert(layout.entries[i].tokenIndex);
    std::multiset<unsigned int> mainTokens;
    for (unsigned i = 0; i < plan.mainTokens.size(); ++i)
        if (plan.mainTokens[i] != VuKernelRewritePlan::NO_TOKEN)
            mainTokens.insert(plan.mainTokens[i]);
    CHECK(mainTokens == layoutTokens);
}
