// Includes the copied generator to exercise its actual private bodies/stager.
#include "vugen.cpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

static std::vector<uint32_t> output(const vusim::Result& r, int tags, int regs,
                                    bool once=false) {
  assert(r.ok && !r.hitStepLimit && r.kicks.size() == r.kickMem.size());
  std::vector<uint32_t> out;
  for (size_t i = 0; i < r.kicks.size(); ++i) {
    const auto& mem = r.kickMem[i];
    const int activeTags=once && i>0?1:tags;
    const int start = r.kicks[i], prim = start + activeTags - 1;
    const int count = mem[prim * 4] & 0x7fff;
    assert(count <= 21 && count % 3 == 0);
    const int end = (start + activeTags + count * regs) * 4;
    out.insert(out.end(), mem.begin() + (start + activeTags) * 4, mem.begin() + end);
  }
  return out;
}

static bool stateEqual(const vusim::Result& actual,
                       const std::vector<std::vector<uint32_t>>& reference,
                       int tags,bool once){
  if(actual.kicks.size()!=reference.size())return false;
  std::vector<uint32_t> state;
  for(size_t t=0;t<actual.kicks.size();++t){
    const auto& mem=actual.kickMem[t];
    const int start=actual.kicks[t]*4;
    const bool full=!once || t==0;
    if(full)state.assign(mem.begin()+start,mem.begin()+start+(tags-1)*4);
    if(!std::equal(state.begin(),state.end(),reference[t].begin()))return false;
    const int prim=start+(full?tags-1:0)*4;
    if(!std::equal(mem.begin()+prim,mem.begin()+prim+4,
                   reference[t].begin()+(tags-1)*4))return false;
  }
  return true;
}

static vuir::Program folded(vuir::Program source) {
  std::vector<int> map(source.code.size(), -1);
  std::vector<vuir::Instr> kept;
  unsigned removed = 0;
  for (size_t i = 0; i < source.code.size(); ++i) {
    if (source.code[i].comment == "CalculateTyraDirectionalLights") {
      assert(i + 2 < source.code.size());
      i += 2; removed += 3;
    } else {
      map[i] = static_cast<int>(kept.size());
      kept.push_back(source.code[i]);
    }
  }
  assert(removed == 9);
  for (auto& in : kept) if (in.target >= 0) {
    assert(map[in.target] >= 0); in.target = map[in.target];
  }
  source.code = std::move(kept);
  return source;
}

