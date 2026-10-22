"""A44 real residency reservation backend; cold target gate, not a FPS fix."""
import argparse,importlib.util,json,shutil
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('source',type=Path)
a=p.parse_args();repo=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('quiet',repo/'tools/tyrax2-quiet-fixture.py')
q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q)
assert not a.out.exists()
binding=json.loads((a.source/'native-binding.json').read_text())
for rel,h in binding['files'].items():assert q.sha(a.source/rel)==h,rel
report=json.loads((a.source/'cell-sdk-emulator.json').read_text())
assert report['valid'] and report['elfSha256']==binding['elfSha256']
def ignore(path,names):
    return q.ignore(path,names)+[n for n in names if n.startswith(('tape-','command-')) and n.endswith('.bin')]
for part in ('game','tyra/engine'):
    shutil.copytree(a.source/part,a.out/part,ignore=ignore)
for rel in ('tyra/Makefile.base','authored-manifest.json','world-cell-sdk-manifest.json'):
    shutil.copy2(a.source/rel,a.out/rel)
engine=a.out/'tyra/engine'
header=engine/'inc/renderer/core/texture/renderer_core_texture.hpp'
text=header.read_text()
text=q.once(text,'  RendererCoreTextureBuffers useTexture(const Texture* t_tex);','''
  typedef void (*WorldTextureFence)(void*);
  bool reserveWorldTextureSet(const std::vector<const Texture*>&,u32,u32,
                             WorldTextureFence,void*);
  void releaseWorldTextureSet();
  RendererCoreTextureBuffers worldTextureSlot(u32);
  u64 getWorldTextureEpoch()const{return worldTextureEpoch;}
  u32 getWorldTextureWords()const{return worldTextureWords;}
  bool diagnosticWorldVictimCheck();
  RendererCoreTextureBuffers useTexture(const Texture* t_tex);''')
text=q.once(text,' private:\n',''' private:
  std::vector<u32> worldTextureIds;
  u64 worldTextureEpoch=1;
  u32 worldTextureWords=0;
  WorldTextureFence worldTextureFence=nullptr;
  void* worldTextureFenceContext=nullptr;
  bool isWorldTexturePinned(u32)const;
  bool tryMakeRoomFor(const Texture*);
''')
header.write_text(text)
cpp=engine/'src/renderer/core/texture/renderer_core_texture.cpp'
text=cpp.read_text()
text=q.once(text,'#include <string>','#include <string>\n#include <stdint.h>\n#include "renderer/core/paths/path1/vif1_queue.hpp"')
text=q.once(text,'  auto allocated = getAllocatedBuffersByTextureId(texId);',
    '  TYRA_ASSERT(!isWorldTexturePinned(texId),"A44 freeing reserved world texture");\n  auto allocated = getAllocatedBuffersByTextureId(texId);')
text=q.once(text,'void RendererCoreTexture::evictAll() {',
    'void RendererCoreTexture::evictAll() {\n  TYRA_ASSERT(worldTextureIds.empty(),"A44 layout reset with live world slots");')
victim='int RendererCoreTexture::pickVictim() const {'
assert text.count(victim)==1
start=text.index(victim)
end=text.index('void RendererCoreTexture::makeRoomFor(const Texture* t_tex) {',start)
body=text[start:end]
anchor='  for (u32 i = 0; i < currentAllocations.size(); i++) {'
assert body.count(anchor)==2
body=body.replace(anchor,anchor+'\n    if(isWorldTexturePinned(currentAllocations[i].id))continue;')
text=text[:start]+body+text[end:]
text=q.once(text,'void RendererCoreTexture::makeRoomFor(const Texture* t_tex) {',
    'bool RendererCoreTexture::tryMakeRoomFor(const Texture* t_tex) {')
