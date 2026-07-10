#include "isoexport.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

#include "iso9660.hpp"

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
        if (rel == "log.txt") continue;
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
    for (const std::string& s : p.sounds) take(adpcmPathOf(s), "startup");
    for (const SceneData& s : p.scenes) {
        const std::string group = "scene:" + s.name;
        const std::string terrainTex = project::resolvedSettings(p, s).terrainTexture;
        if (!terrainTex.empty()) take(binPathOf(terrainTex), group);
        for (const SceneObject& o : s.objects) {
            if (!o.texturePath.empty()) take(binPathOf(o.texturePath), group);
            if (!o.modelPath.empty()) take(binPathOf(o.modelPath), group);
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