int main(int argc, char** argv) {
  assert(argc == 2);
  const std::filesystem::path root(argv[1]);
  uint32_t seed = 912835;
  unsigned cases = 0, triangles = 0, nonempty = 0, clipped = 0;
  bool parityRejected = false, outputRejected = false, ringRejected = false;
  bool stateRejected=false,firstStateRejected=false;
  unsigned onceCases=0,emptyFirst=0;
  for (auto desc : {vugen::descClipColor(), vugen::descClipTextureColor()}) {
    for (const int flavor : {0, 1, 2}) {
    const bool fold = flavor != 0, paint = flavor == 2 && desc.texture;
    for (const int layout : {0, 1, 2, 3, 4, 5}) {
    const bool strip = (layout & 1) != 0, hybrid = layout >= 2;
    const bool once=layout>=4;
    vuir::Program ring;
    if(once)vugen::buildHybridOnceRingBody(desc,ring,nullptr,paint);
    else if (hybrid) vugen::buildHybridRingBody(desc, ring, nullptr, paint);
    else if (strip) vugen::buildStripRingBody(desc, ring, nullptr, paint);
    else vugen::buildRingBody(desc, ring, nullptr, paint);
    if (fold) ring = folded(ring);
    std::string suffix = hybrid ? "_ring_hybrid" : strip ? "_ring_strip" : "_ring";
    if(once)suffix+="_once";
    if (fold) suffix += "_fold";
    if (paint) suffix += "_paint";
    auto emittedDesc = desc;
    emittedDesc.asmName += suffix; emittedDesc.vclName += suffix;
    std::ofstream(root / (desc.fileStem + suffix + ".vclpp"))
        << vugen::emitVclpp(emittedDesc, ring, paint);
    auto base = paint ? vugen::buildPaintExperiment(desc) : vugen::build(desc);
    assert(base.errors.empty());
    if (fold) base.program = folded(base.program);
    const int tags = base.tagQuads, regs = base.regsPerVertex;
    for (int trial = 0; trial < 320; ++trial) {
      const int count = strip ? 3 + trial % 70 : 3 * (trial % 24 + 1);
      const int ntri = strip ? count - 2 : count / 3;
      const int top = trial % 2 ? 484 : 24;
      const int variant = (trial / 64) % 4;
      const bool lit = variant == 1 || (!desc.texture && variant == 2);
      const bool single = trial % 3 == 0 && !lit && !(paint && variant == 2);
      const int streams = 1 + desc.texture + !single;
      const int begin = top + 2 + count * streams;
      const int ringSize = tags + 21 * regs;
      assert(2 + count * streams + 2 * ringSize <= 460);
      auto inputDesc = desc;
      inputDesc.runtimeClipVariant = variant == 3 ? -1 : variant;
      inputDesc.dirLights = lit;
      inputDesc.env = desc.texture && variant == 2;
      auto input = vugen::stageInput(inputDesc, top, count, seed, single,
                                     trial % 2, nullptr, 0);
      const int parity = strip ? (trial / 2) % 2 : 0;
      input[(top + 1) * 4 + 3] = parity | (hybrid && strip ? 2u : 0u);
      // All 64 masks, including zero; alternate wide side/far crossings.
      uint32_t mask = 0;
      for (int i = 0; i < 6; ++i) mask |= ((trial >> i) & 1u) << (5-i);
      input[top * 4 + 3] = count | (mask << 10);
      if (trial % 4 == 0)
        for (int i = 0; i < count; ++i) {
          input[(top + 2 + i) * 4] = vugen::asBits(vugen::randomFloat(seed, -400000, 400000));
          input[(top + 2 + i) * 4 + 1] = vugen::asBits(vugen::randomFloat(seed, -400000, 400000));
          input[(top + 2 + i) * 4 + 2] = vugen::asBits(vugen::randomFloat(seed, -1200, 55));
        }
      vusim::Config cfg; cfg.top = top; cfg.maxSteps = 2000000;
      auto actual = vusim::run(ring, input, cfg);
      if (!actual.ok || actual.hitStepLimit) {
        std::cerr << "ring failed trial " << trial << "\n"; return 1;
      }
      assert(actual.kicks.size() == static_cast<size_t>(ntri));
      std::vector<uint32_t> expected;
      std::vector<std::vector<uint32_t>> referenceTags;
      for (int t = 0; t < ntri; ++t) {
        auto one = input;
        one[top * 4 + 3] = (input[top * 4 + 3] & ~0x3ffu) | 3;
        one[(top + 1) * 4 + 3] = 0;
        for (int stream = 0; stream < streams; ++stream)
          for (int corner = 0; corner < 3; ++corner) {
            const int local = strip && ((t + parity) & 1) && corner < 2
                                  ? 1-corner : corner;
            const int index = strip ? t + local : t*3 + corner;
            std::copy_n(input.begin() + (top + 2 + count * stream + index) * 4,
                        4, one.begin() + (top + 2 + 3 * stream + corner) * 4);
          }
        auto reference = vusim::run(base.program, one, cfg);
        auto emitted = output(reference, tags, regs);
        nonempty += !emitted.empty();
        clipped += emitted.size() > static_cast<size_t>(3 * regs * 4);
        expected.insert(expected.end(), emitted.begin(), emitted.end());
        assert(actual.kicks[t] == begin + (t % 2) * ringSize+
                                  (once && t>0?tags-1:0));
        const auto& ref = reference.kickMem[0];
        referenceTags.emplace_back(ref.begin()+reference.kicks[0]*4,
                                   ref.begin()+(reference.kicks[0]+tags)*4);
        if(once && t==0 && emitted.empty())++emptyFirst;
      }
      assert(stateEqual(actual,referenceTags,tags,once));
      const auto emitted = output(actual, tags, regs,once);
      if (emitted != expected) {
        std::cerr << "output differs " << desc.fileStem << " trial=" << trial
                  << " actual=" << emitted.size() << " expected="
                  << expected.size() << "\n"; return 1;
      }
      if (!emitted.empty() && !outputRejected) {
        auto bad = actual;
        for (size_t i = 0; i < bad.kicks.size(); ++i)
          if ((bad.kickMem[i][(bad.kicks[i] + (once && i>0?1:tags) - 1) * 4] & 0x7fff) != 0) {
            bad.kickMem[i][(bad.kicks[i] + (once && i>0?1:tags)) * 4] ^= 1;
            break;
          }
        outputRejected = output(bad, tags, regs,once) != expected;
      }
      if (strip && !parityRejected) {
        auto badInput = input;
        badInput[(top + 1) * 4 + 3] ^= 1;
        parityRejected = output(vusim::run(ring, badInput, cfg), tags, regs,once) != expected;
      }
      if (actual.kicks.size() > 2 && !ringRejected) {
        auto bad = actual;
        bad.kicks[1] = bad.kicks[0]+(once?tags-1:0);
        ringRejected = output(bad, tags, regs,once) != expected;
      }
      if(once){
        ++onceCases;
        auto bad=actual;bad.kickMem[0][bad.kicks[0]*4]^=1;
        stateRejected|=!stateEqual(bad,referenceTags,tags,true);
        bad=actual;bad.kicks[0]+=tags-1;
        firstStateRejected|=!stateEqual(bad,referenceTags,tags,true);
      }
      for (int word = top * 4; word < begin * 4; ++word)
        if (!(paint && variant == 2 && word >= (top + 2 + 2*count)*4))
          assert(actual.mem[word] == input[word]);
      ++cases; triangles += ntri;
    }
    }
    }
  }
  assert(nonempty > 0 && clipped > 0 && parityRejected && outputRejected && ringRejected);
  assert(onceCases>0 && emptyFirst>0 && stateRejected && firstStateRejected);
  std::cout << "PASS cases=" << cases << " triangles=" << triangles
            << " nonempty=" << nonempty << " fan-expanded=" << clipped
            << " exact ordered output/effective state; immutable inputs; rejection-controls=5"
            << " once-cases="<<onceCases<<" empty-first="<<emptyFirst<<"\n";
}
