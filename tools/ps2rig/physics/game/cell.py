"""game/cell.py ARM - arm bit 256 (read straight from host:arm.txt, once,
because the batch list is built at scene load before loop() reads the arm):
static batch base cell x2."""
import sys, os
root = sys.argv[1]
p = os.path.join(root, 'src/gen/game_collision.gen.cpp')
s = open(p, encoding='utf-8').read()
a = '  const float baseCellW = mapW * 0.25F > 48.0F ? mapW * 0.25F : 48.0F;\n'
assert s.count(a) == 1
s = s.replace(a,
    '  static int cellArm_ = -1;\n'
    '  if (cellArm_ < 0) { cellArm_ = 0; FILE* af_ = fopen("host:arm.txt", "r"); if (af_) { if (fscanf(af_, "%d", &cellArm_) != 1) cellArm_ = 0; fclose(af_); } TYRA_LOG("CELLARM ", cellArm_); }\n'
    '  const float baseCellW = (mapW * 0.25F > 48.0F ? mapW * 0.25F : 48.0F) * ((cellArm_ & 256) ? 2.0F : 1.0F);\n')
if '#include <cstdio>' not in s:
    s = '#include <cstdio>\n' + s
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('cell ok')
