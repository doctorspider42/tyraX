    // A31 cold-only actual-writer qualification. No priced-frame path changes.
    if (ClipConsumer::observe && buffers[i]->coarseClipStrip &&
        buffers[i]->bag->color->many && !buffers[i]->bag->lighting &&
        !buffers[i]->bag->billboard &&
        (!buffers[i]->bag->texture ||
         !buffers[i]->bag->texture->coordinatesAreNormals)) {
      auto* original = buffers[i];
      TYRA_ASSERT(original->nativeClipStrip, "A31 requires native input");
      std::array<std::vector<WorldBody::Quad>,3> source;
      const void* raw[3]={original->vertices,original->colors,original->sts};
      const bool textured=original->bag->texture!=nullptr;
      for(unsigned stream=0;stream<(textured?3U:2U);++stream){
        source[stream].resize(original->size);
        memcpy(source[stream].data(),raw[stream],original->size*sizeof(Vec4));
      }
      WorldStripPlan::Source input{
        {WorldStripPlan::Input{source[0].data(),original->size},
         WorldStripPlan::Input{source[1].data(),original->size},
         WorldStripPlan::Input{source[2].data(),textured?original->size:0U}},
        original->size,((original->size+2)/3)*3,72,textured};
      auto prepared=WorldStripPlan::build(input,1);
      TYRA_ASSERT(prepared->slices.size()==1,"A31 unexpected body partition");
      const auto& owned=prepared->slices.front();
      StaPipBagPackage pkg;
      pkg.bag=original->bag;pkg.size=original->size;
      pkg.vertices=original->vertices;pkg.colors=original->colors;
      pkg.sts=original->sts;pkg.normals=nullptr;
      pkg.clipPlaneMask=original->clipPlaneMask;
      StaPipQBuffer copy;copy.setMaxVertCount(original->size);
      copy.fillNativeClipStrip(pkg,0,original->size-2);
      copy.nativeClipParity=original->nativeClipParity;
      copy.vertices=reinterpret_cast<Vec4*>(const_cast<WorldBody::Quad*>(owned.streams[0].data()));
      copy.colors=reinterpret_cast<Vec4*>(const_cast<WorldBody::Quad*>(owned.streams[1].data()));
      copy.sts=textured?reinterpret_cast<Vec4*>(const_cast<WorldBody::Quad*>(owned.streams[2].data())):nullptr;
      auto* temporary=packet2_create(64,P2_TYPE_NORMAL,P2_MODE_CHAIN,true);
      TYRA_ASSERT(temporary,"A31 compiler packet allocation");
      auto resolver=[](unsigned address,unsigned count,void*)->const WorldBody::Quad*{
        if(!count || count>65535 || address<0x100000 || (address&15) ||
           address>0x02000000U-count*16)return nullptr;
        return reinterpret_cast<const WorldBody::Quad*>(address);
      };
      auto emit=[&](StaPipQBuffer* b){
        packet2_reset(temporary,false);
        program->addBufferDataToPacket(temporary,b,prim,emitState);
        if(emitState)packet2_utils_vu_add_start_program(temporary,program->getDestinationAddress());
        else packet2_utils_vu_add_continue_program(temporary);
        return WorldBody::flatten(temporary->base,packet2_get_qw_count(temporary),resolver);
      };
      const auto reference=emit(original);
      const bool consumerObserve=ClipConsumer::observe,trialObserve=ClipTrial::observe;
      ClipConsumer::observe=false;ClipTrial::observe=false;
      auto compiled=emit(&copy);
      ClipConsumer::observe=consumerObserve;ClipTrial::observe=trialObserve;
      packet2_free(temporary);
      TYRA_ASSERT(compiled==reference,"A31 compiled SDK body differs");
      TYRA_ASSERT(compiled.size()>0 && compiled.size()<=65535,"A31 body REF bound");
      WorldBody::words+=static_cast<unsigned>(compiled.size()*4);
      ++WorldBody::checks;
      // Diagnostic storage is reset only after the cold arm's full drain.
      WorldBody::storage.push_back(std::move(compiled));
      const auto& body=WorldBody::storage.back();
      emitRef(currentPacket,body.data(),static_cast<unsigned>(body.size()));
      lastProgramName=program->getName();
      continue;
    }
