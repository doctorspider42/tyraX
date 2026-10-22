void Candidate::poolBatchFlush() {
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
  const auto poolReasonResult = NightAblation::poolReason(pb.key, pb.lastKey);
  if (NightAblation::collectCounters) {
    size_t poolVertices = 0;
    bool poolVertexOverflow = false;
    for (const LightPool* member : pb.members) {
      if (member->verts.size() > 0xffffffffu - poolVertices) {
        poolVertexOverflow = true;
        break;
      }
      poolVertices += member->verts.size();
    }
    NightAblation::observePoolCache(poolVertexOverflow ? NightAblation::PoolInvalid : poolReasonResult,
                                  pb.lastKey.empty(), poolVertices);
  }
  if (NightAblation::poolColorSplitCandidate) {
    if (pb.key != pb.lastKey) {
      // PRIVATE candidate: the existing member key separates geometry identity
      // and source stamps (words 0..2) from RGB/FIX (words 3..6).
      const bool geometryChanged = poolReasonResult != NightAblation::PoolColorOnly;
      if (geometryChanged) {
        pb.verts.clear();
        pb.sts.clear();
      }
      pb.colors.clear();
      for (size_t m = 0; m < pb.members.size(); ++m) {
        const LightPool& b = *pb.members[m];
        // Keep the original arithmetic and member/vertex order for every color.
        const float f = pb.memberFix[m] / 128.0F;
        const Tyra::Color c(b.color.r * f, b.color.g * f, b.color.b * f, 128.0F);
        const BagArray<Tyra::Vec4>& cv = b.verts;
        const BagArray<Tyra::Vec4>& cs = b.sts;
        for (size_t k = 0; k < cv.size(); ++k) {
          if (geometryChanged) {
            pb.verts.push_back(cv[k]);
            pb.sts.push_back(cs[k]);
          }
          pb.colors.push_back(c);
        }
      }
      if (geometryChanged) {
        pb.verts.bind(pb.bag);
        pb.bag->count = (u32)pb.verts.size();
        pb.sts.bind(pb.texBag);
        pb.bag->bboxVersion = ++g_bboxStamp;
      }
      pb.colors.bind(pb.colorBag);
      pb.colorBag->single = nullptr;
      pb.lastKey = pb.key;
    }
  } else {
    if (pb.key != pb.lastKey) {
      pb.verts.clear();
      pb.sts.clear();
      pb.colors.clear();
      for (size_t m = 0; m < pb.members.size(); ++m) {
        const LightPool& b = *pb.members[m];
        // The old pass drew Cs * FIX / 128 with Cs = tex * colour / 128; the
        // batch draws at FIX 128, so FIX / 128 moves into the colour.
        const float f = pb.memberFix[m] / 128.0F;
        const Tyra::Color c(b.color.r * f, b.color.g * f, b.color.b * f, 128.0F);
        const BagArray<Tyra::Vec4>& cv = b.verts;
        const BagArray<Tyra::Vec4>& cs = b.sts;
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
    }
  }
  (NightAblation::submitted(NightAblation::LightEffects), NightAblation::extraSubmitted(NightAblation::Pools), stapip.core.render(pb.bag.get()));
  pb.members.clear();
  pb.memberFix.clear();
}