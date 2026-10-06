/*	  Copyright (C) 2000,2001,2002  Sony Computer Entertainment America

       	  This file is subject to the terms and conditions of the GNU Lesser
	  General Public License Version 2.1. See the file "COPYING" in the
	  main directory of this archive for more details.                             */

#include "ps2s/perfmon.h"
#include "ps2s/core.h"
#include <stdio.h>

// private stuff for perf class, yeah yeah they're #defines..

#define PRF_D(_id, _val) \
    {                    \
        _id, _val        \
    }
#define MPRF_NOT_READY -1

#define PRFEVT_0_PROCESSOR_CYCLE 1
#define PRFEVT_0_SINGLE_INST_ISSUE 2
#define PRFEVT_0_BRANCH_ISSUED 3
#define PRFEVT_0_BTAC_MISS 4
#define PRFEVT_0_TLB_MISS 5
#define PRFEVT_0_ICACHE_MISS 6
#define PRFEVT_0_DTLB_ACCESSED 7
#define PRFEVT_0_NON_BLOCK_LOAD 8
#define PRFEVT_0_WBB_SINGLE_REQUEST 9
#define PRFEVT_0_WBB_BURST_REQUEST 10
#define PRFEVT_0_CPU_ADDRESS_BUS_BUSY 11
#define PRFEVT_0_INSTRUCTION_COMPLETED 12
#define PRFEVT_0_NON_BDS_INSTRUCTION_COMPLETED 13
#define PRFEVT_0_COP2_INSTRUCTION_COMPLETED 14
#define PRFEVT_0_LOAD_COMPLETED 15
#define PRFEVT_0_NO_EVENT 16 // hmm - whassis?

#define PRFEVT_1_LOW_ORDER_BRANCH_ISSUES 0
#define PRFEVT_1_PROCESSOR_CYCLE 1
#define PRFEVT_1_DUAL_INSTRUCTION_ISSUE 2
#define PRFEVT_1_BRANCH_MISPREDICTED 3
#define PRFEVT_1_TLB_MISS 4
#define PRFEVT_1_DTLB_MISS 5
#define PRFEVT_1_DCACHE_MISS 6
#define PRFEVT_1_WBB_SINGLE_REQUEST_UNAVAILABLE 7
#define PRFEVT_1_WBB_BURST_REQUEST_UNAVAILABLE 8
#define PRFEVT_1_WBB_BURST_REQUEST_ALMOST_FULL 9
#define PRFEVT_1_WBB_BURST_REQUEST_FULL 10
#define PRFEVT_1_CPU_DATA_BUS_BUSY 11
#define PRFEVT_1_INSTRUCTION_COMPLETED 12
#define PRFEVT_1_NON_BDS_INSTRUCTION_COMPLETED 13
#define PRFEVT_1_COP1_INSTRUCTION_COMPLETED 14
#define PRFEVT_1_STORE_COMPLETED 15
#define PRFEVT_1_NO_EVENT 16

PerfTest::prfDef PerfTest::prfTable[NUM_PRF_TESTS] = {
    PRF_D(0, PRFEVT_0_PROCESSOR_CYCLE),
    PRF_D(0, PRFEVT_0_INSTRUCTION_COMPLETED),
    PRF_D(0, PRFEVT_0_NON_BDS_INSTRUCTION_COMPLETED),
    PRF_D(1, PRFEVT_1_COP1_INSTRUCTION_COMPLETED),
    PRF_D(0, PRFEVT_0_COP2_INSTRUCTION_COMPLETED),

    PRF_D(1, PRFEVT_1_DUAL_INSTRUCTION_ISSUE),
    PRF_D(0, PRFEVT_0_SINGLE_INST_ISSUE),

    PRF_D(1, PRFEVT_1_LOW_ORDER_BRANCH_ISSUES),
    PRF_D(0, PRFEVT_0_BRANCH_ISSUED),

    PRF_D(1, PRFEVT_1_BRANCH_MISPREDICTED),
    PRF_D(0, PRFEVT_0_BTAC_MISS),

    PRF_D(0, PRFEVT_0_TLB_MISS),
    PRF_D(1, PRFEVT_1_DTLB_MISS),
    PRF_D(0, PRFEVT_0_DTLB_ACCESSED),
    PRF_D(1, PRFEVT_1_DCACHE_MISS),
    PRF_D(0, PRFEVT_0_ICACHE_MISS),

    PRF_D(0, PRFEVT_0_WBB_SINGLE_REQUEST),
    PRF_D(0, PRFEVT_0_WBB_BURST_REQUEST),

    PRF_D(1, PRFEVT_1_CPU_DATA_BUS_BUSY),
    PRF_D(0, PRFEVT_0_CPU_ADDRESS_BUS_BUSY),

    PRF_D(0, PRFEVT_0_NON_BLOCK_LOAD),
    PRF_D(0, PRFEVT_0_LOAD_COMPLETED),
    PRF_D(1, PRFEVT_1_STORE_COMPLETED),

    PRF_D(1, PRFEVT_1_WBB_SINGLE_REQUEST_UNAVAILABLE),
    PRF_D(1, PRFEVT_1_WBB_BURST_REQUEST_UNAVAILABLE),
    PRF_D(1, PRFEVT_1_WBB_BURST_REQUEST_ALMOST_FULL),
    PRF_D(1, PRFEVT_1_WBB_BURST_REQUEST_FULL)
};

