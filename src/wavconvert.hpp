#pragma once

#include <filesystem>
#include <string>

// WAV inspection + in-place conversion used by the audio import/rescan flow.
namespace wavconvert {

// Reads the fmt chunk. Returns false when the file is not a parseable
// RIFF/WAVE. audioFormat: 1 = integer PCM (the only thing the PS2 side
// streams), 3 = float, others = compressed. WAVE_FORMAT_EXTENSIBLE is
// resolved to the real format tag.
bool readFormat(const std::string& path, int& audioFormat, int& channels,
                int& sampleRate, int& bitsPerSample);

// Rewrites a WAV as 16-bit integer PCM at targetRate (0 = keep the source
// rate) IN PLACE - imported and hand-dropped assets get fixed where they are,
// so the project never stores a second copy. Decodes integer PCM
// (8/16/24/32-bit) and 32-bit float, mono or stereo; downsampling
// box-averages the source span per output sample (a crude but alias-safe
// low-pass), upsampling interpolates linearly. toMono averages the channels
// (halves the PS2 streaming byte rate). On failure the original file is
// left untouched and `error` says why.
bool convertTo16(const std::filesystem::path& path, int targetRate,
                 std::string& error, bool toMono = false);

}  // namespace wavconvert
