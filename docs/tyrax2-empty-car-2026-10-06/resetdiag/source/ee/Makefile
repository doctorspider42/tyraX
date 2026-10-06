EE_BIN  = ps2link.elf
EE_OBJS = ps2link.o cmdHandler.o excepHandler.o exceptions.o
EE_INCS += -I../include

# This is for the sbv patch
EE_LIBS += -lpatches -ldebug

# IRX libs
IRX_FILES += ioptrap.irx ps2dev9.irx poweroff.irx
IRX_FILES += ps2ip.irx netman.irx smap.irx udptty.irx
IRX_FILES += ps2link.irx
# TyraX: bake in the USB HID stack (usbd + keyboard + mouse). These ship
# prebuilt in $(PS2SDK)/iop/irx/ and are picked up by the generic %_irx.c rule.
# NOUSB=1 leaves them out: they are ~50 KB of the image, and at the low link
# address that tail is the difference between booting from FreeMcBoot's menu
# and a black screen. Nothing else in ps2link depends on them.
NOUSB ?= 0
ifneq ($(NOUSB),1)
IRX_FILES += usbd.irx ps2kbd.irx ps2mouse.irx
EE_CFLAGS += -DTYRAX_USB_HID
endif
EE_OBJS += $(IRX_FILES:.irx=_irx.o) ps2ip_nm_irx.o

# Compile with -Werror
EE_CFLAGS += -Werror

# This is to enable the debug mode into ps2link
ifeq ($(DEBUG),1)
EE_CFLAGS += -DDEBUG -g
endif
EE_CFLAGS += -Os
EE_CFLAGS += -fdata-sections -ffunction-sections

ifeq ($(DEBUG),1)
EE_LDFLAGS += -g
else
EE_LDFLAGS += -s
endif
EE_LDFLAGS += -Wl,--gc-sections
EE_LDFLAGS += -Wl,-Map,ps2link.map

# This is to read the closest tag version
APP_VERSION := r7-resetdiag
EE_CFLAGS += -DAPP_VERSION=\"$(APP_VERSION)\"

# Use NEWLIB NANO for making smaller binaries
NEWLIB_NANO = 1

# Use custom linkfile
ifeq ($(LOADHIGH),1)
EE_LINKFILE = linkfile.loadhigh
else
EE_LINKFILE = linkfile
endif

all: $(EE_BIN)

clean:
	-rm -f $(EE_OBJS) $(EE_BIN)

# IRX files
# Special rule for ps2ip-nm.irx becasue - aren't valid
ps2ip_nm_irx.c:
	$(PS2SDK)/bin/bin2c $(PS2SDK)/iop/irx/ps2ip-nm.irx $@ ps2ip_nm_irx
# Special rule for local ps2link.irx
ps2link_irx.c:
	$(PS2SDK)/bin/bin2c ../iop/ps2link.irx $@ ps2link_irx
%_irx.c:
	$(PS2SDK)/bin/bin2c $(PS2SDK)/iop/irx/$*.irx $@ $*_irx


include $(PS2SDK)/Defs.make
include $(PS2SDK)/samples/Makefile.eeglobal_cpp
