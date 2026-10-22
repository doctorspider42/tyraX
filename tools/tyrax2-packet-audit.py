"""Build a private actual-packet filter-order regression; no device actions."""
import argparse
import hashlib
import importlib.util
import json
import shutil
from pathlib import Path

repo = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('out', type=Path)
parser.add_argument('source', type=Path, help='Completed A10 disabled fixture')
args = parser.parse_args()
out, source = args.out.resolve(), args.source.resolve()
assert not out.exists(), 'Refuse to overwrite evidence'
spec = importlib.util.spec_from_file_location('quiet', repo/'tools/tyrax2-quiet-fixture.py')
quiet = importlib.util.module_from_spec(spec)
spec.loader.exec_module(quiet)
shutil.copytree(repo/'vendor/tyra/engine', out/'tyra/engine', ignore=quiet.ignore)
shutil.copy2(repo/'vendor/tyra/Makefile.base', out/'tyra/Makefile.base')
(out/'game/src').mkdir(parents=True)
(out/'game/inc').mkdir()
(out/'game/.res-baked').mkdir()
shutil.copy2(source/'game/Makefile', out/'game/Makefile')
main = (source/'game/src/main.cpp').read_text(encoding='utf-8-sig')
main = quiet.once(main, '#include "terrain_game.hpp"\n#include "paint_raster.hpp"', '#include "packet_audit_game.hpp"')
main = quiet.once(main, 'PaintRasterGame game(&engine);', 'PacketAuditGame game(&engine);')
(out/'game/src/main.cpp').write_text(main, encoding='utf-8')
shutil.copy2(repo/'tools/tyrax2-packet-audit.hpp', out/'game/inc/packet_audit_game.hpp')
shutil.copy2(source/'game/bin/paint-raster.png', out/'game/.res-baked/packet-audit.png')
header = '''#pragma once
#include <packet2.h>
namespace PacketAudit {
extern bool legacy;
extern unsigned checks, errors, culls, clips, replays;
void emitted(const qword_t*, unsigned, bool);
}
'''
(out/'tyra/engine/inc/debug/packet_audit.hpp').write_text(header)
core = out/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp'
data = core.read_text(encoding='utf-8-sig')
data = quiet.once(data, 'namespace Tyra {', '#include "debug/packet_audit.hpp"\nnamespace Tyra {')
data = quiet.once(data, '  qbufferRenderer.setInfo(bag->info);\n  qbufferRenderer.sendObjectData',
                  '  if (!PacketAudit::legacy) qbufferRenderer.setInfo(bag->info);\n  qbufferRenderer.sendObjectData')
data = quiet.once(data, '  qbufferRenderer.setClipperMVP(&mvp);',
                  '  qbufferRenderer.setClipperMVP(&mvp);\n  if (PacketAudit::legacy) qbufferRenderer.setInfo(bag->info);')
core.write_text(data)
q = out/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
data = q.read_text(encoding='utf-8-sig')
data = quiet.once(data, 'namespace Tyra {', '#include "debug/packet_audit.hpp"\nnamespace Tyra {')
marker = '    emitUnpack(objectDataPacket, VU1_OPTIONS_ADDR, opt, optQw);'
data = quiet.once(data, marker, marker+'\n    PacketAudit::emitted(objectDataPacket->next - optQw, optQw, bag->info->textureMappingType == TyraLinear);')
for method, field in [('cull', 'culls'), ('clip', 'clips')]:
    marker = f'void StaPipQBufferRenderer::{method}(StaPipQBuffer* buffer) {{'
    data = quiet.once(data, marker, marker+f'\n  if(buffer->size) ++PacketAudit::{field};')
data = quiet.once(data, '  baked.countHit();\n  objectDataPending = true;',
                  '  baked.countHit();\n  ++PacketAudit::replays;\n  objectDataPending = true;')
q.write_text(data)
manifest = dict(experiment='A16 actual TEX1 packet order regression', cases=18,
                order=['fixed', 'legacy negative control', 'fixed'],
                shapes=['inside', 'near crossing', 'side crossing'], batch=[False, True],
                engineSourceSha256=hashlib.sha256((repo/'vendor/tyra/engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp').read_bytes()).hexdigest(),
                limitations=['No performance claim', 'Not a scene workload'])
(out/'packet-audit-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
print(out)
