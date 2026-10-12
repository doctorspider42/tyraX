static ObjectHead::Facts hotHeadFacts(const lod_t* lod,const PipelineInfoBag* info,
    u32 epoch,u32 shape,u32 zMethod,const ObjectHead::ClipReference& ref){
  ObjectHead::Facts f;f.epoch=epoch;f.shape=shape;
  f.fixed[0]=lod->calculation;f.fixed[1]=lod->max_level;
  f.fixed[2]=lod->mag_filter;f.fixed[3]=lod->min_filter;
  f.fixed[4]=lod->mipmap_select;f.fixed[5]=lod->l;
  memcpy(&f.fixed[6],&lod->k,4);
  f.fixed[7]=u32(info->zTestType);f.fixed[8]=info->dateLit;f.fixed[9]=zMethod;
  f.fixed[10]=info->additiveBlendFix;f.fixed[11]=info->subtractiveBlendFix;
  if(shape&ObjectHead::Clip)for(unsigned n=0;n<4;++n)f.fixed[12+n]=ref.tag.w[n];
  return f;
}
