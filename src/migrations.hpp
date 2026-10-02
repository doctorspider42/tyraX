#pragma once

#include <string>
#include <vector>

struct Project;

// Project-format migrations. A step upgrades a loaded project from format
// `from` to `from + 1`; project::load's tolerant reader has already parsed the
// file (every field it does not find simply defaults, so parsing never needs
// the version - and anything OLDER than version::kMinFormatVersion is refused
// there before a step could run), and a step transforms the in-memory model
// where the old data's
// MEANING changed - rescaled units, split/merged fields, restructured
// references. A step that needs data the reader no longer parses can re-read
// files itself via Project::dir. Steps run in order and the chain covers any
// version gap, so a project can jump several versions in one go.
//
// Not every format bump has a step: purely additive changes (new fields with
// safe defaults) bump version::kFormatVersion without registering anything
// here, and such projects open silently. The migration prompt + backup in the
// editor appear exactly when stepsFor() is non-empty - when something
// irreversible is about to happen.
namespace migrations {

// One constraint on every step, learned by writing a throwaway one: a step must
// NOT change Project::name. save() writes <name>.tyra and does not remove a
// manifest under the old name, so a renaming step leaves TWO .tyra files in the
// project directory - and load() takes whichever the directory iterator yields
// first, which can be the pre-migration one. Rename fields, not the project.
struct Migration {
    int from;             // upgrades from this format version to from + 1
    const char* summary;  // one line for the migration prompt and logs
    // Transforms the loaded project in memory. Returns false + err when the
    // project cannot be migrated (the caller aborts and leaves disk untouched).
    bool (*apply)(Project& p, std::string& err);
};

// Every registered step, ordered by ascending `from` - run() applies them in
// registration order, so an out-of-order entry would transform data a later
// step still expects untouched. Checked by validate() rather than by eye.
const std::vector<Migration>& all();

// Registry sanity: ascending, unique `from`, each inside [0, kFormatVersion),
// and each step actually filled in. Returns "" when the registry is sound.
//
// Two places run it, on purpose. all() shouts on stderr at the registry's FIRST
// USE, because the likeliest authoring mistake - a step registered without the
// matching kFormatVersion bump - makes stepsFor() return nothing, so the gate
// never fires, run() is never reached, and the step would silently never run.
// run() also checks, and there a bad registry aborts the migration with disk
// untouched rather than transforming data in the wrong order.
//
// Deliberately NOT a "no gaps" check: a purely additive format bump registers no
// step at all, so missing versions in the chain are the normal case (stepsFor
// simply finds nothing to do for that bump). What must hold is that the steps
// which ARE registered are ordered and in range.
std::string validate();

// The steps needed to lift a project saved at `fileVersion` to the current
// version::kFormatVersion. Empty = nothing to transform: open silently (the
// version is re-stamped on the next save).
std::vector<const Migration*> stepsFor(int fileVersion);

// Runs stepsFor(fileVersion) on the loaded project. Returns "" on success or
// "step vN -> vN+1 (<summary>): <error>" on the first failing step. Purely
// in-memory - the caller decides when to write (save only after success).
std::string run(Project& p, int fileVersion);

// Copies the format-bearing project files (<name>.tyra, objects/,
// terrain-*.heights, terrain-*.splat, flow-nodes/, screen-effects/) into
// <dir>/_backup/format-v<fileVersion>-<timestamp>/ before a migration.
// res/ assets are skipped - migrations do not touch them. Returns "" on
// success and reports the created directory via backupDir.
//
// This list has to cover everything the post-migration save writes
// (project::save + saveHeights + saveSplat): a file the save overwrites but the
// backup skipped is unrecoverable. Adding a persisted file to project.cpp means
// adding it here too.
std::string backup(const Project& p, int fileVersion, std::string& backupDir);

}  // namespace migrations
