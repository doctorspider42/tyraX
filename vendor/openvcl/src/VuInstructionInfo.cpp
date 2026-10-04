#include "VuInstructionInfo.h"

#include <cctype>
#include <cstring>

namespace vcl
{

namespace
{
	struct VuInstructionOpcodeInfo
	{
		VuInstructionOpcode opcode;
		const char* mnemonic;
	};

	const VuInstructionOpcodeInfo kInstructionOpcodes[] =
	{
		{ VU_OP_ABS, "abs" },
		{ VU_OP_ADD, "add" },
		{ VU_OP_ADDA, "adda" },
		{ VU_OP_ADDAI, "addai" },
		{ VU_OP_ADDAQ, "addaq" },
		{ VU_OP_ADDI, "addi" },
		{ VU_OP_ADDQ, "addq" },
		{ VU_OP_B, "b" },
		{ VU_OP_BAL, "bal" },
		{ VU_OP_CLIP, "clip" },
		{ VU_OP_CLIPLW, "cliplw" },
		{ VU_OP_CLIPW, "clipw" },
		{ VU_OP_DIV, "div" },
		{ VU_OP_EATAN, "eatan" },
		{ VU_OP_EATANXY, "eatanxy" },
		{ VU_OP_EATANXZ, "eatanxz" },
		{ VU_OP_EEXP, "eexp" },
		{ VU_OP_ELENG, "eleng" },
		{ VU_OP_ERCPR, "ercpr" },
		{ VU_OP_ERLENG, "erleng" },
		{ VU_OP_ERSADD, "ersadd" },
		{ VU_OP_ERSQRT, "ersqrt" },
		{ VU_OP_ESADD, "esadd" },
		{ VU_OP_ESIN, "esin" },
		{ VU_OP_ESUM, "esum" },
		{ VU_OP_FCAND, "fcand" },
		{ VU_OP_FCEQ, "fceq" },
		{ VU_OP_FCGET, "fcget" },
		{ VU_OP_FCOR, "fcor" },
		{ VU_OP_FCSET, "fcset" },
		{ VU_OP_FMAND, "fmand" },
		{ VU_OP_FMEQ, "fmeq" },
		{ VU_OP_FMOR, "fmor" },
		{ VU_OP_FSAND, "fsand" },
		{ VU_OP_FSEQ, "fseq" },
		{ VU_OP_FSOR, "fsor" },
		{ VU_OP_FSSET, "fsset" },
		{ VU_OP_FTOI0, "ftoi0" },
		{ VU_OP_FTOI12, "ftoi12" },
		{ VU_OP_FTOI15, "ftoi15" },
		{ VU_OP_FTOI4, "ftoi4" },
		{ VU_OP_IADD, "iadd" },
		{ VU_OP_IADDI, "iaddi" },
		{ VU_OP_IADDIU, "iaddiu" },
		{ VU_OP_IAND, "iand" },
		{ VU_OP_IBEQ, "ibeq" },
		{ VU_OP_IBGEZ, "ibgez" },
		{ VU_OP_IBGTZ, "ibgtz" },
		{ VU_OP_IBLEZ, "iblez" },
		{ VU_OP_IBLTZ, "ibltz" },
		{ VU_OP_IBNE, "ibne" },
		{ VU_OP_ILW, "ilw" },
		{ VU_OP_ILWR, "ilwr" },
		{ VU_OP_IOR, "ior" },
		{ VU_OP_ISUB, "isub" },
		{ VU_OP_ISUBIU, "isubiu" },
		{ VU_OP_ISW, "isw" },
		{ VU_OP_ISWR, "iswr" },
		{ VU_OP_ITOF0, "itof0" },
		{ VU_OP_ITOF12, "itof12" },
		{ VU_OP_ITOF15, "itof15" },
		{ VU_OP_ITOF4, "itof4" },
		{ VU_OP_JALR, "jalr" },
		{ VU_OP_JR, "jr" },
		{ VU_OP_LOI, "loi" },
		{ VU_OP_LQ, "lq" },
		{ VU_OP_LQD, "lqd" },
		{ VU_OP_LQI, "lqi" },
		{ VU_OP_MADD, "madd" },
		{ VU_OP_MADDA, "madda" },
		{ VU_OP_MADDAI, "maddai" },
		{ VU_OP_MADDAQ, "maddaq" },
		{ VU_OP_MADDI, "maddi" },
		{ VU_OP_MADDQ, "maddq" },
		{ VU_OP_MAX, "max" },
		{ VU_OP_MAXI, "maxi" },
		{ VU_OP_MFIR, "mfir" },
		{ VU_OP_MFP, "mfp" },
		{ VU_OP_MINI, "mini" },
		{ VU_OP_MINII, "minii" },
		{ VU_OP_MOVE, "move" },
		{ VU_OP_MR32, "mr32" },
		{ VU_OP_MSUB, "msub" },
		{ VU_OP_MSUBA, "msuba" },
		{ VU_OP_MSUBAI, "msubai" },
		{ VU_OP_MSUBAQ, "msubaq" },
		{ VU_OP_MSUBI, "msubi" },
		{ VU_OP_MSUBQ, "msubq" },
		{ VU_OP_MTIR, "mtir" },
		{ VU_OP_MUL, "mul" },
		{ VU_OP_MULA, "mula" },
		{ VU_OP_MULAI, "mulai" },
		{ VU_OP_MULAQ, "mulaq" },
		{ VU_OP_MULI, "muli" },
		{ VU_OP_MULQ, "mulq" },
		{ VU_OP_NOP, "nop" },
		{ VU_OP_OPMSUB, "opmsub" },
		{ VU_OP_OPMULA, "opmula" },
		{ VU_OP_RGET, "rget" },
		{ VU_OP_RINIT, "rinit" },
		{ VU_OP_RNEXT, "rnext" },
		{ VU_OP_RSQRT, "rsqrt" },
		{ VU_OP_RXOR, "rxor" },
		{ VU_OP_SQ, "sq" },
		{ VU_OP_SQD, "sqd" },
		{ VU_OP_SQI, "sqi" },
		{ VU_OP_SQRT, "sqrt" },
		{ VU_OP_SUB, "sub" },
		{ VU_OP_SUBA, "suba" },
		{ VU_OP_SUBAI, "subai" },
		{ VU_OP_SUBAQ, "subaq" },
		{ VU_OP_SUBI, "subi" },
		{ VU_OP_SUBQ, "subq" },
		{ VU_OP_WAITP, "waitp" },
		{ VU_OP_WAITQ, "waitq" },
		{ VU_OP_XGKICK, "xgkick" },
		{ VU_OP_XITOP, "xitop" },
		{ VU_OP_XTOP, "xtop" },
		{ VU_OP_INVALID, 0 }
	};

#define VU_INFO(mn, name, args, opFlags, pattern, opUnit, pipe, unit, thr, lat, flags, reads, writes, memKind, memFlags, delay, bypass) \
	{ mn, vuInstructionMnemonic(mn), name, args, opFlags, pattern, opUnit, pipe, unit, thr, lat, flags, reads, writes, memKind, memFlags, delay, bypass }

#define VU_UPPER_DEST(mn, name, args, pattern) \
	VU_INFO(mn, name, args, Operand::UPPER|Operand::DEST, pattern, Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 3, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_MAC|VU_RESOURCE_STATUS, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE)

#define VU_UPPER_BC(mn, name, args, pattern) \
	VU_INFO(mn, name, args, Operand::UPPER|Operand::BROADCAST, pattern, Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 3, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_MAC|VU_RESOURCE_STATUS, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE)

#define VU_UPPER_DEST_READS(mn, name, args, pattern, reads) \
	VU_INFO(mn, name, args, Operand::UPPER|Operand::DEST, pattern, Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 3, VU_INSTR_NONE, reads, VU_RESOURCE_MAC|VU_RESOURCE_STATUS, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE)

#define VU_UPPER_DEST_ACC_READ(mn, name, args, pattern) \
	VU_INFO(mn, name, args, Operand::UPPER|Operand::DEST, pattern, Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 3, VU_INSTR_NONE, VU_RESOURCE_ACC, VU_RESOURCE_MAC|VU_RESOURCE_STATUS, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE)

#define VU_UPPER_DEST_ACC_READS(mn, name, args, pattern, reads) \
	VU_INFO(mn, name, args, Operand::UPPER|Operand::DEST, pattern, Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 3, VU_INSTR_NONE, VU_RESOURCE_ACC|reads, VU_RESOURCE_MAC|VU_RESOURCE_STATUS, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE)

#define VU_UPPER_BC_ACC_READ(mn, name, args, pattern) \
	VU_INFO(mn, name, args, Operand::UPPER|Operand::BROADCAST, pattern, Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 3, VU_INSTR_NONE, VU_RESOURCE_ACC, VU_RESOURCE_MAC|VU_RESOURCE_STATUS, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE)

#define VU_ACC_DEST(mn, name, args, pattern) \
	VU_INFO(mn, name, args, Operand::UPPER|Operand::DEST, pattern, Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 3, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_ACC|VU_RESOURCE_MAC|VU_RESOURCE_STATUS, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE)

#define VU_ACC_BC(mn, name, args, pattern) \
	VU_INFO(mn, name, args, Operand::UPPER|Operand::BROADCAST, pattern, Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 3, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_ACC|VU_RESOURCE_MAC|VU_RESOURCE_STATUS, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE)

#define VU_ACC_READS(mn, name, args, pattern, reads) \
	VU_INFO(mn, name, args, Operand::UPPER|Operand::DEST, pattern, Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 3, VU_INSTR_NONE, reads, VU_RESOURCE_ACC|VU_RESOURCE_MAC|VU_RESOURCE_STATUS, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE)

#define VU_ACC_ACC_READ(mn, name, args, pattern) \
	VU_INFO(mn, name, args, Operand::UPPER|Operand::DEST, pattern, Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 3, VU_INSTR_NONE, VU_RESOURCE_ACC, VU_RESOURCE_ACC|VU_RESOURCE_MAC|VU_RESOURCE_STATUS, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE)

#define VU_ACC_ACC_READS(mn, name, args, pattern, reads) \
	VU_INFO(mn, name, args, Operand::UPPER|Operand::DEST, pattern, Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 3, VU_INSTR_NONE, VU_RESOURCE_ACC|reads, VU_RESOURCE_ACC|VU_RESOURCE_MAC|VU_RESOURCE_STATUS, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE)

#define VU_ACC_BC_ACC_READ(mn, name, args, pattern) \
	VU_INFO(mn, name, args, Operand::UPPER|Operand::BROADCAST, pattern, Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 3, VU_INSTR_NONE, VU_RESOURCE_ACC, VU_RESOURCE_ACC|VU_RESOURCE_MAC|VU_RESOURCE_STATUS, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE)

