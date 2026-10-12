// Snapshot ordinary output before/after the experimental generator change.
#include "vugen.hpp"
#include "vuasm.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
  assert(argc == 3);
  const std::filesystem::path out(argv[1]);
  assert(!std::filesystem::exists(out));
  std::filesystem::create_directories(out);
  for (const auto& d : vugen::allDescs()) {
    const auto b = vugen::build(d);
    assert(b.errors.empty());
    std::ofstream(out / (d.fileStem + ".vclpp")) << b.vclpp;
    std::ofstream(out / (d.fileStem + ".hpp")) << b.eeHeader;
    std::ofstream(out / (d.fileStem + ".cpp")) << b.eeSource;
  }
  std::cout << "PASS ordinary descriptions=" << vugen::allDescs().size() << "\n";
  const std::filesystem::path engine(argv[2]);
  vuasm::Options options;
  options.includeRoot = engine.string();
  unsigned supported = 0;
  for (const auto& d : vugen::allDescs()) {
    const auto b = vugen::buildPaintExperiment(d);
    const bool allowed = d.programEnum == "StaPipCullTextureEnv" ||
                         d.programEnum == "StaPipClipTextureColor" ||
                         d.programEnum == "StaPipClipTextureEnv";
    assert(b.errors.empty() == allowed);
    if (!allowed) { assert(b.vclpp.empty()); continue; }
    ++supported;
    assert(b.vclpp.find(".name " + d.asmName + "_PAINT\n") != std::string::npos);
    assert(b.eeSource.find(vugen::residentImage(d) + "_PAINT_CodeStart") != std::string::npos);
    const auto dest = out / d.dir;
    std::filesystem::create_directories(dest);
    std::ofstream(dest / (d.fileStem + "_paint.vclpp")) << b.vclpp;
    std::ofstream(dest / (d.fileStem + "_paint.hpp")) << b.eeHeader;
    std::ofstream(dest / (d.fileStem + "_paint.cpp")) << b.eeSource;
    vuir::Program old, parsed;
    std::string error;
    const auto path = engine / "src/renderer/3d/pipeline/static/core/programs" /
                      d.dir / (d.fileStem + ".vclpp");
    assert(vuasm::parseFile(path.string(), options, old, error));
    assert(vuasm::parseText(b.vclpp, "generated paint", options, parsed, error));
    for (const int mode : {-1, 0, 1, 2}) {
      auto input = d;
      if (d.sharedClipEnv) input = vugen::descClipTextureEnv();
      input.runtimeClipVariant = mode;
      const auto same = vugen::equivalence(old, parsed, input, 600, 71953);
      if (!same.identical) std::cerr << path << " mode=" << mode << " "
                                    << same.error << " " << same.detail << "\n";
      assert(same.ran && same.identical);
    }
    if (d.sharedClipEnv) {
      auto input = vugen::descClipTextureDirLights();
      input.runtimeClipVariant = 1;
      input.runtimeColorLane = -1;
      const auto same = vugen::equivalence(old, parsed, input, 600, 45673);
      assert(same.ran && same.identical);
      input = vugen::descClipTextureColor();
      const auto tc = vugen::equivalence(old, parsed, input, 600, 99183);
      assert(tc.ran && tc.identical);
    }
    auto custom = d;
    custom.custom = true;
    assert(!vugen::buildPaintExperiment(custom).errors.empty());
    // A real instruction mutation must make the comparison fail.
    unsigned changes = 0;
    for (auto& op : parsed.code)
      if (op.op == vuir::Op::Loi && op.fimm == 220.0f) { op.fimm = 0; ++changes; }
    assert(changes == 1);
    auto input = d.sharedClipEnv ? vugen::descClipTextureEnv() : d;
    input.runtimeClipVariant = 2;
    const auto bad = vugen::equivalence(old, parsed, input, 600, 71953);
    assert(bad.ran && !bad.identical);
    std::cout << "PASS paint=" << d.fileStem << " modes=4 negative=specular-zero\n";
  }
  assert(supported == 3);
  std::cout << "PASS compared draws=8400 unsupported=12 custom=3 negative=3\n";
}
