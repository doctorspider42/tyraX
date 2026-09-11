#ifndef __OPENVCL_VUINSTRUCTIONINFO_H__
#define __OPENVCL_VUINSTRUCTIONINFO_H__

/*
 * VuInstructionInfo.h
 *
 * Shared VU instruction metadata for scheduling and cost analysis.
 */

#include <string>

#include "Operand.h"

namespace vcl
{

enum VuPipelineSlot
{
	VU_PIPE_UNKNOWN,
	VU_PIPE_NOP,
	VU_PIPE_UPPER,
	VU_PIPE_LOWER
};

enum VuExecutionUnit
{
	VU_EXEC_UNKNOWN,
	VU_EXEC_NOP,
	VU_EXEC_FMAC,
	VU_EXEC_FDIV,
	VU_EXEC_LSU,
	VU_EXEC_IALU,
	VU_EXEC_BRU,
	VU_EXEC_RANDU,
	VU_EXEC_EFU
};

enum VuInstructionFlags
{
	VU_INSTR_NONE      = 0,
	VU_INSTR_BRANCH    = 1 << 0,
	VU_INSTR_WAIT_Q    = 1 << 1,
	VU_INSTR_WAIT_P    = 1 << 2,
	VU_INSTR_WRITES_Q  = 1 << 3,
	VU_INSTR_WRITES_P  = 1 << 4,
	VU_INSTR_WRITES_I  = 1 << 5,
	VU_INSTR_XGKICK    = 1 << 6,
	VU_INSTR_UNCONDITIONAL_BRANCH = 1 << 7,
	VU_INSTR_LINK_BRANCH = 1 << 8,
	VU_INSTR_REGISTER_BRANCH = 1 << 9
};

enum VuResourceFlags
{
	VU_RESOURCE_NONE   = 0,
	VU_RESOURCE_ACC    = 1 << 0,
	VU_RESOURCE_I      = 1 << 1,
	VU_RESOURCE_Q      = 1 << 2,
	VU_RESOURCE_P      = 1 << 3,
	VU_RESOURCE_R      = 1 << 4,
	VU_RESOURCE_MAC    = 1 << 5,
	VU_RESOURCE_CLIP   = 1 << 6,
	VU_RESOURCE_STATUS = 1 << 7
};

// Resources whose writes ACCUMULATE instead of replacing: a second write does not
// retire the first, it merges with it, so two writers commute and there is no
// write-after-write hazard between them at all.
//
// The status register is the one. Its sticky half (ZS/SS/US/OS/IS/DS, bits 6..11)
// is OR-ed into by every FMAC and by DIV/SQRT/RSQRT, and only FSSET clears it - so
// modelling status as an ordinary register would be wrong in the expensive
// direction (an FMAC's contribution deleted or hoisted past a reader) AND in the
// cheap one (a WAW edge between every pair of FMACs, which under the pairwise
// dependency builder would serialise every program that contains one `fsand`).
//
// Two things still order accumulating writes, and they are NOT WAW:
//   * every contributor since the last clear must precede a reader (RAW), and a
//     reader must precede every later contributor (WAR) - both pairwise tests
//     already cover this once the table declares the reads and writes;
//   * FSSET clears, so it is declared read-AND-write of the resource. That single
//     declaration turns it into a hard barrier under the same RAW/WAR tests,
//     without any test having to know what a "clear" is - the same trick that
//     makes RNEXT/RXOR correct.
//
// The non-sticky half (Z/S/U/O/I/D, bits 0..5) IS last-writer-wins, and that is
// modelled where instructions actually get reordered wholesale - see
// addAccumulatingImplicitFlagDependencies() in VuSchedulerAnalysis.cpp, which pins
// the last contributor before a reader whose mask names one of those bits.
enum VuAccumulatingResourceFlags
{
	VU_RESOURCE_ACCUMULATING = VU_RESOURCE_STATUS
};

enum VuMemoryKind
{
	VU_MEMORY_NONE,
	VU_MEMORY_LOAD,
	VU_MEMORY_STORE,
	VU_MEMORY_XGKICK
};

enum VuMemoryFlags
{
	VU_MEMORY_FLAG_NONE    = 0,
	VU_MEMORY_FLAG_PREDEC  = 1 << 0,
	VU_MEMORY_FLAG_POSTINC = 1 << 1
};

enum VuBypassFlags
{
	VU_BYPASS_NONE         = 0,
	VU_BYPASS_FTOI_TO_MTIR = 1 << 0,
	VU_BYPASS_LOAD_TO_FTOI = 1 << 1,
	VU_BYPASS_LOAD_TO_MINII = 1 << 2
};

enum VuInstructionOpcode
{
	VU_OP_INVALID,
	VU_OP_ABS,
	VU_OP_ADD,
	VU_OP_ADDA,
	VU_OP_ADDAI,
	VU_OP_ADDAQ,
	VU_OP_ADDI,
	VU_OP_ADDQ,
	VU_OP_B,
	VU_OP_BAL,
	VU_OP_CLIP,
	VU_OP_CLIPLW,
	VU_OP_CLIPW,
	VU_OP_DIV,
	VU_OP_EATAN,
	VU_OP_EATANXY,
	VU_OP_EATANXZ,
	VU_OP_EEXP,
	VU_OP_ELENG,
	VU_OP_ERCPR,
	VU_OP_ERLENG,
	VU_OP_ERSADD,
	VU_OP_ERSQRT,
	VU_OP_ESADD,
	VU_OP_ESIN,
	VU_OP_ESUM,
	VU_OP_FCAND,
	VU_OP_FCEQ,
	VU_OP_FCGET,
	VU_OP_FCOR,
	VU_OP_FCSET,
	VU_OP_FMAND,
	VU_OP_FMEQ,
	VU_OP_FMOR,
	VU_OP_FSAND,
	VU_OP_FSEQ,
	VU_OP_FSOR,
	VU_OP_FSSET,
	VU_OP_FTOI0,
	VU_OP_FTOI12,
	VU_OP_FTOI15,
	VU_OP_FTOI4,
	VU_OP_IADD,
	VU_OP_IADDI,
	VU_OP_IADDIU,
	VU_OP_IAND,
	VU_OP_IBEQ,
	VU_OP_IBGEZ,
	VU_OP_IBGTZ,
	VU_OP_IBLEZ,
	VU_OP_IBLTZ,
	VU_OP_IBNE,
	VU_OP_ILW,
	VU_OP_ILWR,
	VU_OP_IOR,
	VU_OP_ISUB,
	VU_OP_ISUBIU,
	VU_OP_ISW,
	VU_OP_ISWR,
	VU_OP_ITOF0,
	VU_OP_ITOF12,
	VU_OP_ITOF15,
	VU_OP_ITOF4,
	VU_OP_JALR,
	VU_OP_JR,
	VU_OP_LOI,
	VU_OP_LQ,
	VU_OP_LQD,
	VU_OP_LQI,
	VU_OP_MADD,
	VU_OP_MADDA,
	VU_OP_MADDAI,
	VU_OP_MADDAQ,
	VU_OP_MADDI,
	VU_OP_MADDQ,
	VU_OP_MAX,
	VU_OP_MAXI,
	VU_OP_MFIR,
	VU_OP_MFP,
	VU_OP_MINI,
	VU_OP_MINII,
	VU_OP_MOVE,
	VU_OP_MR32,
	VU_OP_MSUB,
	VU_OP_MSUBA,
	VU_OP_MSUBAI,
	VU_OP_MSUBAQ,
	VU_OP_MSUBI,
	VU_OP_MSUBQ,
	VU_OP_MTIR,
	VU_OP_MUL,
	VU_OP_MULA,
	VU_OP_MULAI,
	VU_OP_MULAQ,
	VU_OP_MULI,
	VU_OP_MULQ,
	VU_OP_NOP,
	VU_OP_OPMSUB,
	VU_OP_OPMULA,
	VU_OP_RGET,
	VU_OP_RINIT,
	VU_OP_RNEXT,
	VU_OP_RSQRT,
	VU_OP_RXOR,
	VU_OP_SQ,
	VU_OP_SQD,
	VU_OP_SQI,
	VU_OP_SQRT,
	VU_OP_SUB,
	VU_OP_SUBA,
	VU_OP_SUBAI,
	VU_OP_SUBAQ,
	VU_OP_SUBI,
	VU_OP_SUBQ,
	VU_OP_WAITP,
	VU_OP_WAITQ,
	VU_OP_XGKICK,
	VU_OP_XITOP,
	VU_OP_XTOP
};

struct VuInstructionInfo
{
	VuInstructionOpcode opcode;
	const char* mnemonic;
	const char* operandName;
	unsigned int arguments;
	unsigned int operandFlags;
	const char* operandPattern;
	Operand::Unit operandUnit;
	VuPipelineSlot pipe;
	VuExecutionUnit unit;
	unsigned int throughput;
	unsigned int latency;
	unsigned int flags;
	unsigned int implicitReads;
	unsigned int implicitWrites;
	VuMemoryKind memoryKind;
	unsigned int memoryFlags;
	unsigned int branchDelaySlots;
	unsigned int bypassFlags;
};

const char* vuInstructionMnemonic( VuInstructionOpcode opcode );
VuInstructionOpcode vuInstructionOpcodeForMnemonic( const std::string& mnemonic );
const VuInstructionInfo* allVuInstructionInfos();
const VuInstructionInfo* findVuInstructionInfo( const std::string& mnemonic );
bool isKnownVuInstruction( const std::string& mnemonic );
std::string normalizeVuMnemonic( const std::string& word );
std::string vuInstructionParameterSummary( const VuInstructionInfo& info );
const char* vuInstructionDescription( const VuInstructionInfo& info );

}

#endif
