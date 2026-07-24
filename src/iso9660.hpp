#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Minimal ISO9660 image writer for PS2 discs. No Joliet/RockRidge - the PS2
// CD driver (and PCSX2) read plain ISO9660 directory records. The point of
// rolling our own instead of shelling out to mkisofs: file data extents are
// laid out exactly in input order, so the caller controls which assets sit
// next to each other on the disc (adjacent LBAs = no seek between loads).
namespace iso9660 {

struct FileEntry {
    std::filesystem::path source;  // host file to embed
    // Path inside the image, '/'-separated, no leading slash, e.g.
    // "HUD/USE.PNG". Callers pass upper-case names (ISO9660 identifiers);
    // the ";1" file version suffix is appended automatically.
    std::string isoPath;
};

struct PlacedFile {
    std::string isoPath;
    uint32_t lba = 0;   // first sector of the file data
    uint32_t size = 0;  // bytes
};

// Optional layout controls. Defaults reproduce the plain-ISO9660 image
// byte-for-byte (path tables right after the volume descriptors at LBA 18,
// no trailing padding).
struct Options {
    // First LBA the ISO9660 metadata (path tables, then directory extents,
    // then file data) may occupy. Sectors 18..firstDataLba-1 are left zeroed,
    // reserving room for other on-disc structures (the ESR UDF bridge parks
    // its descriptors + fake DVD-Video partition there). Values <= 18 mean
    // "no gap".
    uint32_t firstDataLba = 18;
    // Extra zeroed sectors appended past the last file, counted into the
    // volume size (the ESR bridge writes its second anchor into the last one).
    uint32_t tailReserveSectors = 0;
};

// Writes the image; files[0] gets the lowest data LBA, files[1] the next,
// and so on. Returns an empty string on success, an error message otherwise.
// outPlacement (optional) receives the final LBA of every file, in layout
// order - useful for reporting the disc layout to the user.
// outTotalSectors (optional) receives the image size in 2048-byte sectors.
std::string write(const std::filesystem::path& isoFile, const std::string& volumeId,
                  const std::vector<FileEntry>& files,
                  std::vector<PlacedFile>* outPlacement = nullptr, const Options& opt = {},
                  uint32_t* outTotalSectors = nullptr);

struct PlannedImage {
    std::vector<PlacedFile> files;  // layout order (same order as the input)
    uint32_t dataStartLba = 0;      // first file-data sector (metadata before it)
    uint32_t totalSectors = 0;      // image size in 2048-byte sectors
};

// Computes the exact same layout write() would produce, without writing
// anything - source files only need to exist to be sized. Backs layout
// previews (the editor's Disc Layout window).
std::string plan(const std::vector<FileEntry>& files, PlannedImage* out);

}  // namespace iso9660
