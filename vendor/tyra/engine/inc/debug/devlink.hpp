/*
# TyraX addition: the EE end of devlink, the devkit's channel through the IOP
# (docs/devlink.md).
#
# Over ps2link every host: file access is a network round trip the EE waits
# out. devlink moves the network onto the IOP: a small module (devlink.irx,
# vendor/tyra/devlink) receives the editor's datagrams and DMAs them straight
# into mailboxes here, in EE RAM. Reading a mailbox is a few uncached loads -
# no RPC, no syscall, no waiting.
#
# Debug-only by construction: nothing in the engine calls this; the generated
# devkit runtime of a debug build does. A release ELF therefore never links
# this object or the embedded module (the release audit checks the marker).
*/

#ifndef TYRA_DEVLINK_HPP
#define TYRA_DEVLINK_HPP

namespace Tyra {

class DevLink {
 public:
  /** UDP port the IOP module listens on. The editor's twin: src/livepad.hpp. */
  static constexpr int kPort = 18198;

  /** Size of one Remote Pad datagram - livepad.bin, byte for byte. */
  static constexpr int kPadSize = 36;

  /**
   * Loads devlink.irx onto the IOP and points it at this ELF's mailboxes.
   * Only under ps2link (IrxLoader::keepIopResident): the module imports the
   * ps2ip ps2link keeps resident, and in PCSX2 the host: files cost nothing
   * anyway. Idempotent; the first call decides. Returns active().
   */
  static bool start();

  /** True once the module loaded and stayed resident. */
  static bool active();

  /**
   * Copies the newest Remote Pad datagram into `out` (kPadSize bytes).
   * False until the first one arrives, or when the copy caught the IOP's DMA
   * mid-write (the caller simply tries again next frame - the state is
   * absolute, so nothing is lost). The checks match livepad.bin's: magic,
   * version, and the footer echoing `seq`.
   */
  static bool readPad(unsigned char* out);

 private:
  static int state;  // 0 = not tried, 1 = active, -1 = unavailable
};

}  // namespace Tyra

#endif
