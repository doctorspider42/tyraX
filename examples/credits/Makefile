TARGET      := credits.elf
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
