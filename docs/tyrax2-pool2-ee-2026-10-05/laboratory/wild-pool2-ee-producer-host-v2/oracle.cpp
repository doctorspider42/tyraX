#include <algorithm>
#include <memory>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <limits>
#include <string>
#include "tyra"
#include "bag_array.gen.hpp"
namespace Vehicle_playground {unsigned int g_contentStamp=0;}
using namespace Vehicle_playground;
using namespace Tyra;
unsigned int g_bboxStamp=0;
namespace NightAblation {bool poolTableEnabled=false; constexpr int LightEffects=1,Pools=2; void submitted(int){} void extraSubmitted(int){}}
struct StubPipeline {struct Core {size_t calls=0; void render(StaPipBag*){++calls;}} core;};
struct LightPool {BagArray<Vec4> verts,sts; Color color; std::unique_ptr<StaPipInfoBag> info=std::make_unique<StaPipInfoBag>(); std::unique_ptr<StaPipTextureBag> texBag=std::make_unique<StaPipTextureBag>();};

namespace Legacy {
struct TerrainGame {

struct PoolBatch {
    BagArray<Tyra::Vec4> verts, sts;
    BagArray<Tyra::Color> colors;
    Tyra::M4x4 mat;
    std::unique_ptr<Tyra::StaPipInfoBag> info;
    std::unique_ptr<Tyra::StaPipColorBag> colorBag;
    std::unique_ptr<Tyra::StaPipTextureBag> texBag;
    std::unique_ptr<Tyra::StaPipBag> bag;
    std::vector<const LightPool*> members;
    std::vector<float> memberFix;
    std::vector<unsigned int> key, lastKey;
    std::vector<Tyra::ExperimentalPoolTable::ColorBits> tableColors;
    Tyra::ExperimentalPoolTable::View tableView;
  } poolBatch_;
StubPipeline stapip;
void poolBatchFlush(); void poolBatchAdd(const LightPool&,float);
};

void TerrainGame::poolBatchAdd(const LightPool& b, float fix) {
  poolBatch_.members.push_back(&b);
  poolBatch_.memberFix.push_back(fix);
}
void TerrainGame::poolBatchFlush() {
  PoolBatch& pb = poolBatch_;
  if (pb.members.empty()) return;
  // One key word run per member: which pool, its two source stamps, its
  // colour and FIX. Equal to last flush's = the arrays already hold this.
  pb.key.clear();
  for (size_t m = 0; m < pb.members.size(); ++m) {
    const LightPool& b = *pb.members[m];
    unsigned int w[7];
    w[0] = (unsigned int)(uintptr_t)&b;
    w[1] = b.verts.stamp();
    w[2] = b.sts.stamp();
    memcpy(&w[3], &b.color.r, 4);
    memcpy(&w[4], &b.color.g, 4);
    memcpy(&w[5], &b.color.b, 4);
    memcpy(&w[6], &pb.memberFix[m], 4);
    pb.key.insert(pb.key.end(), w, w + 7);
  }
  if (!pb.bag) {
    const LightPool& b0 = *pb.members[0];
    pb.mat.identity();
    pb.info = std::make_unique<StaPipInfoBag>();
    *pb.info = *b0.info;  // TestOnly z, precise culling, clip checks, no relight
    pb.info->model = &pb.mat;
    pb.info->shadingType = TyraShadingGouraud;
    pb.info->additiveBlendFix = 128;
    pb.info->dateLit = false;
    pb.colorBag = std::make_unique<StaPipColorBag>();
    pb.texBag = std::make_unique<StaPipTextureBag>();
    pb.texBag->texture = b0.texBag->texture;
    pb.bag = std::make_unique<StaPipBag>();
    pb.bag->info = pb.info.get();
    pb.bag->color = pb.colorBag.get();
    pb.bag->texture = pb.texBag.get();
    pb.bag->lighting = nullptr;
  }
  if (pb.key != pb.lastKey) {
    pb.verts.clear();
    pb.sts.clear();
    pb.colors.clear();
    pb.tableColors.clear();
    bool tableShape=true;
    for (size_t m = 0; m < pb.members.size(); ++m) {
      const LightPool& b = *pb.members[m];
      // The old pass drew Cs * FIX / 128 with Cs = tex * colour / 128; the
      // batch draws at FIX 128, so FIX / 128 moves into the colour.
      const float f = pb.memberFix[m] / 128.0F;
      const Tyra::Color c(b.color.r * f, b.color.g * f, b.color.b * f, 128.0F);
      Tyra::ExperimentalPoolTable::ColorBits coefficient;
      memcpy(&coefficient,c.rgba,16);
      pb.tableColors.push_back(coefficient);
      const BagArray<Tyra::Vec4>& cv = b.verts;
      const BagArray<Tyra::Vec4>& cs = b.sts;
      if(cv.size()!=96 || cs.size()!=96) tableShape=false;
      for (size_t k = 0; k < cv.size(); ++k) {
        pb.verts.push_back(cv[k]);
        pb.sts.push_back(cs[k]);
        pb.colors.push_back(c);
      }
    }
    pb.verts.bind(pb.bag);
    pb.bag->count = (u32)pb.verts.size();
    pb.sts.bind(pb.texBag);
    pb.colors.bind(pb.colorBag);
    pb.colorBag->single = nullptr;
    pb.bag->bboxVersion = ++g_bboxStamp;
    pb.lastKey = pb.key;
    pb.tableView.colors=pb.tableColors.data();
    pb.tableView.members=static_cast<uint32_t>(pb.tableColors.size());
    pb.tableView.total=static_cast<uint32_t>(pb.verts.size());
    pb.tableView.generation=pb.colors.stamp();
    pb.bag->experimentalPoolTable=tableShape ? &pb.tableView : nullptr;
  }
  (NightAblation::submitted(NightAblation::LightEffects), NightAblation::extraSubmitted(NightAblation::Pools), stapip.core.render(pb.bag.get()));
  pb.members.clear();
  pb.memberFix.clear();
}
}

