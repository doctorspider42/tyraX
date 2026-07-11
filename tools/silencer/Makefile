# Built inside the h4570/tyra toolchain container (PS2SDK). The editor's
# "Stop on PS2" execee's the resulting silencer.elf after resetting ps2link
# to key off the SPU2 voices the reset alone cannot touch.
EE_BIN = silencer.elf
EE_OBJS = main.o libsd_irx.o audsrv_irx.o
EE_LIBS = -laudsrv -lpatches

all: $(EE_BIN)

%_irx.s:
	bin2s $(PS2SDK)/iop/irx/$*.irx $@ $*_irx

%_irx.o: %_irx.s
	$(EE_AS) $(EE_ASFLAGS) $< -o $@

clean:
	rm -f $(EE_BIN) $(EE_OBJS) libsd_irx.s audsrv_irx.s

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
