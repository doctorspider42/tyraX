"""game/awakevis.py ARM - awake physics bodies per frame, KFL (x0.01 = count):
36 awake (stepped), 37 of them inside the view frustum (last frame's planes,
a 2-unit box around the centre), 38 within 30 units of the camera, 39 inside
the frustum AND within 30 units, 40 awake bodies on an island of one (no body
contact this frame is not known here - so: held bodies, x0.01), 41 physics
ms (0.01 ms, same as KF phys). Not with other KFL patches."""
import sys
root = sys.argv[1]
p = root + '/src/gen/game_physics.gen.cpp'
s = open(p, encoding='utf-8').read()
a = ('    if (physAsleep(o)) continue;\n'
     '    bpTouch(i);  // the solver may move it: the next refresh must look\n')
assert s.count(a) == 1, s.count(a)
s = s.replace(a, a +
    '    {\n'
    '      kf::labNow[36] += 2949u;\n'
    '      const float* p_ = o.data.position;\n'
    '      const Tyra::Vec4 mn_(p_[0] - 1.0F, p_[1] - 1.0F, p_[2] - 1.0F, 1.0F);\n'
    '      const Tyra::Vec4 mx_(p_[0] + 1.0F, p_[1] + 1.0F, p_[2] + 1.0F, 1.0F);\n'
    '      const bool in_ = Tyra::CoreBBox::frustumCheckAABB(\n'
    '          engine->renderer.core.renderer3D.frustumPlanes.getAll(), mn_, mx_) !=\n'
    '          Tyra::CoreBBoxFrustum::OUTSIDE_FRUSTUM;\n'
    '      const float dx_ = p_[0] - cameraPosition.x, dy_ = p_[1] - cameraPosition.y,\n'
    '                  dz_ = p_[2] - cameraPosition.z;\n'
    '      const bool near_ = dx_ * dx_ + dy_ * dy_ + dz_ * dz_ < 900.0F;\n'
    '      if (in_) kf::labNow[37] += 2949u;\n'
    '      if (near_) kf::labNow[38] += 2949u;\n'
    '      if (in_ && near_) kf::labNow[39] += 2949u;\n'
    '      const float v2_ = o.velocityX * o.velocityX + o.velocityY * o.velocityY + o.velocityZ * o.velocityZ;\n'
    '      if (!in_ && v2_ < PHYS_WAKE_SPEED2) kf::labNow[40] += 2949u;\n'
    '      if (!in_ && v2_ < 16.0F * PHYS_WAKE_SPEED2) kf::labNow[41] += 2949u;\n'
    '      if (!in_ && v2_ >= 16.0F * PHYS_WAKE_SPEED2) kf::labNow[42] += 2949u;\n'
    '    }\n')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
p = root + '/src/terrain_game.cpp'
s = open(p, encoding='utf-8').read()
a = 'kf::labRec[f_ - 200][k_] = k_ < kf::labN ? q(kf::labNow[k_]) : 0;'
assert s.count(a) == 1
s = s.replace(a, 'kf::labRec[f_ - 200][k_] = (k_ < kf::labN || (k_ >= 36 && k_ < 44)) ? q(kf::labNow[k_]) : 0;')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('awakevis ok')
