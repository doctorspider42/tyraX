#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Minimal read-only UDF 1.02 writer, used to turn an ISO9660 image written by
// iso9660::write into a UDF/ISO9660 hybrid ("UDF bridge") disc.
//
// Why a second filesystem at all: the PS2 DVD Player reads discs through its
// own `udfio` module, so a disc it will treat as DVD-Video has to carry a real
// UDF filesystem exposing VIDEO_TS/. The game that boots off that disc keeps
// reading its assets from `cdrom0:`, which is the ISO9660 side - so both have
// to be present, describing the SAME file data (see isoexport::buildFreeDvdBoot
// and docs/freedvdboot.md).
//
// Scope is deliberately narrow: one directory (VIDEO_TS) holding a handful of
// files whose data has ALREADY been placed in the image by the ISO9660 writer.
// This is not a general UDF implementation - it does not need to be, because
// nothing but the DVD Player ever reads the UDF side.
namespace udf {

constexpr uint32_t SECTOR = 2048;

// A file that already exists in the image; the UDF side only adds a second set
// of metadata pointing at the same sectors.
struct FileRef {
    std::string name;   // UDF name inside VIDEO_TS, e.g. "VTS_02_0.IFO"
    uint32_t lba = 0;   // absolute first sector of the data in the image
    uint32_t size = 0;  // bytes
};

struct Options {
    std::string volumeId;             // volume/logical-volume identifier
    uint32_t partitionStartLba = 0;   // partition logical block 0 (the File Set
                                      // Descriptor lands here; the ISO9660 side
                                      // must keep this sector and the next free)
    // First sector past the ISO9660 content - where this writer's own metadata
    // goes. The image must already be tailSectors() long from here (the ISO9660
    // writer reserves them via Options::tailReserveSectors); overlay() fills
    // them in rather than growing the file.
    uint32_t tailStartLba = 0;
    std::vector<FileRef> videoTs;     // files exposed as /VIDEO_TS/<name>
};

// Sectors udf::overlay() appends past isoTotalSectors: the per-file and
// per-directory metadata plus the closing anchor. Callers that need to know the
// final image size up front (layout previews) use this.
uint32_t tailSectors(size_t videoTsFileCount);

// The lowest LBA the ISO9660 side may use for its own metadata/data, given a
// partition start. Below it sit the UDF volume structures and the File Set
// Descriptor, which have fixed homes.
uint32_t firstFreeLba(uint32_t partitionStartLba);

// Writes the UDF structures into an existing ISO9660 image, in place. The file
// grows by tailSectors(). Returns "" on success, an error message otherwise.
std::string overlay(const std::filesystem::path& isoFile, const Options& opt);

}  // namespace udf
