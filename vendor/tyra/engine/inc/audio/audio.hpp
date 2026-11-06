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

  /** Modified by TyraX: how long the audio thread sleeps between polls while
   * no song plays (ms). 0 restores the old 0.5 ms poll. */
  u32 idleSleepMs = 16;

 private:
  ee_thread_t thread;
  int threadId;
  static const u16 threadStackSize;
  u8* threadStack;

  void initAUDSRV();
  void initThread();
};

}  // namespace Tyra
