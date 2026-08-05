/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: SPU2 hardware reverb for sound effects.
*/

#include "audio/audio_reverb.hpp"
#include "debug/debug.hpp"
#include <libsd.h>

namespace Tyra {

// audsrv puts every ADPCM voice AND the streamed music on SPU2 core 1
// (core 0 is left muted), so this is the only core with anything to reverb.
static const int kCore = 1;

// libsd's sceSdInit() flag. 0 = cold: reset the whole SPU2. Anything else
// keeps the registers but STILL clears libsd's transfer callbacks, which is
// why this only ever runs before audsrv comes up (see init()).
static const int kInitCold = 0;

// MMIX (per core) routes the two non-voice sources into the dry and wet
// buses. libsd's own init leaves ALL of them on, music included - so the
// music is wet by default and has to be taken out explicitly.
//   bit 4 MSNDER / bit 5 MSNDEL - core input (the streamed music) -> reverb
//   bit 6 MSNDR  / bit 7 MSNDL  - core input -> dry
// (Confirmed against libsd's own block-transfer handler, which clears bits
// 6/7 - the dry pair - when a stream ends.)
static const u16 kMmixMusicToEffect = (1 << 4) | (1 << 5);

// All 24 ADPCM channels.
static const u32 kAllChannels = 0x00FFFFFF;

AudioReverb::AudioReverb() {
  isAvailable = false;
  isEnabled = false;
  preset = Off;
  delay = 0;
  feedback = 0;
  sendMask = kAllChannels;
  musicSend = false;
}

AudioReverb::~AudioReverb() {}

void AudioReverb::init() {
  // sceSdInit() is what binds the EE->IOP RPC; every other sceSd* call here
  // uses the client it left behind. It also clears libsd's transfer
  // callbacks, so running it after audsrv_init() would stop the music - the
  // caller (Audio::init) keeps this first on purpose.
  //
  // A note on failure: the EE stub spins waiting for the RPC server, so this
  // hangs rather than returns if ps2snd.irx is missing. The IRX loader
  // asserts on that load, which is the guard.
  int ret = sceSdInit(kInitCold);
  if (ret < 0) {
    TYRA_SOFT_ERROR("SPU2 reverb unavailable: sceSdInit() failed");
    isAvailable = false;
    return;
  }

  isAvailable = true;
}

void AudioReverb::enable() {
  if (!isAvailable) return;

  // audsrv's own sceSdInit() ran between init() and here and reset the core
  // attributes, so the effect bit is off again. It has to go on BEFORE the
  // first preset is set: libsd only zeroes the reverb work area when effects
  // were already enabled, and an unzeroed work area circulates as noise.
  sceSdSetCoreAttr(kCore | SD_CORE_EFFECT_ENABLE, 1);
  isEnabled = true;

  // Silence first - the preset below is applied at depth 0 so nothing is
  // heard while the work area is being cleared.
  setDepth(0, 0);

  applyPreset(true);
  applySendMask();
  setMusicSend(musicSend);
}

void AudioReverb::setPreset(const Preset& t_preset) {
  if (preset == t_preset) return;

  preset = t_preset;
  if (!isAvailable || !isEnabled) return;

  applyPreset(true);
}

void AudioReverb::setDepth(const s16& t_left, const s16& t_right) {
  if (!isAvailable) return;

  // EVOLL/EVOLR is the reverb's return level. Writing it directly is exactly
  // what sceSdSetEffectAttr does with attr->depth_*, minus the work-area
  // clear - which is what makes a per-frame ramp affordable.
  sceSdSetParam(kCore | SD_PARAM_EVOLL, static_cast<u16>(t_left));
  sceSdSetParam(kCore | SD_PARAM_EVOLR, static_cast<u16>(t_right));
}

void AudioReverb::setDelay(const u8& t_delay) {
  if (delay == t_delay) return;

  delay = t_delay > 127 ? 127 : t_delay;
  if (!isAvailable || !isEnabled) return;

  // Delay and feedback are carried by the attribute struct, so they can only
  // be changed by re-sending the preset. No work-area clear: the algorithm
  // did not change, only its timing.
  applyPreset(false);
}

void AudioReverb::setFeedback(const u8& t_feedback) {
  if (feedback == t_feedback) return;

  feedback = t_feedback > 127 ? 127 : t_feedback;
  if (!isAvailable || !isEnabled) return;

  applyPreset(false);
}

void AudioReverb::setChannelSend(const s8& t_ch, const bool& t_on) {
  if (t_ch < 0 || t_ch > 23) return;

  u32 bit = 1u << static_cast<u32>(t_ch);
  setSendMask(t_on ? (sendMask | bit) : (sendMask & ~bit));
}

void AudioReverb::setSendMask(const u32& t_mask) {
  u32 masked = t_mask & kAllChannels;
  if (sendMask == masked) return;

  sendMask = masked;
  if (!isAvailable) return;

  applySendMask();
}

void AudioReverb::setMusicSend(const bool& t_on) {
  musicSend = t_on;
  if (!isAvailable) return;

  u16 mmix = sceSdGetParam(kCore | SD_PARAM_MMIX);
  if (t_on)
    mmix |= kMmixMusicToEffect;
  else
    mmix &= static_cast<u16>(~kMmixMusicToEffect);

  sceSdSetParam(kCore | SD_PARAM_MMIX, mmix);
}

void AudioReverb::applyPreset(const bool& t_clearWorkArea) {
  sceSdEffectAttr attr;
  attr.core = kCore;
  attr.mode = static_cast<int>(preset);
  // The depth is owned by setDepth() - a preset change must not undo a ramp
  // that is in progress, so re-read what the hardware currently has.
  attr.depth_L = static_cast<short>(sceSdGetParam(kCore | SD_PARAM_EVOLL));
  attr.depth_R = static_cast<short>(sceSdGetParam(kCore | SD_PARAM_EVOLR));
  attr.delay = delay;
  attr.feedback = feedback;

  // SD_EFFECT_MODE_CLEAR zeroes the work area of the OLD mode and then of the
  // new one. It is a write of up to 96 KB of SPU2 RAM over the IOP, so it
  // belongs to a preset change and nothing else.
  if (t_clearWorkArea) attr.mode |= SD_EFFECT_MODE_CLEAR;

  sceSdSetEffectAttr(kCore, &attr);
}

void AudioReverb::applySendMask() {
  // Per-voice wet send. libsd's init turns every voice's send ON, so this
  // normally REMOVES voices from the reverb rather than adding them. The dry
  // path (VMIXL/VMIXR) is left alone - a voice out of the reverb still plays.
  sceSdSetSwitch(kCore | SD_SWITCH_VMIXEL, sendMask);
  sceSdSetSwitch(kCore | SD_SWITCH_VMIXER, sendMask);
}

}  // namespace Tyra
