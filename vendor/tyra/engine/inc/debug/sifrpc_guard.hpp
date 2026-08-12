/*
# TyraX addition: a VALIDATING SIF_CMD_RPC_END handler.
#
# ps2sdk's own handler (_request_end, ee/kernel/src/sifrpc.c) trusts every
# completion the IOP hands it. A duplicate or stale one therefore dereferences
# a NULL packet pointer and takes the EE down. See src/debug/sifrpc_guard.cpp
# for the measured crash this was written for, and docs/ps2link-setup.md.
*/

#ifndef TYRA_SIFRPC_GUARD_HPP
#define TYRA_SIFRPC_GUARD_HPP

namespace Tyra {

/**
 * Replaces ps2sdk's SIF_CMD_RPC_END command handler with one that checks a
 * completion is live before acting on it, and COUNTS the ones it drops.
 *
 * The counters are the point: a rejection is not normal, so a session that
 * reports a non-zero count has reproduced the fault without crashing, and one
 * that reports zero has not exercised it at all. Never treat a zero count as
 * proof that the race is gone - it is only proof that it did not fire.
 */
class SifRpcGuard {
 public:
  /**
   * Install the handler. Idempotent, and must be called AFTER the last
   * SifInitRpc() - that is what registers ps2sdk's own handler, and the
   * later SifInitRpc() inside ps2sdk's fioInit() is a no-op because
   * sceSifInitRpc() early-returns once initialised.
   *
   * NOT re-armed across an IOP reboot: sceSifInitRpc() re-registers ps2sdk's
   * handler when HasIopRebootedSinceLastCall() is true. Nothing in a TyraX
   * game reboots the IOP after boot, so that path is unreachable today; a
   * future feature that does must call install() again afterwards.
   */
  static void install();

  /** Total completions dropped. 0 in a healthy session. */
  static unsigned int rejected();

  /** Dropped because the packet named no usable client. */
  static unsigned int rejectedBadClient();

  /**
   * Dropped because the client had no outstanding packet - the signature of
   * the crash this class exists to stop (a second completion for one call).
   */
  static unsigned int rejectedNoPacket();

  /**
   * Dropped because the client's packet had already been reused by a later
   * call - a late completion arriving after its slot was recycled.
   */
  static unsigned int rejectedStaleId();
};

}  // namespace Tyra

#endif
