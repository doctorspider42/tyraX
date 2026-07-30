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

include ../Makefile.base

# Modified by TyraX: `| directories` order-only prerequisite - this rule
# overrides Makefile.base's link rule, so it needs the same -j guard (bin/ is
# created by `directories`, which runs under the parallel `resources` goal).
$(TARGET): $(OBJECTS) $(VCL_OBJECTS) $(VU_OBJECTS_VCL) $(VU_OBJECTS_VSM) $(IRXEM_OBJECTS) | directories
	$(AR) rcs $(TARGETDIR)/$(TARGET) $(OBJECTS) $(VCL_OBJECTS) $(VU_OBJECTS_VCL) $(VU_OBJECTS_VSM) $(IRXEM_OBJECTS)
