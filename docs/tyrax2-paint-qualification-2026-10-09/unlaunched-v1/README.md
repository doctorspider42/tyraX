# A13 V1 unlaunched trace-enabled build

The first lean qualification build removed the heavy paint oracle and forced
LOD/fallback modes, but source audit found TYRA_HARDWARE_TRACE=1 inherited from
current engine staging. The runtime trace was not armed; this is not a measured
overhead claim. The build was bound but never launched or deployed. V2 explicitly
compiles trace and frame profiling out and checks the flags in its analyzer.

This folder preserves the complete 470-source native binding, ELF and build
metadata. No emulator/physical results exist for V1. The active candidate and
stage instructions are in the parent archive, not this rejected build.
