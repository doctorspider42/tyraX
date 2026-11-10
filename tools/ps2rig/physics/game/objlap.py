"""game/objlap.py ARM - object-loop laps every frame into KFL slots 36..43:
36 impostor 37 geometry 38 draw-dist+coarse+occlusion 39 billboard+lod 40 pre 41 post 42 tested count
(x0.01 = count) 43 drawn count."""
import sys
root = sys.argv[1]
p = root + '/src/gen/game_physics.gen.cpp'
s = open(p, encoding='utf-8').read()
a = '  const bool lp = costSeq != 0;\n'
assert s.count(a) == 1
s = s.replace(a, '  const bool lp = true;\n')
a = '  if (lp) {\n    // Laps are EE time only and sit inside the Objects row;'
assert s.count(a) == 1
s = s.replace(a, '''  kf::labNow[36] += lpImpostor; kf::labNow[37] += lpGeometry; kf::labNow[38] += lpDraw + lpCoarse + lpOcclusion;
  kf::labNow[39] += lpBillboard + lpLod; kf::labNow[40] += lpPre; kf::labNow[41] += lpPost;
  kf::labNow[42] += lpNTested * 29U; kf::labNow[43] += lpNDrawn * 29U;
  if (costSeq != 0) {
    // Laps are EE time only and sit inside the Objects row;''')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
p = root + '/src/terrain_game.cpp'
s = open(p, encoding='utf-8').read()
a = 'kf::labRec[f_ - 200][k_] = k_ < kf::labN ? q(kf::labNow[k_]) : 0;'
assert s.count(a) == 1
s = s.replace(a, 'kf::labRec[f_ - 200][k_] = (k_ < kf::labN || (k_ >= 36 && k_ < 44)) ? q(kf::labNow[k_]) : 0;')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('objlap ok')
