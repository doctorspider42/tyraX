/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#include "audio/audio.hpp"
#include "debug/debug.hpp"
#include "thread/threading.hpp"
#include <kernel.h>
#include <malloc.h>

extern void* _gp;

void audioThread(Tyra::Audio* audio) {
  while (true) {
    audio->work();
    audio->checkStackGuard();
  }
}

namespace Tyra {

// Modified by TyraX: was 2 KB, which held only while this thread never logged.
// AudioSong::work() runs here and, since the low-ring warning (#253), can call
// TYRA_WARN - an ostringstream plus a host: write, several KB of stack. The
// overflow ran DOWN into whatever the heap had placed below the stack: in
// examples/showcase that was Path1's draw-finish packet, so the next PATH1
// barrier sent VIF1 a saved register frame ("Vif1: Unknown VifCmd! [47]" in
// PCSX2, a frozen game ~16 s into the scene). 16 KB matches the song
// streamer's thread. 16-byte aligned because the EE saves 128-bit registers
// with sq, which faults on a misaligned stack.
const u32 Audio::threadStackSize = 16 * 1024;

namespace {
// The lowest words of the stack: an overflow writes them first.
constexpr u32 kStackGuardWords = 16;
constexpr u32 kStackGuardValue = 0x5AFE57AC;
}  // namespace

Audio::Audio() {
  threadStack = static_cast<u8*>(memalign(16, threadStackSize));
  if (threadStack) {
    u32* guard = reinterpret_cast<u32*>(threadStack);
    for (u32 i = 0; i < kStackGuardWords; ++i) guard[i] = kStackGuardValue;
  }
}

Audio::~Audio() {
  if (threadStack) free(threadStack);  // memalign'd, so free(), not delete[]
}

void Audio::checkStackGuard() {
  if (stackGuardReported || !threadStack) return;
  const u32* guard = reinterpret_cast<const u32*>(threadStack);
  for (u32 i = 0; i < kStackGuardWords; ++i) {
    if (guard[i] != kStackGuardValue) {
      stackGuardReported = true;
      TYRA_SOFT_ERROR("Audio thread stack overflow: its guard word ", i,
                      " was overwritten. Heap memory below the stack is "
                      "corrupt - grow Audio::threadStackSize.");
      return;
    }
  }
}

void Audio::init() {
  // TyraX: the reverb binds libsd's RPC, which ends in sceSdInit() - and that
  // clears libsd's transfer callbacks, the ones audsrv's streaming ring
  // installs. So it goes FIRST; doing it afterwards silences the music.
  reverb.init();

  initAUDSRV();

  song.init();
  adpcm.init();

  // ...and the effect bit goes on only after audsrv's own sceSdInit() has
  // reset the core attributes.
  reverb.enable();

  initThread();

  TYRA_LOG("Audio initialized!");
}

void Audio::initAUDSRV() {
  int ret = audsrv_init();

  TYRA_ASSERT(ret >= 0,
              "AUDSRV returned error string: ", audsrv_get_error_string());
}

void Audio::initThread() {
  thread.gp_reg = &_gp;
  thread.func = reinterpret_cast<void*>(audioThread);
  thread.stack = threadStack;
  thread.stack_size = threadStackSize;
  thread.initial_priority = 0x5;
  threadId = CreateThread(&thread);
  TYRA_ASSERT(threadId >= 0, "Create audio thread failed!");
  StartThread(threadId, this);
}

void Audio::work() {
  Threading::switchThread();

  song.work();
}

}  // namespace Tyra