start,end=q.body_span(text,'RendererCoreTexture::tryMakeRoomFor')
body=text[start:end]
body=q.once(body,'if (gs->vram.canAllocatePair(coreWords, clutWords)) return;',
                 'if (gs->vram.canAllocatePair(coreWords, clutWords)) return true;')
assert '  TYRA_ASSERT(gs->vram.canAllocatePair(coreWords, clutWords),' in body
body=body[:body.index('  TYRA_ASSERT(gs->vram.canAllocatePair(coreWords, clutWords),')]+'''
  return gs->vram.canAllocatePair(coreWords, clutWords);
}'''
wrapper='''
void RendererCoreTexture::makeRoomFor(const Texture* t_tex) {
  TYRA_ASSERT(tryMakeRoomFor(t_tex),"Texture cannot fit beside reserved world slots");
}
'''
methods=(repo/'tools/tyrax2-world-texture-methods.inc').read_text()
text=text[:start]+body+wrapper+'\n'+methods+text[end:]
cpp.write_text(text)
renderer=engine/'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
text=renderer.read_text()
text=q.once(text,'    WorldCellSdkCheck::run(untextured,textured);','''
    WorldCellSdkCheck::run(untextured,textured);
    auto& manager=rendererCore->texture;
    const auto* image=bag->texture->texture;
    const u32 heap=rendererCore->gs.vram.getHeapWords();
    const u64 epoch=manager.getWorldTextureEpoch();
    auto fence=[](void* context){
      auto* core=static_cast<RendererCore*>(context);
      core->synchronizeFrame();core->sync.align3D();core->sync.align2D();
    };
    TYRA_ASSERT(!manager.reserveWorldTextureSet({},heap/2,heap/4,fence,rendererCore) &&
      !manager.reserveWorldTextureSet({nullptr},heap/2,heap/4,fence,rendererCore) &&
      !manager.reserveWorldTextureSet({image},0,heap/4,fence,rendererCore) &&
      !manager.reserveWorldTextureSet({image},heap,heap,fence,rendererCore) &&
      !manager.reserveWorldTextureSet({image},heap/2,heap/4,nullptr,rendererCore) &&
      manager.getWorldTextureEpoch()==epoch,"A44 reservation rejection mutated epoch");
    TYRA_ASSERT(manager.reserveWorldTextureSet({image,image},heap/2,heap/4,fence,rendererCore),
      "A44 world texture reservation failed");
    const auto slot=manager.worldTextureSlot(image->id);
    const auto rebound=manager.useTexture(image);
    TYRA_ASSERT(slot.id==image->id && slot.core==rebound.core && slot.clut==rebound.clut &&
      manager.getWorldTextureWords()>0 && manager.getWorldTextureEpoch()==epoch+1 &&
      manager.diagnosticWorldVictimCheck() &&
      !manager.reserveWorldTextureSet({image},heap/2,heap/4,fence,rendererCore),
      "A44 reserved slot/address/victim mismatch");
    const u32 words=manager.getWorldTextureWords();
    manager.releaseWorldTextureSet();
    TYRA_ASSERT(manager.worldTextureSlot(image->id).id==0 &&
      manager.getWorldTextureWords()==0 && manager.getWorldTextureEpoch()==epoch+2,
      "A44 world texture release failed");
    printf("A44SLOTS textures=1 words=%u heap=%u budget=%u dynamic=%u controls=6 victimTiers=2 fence=GS released=1\\n",
      words,heap,heap/2,heap/4);fflush(stdout);
''')
renderer.write_text(text)
manifest={'experiment':'A44-world-texture-slots','baseElfSha256':binding['elfSha256'],
 'status':'UNQUALIFIED','limits':['Cold reservation backend gate only, not complete world-cell submission.',
 'Address/CLUT allocation leases do not freeze texture pixels or material metadata.',
 'No compiled head, full terrain texture budget, new GPU body/raster or FPS qualification.']}
(a.out/'world-texture-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(a.out,'UNQUALIFIED real world texture reservation backend')
