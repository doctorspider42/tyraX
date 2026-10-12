bool StaPipQBufferRenderer::compileCompleteWorldCell(
    WorldCellComplete::Cell& cell,const std::vector<StaPipBag*>& bags) {
  if(bags.empty() || bags.size()>64 || !ClipRing::active || !vu1Clipping ||
     hasProgramOverrides() || rendererCore->spot.enabled || rendererCore->dynLightCount ||
     cell.key.generation==UINT64_MAX)return false;
  const u64 textureEpoch=rendererCore->texture.getWorldTextureEpoch();
  M4x4 identity;identity.identity();
  std::vector<WorldCellPlan::PassSource> sources;
  std::vector<WorldCellSdk::Binding> bindings;
  for(auto* b:bags){
    if(!b || !b->count || !b->vertices || !b->info || !b->info->model ||
       memcmp(b->info->model->data,identity.data,sizeof(identity.data)) ||
       !b->color || !b->color->many || b->color->single || b->lighting || b->billboard ||
       b->info->transformationType!=TyraMVP || !b->info->fullClipChecks ||
       (b->texture && (!b->texture->texture || !b->texture->coordinates ||
                       b->texture->coordinatesAreNormals)))return false;
    const bool textured=b->texture!=nullptr;
    if(textured && !rendererCore->texture.worldTextureSlot(b->texture->texture->id).id)
      return false;
    const auto topology=b->stripped?WorldCellPlan::Topology::IndependentStrips:
                                   WorldCellPlan::Topology::Triangles;
    prim_t primitive=*prim;
    primitive.type=PRIM_TRIANGLE;
    primitive.antialiasing=b->info->antiAliasingEnabled;
    primitive.blending=b->info->blendingEnabled;primitive.shading=b->info->shadingType;
    primitive.fogging=rendererCore->fog.enabled && !b->info->fogDisabled;
    WorldCellSdk::Binding binding{b,getClipProgramByBag(b),primitive,
      {textureEpoch,u32(sources.size())},worldEpoch,bufferSize,true};
    WorldCellPlan::PassSource source;
    source.count=b->count;source.run=b->stripped?b->packageSize:0;
    source.capacity=WorldCellSdk::capacity(binding,topology);
    source.textured=textured;source.topology=topology;source.material=binding.material;
    source.streams={WorldCellPlan::Input{reinterpret_cast<const WorldCellPlan::Quad*>(b->vertices),b->count},
      WorldCellPlan::Input{reinterpret_cast<const WorldCellPlan::Quad*>(b->color->many),b->count},
      WorldCellPlan::Input{textured?reinterpret_cast<const WorldCellPlan::Quad*>(b->texture->coordinates):nullptr,
                           textured?b->count:0}};
    sources.push_back(source);bindings.push_back(binding);
  }
  worldPool.collect();
  const u32 budget=std::min(u32(65535),u32(worldPool.remainingQwords()));
  if(!budget)return false;
  try{
    auto source=WorldCellPlan::compile(sources,cell.key.generation+1,u64(budget)*16);
    auto compiled=WorldCellSdk::compile(*source,bindings,u64(budget)*16);
    // Decode actual emitted geometry before publishing a complete owner.
    for(unsigned p=0;p<source->passes.size();++p){
      const auto& input=source->passes[p];const auto& output=compiled.passes[p];
      const auto& commands=output.commands;size_t at=0;
      for(const auto& part:input.parts){
        const unsigned n=part.streams[0].size(),streams=input.textured?3:2;
        const unsigned length=4+streams*(n+1);
        if(at>commands.size() || length>commands.size()-at ||
           !(commands[at]==WorldCellPlan::Quad{{0,0,0x01000101,0x6c02c000}}) ||
           commands[at+1].words[3]!=(n|(63U<<10)) ||
           ((commands[at+2].words[1]>>15)&7U)!=PRIM_TRIANGLE ||
           (input.topology==WorldCellPlan::Topology::IndependentStrips &&
            commands[at+2].words[3]!=(2U|part.parity)))return false;
        for(unsigned s=0;s<streams;++s){
          const unsigned pos=at+3+s*(n+1);
          const WorldCellPlan::Quad unpack{{0,0,0x01000101,0x6c00c000|(n<<16)|(2+s*n)}};
          const unsigned logical=s==0?0:(!input.textured || s==2?1:2);
          if(!(commands[pos]==unpack) || memcmp(commands.data()+pos+1,
                 part.streams[logical].data(),n*16))return false;
        }
        const WorldCellPlan::Quad kick{{0,0,0x11000000,at?0x17000000:
                                                 (0x14000000|output.programAddress)}};
        if(!(commands[at+length-1]==kick))return false;
        at+=length;
      }
      if(at!=commands.size() || output.material.slot!=input.material.slot ||
         output.material.generation!=input.material.generation ||
         output.programEpoch!=worldEpoch)return false;
    }
    std::vector<WorldBody::Quad> body;
    auto* packet=packet2_create(64,P2_TYPE_NORMAL,P2_MODE_CHAIN,true);
    if(!packet)return false;
    std::unique_ptr<packet2_t,WorldCellSdk::PacketDeleter> ownedPacket(packet);
    auto resolve=[](unsigned address,unsigned count,void*)->const WorldBody::Quad*{
      if(!count || address<0x100000 || (address&15) ||
         address>0x02000000U-count*16)return nullptr;
      return reinterpret_cast<const WorldBody::Quad*>(address);
    };
    texwrap_t firstWrap=RendererCoreGS::repeatWrap(),lastWrap=firstWrap;
    u32 wrapPrefix=0;
    for(unsigned p=0;p<bags.size();++p){
      auto* b=bags[p];packet2_reset(packet,true);
      const auto& wrap=b->texture?*b->texture->texture->getWrapSettings():
                                  RendererCoreGS::repeatWrap();
      if(p==0 || !RendererCoreGS::wrapEquals(lastWrap,wrap)){
        packet2_chain_open_cnt(packet,0,0,0);
        packet2_vif_flush(packet,0);
        packet2_vif_open_direct(packet,0);
        packet2_add_2x_s64(packet,GIF_SET_TAG(1,1,0,0,GIF_FLG_PACKED,1),GIF_REG_AD);
        packet2_add_2x_s64(packet,GS_SET_CLAMP(wrap.horizontal,wrap.vertical,wrap.minu,
          wrap.maxu,wrap.minv,wrap.maxv),GS_REG_CLAMP_1);
        packet2_vif_close_direct_auto(packet);packet2_chain_close_tag(packet);
        if(p==0){firstWrap=wrap;wrapPrefix=WorldBody::flatten(packet->base,
          packet2_get_qw_count(packet),resolve).size();}
      }
      lastWrap=wrap;
      // This must remain even when the first optional CLAMP prefix is skipped.
      packet2_chain_open_cnt(packet,0,0,0);packet2_vif_flushe(packet,0);
      packet2_vif_pad128(packet);packet2_chain_close_tag(packet);
      lod_t filter=*lod;
      filter.mag_filter=b->info->textureMappingType==TyraLinear?LOD_MAG_LINEAR:LOD_MAG_NEAREST;
      filter.min_filter=b->info->textureMappingType==TyraLinear?LOD_MIN_LINEAR:LOD_MIN_NEAREST;
      alignas(16) qword_t state[3];unsigned n=0;
      state[n].dw[0]=GS_SET_TEX1(filter.calculation,filter.max_level,filter.mag_filter,
        filter.min_filter,filter.mipmap_select,filter.l,int(filter.k*16));
      state[n++].dw[1]=GS_REG_TEX1;
      const int date=b->info->dateLit?1:0;
      if(b->info->zTestType==PipelineZTest_AllPass)
        state[n].dw[0]=GS_SET_TEST(0,0,0,0,date,0,0,ZTEST_METHOD_ALLPASS);
      else if(b->info->zTestType==PipelineZTest_TestOnly)
        state[n].dw[0]=GS_SET_TEST(DRAW_ENABLE,ATEST_METHOD_ALLFAIL,0,ATEST_KEEP_ZBUFFER,
          date,DRAW_DISABLE,DRAW_ENABLE,rendererCore->gs.zBuffer.method);
      else state[n].dw[0]=GS_SET_TEST(DRAW_ENABLE,ATEST_METHOD_NOTEQUAL,0,ATEST_KEEP_ALL,
          date,DRAW_DISABLE,DRAW_ENABLE,rendererCore->gs.zBuffer.method);
      state[n++].dw[1]=GS_REG_TEST;
      if(b->texture){
        const auto slot=rendererCore->texture.worldTextureSlot(b->texture->texture->id);
        const auto* t=slot.core;const auto* c=slot.clut;
        state[n].dw[0]=GS_SET_TEX0(t->address>>6,t->width>>6,t->psm,t->info.width,t->info.height,
          t->info.components,b->texture->textureFunction,c?c->address>>6:0,c?c->psm:0,
          CLUT_STORAGE_MODE1,0,c?CLUT_LOAD:CLUT_NO_LOAD);
        state[n++].dw[1]=GS_REG_TEX0;
      }
      // FLUSHE ends the previous program before absolute material writes.
      packet2_utils_vu_add_unpack_data(packet,VU1_OPTIONS_ADDR+1,state,n,false);
      alignas(16) qword_t alpha;
      const u8 sub=b->info->subtractiveBlendFix,fix=b->info->additiveBlendFix;
      alpha.dw[0]=sub?GS_SET_ALPHA(2,0,2,1,sub):fix?GS_SET_ALPHA(0,2,2,1,fix):
                                                 GS_SET_ALPHA(0,1,0,1,0);
      alpha.dw[1]=GS_REG_ALPHA;
      packet2_utils_vu_add_unpack_data(packet,VU1_ALPHA_ADDR,&alpha,1,false);
      auto head=WorldBody::flatten(packet->base,packet2_get_qw_count(packet),resolve);
      const auto& geometry=compiled.passes[p].commands;
      if(head.size()+geometry.size()>budget-body.size())return false;
      body.insert(body.end(),head.begin(),head.end());
      body.insert(body.end(),geometry.begin(),geometry.end());
    }
    body.shrink_to_fit();
    auto key=WorldOwner::Key{};
    key.generation=cell.key.generation+1;key.programEpoch=worldEpoch;
    key.programAddress=compiled.passes[0].programAddress;
    key.vertices=bags[0]->count;key.run=3;
    memcpy(&key.depthScale,&RendererCoreDepth::scale,4);
    if(!cell.owner)cell.owner.reset(new WorldRuntime::Owner(worldPool));
    if(!cell.owner->publish(key,std::move(body)))return false;
    cell.key=key;cell.textureEpoch=textureEpoch;cell.fogEnabled=rendererCore->fog.enabled;
    cell.lastProgram=bindings.back().program->getName();cell.firstWrap=firstWrap;
    cell.lastWrap=lastWrap;cell.wrapPrefixQwords=wrapPrefix;
    return true;
  }catch(const std::exception&){return false;}
}

