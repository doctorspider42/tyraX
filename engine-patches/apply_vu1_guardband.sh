#!/bin/sh
# tyra-editor engine patch v3: guard-band clip check in the VU1 cull programs.
set -e
F=/tyra/engine/src/renderer/3d/pipeline/shared/vcl_sml.i

if grep -q "tyra-editor guard band" "$F"; then
  echo "vu1 guard band already applied"
  exit 0
fi

awk '
/#macro PerformClipCheck/ {
  print "#macro PerformClipCheck: t_vertex, t_destAddress, t_destAddressOffset";
  print "   ; tyra-editor guard band: accept XY up to 3x outside the clip";
  print "   ; volume - the GS scissor trims the pixels, so edge-crossing";
  print "   ; triangles render correctly without geometric clipping.";
  print "   loi         0.3333333";
  print "   muli.xy     scaledClipVtx, t_vertex, i";
  print "   move.zw     scaledClipVtx, t_vertex";
  print "   clipw.xyz   scaledClipVtx, scaledClipVtx";
  print "   fcand       VI01,       0x3FFFF";
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
echo "vu1 guard band applied"
