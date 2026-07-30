// Who is debugging what, right now - a pointer every running editor publishes.
//
// The devkit's channels all live inside one project's bin/, and projects live
// wherever the user put them. So the first question anyone has ("which project
// is live?") had no answer on disk: editor.ini's recent list is written when a
// project is OPENED, which is both too rare (a project opened before the list
// existed is missing) and too indirect (it says what was opened, not what is
// open NOW). A running editor knows exactly, so it says so.
//
// One small file per process, named after the pid, in a per-user state folder:
//
//   Windows  %LOCALAPPDATA%\tyra-editor\sessions\<pid>.ini
//   else     $XDG_STATE_HOME/tyra-editor/sessions/<pid>.ini
//            (or ~/.local/state/... when XDG_STATE_HOME is unset)
//
// A file per pid rather than one shared file is what makes several editors work
// at once - and they DO run at once here (parallel worktrees, a second instance
// to join a collaboration session). No locking, no merge, no last-writer-wins:
// each process owns exactly one file and deletes it on the way out.
//
// Liveness is the HEARTBEAT, not the pid: asking the OS whether a pid is alive
// differs per platform and lies after pid reuse, while "this file was touched
// four seconds ago" means the same thing everywhere. A session whose heartbeat
// stopped is reported as stale rather than hidden - a crashed editor leaving
// its last known project behind is information, not noise.
#pragma once

#include <string>
#include <vector>

namespace devsession {

struct Info {
    int pid = 0;
    long long started = 0;    // epoch seconds
    long long heartbeat = 0;  // epoch seconds, refreshed by the running editor
    std::string project;      // absolute project directory ("" = none open)
    std::string name;         // project name, for a human-readable line
    std::string scene;        // active scene
    std::string profile;      // "debug" | "release"
    bool liveDebug = false;
    bool liveLink = false;
    // What the editor believes about the GAME, which no file in bin/ states
    // outright: whether snapshots are arriving, at which frame, and over which
    // transport. On ps2link the host file server (ps2client) is spawned BY the
    // editor, so closing the editor freezes every devkit file mid-session -
    // knowing the transport is what turns that from a mystery into a fact.
    bool gameLive = false;
    unsigned gameFrame = 0;
    bool gameHalted = false;
    std::string transport;  // "pcsx2" | "ps2link" | ""

    long long ageSeconds() const;  // since the last heartbeat
    bool live() const;             // heartbeat younger than the stale cutoff
};

/** The sessions folder (created on demand). Empty if there is no home. */
std::string dir();

/** Publish/refresh this process's pointer. Cheap enough for a few times a
 * second; the editor calls it on project changes and on a slow timer. */
bool publish(const Info& info);

/** Remove this process's pointer (clean exit). */
void retire(int pid);

/** Every session on this machine, freshest heartbeat first. Files whose
 * heartbeat is older than a day are deleted as they are read. */
std::vector<Info> list();

/** This process's id, whatever the platform calls it. */
int selfPid();

}  // namespace devsession
