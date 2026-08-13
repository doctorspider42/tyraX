/*
# TyraX addition: a validating SIF_CMD_RPC_END handler - see
# inc/debug/sifrpc_guard.hpp.
*/

#include "debug/sifrpc_guard.hpp"

#include "debug/debug.hpp"

#include <kernel.h>
#include <sifcmd.h>
#include <sifrpc.h>

namespace Tyra {
namespace {

// Written from the SIF0 interrupt handler, read from the game loop, so
// volatile: the read must not be hoisted out of a polling loop. A 32-bit
// aligned word is atomic on the EE, and the handler is the only writer, so
// no lock is needed either way.
volatile unsigned int g_badClient = 0;
volatile unsigned int g_noPacket = 0;
volatile unsigned int g_staleId = 0;
bool g_installed = false;

// Last total handed to report(), so a healthy frame costs one compare.
unsigned int g_reported = 0;

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

  // Only ever reject what is PROVABLY dead. A false rejection is worse than
  // the crash: the caller waits on a semaphore nothing will ever signal, and
  // the game hangs with no diagnostic at all. So no range heuristics here -
  // null and misaligned only.
  if (cd == nullptr || (reinterpret_cast<u32>(cd) & 3u) != 0u) {
    g_badClient = g_badClient + 1;
    return;
  }

  // The crash. A completed call has already had its pkt_addr cleared.
  SifRpcPktHeader_t* pkt = static_cast<SifRpcPktHeader_t*>(cd->hdr.pkt_addr);
  if (pkt == nullptr) {
    g_noPacket = g_noPacket + 1;
    return;
  }

  // A late completion whose packet slot has since been recycled by another
  // call. rpc_packet_free() zeroes rpc_id and _rpc_get_packet() stamps a
  // fresh one, so this cannot false-reject a live call: cd->hdr.rpc_id was
  // copied out of this very packet when the call was made. It is the same
  // check ps2sdk's own sceSifCheckStatRpc() uses.
  if (static_cast<u32>(pkt->rpc_id) != cd->hdr.rpc_id) {
    g_staleId = g_staleId + 1;
    return;
  }

  // From here on: byte-for-byte what ps2sdk's _request_end does.
  if (request->cid == SIF_CMD_RPC_CALL) {
    if (cd->end_function) cd->end_function(cd->end_param);
  } else if (request->cid == SIF_CMD_RPC_BIND) {
    cd->server = request->server;
    cd->buff = request->buff;
    cd->cbuff = request->cbuff;
  }

  if (cd->hdr.sema_id >= 0) iSignalSema(cd->hdr.sema_id);

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
  // The healthy path is this compare and nothing else.
  const unsigned int total = rejected();
  if (total == g_reported) return;
  g_reported = total;

  // TYRA_WARN and not TYRA_LOG: a rejection means the fault reproduced, which
  // is exactly what somebody running a soak is waiting to be told. The editor's
  // log classifier puts a ==WARN line in the Debug panel's warning bucket.
  TYRA_WARN("SIF RPC: dropped ", total, " anomalous completion(s) - noPacket ",
            g_noPacket, ", staleId ", g_staleId, ", badClient ", g_badClient,
            " (docs/ps2link-setup.md)");
}

unsigned int SifRpcGuard::rejected() {
  return g_badClient + g_noPacket + g_staleId;
}

unsigned int SifRpcGuard::rejectedBadClient() { return g_badClient; }

unsigned int SifRpcGuard::rejectedNoPacket() { return g_noPacket; }

unsigned int SifRpcGuard::rejectedStaleId() { return g_staleId; }

}  // namespace Tyra