namespace Candidate {
struct TerrainGame {

struct PoolBatch {
    BagArray<Tyra::Vec4> verts, sts;
    BagArray<Tyra::Color> colors;
    Tyra::M4x4 mat;
    std::unique_ptr<Tyra::StaPipInfoBag> info;
    std::unique_ptr<Tyra::StaPipColorBag> colorBag;
    std::unique_ptr<Tyra::StaPipTextureBag> texBag;
    std::unique_ptr<Tyra::StaPipBag> bag;
    std::vector<const LightPool*> members;
    std::vector<float> memberFix;
    std::vector<unsigned int> key, lastKey;
    std::vector<Tyra::ExperimentalPoolTable::ColorBits> tableColors;
    Tyra::ExperimentalPoolTable::View tableView;
    // Private EE producer experiment: persistent backing only for fallback reads.
    std::vector<Tyra::Color> lazyColors;
    std::vector<uint32_t> lazyReady;
    std::vector<unsigned int> geometryKey, lastGeometryKey;
    unsigned int lazyGeneration=0;
  } poolBatch_;
StubPipeline stapip;
void poolBatchFlush(); void poolBatchAdd(const LightPool&,float);
};

void TerrainGame::poolBatchAdd(const LightPool& b, float fix) {
  poolBatch_.members.push_back(&b);
  poolBatch_.memberFix.push_back(fix);
}
void TerrainGame::poolBatchFlush() {
  PoolBatch& pb = poolBatch_;
  if (pb.members.empty()) return;
  // One key word run per member: which pool, its two source stamps, its
  // colour and FIX. Equal to last flush's = the arrays already hold this.
  const bool compact = NightAblation::poolTableEnabled &&
      pb.members.size()<=16 && std::all_of(pb.members.begin(),pb.members.end(),
          [](const LightPool* b){ return b->verts.size()==96 && b->sts.size()==96; });
  pb.key.clear();
  pb.key.push_back(compact ? 1u : 0u); // arm transition invalidates source identity
  pb.geometryKey.clear();
  for (size_t m = 0; m < pb.members.size(); ++m) {
    const LightPool& b = *pb.members[m];
    unsigned int w[7];
    w[0] = (unsigned int)(uintptr_t)&b;
    w[1] = b.verts.stamp();
    w[2] = b.sts.stamp();
    memcpy(&w[3], &b.color.r, 4);
    memcpy(&w[4], &b.color.g, 4);
    memcpy(&w[5], &b.color.b, 4);
    memcpy(&w[6], &pb.memberFix[m], 4);
    pb.key.insert(pb.key.end(), w, w + 7);
    pb.geometryKey.insert(pb.geometryKey.end(),w,w+3);
  }
  if (!pb.bag) {
    const LightPool& b0 = *pb.members[0];
    pb.mat.identity();
    pb.info = std::make_unique<StaPipInfoBag>();
    *pb.info = *b0.info;  // TestOnly z, precise culling, clip checks, no relight
    pb.info->model = &pb.mat;
    pb.info->shadingType = TyraShadingGouraud;
    pb.info->additiveBlendFix = 128;
    pb.info->dateLit = false;
    pb.colorBag = std::make_unique<StaPipColorBag>();
    pb.texBag = std::make_unique<StaPipTextureBag>();
    pb.texBag->texture = b0.texBag->texture;
    pb.bag = std::make_unique<StaPipBag>();
    pb.bag->info = pb.info.get();
    pb.bag->color = pb.colorBag.get();
    pb.bag->texture = pb.texBag.get();
    pb.bag->lighting = nullptr;
  }
  if (pb.key != pb.lastKey) {
    const bool rebuildGeometry = !compact || pb.geometryKey!=pb.lastGeometryKey;
    if(rebuildGeometry) { pb.verts.clear(); pb.sts.clear(); }
    if(!compact) pb.colors.clear();
    pb.tableColors.clear();
    bool tableShape=true;
    for (size_t m = 0; m < pb.members.size(); ++m) {
      const LightPool& b = *pb.members[m];
      // The old pass drew Cs * FIX / 128 with Cs = tex * colour / 128; the
      // batch draws at FIX 128, so FIX / 128 moves into the colour.
      const float f = pb.memberFix[m] / 128.0F;
      const Tyra::Color c(b.color.r * f, b.color.g * f, b.color.b * f, 128.0F);
      Tyra::ExperimentalPoolTable::ColorBits coefficient;
      memcpy(&coefficient,c.rgba,16);
      pb.tableColors.push_back(coefficient);
      const BagArray<Tyra::Vec4>& cv = b.verts;
      const BagArray<Tyra::Vec4>& cs = b.sts;
      if(cv.size()!=96 || cs.size()!=96) tableShape=false;
      if(rebuildGeometry || !compact) for (size_t k = 0; k < cv.size(); ++k) {
        if(rebuildGeometry) { pb.verts.push_back(cv[k]); pb.sts.push_back(cs[k]); }
        if(!compact) pb.colors.push_back(c);
      }
    }
    pb.verts.bind(pb.bag);
    pb.bag->count = (u32)pb.verts.size();
    pb.sts.bind(pb.texBag);
    if(compact) {
      pb.lazyColors.resize(pb.verts.size()); // resize touches only newly added storage
      pb.lazyReady.resize(pb.verts.size(),0);
      ++pb.lazyGeneration;
      if(pb.lazyGeneration==0) {
        std::fill(pb.lazyReady.begin(),pb.lazyReady.end(),0);
        ++pb.lazyGeneration;
      }
      pb.colorBag->many=pb.lazyColors.data();
      pb.colorBag->contentVersion=&pb.lazyGeneration;
    } else pb.colors.bind(pb.colorBag);
    pb.colorBag->single = nullptr;
    if(rebuildGeometry) pb.bag->bboxVersion = ++g_bboxStamp;
    pb.lastGeometryKey=pb.geometryKey;
    pb.lastKey = pb.key;
    pb.tableView.colors=pb.tableColors.data();
    pb.tableView.members=static_cast<uint32_t>(pb.tableColors.size());
    pb.tableView.total=static_cast<uint32_t>(pb.verts.size());
    pb.tableView.generation=compact ? pb.lazyGeneration : pb.colors.stamp();
    pb.tableView.expanded=compact ? static_cast<void*>(pb.lazyColors.data()) : nullptr;
    pb.tableView.ready=compact ? pb.lazyReady.data() : nullptr;
    pb.bag->experimentalPoolTable=tableShape ? &pb.tableView : nullptr;
  }
  (NightAblation::submitted(NightAblation::LightEffects), NightAblation::extraSubmitted(NightAblation::Pools), stapip.core.render(pb.bag.get()));
  pb.members.clear();
  pb.memberFix.clear();
}
}

