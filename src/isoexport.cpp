#include "isoexport.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <utility>

#include "iso9660.hpp"
#include "menubake.hpp"
#include "templates.hpp"  // bakedModelPath - bin/ holds artifacts, not sources
#include "udf.hpp"

namespace fs = std::filesystem;

namespace isoexport {

// "res/hud/x.png" (project path) -> "hud/x.png" (bin/ path, next to the ELF)
static std::string binPathOf(std::string p) {
    if (p.rfind("res/", 0) == 0) p = p.substr(4);
    return p;
}

// "res/sfx/shot.wav" -> "sfx/shot.adpcm" (the build pipeline converts these)
static std::string adpcmPathOf(const std::string& p) {
    std::string b = binPathOf(p);
    size_t dot = b.find_last_of('.');
    if (dot != std::string::npos) b = b.substr(0, dot);
    return b + ".adpcm";
}

static std::string upper(std::string s) {
    for (char& c : s)
        if (c != '/') c = (char)std::toupper((unsigned char)c);
    return s;
}

struct OrderedFile {
    std::string rel, group;
    bool pinned = false;
};

// Scans bin/ and orders everything by load group; iso-layout.txt pins move
// to the front (behind the ELF) but keep their group label for display.
static std::string orderFiles(const Project& p, std::vector<OrderedFile>& out,
                              bool* manualOrder, const LogFn& log) {
    const fs::path bin = fs::path(p.dir) / "bin";
    std::error_code ec;
    if (!fs::exists(fs::path(p.elfPath()), ec))
        return "ELF not found: " + p.elfPath() + " - build the project first.";

    std::set<std::string> remaining;  // bin-relative, '/'-separated
    for (const auto& e : fs::recursive_directory_iterator(bin, ec)) {
        if (!e.is_regular_file(ec)) continue;
        std::string rel = fs::relative(e.path(), bin, ec).generic_string();
        if (rel.empty() || fs::path(rel).filename().string().front() == '.') continue;
        if (fs::path(rel).extension() == ".iso") continue;
        // runtime artifacts of previous host runs - never ship them
        if (rel == "log.txt" || rel == "ps2link.run") continue;
        // Devkit runtime files + the unstripped symbol copy: work artifacts of
        // a debug session, never disc content (docs/devkit.md).
        if (rel == "livedbg.bin" || rel == "livedbg.cmd" ||
            rel == "livelink.bin" || rel == "livelink.sig" ||
            rel == "livelogic.bin" || rel == "crash.txt")
            continue;
        if (rel.size() > 4 && rel.compare(rel.size() - 4, 4, ".sym") == 0)
            continue;
        remaining.insert(rel);
    }
    if (remaining.empty()) return "bin/ is empty - build the project first.";

    // Automatic order: group rules over the full file set.
    std::vector<OrderedFile> autoOrder;
    auto take = [&](const std::string& rel, const std::string& group) {
        auto it = remaining.find(rel);
        if (it == remaining.end()) return;
        autoOrder.push_back({rel, group, false});
        remaining.erase(it);
    };

    take(p.elfName(), "boot");
    for (const HudImage& h : p.hud) take(binPathOf(h.imagePath), "startup");
    take("hud/use.png", "startup");
    take("hud/loading.png", "startup");
    take("hud/loading-white.png", "startup");
    // Loading screens draw during every scene load (including boot), so their
    // assets sit with the startup group. Text sprites use the same
    // screen-index mangle as the save-time bake ("ls-<i>-<name>").
    for (size_t si = 0; si < p.loadingScreens.size(); ++si) {
        const LoadingScreenDef& ls = p.loadingScreens[si];
        for (const HudImage& h : ls.images) take(binPathOf(h.imagePath), "startup");
        for (const LoadingBar& b : ls.bars)
            if (!b.segImage.imagePath.empty())
                take(binPathOf(b.segImage.imagePath), "startup");
        for (const HudText& t : ls.texts)
            take("hud/" + menubake::textFileName("ls-" + std::to_string(si) + "-" + t.name),
                 "startup");
    }
    // Boot splash images (shown before the first scene) belong to startup too.
    for (const SplashScreen& s : p.splashScreens)
        if (!s.image.imagePath.empty()) take(binPathOf(s.image.imagePath), "startup");
    for (const std::string& s : p.sounds) take(adpcmPathOf(s), "startup");
    for (const SceneData& s : p.scenes) {
        const std::string group = "scene:" + s.name;
        // The terrain material is compiled away (its Kd + texture index are
        // baked); only its map_Kd texture reaches the disc - group that.
        const std::string terrainTex =
            project::resolveTerrainMaterial(p, project::resolvedSettings(p, s).terrainMaterial)
                .texture;
        if (!terrainTex.empty()) take(binPathOf(terrainTex), group);
        for (const SceneObject& o : s.objects) {
            if (!o.materialPath.empty()) take(binPathOf(o.materialPath), group);
            // Models ship as build artifacts - a static .obj as its baked
            // .tmdl, an animated .glb as its .tskl - so the scene group has to
            // claim the artifact name. The source name is still tried: a
            // project whose bake was skipped keeps working (and it costs
            // nothing when the file is not in bin/).
            if (!o.modelPath.empty()) {
                take(binPathOf(templates::bakedModelPath(o.modelPath,
                                                         o.materialPath)),
                     group);
                take(binPathOf(o.modelPath), group);
            }
        }
    }
    for (const std::string& m : p.music) take(binPathOf(m), "music");
    for (const auto& rel : std::set<std::string>(remaining)) take(rel, "other");

    // Manual pins: <project>/iso-layout.txt, one bin-relative path per line.
    std::set<std::string> pinned;
    std::vector<std::string> pinOrder;
    {
        std::ifstream layout(fs::path(p.dir) / "iso-layout.txt");
        std::string line;
        while (std::getline(layout, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            size_t b = line.find_first_not_of(" \t");
            if (b == std::string::npos || line[b] == '#') continue;
            line = line.substr(b, line.find_last_not_of(" \t") - b + 1);
            std::replace(line.begin(), line.end(), '\\', '/');
            if (line == p.elfName()) continue;  // always first anyway
            bool inBin = false;
            for (const auto& f : autoOrder) inBin |= (f.rel == line);
            if (!inBin) {
                if (log) log("[editor] iso-layout.txt: '" + line + "' is not in bin/ - skipped.");
                continue;
            }
            if (pinned.insert(line).second) pinOrder.push_back(line);
        }
    }
    if (manualOrder) *manualOrder = !pinOrder.empty();

    out.clear();
    out.push_back(autoOrder.front());  // the ELF
    for (const auto& rel : pinOrder)
        for (const auto& f : autoOrder)
            if (f.rel == rel) out.push_back({f.rel, f.group, true});
    for (size_t i = 1; i < autoOrder.size(); ++i)
        if (!pinned.count(autoOrder[i].rel)) out.push_back(autoOrder[i]);
    return "";
}

// SYSTEM.CNF is generated on the fly (temp file); \r\n like retail discs.
static std::string writeSystemCnf(const std::string& elfIso, fs::path* out) {
    *out = fs::temp_directory_path() / "tyra-SYSTEM.CNF";
    std::ofstream f(*out, std::ios::trunc | std::ios::binary);
    f << "BOOT2 = cdrom0:\\" << elfIso << ";1\r\nVER = 1.00\r\n";
    if (!f) return "cannot write " + out->string();
    return "";
}

// Shared by plan() and build(): ordered files -> iso9660 entries + name checks.
static std::string makeEntries(const Project& p, const std::vector<OrderedFile>& ordered,
                               const fs::path& cnf, std::vector<iso9660::FileEntry>* entries,
                               const LogFn& log) {
    const fs::path bin = fs::path(p.dir) / "bin";
    std::set<std::string> seen;
    entries->push_back({cnf, "SYSTEM.CNF"});
    seen.insert("SYSTEM.CNF");
    for (const auto& f : ordered) {
        std::string isoPath = upper(f.rel);
        if (log) {
            for (const auto& part : fs::path(isoPath)) {
                const std::string comp = part.generic_string();
                if (comp.size() > 30)
                    log("[editor] Warning: '" + comp + "' is longer than 30 characters - "
                        "may not resolve on a real PS2. Consider renaming.");
                if (comp.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") !=
                    std::string::npos)
                    log("[editor] Warning: '" + comp + "' has characters outside "
                        "A-Z 0-9 . _ - - may not resolve on a real PS2. Consider renaming.");
            }
        }
        if (!seen.insert(isoPath).second)
            return "name collision after upper-casing: " + f.rel;
        entries->push_back({bin / f.rel, isoPath});
    }
    return "";
}

std::string plan(const Project& p, Plan* out, const LogFn& log) {
    std::vector<OrderedFile> ordered;
    std::string err = orderFiles(p, ordered, &out->manualOrder, log);
    if (!err.empty()) return err;

    fs::path cnf;
    if (err = writeSystemCnf(upper(p.elfName()), &cnf); !err.empty()) return err;

    std::vector<iso9660::FileEntry> entries;
    err = makeEntries(p, ordered, cnf, &entries, log);
    if (err.empty()) {
        iso9660::PlannedImage img;
        err = iso9660::plan(entries, &img);
        if (err.empty()) {
            out->items.clear();
            out->dataStartLba = img.dataStartLba;
            out->totalSectors = img.totalSectors;
            for (size_t i = 0; i < img.files.size(); ++i) {
                const auto& f = img.files[i];
                PlanItem item;
                item.isoPath = f.isoPath;
                item.lba = f.lba;
                item.size = f.size;
                item.sectors = (f.size + 2047) / 2048;
                if (i == 0) {
                    item.group = "boot";
                } else {
                    item.relPath = ordered[i - 1].rel;
                    item.group = ordered[i - 1].group;
                    item.pinned = ordered[i - 1].pinned;
                }
                out->items.push_back(item);
            }
        }
    }
    std::error_code ec;
    fs::remove(cnf, ec);
    return err;
}

std::string build(const Project& p, const LogFn& log) {
    std::vector<OrderedFile> ordered;
    bool manual = false;
    std::string err = orderFiles(p, ordered, &manual, log);
    if (!err.empty()) return err;
    if (manual) log("[editor] Applied pinned order from iso-layout.txt.");

    fs::path cnf;
    if (err = writeSystemCnf(upper(p.elfName()), &cnf); !err.empty()) return err;

    std::vector<iso9660::FileEntry> entries;
    std::map<std::string, const OrderedFile*> byIso;
    err = makeEntries(p, ordered, cnf, &entries, log);

    fs::path iso;
    std::vector<iso9660::PlacedFile> placed;
    if (err.empty()) {
        std::string volume = upper(p.name).substr(0, 32);
        for (char& c : volume)
            if (std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_").find(c) ==
                std::string::npos)
                c = '_';
        for (size_t i = 1; i < entries.size(); ++i) byIso[entries[i].isoPath] = &ordered[i - 1];
        iso = fs::path(p.dir) / (p.name + ".iso");
        err = iso9660::write(iso, volume, entries, &placed);
    }
    std::error_code ec;
    fs::remove(cnf, ec);
    if (!err.empty()) return err;

    log("[editor] Disc layout (LBA order = physical order):");
    for (const auto& f : placed) {
        const OrderedFile* of = byIso.count(f.isoPath) ? byIso[f.isoPath] : nullptr;
        std::string group = of ? of->group : "boot";
        if (of && of->pinned) group += "*";
        char line[512];
        snprintf(line, sizeof(line), "[editor]   LBA %6u  %9u B  %-12s %s", f.lba, f.size,
                 group.c_str(), f.isoPath.c_str());
        log(line);
    }
    char sum[256];
    snprintf(sum, sizeof(sum), "[editor] ISO written: %s (%.1f MB, %zu files)",
             iso.string().c_str(), (double)fs::file_size(iso, ec) / (1024.0 * 1024.0),
             placed.size());
    log(sum);
    log("[editor] Boot it in PCSX2 (System > Start File) or burn to DVD-R. Tip: "
        "Project > Disc Layout... reorders files on the disc.");
    return "";
}

// --- FreeDVDBoot -----------------------------------------------------------
//
// The exploit lives entirely in two files the user supplies; the third name in
// VIDEO_TS/ is the ELF to launch. We only place them - see docs/freedvdboot.md
// for what actually happens on the console.
static constexpr const char* kFdvdbTrigger = "VIDEO_TS.IFO";
static constexpr const char* kFdvdbSetup = "VTS_01_0.IFO";
static constexpr const char* kFdvdbProgram = "VTS_02_0.IFO";  // = our ELF

// Partition logical block 0. It has to clear the anchor at sector 256, and
// sitting immediately after it keeps every file addressable by a short_ad.
static constexpr uint32_t kFdvdbPartitionStart = 257;

// The user may point at the version directory or at the VIDEO_TS inside it;
// both are the obvious thing to pick in a file dialog, so accept either.
static fs::path resolveVideoTs(const std::string& exploitDir) {
    std::error_code ec;
    const fs::path root(exploitDir);
    if (fs::is_regular_file(root / "VIDEO_TS" / kFdvdbTrigger, ec))
        return root / "VIDEO_TS";
    if (fs::is_regular_file(root / kFdvdbTrigger, ec)) return root;
    return {};
}

bool isFreeDvdBootDir(const std::string& exploitDir, std::string* why) {
    auto fail = [&](const std::string& msg) {
        if (why) *why = msg;
        return false;
    };
    if (exploitDir.empty()) return fail("no FreeDVDBoot folder set");
    std::error_code ec;
    if (!fs::is_directory(fs::path(exploitDir), ec))
        return fail("not a folder: " + exploitDir);
    const fs::path vts = resolveVideoTs(exploitDir);
    if (vts.empty())
        return fail(std::string("no VIDEO_TS/") + kFdvdbTrigger + " under " + exploitDir);
    if (!fs::is_regular_file(vts / kFdvdbSetup, ec))
        return fail(std::string("missing ") + kFdvdbSetup + " in " + vts.string());
    if (why) why->clear();
    return true;
}

// FreeDVDBoot's loader is still resident when the ELF it launches takes over,
// so an ELF whose segments cover the loader's own code hangs the console. The
// two ranges are documented in CTurt's README (github.com/CTurt/FreeDVDBoot).
struct MemRange {
    uint32_t lo, hi;  // inclusive
};
static constexpr MemRange kFdvdbLoaderRanges[] = {{0x84000, 0x85fff}, {0x250000, 0x29ffff}};

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}
static uint16_t rd16(const uint8_t* p) {
    return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8));
}

