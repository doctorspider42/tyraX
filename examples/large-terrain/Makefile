TARGET      := large-terrain.elf
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
CFLAGS      :=
LIB         := -ltyra
LIBDIRS     := -L$(ENGINEDIR)/bin
INC         := -I$(INCDIR) -I$(ENGINEDIR)/inc
INCDEP      := -I$(INCDIR) -I$(ENGINEDIR)/inc

include /tyra/Makefile.base
