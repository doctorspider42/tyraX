#include <tyra>
#include "bag_array.gen.hpp"
#include <memory>
#include <vector>
#include <array>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <limits>
using namespace Tyra;
using Vehicle_playground::BagArray;
namespace Vehicle_playground {unsigned g_contentStamp=0;}
unsigned g_bboxStamp=0;
#include "night_ablation.hpp"
struct LightPool {
 BagArray<Vec4> verts,sts;Color color{20,40,80,128};
 std::unique_ptr<StaPipInfoBag> info=std::make_unique<StaPipInfoBag>();
 std::unique_ptr<StaPipTextureBag> texBag=std::make_unique<StaPipTextureBag>();
};
struct PoolBatch {
 BagArray<Vec4> verts,sts;BagArray<Color> colors;M4x4 mat;
 std::unique_ptr<StaPipInfoBag> info;std::unique_ptr<StaPipColorBag> colorBag;
 std::unique_ptr<StaPipTextureBag> texBag;std::unique_ptr<StaPipBag> bag;
 std::vector<const LightPool*> members;std::vector<float> memberFix;
 std::vector<unsigned> key,lastKey;
};
struct Capture {std::vector<unsigned char> v,s,c;unsigned calls=0,count=0;};
template<class T> std::vector<unsigned char> bytes(const T* p,size_t n){auto b=reinterpret_cast<const unsigned char*>(p);return n?std::vector<unsigned char>(b,b+n*sizeof(T)):std::vector<unsigned char>{};}
struct Renderer {Capture saved;void render(StaPipBag* b){++saved.calls;saved.count=b->count;saved.v=bytes(b->vertices,b->count);saved.s=bytes(b->texture->coordinates,b->count);saved.c=bytes(b->color->many,b->count);}};
struct Base {PoolBatch poolBatch_;struct {Renderer core;} stapip;void add(const LightPool& b,float f){poolBatch_.members.push_back(&b);poolBatch_.memberFix.push_back(f);}};
struct Original:Base{void poolBatchFlush();};struct Candidate:Base{void poolBatchFlush();};
#include "original.inc"
#include "candidate.inc"
unsigned checks=0,cases=0,colorOnly=0,geometry=0,unchanged=0,empty=0;
void need(bool b,const char* m){++checks;if(!b)throw std::runtime_error(m);}
struct Snap{unsigned vs,ss,cs,bbox;const void* vp;const void* sp;};
Snap snapshot(Candidate& c){auto& p=c.poolBatch_;return {p.verts.stamp(),p.sts.stamp(),p.colors.stamp(),p.bag?p.bag->bboxVersion:0,p.verts.data(),p.sts.data()};}
void fill(LightPool& p,unsigned n,unsigned seed){p.verts.clear();p.sts.clear();for(unsigned k=0;k<n;++k){p.verts.push_back(Vec4(float(seed+k),float(k*2),float(k*3),1));p.sts.push_back(Vec4(float(k)/13,float(k)/17,1,0));}}
void trial(Original& a,Candidate& b,std::array<LightPool,4>& pools,const std::vector<unsigned>& members,const std::vector<float>& fixes,int expect){
 ++cases;need(members.size()==fixes.size(),"valid member input");auto old=snapshot(b);auto before=b.stapip.core.saved.calls;
 std::vector<unsigned char> ev,es,ec;
 for(size_t i=0;i<members.size();++i){const auto& p=pools[members[i]];a.add(p,fixes[i]);b.add(p,fixes[i]);auto v=bytes(p.verts.data(),p.verts.size()),s=bytes(p.sts.data(),p.sts.size());ev.insert(ev.end(),v.begin(),v.end());es.insert(es.end(),s.begin(),s.end());float f=fixes[i]/128.f;Color col(p.color.r*f,p.color.g*f,p.color.b*f,128.f);for(size_t k=0;k<p.verts.size();++k){auto c=bytes(&col,1);ec.insert(ec.end(),c.begin(),c.end());}}
 a.poolBatchFlush();b.poolBatchFlush();auto now=snapshot(b);
 need(a.stapip.core.saved.calls==b.stapip.core.saved.calls,"call parity");need(a.poolBatch_.members.empty()&&b.poolBatch_.members.empty()&&a.poolBatch_.memberFix.empty()&&b.poolBatch_.memberFix.empty(),"clear members");
 if(members.empty()){++empty;need(before==b.stapip.core.saved.calls,"empty no submit");need(old.vs==now.vs&&old.ss==now.ss&&old.cs==now.cs&&old.bbox==now.bbox&&old.vp==now.vp&&old.sp==now.sp,"empty stable");return;}
 need(a.stapip.core.saved.v==b.stapip.core.saved.v,"exact vertices");need(a.stapip.core.saved.s==b.stapip.core.saved.s,"exact ST");need(a.stapip.core.saved.c==b.stapip.core.saved.c,"exact colors all16bytes");need(b.stapip.core.saved.v==ev&&b.stapip.core.saved.s==es&&b.stapip.core.saved.c==ec,"independent member-order output");
 need(b.poolBatch_.bag->contentVersion==b.poolBatch_.verts.stampPtr()&&b.poolBatch_.texBag->contentVersion==b.poolBatch_.sts.stampPtr()&&b.poolBatch_.colorBag->contentVersion==b.poolBatch_.colors.stampPtr(),"actual BagArray binds");
 need(b.poolBatch_.colorBag->single==nullptr&&b.poolBatch_.bag->count==ev.size()/16&&b.poolBatch_.info->additiveBlendFix==128,"bag render shape");
 if(expect==0){++unchanged;need(now.vs==old.vs&&now.ss==old.ss&&now.cs==old.cs&&now.bbox==old.bbox,"unchanged no stamps");}
 if(expect==1){++colorOnly;need(now.vs==old.vs&&now.ss==old.ss&&now.bbox==old.bbox&&now.vp==old.vp&&now.sp==old.sp,"color-only preserves geometry bbox binding");need(now.cs!=old.cs,"color stamp invalidates baked content key");}
 if(expect==2){++geometry;need(now.vs!=old.vs&&now.ss!=old.ss&&now.cs!=old.cs&&now.bbox!=old.bbox,"geometry refresh all streams bbox");}
}
int main(){try{
 NightAblation::setPoolColorSplitCandidate(true);
 Original a;Candidate b;std::array<LightPool,4> p;for(unsigned i=0;i<4;++i)fill(p[i],3+i*3,10+i*7);
 trial(a,b,p,{}, {},0);trial(a,b,p,{0,1},{64,96},2);trial(a,b,p,{0,1},{64,96},0);
 for(unsigned i=0;i<100;++i){p[0].color.r=float(1+i);trial(a,b,p,{0,1},{64,96},1);trial(a,b,p,{0,1},{float(i),96},i==64?0:1);trial(a,b,p,{0,1},{float(i),96},0);}
 // identity, order, duplicate membership, vertex count, source geometry/ST, scene reset.
 trial(a,b,p,{1,0},{96,64},2);trial(a,b,p,{0,0,1},{64,32,96},2);trial(a,b,p,{2},{128},2);
 p[2].verts[0].x+=1;trial(a,b,p,{2},{128},2);p[2].sts[0].x+=1;trial(a,b,p,{2},{128},2);fill(p[2],30,90);trial(a,b,p,{2},{128},2);
 auto captured=b.stapip.core.saved.c;p[2].color.b=90;trial(a,b,p,{2},{128},1);need(captured!=b.stapip.core.saved.c,"copied previous capture unaffected");
 trial(a,b,p,{}, {},0);trial(a,b,p,{2},{128},0);a.poolBatch_=PoolBatch{};b.poolBatch_=PoolBatch{};trial(a,b,p,{2},{128},2);
 p[2]=LightPool{};fill(p[2],9,222);trial(a,b,p,{2},{128},2);
 // finite extremals/signed zero/NaN/Inf: exact old/new host arithmetic, not PS2FP proof.
 for(float value:{0.f,-0.f,255.f,-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){p[2].color.g=value;trial(a,b,p,{2},{128},-1);}
 Vehicle_playground::g_contentStamp=0xfffffff0u;fill(p[2],30,444);trial(a,b,p,{2},{128},2);p[2].color.r=5.f;trial(a,b,p,{2},{128},1);
 fill(p[2],0,0);trial(a,b,p,{2},{128},2);fill(p[2],9,22);trial(a,b,p,{2},{128},2);
 // Same-ELF selector changes real branch behavior without resetting caches.
 NightAblation::setPoolColorSplitCandidate(false);p[2].color.r=11.f;trial(a,b,p,{2},{128},2);
 NightAblation::setPoolColorSplitCandidate(true);p[2].color.r=12.f;trial(a,b,p,{2},{128},1);
 // Cold actual dual-arm function calls bind witnesses to the executed source branch.
 {Original coldOld;Candidate coldNew;NightAblation::collectCounters=true;NightAblation::poolCacheCounter=NightAblation::PoolCacheCounter{};NightAblation::setPoolColorSplitCandidate(false);
 trial(coldOld,coldNew,p,{0},{64},2);p[0].color.r=333.f;trial(coldOld,coldNew,p,{0},{64},2);
 need(NightAblation::poolCacheCounter.flushes==2&&NightAblation::poolCacheCounter.initial==1&&NightAblation::poolCacheCounter.geometryChanged==1&&NightAblation::poolCacheCounter.colorOnlyEligible==1&&NightAblation::poolCacheCounter.geometryRebuilds==2&&NightAblation::poolCacheCounter.colorOnlyUpdates==0,"actual baseline cold branch witnesses");
 NightAblation::setPoolColorSplitCandidate(true);p[0].color.r=334.f;trial(coldOld,coldNew,p,{0},{64},1);
 need(NightAblation::poolCacheCounter.flushes==3&&NightAblation::poolCacheCounter.geometryRebuilds==2&&NightAblation::poolCacheCounter.colorOnlyUpdates==1&&NightAblation::poolCacheCounter.eligibleBatchVerts==p[0].verts.size()*3,"actual candidate cold branch witnesses");NightAblation::collectCounters=false;}
 // Observer must be byte-inactive for every reason when collection is false.
 using namespace NightAblation;poolCacheCounter=PoolCacheCounter{};collectCounters=false;
 for(unsigned mode=0;mode<2;++mode){setPoolColorSplitCandidate(mode!=0);for(unsigned reason=0;reason<6;++reason){auto before=poolCacheCounter;observePoolCache(static_cast<PoolReason>(reason),true,999);need(std::memcmp(&before,&poolCacheCounter,sizeof before)==0,"observer no writes outside cold frame");}}
 collectCounters=true;valid=true;poolCacheCounter=PoolCacheCounter{};setPoolColorSplitCandidate(false);
 observePoolCache(PoolGeometryChanged,true,3);observePoolCache(PoolUnchanged,false,3);observePoolCache(PoolColorOnly,false,3);
 need(poolCacheCounter.flushes==3&&poolCacheCounter.initial==1&&poolCacheCounter.geometryChanged==1&&poolCacheCounter.unchanged==1&&poolCacheCounter.colorOnlyEligible==1&&poolCacheCounter.geometryRebuilds==2&&poolCacheCounter.colorOnlyUpdates==0&&poolCacheCounter.eligibleBatchVerts==9&&valid,"baseline reason and action partition");
 poolCacheCounter=PoolCacheCounter{};setPoolColorSplitCandidate(true);
 observePoolCache(PoolGeometryChanged,true,3);observePoolCache(PoolUnchanged,false,3);observePoolCache(PoolColorOnly,false,3);
 need(poolCacheCounter.geometryRebuilds==1&&poolCacheCounter.colorOnlyUpdates==1&&poolCacheCounter.flushes==3&&valid,"candidate action partition");
 std::vector<unsigned> key{1,2,3,4,5,6,7};need(poolReason(key,{})==PoolGeometryChanged,"initial classify");need(poolReason(key,key)==PoolUnchanged,"equal classify");auto changed=key;changed[3]++;need(poolReason(key,changed)==PoolColorOnly,"RGB only classify");changed=key;changed[6]++;need(poolReason(key,changed)==PoolColorOnly,"FIX only classify");for(unsigned i=0;i<3;++i){changed=key;changed[i]++;need(poolReason(key,changed)==PoolGeometryChanged,"geometry classify");}
 need(poolReason({},key)==PoolInvalid&&poolReason({1},key)==PoolInvalid&&poolReason(key,{1})==PoolInvalid,"malformed key guards");
 observePoolCache(PoolInvalid,false,1);need(!valid&&poolCacheCounter.invalid==1,"invalid rejects");valid=true;poolCacheCounter=PoolCacheCounter{};observePoolCache(PoolColorOnly,true,1);need(!valid&&poolCacheCounter.invalid==1,"initial shape rejects");
 valid=true;poolCacheCounter=PoolCacheCounter{};poolCacheCounter.flushes=0xffffffffu;observePoolCache(PoolUnchanged,false,1);need(!valid&&poolCacheCounter.invalid==1,"counter overflow rejects");
 valid=true;poolCacheCounter=PoolCacheCounter{};poolCacheCounter.eligibleBatchVerts=0xffffffffu;observePoolCache(PoolUnchanged,false,1);need(!valid&&poolCacheCounter.invalid==1,"vertex total overflow rejects");
 collectCounters=false;valid=true;poolCacheCounter=PoolCacheCounter{};setPoolColorSplitCandidate(true);
 // randomized valid sequences include changing geometry and empty transitions.
 unsigned rng=1234567;for(unsigned i=0;i<1000;++i){rng=rng*1664525u+1013904223u;unsigned n=rng%5;std::vector<unsigned> members;std::vector<float> fixes;for(unsigned k=0;k<n;++k){rng=rng*1664525u+1013904223u;unsigned index=rng%4;members.push_back(index);fixes.push_back(float((rng>>16)%256));p[index].color.r=float((rng>>8)%200);if(i%13==0)fill(p[index],3*((rng%8)+1),rng%100);}trial(a,b,p,members,fixes,-1);}
 std::cout<<"PASS cases="<<cases<<" checks="<<checks<<" colorOnly="<<colorOnly<<" geometry="<<geometry<<" unchanged="<<unchanged<<" empty="<<empty<<"\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