	const VuInstructionInfo kInstructions[] =
	{
		VU_INFO(VU_OP_NOP, "NOP", 0, 0, "", Operand::INVALID, VU_PIPE_NOP, VU_EXEC_NOP, 0, 0, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),

		VU_UPPER_DEST(VU_OP_ABS, "ABS", 2, "vf:dest:write,vf:dest"),

		VU_UPPER_DEST(VU_OP_ADD, "ADD", 3, "vf:dest:write,vf:dest,vf:dest"),
		VU_UPPER_DEST_READS(VU_OP_ADDI, "ADDi", 3, "vf:dest:write,vf:dest,i", VU_RESOURCE_I),
		VU_UPPER_DEST_READS(VU_OP_ADDQ, "ADDq", 3, "vf:dest:write,vf:dest,q", VU_RESOURCE_Q),
		VU_UPPER_BC(VU_OP_ADD, "ADD", 3, "vf:dest:write,vf:dest,vf:bc"),
		VU_ACC_DEST(VU_OP_ADDA, "ADDA", 3, "acc:dest:write,vf:dest,vf:dest"),
		VU_ACC_READS(VU_OP_ADDAI, "ADDAi", 3, "acc:dest:write,vf:dest,i", VU_RESOURCE_I),
		VU_ACC_READS(VU_OP_ADDAQ, "ADDAq", 3, "acc:dest:write,vf:dest,q", VU_RESOURCE_Q),
		VU_ACC_BC(VU_OP_ADDA, "ADDA", 3, "acc:dest:write,vf:dest,vf:bc"),

		VU_UPPER_DEST(VU_OP_SUB, "SUB", 3, "vf:dest:write,vf:dest,vf:dest"),
		VU_UPPER_DEST_READS(VU_OP_SUBI, "SUBi", 3, "vf:dest:write,vf:dest,i", VU_RESOURCE_I),
		VU_UPPER_DEST_READS(VU_OP_SUBQ, "SUBq", 3, "vf:dest:write,vf:dest,q", VU_RESOURCE_Q),
		VU_UPPER_BC(VU_OP_SUB, "SUB", 3, "vf:dest:write,vf:dest,vf:bc"),
		VU_ACC_DEST(VU_OP_SUBA, "SUBA", 3, "acc:dest:write,vf:dest,vf:dest"),
		VU_ACC_READS(VU_OP_SUBAI, "SUBAi", 3, "acc:dest:write,vf:dest,i", VU_RESOURCE_I),
		VU_ACC_READS(VU_OP_SUBAQ, "SUBAq", 3, "acc:dest:write,vf:dest,q", VU_RESOURCE_Q),
		VU_ACC_BC(VU_OP_SUBA, "SUBA", 3, "acc:dest:write,vf:dest,vf:bc"),

		VU_UPPER_DEST(VU_OP_MUL, "MUL", 3, "vf:dest:write,vf:dest,vf:dest"),
		VU_UPPER_DEST_READS(VU_OP_MULI, "MULi", 3, "vf:dest:write,vf:dest,i", VU_RESOURCE_I),
		VU_UPPER_DEST_READS(VU_OP_MULQ, "MULq", 3, "vf:dest:write,vf:dest,q", VU_RESOURCE_Q),
		VU_UPPER_BC(VU_OP_MUL, "MUL", 3, "vf:dest:write,vf:dest,vf:bc"),
		VU_ACC_DEST(VU_OP_MULA, "MULA", 3, "acc:dest:write,vf:dest,vf:dest"),
		VU_ACC_READS(VU_OP_MULAI, "MULAi", 3, "acc:dest:write,vf:dest,i", VU_RESOURCE_I),
		VU_ACC_READS(VU_OP_MULAQ, "MULAq", 3, "acc:dest:write,vf:dest,q", VU_RESOURCE_Q),
		VU_ACC_BC(VU_OP_MULA, "MULA", 3, "acc:dest:write,vf:dest,vf:bc"),

		VU_UPPER_DEST_ACC_READ(VU_OP_MADD, "MADD", 3, "vf:dest:write,vf:dest,vf:dest"),
		VU_UPPER_DEST_ACC_READS(VU_OP_MADDI, "MADDi", 3, "vf:dest:write,vf:dest,i", VU_RESOURCE_I),
		VU_UPPER_DEST_ACC_READS(VU_OP_MADDQ, "MADDq", 3, "vf:dest:write,vf:dest,q", VU_RESOURCE_Q),
		VU_UPPER_BC_ACC_READ(VU_OP_MADD, "MADD", 3, "vf:dest:write,vf:dest,vf:bc"),
		VU_ACC_ACC_READ(VU_OP_MADDA, "MADDA", 3, "acc:dest:write,vf:dest,vf:dest"),
		VU_ACC_ACC_READS(VU_OP_MADDAI, "MADDAi", 3, "acc:dest:write,vf:dest,i", VU_RESOURCE_I),
		VU_ACC_ACC_READS(VU_OP_MADDAQ, "MADDAq", 3, "acc:dest:write,vf:dest,q", VU_RESOURCE_Q),
		VU_ACC_BC_ACC_READ(VU_OP_MADDA, "MADDA", 3, "acc:dest:write,vf:dest,vf:bc"),

		VU_UPPER_DEST_ACC_READ(VU_OP_MSUB, "MSUB", 3, "vf:dest:write,vf:dest,vf:dest"),
		VU_UPPER_DEST_ACC_READS(VU_OP_MSUBI, "MSUBi", 3, "vf:dest:write,vf:dest,i", VU_RESOURCE_I),
		VU_UPPER_DEST_ACC_READS(VU_OP_MSUBQ, "MSUBq", 3, "vf:dest:write,vf:dest,q", VU_RESOURCE_Q),
		VU_UPPER_BC_ACC_READ(VU_OP_MSUB, "MSUB", 3, "vf:dest:write,vf:dest,vf:bc"),
		VU_ACC_ACC_READ(VU_OP_MSUBA, "MSUBA", 3, "acc:dest:write,vf:dest,vf:dest"),
		VU_ACC_ACC_READS(VU_OP_MSUBAI, "MSUBAi", 3, "acc:dest:write,vf:dest,i", VU_RESOURCE_I),
		VU_ACC_ACC_READS(VU_OP_MSUBAQ, "MSUBAq", 3, "acc:dest:write,vf:dest,q", VU_RESOURCE_Q),
		VU_ACC_BC_ACC_READ(VU_OP_MSUBA, "MSUBA", 3, "acc:dest:write,vf:dest,vf:bc"),

		VU_UPPER_DEST(VU_OP_MAX, "MAX", 3, "vf:dest:write,vf:dest,vf:dest"),
		VU_UPPER_DEST_READS(VU_OP_MAXI, "MAXi", 3, "vf:dest:write,vf:dest,i", VU_RESOURCE_I),
		VU_UPPER_BC(VU_OP_MAX, "MAX", 3, "vf:dest:write,vf:dest,vf:bc"),

		VU_UPPER_DEST(VU_OP_MINI, "MINI", 3, "vf:dest:write,vf:dest,vf:dest"),
		VU_UPPER_DEST_READS(VU_OP_MINII, "MINIi", 3, "vf:dest:write,vf:dest,i", VU_RESOURCE_I),
		VU_UPPER_BC(VU_OP_MINI, "MINI", 3, "vf:dest:write,vf:dest,vf:bc"),

		VU_INFO(VU_OP_OPMULA, "OPMULA", 3, Operand::UPPER|Operand::XYZ, "acc:dest:write,vf:dest,vf:dest", Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 3, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_ACC|VU_RESOURCE_MAC|VU_RESOURCE_STATUS, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_OPMSUB, "OPMSUB", 3, Operand::UPPER|Operand::XYZ, "vf:dest:write,vf:dest,vf:dest", Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 3, VU_INSTR_NONE, VU_RESOURCE_ACC, VU_RESOURCE_MAC|VU_RESOURCE_STATUS, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),

		VU_INFO(VU_OP_FTOI0, "FTOI0", 2, Operand::UPPER|Operand::DEST, "vf:dest:write,vf:dest", Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 3, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_MAC|VU_RESOURCE_STATUS, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_FTOI_TO_MTIR),
		VU_INFO(VU_OP_FTOI4, "FTOI4", 2, Operand::UPPER|Operand::DEST, "vf:dest:write,vf:dest", Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 3, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_MAC|VU_RESOURCE_STATUS, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_FTOI_TO_MTIR),
		VU_INFO(VU_OP_FTOI12, "FTOI12", 2, Operand::UPPER|Operand::DEST, "vf:dest:write,vf:dest", Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 3, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_MAC|VU_RESOURCE_STATUS, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_FTOI_TO_MTIR),
		VU_INFO(VU_OP_FTOI15, "FTOI15", 2, Operand::UPPER|Operand::DEST, "vf:dest:write,vf:dest", Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 3, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_MAC|VU_RESOURCE_STATUS, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_FTOI_TO_MTIR),
		VU_UPPER_DEST(VU_OP_ITOF0, "ITOF0", 2, "vf:dest:write,vf:dest"),
		VU_UPPER_DEST(VU_OP_ITOF4, "ITOF4", 2, "vf:dest:write,vf:dest"),
		VU_UPPER_DEST(VU_OP_ITOF12, "ITOF12", 2, "vf:dest:write,vf:dest"),
		VU_UPPER_DEST(VU_OP_ITOF15, "ITOF15", 2, "vf:dest:write,vf:dest"),

		// CLIP reads its first VF operand; it writes only MAC/CLIP flags.
		VU_INFO(VU_OP_CLIP, "CLIP", 2, Operand::UPPER|Operand::XYZ, "vf:dest,vf:wcomp", Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 3, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_MAC|VU_RESOURCE_CLIP|VU_RESOURCE_STATUS, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_CLIPW, "CLIPw", 2, Operand::UPPER|Operand::XYZ, "vf:dest,vf:wcomp", Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 3, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_MAC|VU_RESOURCE_CLIP|VU_RESOURCE_STATUS, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_CLIPLW, "CLIPLw", 2, Operand::UPPER|Operand::XYZ, "vf:dest,vf:wcomp", Operand::FMAC, VU_PIPE_UPPER, VU_EXEC_FMAC, 1, 3, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_MAC|VU_RESOURCE_CLIP|VU_RESOURCE_STATUS, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),

		VU_INFO(VU_OP_DIV, "DIV", 3, Operand::LOWER, "q:write,vf:flag,vf:flag", Operand::FDIV, VU_PIPE_LOWER, VU_EXEC_FDIV, 7, 7, VU_INSTR_WRITES_Q, VU_RESOURCE_NONE, VU_RESOURCE_Q|VU_RESOURCE_STATUS, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_SQRT, "SQRT", 2, Operand::LOWER, "q:write,vf:flag", Operand::FDIV, VU_PIPE_LOWER, VU_EXEC_FDIV, 7, 7, VU_INSTR_WRITES_Q, VU_RESOURCE_NONE, VU_RESOURCE_Q|VU_RESOURCE_STATUS, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_RSQRT, "RSQRT", 3, Operand::LOWER, "q:write,vf:flag,vf:flag", Operand::FDIV, VU_PIPE_LOWER, VU_EXEC_FDIV, 13, 13, VU_INSTR_WRITES_Q, VU_RESOURCE_NONE, VU_RESOURCE_Q|VU_RESOURCE_STATUS, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_WAITQ, "WAITQ", 0, Operand::LOWER, "", Operand::FDIV, VU_PIPE_LOWER, VU_EXEC_FDIV, 1, 1, VU_INSTR_WAIT_Q, VU_RESOURCE_Q, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),

		VU_INFO(VU_OP_IADD, "IADD", 3, Operand::LOWER, "vi:write,vi,vi", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_IADDI, "IADDI", 3, Operand::LOWER, "vi:write,vi,imm:evaluate", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_IADDIU, "IADDIU", 3, Operand::LOWER, "vi:write,vi,imm:evaluate", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_IAND, "IAND", 3, Operand::LOWER, "vi:write,vi,vi", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_IOR, "IOR", 3, Operand::LOWER, "vi:write,vi,vi", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_ISUB, "ISUB", 3, Operand::LOWER, "vi:write,vi,vi", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_ISUBIU, "ISUBIU", 3, Operand::LOWER, "vi:write,vi,imm:evaluate", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),

		VU_INFO(VU_OP_MOVE, "MOVE", 2, Operand::LOWER|Operand::DEST, "vf:dest:write,vf:dest", Operand::INVALID, VU_PIPE_LOWER, VU_EXEC_UNKNOWN, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_MFIR, "MFIR", 2, Operand::LOWER|Operand::DEST, "vf:dest:write,vi", Operand::INVALID, VU_PIPE_LOWER, VU_EXEC_UNKNOWN, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_MTIR, "MTIR", 2, Operand::LOWER, "vi:write,vf:flag", Operand::INVALID, VU_PIPE_LOWER, VU_EXEC_UNKNOWN, 1, 1, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_MR32, "MR32", 2, Operand::LOWER|Operand::DEST, "vf:dest:write,vf:dest:rotate", Operand::INVALID, VU_PIPE_LOWER, VU_EXEC_UNKNOWN, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),

		VU_INFO(VU_OP_LQ, "LQ", 2, Operand::LOWER|Operand::DEST, "vf:dest:write,(vi):zero|imm(vi):evaluate", Operand::LSU, VU_PIPE_LOWER, VU_EXEC_LSU, 1, 3, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_LOAD, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_LOAD_TO_FTOI|VU_BYPASS_LOAD_TO_MINII),
		VU_INFO(VU_OP_LQD, "LQD", 2, Operand::LOWER|Operand::DEST, "vf:dest:write,(vi):predec", Operand::LSU, VU_PIPE_LOWER, VU_EXEC_LSU, 1, 3, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_LOAD, VU_MEMORY_FLAG_PREDEC, 0, VU_BYPASS_LOAD_TO_FTOI|VU_BYPASS_LOAD_TO_MINII),
		VU_INFO(VU_OP_LQI, "LQI", 2, Operand::LOWER|Operand::DEST, "vf:dest:write,(vi):postinc", Operand::LSU, VU_PIPE_LOWER, VU_EXEC_LSU, 1, 3, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_LOAD, VU_MEMORY_FLAG_POSTINC, 0, VU_BYPASS_LOAD_TO_FTOI|VU_BYPASS_LOAD_TO_MINII),
		VU_INFO(VU_OP_SQ, "SQ", 2, Operand::LOWER|Operand::DEST, "vf:dest,(vi):zero|imm(vi):evaluate", Operand::LSU, VU_PIPE_LOWER, VU_EXEC_LSU, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_STORE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_SQD, "SQD", 2, Operand::LOWER|Operand::DEST, "vf:dest,(vi):predec", Operand::LSU, VU_PIPE_LOWER, VU_EXEC_LSU, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_STORE, VU_MEMORY_FLAG_PREDEC, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_SQI, "SQI", 2, Operand::LOWER|Operand::DEST, "vf:dest,(vi):postinc", Operand::LSU, VU_PIPE_LOWER, VU_EXEC_LSU, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_STORE, VU_MEMORY_FLAG_POSTINC, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_ILW, "ILW", 2, Operand::LOWER|Operand::DEST, "vi:write,(vi):zero:dest|imm(vi):evaluate:dest", Operand::LSU, VU_PIPE_LOWER, VU_EXEC_LSU, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_LOAD, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_ISW, "ISW", 2, Operand::LOWER|Operand::DEST, "vi,(vi):zero:dest|imm(vi):evaluate:dest", Operand::LSU, VU_PIPE_LOWER, VU_EXEC_LSU, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_STORE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_ILWR, "ILWR", 2, Operand::LOWER|Operand::DEST, "vi:write,(vi):dest", Operand::LSU, VU_PIPE_LOWER, VU_EXEC_LSU, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_LOAD, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_ISWR, "ISWR", 2, Operand::LOWER|Operand::DEST, "vi,(vi):dest", Operand::LSU, VU_PIPE_LOWER, VU_EXEC_LSU, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_STORE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),

		VU_INFO(VU_OP_LOI, "LOI", 1, Operand::LOWER|Operand::IWRITE, "imm:evaluate:raw", Operand::LSU, VU_PIPE_LOWER, VU_EXEC_LSU, 1, 1, VU_INSTR_WRITES_I, VU_RESOURCE_NONE, VU_RESOURCE_I, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),

		// R is the random unit's 23-bit LFSR, and only two of these four touch it
		// the way a plain register is touched. RINIT replaces it outright and RGET
		// only copies it out, so those two are a write and a read. The other two
		// are read-modify-writes and were described as if they were not:
		//
		//   RNEXT advances the LFSR and THEN copies it out, so it is a writer as
		//   much as a reader. Described as read-only, two RNEXTs carried no
		//   dependency between them at all, an RNEXT and a later RGET carried
		//   none either - the scheduler reordered them - and --drop-dead-writes
		//   deleted an RNEXT whose destination register was dead, which silently
		//   drops an LFSR step and changes every value drawn after it.
		//
		//   RXOR folds a source field INTO the current R, so the previous value is
		//   its input. Described as write-only, the dead-write walk treated the
		//   next RXOR as an overwrite and deleted the earlier one - the same
		//   mistake as treating CLIP as a register instead of a shift window, and
		//   with the same shape: a later write that consumes the earlier one
		//   rather than replacing it.
		VU_INFO(VU_OP_RINIT, "RINIT", 2, Operand::LOWER, "r:write,vf:flag", Operand::RANDU, VU_PIPE_LOWER, VU_EXEC_RANDU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_R, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_RGET, "RGET", 2, Operand::LOWER|Operand::DEST, "vf:dest:write,r", Operand::RANDU, VU_PIPE_LOWER, VU_EXEC_RANDU, 1, 4, VU_INSTR_NONE, VU_RESOURCE_R, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_RNEXT, "RNEXT", 2, Operand::LOWER|Operand::DEST, "vf:dest:write,r", Operand::RANDU, VU_PIPE_LOWER, VU_EXEC_RANDU, 1, 4, VU_INSTR_NONE, VU_RESOURCE_R, VU_RESOURCE_R, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_RXOR, "RXOR", 2, Operand::LOWER, "r:write,vf:flag", Operand::RANDU, VU_PIPE_LOWER, VU_EXEC_RANDU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_R, VU_RESOURCE_R, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),

		// The status register was declared as neither read nor written by anything,
		// so an `fsand` had no edge to the FMAC whose flags it exists to read and a
		// dead-code pass deleted that FMAC outright. FSAND/FSEQ/FSOR read it.
		//
		// FSSET is declared read-AND-write on purpose. It CLEARS the sticky bits,
		// which is the one thing that retires an accumulated contribution, so it has
		// to be a hard barrier: no contributor may sink past it and none may be
		// hoisted above it. Declaring it a reader as well as a writer says exactly
		// that in terms every existing pairwise test already understands (RAW from
		// contributors into it, WAR from it out to later contributors) without any
		// of them having to learn what a "clear" is - the same construction that
		// makes RNEXT/RXOR correct. It is also literally true of the register: the
		// non-sticky half survives an FSSET, so the result depends on the old value.
		VU_INFO(VU_OP_FSAND, "FSAND", 2, Operand::LOWER, "vi:write,imm:evaluate", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_STATUS, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_FSEQ, "FSEQ", 2, Operand::LOWER, "vi:write,imm:evaluate", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_STATUS, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_FSOR, "FSOR", 2, Operand::LOWER, "vi:write,imm:evaluate", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_STATUS, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_FSSET, "FSSET", 1, Operand::LOWER, "imm:evaluate", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 4, VU_INSTR_NONE, VU_RESOURCE_STATUS, VU_RESOURCE_STATUS, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_FMAND, "FMAND", 2, Operand::LOWER, "vi:write,vi", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_MAC, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_FMEQ, "FMEQ", 2, Operand::LOWER, "vi:write,vi", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_MAC, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_FMOR, "FMOR", 2, Operand::LOWER, "vi:write,vi", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_MAC, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_FCAND, "FCAND", 2, Operand::LOWER, "vi:write,imm:evaluate", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_CLIP, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_FCEQ, "FCEQ", 2, Operand::LOWER, "vi:write,imm:evaluate", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_CLIP, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_FCOR, "FCOR", 2, Operand::LOWER, "vi:write,imm:evaluate", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_CLIP, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_FCSET, "FCSET", 1, Operand::LOWER, "imm:evaluate", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 4, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_CLIP, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_FCGET, "FCGET", 1, Operand::LOWER, "vi:write", Operand::IALU, VU_PIPE_LOWER, VU_EXEC_IALU, 1, 1, VU_INSTR_NONE, VU_RESOURCE_CLIP, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),

		VU_INFO(VU_OP_IBEQ, "IBEQ", 3, Operand::LOWER|Operand::DYNAMIC, "vi,vi,imm:branch", Operand::BRU, VU_PIPE_LOWER, VU_EXEC_BRU, 2, 2, VU_INSTR_BRANCH, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 1, VU_BYPASS_NONE),
		VU_INFO(VU_OP_IBGEZ, "IBGEZ", 2, Operand::LOWER|Operand::DYNAMIC, "vi,imm:branch", Operand::BRU, VU_PIPE_LOWER, VU_EXEC_BRU, 2, 2, VU_INSTR_BRANCH, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 1, VU_BYPASS_NONE),
		VU_INFO(VU_OP_IBGTZ, "IBGTZ", 2, Operand::LOWER|Operand::DYNAMIC, "vi,imm:branch", Operand::BRU, VU_PIPE_LOWER, VU_EXEC_BRU, 2, 2, VU_INSTR_BRANCH, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 1, VU_BYPASS_NONE),
		VU_INFO(VU_OP_IBLEZ, "IBLEZ", 2, Operand::LOWER|Operand::DYNAMIC, "vi,imm:branch", Operand::BRU, VU_PIPE_LOWER, VU_EXEC_BRU, 2, 2, VU_INSTR_BRANCH, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 1, VU_BYPASS_NONE),
		VU_INFO(VU_OP_IBLTZ, "IBLTZ", 2, Operand::LOWER|Operand::DYNAMIC, "vi,imm:branch", Operand::BRU, VU_PIPE_LOWER, VU_EXEC_BRU, 2, 2, VU_INSTR_BRANCH, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 1, VU_BYPASS_NONE),
		VU_INFO(VU_OP_IBNE, "IBNE", 3, Operand::LOWER|Operand::DYNAMIC, "vi,vi,imm:branch", Operand::BRU, VU_PIPE_LOWER, VU_EXEC_BRU, 2, 2, VU_INSTR_BRANCH, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 1, VU_BYPASS_NONE),
		VU_INFO(VU_OP_B, "B", 1, Operand::LOWER, "imm:branch", Operand::BRU, VU_PIPE_LOWER, VU_EXEC_BRU, 2, 2, VU_INSTR_BRANCH|VU_INSTR_UNCONDITIONAL_BRANCH, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 1, VU_BYPASS_NONE),
		VU_INFO(VU_OP_BAL, "BAL", 2, Operand::LOWER, "vi:write:address,imm:branch", Operand::BRU, VU_PIPE_LOWER, VU_EXEC_BRU, 2, 2, VU_INSTR_BRANCH|VU_INSTR_LINK_BRANCH, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 1, VU_BYPASS_NONE),
		VU_INFO(VU_OP_JR, "JR", 1, Operand::LOWER, "vi:branch", Operand::BRU, VU_PIPE_LOWER, VU_EXEC_BRU, 2, 2, VU_INSTR_BRANCH|VU_INSTR_UNCONDITIONAL_BRANCH|VU_INSTR_REGISTER_BRANCH, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 1, VU_BYPASS_NONE),
		VU_INFO(VU_OP_JALR, "JALR", 2, Operand::LOWER, "vi:write:address,vi:branch", Operand::BRU, VU_PIPE_LOWER, VU_EXEC_BRU, 2, 2, VU_INSTR_BRANCH|VU_INSTR_LINK_BRANCH|VU_INSTR_REGISTER_BRANCH, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 1, VU_BYPASS_NONE),

		VU_INFO(VU_OP_MFP, "MFP", 2, Operand::LOWER|Operand::DEST, "vf:dest:write,p", Operand::EFU, VU_PIPE_LOWER, VU_EXEC_EFU, 1, 4, VU_INSTR_NONE, VU_RESOURCE_P, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_WAITP, "WAITP", 0, Operand::LOWER, "", Operand::EFU, VU_PIPE_LOWER, VU_EXEC_EFU, 1, 1, VU_INSTR_WAIT_P, VU_RESOURCE_P, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_ESADD, "ESADD", 2, Operand::LOWER, "p:write,vf", Operand::EFU, VU_PIPE_LOWER, VU_EXEC_EFU, 10, 11, VU_INSTR_WRITES_P, VU_RESOURCE_NONE, VU_RESOURCE_P, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_ERSADD, "ERSADD", 2, Operand::LOWER, "p:write,vf", Operand::EFU, VU_PIPE_LOWER, VU_EXEC_EFU, 17, 18, VU_INSTR_WRITES_P, VU_RESOURCE_NONE, VU_RESOURCE_P, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_ELENG, "ELENG", 2, Operand::LOWER, "p:write,vf", Operand::EFU, VU_PIPE_LOWER, VU_EXEC_EFU, 17, 18, VU_INSTR_WRITES_P, VU_RESOURCE_NONE, VU_RESOURCE_P, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_ERLENG, "ERLENG", 2, Operand::LOWER, "p:write,vf", Operand::EFU, VU_PIPE_LOWER, VU_EXEC_EFU, 23, 24, VU_INSTR_WRITES_P, VU_RESOURCE_NONE, VU_RESOURCE_P, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_EATANXY, "EATANxy", 2, Operand::LOWER, "p:write,vf", Operand::EFU, VU_PIPE_LOWER, VU_EXEC_EFU, 53, 54, VU_INSTR_WRITES_P, VU_RESOURCE_NONE, VU_RESOURCE_P, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_EATANXZ, "EATANxz", 2, Operand::LOWER, "p:write,vf", Operand::EFU, VU_PIPE_LOWER, VU_EXEC_EFU, 53, 54, VU_INSTR_WRITES_P, VU_RESOURCE_NONE, VU_RESOURCE_P, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_ESUM, "ESUM", 2, Operand::LOWER, "p:write,vf", Operand::EFU, VU_PIPE_LOWER, VU_EXEC_EFU, 11, 12, VU_INSTR_WRITES_P, VU_RESOURCE_NONE, VU_RESOURCE_P, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_ERCPR, "ERCPR", 2, Operand::LOWER, "p:write,vf", Operand::EFU, VU_PIPE_LOWER, VU_EXEC_EFU, 11, 12, VU_INSTR_WRITES_P, VU_RESOURCE_NONE, VU_RESOURCE_P, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_ERSQRT, "ERSQRT", 2, Operand::LOWER, "p:write,vf:flag", Operand::EFU, VU_PIPE_LOWER, VU_EXEC_EFU, 17, 18, VU_INSTR_WRITES_P, VU_RESOURCE_NONE, VU_RESOURCE_P, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_ESIN, "ESIN", 2, Operand::LOWER, "p:write,vf:flag", Operand::EFU, VU_PIPE_LOWER, VU_EXEC_EFU, 28, 29, VU_INSTR_WRITES_P, VU_RESOURCE_NONE, VU_RESOURCE_P, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_EATAN, "EATAN", 2, Operand::LOWER, "p:write,vf:flag", Operand::EFU, VU_PIPE_LOWER, VU_EXEC_EFU, 53, 54, VU_INSTR_WRITES_P, VU_RESOURCE_NONE, VU_RESOURCE_P, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_EEXP, "EEXP", 2, Operand::LOWER, "p:write,vf:flag", Operand::EFU, VU_PIPE_LOWER, VU_EXEC_EFU, 43, 44, VU_INSTR_WRITES_P, VU_RESOURCE_NONE, VU_RESOURCE_P, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),

		VU_INFO(VU_OP_XGKICK, "XGKICK", 1, Operand::LOWER, "vi", Operand::INVALID, VU_PIPE_LOWER, VU_EXEC_UNKNOWN, 1, 1, VU_INSTR_XGKICK, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_XGKICK, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_XTOP, "XTOP", 1, Operand::LOWER, "vi:write", Operand::INVALID, VU_PIPE_LOWER, VU_EXEC_UNKNOWN, 1, 1, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),
		VU_INFO(VU_OP_XITOP, "XITOP", 1, Operand::LOWER, "vi:write", Operand::INVALID, VU_PIPE_LOWER, VU_EXEC_UNKNOWN, 1, 1, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE),

		VU_INFO(VU_OP_INVALID, 0, 0, 0, "", Operand::INVALID, VU_PIPE_UNKNOWN, VU_EXEC_UNKNOWN, 0, 0, VU_INSTR_NONE, VU_RESOURCE_NONE, VU_RESOURCE_NONE, VU_MEMORY_NONE, VU_MEMORY_FLAG_NONE, 0, VU_BYPASS_NONE)
	};

#undef VU_ACC_BC_ACC_READ
#undef VU_ACC_ACC_READS
#undef VU_ACC_ACC_READ
#undef VU_ACC_READS
#undef VU_ACC_BC
#undef VU_ACC_DEST
#undef VU_UPPER_BC_ACC_READ
#undef VU_UPPER_DEST_ACC_READS
#undef VU_UPPER_DEST_ACC_READ
#undef VU_UPPER_DEST_READS
#undef VU_UPPER_BC
#undef VU_UPPER_DEST
#undef VU_INFO

