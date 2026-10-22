// Includes the copied generator to exercise its actual private bodies/stager.
#include "vugen.cpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

static std::vector<uint32_t> output(const vusim::Result& r, int tags, int regs) {
  assert(r.ok && !r.hitStepLimit && r.kicks.size() == r.kickMem.size());
  std::vector<uint32_t> out;
  for (size_t i = 0; i < r.kicks.size(); ++i) {
    const auto& mem = r.kickMem[i];
    const int start = r.kicks[i], prim = start + tags - 1;
    const int count = mem[prim * 4] & 0x7fff;
    assert(count <= 21 && count % 3 == 0);
    const int end = (start + tags + count * regs) * 4;
    out.insert(out.end(), mem.begin() + (start + tags) * 4, mem.begin() + end);
  }
  return out;
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
  for (auto desc : {vugen::descClipColor(), vugen::descClipTextureColor()}) {
    for (const int flavor : {0, 1, 2}) {
    const bool fold = flavor != 0, paint = flavor == 2 && desc.texture;
    for (const int layout : {0, 1, 2, 3}) {
    const bool strip = (layout & 1) != 0, hybrid = layout >= 2;
    vuir::Program ring;
    if (hybrid) vugen::buildHybridRingBody(desc, ring, nullptr, paint);
    else if (strip) vugen::buildStripRingBody(desc, ring, nullptr, paint);
    else vugen::buildRingBody(desc, ring, nullptr, paint);
    if (fold) ring = folded(ring);
    std::string suffix = hybrid ? "_ring_hybrid" : strip ? "_ring_strip" : "_ring";
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
        assert(actual.kicks[t] == begin + (t % 2) * ringSize);
        const auto& m = actual.kickMem[t];
        const auto& ref = reference.kickMem[0];
        // State tag blocks and the primitive descriptor remain identical.
        for (int q = 0; q < tags; ++q)
          for (int lane = 0; lane < 4; ++lane)
            assert(m[(actual.kicks[t] + q) * 4 + lane] ==
                   ref[(reference.kicks[0] + q) * 4 + lane]);
      }
      const auto emitted = output(actual, tags, regs);
      if (emitted != expected) {
        std::cerr << "output differs " << desc.fileStem << " trial=" << trial
                  << " actual=" << emitted.size() << " expected="
                  << expected.size() << "\n"; return 1;
      }
      if (!emitted.empty() && !outputRejected) {
        auto bad = actual;
        for (size_t i = 0; i < bad.kicks.size(); ++i)
          if ((bad.kickMem[i][(bad.kicks[i] + tags - 1) * 4] & 0x7fff) != 0) {
            bad.kickMem[i][(bad.kicks[i] + tags) * 4] ^= 1;
            break;
          }
        outputRejected = output(bad, tags, regs) != expected;
      }
      if (strip && !parityRejected) {
        auto badInput = input;
        badInput[(top + 1) * 4 + 3] ^= 1;
        parityRejected = output(vusim::run(ring, badInput, cfg), tags, regs) != expected;
      }
      if (actual.kicks.size() > 2 && !ringRejected) {
        auto bad = actual;
        bad.kicks[1] = bad.kicks[0];
        ringRejected = output(bad, tags, regs) != expected;
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
  std::cout << "PASS cases=" << cases << " triangles=" << triangles
            << " nonempty=" << nonempty << " fan-expanded=" << clipped
            << " exact ordered output/state; immutable inputs; rejection-controls=3\n";
}