bool StaPipQBufferRenderer::beginCompleteWorldView(){
  if(!ClipRing::active || !vu1Clipping || hasProgramOverrides() ||
     rendererCore->spot.enabled || rendererCore->dynLightCount)return false;
  flushPendingPacket();
  auto* packet=packets[context];packet2_reset(packet,false);packetWrapSet=false;
  emitQword(packet,flusheHeader());
  const auto& vp=rendererCore->renderer3D.getViewProj();
  emitUnpack(packet,VU1_MVP_MATRIX_ADDR,vp.data,4);
  addClipChain(packet);
  alignas(16) qword_t options{};
  float scale=rendererCore->fog.scale,offset=rendererCore->fog.offset;
  u32 bits;memcpy(&bits,&scale,4);
  if(bits && !(bits&65535)){bits|=1;memcpy(&scale,&bits,4);}
  memcpy(&options.sw[2],&scale,4);memcpy(&options.sw[3],&offset,4);
  emitUnpack(packet,VU1_OPTIONS_ADDR,&options,1);
  objectDataPending=true;
  return true;
}

bool StaPipQBufferRenderer::renderCompleteWorldCell(WorldCellComplete::Cell& cell){
  u32 depth;memcpy(&depth,&RendererCoreDepth::scale,4);
  if(!ClipRing::active || !vu1Clipping || hasProgramOverrides() ||
     !cell.owner || cell.key.programEpoch!=worldEpoch || cell.key.depthScale!=depth ||
     cell.textureEpoch!=rendererCore->texture.getWorldTextureEpoch() ||
     cell.fogEnabled!=rendererCore->fog.enabled || rendererCore->spot.enabled ||
     rendererCore->dynLightCount)return false;
  const auto* body=cell.owner->admit(cell.key);
  if(!body)return false;
  auto* packet=packets[context];
  if(!objectDataPending){packet2_reset(packet,false);objectDataPending=true;}
  const auto& currentWrap=packetWrapSet?packetWrap:rendererCore->gs.currentTextureWrap();
  const u32 skip=RendererCoreGS::wrapEquals(currentWrap,cell.firstWrap)?cell.wrapPrefixQwords:0;
  if(skip>=body->size())return false;
  emitRef(packet,body->data()+skip,u32(body->size())-skip);
  packetWrap=cell.lastWrap;packetWrapSet=true;
  lastProgramName=cell.lastProgram;++submissionBatchBags;
  flushPendingPacket();
  cell.owner->submitted(cell.key,worldLastSequence,worldLastCopied);
  submissionTextureReadersOutstanding=true;
  return true;
}
