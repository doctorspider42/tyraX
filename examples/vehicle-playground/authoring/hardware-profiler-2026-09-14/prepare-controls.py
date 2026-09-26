from pathlib import Path
import shutil,json,hashlib
r=Path(__file__).parent
for arm in ['control','no-host','16bpp','no-extra-passes']:
 d=r/arm
 assert not d.exists()
 shutil.copytree(r/'baseline',d,ignore=shutil.ignore_patterns('*.csv','*.log','hardware-trace.cfg','livedbg.bin','livedbg.cmd','frame.tga'))
 if arm=='no-host':
  for p in (d/'src/gen').glob('live_*.cpp'):
   s=p.read_text(encoding='utf-8-sig')
   if 'fopen(' in s:
    s=s.replace('fopen(', 'diagnosticNoHostOpen(')
    s='#include <cstdio>\nstatic FILE* diagnosticNoHostOpen(const char*, const char*) { return nullptr; }\n'+s
    p.write_bytes(s.encode('utf-8'))
 if arm=='16bpp':
  p=d/'src/main.cpp';s=p.read_text(encoding='utf-8-sig');assert 'ColorDepth::Bits32' in s
  p.write_bytes(s.replace('ColorDepth::Bits32','ColorDepth::Bits16').encode('utf-8'))
 if arm=='no-extra-passes':
  p=d/'src/terrain_game.cpp';s=p.read_text(encoding='utf-8-sig')
  s=s.replace('(refreshEnvMap || !sharedEnvBasisValid)', '(!sharedEnvBasisValid)')
  for call in ['renderProjShadows();','updateAndRenderLightPools();','updateAndRenderBlobShadows();','updateAndRenderLightBeams();']:
   assert call in s,call;s=s.replace(call,'/* Diagnostic disabled: '+call+' */')
  p.write_bytes(s.encode('utf-8'))
 (d/'bin/district-benchmark-pose.txt').write_text('0\n',encoding='ascii')
manifest=json.loads(Path('D:/tyra-verify-textures-0914/asset-manifest.json').read_text(encoding='utf-8'))
print(type(manifest),len(manifest))
print('Prepared four controls; no production quality changes')
