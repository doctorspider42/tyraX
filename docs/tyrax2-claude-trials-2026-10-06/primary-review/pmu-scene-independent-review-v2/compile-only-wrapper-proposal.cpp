#include <stdint.h>
namespace NightPMU {using U=uint32_t;constexpr U stoppedPcr=0x000340d0U,activePcr=0x800340d0U;

// Pinned Sony EE manual + ps2dev/binutils-gdb provide syntax/encoding.
// Actual linked placement and safe enabled lifetime are separate root gates.
inline U readPcr(){U v;asm volatile(".set push\n.set noreorder\nmfps %0,0\nsync.p\n.set pop":"=r"(v)::"memory");return v;}
inline U readI(){U v;asm volatile(".set push\n.set noreorder\nmfpc %0,0\nsync.p\n.set pop":"=r"(v)::"memory");return v;}
inline U readD(){U v;asm volatile(".set push\n.set noreorder\nmfpc %0,1\nsync.p\n.set pop":"=r"(v)::"memory");return v;}
inline void configureStopped(){const U v=stoppedPcr;asm volatile(".set push\n.set noreorder\nmtps %0,0\nsync.p\n.set pop"::"r"(v):"memory");}
// Caller has just observed locally owned STOPPED state. Never reset enabled
// counters; no COP0 Count write, no interrupt masking and no consumer fence.
inline void resetEnable(){const U v=activePcr;asm volatile(".set push\n.set noreorder\nmtpc $0,0\nmtpc $0,1\nsync.p\nmtps %0,0\nsync.p\n.set pop"::"r"(v):"memory");}
inline void stopOwned(){const U v=stoppedPcr;asm volatile(".set push\n.set noreorder\nmtps %0,0\nsync.p\n.set pop"::"r"(v):"memory");}

}
extern "C" __attribute__((noinline,used)) uint32_t pmu_opcode_readPcr(){return NightPMU::readPcr();}
extern "C" __attribute__((noinline,used)) uint32_t pmu_opcode_readI(){return NightPMU::readI();}
extern "C" __attribute__((noinline,used)) uint32_t pmu_opcode_readD(){return NightPMU::readD();}
extern "C" __attribute__((noinline,used)) void pmu_opcode_configureStopped(){NightPMU::configureStopped();}
extern "C" __attribute__((noinline,used)) void pmu_opcode_resetEnable(){NightPMU::resetEnable();}
extern "C" __attribute__((noinline,used)) void pmu_opcode_stopOwned(){NightPMU::stopOwned();}
