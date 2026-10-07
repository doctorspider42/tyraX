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

#include "./audio_adpcm.hpp"
#include "./audio_reverb.hpp"
#include "./audio_song.hpp"

namespace Tyra {

/** Class responsible for audio. */
class Audio {
 public:
  Audio();
  ~Audio();

  void init();

  AudioSong song;
  AudioAdpcm adpcm;

  /** SPU2 hardware reverb on the sound-effect core. Added by TyraX. */
  AudioReverb reverb;

  void work();

  /** Added by TyraX: reports (once) a write past the bottom of the audio
   * thread's stack. Called by the audio thread after every work(). */
  void checkStackGuard();

 private:
  ee_thread_t thread;
  int threadId;
  static const u32 threadStackSize;
  u8* threadStack;
  bool stackGuardReported = false;

  void initAUDSRV();
  void initThread();
};

}  // namespace Tyra
