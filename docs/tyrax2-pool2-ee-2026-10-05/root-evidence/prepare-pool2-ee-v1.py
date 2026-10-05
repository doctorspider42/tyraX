"""Private source-only Pool2 producer experiment; no build or device mutation."""
from pathlib import Path
import hashlib, json, shutil
lab=Path('F:/Projects/tyrax2-lab-20261001')
base=lab/'wild-pool-table-physical-v3'
out=lab/'wild-pool2-ee-physical-v1'
assert not out.exists()
manifest=json.loads((base/'target-source-manifest.json').read_text())['files']
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
out.mkdir()
for rel, expected in manifest.items():
    source=base/rel;assert sha(source)==expected,rel
    target=out/rel;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
shutil.copytree(base/'game/.res-baked',out/'game/.res-baked')
changed=[]
def edit(rel, old, new, count=1):
    path=out/rel;text=path.read_text(encoding='utf-8');assert text.count(old)==count,(rel,old,text.count(old))
    path.write_bytes(text.replace(old,new).encode());changed.append(rel)
header='tyra/engine/inc/renderer/3d/pipeline/static/core/stapip_pool_color_table.hpp'
edit(header,'struct View { const ColorBits* colors=nullptr; uint32_t members=0,total=0,generation=0; };', '''// Modified by TyraX: private producer-owned lazy legacy backing. No DMA REF
// may consume it before materializeRange; coefficient streams never consume it.
struct View {
 const ColorBits* colors=nullptr; uint32_t members=0,total=0,generation=0;
 void* expanded=nullptr; uint32_t* ready=nullptr;
};
inline bool materializeRange(const View* view,const void* first,uint32_t count) {
 if(!view || !view->expanded) return true; // ordinary V3 source
 if(!view->ready || !view->colors || view->members==0 || view->members>16 ||
    view->total!=view->members*96u || view->generation==0 || !first) return false;
 const uintptr_t base=reinterpret_cast<uintptr_t>(view->expanded);
 const uintptr_t here=reinterpret_cast<uintptr_t>(first);
 if(here<base || (here-base)%16u) return true; // engine-owned copied/clipped buffer
 const uintptr_t offset=(here-base)/16u;
 if(offset>=view->total) return true; // engine-owned buffer, not source backing
 if(count>view->total-offset) return false;
 auto* bytes=static_cast<unsigned char*>(view->expanded);
 for(uint32_t i=static_cast<uint32_t>(offset);i<offset+count;++i) {
  if(view->ready[i]!=view->generation) {
   std::memcpy(bytes+i*16u,&view->colors[i/96u],16);
   view->ready[i]=view->generation;
  }
 }
 return true;
}''')
game='game/inc/terrain_game.hpp'
edit(game,'    Tyra::ExperimentalPoolTable::View tableView;', '''    Tyra::ExperimentalPoolTable::View tableView;
    // Private EE producer experiment: persistent backing only for fallback reads.
    std::vector<Tyra::Color> lazyColors;
    std::vector<uint32_t> lazyReady;
    std::vector<unsigned int> geometryKey, lastGeometryKey;
    unsigned int lazyGeneration=0;''')
lighting='game/src/gen/game_lighting.gen.cpp'
edit(lighting,'  pb.key.clear();\n  for (size_t m = 0;', '''  const bool compact = NightAblation::poolTableEnabled &&
      pb.members.size()<=16 && std::all_of(pb.members.begin(),pb.members.end(),
          [](const LightPool* b){ return b->verts.size()==96 && b->sts.size()==96; });
  pb.key.clear();
  pb.key.push_back(compact ? 1u : 0u); // arm transition invalidates source identity
  pb.geometryKey.clear();
  for (size_t m = 0;''')
edit(lighting,'    pb.key.insert(pb.key.end(), w, w + 7);','''    pb.key.insert(pb.key.end(), w, w + 7);
    pb.geometryKey.insert(pb.geometryKey.end(),w,w+3);''')
edit(lighting,'''    pb.verts.clear();
    pb.sts.clear();
    pb.colors.clear();
    pb.tableColors.clear();''','''    const bool rebuildGeometry = !compact || pb.geometryKey!=pb.lastGeometryKey;
    if(rebuildGeometry) { pb.verts.clear(); pb.sts.clear(); }
    if(!compact) pb.colors.clear();
    pb.tableColors.clear();''')
edit(lighting,'''        pb.verts.push_back(cv[k]);
        pb.sts.push_back(cs[k]);
        pb.colors.push_back(c);''','''        if(rebuildGeometry) { pb.verts.push_back(cv[k]); pb.sts.push_back(cs[k]); }
        if(!compact) pb.colors.push_back(c);''')
