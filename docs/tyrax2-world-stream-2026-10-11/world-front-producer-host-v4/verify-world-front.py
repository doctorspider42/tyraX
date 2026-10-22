"""Bound A35 fields/lifecycle plus actual bbox algorithms, not full renderer emulation."""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('out',type=Path)
p.add_argument('--fixture',type=Path,required=True);a=p.parse_args()
repo=Path(__file__).resolve().parents[1];engine=a.fixture/'tyra/engine'
subprocess.run([sys.executable,str(repo/'tools/verify-prepared-front.py'),str(a.out),
 '--engine',str(engine)],check=True)
binding=json.loads((a.fixture/'native-binding.json').read_text())
for rel in ('tyra/engine/inc/debug/world_front.hpp',
 'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp'):
 assert hashlib.sha256((a.fixture/rel).read_bytes()).hexdigest()==binding['files'][rel]
shutil.copy2(engine/'inc/debug/world_front.hpp',a.out/'inc/debug/world_front.hpp')
for rel in ('renderer/3d/pipeline/shared/pipeline_transformation_type.hpp',
 'renderer/3d/pipeline/shared/bag/pipeline_info_bag_frustum_culling.hpp'):
 src=engine/'inc'/rel;assert hashlib.sha256(src.read_bytes()).hexdigest()==binding['files']['tyra/engine/inc/'+rel]
 dst=a.out/'inc'/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
core=(engine/'src/renderer/3d/pipeline/static/core/stapip_core.cpp').read_text()
shape=core[core.index('static bool ticketShape('):core.index('bool StaPipCore::prepareWorldTicket(')]
start=core.index('  if(t.vertices!=');end=core.index('return false;',start)+len('return false;')
match=core[start:end].replace('getWorldEpoch()','epoch').replace('qbufferRenderer.getBufferSize()','bufferSize')
adapter='''#include "renderer/3d/pipeline/shared/pipeline_transformation_type.hpp"
#include "renderer/3d/pipeline/shared/bag/pipeline_info_bag_frustum_culling.hpp"
namespace Tyra {
struct TicketInfo {M4x4* model;bool fullClipChecks;PipelineTransformationType transformationType;PipelineInfoBagFrustumCulling frustumCulling;bool fogDisabled=false;};
struct TicketColor {void* many;void* single;unsigned* contentVersion;};
struct TicketTexture {void* texture;Vec4* coordinates;unsigned* contentVersion;bool coordinatesAreNormals;};
struct StaPipBag {bool stripped;Vec4* vertices;unsigned* contentVersion;unsigned count,packageSize;TicketInfo* info;TicketColor* color;void* lighting;void* billboard;TicketTexture* texture;unsigned bboxVersion;WorldFront::Attachment worldTicket;};
'''+shape+'''static bool actualTicketMatches(const WorldFront::Owner& t,const StaPipBag* b,unsigned epoch,unsigned bufferSize){
'''+match+'\n  return true;\n}\n}\n'
(a.out/'actual-ticket-admission.inc').write_text(adapter)
start=core.index('bool StaPipCore::prepareWorldTicket(')
end=core.index('bool StaPipCore::renderWorldTicket(',start)
producer=core[start:end].replace('reinterpret_cast<u32>(b->vertices)',
 'static_cast<u32>(reinterpret_cast<std::uintptr_t>(b->vertices))')
(a.out/'producer-pins.json').write_text(json.dumps({'elfSha256':binding['elfSha256'],
 'coreSha256':binding['files']['tyra/engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp'],
 'ownerSha256':binding['files']['tyra/engine/inc/debug/world_front.hpp'],
 'originalProducerSha256':hashlib.sha256(core[start:end].encode()).hexdigest(),
 'abiReplacement':'native pointer-to-u32 cast via uintptr_t on host',
 'scope':'extracted actual producer; SDK/compiler/cacher host shims, no target proof'},indent=2)+'\n')
(a.out/'actual-ticket-producer.inc').write_text('namespace Tyra {\n'+producer+'\n}\n')
for name in ('verify-world-front.cpp','verify-world-front.py','verify-world-front-producer.inc'):shutil.copy2(repo/'tools'/name,a.out/name)
sources=list((a.out/'src').rglob('*.cpp'))
for mode,flags in (('optimized',['-O2']),('sanitized',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])):
 binary=a.out/f'ticket-{mode}'
 for stage,cmd in (('build',['g++','-std=c++17','-Wall','-Wextra','-Werror',
  '-Wno-ignored-qualifiers','-Wno-deprecated-copy','-Wno-misleading-indentation','-Wno-missing-field-initializers',*flags,
  '-I'+str(a.out/'inc'),str(a.out/'verify-world-front.cpp'),*map(str,sources),'-o',str(binary)]),('run',[str(binary)])):
  r=subprocess.run(cmd,text=True,capture_output=True);(a.out/f'ticket-{mode}-{stage}.log').write_text(r.stdout+r.stderr)
  r.check_returncode()
  if stage=='run':print(r.stdout,end='')
inventory={str(f.relative_to(a.out)):hashlib.sha256(f.read_bytes()).hexdigest() for f in a.out.rglob('*')
 if f.is_file() and f.name not in ('sha256.json','verify-optimized','verify-sanitized','ticket-optimized','ticket-sanitized')}
(a.out/'sha256.json').write_text(json.dumps(inventory,indent=2)+'\n')