size_t checks=0,flushes=0,colorOnly=0,partialRanges=0;
void demand(bool b,const std::string& msg){++checks;if(!b) throw std::runtime_error(msg);}
template<class T> std::vector<unsigned char> bytes(const T* p,size_t n){auto* b=reinterpret_cast<const unsigned char*>(p);return std::vector<unsigned char>(b,b+n*sizeof(T));}
struct State {unsigned vs,ss,cs,bs,legacyCs;std::vector<unsigned char> v,s,c;std::vector<uint32_t> ready;};
State state(Candidate::TerrainGame& g){auto& p=g.poolBatch_;return {p.verts.stamp(),p.sts.stamp(),p.colorBag?*p.colorBag->contentVersion:0,p.bag?p.bag->bboxVersion:0,p.colors.stamp(),bytes(p.verts.data(),p.verts.size()),bytes(p.sts.data(),p.sts.size()),bytes(p.lazyColors.data(),p.lazyColors.size()),p.lazyReady};}
void pairFlush(Legacy::TerrainGame& a,Candidate::TerrainGame& b,std::vector<LightPool>& pools,const std::vector<int>& order,const std::vector<float>& fix,bool arm,const std::string& mode){
 auto before=state(b);NightAblation::poolTableEnabled=arm;
 for(size_t i=0;i<order.size();++i){a.poolBatchAdd(pools[order[i]],fix[i]);b.poolBatchAdd(pools[order[i]],fix[i]);}
 a.poolBatchFlush();if(mode=="wrap")b.poolBatch_.lazyGeneration=std::numeric_limits<unsigned int>::max();b.poolBatchFlush();++flushes;
 auto& ap=a.poolBatch_;auto& bp=b.poolBatch_;auto after=state(b);
 demand(ap.bag->count==bp.bag->count,"count");
 demand(bytes(ap.bag->vertices,ap.bag->count)==bytes(bp.bag->vertices,bp.bag->count),"positions");
 demand(bytes(ap.texBag->coordinates,ap.bag->count)==bytes(bp.texBag->coordinates,bp.bag->count),"ST");
 demand(bp.bag->contentVersion==bp.verts.stampPtr()&&bp.texBag->contentVersion==bp.sts.stampPtr(),"geometry stamp binding");
 bool compact=arm&&order.size()<=16&&std::all_of(order.begin(),order.end(),[&](int i){return pools[i].verts.size()==96&&pools[i].sts.size()==96;});
 if(mode=="warm"){
  demand(before.vs==after.vs&&before.ss==after.ss&&before.cs==after.cs&&before.bs==after.bs,"warm stamps");
  demand(before.v==after.v&&before.s==after.s&&before.c==after.c&&before.ready==after.ready,"warm bytes");
 }
 if(mode=="color"&&compact){++colorOnly;
  demand(before.vs==after.vs&&before.ss==after.ss&&before.bs==after.bs,"compact color geometry stamps");
  demand(before.v==after.v&&before.s==after.s&&before.c==after.c&&before.ready==after.ready,"compact color wrote geometry/backing");
  demand(before.legacyCs==after.legacyCs,"compact color touched legacy pervertex BagArray");
  demand(before.cs!=after.cs,"color version advancement");
 }
 if(mode=="geometry"||mode=="order") demand(before.bs!=after.bs,"geometry bbox must invalidate");
 demand(bp.bag->info->additiveBlendFix==128&&!bp.bag->info->dateLit&&bp.bag->info->shadingType==TyraShadingGouraud,"render info");
 if(compact){
  demand(bp.colorBag->contentVersion==&bp.lazyGeneration&&bp.tableView.generation==bp.lazyGeneration,"lazy generation binding");
  auto old=bytes(bp.lazyColors.data(),bp.lazyColors.size());auto ready=bp.lazyReady;
  demand(!ExperimentalPoolTable::materializeRange(&bp.tableView,bp.colorBag->many+bp.bag->count-1,2),"reject overflowing partial range");
  demand(old==bytes(bp.lazyColors.data(),bp.lazyColors.size())&&ready==bp.lazyReady,"rejected partial unchanged");
  size_t off=bp.bag->count>96?90:3,count=std::min<size_t>(75,bp.bag->count-off);
  demand(ExperimentalPoolTable::materializeRange(&bp.tableView,bp.colorBag->many+off,static_cast<uint32_t>(count)),"partial fill");++partialRanges;
  for(size_t k=0;k<bp.bag->count;++k){
   if(k>=off&&k<off+count){demand(std::memcmp(bp.colorBag->many+k,ap.colorBag->many+k,16)==0,"partial color");demand(bp.lazyReady[k]==bp.lazyGeneration,"partial ready");}
   else {demand(std::memcmp(bp.colorBag->many+k,old.data()+k*16,16)==0&&bp.lazyReady[k]==ready[k],"partial touched outside");}
  }
  auto partial=bytes(bp.lazyColors.data(),bp.lazyColors.size());
  demand(ExperimentalPoolTable::materializeRange(&bp.tableView,bp.colorBag->many+off,static_cast<uint32_t>(count)),"repeat partial");
  demand(partial==bytes(bp.lazyColors.data(),bp.lazyColors.size()),"repeat partial bytes");
  demand(ExperimentalPoolTable::materializeRange(&bp.tableView,bp.colorBag->many,bp.bag->count),"full fill");
 } else demand(bp.colorBag->contentVersion==bp.colors.stampPtr(),"legacy color stamp binding");
 demand(bytes(ap.colorBag->many,ap.bag->count)==bytes(bp.colorBag->many,bp.bag->count),"expanded coefficients");
 demand(bp.members.empty()&&bp.memberFix.empty(),"cleared submission");
}
int main(){try {
 for(int n=1;n<=17;++n){
  std::vector<LightPool> pools(static_cast<size_t>(n)); std::vector<int> order;std::vector<float> fix;
  for(int m=0;m<n;++m){order.push_back(m);fix.push_back(static_cast<float>((m*17)%129));pools[m].color=Color(12.5F+m,64.0F-m,127.0F-m,128);
   for(int k=0;k<96;++k){pools[m].verts.push_back(Vec4{float(m),float(k),float(m+k),1});pools[m].sts.push_back(Vec4{float(k)/96,float(m)/16,1,0});}}
  Legacy::TerrainGame a;Candidate::TerrainGame b;
  for(bool arm:{false,true,false,true}){
   pairFlush(a,b,pools,order,fix,arm,"flip");pairFlush(a,b,pools,order,fix,arm,"warm");
   for(int j=0;j<8;++j){if(j%2)pools[j%n].color.r+=0.25F;else fix[j%n]+=0.5F;pairFlush(a,b,pools,order,fix,arm,"color");pairFlush(a,b,pools,order,fix,arm,"warm");}
   pools[0].verts[7].y+=0.5F;pairFlush(a,b,pools,order,fix,arm,"geometry");
   pools[n-1].sts[11].x+=0.03125F;pairFlush(a,b,pools,order,fix,arm,"geometry");
   if(n>1){std::reverse(order.begin(),order.end());pairFlush(a,b,pools,order,fix,arm,"order");}
  }
  pools[0].color.a+=0.125F;pairFlush(a,b,pools,order,fix,true,"warm");
  if(n<=16){pools[0].color.g+=0.125F;pairFlush(a,b,pools,order,fix,true,"wrap");
   demand(b.poolBatch_.lazyGeneration==1,"wrapped generation is nonzero");}
  // Shape refusal retains ordinary per-vertex output, with a short paired stream.
  pools[0].verts.resize(93);pools[0].sts.resize(93);pairFlush(a,b,pools,order,fix,true,"geometry");
 }
 std::cout<<"PASS checks="<<checks<<" flushes="<<flushes<<" compact_color_only="<<colorOnly<<" partial_ranges="<<partialRanges<<"\n";
 return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL after "<<flushes<<" flushes: "<<e.what()<<"\n";return 1;}}
