#!/bin/sh
set -eu
TASKROOT=/mnt/f/Projects/tyrax2-lab-20261001/ps2link-reset-breadcrumbs-v1
export PS2DEV=/home/spider/.cache/tyrax/native/toolchains/eff2c8918589264d49aa9a0f
export PS2SDK=$PS2DEV/ps2sdk
export GSKIT=$PS2DEV/gsKit
export PATH=$PS2SDK/bin:$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PATH
cd "$TASKROOT/source"
make NOUSB=1 LOADHIGH=1 DEBUG=0 clean > "$TASKROOT/clean-before-final.log" 2>&1
make -j6 NOUSB=1 LOADHIGH=0 DEBUG=0 ee > "$TASKROOT/build-low.log" 2>&1
cp ee/ps2link.elf "$TASKROOT/ps2link-r7-resetdiag-low.elf"
cp ee/ps2link.map "$TASKROOT/link-low.map"
(cd "$PS2DEV/share/ps2-packer/module" && ps2-packer -s "$PS2DEV/share/ps2-packer/stub/lzma-1d00-stub" "$TASKROOT/source/ee/ps2link.elf" "$TASKROOT/ps2link-r7-resetdiag-low-packed.elf") > "$TASKROOT/pack-low.log" 2>&1
# Separate clean variant: never reuse objects built for a different linker address.
make NOUSB=1 LOADHIGH=0 DEBUG=0 clean > "$TASKROOT/clean-between-variants.log" 2>&1
make -j6 NOUSB=1 LOADHIGH=1 DEBUG=0 ee > "$TASKROOT/build-high.log" 2>&1
cp ee/ps2link.elf "$TASKROOT/ps2link-r7-resetdiag-high.elf"
cp ee/ps2link.map "$TASKROOT/link-high.map"
