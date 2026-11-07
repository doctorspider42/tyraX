/*
# TyraX addition - see inc/debug/devlink.hpp and docs/devlink.md.
*/

#include "debug/devlink.hpp"
#include "debug/debug.hpp"
#include "irx/irx_loader.hpp"

#include <kernel.h>
#include <loadfile.h>
#include <sifdma.h>
#include <stdio.h>
#include <string.h>

extern unsigned char devlink_irx[];
extern int size_devlink_irx;

namespace Tyra {

namespace {

// Release-audit marker: this string in an ELF means devkit code shipped.
const char kDevkitMarker[] __attribute__((used)) = "TXDEVKIT-devlink";

// The mailboxes the IOP DMAs into. Each is 64-byte aligned and sized in whole
// lines so it owns its cache lines outright: the IOP writes them behind the
// cache's back, and a dirty line here written back later would overwrite a
// datagram with stale bytes. They are cleared, written back and invalidated
// once in start(), and from then on only ever READ, through the uncached
// segment.
unsigned char padBox[64] __attribute__((aligned(64)));
// u32 length + 12 bytes of padding, then up to kCmdMax bytes of livedbg.cmd.
unsigned char cmdBox[16 + DevLink::kCmdMax] __attribute__((aligned(64)));
// The IOP's answer: magic, its two report slots' addresses, the largest
// report a slot takes, and the last sequence it finished sending per slot.
unsigned char helloBox[64] __attribute__((aligned(64)));

const unsigned int kPadMagic = 0x44505854U;    // "TXPD"
const unsigned int kPadVersion = 1U;
const unsigned int kCmdMagic = 0x43445854U;    // "TXDC"
const unsigned int kHelloMagic = 0x4C484458U;  // "XDHL"
const unsigned int kOutMagic = 0x4F445854U;    // "TXDO"
const unsigned int kFooterXor = 0x5A5A5A5AU;

// What the game hands to the IOP: header (magic, seq, len, 0), the report,
// then a quadword whose first word echoes seq - one SIF1 DMA per report, so
// the footer lands last. One staging buffer per IOP slot, because a buffer may
// not be touched again until its DMA has finished.
const int kStage = 16 + DevLink::kReportMax + 16;
unsigned char stage[2][kStage] __attribute__((aligned(64)));
int stageDma[2] = {0, 0};
unsigned int stageSeq[2] = {0, 0};  // what each slot was last given
unsigned int reportSeq = 0;
int nextSlot = 0;

inline volatile unsigned int* uncached(void* p) {
  return reinterpret_cast<volatile unsigned int*>(
      (reinterpret_cast<unsigned int>(p) & 0x1FFFFFFFU) | 0x20000000U);
}

inline unsigned int physical(void* p) {
  return reinterpret_cast<unsigned int>(p) & 0x1FFFFFFFU;
}

// How many EE kernel semaphores and threads this boot can still create. The
// ps2link reset wedge (docs/ps2link-setup.md) appears after ~15 reset deploys,
// and pkoReset only terminates the game's MAIN thread before ExecPS2 - if the
// kernel keeps the rest of a dead game's objects, every deploy leaks some and
// these numbers fall run after run. Logged once per boot; the objects are
// deleted again at once.
void logKernelHeadroom() {
  static int ids[512];
  int sema = 0;
  for (; sema < 512; ++sema) {
    ee_sema_t s;
    s.init_count = 0;
    s.max_count = 1;
    s.option = 0;
    s.attr = 0;
    const int id = CreateSema(&s);
    if (id < 0) break;
    ids[sema] = id;
  }
  for (int i = 0; i < sema; ++i) DeleteSema(ids[i]);

  static unsigned char stack[0x200] __attribute__((aligned(16)));
  int threads = 0;
  for (; threads < 512; ++threads) {
    ee_thread_t t;
    memset(&t, 0, sizeof(t));
    t.func = reinterpret_cast<void*>(&logKernelHeadroom);  // never started
    t.stack = stack;
    t.stack_size = sizeof(stack);
    t.gp_reg = nullptr;
    t.initial_priority = 127;
    const int id = CreateThread(&t);
    if (id < 0) break;
    ids[threads] = id;
  }
  for (int i = 0; i < threads; ++i) DeleteThread(ids[i]);
  TYRA_LOG("KHEAD free semaphores ", sema, " threads ", threads);
}

void clearBox(unsigned char* box, int size) {
  volatile unsigned int* w = uncached(box);
  for (int i = 0; i < size / 4; ++i) w[i] = 0;
  SyncDCache(box, box + size);
  InvalidDCache(box, box + size);
}

}  // namespace

int DevLink::state = 0;

bool DevLink::active() { return state > 0; }

bool DevLink::start() {
  if (state != 0) return state > 0;
  state = -1;
  if (!IrxLoader::keepIopResident) return false;
  logKernelHeadroom();

  // Leave no dirty line over a mailbox (crt0 zeroed .bss through the cache),
  // then never touch one cached again.
  clearBox(padBox, sizeof(padBox));
  clearBox(cmdBox, sizeof(cmdBox));
  clearBox(helloBox, sizeof(helloBox));

  // The module takes its mailbox addresses as arguments - no SIF RPC needed
  // for setup. Arguments are NUL-separated strings.
  char args[96];
  int n = snprintf(args, sizeof(args), "pad=0x%08x", physical(padBox)) + 1;
  n += snprintf(args + n, sizeof(args) - n, "cmd=0x%08x", physical(cmdBox)) + 1;
  n += snprintf(args + n, sizeof(args) - n, "hello=0x%08x",
                physical(helloBox)) + 1;
  int ret = 0;
  const int id = SifExecModuleBuffer(devlink_irx, size_devlink_irx, n, args, &ret);
  // RESIDENT_END = 0, NO_RESIDENT_END = 1: anything else means it is not
  // listening (no ps2ip on this IOP, a misaligned mailbox, no IOP memory).
  if (id < 0 || ret != 0) {
    TYRA_LOG("devlink: not available (id ", id, ", ret ", ret,
             ") - the devkit stays on host: files");
    return false;
  }
  state = 1;
  TYRA_LOG("devlink: up on udp/", kPort,
           " - Remote Pad and the Live Debugger use it");
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

int DevLink::readCommand(unsigned char* out, int cap) {
  if (state <= 0) return 0;
  volatile unsigned int* box = uncached(cmdBox);
  const unsigned int len = box[0];
  // Any byte length: the format's lists are 2-byte entries.
  if (len < 16 || len > (unsigned int)kCmdMax || (int)len > cap) return 0;
  volatile unsigned int* payload = box + 4;
  const unsigned int seq = payload[2];
  // Word copies into an aligned buffer: `out` is the caller's byte array and
  // may sit anywhere.
  static unsigned int dst[kCmdMax / 4];
  for (unsigned int i = 0; i < (len + 3) / 4; ++i) dst[i] = payload[i];
  // The same torn-copy guard as the pad: the format's own magic, a footer
  // echoing the seq, and nothing moved while we copied.
  if (dst[0] != kCmdMagic) return 0;
  if (dst[2] != seq || payload[2] != seq || box[0] != len) return 0;
  unsigned int foot;
  memcpy(&foot, reinterpret_cast<unsigned char*>(dst) + len - 4, 4);
  if (foot != (seq ^ kFooterXor)) return 0;
  memcpy(out, dst, len);
  static bool announced = false;
  if (!announced) {
    announced = true;
    TYRA_LOG("devlink: first Live Debugger command arrived (seq ", seq, ")");
  }
  return (int)len;
}

bool DevLink::sendReport(const void* data, int len) {
  if (state <= 0 || len <= 0 || len > kReportMax) return false;
  volatile unsigned int* hello = uncached(helloBox);
  if (hello[0] != kHelloMagic || (int)hello[3] < len) return false;
  // A slot is ours when the IOP has acknowledged what we last gave it and
  // our own DMA into it has finished. Neither waits: a busy pair of slots
  // skips this report - the next one supersedes it.
  for (int t = 0; t < 2; ++t) {
    const int k = (nextSlot + t) & 1;
    if (hello[4 + k] != stageSeq[k]) continue;
    if (stageDma[k] != 0 && sceSifDmaStat(stageDma[k]) >= 0) continue;
    const unsigned int slotAddr = hello[1 + k];
    if (slotAddr == 0) continue;

    unsigned char* s = stage[k];
    const unsigned int seq = ++reportSeq;
    const unsigned int h[4] = {kOutMagic, seq, (unsigned int)len, 0U};
    memcpy(s, h, 16);
    memcpy(s + 16, data, (size_t)len);
    const int footAt = (16 + len + 15) & ~15;
    memset(s + 16 + len, 0, (size_t)(footAt - 16 - len));
    const unsigned int foot[4] = {seq ^ kFooterXor, 0U, 0U, 0U};
    memcpy(s + footAt, foot, 16);
    const int size = footAt + 16;
    SyncDCache(s, s + size);

    SifDmaTransfer_t dma;
    dma.src = s;
    dma.dest = reinterpret_cast<void*>(slotAddr);
    dma.size = size;
    dma.attr = 0;
    // sceSifSetDma is a critical region the kernel does not guard itself
    // (the sbv fileio patch exists for exactly that).
    DI();
    const int id = sceSifSetDma(&dma, 1);
    EI();
    if (id == 0) return false;
    stageDma[k] = id;
    stageSeq[k] = seq;
    nextSlot = k ^ 1;
    return true;
  }
  return false;
}

}  // namespace Tyra
