bool WorldRuntime::Spans::add(const void* p,unsigned n){return Tyra::Vif1Queue::registerImmutableSpan(p,n);}
void WorldRuntime::Spans::retire(const void* p){Tyra::Vif1Queue::retireImmutableSpan(p);}
bool WorldRuntime::Spans::collect(const void* p){return Tyra::Vif1Queue::collectRetiredImmutableSpan(p);}
void WorldRuntime::Spans::drain(){Tyra::Vif1Queue::drain();}
void WorldRuntime::Spans::waitFor(unsigned n){Tyra::Vif1Queue::waitFor(n);}

namespace Tyra {
static u32 worldPrimKey(const prim_t& p,const StaPipBag* bag){
  const u32 mapping=bag->texture && bag->texture->texture?1U:0U;
  return u32(p.type)|(u32(p.shading)<<4)|(mapping<<6)|(u32(p.fogging)<<7)|
    (u32(p.blending)<<8)|(u32(p.antialiasing)<<9)|(u32(p.mapping_type)<<10)|(u32(p.colorfix)<<11);
}
static u32 worldStamp(const u32* p){return p?*p:0U;}
static bool worldEligible(const StaPipBag* b){
  return b && b->stripped && b->packageSize>=3 && b->packageSize%3==0 &&
    b->count>=3 && (b->count%b->packageSize==0 || b->count%b->packageSize>=3) &&
    b->info && b->info->fullClipChecks && b->info->transformationType==TyraMVP &&
    b->info->frustumCulling==PipelineInfoBagFrustumCulling_Precise &&
    b->vertices && b->color && b->color->many && !b->lighting && !b->billboard &&
    (!b->texture || (b->texture->texture && b->texture->coordinates && !b->texture->coordinatesAreNormals));
}
static WorldOwner::Key worldKey(StaPipBag* b,u32 epoch,u32 address,const prim_t& p){
  WorldOwner::Key k;k.generation=b->worldGeneration;k.programEpoch=epoch;k.programAddress=address;
  k.primitive=worldPrimKey(p,b);const float scale=RendererCoreDepth::scale;
  memcpy(&k.depthScale,&scale,4);k.vertices=b->count;k.run=b->packageSize;
  k.bboxVersion=b->bboxVersion;
  k.content={worldStamp(b->contentVersion),
    worldStamp(b->color->contentVersion),worldStamp(b->texture?b->texture->contentVersion:nullptr),0};
  return k;
}
static void worldTriangle(u32& hash,const Vec4* xyz,const Vec4* colors,const Vec4* sts,u32 t,bool odd){
  const u32 corners[3]={t+u32(odd),t+u32(!odd),t+2};
  for(u32 c:corners)for(const Vec4* stream:{xyz,colors,sts})if(stream){
    u32 bits[4];memcpy(bits,stream+c,16);for(u32 v:bits)WorldRuntime::word(hash,v);
  }
}
static std::pair<u32,u32> worldSource(const StaPipBag* b){
  u32 hash=2166136261U,tris=0;
  for(u32 first=0;first<b->count;first+=b->packageSize){
    const u32 n=std::min(b->packageSize,b->count-first);
    for(u32 t=0;t+2<n;++t){
      worldTriangle(hash,b->vertices+first,reinterpret_cast<const Vec4*>(b->color->many)+first,
        b->texture?b->texture->coordinates+first:nullptr,t,t&1);++tris;
    }
  }
  return {hash,tris};
}
bool StaPipQBufferRenderer::prepareWorldGeometry(StaPipBag* bag,u32 capacity){
  if(!WorldRuntime::active && !WorldRuntime::cold)return false;
#if !TYRA_VIF1_QUEUE
  return false;
#endif
  if(!worldEligible(bag) || !ClipRing::active || !vu1Clipping || hasProgramOverrides())return false;
  if(!bag->worldOwner)bag->worldOwner.reset(new WorldRuntime::Owner(worldPool));
  bag->worldOwner->invalidate();worldPool.collect();
  if(bag->worldGeneration==UINT64_MAX)return false;
  ++bag->worldGeneration;
  prim_t localPrim=*prim;
  localPrim.antialiasing=bag->info->antiAliasingEnabled;
  localPrim.blending=bag->info->blendingEnabled;localPrim.shading=bag->info->shadingType;
  localPrim.fogging=rendererCore->fog.enabled && !bag->info->fogDisabled?DRAW_ENABLE:DRAW_DISABLE;
  if(capacity<3)return false;
  auto* program=getClipProgramByBag(bag);const u32 step=std::min(u32(72),capacity)-2;
  const u32 streams=bag->texture?3:2;
  u32 reservation=0;
  for(u32 first=0;first<bag->count;first+=bag->packageSize){
    const u32 n=std::min(bag->packageSize,bag->count-first);
    for(u32 t=0;t+2<n;t+=step){
      const u32 bytes=12+streams*(std::min(step,n-t-2)+2);
      if(bytes>65535-reservation || bytes>worldPool.remainingQwords()-reservation)return false;
      reservation+=bytes;
    }
  }
  auto* packet=packet2_create(64,P2_TYPE_NORMAL,P2_MODE_CHAIN,true);
  if(!packet)return false;
  std::vector<WorldBody::Quad> body;
  body.reserve(reservation);
  const bool consumer=ClipConsumer::observe,trial=ClipTrial::observe;
  ClipConsumer::observe=false;ClipTrial::observe=false;
  auto resolver=[](unsigned address,unsigned count,void*)->const WorldBody::Quad*{
    if(!count || count>65535 || address<0x100000 || (address&15) || address>0x02000000U-count*16)return nullptr;
    return reinterpret_cast<const WorldBody::Quad*>(address);
  };
  u32 delivered=2166136261U,triangles=0;
  for(u32 first=0;first<bag->count;first+=bag->packageSize){
    StaPipBagPackage pkg;pkg.bag=bag;pkg.size=std::min(bag->packageSize,bag->count-first);
    pkg.vertices=bag->vertices+first;pkg.colors=reinterpret_cast<const Vec4*>(bag->color->many)+first;
    pkg.sts=bag->texture?bag->texture->coordinates+first:nullptr;pkg.normals=nullptr;pkg.clipPlaneMask=63;
    for(u32 t=0;t+2<pkg.size;t+=step){
      const u32 n=std::min(step,pkg.size-t-2);StaPipQBuffer buffer;
      buffer.setMaxVertCount(n+2);buffer.fillNativeClipStrip(pkg,t,n);
      packet2_reset(packet,false);
      program->addBufferDataToPacket(packet,&buffer,&localPrim,body.empty());
      if(body.empty())packet2_utils_vu_add_start_program(packet,program->getDestinationAddress());
      else packet2_utils_vu_add_continue_program(packet);
      auto fragment=WorldBody::flatten(packet->base,packet2_get_qw_count(packet),resolver);
      const auto payload=WorldPayload::decode(fragment,n+2,bag->texture!=nullptr,t&1,
        body.empty(),program->getDestinationAddress());
      TYRA_ASSERT(memcmp(payload[0],buffer.vertices,(n+2)*16)==0 &&
        memcmp(payload[1],buffer.colors,(n+2)*16)==0 &&
        (!buffer.sts || memcmp(payload[2],buffer.sts,(n+2)*16)==0),"A31 compiled payload differs");
      for(u32 j=0;j<n;++j){
        worldTriangle(delivered,reinterpret_cast<const Vec4*>(payload[0]),
          reinterpret_cast<const Vec4*>(payload[1]),reinterpret_cast<const Vec4*>(payload[2]),j,(t+j)&1);++triangles;
      }
      TYRA_ASSERT(fragment.size()<=12+streams*(n+2),"A31 SDK fragment exceeds reservation");
      body.insert(body.end(),fragment.begin(),fragment.end());
    }
  }
  ClipConsumer::observe=consumer;ClipTrial::observe=trial;packet2_free(packet);
  const auto source=worldSource(bag);
  TYRA_ASSERT(source.first==delivered && source.second==triangles,"A31 owner source/delivery differs");
  bag->worldSourceHash=delivered;bag->worldTriangles=triangles;
  return bag->worldOwner->publish(worldKey(bag,worldEpoch,program->getDestinationAddress(),localPrim),std::move(body));
}
bool StaPipQBufferRenderer::replayPreparedWorld(StaPipBag* bag){
  if((!WorldRuntime::active && !ClipConsumer::observe) || !bag->worldOwner ||
      !worldEligible(bag) || !ClipRing::active || !vu1Clipping || hasProgramOverrides())return false;
  auto* program=getClipProgramByBag(bag);
  const auto key=worldKey(bag,worldEpoch,program->getDestinationAddress(),*prim);
  const auto* body=bag->worldOwner->admit(key);if(!body)return false;
  WorldRuntime::admitted=true;
  if(ClipConsumer::observe){
    const auto source=worldSource(bag);
    TYRA_ASSERT(source.first==bag->worldSourceHash && source.second==bag->worldTriangles,"A31 stale prepared source");
    ++WorldRuntime::sources;WorldRuntime::triangles+=source.second;
    WorldRuntime::word(WorldRuntime::sourceHash,source.first);WorldRuntime::word(WorldRuntime::sourceHash,source.second);
  }
  if(!WorldRuntime::active)return false;
  auto* currentPacket=packets[context];
  TYRA_ASSERT(objectDataPending && packet2_get_qw_count(currentPacket)+4<=packetSize,"A31 live head/REF reservation");
  emitRef(currentPacket,body->data(),static_cast<u32>(body->size()));
  lastProgramName=program->getName();++submissionBatchBags;
  flushPendingPacket();
  bag->worldOwner->submitted(key,worldLastSequence,worldLastCopied);
  if(ClipTrial::observe){++WorldRuntime::trialDraws;WorldRuntime::trialQwords+=body->size();}
  if(ClipConsumer::observe)++WorldRuntime::draws;
  return true;
}
}
