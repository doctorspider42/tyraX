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

  /**
   * Log the counters, but ONLY when they have changed since the last call, so
   * this is a compare-and-return on every healthy frame. Called once per frame
   * from Engine::realLoop; must NOT be called from the handler itself, which
   * runs in interrupt context where stdio is not usable.
   *
   * NOTE, measured 2026-08-13 and not what an earlier version of this comment
   * claimed: TYRA_WARN is a no-op only under NDEBUG, and **a game build never
   * defines NDEBUG** — the release profile only drops `-g` and sets
   * `KEEPSYM=0`. So this call and its format string DO ship in a release ELF,
   * along with every other TYRA_LOG in the engine. It is harmless (a retail
   * console has nowhere to send the EE console, and `--audit-release` stays
   * clean because none of it is devkit code), but do not write "compiles out"
   * about a TYRA_* macro in this codebase without checking first.
   */
  static void report();

  /** Total completions dropped. 0 in a healthy session. */
  static unsigned int rejected();

  /** Dropped because the packet named no usable client. */
  static unsigned int rejectedBadClient();

  /**
   * Completions whose client had no outstanding packet - the signature of the
   * crash this class exists to stop. NOTE these are no longer "dropped": the
   * client is still completed (end_function + semaphore), only the packet free
   * is skipped, because skipping the completion is what hung the game.
   */
  static unsigned int rejectedNoPacket();

  /**
   * Retained at 0. The rpc_id check this counted was removed after it was
   * measured to hang the game (docs/backlog.md); the accessor stays so a
   * caller reading all three keeps compiling.
   */
  static unsigned int rejectedStaleId();
};

}  // namespace Tyra

#endif
