/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022-2022, Tyra - https://github.com/h4570/tyrav2
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
# Wellinator Carvalho <wellcoj@gmail.com>
*/

#pragma once

#include "./audio_listener_ref.hpp"
#include "./adpcm_result.hpp"
#include <audsrv.h>
#include <string>

namespace Tyra {

/**
 * Class responsible for playing ADPCM samples.
 * ADPCM sample can't be stopped.
 */
class AudioAdpcm {
 public:
  AudioAdpcm();
  ~AudioAdpcm();

  void init();

  /**
   * Load ADPCM sample.
   * ADPCM sample is an output from "Convert WAV to ADPCM" task (adpenc tool).
   * Adpenc expects 22kHz 16bit file.
   * @param t_path Example: "host:hit.adpcm" or "host:folder/jump.adpcm"
   */
  audsrv_adpcm_t* load(const char* t_path);
  audsrv_adpcm_t* load(const std::string& t_path);

  /**
   * Frees up all memory taken by samples, and stops all voices from
   * being played.
   */
  void reset();

  /**
   * Try play ADPCM sample if channel(s) is not occupied.
   * @param t_adpcm ADPCM data, created by load();
   * @param t_ch Channel (0-47: 0-23 on SPU2 core 1, 24-47 on core 0 - see
   *             vendor/tyra/audsrv). Type -1 for use any free channel.
   */
  AdpcmResult tryPlay(audsrv_adpcm_t* t_adpcm);
  AdpcmResult tryPlay(audsrv_adpcm_t* t_adpcm, const s8& t_ch);

  /**
   * Play on a channel EVEN IF it is still busy - the voice restarts.
   *
   * Added by TyraX. tryPlay() refuses a busy channel, which is right for a
   * sound that must not cut itself off but wrong for the usual reason to pin a
   * channel in the first place (a footstep, a UI beep, a weapon: the new one
   * replaces the old one rather than being dropped). Keying a playing voice is
   * something the SPU2 does happily; the refusal was audsrv's own check.
   * @param t_ch Channel (0-47). A negative channel has nothing to force and
   *             falls back to tryPlay's "any free voice".
   */
  AdpcmResult forcePlay(audsrv_adpcm_t* t_adpcm, const s8& t_ch);

  /**
   * The 24 voices of one SPU2 core that have FINISHED their sample, as a bit
   * per voice (bit N = that core's voice N is free).
   *
   * Added by TyraX: the only way to ask "is this channel still playing"
   * without guessing, and the input to any priority/stealing scheme - a voice
   * that has ended is free for the taking and needs no steal at all. One IOP
   * RPC per call, so ask when the bank is contended, never per frame.
   * @param t_core 1 = channels 0-23, 0 = channels 24-47.
   */
  u32 endedMask(const int& t_core);

  /**
   * Play ADPCM sample, if channel is occupied, wait for it.
   * If not used properly, can hugely reduce performance.
   * @param t_adpcm ADPCM data, created by load();
   * @param t_ch Channel (0-47). Type -1 for use any free channel.
   */
  void playWait(audsrv_adpcm_t* t_adpcm);
  void playWait(audsrv_adpcm_t* t_adpcm, const s8& t_ch);

  /**
   * Set ADPCM volume (centered - equal left/right).
   * @param t_vol Value 0-100
   * @param t_ch Channel (0-47)
   */
  void setVolume(const u8& t_vol, const s8& t_ch) {
    audsrv_adpcm_set_volume(t_ch, t_vol);  // 2-arg macro -> centered (pan 0)
  }

  /**
   * Set ADPCM volume with stereo panning.
   * @param t_vol Value 0-100
   * @param t_pan -100 (full left) .. 0 (center) .. 100 (full right)
   * @param t_ch Channel (0-47)
   */
  // Added by TyraX: positional stereo for sound emitters. Uses the forked
  // audsrv's audsrv_adpcm_set_volume_and_pan (vendor/tyra/audsrv/README.md).
  void setVolumeAndPan(const u8& t_vol, const s8& t_pan, const s8& t_ch) {
    audsrv_adpcm_set_volume_and_pan(t_ch, t_vol, t_pan);
  }

  /**
   * Retune a PLAYING voice - the SPU2 pitch register.
   *
   * Added by TyraX for vehicle engine sound (docs/vehicles.md): a continuous
   * note whose pitch tracks the engine speed is one looping sample plus this,
   * and audsrv has no pitch call of its own. 0x1000 is the rate the sample was
   * encoded at; the register is 14 bits, so 0x3FFF (~4x) is the ceiling.
   *
   * COSTS A BLOCKING IOP RPC (sceSdSetParam -> SifCallRpc with no callback), so
   * a caller must write it only when the value actually CHANGES rather than
   * every frame - see the note beside logVoiceState in the .cpp, which is why
   * reading these registers is debug-only and once per channel.
   * @param t_ch    Channel (0-47).
   * @param t_pitch 0x1000 = the sample's own rate. Clamped to 0x3FFF.
   */
  void setPitch(const s8& t_ch, const u16& t_pitch);

  /**
   * The pitch register a freshly loaded sample plays at, i.e. what setPitch
   * must be measured against. audsrv reports it from the sample's own header.
   */
  static u16 naturalPitch(const audsrv_adpcm_t* t_adpcm) {
    if (!t_adpcm || t_adpcm->pitch <= 0) return 0x1000;
    return (u16)t_adpcm->pitch;
  }

 private:
  void initAUDSRV();
};

}  // namespace Tyra
