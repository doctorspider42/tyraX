TARGET		:= libtyra.a

#The Directories, Source, Includes, Objects, Binary and Resources
SRCDIR      := src
INCDIR      := inc
BUILDDIR    := obj
TARGETDIR   := bin
RESDIR      := res
SRCEXT      := cpp
VSMEXT		:= vsm
VCLEXT		:= vcl
VCLPPEXT	:= vclpp
DEPEXT      := d
OBJEXT      := o

#Flags, Libraries and Includes
CFLAGS      := 
LIB         := 
LIBDIRS     := 
INC         := -I$(INCDIR)
INCDEP      := -I$(INCDIR)

# Modified by TyraX: the shared rule archives .a targets and tracks bin/libtyra.a.
# A second recipe here used to emit overriding/ignoring-recipe warnings.
TYRA_MAKEFILE := ../Makefile.base
include $(TYRA_MAKEFILE)
