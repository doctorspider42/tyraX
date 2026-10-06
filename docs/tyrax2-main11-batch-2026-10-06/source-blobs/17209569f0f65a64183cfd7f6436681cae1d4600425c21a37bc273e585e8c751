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
// libsd: endedMask() reads the SPU2's ENDX register in every build, and the
// debug voice-state dump below reads the rest of them.
#include <libsd.h>
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

  // One line per sample, at load time: which file reached SPU2 RAM, how big it
  // was and under which id audsrv filed it (the id is this pointer - that is
  // audsrv's own convention, and it is what a later "audsrv does not know this
  // sample" would be about).
  TYRA_LOG("ADPCM loaded: ", t_path, " (", (int)size, " bytes, pitch ",
           result->pitch, ", id ", (int)(long)result, ")");

  return result;
}

audsrv_adpcm_t* AudioAdpcm::load(const std::string& t_path) {
  return load(t_path.c_str());
}

AdpcmResult AudioAdpcm::tryPlay(audsrv_adpcm_t* t_adpcm) {
  return tryPlay(t_adpcm, -1);
}

// Added by TyraX: "the sound just does not play" is the hardest audio bug to
// see, because every way it fails is silent - a sample that never loaded, a
// channel still busy, an id audsrv does not know. Report each distinct cause
// ONCE per channel (a sound emitter retriggers every frame; a log line per
// frame over ps2link would cost more than the sound). Debug builds only.
static void logAdpcmFailure(const s8& t_ch, const char* why, int res) {
#ifndef NDEBUG
  // Bit per channel, per reason. Channel -1 (any free channel) uses bit 63.
  static u64 seenNull = 0, seenBusy = 0, seenErr = 0;
  const int bit = t_ch < 0 ? 63 : t_ch;
  u64* mask = why[0] == 'n' ? &seenNull : (why[0] == 'b' ? &seenBusy : &seenErr);
  if (*mask & (1ULL << bit)) return;
  *mask |= 1ULL << bit;
  TYRA_ERROR("ADPCM not played on channel ", (int)t_ch, ": ", why,
             " (audsrv returned ", res, ")");
#else
  (void)t_ch;
  (void)why;
  (void)res;
#endif
}

// Added by TyraX: what the SPU2 is ACTUALLY set to for a voice, logged once
// per channel at its first successful play. A sound that plays but cannot be
// heard has a dozen causes and every one of them is a register: the voice's
// own volume, its envelope, whether it is routed into the dry mix (VMIXL/R) or
// only the reverb (VMIXEL/R), whether the core passes voices through at all
// (MMIX) and where in sound RAM the sample sits. Reading them costs an RPC
// each, which is why this is once per channel and debug-only - but it turns
// "there is no sound" into a diff between a channel that works and one that
// does not.
static void logVoiceState(const s8& t_ch) {
#ifndef NDEBUG
  static u64 seen = 0;
  if (t_ch < 0 || t_ch >= (s8)AUDSRV_ADPCM_CHANNELS) return;
  if (seen & (1ULL << t_ch)) return;
  seen |= 1ULL << t_ch;

  const int core = AUDSRV_ADPCM_CH_CORE(t_ch);
  const int v = AUDSRV_ADPCM_CH_VOICE(t_ch);
  const u16 voice = (u16)SD_VOICE(core, v);
  TYRA_LOG("SPU2 channel ", (int)t_ch, " = core ", core, " voice ", v,
           ": voll ", sceSdGetParam(voice | SD_VPARAM_VOLL), " volr ",
           sceSdGetParam(voice | SD_VPARAM_VOLR), " pitch ",
           sceSdGetParam(voice | SD_VPARAM_PITCH), " adsr1 ",
           sceSdGetParam(voice | SD_VPARAM_ADSR1), " adsr2 ",
           sceSdGetParam(voice | SD_VPARAM_ADSR2), " ssa ",
           sceSdGetAddr(voice | SD_VADDR_SSA));
  TYRA_LOG("SPU2 core ", core, ": vmixl ",
           sceSdGetSwitch((u16)(core | SD_SWITCH_VMIXL)), " vmixr ",
           sceSdGetSwitch((u16)(core | SD_SWITCH_VMIXR)), " vmixel ",
           sceSdGetSwitch((u16)(core | SD_SWITCH_VMIXEL)), " vmixer ",
           sceSdGetSwitch((u16)(core | SD_SWITCH_VMIXER)), " mmix ",
           sceSdGetParam((u16)(core | SD_PARAM_MMIX)), " mvoll ",
           sceSdGetParam((u16)(core | SD_PARAM_MVOLL)), " evoll ",
           sceSdGetParam((u16)(core | SD_PARAM_EVOLL)));
#else
  (void)t_ch;
#endif
}