// Reports every PT_LOAD segment that overlaps the loader. Returns an error only
// when the ELF cannot be parsed at all; a clash is reported through out.
static std::string checkElfAgainstLoader(const fs::path& elf, std::vector<std::string>* clashes) {
    std::ifstream f(elf, std::ios::binary);
    if (!f) return "cannot read " + elf.string();
    uint8_t hdr[52];
    if (!f.read((char*)hdr, sizeof(hdr))) return "not an ELF (too short): " + elf.string();
    if (memcmp(hdr, "\x7f" "ELF", 4) != 0 || hdr[4] != 1 || hdr[5] != 1)
        return "not a 32-bit little-endian ELF: " + elf.string();

    const uint32_t phoff = rd32(&hdr[0x1c]);
    const uint16_t phentsize = rd16(&hdr[0x2a]);
    const uint16_t phnum = rd16(&hdr[0x2c]);
    if (phentsize < 32 || phnum == 0) return "ELF has no program headers: " + elf.string();

    for (uint16_t i = 0; i < phnum; ++i) {
        f.seekg((std::streamoff)phoff + (std::streamoff)i * phentsize);
        uint8_t ph[32];
        if (!f.read((char*)ph, sizeof(ph))) return "truncated program header: " + elf.string();
        if (rd32(&ph[0]) != 1) continue;  // PT_LOAD only
        const uint32_t vaddr = rd32(&ph[0x08]), memsz = rd32(&ph[0x14]);
        if (memsz == 0) continue;
        const uint32_t end = vaddr + memsz - 1;
        for (const MemRange& r : kFdvdbLoaderRanges) {
            if (vaddr > r.hi || end < r.lo) continue;
            char msg[160];
            snprintf(msg, sizeof(msg),
                     "segment 0x%08X-0x%08X overlaps the loader's 0x%X-0x%X", vaddr, end,
                     r.lo, r.hi);
            clashes->push_back(msg);
        }
    }
    return "";
}

