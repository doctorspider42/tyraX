"""bench_patch.py <project> <skip 0|1> - laps for the headline benchmark
(frame-prep audit, "What is not attributed"), on a project already
instrumented by make_engine_probe.py (the engine copy has cld_probe.hpp).

- The scripted crash drive (patch_kf.py's) moved into the probe's first
  drive windows: Cld::frame 2100..2600 (windows 2100-2249, 2250-2399 =
  drive frames 0-300, the R12 window 250-450 is drive frames 0-200).
- The FTUPD update laps (UPD_LAP 0-9) become Cld scopes "u0".."u9" (ticks +
  PMU I/D misses), plus "u.settleB" / "u.settleT", "f.begin", "f.body",
  "f.end", and "scene" around renderScene.
- renderScene's cost rows (costStart / costEnd) become Cld scopes named by
  their labels ("r.Terrain", "r.Objects", ...).
- skip 1: Cld::rnSkip from the drive start (the renderNow ceiling probe; the
  engine half is rn_patch.py).
"""
import sys, os
root, skipArg = sys.argv[1], sys.argv[2]
skip = skipArg != "0"


def edit(rel, pairs):
    p = os.path.join(root, rel)
    s = open(p, encoding="utf-8").read()
    for a, b, c in pairs:
        n = s.count(a)
        assert n == c, (rel, a[:70], n)
        s = s.replace(a, b)
    if '#include "debug/cld_probe.hpp"' not in s:
        s = '#include "debug/cld_probe.hpp"\n' + s
    open(p, "w", encoding="utf-8", newline="\n").write(s)


drive = ("      if (Cld::frame > 2100 && Cld::frame < 2600) { inThrottle = 1.0F; inBrake = 0.0F; inNos = 1; }\n")
edit("src/gen/game_vehicles.gen.cpp", [
    ("      if (!vehSubStepRepeat_ && engine->pad.getClicked().DpadUp)",
     drive + "      if (!vehSubStepRepeat_ && engine->pad.getClicked().DpadUp)", 1)])

lapdef = """  // CLD laps (bench_patch.py): every UPD_LAP is a Cld scope.
  static const char* const kUpdN[10] = {"u0","u1","u2","u3","u4","u5","u6","u7","u8","u9"};
  Cld::U updT = Cld::ticks(), updI = Cld::pc0(), updD = Cld::pc1();
#define UPD_LAP(i) do { const Cld::U t_ = Cld::ticks(), i_ = Cld::pc0(), d_ = Cld::pc1(); \\
    Cld::scope(kUpdN[i], t_ - updT, i_ - updI, d_ - updD); updT = t_; updI = i_; updD = d_; } while (0)
#if 0
"""
edit("src/terrain_game.cpp", [
    ("  beamBatchCall = 0;  // the light-beam batch slots start over (see BeamBatch)\n#if TYRA_FRAME_PROFILE\n",
     "  beamBatchCall = 0;  // the light-beam batch slots start over (see BeamBatch)\n"
     + (("  Cld::rnSkip = Cld::frame >= 2100 && (Cld::frame & 1);  // renderNow ceiling probe, odd frames\n"
         if skipArg == "2" else "  Cld::rnSkip = Cld::frame >= 2100;  // renderNow ceiling probe\n")
        if skip else "")
     + lapdef, 1),
    ("#define UPD_LAP(i) ((void)0)\n#endif\n", "#endif\n", 1),
    ("  if (!menuActive) settleStaticBatches();\n  if (!menuActive) settleTerrainChunks();\n",
     "  { Cld::S cldS1(\"u.settleB\"); if (!menuActive) settleStaticBatches(); }\n"
     "  { Cld::S cldS2(\"u.settleT\"); if (!menuActive) settleTerrainChunks(); }\n", 1),
    ("  engine->renderer.beginFrame(CameraInfo3D(&cameraPosition, &cameraLookAt, &cameraUp));\n  {\n",
     "  { Cld::S cldB(\"f.begin\"); engine->renderer.beginFrame(CameraInfo3D(&cameraPosition, &cameraLookAt, &cameraUp)); }\n"
     "  Cld::S* cldBody = new (alloca(sizeof(Cld::S))) Cld::S(\"f.body\");\n  {\n", 1),
    ("    drawVideoConfirm(engine);\n  }\n  engine->renderer.endFrame();\n",
     "    drawVideoConfirm(engine);\n  }\n  cldBody->~S();\n"
     "  { Cld::S cldE(\"f.end\"); engine->renderer.endFrame(); }\n", 1),
])
if "#include <alloca.h>" not in open(os.path.join(root, "src/terrain_game.cpp")).read():
    edit("src/terrain_game.cpp", [("void TerrainGame::loop() {\n", "void TerrainGame::loop() {\n", 1)])
    p = os.path.join(root, "src/terrain_game.cpp")
    s = open(p, encoding="utf-8").read()
    open(p, "w", encoding="utf-8", newline="\n").write("#include <alloca.h>\n#include <new>\n" + s)

edit("src/gen/game_physics.gen.cpp", [
    ("void TerrainGame::renderScene() {\n",
     "void TerrainGame::renderScene() {\n  Cld::S cldScene(\"scene\");\n", 1),
    ("  auto costStart = [&]() -> u32 { return (costSeq || (Tyra::HardwareTrace::active && Tyra::HardwareTrace::detailed)) ? profTicks() : 0; };\n"
     "  auto costEnd = [&](const char* label, int object, u32 start) {\n",
     "  // CLD laps (bench_patch.py): cost rows are Cld scopes.\n"
     "  struct CldMark { Cld::U t, i, d; };\n"
     "  static CldMark cldMarks[4096];\n"
     "  static u32 cldMarkN = 0;\n"
     "  cldMarkN = 0;\n"
     "  auto costStart = [&]() -> u32 { const u32 k = cldMarkN < 4095 ? cldMarkN++ : 4095; cldMarks[k] = {Cld::ticks(), Cld::pc0(), Cld::pc1()}; return k; };\n"
     "  auto costEnd = [&](const char* label, int object, u32 start) {\n"
     "    { static char nb[64][24]; static const char* nk[64]; static int nn = 0; const char* nm = nullptr;\n"
     "      for (int q = 0; q < nn; ++q) if (nk[q] == label) { nm = nb[q]; break; }\n"
     "      if (!nm && nn < 64) { snprintf(nb[nn], 24, \"r.%s\", label); nk[nn] = label; nm = nb[nn++]; }\n"
     "      const CldMark& m0 = cldMarks[start];\n"
     "      if (nm) Cld::scope(nm, Cld::ticks() - m0.t, Cld::pc0() - m0.i, Cld::pc1() - m0.d); }\n"
     "    (void)object; return;\n", 1),
])
print("bench laps in", "skip" if skip else "")
