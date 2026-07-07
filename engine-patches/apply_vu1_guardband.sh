#!/bin/sh
# tyra-editor engine patch: guard-band clip check in the VU1 cull programs (v2).
# v2 fixes a bug where vertices BEHIND the camera (w <= 0) passed the widened
# XY test: the original 1x window rejected them by accident, the 3x window
# does not, and the marginal |z| <= |w| test drowns in float precision. The
# perspective divide by a negative w then mirrored such triangles into giant
# polygons smeared across the screen. An explicit w-sign test (STATUS flags,
# same idiom as the sml library's BGTZ macros) rejects them robustly.
set -e
F=/tyra/engine/src/renderer/3d/pipeline/shared/vcl_sml.i

if grep -q "tyra-editor guard band v2" "$F"; then
  echo "vu1 guard band v2 already applied"
  exit 0
fi

awk '
/#macro PerformClipCheck/ {
  print "#macro PerformClipCheck: t_vertex, t_destAddress, t_destAddressOffset";
  print "   ; tyra-editor guard band v2: accept XY up to 3x outside the clip";
  print "   ; volume - the GS scissor trims the pixels, so edge-crossing";
  print "   ; triangles render correctly without geometric clipping.";
  print "   ; Vertices behind the camera (w <= 0) are rejected explicitly:";
  print "   ; the widened XY window no longer catches them and the plain";
  print "   ; |z| <= |w| test is borderline in single precision.";
  print "   loi         0.3333333";
  print "   muli.xy     scaledClipVtx, t_vertex, i";
  print "   move.zw     scaledClipVtx, t_vertex";
  print "   mul.w       vclsmlftemp, t_vertex, vf00   ; STATUS sign/zero of w";
  print "   clipw.xyz   scaledClipVtx, scaledClipVtx";
  print "   fcand       VI01,       0x3FFFF";
  print "   fsand       vclsmlitemp, 0x3              ; NEG | ZERO -> w <= 0";
  print "   ior         VI01,       VI01, vclsmlitemp";
  print "   iaddiu      adcBit,     VI01, 0x7FFF";
  print "   isw.w       adcBit,     t_destAddressOffset(t_destAddress)";
  skip = 1; next
}
skip && /#endmacro/ { print "#endmacro"; skip = 0; next }
skip { next }
{ print }
' "$F" > "$F.new"
mv "$F.new" "$F"

# VU1 microprograms are not covered by the C++ dependency tracking -
# force their rebuild.
find /tyra/engine/obj -name "*vu1.o*" -delete 2>/dev/null || true
echo "vu1 guard band v2 applied"