// constructor - does nothing that could fault

PerfTest::PerfTest()
{
    uint32_t n;
    int n0 = 0;
    int n1 = 0;

    for (n = 0; n != Core::COP0_NUM_PERF_COUNTERS; n++) {
        m_iCurrentTest[n] = 0;
    }
    for (n = 0; n != NUM_PRF_TESTS; n++) {
        if (prfTable[n].cnt_id)
            n1++;
        else
            n0++;
        m_testVals[n] = MPRF_NOT_READY;
    }
    if (n0 > n1)
        m_nTests = n0;
    else
        m_nTests = n1;

    printf("PerfTest:: starting\n");
}

// destructor - does nothing, nice.

PerfTest::~PerfTest()
{
}

void PerfTest::StepTests()
{
    for (uint32_t n = 0; n != Core::COP0_NUM_PERF_COUNTERS; n++) {
        m_iCurrentTest[n] = GetNextTestID(n, m_iCurrentTest[n]);
    }
}

void PerfTest::DebugState()
{
    printf("PerfTest::Debug\n");
    printf("	test0 - %s\n", Core::ev0_Name[prfTable[m_iCurrentTest[0]].cnt_val]);
    printf("	test1 - %s\n", Core::ev1_Name[prfTable[m_iCurrentTest[1]].cnt_val]);
    printf("PerfTest::Debug -done\n");
}

void PerfTest::DumpCycleCount(int nLoops)
{
    printf("total counted cycles %12d ", m_CycleCount);
    if (nLoops > 1) {
        printf(" %7d ", (m_CycleCount + (nLoops >> 1)) / nLoops);
    }

    DumpBusStats(nLoops);
}

// type out some bus activity stats

void PerfTest::DumpBusStats(int nLoops)
{
    int t;
    t = m_testVals[PERF_CPU_DATA_BUS_BUSY];
    t *= 2; // as bus clocks
    if (t != MPRF_NOT_READY) {
        printf("DATA_BUS_BUSY %3d ", (int)(((float)t * 100.0f) / (float)m_CycleCount));
    }

    t = m_testVals[PERF_CPU_ADDRESS_BUS_BUSY];
    t *= 2; // as bus clocks
    if (t != MPRF_NOT_READY) {
        printf("ADDR_BUS_BUSY %3d ", (int)(((float)t * 100.0f) / (float)m_CycleCount));
    }
}

