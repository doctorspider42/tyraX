/*	  Copyright (C) 2000,2001,2002  Sony Computer Entertainment America

       	  This file is subject to the terms and conditions of the GNU Lesser
	  General Public License Version 2.1. See the file "COPYING" in the
	  main directory of this archive for more details.                             */

#include "ps2s/core.h"

namespace Core {

const char* ev0_Name[COP0_NUM_PERF_EVENTS] = {
    "PRFEVT_0_NOTHING",
    "PRFEVT_0_PROCESSOR_CYCLE",
    "PRFEVT_0_SINGLE_INST_ISSUE",
    "PRFEVT_0_BRANCH_ISSUED",
    "PRFEVT_0_BTAC_MISS",
    "PRFEVT_0_TLB_MISS",
    "PRFEVT_0_ICACHE_MISS",
    "PRFEVT_0_DTLB_ACCESSED",
    "PRFEVT_0_NON_BLOCK_LOAD",
    "PRFEVT_0_WBB_SINGLE_REQUEST",
    "PRFEVT_0_WBB_BURST_REQUEST",
    "PRFEVT_0_CPU_ADDRESS_BUS_BUSY(BUS_CLOCKS)",
    "PRFEVT_0_INSTRUCTION_COMPLETED",
    "PRFEVT_0_NON_BDS_INSTRUCTION_COMPLETED",
    "PRFEVT_0_COP2_INSTRUCTION_COMPLETED",
    "PRFEVT_0_LOAD_COMPLETED",
    "PRFEVT_0_NO_EVENT"
};

const char* ev1_Name[COP0_NUM_PERF_EVENTS] = {
    "PRFEVT_1_LOW_ORDER_BRANCH_ISSUES",
    "PRFEVT_1_PROCESSOR_CYCLE",
    "PRFEVT_1_DUAL_INSTRUCTION_ISSUE",
    "PRFEVT_1_BRANCH_MISPREDICTED",
    "PRFEVT_1_TLB_MISS",
    "PRFEVT_1_DTLB_MISS",
    "PRFEVT_1_DCACHE_MISS",
    "PRFEVT_1_WBB_SINGLE_REQUEST_UNAVAILABLE",
    "PRFEVT_1_WBB_BURST_REQUEST_UNAVAILABLE",
    "PRFEVT_1_WBB_BURST_REQUEST_ALMOST_FULL",
    "PRFEVT_1_WBB_BURST_REQUEST_FULL",
    "PRFEVT_1_CPU_DATA_BUS_BUSY(BUS_CLOCKS)   ",
    "PRFEVT_1_INSTRUCTION_COMPLETED",
    "PRFEVT_1_NON_BDS_INSTRUCTION_COMPLETED",
    "PRFEVT_1_COP1_INSTRUCTION_COMPLETED",
    "PRFEVT_1_STORE_COMPLETED",
    "PRFEVT_1_NO_EVENT"
};

void SetupPerfCounters(uint32_t evt_0, uint32_t evt_1)
{
    asm __volatile__(

        ".set	noreorder							\n"

        "mtps	$0,0			# halt performance counters		\n"
        "sync.p				# 					\n"

        "mtpc	$0,0 			# set perfcounter 0 to $0		\n"
        "mtpc	$0,1 			# set perfcounter 1 to $0		\n"

        "sync.p				# 					\n"

        "andi	%0,%0,31 		# mask off unused bits			\n"
        "andi	%1,%1,31							\n"

        "sll    	%0,%0,5							\n"
        "sll    	%1,%1,15						\n"

        "ori    	%0,%0,16 	# set U0				\n"
        "ori    	%1,%1,1<<14	# set U1				\n"

        "or     	%0,%1,%0						\n"
        "lui    	%1,0x8000	# master enable				\n"

        "or     	%0,%1,%0	# set top bit - this is Counter Enable	\n"
        "mtps	%0,0 			# truly - we rule			\n"
        "sync.p									\n"

        ".set	reorder								\n"

        : // no output
        : "r"(evt_0), "r"(evt_1));
}

}; // end core
