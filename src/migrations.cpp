#include "migrations.hpp"

#include <ctime>
#include <filesystem>
#include <system_error>

#include "project.hpp"
#include "version.hpp"

namespace fs = std::filesystem;

namespace migrations {

const std::vector<Migration>& all() {
    // Format history. v0 -> v1 needs no step: v1 only introduced the
    // formatVersion/editorVersion stamp, and project::load's legacy shims
    // already lift every pre-v1 shape (inline objects, single "layout",
    // project-level terrain, ...) on plain load.
    static const std::vector<Migration> steps = {};
    return steps;
}

std::vector<const Migration*> stepsFor(int fileVersion) {
    std::vector<const Migration*> out;
    for (const Migration& m : all())
        if (m.from >= fileVersion && m.from < version::kFormatVersion)
            out.push_back(&m);
    return out;
}

std::string run(Project& p, int fileVersion) {
    for (const Migration* m : stepsFor(fileVersion)) {
        std::string err;
        if (!m->apply(p, err))
            return "step v" + std::to_string(m->from) + " -> v" +
                   std::to_string(m->from + 1) + " (" + m->summary + "): " + err;
    }
    return "";
}

std::string backup(const Project& p, int fileVersion, std::string& backupDir) {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char stamp[32];
    std::snprintf(stamp, sizeof(stamp), "%04d%02d%02d-%02d%02d%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                  tm.tm_min, tm.tm_sec);
    const fs::path root = fs::path(p.dir);
    const fs::path dest = root / "_backup" /
                          ("format-v" + std::to_string(fileVersion) + "-" + stamp);
    std::error_code ec;
    fs::create_directories(dest, ec);
    if (ec) return "cannot create backup directory: " + dest.string();

    auto copyDir = [&](const char* name) -> std::string {
        const fs::path src = root / name;
        if (!fs::is_directory(src, ec)) return "";
        fs::copy(src, dest / name,
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing,
                 ec);
        if (ec) return "cannot back up " + src.string() + ": " + ec.message();
        return "";
    };

    // The format-bearing files at the project root: the .tyra manifest(s), the
    // per-scene heightmaps (incl. the legacy single-scene terrain.heights) and
    // the per-scene splat sidecars. This list must cover everything the
    // migration save writes (project::save + saveHeights + saveSplat) - a file
    // the save overwrites but the backup skipped could not be restored.
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string fn = entry.path().filename().string();
        const std::string ext = entry.path().extension().string();
        const bool manifest = ext == ".tyra";
        const bool terrain = fn == "terrain.heights" ||
                             (fn.rfind("terrain-", 0) == 0 &&
                              (ext == ".heights" || ext == ".splat"));
        if (!manifest && !terrain) continue;
        fs::copy_file(entry.path(), dest / fn,
                      fs::copy_options::overwrite_existing, ec);
        if (ec) return "cannot back up " + fn + ": " + ec.message();
    }
    for (const char* d : {"objects", "flow-nodes", "screen-effects"})
        if (auto err = copyDir(d); !err.empty()) return err;

    backupDir = dest.string();
    return "";
}

}  // namespace migrations