// Added by TyraX: the one write of the SPU2 pitch register. audsrv has no pitch
// call, but the voice a channel maps to is fixed and libsd is already linked
// here (endedMask reads ENDX in every build), so retuning a playing voice is a
// single register write. Used by vehicle engine sound (docs/vehicles.md).
//
// It is a BLOCKING RPC - see logVoiceState above, where that cost is the reason
// reading these registers is debug-only. Callers must write only on a real
// change.
void AudioAdpcm::setPitch(const s8& t_ch, const u16& t_pitch) {
  if (t_ch < 0 || t_ch >= (s8)AUDSRV_ADPCM_CHANNELS) return;
  const int core = AUDSRV_ADPCM_CH_CORE(t_ch);
  const int v = AUDSRV_ADPCM_CH_VOICE(t_ch);
  // 14-bit register; anything above 0x3FFF wraps into a much LOWER pitch, which
  // sounds like the engine suddenly dropping an octave rather than like a clamp.
  const u16 pitch = t_pitch > 0x3FFF ? (u16)0x3FFF : t_pitch;
  sceSdSetParam((u16)(SD_VOICE(core, v) | SD_VPARAM_PITCH), pitch);
}

AdpcmResult AudioAdpcm::tryPlay(audsrv_adpcm_t* t_adpcm, const s8& t_ch) {
  // Modified by TyraX: load() now returns nullptr for samples that
  // failed to load (e.g. too large for SPU2); treat that as a benign no-op
  // instead of dereferencing it.
  if (!t_adpcm) {
    logAdpcmFailure(t_ch, "no sample (it failed to load)", 0);
    return AdpcmResult::ADPCM_ERROR;
  }
  int res = audsrv_ch_play_adpcm(t_ch, t_adpcm);
  if (res >= 0) {
    // Modified by TyraX: audsrv reports "this sample is not loaded" as a
    // POSITIVE AUDSRV_ERR_ARGS (5), which no sign test can tell from a channel
    // number - so a sample the IOP does not know about used to read as a
    // successful play and simply made no sound. A requested channel must come
    // back as itself.
    if (t_ch >= 0 && res != t_ch) {
      logAdpcmFailure(t_ch, "audsrv does not know this sample", res);
      return AdpcmResult::ADPCM_ERROR;
    }
    logVoiceState((s8)res);
    return AdpcmResult::ADPCM_OK;
  } else if (res == -AUDSRV_ERR_NO_MORE_CHANNELS) {
    // Normal for a retriggering emitter (the sample is still playing), so this
    // one is reported once per channel and never again.
    logAdpcmFailure(t_ch, "busy - the previous sample is still playing", res);
    if (t_ch < 0) {
      return AdpcmResult::ADPCM_NO_FREE_CHANNELS;
    } else {
      return AdpcmResult::ADPCM_CHANNEL_USED;
    }
  } else {
    logAdpcmFailure(t_ch, "error from audsrv", res);
    return AdpcmResult::ADPCM_ERROR;
  }
}

// Added by TyraX - see the header. The flag rides in the channel number, so
// this is the same RPC tryPlay makes with one bit set.
AdpcmResult AudioAdpcm::forcePlay(audsrv_adpcm_t* t_adpcm, const s8& t_ch) {
  if (!t_adpcm) {
    logAdpcmFailure(t_ch, "no sample (it failed to load)", 0);
    return AdpcmResult::ADPCM_ERROR;
  }
  if (t_ch < 0) return tryPlay(t_adpcm, t_ch);  // nothing to force

  const int res =
      audsrv_ch_play_adpcm((int)t_ch | AUDSRV_ADPCM_FORCE, t_adpcm);
  if (res == (int)t_ch) {
    logVoiceState(t_ch);  // once per channel, like tryPlay
    return AdpcmResult::ADPCM_OK;
  }
  // A forced play has no "busy" answer left, so anything but the channel
  // itself is a real error (an unknown sample, or an audsrv too old to know
  // the flag - which answers with the ordinary busy refusal).
  logAdpcmFailure(t_ch, "forced play refused", res);
  return AdpcmResult::ADPCM_ERROR;
}

// Added by TyraX - see the header.
u32 AudioAdpcm::endedMask(const int& t_core) {
  // libsd encodes the core as the low bit of the register selector - the same
  // (core | SD_...) shape AudioReverb uses; there is no SD_CORE_n constant in
  // its public header (audsrv defines its own privately).
  return sceSdGetSwitch((u16)((t_core ? 1 : 0) | SD_SWITCH_ENDX)) & 0x00FFFFFFu;
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