	std::string lower( const std::string& text )
	{
		std::string result = text;
		for( std::string::iterator i = result.begin(); i != result.end(); ++i )
			*i = static_cast<char>( std::tolower( static_cast<unsigned char>( *i ) ) );
		return result;
	}

	bool startsWith( const std::string& text, const char* prefix )
	{
		const std::string prefixText( prefix );
		return text.size() >= prefixText.size() && text.compare( 0, prefixText.size(), prefixText ) == 0;
	}

	bool hasBroadcastSuffix( const std::string& mnemonic )
	{
		if( mnemonic.size() <= 1 )
			return false;
		const char suffix = mnemonic[mnemonic.size() - 1];
		return suffix == 'x' || suffix == 'y' || suffix == 'z' || suffix == 'w';
	}

	bool patternHasTag( const std::string& pattern, const char* tag )
	{
		std::string::size_type pos = pattern.find( ':' );
		while( pos != std::string::npos )
		{
			std::string::size_type tagStart = pos + 1;
			std::string::size_type tagEnd = pattern.find( ':', tagStart );
			std::string current = pattern.substr( tagStart, tagEnd == std::string::npos
			                                                ? std::string::npos
			                                                : tagEnd - tagStart );
			if( current == tag )
				return true;
			pos = tagEnd;
		}
		return false;
	}

