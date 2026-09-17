#include "scripts/script.hpp"
#include "scripts/district_data.hpp"
#include "file/file_utils.hpp"
#include <cstdio>
namespace Vehicle_playground {
class DistrictBenchmark : public Script {
  unsigned int frame = 0;
  struct Sample { unsigned int frame, phase; float fps; } samples[32];
  int count = 0;
  unsigned int heldPhase = 3;
  bool written = false;
 public:
  void update(ScriptContext& ctx) override {
    // Read pose commands only AFTER measurement; captures must not disturb samples.
    if (frame >= 1440 && frame % 30 == 0) {
      FILE* f = std::fopen(Tyra::FileUtils::fromCwd("district-benchmark-pose.txt").c_str(), "r");
      if (f) {
        unsigned int requested = 3;
        if (std::fscanf(f, "%u", &requested) == 1 && requested < 4)
          heldPhase = requested;
        std::fclose(f);
      }
    }
    const unsigned int phase = frame < 1440 ? frame / 360 : heldPhase;
    if (ctx.saveValues && DISTRICT_NIGHT_VALUE < ctx.saveValueCount)
      ctx.saveValues[DISTRICT_NIGHT_VALUE] = (phase & 1) ? 1.0F : 0.0F;
    ctx.cameraOverride = true;
    ctx.cameraEye = phase < 2 ? Tyra::Vec4(0,4,-32,1) : Tyra::Vec4(4,9,102,1);
    ctx.cameraAt = phase < 2 ? Tyra::Vec4(0,1,-12,1) : Tyra::Vec4(65,3,106,1);
    ctx.cameraUp = Tyra::Vec4(0,1,0,0);
    if (frame < 1440 && frame % 360 >= 120 && frame % 30 == 0 && count < 32)
      samples[count++] = {frame, phase, ctx.engine->info.getFps()};
    if (frame >= 1440 && !written) {
      FILE* f = std::fopen(Tyra::FileUtils::fromCwd("district-benchmark.csv").c_str(), "w");
      if (f) {
        std::fprintf(f,"frame,phase,fps\n");
        for (int i=0;i<count;++i)
          std::fprintf(f,"%u,%u,%.3f\n",samples[i].frame,samples[i].phase,samples[i].fps);
        std::fclose(f);
        written = true;
      }
    }
    ++frame;
  }
};
}
TYRA_SCRIPT(Vehicle_playground::DistrictBenchmark);
