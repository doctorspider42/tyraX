namespace Tyra {
bool StaPipQBufferRenderer::compileRoadPackages(StaPipBag* bag,RoadPrepared::Owner& result,u32 capacity) {
#if !TYRA_VIF1_QUEUE
  return false;
#endif
  if(!worldEligible(bag) || !bag->contentVersion || !bag->color->contentVersion ||
     (bag->texture && !bag->texture->contentVersion) || !ClipRing::active ||
     !vu1Clipping || hasProgramOverrides())return false;
  try {
    worldPool.collect();
    prim_t local=*prim;local.antialiasing=bag->info->antiAliasingEnabled;
    local.blending=bag->info->blendingEnabled;local.shading=bag->info->shadingType;
    local.fogging=rendererCore->fog.enabled && !bag->info->fogDisabled?DRAW_ENABLE:DRAW_DISABLE;
    result.primitive=worldPrimKey(local,bag);
    std::vector<WorldBody::Quad> geometry;
    const unsigned budget=std::min<std::uint32_t>(65535U,worldPool.remainingQwords());
    unsigned reservation=0;
    const unsigned streams=bag->texture?3:2;
    for(const auto& job:result.plan.jobs){
      const unsigned cull=streams*(job.count+1);
      if(cull>budget-reservation)return false;reservation+=cull;
      for(const auto& range:job.clips){
        const unsigned clip=streams*(range.count+1);
        if(clip>budget-reservation)return false;reservation+=clip;
      }
    }
    geometry.reserve(reservation);
    auto append=[&](unsigned first,unsigned count,bool clip,unsigned parity){
      RoadPrepared::Fragment fragment;
      fragment.sdk=RoadPackageSdk::compile(bag,clip?getClipProgramByBag(bag):getCullProgramByBag(bag),
        local,worldEpoch,bufferSize,first,count,clip,parity,65535);
      fragment.first=first;fragment.offset=geometry.size();fragment.qwords=fragment.sdk.geometry.size();
      if(fragment.qwords>budget-geometry.size())throw std::length_error("road prepared geometry budget");
      geometry.insert(geometry.end(),fragment.sdk.geometry.begin(),fragment.sdk.geometry.end());
      std::vector<WorldBody::Quad>().swap(fragment.sdk.geometry);
      return fragment;
    };
    for(const auto& job:result.plan.jobs){
      RoadPrepared::Package p;p.cull=append(job.first,job.count,false,0);
      for(const auto& range:job.clips)p.clips.push_back(append(range.first,range.count,true,range.parity));
      result.packages.push_back(std::move(p));
    }
    geometry.shrink_to_fit();
    result.key=worldKey(bag,worldEpoch,getCullProgramByBag(bag)->getDestinationAddress(),local);
    result.key.generation=1;
    result.geometry.reset(new WorldRuntime::Owner(worldPool));
    if(!result.geometry->publish(result.key,std::move(geometry)))return false;
    result.vertices=bag->vertices;result.colors=bag->color->many;
    result.sts=bag->texture?bag->texture->coordinates:nullptr;
    result.capacity=capacity;result.bufferSize=bufferSize;
    result.selected.resize(result.packages.size());
    return true;
  }catch(const std::bad_alloc&){return false;}
   catch(const std::length_error&){return false;}
}
bool StaPipQBufferRenderer::roadPackagesReady(StaPipBag* bag,const RoadPrepared::Owner& owner) {
  u32 depth;memcpy(&depth,&RendererCoreDepth::scale,4);
  return owner.geometry && owner.geometry->admit(owner.key) &&
    owner.key.programEpoch==worldEpoch && owner.key.depthScale==depth &&
    owner.bufferSize==bufferSize && owner.primitive==worldPrimKey(*prim,bag) &&
    owner.key.vertices==bag->count && owner.key.run==bag->packageSize &&
    owner.key.bboxVersion==bag->bboxVersion &&
    owner.vertices==bag->vertices && owner.colors==bag->color->many &&
    owner.sts==(bag->texture?bag->texture->coordinates:nullptr) &&
    owner.key.content[0]==worldStamp(bag->contentVersion) &&
    owner.key.content[1]==worldStamp(bag->color->contentVersion) &&
    owner.key.content[2]==worldStamp(bag->texture?bag->texture->contentVersion:nullptr);
}
void StaPipQBufferRenderer::emitRoadPackage(packet2_t* packet,StaPipQBuffer* buffer,
                                           StaPipVU1Program* program,bool start) {
  auto& fragment=*buffer->roadFragment;auto& owner=*buffer->roadOwner;
  const auto* geometry=owner.geometry->admit(owner.key);
  TYRA_ASSERT(geometry && fragment.sdk.address==program->getDestinationAddress() &&
    fragment.sdk.count==buffer->size && fragment.offset+fragment.qwords<=geometry->size(),
    "Road prepared fragment changed after admission");
  const unsigned wireStart=packet2_get_qw_count(packet);
  const auto head=RoadPackageSdk::liveHead(fragment.sdk,fragment.sdk.clip?buffer->clipPlaneMask:0,start);
  TYRA_ASSERT(packet->tag_opened_at==nullptr,"Road fragment starts inside an open DMA tag");
  qword_t cnt;cnt.dw[0]=0x10000003;cnt.dw[1]=0;emitQword(packet,cnt);
  memcpy(packet->next,head.data(),sizeof(head));packet2_advance_next(packet,sizeof(head));
  emitRef(packet,geometry->data()+fragment.offset,fragment.qwords);
  cnt.dw[0]=0x10000001;emitQword(packet,cnt);
  memcpy(packet->next,&fragment.sdk.kicks[start],sizeof(RoadPackageSdk::Quad));
  packet2_advance_next(packet,sizeof(RoadPackageSdk::Quad));
  static bool wireChecked=false;
  if(!wireChecked){
    std::unique_ptr<packet2_t,WorldCellSdk::PacketDeleter> reference(
      packet2_create(64,P2_TYPE_NORMAL,P2_MODE_CHAIN,true));
    TYRA_ASSERT(reference,"Road transport reference allocation failed");
    packet2_reset(reference.get(),true);
    auto resolve=[](unsigned address,unsigned count,void*)->const RoadPackageSdk::Quad*{
      if(!count || count>65535 || address<0x100000 || (address&15) || address>0x02000000U-count*16)return nullptr;
      return reinterpret_cast<const RoadPackageSdk::Quad*>(address);
    };
    program->addBufferDataToPacket(reference.get(),buffer,prim,start);
    if(start)packet2_utils_vu_add_start_program(reference.get(),program->getDestinationAddress());
    else packet2_utils_vu_add_continue_program(reference.get());
    const auto ordinary=WorldBody::flatten(reference->base,packet2_get_qw_count(reference.get()),resolve);
    std::vector<WorldBody::Quad> rebuilt(head.begin(),head.end());
    rebuilt.insert(rebuilt.end(),geometry->begin()+fragment.offset,geometry->begin()+fragment.offset+fragment.qwords);
    rebuilt.push_back(fragment.sdk.kicks[start]);
    TYRA_ASSERT(rebuilt==ordinary,"Road live prepared bytes differ from actual ordinary writer");
    std::vector<WorldBody::Quad> expected{WorldBody::Quad{{0,0,0,0}}};
    expected.insert(expected.end(),ordinary.begin(),ordinary.begin()+3);
    expected.push_back(WorldBody::Quad{{0,0,0,0}});
    expected.insert(expected.end(),ordinary.begin()+3,ordinary.end()-1);
    expected.push_back(WorldBody::Quad{{0,0,0,0}});expected.push_back(ordinary.back());
    const auto delivered=WorldBody::flatten(packet->base+wireStart,packet2_get_qw_count(packet)-wireStart,resolve);
    TYRA_ASSERT(delivered==expected,"Road actual CNT/REF transport differs from ordinary VIF stream");
    wireChecked=true;
    std::printf("ROADWIRE count=%u sourceWriter=1 transport=1 outsidePrice=1\n",buffer->size);std::fflush(stdout);
  }
  lastProgramName=program->getName();++RoadPrepared::emitted;
  if(roadPacketOwners.empty() || roadPacketOwners.back()!=&owner)roadPacketOwners.push_back(&owner);
  buffer->roadFragment=nullptr;buffer->roadOwner=nullptr;
}
}