	void appendParameterNote( std::string& notes, const char* note )
	{
		if( !notes.empty() )
			notes += ",";
		notes += note;
	}

	std::string summarizeOperandAlternative( const std::string& pattern )
	{
		std::string::size_type tag = pattern.find( ':' );
		std::string base = tag == std::string::npos ? pattern : pattern.substr( 0, tag );
		std::string notes;

		if( base == "imm" && patternHasTag( pattern, "branch" ) )
			return "label";

		if( patternHasTag( pattern, "dest" ) )
			appendParameterNote( notes, "dest" );
		if( patternHasTag( pattern, "write" ) )
			appendParameterNote( notes, "write" );
		if( patternHasTag( pattern, "bc" ) )
			appendParameterNote( notes, "bc" );
		if( patternHasTag( pattern, "flag" ) )
			appendParameterNote( notes, "component" );
		if( patternHasTag( pattern, "wcomp" ) )
			appendParameterNote( notes, "w" );
		if( patternHasTag( pattern, "predec" ) )
			appendParameterNote( notes, "predec" );
		if( patternHasTag( pattern, "postinc" ) )
			appendParameterNote( notes, "postinc" );
		if( patternHasTag( pattern, "address" ) )
			appendParameterNote( notes, "address" );
		if( patternHasTag( pattern, "branch" ) )
			appendParameterNote( notes, "branch" );
		if( patternHasTag( pattern, "rotate" ) )
			appendParameterNote( notes, "rotate" );
		if( patternHasTag( pattern, "raw" ) )
			appendParameterNote( notes, "raw" );

		if( notes.empty() )
			return base;
		return base + "[" + notes + "]";
	}

