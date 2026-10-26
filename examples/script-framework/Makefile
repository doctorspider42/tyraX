TARGET      := script-framework.elf
ENGINEDIR	?= /tyra/engine
TYRA_MAKEFILE ?= /tyra/Makefile.base

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
# The C++ standard comes from Project > Preferences > Build & debug.
CFLAGS      := -std=gnu++20 -g
KEEPSYM     := 1
TYRAX_PROFILE := debug
LIB         := -ltyra -leedebug
LIBDIRS     := -L$(ENGINEDIR)/bin
INC         := -I$(INCDIR) -I$(ENGINEDIR)/inc
INCDEP      := -I$(INCDIR) -I$(ENGINEDIR)/inc

include $(TYRA_MAKEFILE)

# [[tyra::prop]] marks a script's public fields for the editor (docs/script-
# api.md); the compiler has nothing to do with it. GCC 13+ can ignore just that
# namespace, the GCC 11 of the default Docker image only every unknown one.
TYRAX_ATTR_FLAG := $(if $(shell $(CXX) -Wno-attributes=tyra:: -x c++ -fsyntax-only /dev/null 2>/dev/null && echo y),-Wno-attributes=tyra::,-Wno-attributes)
CFLAGS      += $(TYRAX_ATTR_FLAG)

# Extra flags (same Preferences page) go AFTER the engine's own -O3 -Wall, so
# an -O2 or a -Wno-... typed there wins. They reach this game's sources only;
# the engine library is shared by every project of the checkout.
# user.mk is yours: never generated, never overwritten. It is read last, so it
# can append to CFLAGS / LINKFLAGS / LIB / INC (branch on $(TYRAX_PROFILE) =
# debug | release) and add rules. Editing it rebuilds the game, like a flag
# change does.
-include user.mk
$(OBJECTS) $(OUTPUT): $(wildcard user.mk)
