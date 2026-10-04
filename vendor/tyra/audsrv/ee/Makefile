# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright 2005, ps2dev - http://www.ps2dev.org
# Licenced under GNU Library General Public License version 2
# Review ps2sdk README & LICENSE files for further details.

EE_OBJS = audsrv_rpc.o erl-support.o

# Modified by TyraX - not upstream PS2SDK.
#
# Which spelling of the SIF RPC entry points to emit (see src/sif-compat.h).
# It cannot be detected from the sources: this module is always compiled against
# the PINNED ps2sdk source tree, and what differs is the ps2sdk the result will
# be LINKED against - old images export SifCallRpc, current ones sceSifCallRpc,
# with no overlap. So the environment says, because only it knows.
#
# It goes here rather than on make's command line on purpose: Rules.make does
# `EE_CFLAGS := ... $(EE_INCS) $(EE_CFLAGS)`, and a command-line EE_CFLAGS
# overrides that whole assignment - include paths included, which fails as
# "compilation terminated" on a missing header rather than as a bad flag.
ifdef TYRAX_PS2SDK_SCE_SIF
EE_CFLAGS += -DTYRAX_PS2SDK_SCE_SIF=1
endif

include $(PS2SDKSRC)/Defs.make
include $(PS2SDKSRC)/ee/Rules.lib.make
include $(PS2SDKSRC)/ee/Rules.make
include $(PS2SDKSRC)/ee/Rules.release
