"""Private VU paint prepass A/B/A. No production shader or codegen changes."""
import importlib.util
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
out, source = [Path(p).resolve() for p in sys.argv[1:3]]
modes = [6,0,6] if '--reverse' in sys.argv else [0,6,0]
subprocess.run([sys.executable, str(ROOT/'tools/tyrax2-reflection-turn.py'),
                str(out), str(source), '--scatter-copy']+
                (['--night-roads'] if '--night-roads' in sys.argv else []), check=True)

def edit(path, old, new):
    text = path.read_text(encoding='utf-8-sig')
    assert text.count(old) == 1, (path, old, text.count(old))
    path.write_bytes(text.replace(old, new).encode())

engine = out/'tyra/engine'
core = engine/'src/renderer/3d/pipeline/static/core'
if '--night-roads' in sys.argv:
    edit(out/'game/src/terrain_game.cpp', '    if(groundShadowPins.empty())MotionProbe::valid=false;', '''    if(groundShadowPins.empty())MotionProbe::valid=false;
    unsigned roads=0, lights=0;
    for(const auto& c:procChunks) if(c.owner==-3 && c.bag && c.bag->count) ++roads;
    for(const auto& l:engine->renderer.core.dynLights) if(l.enabled) ++lights;
    if(!roads || !lights || daynight::g_hour!=0.0F || !daynight::g_paused) MotionProbe::valid=false;
    printf("LOG: NIGHTROADS phase=%u hour=%f paused=%u chunks=%u lights=%u\\n",
        MotionProbe::phase,daynight::g_hour,unsigned(daynight::g_paused),roads,lights);''')
edit(engine/'inc/debug/motion_probe.hpp', 'constexpr U modes[]={0,5,0};',
     'constexpr U modes[]={'+','.join(map(str,modes))+'};')
edit(engine/'inc/debug/motion_probe.hpp', 'PaintBlocked, Count', 'PaintBlocked, PaintVu, Count')
edit(engine/'inc/debug/motion_probe.hpp', 'paintBlocked,captureFrozen', 'paintBlocked,paintVu,captureFrozen')
bag = engine/'inc/renderer/3d/pipeline/static/core/bag/stapip_texture_bag.hpp'
edit(bag, '  Vec4 envRight;', '''  // Private trial: accepted paint basis, NOT the capture sampling basis.
  bool paintVu = false;
  Vec4 paintForward;
  Vec4 paintHalf;
  Vec4 envRight;''')

physics = out/'game/src/gen/game_physics.gen.cpp'
edit(physics, '      part.envTexBag->textureFunction = 3;', '''      part.envTexBag->paintVu = MotionProbe::variant() == 6;
      if(part.envTexBag->paintVu) MotionProbe::add(MotionProbe::PaintVu);
      part.envTexBag->textureFunction = 3;''')
edit(physics, '        // The tier the bags currently point at', '''        if(part.envTexBag->paintVu) {
          part.envTexBag->paintForward = fwdL;
          part.envTexBag->paintHalf = hL;
        } else {
        // The tier the bags currently point at''')
edit(physics, '    if(vehiclePaintFor(objectIndex)) MotionProbe::add(MotionProbe::EnvDrawn);',
     '    } // private EE paint alternative\n    if(vehiclePaintFor(objectIndex)) MotionProbe::add(MotionProbe::EnvDrawn);')
# The closing brace is interchangeable syntactically, but keep it next to the
# original palette loop for clarity (the following two braces close changed/paint).
edit(physics, '''        }
      }
    }
    } // private EE paint alternative''', '''        }
        } // private EE paint alternative
      }
    }''')

q = core/'stapip_qbuffer_renderer.cpp'
edit(q, 'const s32 variantLane = sharedClipVariant ? 1 : (spotActive ? -1 : 0);',
     '''const bool paintVu = bag->texture && bag->texture->coordinatesAreNormals && bag->texture->paintVu;
    TYRA_ASSERT(!paintVu || (vu1Clipping && !repository.hasAnyOverride() && bag->color->many && !bag->color->single),
                "Private VU paint requires ordinary VU clipping and many colors");
    const s32 variantLane = paintVu ? 2 : (sharedClipVariant ? 1 : (spotActive ? -1 : 0));''')