void PerfTest::DumpStats(int nLoops)
{
    printf("PerfTest: dump stats");
    if (nLoops > 1) {
        // 	printf(" - nLoops=%d",nLoops);
    }
    printf("\n");

    printf("total counted cycles %d ", m_CycleCount);
    if (nLoops > 1) {
        printf(" %d", (m_CycleCount + (nLoops >> 1)) / nLoops);
    }
    printf("\n");

    for (uint32_t n = 0; n != NUM_PRF_TESTS; n++) {
        int lval = 0;
        if (m_testVals[n] == MPRF_NOT_READY) {
            printf("NOT_READY ");
        } else {
            printf("%9d ", m_testVals[n]);
        }
        if (nLoops > 1) {
            if (m_testVals[n] == MPRF_NOT_READY) {
                printf("NOT_READY ");
            } else {
                printf("%9d ", (lval = (m_testVals[n] + (nLoops >> 1)) / nLoops));
            }
        }
        if (prfTable[n].cnt_id) {
            printf(" - %s", Core::ev1_Name[prfTable[n].cnt_val] + 9);
        } else {
            printf(" - %s", Core::ev0_Name[prfTable[n].cnt_val] + 9);
        }
        if ((nLoops > 1) && ((n == PERF_CPU_DATA_BUS_BUSY) || (n == PERF_CPU_ADDRESS_BUS_BUSY))) {
            printf(" CPU_CLOCKS=%d", lval * 2);
        }

        printf("\n");
    }
    if (m_testVals[PERF_DTLB_ACCESSED]) {
        printf("dcache miss frq %d%%\n", m_testVals[PERF_DCACHE_MISS] * 100 / m_testVals[PERF_DTLB_ACCESSED]);
    } else {
        printf("no l/s activity\n");
    }
    printf("cpu wait cycles %d%% ",
        ((m_testVals[PERF_PROCESSOR_CYCLE] - m_testVals[PERF_SINGLE_INST_ISSUE] - m_testVals[PERF_DUAL_INSTRUCTION_ISSUE]) * 100) / m_testVals[PERF_PROCESSOR_CYCLE]);

    if (nLoops > 1) {
        printf(" (%d)", (m_testVals[PERF_PROCESSOR_CYCLE] - m_testVals[PERF_SINGLE_INST_ISSUE] - m_testVals[PERF_DUAL_INSTRUCTION_ISSUE] + (nLoops >> 1)) / nLoops);
    }
    printf("\n");
    printf("insn throughput %d%%\n", (m_testVals[PERF_INSTRUCTION_COMPLETED] * 100) / m_testVals[PERF_PROCESSOR_CYCLE]);
    printf("code lazy       %d%%\n", (m_testVals[PERF_SINGLE_INST_ISSUE] * 100) / (m_testVals[PERF_DUAL_INSTRUCTION_ISSUE] + m_testVals[PERF_SINGLE_INST_ISSUE]));

    printf("PerfTest::DumpStats() done\n\n");
}

int PerfTest::sDumpStats(char* buffer, int nLoops)
{
    int nc = 0;
    nc += sprintf(buffer + nc, "PerfTest: dump stats");

    if (nLoops > 1) {
        nc += sprintf(buffer + nc, " - nLoops=%d", nLoops);
    }
    nc += sprintf(buffer + nc, "\n");

    nc += sprintf(buffer + nc, "total counted cycles %d ", m_CycleCount);
    if (nLoops > 1) {
        nc += sprintf(buffer + nc, " %d", (m_CycleCount + (nLoops >> 1)) / nLoops);
    }
    nc += sprintf(buffer + nc, "\n");

    for (uint32_t n = 0; n != NUM_PRF_TESTS; n++) {
        if (m_testVals[n] == MPRF_NOT_READY) {
            nc += sprintf(buffer + nc, "NOT_READY ");
        } else {
            nc += sprintf(buffer + nc, "%9d ", m_testVals[n]);
        }
        if (nLoops > 1) {
            if (m_testVals[n] == MPRF_NOT_READY) {
                nc += sprintf(buffer + nc, "NOT_READY ");
            } else {
                nc += sprintf(buffer + nc, "%9d ", (m_testVals[n] + (nLoops >> 1)) / nLoops);
            }
        }
        if (prfTable[n].cnt_id) {
            nc += sprintf(buffer + nc, " - %s\n", Core::ev1_Name[prfTable[n].cnt_val] + 9);
        } else {
            nc += sprintf(buffer + nc, " - %s\n", Core::ev0_Name[prfTable[n].cnt_val] + 9);
        }
    }
    nc += sprintf(buffer + nc, "PerfTest::DumpStats() done\n\n");

    if (m_testVals[PERF_DTLB_ACCESSED]) {
        nc += printf(buffer + nc, "dcache miss freq %d%%\n", m_testVals[PERF_DCACHE_MISS] * 100 / m_testVals[PERF_DTLB_ACCESSED]);
    } else {
        nc += sprintf(buffer + nc, "no l/s activity\n");
    }
    nc += sprintf(buffer + nc, "cpu wait cycles %d ", m_testVals[PERF_PROCESSOR_CYCLE] - m_testVals[PERF_SINGLE_INST_ISSUE] - m_testVals[PERF_DUAL_INSTRUCTION_ISSUE]);
    if (nLoops > 1) {
        nc += sprintf(buffer + nc, " (%d)", (m_testVals[PERF_PROCESSOR_CYCLE] - m_testVals[PERF_SINGLE_INST_ISSUE] - m_testVals[PERF_DUAL_INSTRUCTION_ISSUE] + (nLoops >> 1)) / nLoops);
    }
    nc += sprintf(buffer + nc, "\n");

    return nc;
}

int PerfTest::GetNextTestID(uint32_t counter_id, uint32_t ctest)
{
    if (counter_id >= 2)
        return 0;
    do {
        ctest++;
        if (ctest >= NUM_PRF_TESTS) {
            ctest = 0; // wrap to next
        }
    } while (prfTable[ctest].cnt_id != counter_id);

    return ctest;
}
