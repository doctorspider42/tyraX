TARGET      := iop-compute.elf
ENGINEDIR	:= /tyra/engine

#The Directories, Source, Includes, Objects, Binary and Resources
SRCDIR      := src
INCDIR      := inc
BUILDDIR    := obj
TARGETDIR   := bin
# .res-baked is the texture-quantized mirror of res/ the editor writes at the
# start of every build (see Project > Preferences > Textures); sources in
# res/ stay full quality.
RESDIR      := .res-baked
SRCEXT      := cpp
VSMEXT		:= vsm
VCLEXT		:= vcl
VCLPPEXT	:= vclpp
DEPEXT      := d
OBJEXT      := o

#Flags, Libraries and Includes
# Debug builds carry -g (for the crash reporter's addr2line lookups) and link
# ps2sdk's libeedebug, which the engine's crash handler uses to hook the EE
# exception vectors. Release builds carry neither, and KEEPSYM off means no
# unstripped copy of the ELF is kept - see docs/devkit.md.
CFLAGS      := -g
KEEPSYM     := 1
LIB         := -ltyra -leedebug
LIBDIRS     := -L$(ENGINEDIR)/bin
INC         := -I$(INCDIR) -I$(ENGINEDIR)/inc
INCDEP      := -I$(INCDIR) -I$(ENGINEDIR)/inc

include /tyra/Makefile.base

#--- IOP co-processor module (Project > Preferences > Build; docs/iop-compute.md)
# AFTER the include, and that is not cosmetic: GNU make takes the FIRST target
# it sees as the default goal, so with this block above the include the
# dependency line below became the default goal and `make` stopped building the
# ELF at all - it answered "obj/gen/txiop.o is up to date" and exited 0.
# Built with the IOP toolchain in the same container as the game, then embedded
# into the ELF by the .irx-em rule in Makefile.base (bin2s). obj/gen/txiop.o is
# that embedding step, so it is what has to wait for the module.
#
# The built .irx lands in $(BUILDDIR), NOT in iop/: the editor's Runner rsyncs
# the project sources into the container and only syncs bin/ back, so a build
# artefact sitting in the source tree is one the next sync can wipe - which
# would quietly rebuild the module on every build.
IOPCC       := mipsel-ps2-irx-gcc
IOPDIR      := iop
# -fno-builtin: the IOP kernel is not libc and GCC must not assume it is.
# -msoft-float / -mno-explicit-relocs: required by the IOP ABI - the latter
# because the kernel's loader cannot deal with the multiple LO relocs newer
# GCC emits after one HI reloc.
IOP_CFLAGS  := -D_IOP -fno-builtin -G0 -O2 -Wall -msoft-float \
               -mno-explicit-relocs -I$(IOPDIR) \
               -I$(PS2SDK)/iop/include -I$(PS2SDK)/common/include
IOP_MODOBJS := $(BUILDDIR)/iop/txiop.$(OBJEXT) \
               $(BUILDDIR)/iop/user_jobs.$(OBJEXT) \
               $(BUILDDIR)/iop/imports.$(OBJEXT)

$(BUILDDIR)/gen/txiop.$(OBJEXT): $(BUILDDIR)/iop/txiop.irx

$(BUILDDIR)/iop/txiop.irx: $(IOP_MODOBJS)
	$(IOPCC) -nostdlib -s -o $@ $^

# -fno-toplevel-reorder for the import table only: GCC's reordering breaks the
# table apart and the module then fails to resolve at load time.
$(BUILDDIR)/iop/imports.c: $(IOPDIR)/imports.lst
	@mkdir -p $(dir $@)
	@echo '#include "irx.h"' > $@
	@cat $< >> $@

$(BUILDDIR)/iop/imports.$(OBJEXT): $(BUILDDIR)/iop/imports.c
	@mkdir -p $(dir $@)
	$(IOPCC) $(IOP_CFLAGS) -fno-toplevel-reorder -c $< -o $@

$(BUILDDIR)/iop/%.$(OBJEXT): $(IOPDIR)/%.c $(IOPDIR)/txiop.h
	@mkdir -p $(dir $@)
	$(IOPCC) $(IOP_CFLAGS) -c $< -o $@
