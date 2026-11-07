/*
# TyraX addition - see inc/debug/devlink.hpp and docs/devlink.md.
*/

#include "debug/devlink.hpp"
#include "debug/debug.hpp"
#include "irx/irx_loader.hpp"

#include <kernel.h>
#include <loadfile.h>
#include <stdio.h>
#include <string.h>

extern unsigned char devlink_irx[];
extern int size_devlink_irx;

namespace Tyra {

namespace {

// Release-audit marker: this string in an ELF means devkit code shipped.
const char kDevkitMarker[] __attribute__((used)) = "TXDEVKIT-devlink";

// The Remote Pad mailbox. 64-byte aligned so it owns its cache line outright:
// the IOP writes it by DMA behind the cache's back, and a dirty line here
// written back later would overwrite a datagram with stale bytes. It is
// cleared, written back and invalidated once in start(), and from then on
// only ever READ, through the uncached segment.
unsigned char padBox[64] __attribute__((aligned(64)));

const unsigned int kPadMagic = 0x44505854U;  // "TXPD"
const unsigned int kPadVersion = 1U;
const unsigned int kFooterXor = 0x5A5A5A5AU;

inline volatile unsigned int* uncached(void* p) {
  return reinterpret_cast<volatile unsigned int*>(
      (reinterpret_cast<unsigned int>(p) & 0x1FFFFFFFU) | 0x20000000U);
}

}  // namespace

int DevLink::state = 0;

bool DevLink::active() { return state > 0; }

bool DevLink::start() {
  if (state != 0) return state > 0;
  state = -1;
  if (!IrxLoader::keepIopResident) return false;

  // Leave no dirty line over the mailbox (crt0 zeroed .bss through the
  // cache), then never touch it cached again.
  volatile unsigned int* box = uncached(padBox);
  for (int i = 0; i < 16; ++i) box[i] = 0;
  SyncDCache(padBox, padBox + sizeof(padBox));
  InvalidDCache(padBox, padBox + sizeof(padBox));

  // The module takes its mailbox addresses as arguments - no SIF RPC needed
  // for setup. Arguments are NUL-separated strings.
  char args[32];
  const int n =
      snprintf(args, sizeof(args), "pad=0x%08x",
               reinterpret_cast<unsigned int>(padBox) & 0x1FFFFFFFU);
  int ret = 0;
  const int id =
      SifExecModuleBuffer(devlink_irx, size_devlink_irx, n + 1, args, &ret);
  // RESIDENT_END = 0, NO_RESIDENT_END = 1: anything else means it is not
  // listening (no ps2ip on this IOP, a misaligned mailbox, no thread).
  if (id < 0 || ret != 0) {
    TYRA_LOG("devlink: not available (id ", id, ", ret ", ret,
             ") - the devkit stays on host: files");
    return false;
  }
  state = 1;
  TYRA_LOG("devlink: up on udp/", kPort, " - Remote Pad reads the mailbox");
  return true;
}

bool DevLink::readPad(unsigned char* out) {
  if (state <= 0) return false;
  volatile unsigned int* box = uncached(padBox);
  unsigned int w[9];
  const unsigned int seq = box[2];
  for (int i = 0; i < 9; ++i) w[i] = box[i];
  // The IOP writes the three quadwords in order, footer last. A copy that
  // straddled a DMA shows as a footer that does not echo the seq read first,
  // or a seq that moved underneath us.
  if (w[0] != kPadMagic || w[1] != kPadVersion) return false;
  if (w[2] != seq || box[2] != seq) return false;
  if (w[8] != (seq ^ kFooterXor)) return false;
  memcpy(out, w, kPadSize);
  // Once per boot: the proof, in the [ps2] log, that a datagram made it from
  // the editor through the IOP into this mailbox (docs/devlink.md).
  static bool announced = false;
  if (!announced) {
    announced = true;
    TYRA_LOG("devlink: first Remote Pad state arrived (seq ", seq, ")");
  }
  return true;
}

}  // namespace Tyra
