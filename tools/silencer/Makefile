# Built inside the h4570/tyra toolchain container (PS2SDK).
#
# MANUAL FALLBACK ONLY since ps2link r3, which silences the SPU2 itself on both
# sides of its reset (spu2Silence() in tools/ps2link/tyrax.patch). "Stop on PS2"
# no longer deploys this. Keep it for a console still running a pre-r3 ps2link:
#   ps2client -h <ip> execee host:silencer.elf     # cwd = tools/silencer
# See docs/ps2link-setup.md.
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