	std::string summarizeOperandPattern( const std::string& pattern )
	{
		std::string result;
		std::string::size_type start = 0;
		while( start <= pattern.size() )
		{
			std::string::size_type end = pattern.find( '|', start );
			std::string part = pattern.substr( start, end == std::string::npos
			                                         ? std::string::npos
			                                         : end - start );
			if( !result.empty() )
				result += "|";
			result += summarizeOperandAlternative( part );
			if( end == std::string::npos )
				break;
			start = end + 1;
		}
		return result;
	}
}

const char* vuInstructionMnemonic( VuInstructionOpcode opcode )
{
	for( const VuInstructionOpcodeInfo* i = kInstructionOpcodes; i->mnemonic; ++i )
	{
		if( i->opcode == opcode )
			return i->mnemonic;
	}
	return 0;
}

VuInstructionOpcode vuInstructionOpcodeForMnemonic( const std::string& mnemonic )
{
	const std::string lowered = lower( mnemonic );
	for( const VuInstructionOpcodeInfo* i = kInstructionOpcodes; i->mnemonic; ++i )
	{
		if( lowered == i->mnemonic )
			return i->opcode;
	}
	return VU_OP_INVALID;
}

const VuInstructionInfo* allVuInstructionInfos()
{
	return kInstructions;
}

const VuInstructionInfo* findVuInstructionInfo( const std::string& mnemonic )
{
	VuInstructionOpcode opcode = vuInstructionOpcodeForMnemonic( mnemonic );
	if( opcode == VU_OP_INVALID )
		return 0;

	for( const VuInstructionInfo* info = kInstructions; info->mnemonic; ++info )
	{
		if( opcode == info->opcode )
			return info;
	}
	return 0;
}

bool isKnownVuInstruction( const std::string& mnemonic )
{
	return findVuInstructionInfo( mnemonic ) != 0;
}

std::string normalizeVuMnemonic( const std::string& word )
{
	std::string mnemonic = lower( word );

	std::string::size_type flag = mnemonic.find( '[' );
	if( flag != std::string::npos )
		mnemonic = mnemonic.substr( 0, flag );

	std::string::size_type fields = mnemonic.find( '.' );
	if( fields != std::string::npos )
		mnemonic = mnemonic.substr( 0, fields );

	if( isKnownVuInstruction( mnemonic ) )
		return mnemonic;

	if( hasBroadcastSuffix( mnemonic ) )
	{
		std::string base = mnemonic.substr( 0, mnemonic.size() - 1 );
		const VuInstructionInfo* info = findVuInstructionInfo( base );
		if( info && info->pipe == VU_PIPE_UPPER )
			return base;
	}

	return mnemonic;
}

std::string vuInstructionParameterSummary( const VuInstructionInfo& info )
{
	if( !info.operandPattern || !info.operandPattern[0] )
		return "";

	std::string result;
	std::string pattern( info.operandPattern );
	std::string::size_type start = 0;
	while( start <= pattern.size() )
	{
		std::string::size_type end = pattern.find( ',', start );
		std::string part = pattern.substr( start, end == std::string::npos
		                                         ? std::string::npos
		                                         : end - start );
		if( !result.empty() )
			result += ", ";
		result += summarizeOperandPattern( part );
		if( end == std::string::npos )
			break;
		start = end + 1;
	}
	return result;
}

const char* vuInstructionDescription( const VuInstructionInfo& info )
{
	if( !info.mnemonic )
		return "";

	const std::string mnemonic = lower( info.mnemonic );
	const std::string operand = info.operandName ? lower( info.operandName ) : mnemonic;

	if( info.opcode == VU_OP_NOP )
		return "No operation.";
	if( info.memoryKind == VU_MEMORY_LOAD )
		return "Load data from VU memory into a vector or integer register.";
	if( info.memoryKind == VU_MEMORY_STORE )
		return "Store a vector or integer register to VU memory.";
	if( info.memoryKind == VU_MEMORY_XGKICK )
		return "Start a GIF transfer from VU memory.";
	if( info.flags & VU_INSTR_WAIT_Q )
		return "Wait until the Q result is available.";
	if( info.flags & VU_INSTR_WAIT_P )
		return "Wait until the P result is available.";
	if( info.flags & VU_INSTR_BRANCH )
	{
		if( info.flags & VU_INSTR_UNCONDITIONAL_BRANCH )
			return "Branch after one delay slot.";
		return "Branch after one delay slot when the integer condition is met.";
	}
	if( (info.flags & VU_INSTR_WRITES_Q) != 0 )
		return "Start a Q scalar divide/square-root operation.";
	if( (info.flags & VU_INSTR_WRITES_P) != 0 )
		return "Start an elementary-function operation that writes P.";
	if( info.opcode == VU_OP_LOI )
		return "Load an immediate scalar into the I register.";
	if( startsWith( operand, "ftoi" ) )
		return "Convert floating-point vector fields to fixed-point integer fields.";
	if( startsWith( operand, "itof" ) )
		return "Convert fixed-point integer vector fields to floating-point fields.";
	if( startsWith( operand, "clip" ) )
		return "Update MAC and CLIP flags from clip-space bounds.";
	if( startsWith( operand, "madd" ) )
		return "Multiply vector fields and add ACC.";
	if( startsWith( operand, "msub" ) )
		return "Multiply vector fields and subtract from ACC.";
	if( startsWith( operand, "mul" ) )
		return "Multiply vector fields.";
	if( startsWith( operand, "add" ) )
		return "Add vector fields.";
	if( startsWith( operand, "sub" ) )
		return "Subtract vector fields.";
	if( startsWith( operand, "max" ) )
		return "Take the per-field vector maximum.";
	if( startsWith( operand, "mini" ) )
		return "Take the per-field vector minimum.";
	if( startsWith( operand, "opm" ) )
		return "Perform an outer-product vector operation.";
	if( startsWith( operand, "i" ) && info.unit == VU_EXEC_IALU )
		return "Perform an integer ALU operation on VI registers or immediates.";
	if( startsWith( operand, "f" ) && info.unit == VU_EXEC_IALU )
		return "Read or update VU status flags.";
	if( startsWith( operand, "r" ) && info.unit == VU_EXEC_RANDU )
		return "Access the VU random-number unit.";
	if( info.opcode == VU_OP_MOVE )
		return "Copy vector register fields.";
	if( info.opcode == VU_OP_MFIR )
		return "Move an integer register value into vector fields.";
	if( info.opcode == VU_OP_MTIR )
		return "Move a vector field into an integer register.";
	if( info.opcode == VU_OP_MR32 )
		return "Rotate vector register fields.";
	if( info.opcode == VU_OP_XTOP || info.opcode == VU_OP_XITOP )
		return "Read the VIF TOP/ITOP register.";
	if( info.unit == VU_EXEC_EFU )
		return "Execute an elementary-function unit operation.";
	if( info.pipe == VU_PIPE_UPPER )
		return "Execute an upper-pipe vector operation.";
	if( info.pipe == VU_PIPE_LOWER )
		return "Execute a lower-pipe VU operation.";
	return "VU instruction.";
}

}
