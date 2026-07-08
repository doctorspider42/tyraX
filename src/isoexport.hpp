#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "project.hpp"

// Builds a bootable PS2 disc image (<project>/<name>.iso) out of bin/.
//
// Disc layout is what makes this more than a plain mkisofs call: the drive
// reads fastest when files loaded together sit on adjacent sectors, so the
// exporter orders file data by load group:
//   1. SYSTEM.CNF + the ELF (read once by the loader),
//   2. pinned files from <project>/iso-layout.txt (optional, one
//      bin-relative path per line, '#' comments - also written by the
//      editor's Disc Layout window),
//   3. startup assets - HUD images, the built-in use/loading sprites and
//      all sound effects (everything the game opens in init()),
//   4. per-scene assets in scene order (terrain texture, object textures,
//      models) - scene 0 is also read at startup,
//   5. music tracks (streamed, seeks anyway), then any remaining files.
namespace isoexport {

using LogFn = std::function<void(const std::string&)>;

struct PlanItem {
    std::string relPath;  // bin-relative ("hud/use.png"); empty for SYSTEM.CNF
    std::string isoPath;  // on-disc name ("HUD/USE.PNG")
    std::string group;    // "boot", "startup", "scene:<name>", "music", "other"
    bool pinned = false;  // ordered by iso-layout.txt instead of the group rules
    uint32_t lba = 0;     // first data sector
    uint32_t size = 0;    // bytes
    uint32_t sectors = 0;
};

struct Plan {
    std::vector<PlanItem> items;  // in layout order (lowest LBA first)
    uint32_t dataStartLba = 0;    // sectors below this hold ISO9660 metadata
    uint32_t totalSectors = 0;
    bool manualOrder = false;  // iso-layout.txt exists and pinned something
};

// Computes the disc layout without writing anything (Disc Layout window).
// log (optional) receives name warnings. Returns "" or an error message.
std::string plan(const Project& p, Plan* out, const LogFn& log = nullptr);

// Writes the image. Returns "" on success, an error message otherwise.
std::string build(const Project& p, const LogFn& log);

// Persists a manual order into <project>/iso-layout.txt (bin-relative paths;
// the ELF and SYSTEM.CNF stay first regardless). Empty list = reset to the
// automatic group order (deletes the file).
std::string saveManualOrder(const Project& p, const std::vector<std::string>& relPaths);

}  // namespace isoexport
