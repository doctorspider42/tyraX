"""rn_patch.py <engine-root-copy> <bench|district> - after make_arm.py /
make_engine_probe.py: the StaPip flush laps and the renderNow ceiling probe.

- Cld::rnSkip (new) and scopes "sp.flush" (StaPipCore::flushDeferredRun),
  "sp.cross" (crossClassify), "sp.pass" (the stamp pass call).
- renderNow returns at entry while the probe is on: every draw that neither
  the stamp pass nor the crossing pass took (and every draw outside a
  recorded flush) is dropped. No new stamps or crossing records are captured
  while it is on, so it is an upper bound.
  bench:    on while Cld::rnSkip (the game sets it from the drive start).
  district: on in arm 1 drive windows (Cld::arm == 1 && Cld::state == 2).
"""
import sys, pathlib
eng = pathlib.Path(sys.argv[1]) / "engine"
mode = sys.argv[2]


def edit(p, old, new, count=1):
    t = p.read_text(encoding="utf-8")
    n = t.count(old)
    assert n == count, (str(p), old[:70], n)
    p.write_text(t.replace(old, new), encoding="utf-8", newline="\n")


h = eng / "inc/debug/cld_probe.hpp"
edit(h, "inline int state = 0, cycle = 0, arm = 0;\n",
     "inline int state = 0, cycle = 0, arm = 0;\ninline bool rnSkip = false;  // rn_patch.py\n")
c = eng / "src/renderer/3d/pipeline/static/core/stapip_core.cpp"
cond = {"bench": "Cld::rnSkip",
        "district": "(Cld::arm == 1 && Cld::state == 2)",
        "district-alt": "(Cld::arm == 1 && Cld::state == 2 && (Cld::frame & 1))"}[mode]
if mode in ("bench", "district-alt"):  # per-frame work series (CLDW lines)
    t = h.read_text(encoding="utf-8")
    if "#define CLD_SERIES" not in t:
        h.write_text("#define CLD_SERIES 1\n" + t, encoding="utf-8", newline="\n")
edit(c, '  Cld::S cldCore("core");\n',
     '  if (' + cond + ') return;  // rn_patch.py: renderNow ceiling probe\n  Cld::S cldCore("core");\n')
edit(c, "void StaPipCore::flushDeferredRun() {\n#if TYRA_STAPIP_STAMP\n",
     'void StaPipCore::flushDeferredRun() {\n  Cld::S cldFlush("sp.flush");\n#if TYRA_STAPIP_STAMP\n')
edit(c, "        crossClassify(recorded.data(), (u32)recorded.size(), planAt);\n",
     '        { Cld::S cldX("sp.cross"); crossClassify(recorded.data(), (u32)recorded.size(), planAt); }\n')
edit(c, "      qbufferRenderer.stampPass<false>(passList, (u32)recorded.size(), pass, 0,\n                                       planAt);\n",
     '      { Cld::S cldP("sp.pass"); qbufferRenderer.stampPass<false>(passList, (u32)recorded.size(), pass, 0,\n                                       planAt); }\n')
print("rn probe in", mode)
