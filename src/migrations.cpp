#include "migrations.hpp"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <system_error>

#include "project.hpp"
#include "version.hpp"

namespace fs = std::filesystem;

namespace migrations {

// The registry check itself, over the vector rather than over all(), so all()
// can run it during its own initialization without recursing.
static std::string check(const std::vector<Migration>& steps) {
    for (size_t i = 0; i < steps.size(); ++i) {
        const Migration& m = steps[i];
        const std::string which =
            "step v" + std::to_string(m.from) + " -> v" + std::to_string(m.from + 1);
        if (m.from < version::kMinFormatVersion ||
            m.from >= version::kFormatVersion)
            return which + " is outside the format range: this editor reads v" +
                   std::to_string(version::kMinFormatVersion) + " and writes v" +
                   std::to_string(version::kFormatVersion) +
                   ", so a step must start at v" +
                   std::to_string(version::kMinFormatVersion) +
                   " at the earliest and upgrade to v" +
                   std::to_string(version::kFormatVersion) +
                   " at the latest. Bump kFormatVersion in the same commit as "
                   "the step (version.hpp).";
        if (i > 0 && m.from <= steps[i - 1].from)
            return which + " is registered after v" +
                   std::to_string(steps[i - 1].from) + " -> v" +
                   std::to_string(steps[i - 1].from + 1) +
                   ", but run() applies steps in registration order - list them "
                   "by ascending 'from', once each.";
        if (!m.apply) return which + " has no apply function.";
        if (!m.summary || !*m.summary)
            return which + " has no summary, and the summary is what the "
                           "migration prompt shows the user.";
    }
    return "";
}

const std::vector<Migration>& all() {
    // Format history. There is no v0 -> v1 step and there cannot be one: the
    // reader no longer parses any pre-v1 shape, so such a file is refused at
    // the version::kMinFormatVersion gate before a step could see it.
    static const std::vector<Migration> steps = {};

    // Checked HERE, at the registry's first use, and not only in run(): the most
    // likely authoring mistake is registering a step and forgetting to bump
    // kFormatVersion, and that makes stepsFor() return NOTHING - so run() is
    // never reached, the gate never fires and the step silently never runs. The
    // symptom would be "my migration does nothing", which is a bad afternoon.
    // stepsFor() is consulted on every open and every headless command, so this
    // prints on the first one either way, in Release too (an assert would not).
    static const bool checked = [&] {
        if (const std::string e = check(steps); !e.empty())
            std::fprintf(stderr,
                         "[editor] BROKEN MIGRATION REGISTRY (migrations.cpp): %s\n"
                         "[editor] No project will be migrated until this is fixed.\n",
                         e.c_str());
        return true;
    }();
    (void)checked;
    return steps;
}

std::string validate() { return check(all()); }

std::vector<const Migration*> stepsFor(int fileVersion) {
    std::vector<const Migration*> out;
    for (const Migration& m : all())
        if (m.from >= fileVersion && m.from < version::kFormatVersion)
            out.push_back(&m);
    return out;
}

std::string run(Project& p, int fileVersion) {
    // Before the first transform, so a bad registry costs nothing: every caller
    // treats a non-empty return as "abort, disk untouched".
    if (std::string e = validate(); !e.empty())
        return "the migration registry is inconsistent (migrations.cpp): " + e;
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
    // per-scene heightmaps and the per-scene splat sidecars. This list must
    // cover everything the migration save writes (project::save + saveHeights +
    // saveSplat) - a file the save overwrites but the backup skipped could not
    // be restored.
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string fn = entry.path().filename().string();
        const std::string ext = entry.path().extension().string();
        const bool manifest = ext == ".tyra";
        const bool terrain = fn.rfind("terrain-", 0) == 0 &&
                             (ext == ".heights" || ext == ".splat");
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
