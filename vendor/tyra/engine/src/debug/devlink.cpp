/*
# TyraX addition - see inc/debug/devlink.hpp and docs/devlink.md.
*/

#include "debug/devlink.hpp"
#include "debug/debug.hpp"
#include "irx/irx_loader.hpp"

#include "file/file_utils.hpp"

#include <kernel.h>
#include <loadfile.h>
#include <malloc.h>
#include <sifdma.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <timer.h>

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

// Write back and drop every cached line over a DMA target FIRST, then clear
// it through the uncached segment: done the other way round, a dirty line
// written back afterwards would overwrite the zeros (or, later, the IOP's
// data) with whatever the line held.
void logSink(const char* text, int len);  // below, with the log buffer

void clearBox(unsigned char* box, int size) {
  SyncDCache(box, box + size);
  InvalidDCache(box, box + size);
  volatile unsigned int* w = uncached(box);
  for (int i = 0; i < size / 4; ++i) w[i] = 0;
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
           " - pad, debugger, file channels, logs and watchdog use it");
  // From here plain LOG lines go to the IOP once a frame (phase 4).
  TyraDebug::logSink = &logSink;
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
  return sendSlot(0, nullptr, 0, data, len);
}

// One report into a free IOP slot: header (magic, seq, length, kind), an
// optional prefix, the data, then a footer quadword echoing seq.
bool DevLink::sendSlot(unsigned int kind, const void* prefix, int prefixLen,
                       const void* data, int dataLen) {
  const int len = prefixLen + dataLen;
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
    const unsigned int h[4] = {kOutMagic, seq, (unsigned int)len, kind};
    memcpy(s, h, 16);
    if (prefixLen > 0) memcpy(s + 16, prefix, (size_t)prefixLen);
    if (dataLen > 0) memcpy(s + 16 + prefixLen, data, (size_t)dataLen);
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

// ------------------------------------------------------------ file mirrors --

namespace {

const unsigned int kKindRegister = 1;
const unsigned int kKindFile = 2;
const unsigned int kKindLog = 3;
const unsigned int kKindHeartbeat = 4;

// Phase 4: plain LOG lines collect here and leave once a frame. A full buffer
// drops the newest lines and says how many, rather than blocking the game.
char logRing[8 * 1024];
int logLen = 0;
int logDropped = 0;

void logSink(const char* text, int len) {
  if (logLen + len > (int)sizeof(logRing)) {
    ++logDropped;
    return;
  }
  memcpy(logRing + logLen, text, (size_t)len);
  logLen += len;
}

// Phase 5: one heartbeat a second, so the IOP can tell "quiet" from "dead".
unsigned long long lastBeat = 0;
const int kMaxMirrors = 8;
const int kMaxOutFiles = 4;

struct Mirror {
  unsigned int id = 0;
  int cap = 0;
  unsigned char* ctrl = nullptr;     // {active half, len, seq, footer}
  unsigned char* half[2] = {nullptr, nullptr};
  unsigned int lastSeq = 0;          // the content last copied out
};
Mirror mirrors[kMaxMirrors];
int mirrorCount = 0;
bool registerPending = false;

struct OutFile {
  unsigned int id = 0;
  unsigned char* data = nullptr;
  int cap = 0;
  int len = 0;
  int sent = 0;           // bytes handed over so far
  unsigned int seq = 0;   // which version of the file this is
  bool pending = false;
  bool owned = false;     // writeFileOwned: free `data` once it has all left
};
OutFile outFiles[kMaxOutFiles];
unsigned int outFileSeq = 0;

// The id both ends agree on: FNV-1a of the file name.
unsigned int fileId(const char* name) {
  unsigned int h = 2166136261U;
  for (const char* p = name; *p; ++p) h = (h ^ (unsigned char)*p) * 16777619U;
  return h ? h : 1U;
}

unsigned char* allocLines(int size) {
  const int rounded = (size + 63) & ~63;
  unsigned char* p = static_cast<unsigned char*>(memalign(64, (size_t)rounded));
  if (p) clearBox(p, rounded);
  return p;
}

int hostRead(const char* name, void* buf, int cap) {
  FILE* f = fopen(FileUtils::fromCwd(name).c_str(), "rb");
  if (!f) return -1;
  const size_t got = fread(buf, 1, (size_t)cap, f);
  fclose(f);
  return (int)got;
}

bool hostWrite(const char* name, const void* data, int len) {
  FILE* f = fopen(FileUtils::fromCwd(name).c_str(), "wb");
  if (!f) return false;
  fwrite(data, 1, (size_t)len, f);
  fclose(f);
  return true;
}

}  // namespace

int DevLink::readFile(const char* name, void* buf, int cap) {
  start();
  if (state <= 0) return hostRead(name, buf, cap);
  pump();
  const unsigned int id = fileId(name);
  Mirror* m = nullptr;
  for (int i = 0; i < mirrorCount; ++i)
    if (mirrors[i].id == id) m = &mirrors[i];
  if (!m) {
    if (mirrorCount >= kMaxMirrors || cap <= 0) return hostRead(name, buf, cap);
    Mirror& n = mirrors[mirrorCount];
    // Chunks are DMA'd as whole quadwords, so a half gets a chunk's worth of
    // slack past `cap`.
    const int halfSize = ((cap + 1023) & ~1023) + 64;
    n.ctrl = allocLines(64);
    n.half[0] = allocLines(halfSize);
    n.half[1] = allocLines(halfSize);
    if (!n.ctrl || !n.half[0] || !n.half[1]) return hostRead(name, buf, cap);
    n.id = id;
    n.cap = cap;
    ++mirrorCount;
    registerPending = true;
    pump();
    TYRA_LOG("devlink: mirroring ", name, " (", cap, " bytes)");
    return -1;  // nothing has arrived yet
  }
  volatile unsigned int* c = uncached(m->ctrl);
  const unsigned int half = c[0], len = c[1], seq = c[2], foot = c[3];
  if (seq == 0 || foot != (seq ^ kFooterXor) || half > 1U) return -1;
  if (len == 0 || (int)len > cap || (int)len > m->cap) return -1;  // deleted
  unsigned char* src = m->half[half];
  if (seq != m->lastSeq) {
    // This half was filled by DMA behind the cache; the game never writes
    // it, so dropping its lines is all it takes to read it at full speed.
    InvalidDCache(src, src + ((len + 63) & ~63U));
    m->lastSeq = seq;
  }
  memcpy(buf, src, len);
  // The IOP may have flipped (and started refilling the other half) while
  // we copied. Same content check as every devkit format: try next poll.
  if (c[2] != seq) return -1;
  return (int)len;
}

bool DevLink::writeFile(const char* name, const void* data, int len) {
  start();
  if (state <= 0 || len <= 0) return hostWrite(name, data, len);
  const unsigned int id = fileId(name);
  OutFile* o = nullptr;
  for (int i = 0; i < kMaxOutFiles && !o; ++i)
    if (outFiles[i].id == id) o = &outFiles[i];
  for (int i = 0; i < kMaxOutFiles && !o; ++i)
    if (outFiles[i].id == 0) o = &outFiles[i];
  if (!o) return hostWrite(name, data, len);
  if (o->cap < len || o->owned) {
    o->owned = false;
    o->cap = 0;
    free(o->data);
    o->data = static_cast<unsigned char*>(malloc((size_t)len));
    o->cap = o->data ? len : 0;
    if (!o->data) {
      o->id = 0;
      return hostWrite(name, data, len);
    }
  }
  // Latest wins: a newer version replaces one still going out.
  o->id = id;
  memcpy(o->data, data, (size_t)len);
  o->len = len;
  o->sent = 0;
  o->seq = ++outFileSeq;
  o->pending = true;
  pump();
  return true;
}

bool DevLink::writeFileOwned(const char* name, void* data, int len) {
  start();
  if (!data) return false;
  const unsigned int id = fileId(name);
  OutFile* o = nullptr;
  if (state > 0 && len > 0) {
    for (int i = 0; i < kMaxOutFiles && !o; ++i)
      if (outFiles[i].id == id) o = &outFiles[i];
    for (int i = 0; i < kMaxOutFiles && !o; ++i)
      if (outFiles[i].id == 0) o = &outFiles[i];
  }
  if (!o) {
    const bool ok = hostWrite(name, data, len);
    free(data);
    return ok;
  }
  // Latest wins, as in writeFile: whatever this name still had queued is
  // dropped (the editor's assembler starts over on the new seq).
  free(o->data);
  o->id = id;
  o->data = static_cast<unsigned char*>(data);
  o->cap = len;
  o->len = len;
  o->sent = 0;
  o->seq = ++outFileSeq;
  o->pending = true;
  o->owned = true;
  pump();
  return true;
}

void DevLink::pump() {
  if (state <= 0) return;
  if (logLen > 0 || logDropped > 0) {
    if (logDropped > 0) {
      char note[64];
      const int n = snprintf(note, sizeof(note),
                             "LOG: devlink: %d log line(s) dropped\n", logDropped);
      if (logLen + n <= (int)sizeof(logRing)) {
        memcpy(logRing + logLen, note, (size_t)n);
        logLen += n;
        logDropped = 0;
      }
    }
    if (sendSlot(kKindLog, nullptr, 0, logRing, logLen)) logLen = 0;
  }
  const unsigned long long now = GetTimerSystemTime();
  if (now - lastBeat > (unsigned long long)kBUSCLK) {
    const unsigned int beat = 0;
    if (sendSlot(kKindHeartbeat, nullptr, 0, &beat, 4)) lastBeat = now;
  }
  if (registerPending && mirrorCount > 0) {
    unsigned int table[kMaxMirrors * 5];
    for (int i = 0; i < mirrorCount; ++i) {
      table[i * 5 + 0] = mirrors[i].id;
      table[i * 5 + 1] = physical(mirrors[i].ctrl);
      table[i * 5 + 2] = physical(mirrors[i].half[0]);
      table[i * 5 + 3] = physical(mirrors[i].half[1]);
      table[i * 5 + 4] = (unsigned int)mirrors[i].cap;
    }
    if (sendSlot(kKindRegister, nullptr, 0, table, mirrorCount * 20))
      registerPending = false;
  }
  // Outbound files go one segment per free slot: {id, seq, offset, total}
  // and up to a slot's worth of the file.
  // A segment only leaves while BOTH slots are free, so one always stays
  // open for the debugger's report and the log: a 900 KB screenshot is ~30
  // segments, and taking every slot for those seconds would read as a hung
  // game in the editor (livedbg.bin stops changing).
  volatile unsigned int* hello = uncached(helloBox);
  for (int i = 0; i < kMaxOutFiles; ++i) {
    OutFile& o = outFiles[i];
    while (o.pending) {
      for (int k = 0; k < 2; ++k)
        if (hello[4 + k] != stageSeq[k] ||
            (stageDma[k] != 0 && sceSifDmaStat(stageDma[k]) >= 0))
          return;
      const int n = o.len - o.sent < kReportMax - 16 ? o.len - o.sent
                                                       : kReportMax - 16;
      const unsigned int h[4] = {o.id, o.seq, (unsigned int)o.sent,
                                 (unsigned int)o.len};
      if (!sendSlot(kKindFile, h, 16, o.data + o.sent, n)) return;
      o.sent += n;
      if (o.sent >= o.len) {
        o.pending = false;
        // sendSlot copied the last segment into its staging buffer, so an
        // owned block has nothing left to give.
        if (o.owned) {
          free(o.data);
          o.data = nullptr;
          o.cap = 0;
          o.owned = false;
        }
      }
    }
  }
}

}  // namespace Tyra
