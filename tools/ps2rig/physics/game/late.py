"""game/late.py ARM - game side of engine/lateprobe.py: tags loop phases
(1 = loop start, i+2 = after UPD_LAP(i), 20 = after beginFrame, 21 = before
endFrame, 22 = after endFrame) and dumps the LATE ring with the KF dump."""
import sys, os
root = sys.argv[1]
p = os.path.join(root, 'src/terrain_game.cpp')
s = open(p, encoding='utf-8').read()


def rep(a, b, n=1):
    global s
    assert s.count(a) == n, (a[:60], s.count(a))
    s = s.replace(a, b)


rep("  u32 updT = Tyra::FrameProfile::ticks();\n",
    "  u32 updT = Tyra::FrameProfile::ticks();\n"
    "  Tyra::FrameProfile::probePhase = 1; Tyra::FrameProfile::probeFrame = g_frameStamp;\n")
rep("    ftrig::updLap[i] += updNow - updT;                      \\\n",
    "    ftrig::updLap[i] += updNow - updT;                      \\\n"
    "    Tyra::FrameProfile::probePhase = (i) + 2;               \\\n")
rep("  engine->renderer.beginFrame(CameraInfo3D(&cameraPosition, &cameraLookAt, &cameraUp));\n",
    "  engine->renderer.beginFrame(CameraInfo3D(&cameraPosition, &cameraLookAt, &cameraUp));\n"
    "  Tyra::FrameProfile::probePhase = 20;\n")
rep("  engine->renderer.endFrame();\n",
    "  Tyra::FrameProfile::probePhase = 21;\n  engine->renderer.endFrame();\n  Tyra::FrameProfile::probePhase = 22;\n")
rep("      kf::dumped = true;\n",
    "      kf::dumped = true;\n"
    "      {\n"
    "        const u32 n_ = Tyra::FrameProfile::lateCount < 512 ? Tyra::FrameProfile::lateCount : 512;\n"
    "        TYRA_LOG(\"LATE count \", (int)Tyra::FrameProfile::lateCount);\n"
    "        for (u32 b_ = 0; b_ < n_; b_ += 24) {\n"
    "          char l_[900]; int o_ = snprintf(l_, sizeof(l_), \"LATE\");\n"
    "          for (u32 j_ = b_; j_ < n_ && j_ < b_ + 24; ++j_) {\n"
    "            const Tyra::FrameProfile::LateEvent& e_ = Tyra::FrameProfile::lateRing[(Tyra::FrameProfile::lateCount - n_ + j_) & 511];\n"
    "            o_ += snprintf(l_ + o_, sizeof(l_) - o_, \" %u:%u:%u:%u\", (unsigned)e_.frame, (unsigned)e_.phase, (unsigned)e_.kind, (unsigned)(e_.ticks / 2949U));\n"
    "          }\n"
    "          TYRA_LOG(l_);\n"
    "        }\n"
    "      }\n")
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('late ok')
