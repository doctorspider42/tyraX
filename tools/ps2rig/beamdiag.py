# Imported by make_arm.py for --beamdiag: once every 600 frames print how many
# light beams / dyn lights / pools / blobs the frame handles.
def apply(proj, edit):
    p = proj / 'src/gen/game_physics.gen.cpp'
    t = p.read_text(encoding='utf-8-sig')
    a = '  { const u32 ct=costStart(); updateAndRenderLightBeams(); costEnd("Light_beams",-1,ct); }\n'
    assert t.count(a) == 1
    b = ('  { static unsigned cldBd = 0; if ((++cldBd % 600) == 0) { int dyn = 0; for (const LightBeam& lb : lightBeams) '
         'if (lb.objIndex < (int)runtimeObjects.size() && runtimeObjects[lb.objIndex].data.lightDynamic) ++dyn;\n'
         '    printf("BEAMDIAG beams=%d dynBeams=%d dynLights=%d objs=%d\\n", (int)lightBeams.size(), dyn, (int)g_dynLights.size(), (int)runtimeObjects.size()); fflush(stdout); } }\n')
    t = t.replace(a, b + a)
    p.write_bytes(t.encode())