edit(q, '      emitUnpack(objectDataPacket, VU1_ENV_BASIS_ADDR, e, 3);', '''      emitUnpack(objectDataPacket, VU1_ENV_BASIS_ADDR, e, 3);
      if(bag->texture->paintVu) {
        const Vec4& f = bag->texture->paintForward;
        const Vec4& h = bag->texture->paintHalf;
        alignas(16) const float p[8] = {f.x,f.y,f.z,0.0F,h.x,h.y,h.z,0.0F};
        emitUnpack(objectDataPacket, VU1_LIGHTS_DIRS_ADDR, p, 2);
      }''')
# Prepass modifies only VU input colors, never EE arrays or vertex positions.
# It runs before any clipping interpolation. Mode 2 is private, env-only.
prepass = '''
    ; Private paint prepass. Accepted basis in 12/13; ordinary ABI unchanged.
    ilw.y paintMode, 8(vi00)
    iaddiu paintTwo, vi00, 2
    ibne paintMode, paintTwo, paintDone
    ilw.w paintCount, 0(buffer)
    iaddiu paintMask, vi00, 1023
    iand paintCount, paintCount, paintMask
    iblez paintCount, paintDone
    iaddiu paintNormal, buffer, 2
    iadd paintNormal, paintNormal, paintCount
    iadd paintColor, paintNormal, paintCount
    lq paintFwd, 12(vi00)
    lq paintHalf, 13(vi00)
paintLoop:
    lq paintN, 0(paintNormal)
    mul.xyz paintTmp, paintN, paintFwd
    add.x paintD, paintTmp, paintTmp[y]
    add.x paintD, paintD, paintTmp[z]
    abs.x paintD, paintD
    sub.x paintD, vf00, paintD
    add.x paintD, paintD, vf00[w]
    loi 0.70
    mul.x paintD, paintD, i
    loi 0.30
    add.x paintD, paintD, i
    loi 128
    mul.x paintD, paintD, i
    add.xyz paintOut, vf00, paintD[x]
    mul.xyz paintTmp, paintN, paintHalf
    add.x paintD, paintTmp, paintTmp[y]
    add.x paintD, paintD, paintTmp[z]
    max.x paintD, paintD, vf00[x]
    mul.x paintD, paintD, paintD
    mul.x paintD, paintD, paintD
    mul.x paintD, paintD, paintD
    loi 220
    mul.x paintD, paintD, i
    add.x paintD, paintD, vf00[w]
    loi 255
    mini.x paintD, paintD, i
    sub.w paintOut, vf00, vf00[w]
    add.w paintOut, paintOut, paintD[x]
    sq paintOut, 0(paintColor)
    iaddiu paintNormal, paintNormal, 1
    iaddiu paintColor, paintColor, 1
    iaddi paintCount, paintCount, -1
    ibgtz paintCount, paintLoop
paintDone:
'''
for name in ['cull/stapip_cull_tce_vu1.vclpp',
             'cull/stapip_cull_tce_envn_vu1.vclpp',
             'clip/stapip_clip_tc_vu1.vclpp',
             'clip/stapip_clip_tc_fold_vu1.vclpp',
             'clip/stapip_clip_tce_vu1.vclpp']:
    path = core/'programs'/name
    text = path.read_text()
    import re
    match = re.search(r'begin:\n    xtop\s+buffer\n', text)
    assert match, path
    edit(path, match[0], match[0]+prepass)

spec = importlib.util.spec_from_file_location('quiet', ROOT/'tools/tyrax2-quiet-fixture.py')
quiet = importlib.util.module_from_spec(spec)
spec.loader.exec_module(quiet)
(out/'source-manifest.json').write_text(json.dumps(dict(
    experiment='private-vu-paint-prepass', modes=modes, warmup=1200,
    park=120, sweep=720, cycle=240, sourceProject=str(source), nightRoads='--night-roads' in sys.argv,
    restrictions=['VU clipping only', 'no program overrides', 'many colors only'],
    files=quiet.source_inputs(out, 'vehicle-playground.tyra')), indent=2)+'\n')
print(out)
