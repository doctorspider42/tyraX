"""cen_patch.py <engine-root-copy> - census of the draws that reach
StaPipCore::renderNow (architecture loop round 15). After make_arm.py /
make_engine_probe.py.

- Every `goto general` of the stamp pass records its source line
  (Cld::cenRsn); a draw reaching renderNow from the pass carries it, any
  other renderNow call carries 0.
- renderNow counts each draw by (revision class, reason line, repeat), where
  repeat = the same bag reached renderNow in the previous frame too.
- Every 150 frames: CENS f=<frame> st=<state> frames=<n> followed by one
  CENR cls=<c> rsn=<line> rep=<0|1> per10=<draws a frame x10> row.
"""
import sys, pathlib, re
eng = pathlib.Path(sys.argv[1]) / "engine"


def edit(p, old, new, count=1):
    t = p.read_text(encoding="utf-8")
    n = t.count(old)
    assert n == count, (str(p), old[:70], n)
    p.write_text(t.replace(old, new), encoding="utf-8", newline="\n")


h = eng / "inc/debug/cld_probe.hpp"
edit(h, "inline int state = 0, cycle = 0, arm = 0;\n", """inline int state = 0, cycle = 0, arm = 0;
// cen_patch.py: the renderNow census
inline unsigned cenRsn = 0;
inline unsigned cenLines[48]; inline int cenLineN = 0;
inline unsigned cenTab[32][48][2];
inline const void* cenBag[2048]; inline unsigned cenLast[2048];
inline unsigned cenFrames = 0, cenPrevFrame = 0;
inline void cenDump() {
  printf("CENS f=%u st=%d frames=%u\\n", frame, state, cenFrames);
  for (int c = 0; c < 32; ++c)
    for (int l = 0; l < cenLineN; ++l)
      for (int r = 0; r < 2; ++r)
        if (cenTab[c][l][r]) {
          printf("CENR cls=%d rsn=%u rep=%d per10=%u\\n", c, cenLines[l], r,
                 cenTab[c][l][r] * 10 / (cenFrames ? cenFrames : 1));
          cenTab[c][l][r] = 0;
        }
  cenFrames = 0;
}
inline void cenCount(const void* bag, unsigned cls) {
  if (frame != cenPrevFrame) {
    cenPrevFrame = frame; ++cenFrames;
    if (frame % 150 == 0) cenDump();
  }
  const unsigned rsn = cenRsn; cenRsn = 0;
  int li = -1;
  for (int l = 0; l < cenLineN; ++l) if (cenLines[l] == rsn) { li = l; break; }
  if (li < 0) { if (cenLineN >= 48) return; li = cenLineN; cenLines[cenLineN++] = rsn; }
  const unsigned hsh = (static_cast<unsigned>(reinterpret_cast<unsigned long>(bag)) >> 4) * 2654435761u >> 21;
  unsigned k = hsh & 2047; int probe = 0;
  while (cenBag[k] != nullptr && cenBag[k] != bag && probe < 8) { k = (k + 1) & 2047; ++probe; }
  const bool rep = cenBag[k] == bag && cenLast[k] + 1 == frame;
  if (cenBag[k] == nullptr || cenBag[k] == bag || probe >= 8) { cenBag[k] = bag; cenLast[k] = frame; }
  cenTab[cls < 32 ? cls : 0][li][rep ? 1 : 0]++;
}
""")
c = eng / "src/renderer/3d/pipeline/static/core/stapip_core.cpp"
edit(c, '  Cld::S cldCore("core");\n',
     '  Cld::S cldCore("core");\n  Cld::cenCount(bag, bag->revisionClass | (bag->lighting ? 4u : 0u) | (bag->billboard ? 8u : 0u) | ((bag->texture && bag->texture->coordinatesAreNormals) ? 16u : 0u));  // cen_patch.py\n')
q = eng / "src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp"
t = q.read_text(encoding="utf-8")
if "cld_probe.hpp" not in t:
    t = '#include "debug/cld_probe.hpp"\n' + t
s = t.index("u32 StaPipQBufferRenderer::stampPass(const PassDraw* list, u32 n, PassContext& c,")
e = t.index("template u32 StaPipQBufferRenderer::stampPass<false>")
body = t[s:e]
n0 = body.count("goto general;")
body = body.replace("goto general;", "{ Cld::cenRsn = __LINE__; goto general; }")
t = t[:s] + body + t[e:]
q.write_text(t, encoding="utf-8", newline="\n")
print("census in,", n0, "general exits tagged")
