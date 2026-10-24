# Imported by make_arm.py for --os-engine / --os-game: -Os for one half only.
# Appended after the Makefile's include of Makefile.base, so it overrides -O3.
# src/math stays -O3 under --os-engine: math at -Os makes the rig die with an
# IOP jump to a float PC ~240 frames into its first drive (2026-10-15, cause
# unknown, see the engine skill's pitfalls).
def apply(eng, proj, argv):
    if '--os-engine' in argv:
        p = eng / 'Makefile'
        p.write_bytes(p.read_bytes() + b'\nCFLAGS += -Os\n'
                      b'$(filter obj/math/%,$(OBJECTS)): CFLAGS += -O3\n')
        print('engine -Os (math -O3)')
    if '--os-game' in argv:
        p = proj / 'Makefile'
        p.write_bytes(p.read_bytes() + b'\nCFLAGS += -Os\n')
        print('game -Os')
