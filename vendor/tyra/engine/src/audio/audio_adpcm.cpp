/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#include "audio/audio_adpcm.hpp"
#include "debug/debug.hpp"
#include <tamtypes.h>
#include <malloc.h>
#include <kernel.h>
#include <cstdlib>
#include <audsrv.h>
#include "thread/threading.hpp"

namespace Tyra {

AudioAdpcm::AudioAdpcm() {}

AudioAdpcm::~AudioAdpcm() {}

void AudioAdpcm::init() { initAUDSRV(); }

void AudioAdpcm::initAUDSRV() {
  int ret = audsrv_adpcm_init();

  TYRA_ASSERT(ret >= 0,
              "AUDSRV returned error string:", audsrv_get_error_string());
}

void AudioAdpcm::reset() { audsrv_adpcm_init(); }

// Modified by TyraX: the upstream version read the whole file into a
// variable-length array on the EE stack (u8 data[size]) - fine for a few-KB
// one-shot, a guaranteed stack overflow for anything larger - and sized it
// with fseek/ftell, which are unreliable over the PS2 host filesystem. Read
// incrementally into a heap buffer instead, and return nullptr on any failure
// (open error, OOM, or audsrv rejecting the sample because it does not fit
// SPU2's ~2 MB sample RAM) so a bad sample degrades to silence a caller can
// detect, not a crash or a silent no-op.
audsrv_adpcm_t* AudioAdpcm::load(const char* t_path) {
  FILE* file = fopen(t_path, "rb");
  if (!file) {
    TYRA_ERROR("Failed to open ADPCM sample: ", t_path);
    return nullptr;
  }

  // No fseek/ftell: host fs reports bogus sizes. Grow the buffer as we read.
  size_t capacity = 64 * 1024;
  size_t size = 0;
  u8* data = static_cast<u8*>(malloc(capacity));
  while (data) {
    if (size == capacity) {
      size_t grownCap = capacity * 2;
      u8* grown = static_cast<u8*>(realloc(data, grownCap));
      if (!grown) {
        free(data);
        data = nullptr;
        break;
      }
      data = grown;
      capacity = grownCap;
    }
    size_t got = fread(data + size, 1, capacity - size, file);
    size += got;
    if (got == 0) break;  // EOF (or read error - size is what we managed)
  }
  fclose(file);

  if (!data) {
    TYRA_ERROR("Out of memory reading ADPCM sample: ", t_path);
    return nullptr;
  }

  auto* result = new audsrv_adpcm_t();
  result->size = 0;
  result->buffer = 0;
  result->loop = 0;
  result->pitch = 0;
  result->channels = 0;

  // audsrv_load_adpcm copies the sample into SPU2 RAM, so the host buffer is
  // only needed for the duration of the call.
  const int err = audsrv_load_adpcm(result, data, size);
  free(data);
  if (err) {
    TYRA_ERROR("AUDSRV failed to load ADPCM (too large for SPU2 RAM?): ",
               audsrv_get_error_string());
    delete result;
    return nullptr;
  }

  return result;
}

audsrv_adpcm_t* AudioAdpcm::load(const std::string& t_path) {
  return load(t_path.c_str());
}

AdpcmResult AudioAdpcm::tryPlay(audsrv_adpcm_t* t_adpcm) {
  return tryPlay(t_adpcm, -1);
}

AdpcmResult AudioAdpcm::tryPlay(audsrv_adpcm_t* t_adpcm, const s8& t_ch) {
  // Modified by TyraX: load() now returns nullptr for samples that
  // failed to load (e.g. too large for SPU2); treat that as a benign no-op
  // instead of dereferencing it.
  if (!t_adpcm) return AdpcmResult::ADPCM_ERROR;
  int res = audsrv_ch_play_adpcm(t_ch, t_adpcm);
  if (res >= 0) {
    return AdpcmResult::ADPCM_OK;
  } else if (res == -AUDSRV_ERR_NO_MORE_CHANNELS) {
    if (t_ch < 0) {
      return AdpcmResult::ADPCM_NO_FREE_CHANNELS;
    } else {
      return AdpcmResult::ADPCM_CHANNEL_USED;
    }
  } else {
    return AdpcmResult::ADPCM_ERROR;
  }
}

void AudioAdpcm::playWait(audsrv_adpcm_t* t_adpcm) { playWait(t_adpcm, -1); }

void AudioAdpcm::playWait(audsrv_adpcm_t* t_adpcm, const s8& t_ch) {
  int res = tryPlay(t_adpcm, t_ch);

  while (res == AdpcmResult::ADPCM_NO_FREE_CHANNELS ||
         res == AdpcmResult::ADPCM_CHANNEL_USED) {
    Threading::switchThread();
    res = tryPlay(t_adpcm, t_ch);
  }
}

}  // namespace Tyra
