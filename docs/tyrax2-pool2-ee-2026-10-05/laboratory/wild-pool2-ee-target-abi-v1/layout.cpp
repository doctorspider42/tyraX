#include "debug/night_sampler.hpp"
#include "debug/night_ablation.hpp"
extern "C" const unsigned night_layout[] = {sizeof(NightSampler::Sample), sizeof(NightSampler::samples), sizeof(NightSampler::Chunk), sizeof(NightSampler::chunks), sizeof(NightAblation::Counter), sizeof(NightAblation::commonCounter), sizeof(NightAblation::extraCounter), sizeof(NightAblation::WildCounter), sizeof(NightAblation::PoolTableCounter)};