std::string buildFreeDvdBoot(const Project& p, const std::string& exploitDir, const LogFn& log) {
    std::string why;
    if (!isFreeDvdBootDir(exploitDir, &why))
        return "FreeDVDBoot folder: " + why +
               " - download Filesystems/<your DVD Player version>/ from "
               "github.com/CTurt/FreeDVDBoot and set it in Edit > Preferences.";
    const fs::path vts = resolveVideoTs(exploitDir);

    std::vector<OrderedFile> ordered;
    bool manual = false;
    std::string err = orderFiles(p, ordered, &manual, log);
    if (!err.empty()) return err;
    if (manual) log("[editor] Applied pinned order from iso-layout.txt.");

    // Which program the exploit launches. Booting the game ELF *directly* is
    // the nicer disc - insert it and the game runs - but FreeDVDBoot's loader
    // is still resident when that ELF takes over, so it only works for an ELF
    // that stays clear of the loader's code. A Tyra game does NOT: even an
    // empty project links at 0x100000 and its BSS runs past 0x38F000, straight
    // through 0x250000-0x29FFFF. So the normal disc chainloads through
    // uLaunchELF instead - the initial program CTurt ships in the same folder,
    // which has no such restriction - and the game is picked from its browser.
    std::vector<std::string> clashes;
    if (err = checkElfAgainstLoader(fs::path(p.elfPath()), &clashes); !err.empty()) return err;
    const bool directBoot = clashes.empty();
    std::error_code uec;
    const bool haveULaunchElf = fs::is_regular_file(vts / kFdvdbProgram, uec);
    if (!directBoot && !haveULaunchElf) {
        std::string msg =
            "this ELF cannot be launched directly by FreeDVDBoot, and the folder has no "
            + std::string(kFdvdbProgram) + " (uLaunchELF) to chainload through:";
        for (const std::string& c : clashes) msg += "\n  - " + c;
        msg += "\n  Download the complete Filesystems/<version>/ folder - "
               "VTS_02_0.IFO is uLaunchELF and this export needs it.";
        return msg;
    }
    if (directBoot)
        log("[editor] ELF clears FreeDVDBoot's reserved ranges (0x84000-0x85FFF, "
            "0x250000-0x29FFFF) - the disc boots straight into the game.");
    else
        log("[editor] ELF covers FreeDVDBoot's loader (" + clashes.front() +
            "), so the disc boots uLaunchELF - pick " + upper(p.elfName()) +
            " from its browser to start the game.");

    fs::path cnf;
    if (err = writeSystemCnf(upper(p.elfName()), &cnf); !err.empty()) return err;

    std::vector<iso9660::FileEntry> entries;
    err = makeEntries(p, ordered, cnf, &entries, log);

    fs::path iso;
    std::vector<iso9660::PlacedFile> placed;
    uint32_t tailStart = 0;
    // The three VIDEO_TS files go in right behind SYSTEM.CNF and the ELF, so
    // the DVD Player's reads stay near the front of the disc.
    const std::array<std::pair<const char*, fs::path>, 3> videoTs{{
        {kFdvdbTrigger, vts / kFdvdbTrigger},
        {kFdvdbSetup, vts / kFdvdbSetup},
        // Either the game itself or uLaunchELF - see the mode choice above.
        // The game ELF is on the disc under its own name regardless, so the
        // chainloaded case has something to find (and SYSTEM.CNF something to
        // name on a modded console).
        {kFdvdbProgram, directBoot ? fs::path(p.elfPath()) : vts / kFdvdbProgram},
    }};
    if (err.empty()) {
        std::vector<iso9660::FileEntry> withVts;
        withVts.push_back(entries[0]);  // SYSTEM.CNF
        if (entries.size() > 1) withVts.push_back(entries[1]);  // the ELF
        for (const auto& [name, src] : videoTs)
            withVts.push_back({src, std::string("VIDEO_TS/") + name});
        for (size_t i = 2; i < entries.size(); ++i) withVts.push_back(entries[i]);
        entries.swap(withVts);

        std::string volume = upper(p.name).substr(0, 32);
        for (char& c : volume)
            if (std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_").find(c) ==
                std::string::npos)
                c = '_';

        iso9660::Options opt;
        // Sectors 0..256 belong to the UDF volume structures and the anchor;
        // the File Set Descriptor and its terminator take the two after that.
        opt.firstDataLba = udf::firstFreeLba(kFdvdbPartitionStart);
        opt.tailReserveSectors = udf::tailSectors(videoTs.size());

        iso9660::PlannedImage img;
        err = iso9660::plan(entries, &img, opt);
        if (err.empty()) {
            tailStart = img.totalSectors - opt.tailReserveSectors;
            iso = fs::path(p.dir) / (p.name + "-fdvdb.iso");
            err = iso9660::write(iso, volume, entries, &placed, opt);
        }
    }
    std::error_code ec;
    fs::remove(cnf, ec);
    if (!err.empty()) return err;

    // Second filesystem over the same data: what the DVD Player actually reads.
    udf::Options uopt;
    uopt.volumeId = "TYRAX";
    uopt.partitionStartLba = kFdvdbPartitionStart;
    uopt.tailStartLba = tailStart;
    for (const auto& [name, src] : videoTs) {
        (void)src;
        const std::string iso9660Path = std::string("VIDEO_TS/") + name;
        const auto it = std::find_if(placed.begin(), placed.end(), [&](const auto& f) {
            return f.isoPath == iso9660Path;
        });
        if (it == placed.end()) return std::string("internal: ") + name + " was not placed";
        uopt.videoTs.push_back({name, it->lba, it->size});
    }
    if (err = udf::overlay(iso, uopt); !err.empty()) return err;

    for (const udf::FileRef& f : uopt.videoTs) {
        char line[256];
        snprintf(line, sizeof(line), "[editor]   UDF /VIDEO_TS/%-14s LBA %6u  %9u B",
                 f.name.c_str(), f.lba, f.size);
        log(line);
    }
    char sum[512];
    snprintf(sum, sizeof(sum),
             "[editor] FreeDVDBoot ISO written: %s (%.1f MB, %zu files)", iso.string().c_str(),
             (double)fs::file_size(iso, ec) / (1024.0 * 1024.0), placed.size());
    log(sum);
    log(std::string("[editor] Burn to DVD-R at the lowest speed, finalised. Set the "
                    "console's system language to English, then insert the disc - "
                    "nothing has to be installed on the PS2. ") +
        (directBoot ? "The game starts on its own."
                    : "uLaunchELF comes up; open the DVD and run " + upper(p.elfName()) +
                          ".") +
        " PCSX2 cannot boot this path (it does not emulate the DVD Player); it still "
        "boots the disc as a plain PS2 image.");
    return "";
}

std::string saveManualOrder(const Project& p, const std::vector<std::string>& relPaths) {
    const fs::path file = fs::path(p.dir) / "iso-layout.txt";
    std::error_code ec;
    if (relPaths.empty()) {
        fs::remove(file, ec);
        return "";
    }
    std::ofstream f(file, std::ios::trunc);
    if (!f) return "cannot write " + file.string();
    f << "# Disc order for Export PS2 ISO (bin-relative paths, top = lowest LBA).\n"
         "# Managed by the editor's Disc Layout window; delete this file to\n"
         "# return to the automatic group order.\n";
    for (const auto& rel : relPaths) f << rel << "\n";
    if (!f) return "cannot write " + file.string();
    return "";
}

}  // namespace isoexport