edit(lighting,'''    pb.colors.bind(pb.colorBag);
    pb.colorBag->single = nullptr;
    pb.bag->bboxVersion = ++g_bboxStamp;''','''    if(compact) {
      pb.lazyColors.resize(pb.verts.size()); // resize touches only newly added storage
      pb.lazyReady.resize(pb.verts.size(),0);
      pb.lazyGeneration=++g_contentStamp;
      if(pb.lazyGeneration==0) {
        std::fill(pb.lazyReady.begin(),pb.lazyReady.end(),0);
        pb.lazyGeneration=++g_contentStamp;
      }
      pb.colorBag->many=pb.lazyColors.data();
      pb.colorBag->contentVersion=&pb.lazyGeneration;
    } else pb.colors.bind(pb.colorBag);
    pb.colorBag->single = nullptr;
    if(rebuildGeometry) pb.bag->bboxVersion = ++g_bboxStamp;
    pb.lastGeometryKey=pb.geometryKey;''')
edit(lighting,'    pb.tableView.generation=pb.colors.stamp();','''    pb.tableView.generation=compact ? pb.lazyGeneration : pb.colors.stamp();
    pb.tableView.expanded=compact ? static_cast<void*>(pb.lazyColors.data()) : nullptr;
    pb.tableView.ready=compact ? pb.lazyReady.data() : nullptr;''')
# Copy paths consume source color bytes immediately; pointer paths defer until
# cull eligibility/clip selection. All fallback writes remain source-owned.
qbuffer='tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer.cpp'
edit(qbuffer,'    if (pkg1.bag->color->many) memcpy(colors + offset, pkg->colors, bytes);', '''    if (pkg1.bag->color->many) {
      TYRA_ASSERT(ExperimentalPoolTable::materializeRange(pkg->bag->experimentalPoolTable,
          pkg->colors,pkg->size),"Invalid lazy pool source");
      memcpy(colors + offset, pkg->colors, bytes);
    }''',count=2)
edit(qbuffer,'  if (pkg.bag->color->many) memcpy(colors, pkg.colors, bytes);','''  if (pkg.bag->color->many) {
    TYRA_ASSERT(ExperimentalPoolTable::materializeRange(pkg.bag->experimentalPoolTable,
        pkg.colors,pkg.size),"Invalid lazy pool source");
    memcpy(colors, pkg.colors, bytes);
  }''')
edit(qbuffer,'  const bool wantColors = pkg.bag->color->many != nullptr;','''  const bool wantColors = pkg.bag->color->many != nullptr;
  if(wantColors) TYRA_ASSERT(ExperimentalPoolTable::materializeRange(
      pkg.bag->experimentalPoolTable,pkg.colors,pkg.size),"Invalid lazy pool strip");''')
renderer='tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
edit(renderer,'    poolCompared=true;','''    TYRA_ASSERT(ExperimentalPoolTable::materializeRange(poolBag->experimentalPoolTable,
        buffer->colors,buffer->size),"Invalid lazy pool oracle source");
    poolCompared=true;''')
edit(renderer,'  buffer->poolTableApplied=NightAblation::poolTableEnabled && poolEligible && !poolMismatch;','''  buffer->poolTableApplied=NightAblation::poolTableEnabled && poolEligible && !poolMismatch;
  if(!buffer->poolTableApplied) TYRA_ASSERT(ExperimentalPoolTable::materializeRange(
      poolBag->experimentalPoolTable,buffer->colors,buffer->size),"Invalid lazy pool fallback");''')
edit(renderer,'  buffer->poolTableApplied=false;\n  if(buffer->size!=0', '''  buffer->poolTableApplied=false;
  if(buffer->size!=0) TYRA_ASSERT(ExperimentalPoolTable::materializeRange(
      buffer->bag->experimentalPoolTable,buffer->colors,buffer->size),"Invalid lazy pool clip");
  if(buffer->size!=0''')
record={'status':'PRIVATE_SOURCE_PREPARATION_ONLY_NOT_FROZEN', 'baseManifestSha256':sha(base/'target-source-manifest.json'),
        'changedFiles':{rel:sha(out/rel) for rel in sorted(set(changed))},
        'remainingGates':['host extraction and routing audit','native','actual output and moving/clipped fallback','physical both orders'],
        'orderedNextCandidates':[2,5,4]}
(out/'root-preparation.json').write_bytes((json.dumps(record,indent=2)+'\n').encode())
print(json.dumps(record,indent=2))
