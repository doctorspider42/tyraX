# Imported by make_arm.py for --camcheck: every frame, compare the chase
# camera's list sweep with the general sweepSphere and count disagreements.
def apply(proj, edit):
    import pathlib
    p = proj / 'src/gen/game_vehicles.gen.cpp'
    t = p.read_text(encoding='utf-8-sig')
    a = '  vehColFresh_ = false;\n'
    assert t.count(a) == 1, t.count(a)
    b = ('  { static unsigned cldCamN = 0, cldCamBad = 0; ++cldCamN;\n'
         '    const float cldRef = sweepSphere(px, py, pz, dx / vehCamWanted_, dy / vehCamWanted_, dz / vehCamWanted_, vehCamWanted_, CAM_RADIUS, v.object);\n'
         '    const float cldD = cldRef - vehCamAllowed_;\n'
         '    if (cldD > 0.0001F || cldD < -0.0001F) { if (++cldCamBad < 20) printf("CAMCHECK diff ref=%d list=%d\\n", (int)(cldRef * 10000), (int)(vehCamAllowed_ * 10000)); }\n'
         '    if ((cldCamN % 1800) == 0) { printf("CAMCHECK n=%u bad=%u\\n", cldCamN, cldCamBad); fflush(stdout); } }\n')
    t = t.replace(a, a + b)
    p.write_bytes(t.encode())
