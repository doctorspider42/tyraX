#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

// Turns a freshly written PS2 ISO9660 image into an ESR-compatible disc.
//
// A modded PS2 (with the ESR loader installed) will only boot a DVD-R backup
// if the disc looks like a DVD-Video title: it must carry a UDF file system
// whose partition is repointed at a small fake VIDEO_TS structure. The DVD
// player recognises that, hands off to ESR, and ESR then boots the game the
// normal way over the ISO9660 file system (cdrom0:\<ELF>).
//
// This module does two things, in order:
//   1. writeBridge()  - lays a minimal UDF 1.02 bridge into the sectors the
//      ISO9660 writer left reserved (the volume-recognition sequence, the
//      main + reserve volume-descriptor sequences with the partition
//      descriptor at the exact LBAs esrtool expects, an integrity descriptor
//      and the anchors).
//   2. patch()        - the ESR patch itself, ported from esrtool
//      (ali-raheem, MIT: https://github.com/ali-raheem/esrtool), which traces
//      back to Tatsh's original esr-disc-patcher. It repoints both partition
//      descriptors at the fake DVD-Video partition (LBA 128), stashes the
//      originals in the reserved sectors 14/15, recomputes the descriptor-tag
//      CRC + checksum, and drops the 24 KiB fake partition in place.
//
// The ISO9660 file system is untouched, so the same image still boots as a
// plain disc in PCSX2 / a real PS2 - the UDF bridge only matters to ESR.
namespace esrudf {

using LogFn = std::function<void(const std::string&)>;

// Reserved-layout contract with the ISO9660 writer: an ESR image must keep the
// metadata/file data above LBA 257 (the UDF descriptors + fake partition live
// below it) and reserve one trailing sector for the second anchor.
constexpr uint32_t kFirstDataLba = 257;
constexpr uint32_t kTailReserveSectors = 1;

// Writes the UDF bridge then applies the ESR patch to isoFile in place.
// totalSectors is the image size the ISO9660 writer reported (used to place
// the trailing anchor). Returns "" on success, an error message otherwise.
std::string makeEsrCompatible(const std::filesystem::path& isoFile, uint32_t totalSectors,
                              const LogFn& log = nullptr);

}  // namespace esrudf
