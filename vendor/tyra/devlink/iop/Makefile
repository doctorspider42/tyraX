# devlink.irx - see ../README.md. Built by ../build.sh / ../build.ps1 with
# the pinned PS2DEV v2.0.0 toolchain (the same one ps2link is built with, so
# the ps2ip import table matches the ps2ip ps2link keeps resident).

IOP_BIN  = devlink.irx
IOP_OBJS = devlink.o imports.o

IOP_CFLAGS += -Werror

all: $(IOP_BIN)

clean:
	-rm -f $(IOP_OBJS) $(IOP_BIN)
	-rm -rf obj

include $(PS2SDK)/Defs.make
include $(PS2SDK)/samples/Makefile.iopglobal
