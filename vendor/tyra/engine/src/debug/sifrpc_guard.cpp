/*
# TyraX addition: a validating SIF_CMD_RPC_END handler - see
# inc/debug/sifrpc_guard.hpp.
*/

#include "debug/sifrpc_guard.hpp"

#include "debug/debug.hpp"

#include <kernel.h>
#include <sifcmd.h>
#include <sifrpc.h>

// ps2sdk's sifcmd.c declares `_sif_cmd_data` with EXTERNAL linkage (nm shows a
// capital B), so the dispatch table it points at can be read directly. That is
// the only way to answer "is my handler the one registered right now" from
// inside the game - dumpmem could not be made to answer reliably while the game
// is polling. Layout mirrors `struct cmd_data` as sceSifInitCmd() fills it.
extern "C" {
struct TxSifCmdHandler {
  void* handler;
  void* harg;
};
struct TxSifCmdData {
  void* pktbuf;
  void* unused;
  TxSifCmdHandler* sys_cmd_handlers;
  u32 nr_sys_handlers;
  void* usr_cmd_handlers;
  u32 nr_usr_handlers;
  void* sregs;
  void* iopbuf;
};
extern TxSifCmdData _sif_cmd_data;
}

namespace Tyra {
namespace {

// Written from the SIF0 interrupt handler, read from the game loop, so
// volatile: the read must not be hoisted out of a polling loop. A 32-bit
// aligned word is atomic on the EE, and the handler is the only writer, so
// no lock is needed either way.
// Every completion this handler was given. Without it, `rejected() == 0` is
// ambiguous - it reads the same whether nothing went wrong or whether the
// handler is not on the dispatch path at all. A game deployed over ps2link runs
// in ps2link's EE address space, so there are TWO ps2sdk sifrpc instances live
// and only the game's carries this guard; a zero total would mean completions
// are going through ps2link's unguarded _request_end and this class can never
// see the fault it exists for. So the total is the thing that makes the zero
// mean something.
volatile unsigned int g_seen = 0;
volatile unsigned int g_badClient = 0;
volatile unsigned int g_noPacket = 0;
volatile unsigned int g_staleId = 0;
bool g_installed = false;

// Last total handed to report(), so a healthy frame costs one compare.
unsigned int g_reported = 0;
bool g_announced = false;

/**
 * ps2sdk's _request_end (ee/kernel/src/sifrpc.c) plus the three checks it is
 * missing. It is `static` there, so this is a reimplementation rather than a
 * wrapper - and it deliberately deviates in NOTHING except the early-outs, so
 * the two cannot drift in behaviour.
 *
 * WHY. On a real PS2 a debug-profile game took an EE exception after ~1800
 * frames:
 *
 *     Cause 0x70008408  -> ExcCode 2 (TLBL, fault on a LOAD)
 *     BadAddr 0x00000010
 *     EPC 0x00271C78    -> rpc_packet_free / _request_end, sifrpc.c
 *
 * `rec_id` sits at offset 0x10 of SifRpcRendPkt_t, and rpc_packet_free()
 * read-modify-writes it. So BadAddr 0x10 pins the fault, to the byte, on
 *
 *     rpc_packet_free(cd->hdr.pkt_addr)   with cd->hdr.pkt_addr == NULL
 *
 * and `cd` itself was valid - the handler had already read end_function and
 * sema_id out of it. The ONLY code that stores NULL there is _request_end's
 * own last line, so the EE had been handed a SECOND completion for a call it
 * had already completed.
 *
 * WHY HERE AND NOT IN A NORMAL GAME. ps2sdk's RPC clients are each serialised
 * internally (fileio by _fio_io_sema, audsrv by its completion_sema), but a
 * TyraX game runs four SIF RPC participants at once - the game loop's host:
 * traffic, audsrv on the audio thread (0x5), a second fileio user on the song
 * streamer thread (0x6, which PREEMPTS the loop at 0x40), and an EE-side
 * audsrv RPC server (0x60) - and under ps2link a host: round trip is
 * milliseconds of network instead of microseconds of emulator, which widens
 * every window in that machinery by about a thousandfold. The devkit path
 * makes it worse again: seven channel files are polled on ONE frame in 25,
 * back to back, because every cooldown starts at 1 and re-arms to the same
 * number.
 *
 * WHAT THIS DOES AND DOES NOT FIX. It stops the EE dying on an anomalous
 * completion, and it counts them so the anomaly stops being invisible. It does
 * NOT remove whatever produces the duplicate - a dropped completion still
 * means the thread waiting on that call is never signalled. If the counters
 * ever come back non-zero, that producer is the next thing to find, and the
 * count is the instrument for finding it.
 */
void requestEnd(void* packet, void* harg) {
  (void)harg;

  // NOTE ON MEMBER NAMES. ps2sdk renamed these fields upstream (client -> cd,
  // server -> sd, buff -> buf, cbuff -> cbuf). These are the names in the
  // toolchain image this engine is pinned to (h4570/tyra); the field LAYOUT is
  // identical in both, so bumping the image is a rename here and nothing more.
  SifRpcRendPkt_t* request = static_cast<SifRpcRendPkt_t*>(packet);
  SifRpcClientData_t* cd = request->client;

  g_seen = g_seen + 1;

  // A completion naming no usable client tells us nothing to complete, so it
  // is the one case with nothing to do but count it.
  if (cd == nullptr || (reinterpret_cast<u32>(cd) & 3u) != 0u) {
    g_badClient = g_badClient + 1;
    return;
  }

  SifRpcPktHeader_t* pkt = static_cast<SifRpcPktHeader_t*>(cd->hdr.pkt_addr);

  // ALWAYS COMPLETE THE CLIENT, even when the packet is unusable. This is the
  // lesson of the first version, which returned early here and was measured to
  // HANG the game on 2 of 4 teardowns while an unguarded build survived 6 of 6
  // (docs/backlog.md). Two independent ways an early return hangs it:
  //
  //   - the thread inside sceSifCallRpc is parked in WaitSema(cd->hdr.sema_id)
  //     and only this signal releases it;
  //   - and skipping end_function hangs the NEXT call as well, one step later -
  //     fioOpen/fioRead take _fio_completion_sema BEFORE the RPC and it is
  //     _fio_intr, i.e. the end_function, that releases it.
  //
  // So a guard that merely refuses to crash is not enough: it has to hand the
  // caller its completion. Running end_function twice on a genuine duplicate is
  // benign (the fio handlers re-copy identical bytes and re-signal a semaphore
  // whose max_count is 1).
  if (request->cid == SIF_CMD_RPC_CALL) {
    if (cd->end_function) cd->end_function(cd->end_param);
  } else if (request->cid == SIF_CMD_RPC_BIND) {
    cd->server = request->server;
    cd->buff = request->buff;
    cd->cbuff = request->cbuff;
  }

  if (cd->hdr.sema_id >= 0) iSignalSema(cd->hdr.sema_id);

  // THE ONLY THING GUARDED IS THE FREE, because that is the only thing that
  // dereferences the packet - `rec_id` sits at offset 0x10, which is the
  // BadAddr the console reported.
  if (pkt == nullptr) {
    g_noPacket = g_noPacket + 1;
    return;
  }

  // The rpc_id check that used to live here is GONE, deliberately. It claimed
  // it "cannot false-reject a live call" on the grounds that cd->hdr.rpc_id
  // only goes stale when the packet is recycled. That is false:
  // _SifCmdIntHandler calls EI() at its top, so completions dispatch
  // re-entrantly, and ps2sdk signals the semaphore BEFORE clearing pkt_addr -
  // so a woken thread can start the next call on this same client inside that
  // window, which makes a legitimate late completion indistinguishable from a
  // recycled one. Never re-add a check whose safety argument rests on that.

  // rpc_packet_free(), inlined - it is static in ps2sdk. PACKET_F_ALLOC is
  // 0x01 and is private to sifrpc.c, hence the literal.
  SifRpcRendPkt_t* rend = reinterpret_cast<SifRpcRendPkt_t*>(pkt);
  rend->rpc_id = 0;
  rend->rec_id &= ~0x01;
  cd->hdr.pkt_addr = nullptr;
}

}  // namespace

void SifRpcGuard::install() {
  if (g_installed) return;
  g_installed = true;

  // DI/EI because a completion landing between the read and the write of the
  // handler slot would be dispatched to a half-written function pointer.
  DI();
  SifAddCmdHandler(SIF_CMD_RPC_END, requestEnd, nullptr);
  EI();
}

void SifRpcGuard::report() {
  // Say ONCE that completions are actually reaching this handler. Without it a
  // zero rejected() count is ambiguous: a game deployed over ps2link shares its
  // EE address space with ps2link's own ps2sdk, so there are two sifrpc
  // instances and only this one is guarded. "The game works" does NOT settle
  // it - ps2link's _request_end takes `cd` from the packet payload and would
  // complete this game's client just as correctly. The count is the only
  // evidence, so it goes in the log where anybody can see it.
  if (!g_announced) {
    g_announced = true;
    // Report the DISPATCH TABLE regardless of whether anything arrived: on
    // ps2link the count stays at zero, and the question is whether that is
    // because nothing was dispatched or because this handler is not the one
    // registered. Print both so the log answers it either way.
    const void* slot = nullptr;
    const u32 id = SIF_CMD_RPC_END & 0x7FFFFFFFu;
    if (_sif_cmd_data.sys_cmd_handlers && id < _sif_cmd_data.nr_sys_handlers)
      slot = _sif_cmd_data.sys_cmd_handlers[id].handler;
    TYRA_LOG("SIF RPC guard: seen ", g_seen, ", slot ", (u32)(unsigned long)slot,
             ", mine ", (u32)(unsigned long)&requestEnd, ", pktbuf ",
             (u32)(unsigned long)_sif_cmd_data.pktbuf, ", iopbuf ",
             (u32)(unsigned long)_sif_cmd_data.iopbuf);
  }

  // The healthy path is this compare and nothing else.
  const unsigned int total = rejected();
  if (total == g_reported) return;
  g_reported = total;

  // TYRA_WARN and not TYRA_LOG: a rejection means the fault reproduced, which
  // is exactly what somebody running a soak is waiting to be told. The editor's
  // log classifier puts a ==WARN line in the Debug panel's warning bucket.
  TYRA_WARN("SIF RPC: guarded ", total, " of ", g_seen,
            " completion(s) - noPacket ", g_noPacket, ", badClient ",
            g_badClient, " (docs/ps2link-setup.md)");
}

unsigned int SifRpcGuard::seen() { return g_seen; }

unsigned int SifRpcGuard::rejected() {
  return g_badClient + g_noPacket + g_staleId;
}

unsigned int SifRpcGuard::rejectedBadClient() { return g_badClient; }

unsigned int SifRpcGuard::rejectedNoPacket() { return g_noPacket; }

unsigned int SifRpcGuard::rejectedStaleId() { return g_staleId; }

}  // namespace Tyra
