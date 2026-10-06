#include "debug/night_ablation.hpp"
extern "C" __attribute__((noinline,used)) uint32_t pmu_export_readPcr(){return NightPMU::readPcr();}
extern "C" __attribute__((noinline,used)) uint32_t pmu_export_readI(){return NightPMU::readI();}
extern "C" __attribute__((noinline,used)) uint32_t pmu_export_readD(){return NightPMU::readD();}
extern "C" __attribute__((noinline,used)) void pmu_export_configureStopped(){NightPMU::configureStopped();}
extern "C" __attribute__((noinline,used)) void pmu_export_resetEnable(){NightPMU::resetEnable();}
extern "C" __attribute__((noinline,used)) void pmu_export_stopOwned(){NightPMU::stopOwned();}
