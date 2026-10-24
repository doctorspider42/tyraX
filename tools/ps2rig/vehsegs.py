# Imported by make_arm.py for --vehsegs: phase scopes inside updateVehicles
# (game_vehicles.gen.cpp). Each mark charges the time since the previous mark
# to the segment that just ended, so an early `continue` in the car loop is
# charged to whatever phase it left from. Anchors are searched in order from
# the start of updateVehicles, so look-alikes elsewhere in the file are safe.
SEQ = [
    ('  if (!(TYRA_VEH_SUBSTEP_REUSE && vehSubStepRepeat_)) buildVehicleColliders();\n', '  CLDV("V.colliders");\n', 'before'),
    ('  for (int vi = 0; vi < vehicleCount_; ++vi) {\n', '  CLDV("V.useTarget");\n', 'before'),
    ('  for (int vi = 0; vi < vehicleCount_; ++vi) {\n', '    CLDV("V.car.pre");\n', 'after'),
    ('    // GATHER the nearby colliders ONCE per vehicle per frame - the trig', '    CLDV("V.car.gather");\n', 'before'),
    ("    // THE SPRUNG RIG - the host twin's exact arrangement (vehiclesim.cpp,", '    CLDV("V.car.rig");\n', 'before'),
    ("    // AXIS-SEPARATED, the host twin's structure exactly (vehiclesim::step -", '    CLDV("V.car.axis");\n', 'before'),
    ('    // PHYSICS BODIES: momentum, not a wall (docs/vehicles.md). Every body', '    CLDV("V.car.bodies");\n', 'before'),
    ('  for (int a = 0; a < vehicleCount_; ++a) {\n', '  CLDV("V.carcar");\n', 'before'),
    ('  static int aiLog = 0;', '  CLDV("V.tail");\n', 'before'),
    ('\n}\n', '\n  CLDV(nullptr);\n#undef CLDV', 'before'),
]


def apply(proj, edit):
    p = proj / 'src/gen/game_vehicles.gen.cpp'
    t = p.read_text(encoding='utf-8-sig')
    head = 'void TerrainGame::updateVehicles(float dt) {\n'
    assert t.count(head) == 1
    ins = ('  const char* cldSeg = "V.head"; unsigned cldT = Cld::ticks();\n'
           '#define CLDV(n) do { const unsigned cldNow = Cld::ticks(); '
           'if (cldSeg) Cld::scope(cldSeg, cldNow - cldT); cldSeg = (n); cldT = cldNow; } while (0)\n')
    pos = t.index(head) + len(head)
    t = t[:pos] + ins + t[pos:]
    pos += len(ins)
    last = None
    for anchor, text, where in SEQ:
        # 'after' re-uses the previous anchor position
        if where == 'after':
            i = last + len(anchor)
            t = t[:i] + text + t[i:]
            pos = i + len(text)
            continue
        i = t.index(anchor, pos)
        t = t[:i] + text + t[i:]
        last = i + len(text)
        pos = last + len(anchor)
    if '#include "debug/cld_probe.hpp"' not in t:
        t = '#include "debug/cld_probe.hpp"\n' + t
    p.write_bytes(t.encode())
