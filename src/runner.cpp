#include "runner.hpp"

#include "devsession.hpp"
#include "isoexport.hpp"
#include "elfsym.hpp"
#include "pcsx2_config.hpp"
#include "platform.hpp"
#include "templates.hpp"
#include "texbake.hpp"
#include "vehbake.hpp"
#include "wavconvert.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

// Process names to reap when clearing the field before a launch. The emulator
// is looked up by the basename of whatever path we would actually start, so a
// PCSX2 AppImage or a distro binary is covered as well as the stock names.
std::vector<std::string> emulatorProcessNames(const std::string& exe) {
    std::vector<std::string> names{"pcsx2-qt", "pcsx2"};
    if (!exe.empty()) {
        std::string base = fs::path(exe).filename().string();
        if (!base.empty() && base != "pcsx2-qt" && base != "pcsx2")
            names.push_back(base);
    }
    return names;
}

// --- who owns the ps2link file server --------------------------------------
//
// Exactly one `ps2client` can serve a console: it is the host: filesystem for
// the whole session, and the local ports it binds (TCP 18193 out, UDP 18194 in)
// are a machine-wide resource. That is why a deploy has to clear the field
// first - and for a long time it did so with `taskkill /F /IM ps2client.exe`,
// machine-wide, by name. Measured consequence: deploying project B killed
// project A's file server, and A's console kept running blocked on host: with
// its devkit files frozen at their last write, while the [ps2] log lines kept
// arriving (that stream is UDP straight to whichever ps2client is listening and
// never passes through the file server, so only the LIVE half of the transport
// was visible). See docs/ps2link-setup.md.
//
// A server identifies itself in its own command line, which is what the deploy
// spawns: `ps2client -h <ip> execee host:<name>.elf -ps2link`. So the console
// and the game are both in there, and the two together decide whether a server
// is this project's to reap. The residual limit is honest and worth knowing:
// the ELF name is the project's NAME, so two copies of one project (a worktree,
// a scratch copy of an example) look alike - the console check is what
// separates them, and two copies serving one console cannot both exist anyway.
struct Ps2Client {
    unsigned long long pid = 0;
    std::string ip;   // -h <ip>
    std::string elf;  // execee host:<elf>; empty for a `listen` witness
};

// Command line -> arguments. Deliberately small: it has to survive both a
// Windows command line (quoted paths) and a /proc/<pid>/cmdline joined with
// spaces, and all it is asked for is flags and bare tokens.
std::vector<std::string> splitArgs(const std::string& cmd) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false, started = false;
    for (char c : cmd) {
        if (c == '"') {
            quoted = !quoted;
            started = true;
        } else if (!quoted && (c == ' ' || c == '\t')) {
            if (started) out.push_back(cur);
            cur.clear();
            started = false;
        } else {
            cur += c;
            started = true;
        }
    }
    if (started) out.push_back(cur);
    return out;
}

std::vector<Ps2Client> ps2Clients() {
    std::vector<Ps2Client> out;
    for (const platform::RunningProcess& proc : platform::processesNamed("ps2client")) {
        Ps2Client c;
        c.pid = proc.pid;
        const std::vector<std::string> args = splitArgs(proc.commandLine);
        for (size_t i = 0; i < args.size(); i++) {
            if (args[i] == "-h" && i + 1 < args.size()) c.ip = args[i + 1];
            else if (args[i].rfind("host:", 0) == 0) c.elf = args[i].substr(5);
        }
        out.push_back(std::move(c));
    }
    return out;
}

// Is this server this project's, i.e. ours to clean up? Both halves matter: the
// ELF says which game and -h says which console, and a deploy must never reach
// across consoles even for a server running the same game. An unreadable
// command line leaves both empty, which fails the test - by design.
bool servesProject(const Ps2Client& c, const Project& p) {
    if (c.elf.empty() || c.elf != p.elfName()) return false;
    return c.ip.empty() || p.ps2LinkIp.empty() || c.ip == p.ps2LinkIp;
}

// The project directory of a running editor that owns this server, or "" when
// nobody does. A session already publishes its project (devsession.hpp) and the
// deployed ELF is <name>.elf, so the two match up with no second registry.
//
// "Running" is checked TWO ways on purpose. The heartbeat is the normal answer,
// but it stops while an editor sits in a native file dialog - that call blocks
// the UI thread - so a minute with a picker open would make a perfectly live
// session look abandoned. The pid being a live tyrax-editor process is the
// backstop, and the pair is what makes reaping an orphan safe: we only ever
// take a server nobody could still be using.
std::string ps2ClientOwner(const Ps2Client& c) {
    if (c.elf.empty()) return "";
    // Both the name we are running under and the stock one, so a harness or a
    // renamed check binary still SEES the real editor - the direction that must
    // not fail is "nobody owns it", since that one ends in a kill.
    std::vector<unsigned long long> editors;
    std::vector<std::string> names{"tyrax-editor"};
    if (const std::string self = fs::path(platform::exePath()).filename().string();
        !self.empty() && self != "tyrax-editor" && self != "tyrax-editor.exe")
        names.push_back(self);
    for (const std::string& n : names)
        for (const platform::RunningProcess& e : platform::processesNamed(n))
            editors.push_back(e.pid);
    for (const devsession::Info& s : devsession::list()) {
        if (s.project.empty() || s.name + ".elf" != c.elf) continue;
        if (s.pid == devsession::selfPid()) continue;  // us; our own is killed by handle
        bool alive = s.live();
        for (unsigned long long pid : editors) alive = alive || pid == (unsigned long long)s.pid;
        if (alive) return s.project;
    }
    return "";
}

std::string describe(const Ps2Client& c) {
    std::string s = "ps2client pid " + std::to_string(c.pid);
    if (!c.elf.empty()) s += " serving host:" + c.elf;
    else s += " (a listener, no game)";
    if (!c.ip.empty()) s += " on " + c.ip;
    return s;
}

}  // namespace

Runner::~Runner() {
    join();
    killPs2Client();
}

void Runner::join() {
    if (thread_.joinable()) thread_.join();
}

std::string Runner::log() const {
    std::lock_guard<std::mutex> lock(logMutex_);
    return log_;
}

void Runner::clearLog() {
    std::lock_guard<std::mutex> lock(logMutex_);
    log_.clear();
}

void Runner::appendLine(const std::string& line) {
    // Timestamp every line - over the network host: filesystem load times are
    // dominated by asset sizes, and the [ps2] log stream is the only way to
    // profile where a slow console boot actually spends its time.
    const std::string stamp = platform::logTimeStamp();
    std::lock_guard<std::mutex> lock(logMutex_);
    log_ += stamp;
    log_ += line;
    log_ += '\n';
}

void Runner::buildAndRun(const Project& p, bool runEmulator, bool rebuild) {
    if (busy()) return;
    join();
    cancelRequested_ = false;
    state_ = State::Running;
    thread_ = std::thread(&Runner::worker, this, p, true, runEmulator, false, rebuild);
}

void Runner::runEmulatorOnly(const Project& p) {
    if (busy()) return;
    join();
    cancelRequested_ = false;
    state_ = State::Running;
    thread_ = std::thread(&Runner::worker, this, p, false, true, false, false);
}

void Runner::buildAndRunPs2(const Project& p, bool build, bool rebuild) {
    if (busy()) return;
    join();
    cancelRequested_ = false;
    state_ = State::Running;
    thread_ = std::thread(&Runner::worker, this, p, build, true, true, rebuild);
}

void Runner::clean(const Project& p) {
    if (busy()) return;
    join();
    cancelRequested_ = false;
    state_ = State::Running;
    thread_ = std::thread([this, p] {
        appendLine("[editor] === Clean: " + p.name + " ===");
        // bin\ is locked by anything running out of it: ps2client keeps it as
        // its cwd (file server), PCSX2 holds the ELF. Kill ours by handle AND
        // the strays THIS PROJECT owns - an orphan from a previous editor
        // instance survives killPs2Client() and made remove_all fail with
        // "Access is denied". (POSIX has no such locking, but reaping our own
        // strays is right there too: they would otherwise keep serving a
        // half-deleted bin/.) A refusal is not fatal here - Clean never touches
        // the console, and the per-file report below names whatever stays
        // locked, which is a better answer than killing somebody else's server.
        claimPs2Channel(p);
        killEmulatorsFor(p, lastEmulator_);
        // Container game volume (obj + bin). Failure is fine - a stopped
        // container just means there is nothing cached there to clean.
        if (exec("docker compose exec -T compiler sh -c " +
                     platform::shellArg("rm -rf /src/obj /src/bin"),
                 p.dir) != 0)
            appendLine("[editor] Container not running - cleaned the host side only.");

        // Host bin\: per-file, clearing read-only first (remove_all refuses
        // those on Windows), retrying a few times (taskkill returns before
        // the killed process actually releases its handles), and naming the
        // file that stays locked - "Access is denied" alone is undebuggable.
        const fs::path bin = fs::path(p.dir) / "bin";
        std::string stuck;
        for (int attempt = 0; attempt < 4; attempt++) {
            if (attempt) platform::sleepMs(500);
            stuck.clear();
            std::error_code ec;
            for (fs::recursive_directory_iterator it(bin, ec), end; it != end;
                 it.increment(ec)) {
                if (it->is_regular_file(ec)) {
                    fs::permissions(it->path(), fs::perms::owner_write,
                                    fs::perm_options::add, ec);
                    fs::remove(it->path(), ec);
                    if (ec && stuck.empty()) stuck = it->path().string();
                }
            }
            std::error_code rmEc;
            fs::remove_all(bin, rmEc);  // now-empty tree (dirs + leftovers)
            if (!rmEc && !fs::exists(bin, rmEc)) {
                appendLine("[editor] Removed bin\\ - run a Build to regenerate.");
                state_ = State::Success;
                return;
            }
        }
        appendLine("[editor] Clean failed - still locked: " +
                   (stuck.empty() ? bin.string() : stuck) +
                   " (which program has it open? Explorer preview, an audio "
                   "player, the Debug window of another editor instance...)");
        state_ = State::Failed;
    });
}

void Runner::exportIso(const Project& p) {
    if (busy()) return;
    join();
    state_ = State::Running;
    thread_ = std::thread([this, p] {
        appendLine("[editor] === ISO export: " + p.name + " ===");
        const std::string err =
            isoexport::build(p, [this](const std::string& l) { appendLine(l); });
        if (!err.empty()) appendLine("[editor] ISO export failed: " + err);
        state_ = err.empty() ? State::Success : State::Failed;
    });
}

void Runner::cancel() {
    if (!busy()) return;
    cancelRequested_ = true;
    appendLine("[editor] Cancelling...");
    std::lock_guard<std::mutex> lock(execProcMutex_);
    // Kills the whole tree, not just the shell wrapper - the process doing the
    // work is docker/make, and orphaning it would leave the container busy.
    if (execProc_) execProc_->kill();
}

int Runner::exec(const std::string& cmdline, const std::string& cwd) {
    if (cancelRequested_) return -1;
    appendLine("> " + cmdline);

    platform::Process::Options opts;
    opts.cwd = cwd;
    opts.capture = true;
    std::unique_ptr<platform::Process> proc = platform::Process::start(cmdline, opts);
    if (!proc) {
        appendLine("[editor] Failed to start: " + cmdline);
        return -1;
    }
    platform::Process* raw = proc.get();
    {
        std::lock_guard<std::mutex> lock(execProcMutex_);
        execProc_ = std::move(proc);
    }

    // vcl's OPTIMISER-TIMEOUT CHATTER, collapsed to one line.
    //
    // A vu-lab build prints about fifty of these:
    //
    //   WARN: time out..
    //   WARN:  WARNING: failed to vuta via processing
    //
    // They are not errors and they are not even a cost. vcl gives an
    // optimisation attempt a wall-clock budget (`-t`, four seconds by default)
    // and abandons the attempt when it runs out - so the count is a property of
    // how busy the machine is, not of the code: one program timed out 29 times
    // inside a 44-file `-j24` build and twice when run alone, and the emitted
    // .vsm was byte-identical both ways (same 281 slots, same 121 unpaired).
    // Raising `-t` to 15 or 60 changed neither the count nor the output.
    //
    // So fifty lines of alarming-looking noise buried the diagnostics that DO
    // matter. They are counted and summarised instead. Only this exact pair is
    // swallowed: `no opt table`, `failed to convert all uta linear->raw` and
    // everything else vcl says still goes straight through, because those are
    // real failures (docs/vu-authoring.md, "Before the cliff: vcl gives up").
    auto isVclTimeoutChatter = [](const std::string& l) {
        if (l.find("time out..") != std::string::npos) return true;
        const size_t at = l.find("WARNING: failed to ");
        return at != std::string::npos &&
               l.find(" via ", at) != std::string::npos;
    };

    std::string line;
    int vclTimeouts = 0;
    while (raw->readLine(line)) {
        if (isVclTimeoutChatter(line)) {
            ++vclTimeouts;
            continue;
        }
        appendLine(line);
    }
    if (vclTimeouts > 0)
        appendLine("[editor] vcl abandoned " + std::to_string(vclTimeouts / 2) +
                   " optimisation attempt(s) on a time limit - harmless, the "
                   "emitted microcode is the same either way "
                   "(docs/vu-authoring.md).");
    const int code = raw->wait();
    {
        std::lock_guard<std::mutex> lock(execProcMutex_);
        execProc_.reset();
    }
    return code;
}

// Where a PCSX2 install lives is the one thing that is genuinely differently
// SHAPED per OS rather than just differently spelled, so it stays here instead
// of moving into platform.cpp: Windows has two Program Files roots, Linux has
// PATH plus flatpak plus a downloaded AppImage in the usual places.
static std::string findPCSX2() {
    std::error_code ec;
#ifdef _WIN32
    std::vector<fs::path> dirs;
    if (const char* pf = getenv("ProgramFiles")) dirs.push_back(fs::path(pf) / "PCSX2");
    if (const char* pf86 = getenv("ProgramFiles(x86)")) dirs.push_back(fs::path(pf86) / "PCSX2");
    for (const auto& dir : dirs)
        for (const char* exe : {"pcsx2-qt.exe", "pcsx2.exe"}) {
            fs::path p = dir / exe;
            if (fs::exists(p, ec)) return p.string();
        }
#else
    // A packaged install (deb/rpm/AUR) puts one of these on PATH.
    for (const char* exe : {"pcsx2-qt", "pcsx2"})
        if (platform::commandExists(exe)) return exe;
    // Debian/Ubuntu ship emulators in /usr/games, which is on the default PATH
    // but not on every trimmed one - probe it explicitly rather than telling a
    // user with a perfectly normal apt install that PCSX2 was not found.
    std::vector<fs::path> candidates{"/usr/games/pcsx2-qt", "/usr/games/pcsx2",
                                     "/var/lib/flatpak/exports/bin/net.pcsx2.PCSX2"};
    if (const fs::path home = platform::homeDir(); !home.empty()) {
        candidates.push_back(home / ".local" / "share" / "flatpak" / "exports" / "bin" /
                             "net.pcsx2.PCSX2");
        // The AppImage is the upstream-recommended download, and it lands
        // wherever the browser put it - probe the two usual spots by prefix.
        for (const char* dir : {"Applications", "Downloads"}) {
            const fs::path d = home / dir;
            if (!fs::exists(d, ec)) continue;
            for (const auto& e : fs::directory_iterator(d, ec)) {
                const std::string name = e.path().filename().string();
                if (name.rfind("pcsx2", 0) == 0 || name.rfind("PCSX2", 0) == 0)
                    if (e.path().extension() == ".AppImage") candidates.push_back(e.path());
            }
        }
    }
    for (const fs::path& c : candidates)
        if (fs::exists(c, ec)) return c.string();
#endif
    return "";
}

// The PCSX2 executable to launch: the project's configured path when set (and
// it exists), otherwise the Program Files auto-detect. Empty when neither is
// available (a configured-but-missing path resolves to empty, not the default).
static std::string resolveEmulator(const Project& p) {
    if (!p.emulatorPath.empty()) {
        std::error_code ec;
        return fs::exists(p.emulatorPath, ec) ? p.emulatorPath : "";
    }
    return findPCSX2();
}

std::string Runner::emulatorLogPath(const Project& p) const {
    const std::string exe = resolveEmulator(p);
    if (exe.empty()) return "";
    // emulog.txt lives in the "logs" folder next to PCSX2.ini's "inis" folder.
    const fs::path ini = pcsx2::findIni(exe);
    if (ini.empty()) return "";
    return (ini.parent_path().parent_path() / "logs" / "emulog.txt").string();
}

bool Runner::launchPCSX2(const Project& p) {
    const std::string exe = resolveEmulator(p);
    if (exe.empty()) {
        if (!p.emulatorPath.empty())
            appendLine("[editor] Configured emulator not found: " + p.emulatorPath +
                       " - check the path in Edit > Preferences.");
        else
            appendLine("[editor] PCSX2 not found. Install it or set the emulator "
                       "path in Edit > Preferences.");
        return false;
    }
    lastEmulator_ = exe;
    std::error_code ec;
    if (!fs::exists(p.elfPath(), ec)) {
        appendLine("[editor] ELF not found: " + p.elfPath() + " - build the project first.");
        return false;
    }

    // PCSX2's host: ELF loader silently gives up on a long path: the emulator
    // logs "ELF Loading: ..." and then the EE never reaches "is executing" -
    // a black window with nothing to go on. Measured on PCSX2 with this
    // engine: 145 characters boot, 147 do not (the "host:" prefix puts that
    // right at a 150-byte buffer). Say so instead of letting the user stare
    // at it. Worth checking on every platform, but it bites on Linux first -
    // a home directory plus a deep project tree passes 145 far sooner than
    // C:\Users\<name>\TyraProjects\<project> does.
    constexpr size_t kMaxElfPathChars = 145;
    if (p.elfPath().size() > kMaxElfPathChars) {
        appendLine("[editor] WARNING: the ELF path is " +
                   std::to_string(p.elfPath().size()) + " characters (" +
                   p.elfPath() +
                   ") - PCSX2 will load it and then fail to start the game. Move "
                   "the project somewhere shorter (at most " +
                   std::to_string(kMaxElfPathChars) + " characters up to and "
                   "including bin/<name>.elf).");
    }

    // Close a previous run of THIS project, and nothing else (see the function).
    killEmulatorsFor(p, exe);

    // The game appends its TYRA_LOG output to bin/log.txt (host fs); drop the
    // stale file so the Debug window shows only this run's log. The
    // ps2link.run marker must go too - a leftover from a PS2 deploy would
    // make this PCSX2 boot skip the IOP reset (see the generated main.cpp).
    std::error_code logEc;
    fs::remove(fs::path(p.dir) / "bin" / "log.txt", logEc);
    fs::remove(fs::path(p.dir) / "bin" / "ps2link.run", logEc);
    // Live Debugger channel (docs/live-debugger.md): both files describe a
    // session that is over. A stale livedbg.cmd would freeze the fresh boot at
    // whatever the last session was doing, and a stale livedbg.bin would read
    // as a game already reporting; the editor re-sends its breakpoints within
    // a tick of the new game coming up.
    fs::remove(fs::path(p.dir) / "bin" / "livedbg.bin", logEc);
    fs::remove(fs::path(p.dir) / "bin" / "livedbg.cmd", logEc);
    // The self-screenshot (docs/devkit.md): the last session's picture would
    // read as an answer to the first capture of this one, and two runs of the
    // same scene look alike enough that nobody would notice.
    fs::remove(fs::path(p.dir) / "bin" / "frame.tga", logEc);
    // Live Logic: the fresh build compiles every graph natively again, so
    // a leftover patch would make the game interpret a stale program.
    fs::remove(fs::path(p.dir) / "bin" / "livelogic.bin", logEc);
    // The time machine (docs/time-machine.md): a leftover livetime.rst would
    // teleport the fresh boot into the last session's world on its first poll,
    // and a stale livetime.bin would read as history that never happened.
    fs::remove(fs::path(p.dir) / "bin" / "livetime.bin", logEc);
    fs::remove(fs::path(p.dir) / "bin" / "livetime.rst", logEc);
    // Remote Pad (docs/remote-pad.md): a leftover state still says "attached"
    // and still holds whatever was held when the last session ended, so the
    // fresh boot would start walking before anyone touched anything.
    fs::remove(fs::path(p.dir) / "bin" / "livepad.bin", logEc);

    // Without "Host Filesystem" the ELF boots but every host: fopen fails,
    // so Tyra asserts on the first asset load. PCSX2 rewrites its ini on
    // exit - fix the setting now, right after the instance was killed.
    const fs::path ini = pcsx2::findIni(exe);
    if (ini.empty()) {
        appendLine("[editor] PCSX2.ini not found - if asset loading fails, enable "
                   "Settings > Emulation > 'Enable Host Filesystem' in PCSX2.");
    } else {
        switch (pcsx2::ensureHostFs(ini)) {
            case pcsx2::HostFsResult::Enabled:
                appendLine("[editor] Enabled 'Host Filesystem' in PCSX2 config (needed for host: asset loading).");
                break;
            case pcsx2::HostFsResult::WriteFailed:
                appendLine("[editor] Could not update " + ini.string() + " - enable "
                           "Settings > Emulation > 'Enable Host Filesystem' in PCSX2 manually.");
                break;
            case pcsx2::HostFsResult::AlreadyEnabled:
                break;
        }
        // Keyboard & mouse preference: point PCSX2's emulated USB ports at
        // the host keyboard/pointer so the ps2kbd/ps2mouse drivers the game
        // loads see real devices. Only touched while the preference is on.
        if (p.settings.keyboardMouse) {
            switch (pcsx2::ensureUsbKbdMouse(ini)) {
                case pcsx2::HostFsResult::Enabled:
                    appendLine(
                        "[editor] Configured PCSX2 USB ports for keyboard & "
                        "mouse (USB1 = keyboard, USB2 = mouse).");
                    break;
                case pcsx2::HostFsResult::WriteFailed:
                    appendLine("[editor] Could not update " + ini.string() +
                               " - set USB Port 1 to 'HID Keyboard' and USB "
                               "Port 2 to 'HID Mouse' in PCSX2 manually.");
                    break;
                case pcsx2::HostFsResult::AlreadyEnabled:
                    break;
            }
        }
    }

    appendLine("[editor] Launching PCSX2: " + exe);
    // (The ELF path is native-separator already: Project::filePath() applies
    // make_preferred, which is what PCSX2 needs - it refuses a boot ELF whose
    // path mixes separators.)
    if (!platform::Process::startDetached(platform::shellArg(exe) + " -elf " +
                                          platform::shellArg(p.elfPath()))) {
        appendLine("[editor] Failed to launch PCSX2.");
        return false;
    }
    return true;
}

// The PS2 deploy tools ship in the repo (tools/...); the editor binary lives
// in build/, so probe upward from it for the tools folder. Returns "" when the
// tool is nowhere to be found.
static std::string findTool(const std::string& relUnderTools) {
    const std::string exe = platform::exePath();
    if (exe.empty()) return "";
    fs::path dir = fs::path(exe).parent_path();
    for (int up = 0; up < 3; up++) {
        fs::path candidate = dir / "tools" / relUnderTools;
        std::error_code ec;
        if (fs::exists(candidate, ec)) return candidate.string();
        dir = dir.parent_path();
    }
    return "";
}

// Falls back to PATH so a system-wide ps2client install also works.
static std::string findPs2Client() {
    const std::string name = std::string("ps2client") + platform::exeSuffix();
    const std::string tool = findTool((fs::path("ps2client") / "bin" / name).string());
    return tool.empty() ? name : tool;
}

void Runner::killPs2Client() {
    std::shared_ptr<platform::Process> proc;
    {
        std::lock_guard<std::mutex> lock(ps2ClientMutex_);
        proc.swap(ps2Client_);  // clear first - ps2ClientAlive() polls it
    }
    if (proc) proc->kill();
    // The pump exits once the process (and thus its pipe) is gone. It holds
    // its own shared_ptr, so the Process outlives this scope until it joins.
    if (ps2Pump_.joinable()) ps2Pump_.join();
}

// Claim the ps2link file-server channel for this project, or explain who has it.
//
// The order is the whole design: OURS BY HANDLE first, because that is the
// common case (this editor redeploying) and the only certain one - the Process
// we spawned, killed as a tree. A SEARCH only for what the handle cannot reach:
// a server left behind by a run of this same project that did not shut down, or
// somebody else's session. Those are told apart by servesProject() above, and
// what is not ours is never killed on the strength of its name.
//
// Returns false when the caller must leave the console alone. The two ways that
// happens are deliberately different: a server a RUNNING editor owns is refused
// with the project named, because taking it is exactly the defect this replaced;
// a server nobody is running any more is an orphan and gets reaped, or a first
// deploy after a crash would be impossible to make and the fix would have traded
// one bug for "only one deploy per boot works".
bool Runner::claimPs2Channel(const Project& p) {
    killPs2Client();

    std::vector<unsigned long long> reported;
    auto seen = [&reported](unsigned long long pid) {
        for (unsigned long long r : reported)
            if (r == pid) return true;
        reported.push_back(pid);
        return false;
    };

    // A foreign server may be a couple of seconds old and about to leave -
    // resetPs2Link runs a `ps2client listen` witness while it works, and another
    // editor mid-teardown holds one just as briefly. Poll before refusing.
    constexpr int kSettleTries = 12;  // 3 s
    std::vector<Ps2Client> blocking;
    for (int attempt = 0; attempt < kSettleTries; attempt++) {
        if (cancelRequested_) return false;
        blocking.clear();
        bool killedAny = false;
        for (const Ps2Client& c : ps2Clients()) {
            if (!servesProject(c, p)) {
                blocking.push_back(c);
                continue;
            }
            if (!seen(c.pid))
                appendLine("[editor] Reaping this project's own stale file "
                           "server (" + describe(c) +
                           ") - left behind by a run that did not shut down.");
            platform::killProcess(c.pid);
            killedAny = true;
        }
        if (!killedAny && blocking.empty()) return true;  // the field is clear
        platform::sleepMs(killedAny && blocking.empty() ? 150 : 250);
    }

    // Out of patience. Reap what nobody owns - a crashed editor's server is an
    // orphan, and refusing on one would make the first deploy after a crash
    // impossible - and refuse for anything a running editor still owns.
    bool refused = false;
    for (const Ps2Client& c : blocking) {
        const std::string owner = ps2ClientOwner(c);
        if (owner.empty()) {
            appendLine("[editor] Reaping an orphaned file server (" + describe(c) +
                       ") - no editor is running that could own it.");
            platform::killProcess(c.pid);
            continue;
        }
        refused = true;
        // Name the right shared thing: the same console can hold one file
        // server, and this PC can run one ps2client at all (it binds the tty
        // and file-request ports, docs/ps2link-setup.md). Both refuse; saying
        // "the console" about a server on a different one would read as a bug.
        const bool sameConsole =
            c.ip.empty() || p.ps2LinkIp.empty() || c.ip == p.ps2LinkIp;
        appendLine("[editor] The ps2link channel is already taken: " + describe(c) +
                   ", for the editor open on " + owner + ". " +
                   (sameConsole
                        ? "Only one ps2client can serve a console at a time"
                        : "Only one ps2client can run on this PC at a time - it "
                          "binds the ports the console answers on") +
                   ", so this would have killed that session - it does not. Stop "
                   "the game there (Build > Stop on PS2) or close that editor, "
                   "then run this again.");
    }
    if (refused) return false;
    platform::sleepMs(200);
    if (ps2Clients().empty()) return true;
    appendLine("[editor] A ps2client is still running and would not go away - "
               "the deploy needs the host: channel to itself.");
    return false;
}

// Close the PCSX2 instances booting THIS project's ELF, and only those.
//
// Nothing about an emulator is singular - several run at once here as a matter
// of course, one per worktree - so reaping every process called `pcsx2` ended
// measurements this editor had no business touching. The `-elf <path>` the
// launcher passes is the discriminator, and it is a PATH, so two copies of one
// project stay apart. An instance whose command line cannot be read is left
// alone and counted: guessing wrong is the failure this replaced.
void Runner::killEmulatorsFor(const Project& p, const std::string& exe) {
    int unreadable = 0;
    for (const std::string& name : emulatorProcessNames(exe)) {
        for (const platform::RunningProcess& proc : platform::processesNamed(name)) {
            if (proc.commandLine.empty()) {
                unreadable++;
                continue;
            }
            if (!platform::commandLineNamesPath(proc.commandLine, p.elfPath()))
                continue;
            appendLine("[editor] Closing the PCSX2 instance running this project "
                       "(pid " + std::to_string(proc.pid) + ").");
            platform::killProcess(proc.pid);
        }
    }
    if (unreadable > 0)
        appendLine("[editor] Left " + std::to_string(unreadable) +
                   " other PCSX2 process(es) alone - their command line could "
                   "not be read, so there is no telling whose they are.");
}

// Resets ps2link, and reports whatever the console says while it happens -
// WITHOUT treating silence as a verdict.
//
// `ps2client reset` is fire-and-forget UDP, so this used to run a ps2client in
// `listen` mode as a witness and retry the command until something came back.
// That was right when a reset still printed "unmounting" from the IOP side;
// ps2link r4 stopped unmounting anything (there is no reboot there to unmount
// for any more) and narrates the restart on the console's own screen instead,
// so a perfectly good reset now says nothing at all over the network. Reading
// that as "the console is hung" made the editor refuse to deploy to a healthy
// console - a false negative that is much worse than the missing signal it was
// meant to replace, because it blocks the thing it was checking.
//
// So: send it once, keep the listener because its output is genuinely useful
// when there is any, and let the caller's own timeout be the judge of a dead
// console - the execee that follows a deploy already reports one within 15 s.
//
// The caller is expected to have killed the deploy's file server first: a game
// polling host: every frame (Remote Pad, Live Link, the time machine) both
// keeps the IOP busy and holds the tty port this listener needs.
Runner::ResetResult Runner::resetPs2Link(const Project& p) {
    const std::string client = findPs2Client();

    platform::Process::Options opts;
    opts.capture = true;
    std::shared_ptr<platform::Process> listener = platform::Process::start(
        platform::shellArg(client) + " -h " + p.ps2LinkIp + " listen", opts);
    std::atomic<int> heard{0};
    std::thread pump;
    if (listener) {
        pump = std::thread([this, listener, &heard] {
            std::string line;
            while (listener->readLine(line)) {
                appendLine("[ps2] " + line);
                heard++;
            }
        });
    }

    ResetResult out = ResetResult::Sent;
    if (exec(platform::shellArg(client) + " -h " + p.ps2LinkIp + " -t 10 reset",
             "") != 0) {
        out = ResetResult::Failed;
    } else {
        // Long enough to catch anything the console does say, short enough not
        // to be a tax on every deploy. The settle wait belongs to the caller.
        for (int i = 0; i < 8 && heard.load() == 0 && !cancelRequested_; i++)
            platform::sleepMs(250);
        if (heard.load() > 0) out = ResetResult::Answered;
    }

    if (listener) listener->kill();
    if (pump.joinable()) pump.join();
    return out;
}

void Runner::stopPs2(const Project& p) {
    if (busy()) return;
    join();
    cancelRequested_ = false;
    state_ = State::Running;
    thread_ = std::thread([this, p] {
        appendLine("[editor] Stopping the game on the PS2...");
        // The file server goes first: the game loses host: (and stops flooding
        // the IOP with per-frame polls, which is what the reset has to cut
        // through), and the tty port is freed for the listener resetPs2Link
        // runs to hear the console with.
        //
        // A refusal stops the RESET, which is the half that matters: if another
        // editor's server has the console, whatever is running there is theirs,
        // and resetting would stop their game on a button that promises to stop
        // ours. Our own server is dead either way - claimPs2Channel kills it by
        // handle before it looks at anybody else's.
        if (!claimPs2Channel(p)) {
            appendLine("[editor] Not resetting the console - the game running "
                       "on it belongs to the session named above.");
            state_ = State::Failed;
            return;
        }
        if (!p.ps2LinkIp.empty()) {
            if (resetPs2Link(p) == ResetResult::Failed)
                appendLine("[editor] Could not reach ps2link at " + p.ps2LinkIp + ".");
            else
                appendLine("[editor] Reset sent - the console should be back in "
                           "ps2link. (r4 restarts without saying anything over the "
                           "network; it narrates it on the TV.)");
        }
        state_ = State::Success;
    });
}

void Runner::stopEmulator(const Project& p) {
    if (busy()) return;
    join();
    cancelRequested_ = false;
    state_ = State::Running;
    thread_ = std::thread([this, p] {
        appendLine("[editor] Stopping PCSX2...");
        killEmulatorsFor(p, lastEmulator_);
        state_ = State::Success;
    });
}

bool Runner::deployToPs2(const Project& p) {
    if (p.ps2LinkIp.empty()) {
        appendLine("[editor] No PS2 address configured - set 'PS2 (ps2link) IP' in "
                   "Edit > Preferences.");
        return false;
    }
    std::error_code ec;
    if (!fs::exists(p.elfPath(), ec)) {
        appendLine("[editor] ELF not found: " + p.elfPath() + " - build the project first.");
        return false;
    }
    const std::string client = findPs2Client();
    const std::string binDir = (fs::path(p.dir) / "bin").string();

    // One file server at a time: clear the previous deploy's ps2client (ours by
    // handle, this project's strays by their command line - a leftover from a
    // crashed editor would hold the local tty port and starve this one). It also
    // has to be gone before the reset: see resetPs2Link. A server that belongs
    // to somebody else's live session stops the deploy here, with the reason in
    // the log, rather than being killed the way it used to be.
    if (!claimPs2Channel(p)) return false;

    // Reset exactly like Stop does, and do not launch into a console that
    // never answered - the execee would just sit there until the 15 s timeout
    // below and report a firewall problem that isn't one.
    appendLine("[editor] Resetting ps2link at " + p.ps2LinkIp + "...");
    if (resetPs2Link(p) == ResetResult::Failed) {
        appendLine("[editor] Could not reach ps2link at " + p.ps2LinkIp +
                   " - is the PS2 on and running PS2LINK.ELF? (Check the IP in "
                   "Edit > Preferences.)");
        return false;
    }

    // Same as the PCSX2 path: the game appends TYRA_LOG to bin/log.txt over
    // host: (here: over the network); drop the stale file for the Debug window.
    std::error_code logEc;
    fs::remove(fs::path(binDir) / "log.txt", logEc);
    fs::remove(fs::path(binDir) / "livedbg.bin", logEc);
    fs::remove(fs::path(binDir) / "livedbg.cmd", logEc);
    fs::remove(fs::path(binDir) / "frame.tga", logEc);  // see the PCSX2 path
    fs::remove(fs::path(binDir) / "livelogic.bin", logEc);
    fs::remove(fs::path(binDir) / "livetime.bin", logEc);
    fs::remove(fs::path(binDir) / "livetime.rst", logEc);
    fs::remove(fs::path(binDir) / "livepad.bin", logEc);

    // ps2link passes execee arguments in a non-standard way that the game's
    // toolchain crt0 does not deliver, so "-ps2link" alone cannot be relied
    // on. This marker is the deploy signal instead: the game probes it over
    // host: before booting the engine (launchPCSX2 deletes it, so emulator
    // runs keep the stock IOP-reset path).
    if (std::ofstream marker(fs::path(binDir) / "ps2link.run"); marker)
        marker << "deployed by TyraX\n";
    else
        appendLine("[editor] Warning: could not write bin/ps2link.run marker.");

    // ps2link reboots the IOP, restarts its own image and reloads every IRX
    // before it listens again - the better part of ten seconds on the console,
    // and the tty only comes back with udptty at the end of it. Deploying into
    // the middle of that gets a game that loads and then waits forever on a
    // pad the freshly loaded padman has not finished handshaking (seen on the
    // console: "Curent pad(0,0) status: DISCONNECT" and a frozen Tyra logo).
    for (int i = 0; i < 40 && !cancelRequested_; i++) platform::sleepMs(250);
    if (cancelRequested_) return false;

    // The execee process is the host: file server for the whole game session -
    // it must outlive this build. -ps2link tells the game to skip the IOP
    // reset that would unload ps2link (see the generated main.cpp). cwd is
    // bin/, so the game's "host:" cwd maps to bin/ exactly like a PCSX2 run.
    appendLine("[editor] Launching on PS2: host:" + p.elfName() + " (assets served from " +
               binDir + ")");

    const std::string cmd = platform::shellArg(client) + " -h " + p.ps2LinkIp +
                            " execee host:" + p.elfName() + " -ps2link";
    platform::Process::Options opts;
    opts.cwd = binDir;
    opts.capture = true;
    std::shared_ptr<platform::Process> proc = platform::Process::start(cmd, opts);
    if (!proc) {
        appendLine("[editor] Failed to start: " + cmd);
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(ps2ClientMutex_);
        ps2Client_ = proc;
    }
    ps2Lines_ = 0;

    // Pump ps2client's output - including the console's printf/TYRA log
    // arriving over UDP 18194 - into the Output panel for the session's
    // lifetime. Reads block until the process dies and the pipe breaks. The
    // thread holds its own shared_ptr so the Process cannot be destroyed out
    // from under a blocked read by killPs2Client().
    ps2Pump_ = std::thread([this, proc] {
        std::string line;
        while (proc->readLine(line)) {
            appendLine("[ps2] " + line);
            ps2Lines_++;
        }
    });

    // ps2link commands are UDP fire-and-forget: against a dead IP both reset
    // and execee "succeed" and ps2client waits forever. The only real
    // liveness signal is the console's log output (ps2link narrates every
    // execee over UDP), so wait for the first line before claiming success.
    for (int waited = 0; ps2Lines_ == 0; waited += 250) {
        if (cancelRequested_) {
            killPs2Client();
            return false;
        }
        if (!proc->running()) {
            appendLine("[editor] ps2client exited during startup - see its output above.");
            killPs2Client();
            return false;
        }
        if (waited >= 15000) {
            appendLine("[editor] No response from " + p.ps2LinkIp + " within 15s - is "
                       "the PS2 on and running PS2LINK.ELF? (Check the IP in Edit > "
                       "Preferences and the firewall/port rules for ps2client.)");
            killPs2Client();
            return false;
        }
        platform::sleepMs(250);
    }
    appendLine("[editor] Game running on PS2. Keep the editor open - it is "
               "serving the game's files over the network.");
    return true;
}

void Runner::worker(Project p, bool build, bool run, bool ps2, bool rebuild) {
    bool ok = true;

    if (build) {
        appendLine(rebuild ? "[editor] === Rebuild started: " + p.name + " ==="
                           : "[editor] === Build started: " + p.name + " ===");

        // A user script in a stale namespace (the usual cause: the project was
        // renamed, or copied from an example and renamed) cannot compile, and
        // the toolchain's own diagnostic for it is forty lines of template noise
        // that never mentions the rename. Stop here instead - unlike the
        // refresh below, whose failures are I/O problems worth warning about and
        // continuing past, this one has no chance of producing a binary.
        if (auto err = project::checkScriptNamespaces(p); !err.empty()) {
            appendLine("[editor] " + err);
            appendLine("[editor] === Build FAILED ===");
            state_ = State::Failed;
            return;
        }

        // Keep docker files and generated sources in sync with the project
        // data (also migrates projects created with older editor versions).
        if (auto err = project::refreshGenerated(p); !err.empty())
            appendLine("[editor] Warning: could not refresh generated files: " + err);

        // Live Link handshake: drop any stale snapshot (the rebuilt game bakes
        // the current scene state - an old livelink.bin would re-apply
        // pre-build values at boot) and record the as-built structure this
        // build bakes in. The editor streams livelink.bin only while the
        // project still matches bin/livelink.sig (see project.hpp). With the
        // preference off (or a release profile) the poller isn't compiled, so
        // remove the sig too - the editor then shows no session at all.
        {
            std::error_code ec;
            fs::create_directories(fs::path(p.dir) / "bin", ec);
            fs::remove(fs::path(p.dir) / "bin" / "livelink.bin", ec);
            // texture hot-reload manifest: the fresh build re-bakes every
            // texture, so a stale reload list must not replay at boot
            fs::remove(fs::path(p.dir) / "bin" / "livetex.bin", ec);
            if (p.settings.buildProfile == "debug" && p.settings.liveLink) {
                std::ofstream sig(fs::path(p.dir) / "bin" / "livelink.sig",
                                  std::ios::trunc);
                if (sig) sig << project::liveLinkSigFile(p);
            } else {
                fs::remove(fs::path(p.dir) / "bin" / "livelink.sig", ec);
            }
        }

        // Vehicle bake: the .glb/.fbx of every definition -> body + wheel
        // .tmdl + colour palette in .res-baked/vehicles/ (docs/vehicles.md).
        // Before texbake, because texbake owns the .res-baked sweep.
        if (auto err = vehbake::bakeProject(
                p, [this](const std::string& l) { appendLine(l); });
            !err.empty())
            appendLine("[editor] Warning: " + err);

        // Texture bake: res/ -> .res-baked/ (PNG quantization per the project
        // policy; the generated Makefile copies .res-baked next to the ELF).
        if (auto err = texbake::bake(p, [this](const std::string& l) { appendLine(l); });
            !err.empty())
            appendLine("[editor] Warning: texture bake failed: " + err);

        const std::string dc = "docker compose exec -T compiler sh -c ";

        // Old template used a fixed container name shared by all projects;
        // remove such a leftover so `compose up` cannot hit a name conflict.
        exec(platform::quiet("docker rm -f tyra-game-compiler"), p.dir);

        // The shared engine volume, which compose no longer owns (it is
        // `external` - see TPL_COMPOSE). Idempotent: creating one that exists
        // succeeds and changes nothing, so this is simply "make sure it is
        // there" on every build rather than a first-run special case.
        exec(platform::quiet("docker volume create " +
                             templates::engineVolumeName()),
             p.dir);

        appendLine("[editor] Starting docker container (first run may download the Tyra image)...");
        // Plain `up -d`, not `up -d --build`: measured 0.32 s against 4.05 s,
        // and there is nothing to build any more (the compose file names the
        // stock image - see TPL_COMPOSE). What must NOT be optimised away is
        // the `up` itself, however cheap the container's state makes it look:
        // it is `up` that reconciles a RUNNING container with the compose file,
        // and the compose project name is the project's own name. Two
        // directories holding a project of the same name - a copy, a second
        // worktree - share it, so skipping `up` when a container answers means
        // building into whichever directory the live container happens to have
        // bind-mounted at /host. That produces a green build log and no ELF.
        const std::string up = rebuild ? "docker compose up -d --force-recreate"
                                       : "docker compose up -d";
        if (exec(up, p.dir) != 0) {
            appendLine("[editor] Failed to start docker container. Is Docker Desktop running?");
            ok = false;
        }

        // Rebuild: throw away every incremental shortcut this pipeline takes -
        // the game objects, the compiled engine and its VU1 microprograms - so
        // the next steps rebuild all of it from source. The way out when a
        // build goes wrong in a way an incremental build cannot see.
        if (ok && rebuild) {
            appendLine("[editor] Rebuild: dropping the game objects and the compiled engine...");
            exec(dc + platform::shellArg(
                     "rm -rf /src/obj /src/bin /tyra/engine/obj /tyra/engine/bin"),
                 p.dir);
        }

        // The Tyra engine is maintained inside the editor repo (vendor/tyra,
        // with the editor's fixes applied directly) and bind-mounted read-only
        // at /engine-src. Sync it into the shared build volume; when anything
        // changed (checksum compare - mounts have unreliable mtimes), rebuild
        // libtyra, force the VU1 microprograms (outside make's dependency
        // tracking) and drop the game ELF so it relinks against the new lib.
        if (ok && exec(dc + platform::shellArg("test -d /engine-src/engine"), p.dir) != 0) {
            appendLine("[editor] Engine sources not mounted at /engine-src - the editor "
                       "must run from its repo (vendor/tyra). Recreate the container "
                       "if docker-compose.yml just changed.");
            ok = false;
        }
        // The audsrv overlay below copies three files as one `&&` chain, so a
        // missing one aborts it *silently* and the damage only surfaces two
        // minutes later, wearing someone else's face: the header never gets
        // copied either, the game compiles against the image's stock PS2SDK one
        // and the build dies on "'audsrv_adpcm_set_volume_and_pan' was not
        // declared" - which reads like an engine bug and is not. Every TyraX
        // PACKAGED before 1.55.3 has exactly that hole (both packagers excluded
        // '*.a' from vendor/tyra and took the committed libaudsrv.a along with
        // the build leftovers), and a checkout has none of it, so the report
        // always came from a user the developer could not reproduce. Name it.
        if (ok && exec(dc + platform::shellArg(
                           "test -f /engine-src/audsrv/bin/audsrv.irx && "
                           "test -f /engine-src/audsrv/bin/libaudsrv.a && "
                           "test -f /engine-src/audsrv/bin/audsrv.h"),
                       p.dir) != 0) {
            appendLine("[editor] The vendored audsrv overlay is incomplete at "
                       "vendor/tyra/audsrv/bin - it needs audsrv.irx, libaudsrv.a and "
                       "audsrv.h. A TyraX installed before 1.55.3 is missing "
                       "libaudsrv.a: update the editor, or copy that one file into "
                       "the install's vendor/tyra/audsrv/bin from the repo.");
            ok = false;
        }
        if (ok) {
            ok = exec(dc + platform::shellArg(
                          "mkdir -p /tyra/engine && "
                           "cmp -s /engine-src/Makefile.base /tyra/Makefile.base || "
                           "cp /engine-src/Makefile.base /tyra/Makefile.base; "
                           // Overlay the TyraX audsrv fork (per-channel L/R panning for
                           // sound emitters) over the image's PS2SDK copies, so the engine
                           // embeds this IRX, links this EE lib and compiles against this
                           // header. These are BUILT ARTIFACTS of the sources vendored
                           // beside them - rebuilt by vendor/tyra/audsrv/build.sh, never by
                           // this pipeline (see that directory's README.md).
                           //
                           // Both copies are STAMPED, and that is load-bearing rather than
                           // tidy: `cp` sets the destination's mtime to now, PS2SDK headers
                           // reach the compiler through -I (so they are ordinary user
                           // headers that land in the .d files - 16 of this game's 18
                           // translation units list audsrv.h), and re-applying the overlay
                           // unconditionally therefore invalidated almost the whole game on
                           // EVERY build. That, together with the editor rewriting its
                           // generated sources, is why no build here was ever incremental.
                           //
                           // Two stamps, because they answer two different questions.
                           // The container-side one guards the files that live in the
                           // IMAGE, so a recreated container has no stamp and re-applies
                           // the overlay. The /tyra one guards the compiled ENGINE in the
                           // shared volume: when the vendored IRX changes, libtyra has to
                           // be relinked so the new one gets re-embedded.
                           "md5sum /engine-src/audsrv/bin/audsrv.irx "
                           "/engine-src/audsrv/bin/libaudsrv.a "
                           "/engine-src/audsrv/bin/audsrv.h > /tmp/audsrv.stamp; "
                           "if ! cmp -s /tmp/audsrv.stamp /usr/local/ps2dev/.audsrv-stamp 2>/dev/null; then "
                           "echo '[editor] Applying the vendored audsrv overlay...' && "
                           "cp /engine-src/audsrv/bin/audsrv.irx /usr/local/ps2dev/ps2sdk/iop/irx/audsrv.irx && "
                           "cp /engine-src/audsrv/bin/libaudsrv.a /usr/local/ps2dev/ps2sdk/ee/lib/libaudsrv.a && "
                           "cp /engine-src/audsrv/bin/audsrv.h /usr/local/ps2dev/ps2sdk/ee/include/audsrv.h && "
                           "cp /tmp/audsrv.stamp /usr/local/ps2dev/.audsrv-stamp; fi; "
                           "if ! cmp -s /tmp/audsrv.stamp /tyra/.audsrv-stamp 2>/dev/null; then "
                           // obj/irx/audsrv.o must go too: the .irx-em make rule depends only
                           // on the .irx-em file, not the IRX binary it embeds, so without
                           // this bin2s never re-runs and the OLD irx stays inside libtyra.
                           "rm -f /tyra/engine/bin/libtyra.a /tyra/engine/obj/irx/audsrv.o; "
                           "cp /tmp/audsrv.stamp /tyra/.audsrv-stamp; fi; "
                           "rsync -rlci --delete --exclude=obj --exclude=bin "
                           "/engine-src/engine/ /tyra/engine/ "
                           "| grep -v '^.d' > /tmp/engine-sync.txt; "
                           "if [ -s /tmp/engine-sync.txt ] || "
                           "[ ! -f /tyra/engine/bin/libtyra.a ]; then "
                           "echo '[editor] Engine sources changed - rebuilding "
                           "libtyra...' && "
                           // The VU1 microprograms sit outside make's dependency
                           // tracking (a .vclpp #includes .i/.h files nothing
                           // declares), so they are force-rebuilt - but ONLY when
                           // one of those actually changed. Nuking them on every
                           // engine edit meant a one-line change to a .cpp paid
                           // 109 s of vcl (measured, -j6), which is most of what
                           // an engine iteration cost. The extension list is the
                           // complete set the VU sources are built from and can
                           // include (grep the .vclpp/.i files: only .i and .h).
                           "if grep -qE '[.](vclpp|vcl|vsm|i|h)$' /tmp/engine-sync.txt; then "
                           "echo '[editor] VU1 sources changed - rebuilding the "
                           "microprograms (takes a minute or two)...'; "
                           "find /tyra/engine/obj -name '*vu1.o*' -delete 2>/dev/null; "
                           "fi; "
                           "cd /tyra/engine && make -j$(nproc) && rm -f /src/bin/*.elf; "
                          "fi"),
                      p.dir) == 0;
            if (!ok) appendLine("[editor] Engine sync/build failed.");
        }

        // Export PS2SDK headers for VS Code IntelliSense (one-time per machine).
        if (ok) {
            if (const fs::path cfg = platform::configDir(); !cfg.empty()) {
                fs::path sdkCache = cfg / "ps2sdk";
                std::error_code ec;
                // THREE trees, and `ports` is not optional even though it looks
                // like it. The game compiles with -I.../ps2sdk/ports/include
                // and `<tyra>` reaches libpng's `png.h`, which lives only
                // there - so a cache without it makes cpptools give up on the
                // whole translation unit ("#include errors detected. Squiggles
                // are disabled for this translation unit") in exactly the file
                // a script author is typing into. The `ports` test is what
                // gates the export, so a machine that cached the first two
                // before this existed re-exports rather than staying broken.
                if (!fs::exists(sdkCache / "ports" / "include", ec)) {
                    appendLine("[editor] Exporting PS2SDK headers for IntelliSense...");
                    fs::create_directories(sdkCache / "ee", ec);
                    fs::create_directories(sdkCache / "common", ec);
                    fs::create_directories(sdkCache / "ports", ec);
                    // Failure is non-fatal - IntelliSense just has fewer headers.
                    exec("docker compose cp compiler:/usr/local/ps2dev/ps2sdk/ee/include " +
                             platform::shellArg((sdkCache / "ee" / "include").string()),
                         p.dir);
                    exec("docker compose cp "
                         "compiler:/usr/local/ps2dev/ps2sdk/common/include " +
                             platform::shellArg((sdkCache / "common" / "include").string()),
                         p.dir);
                    exec("docker compose cp "
                         "compiler:/usr/local/ps2dev/ps2sdk/ports/include " +
                             platform::shellArg((sdkCache / "ports" / "include").string()),
                         p.dir);
                }
            }
        }

        if (ok) {
            appendLine("[editor] Syncing sources into container...");
            // No -c (checksum): it used to be needed because the editor rewrote
            // every generated file on every build, so mtimes lied - but hashing
            // res/ and .res-baked/ end to end on every build is O(assets), and
            // project::writeFile no longer touches a file whose content is
            // unchanged. Size+mtime is the honest comparison again.
            ok = exec(dc + platform::shellArg(
                          "rsync -a --delete --exclude=.git --exclude=.vscode "
                          "--exclude=obj --exclude=bin /host/ /src/"),
                      p.dir) == 0;
        }

        // The project's own VU sources - src/vu/*.cpp (VU1 programs) and
        // src/vu0/*.cpp (VU0 kernels), docs/vu-authoring.md.
        //
        // These are HOST C++: they run at build time and write the microprogram
        // the PS2 compiler then assembles. So they need a host compiler, and the
        // ps2dev image ships none - only the ee/iop/dvp cross toolchains. It is
        // installed once and stamped, exactly like the audsrv overlay above; a
        // recreated container pays for it again, which is a minute, and the
        // alternative is making every user of the editor install a C++ compiler
        // for a feature most projects never touch.
        //
        // Ordering matters twice over: AFTER the rsync, because the sources have
        // to be in the volume, and BEFORE make, because what this writes into
        // src/gen is what make compiles.
        if (ok && project::hasVuSources(p)) {
            appendLine("[editor] Building the project's VU sources...");
            ok = exec(dc + platform::shellArg(
                          "set -e; "
                          "if ! command -v g++ >/dev/null 2>&1; then "
                          "  echo '[editor] Installing a host C++ compiler in "
                          "the container (one time)...'; "
                          "  apt-get update -qq >/dev/null && "
                          "  DEBIAN_FRONTEND=noninteractive apt-get install -y "
                          "-qq --no-install-recommends g++ >/dev/null; "
                          "fi; "
                          "mkdir -p /src/src/gen /src/inc/scripts /src/obj; "
                          // TWO source directories, and either may be empty - a
                          // project can have kernels and no programs or the
                          // other way round. An unmatched glob stays a literal
                          // word in /bin/sh, which g++ would then try to open,
                          // so the list is collected instead of pasted in.
                          // Unquoted on purpose: the container's shell splits
                          // it back into arguments, which is the whole point.
                          "VUSRC=$(ls /src/src/vu/*.cpp /src/src/vu0/*.cpp "
                          "2>/dev/null); "
                          // The stamp gates the COMPILE, never the run.
                          //
                          // Gating the run was a mistake that cost an evening:
                          // the generated sources live under /src, the source
                          // rsync deletes what the host does not have, and the
                          // outputs the stamp vouched for were long gone - or
                          // worse, present but generated from a source that had
                          // since changed. A console spent an hour and a half
                          // running a microprogram from before a fix because a
                          // cache was sure it was current. So: the generator
                          // ALWAYS runs (it takes a fraction of a second and
                          // writes nothing when the content is unchanged, which
                          // is what keeps vcl out of a no-op build), and only
                          // the ~20 s of g++ is skipped.
                          "md5sum /src/vugen/* $VUSRC "
                          "> /tmp/vu.stamp 2>/dev/null; "
                          "if ! cmp -s /tmp/vu.stamp /tmp/vu.stamp.built || "
                          "! test -x /tmp/vugen; then "
                          // -O0: this program runs once and writes a few files;
                          // compiling it fast matters, running it does not.
                          "  g++ -std=c++17 -O0 -w -I/src/vugen -o /tmp/vugen "
                          "/src/vugen/*.cpp $VUSRC && "
                          "  cp /tmp/vu.stamp /tmp/vu.stamp.built; "
                          "fi; "
                          "/tmp/vugen /src/src/gen /src/inc/scripts; "
                          // EVERYTHING it generated goes back to the host, not
                          // just the manifest. The source rsync deletes what the
                          // host does not have, so container-only output is
                          // wiped at the start of the NEXT build - and the
                          // stamp would then happily skip regenerating it. That
                          // is exactly how a build came out with the header
                          // declaring vuscript::install and nothing defining
                          // it. As a bonus the generated microprograms are
                          // readable in the project, like every other generated
                          // file.
                          "mkdir -p /host/src/gen /host/inc/scripts && "
                          // Guarded per file, because a project may have only
                          // kernels or only programs and `cp` on an unmatched
                          // glob is a hard failure under set -e.
                          "for f in /src/src/gen/vu_script* "
                          "/src/src/gen/vu_scripts.manifest "
                          "/src/src/gen/vu0_script*; do "
                          "if [ -e $f ]; then cp $f /host/src/gen/; fi; done; "
                          "for f in /src/inc/scripts/vu_scripts.gen.hpp "
                          "/src/inc/scripts/vu0_script* "
                          "/src/inc/scripts/vu0_kernels.gen.hpp; do "
                          "if [ -e $f ]; then cp $f /host/inc/scripts/; fi; "
                          "done"),
                      p.dir) == 0;
            if (!ok)
                appendLine(
                    "[editor] A VU source failed to build. The errors above are "
                    "ordinary C++ errors from your own file in src/vu/ or "
                    "src/vu0/ - see docs/vu-authoring.md.");
        }

        if (ok) {
            appendLine("[editor] Compiling (PS2DEV toolchain)...");
            // -j like the engine build above: the game is a dozen-odd
            // translation units and every one of them parses the whole engine
            // header set (96s -> 54s on examples/showcase at -j6).
            ok = exec(dc + platform::shellArg("cd /src && make -j$(nproc)"), p.dir) == 0;
        }

        // Sound effects: res/sfx/*.wav -> bin/sfx/*.adpcm (PS2SDK adpenc).
        // Skipped per file when the .adpcm is already newer than its .wav.
        // IFS= disables word splitting so filenames with spaces survive the
        // per-file loop. Inner double-quotes around $f/$o would be cleaner but
        // cannot survive the cmd.exe /S + docker.exe argv unquoting (see exec);
        // single quotes would block the expansion we need, so empty IFS it is.
        // (platform::shellArg is what keeps the $-expansions below for the
        // CONTAINER's shell - without it /bin/sh empties them on the host.)
        // Globs cover two levels of sfx subfolders (res/sfx/steps/wood.wav);
        // an unmatched glob stays a literal word, which the -e test skips.
        //
        // A WAV whose name ends in `-loop.wav` is encoded with adpenc's `-L`,
        // which sets the SPU2 block loop flags so the voice REPEATS instead of
        // ending (docs/sound.md, "Looping samples"). That is the only way to
        // hold a continuous sound - an engine note, a siren - on this hardware:
        // the loop is a property of the ENCODED sample and not of the play
        // call, so nothing at runtime can turn a one-shot into a loop. The
        // convention is in the file name rather than in the project because
        // adpenc runs over `res/sfx` as a directory and has no access to the
        // model; it is the `*-lit.png` arrangement.
        // The staleness test is mtime PLUS, for a loop file, the encoded
        // header's own loop byte (offset 6 of the .adpcm): a project built
        // before -L existed has a bin/sfx/x-loop.adpcm NEWER than its WAV,
        // encoded as a one-shot - mtime alone would skip it for ever and the
        // engine note would play for a fifth of a second and stop, with no
        // error anywhere. Reading the byte back asks the FILE what it is
        // instead of trusting the calendar.
        // NO QUOTES OF ANY KIND may appear in this fragment. The block comment
        // above already says double quotes cannot survive the cmd.exe /S +
        // docker.exe argv unquoting - the first version of the loop-byte test
        // used them anyway and every Windows build died with the shell's
        // *Syntax error: end of file unexpected*: the quotes were stripped on
        // the way in and the -c string stopped PARSING, so no build with a
        // sound in it could succeed on that platform while Linux passed
        // cleanly. Hence: x$L = x-L instead of [ -n "$L" ], and a case
        // pattern over od's raw (space-padded) output instead of tr -d " " -
        // case words are not field-split, so *1 matches however od pads, and
        // the only values our own encoder writes are 0 and 1.
        if (ok) {
            ok = exec(dc + platform::shellArg(
                          "cd /src && IFS= && for f in res/sfx/*.wav "
                           "res/sfx/*/*.wav res/sfx/*/*/*.wav; do "
                           "[ -e $f ] || continue; "
                           "o=${f%.wav}.adpcm && o=bin/${o#res/} && "
                           "mkdir -p $(dirname $o); "
                           "L= && case $f in *-loop.wav) L=-L;; esac; "
                           "R=0; if [ x$L = x-L ] && [ -e $o ]; then "
                           "B=$(od -An -tu1 -j6 -N1 $o); "
                           "case $B in *1) R=0;; *) R=1;; esac; fi; "
                           "if [ ! $o -nt $f ] || [ $R = 1 ]; then "
                           "echo [editor] adpenc $L $f && "
                           "adpenc $L $f $o || exit 1; "
                          "fi; done"),
                      p.dir) == 0;
            if (!ok) appendLine("[editor] Sound conversion (adpenc) failed.");
        }

        // The Makefile's resources step (`cp -r res/*`) also drops the source
        // WAVs into bin/sfx next to the adpenc output. The game only loads
        // the .adpcm, so the WAV copies are dead weight that would bloat the
        // exported ISO - drop them. Orphaned .adpcm (source WAV deleted from
        // res/sfx, e.g. removed in the Sounds panel) must go too: the game
        // volume keeps them forever otherwise and the copy-back rsync
        // resurrects them on the host after every build.
        if (ok)
            exec(dc + platform::shellArg(
                     "cd /src && rm -f bin/sfx/*.wav bin/sfx/*/*.wav "
                      "bin/sfx/*/*/*.wav; IFS= && for o in bin/sfx/*.adpcm "
                      "bin/sfx/*/*.adpcm bin/sfx/*/*/*.adpcm; do "
                      "[ -e $o ] || continue; s=res/${o#bin/} && "
                      "s=${s%.adpcm}.wav; [ -e $s ] || rm -f $o; done"),
                 p.dir);

        if (ok) {
            appendLine("[editor] Copying binaries back to host...");
            // --chown: the container is root, and on a plain Linux Docker that
            // means every file it writes into the bind-mounted project comes
            // out root-owned - the user cannot then delete their own bin/
            // without sudo. Docker Desktop maps ownership itself, so the flag
            // is empty (and omitted) there.
            // No -z: both ends of this copy are the same machine (a docker
            // volume and a bind mount), so compressing the stream only spends
            // CPU. -c stays - this one lands on the host bind mount, whose
            // timestamp granularity is not ours to trust (Docker Desktop), and
            // bin/ is what actually ships.
            const std::string owner = platform::containerFileOwner();
            ok = exec(dc + platform::shellArg(
                          "rsync -ac --include=*/ --include=bin/** --exclude=* " +
                          (owner.empty() ? std::string() : "--chown=" + owner + " ") +
                          "/src/ /host/"),
                      p.dir) == 0;
        }

        // Per-track music build conversion (Music panel "PS2 build"): the
        // copy-back just refreshed bin/audio from the untouched res/ source,
        // so re-convert the copy the game actually streams. Lower rate/mono
        // halve the byte rate - the levers when a track stutters on a real
        // console over the network deploy.
        if (ok) {
            for (const auto& [rel, opt] : p.musicBuild) {
                if (opt.rate == 0 && !opt.mono) continue;
                std::string binRel = rel;
                if (binRel.rfind("res/", 0) == 0) binRel = binRel.substr(4);
                const fs::path wav = fs::path(p.dir) / "bin" / fs::path(binRel);
                std::error_code ec;
                if (!fs::exists(wav, ec)) continue;
                std::string err;
                if (wavconvert::convertTo16(wav, opt.rate, err, opt.mono))
                    appendLine("[editor] music: " + binRel + " -> 16-bit " +
                               (opt.rate ? std::to_string(opt.rate) + " Hz" : "source rate") +
                               (opt.mono ? " mono" : "") + " (build copy only)");
                else
                    appendLine("[editor] WARNING: music build conversion failed for " +
                               binRel + ": " + err);
            }
        }

        if (ok) {
            // The in-container rm of the copied source WAVs never reaches the
            // host (the copy-back rsync has no --delete), so stale WAVs pile
            // up in bin/sfx and would ship in exported ISOs - drop them here.
            // While at it, flag ADPCM one-shots that cannot work: sound
            // effects load whole into the 2 MB SPU RAM at boot (music belongs
            // in res/audio, which streams), and over a network PS2 deploy
            // every extra megabyte is roughly a minute of boot time.
            std::error_code ec;
            const fs::path sfx = fs::path(p.dir) / "bin" / "sfx";
            for (fs::recursive_directory_iterator it(sfx, ec), end; it != end;
                 it.increment(ec)) {
                if (!it->is_regular_file(ec)) continue;
                const fs::path& f = it->path();
                if (f.extension() == ".wav") {
                    fs::remove(f, ec);
                } else if (f.extension() == ".adpcm" &&
                           it->file_size(ec) > 1024 * 1024) {
                    appendLine("[editor] WARNING: " +
                               fs::relative(f, p.dir, ec).string() + " is " +
                               std::to_string(it->file_size(ec) / (1024 * 1024)) +
                               " MB - sound effects load whole into the 2 MB "
                               "SPU RAM and slow every PS2 network boot. Long "
                               "tracks belong in Music (streamed), not Sounds.");
                }
            }
        }

        if (ok) {
            // Static models ship as .tmdl (docs/model-pipeline.md), so texbake
            // stops mirroring their .obj - but bin/ is additive (no --delete on
            // the copy-back), and a project built before this would keep an
            // orphaned ASCII copy that still lands in an exported ISO. Drop
            // any bin/ .obj whose .tmdl sits next to it.
            std::error_code ec;
            const fs::path models = fs::path(p.dir) / "bin" / "models";
            for (fs::recursive_directory_iterator it(models, ec), end; it != end;
                 it.increment(ec)) {
                if (!it->is_regular_file(ec) || it->path().extension() != ".obj")
                    continue;
                const fs::path stem = it->path().parent_path() /
                                      it->path().stem();
                bool superseded = fs::exists(fs::path(stem).concat(".tmdl"), ec);
                if (!superseded) {  // "<stem>__ovr<hash>.tmdl" (material override)
                    const std::string pre = it->path().stem().string() + "__ovr";
                    std::error_code sec;
                    for (const auto& s :
                         fs::directory_iterator(it->path().parent_path(), sec)) {
                        if (s.path().extension() != ".tmdl") continue;
                        if (s.path().stem().string().rfind(pre, 0) == 0)
                            superseded = true;
                    }
                }
                if (superseded) fs::remove(it->path(), ec);
            }
        }

        // Every release build audits itself: the devkit (Live Link / Live
        // Debugger / Live Logic) claims to cost a shipped game nothing, and a
        // claim that is never checked rots. This reads the ELF that was just
        // produced and says so in the build log, with the real numbers - see
        // docs/devkit.md and `--audit-release`.
        if (ok && p.settings.buildProfile == "release") {
            const elfsym::Audit audit = elfsym::auditRelease(p.elfPath());
            appendLine("[editor] " + audit.summary());
            for (const elfsym::AuditFinding& f : audit.findings)
                appendLine("[editor]   leaked: " + f.what + " (" + f.where + ")");
        }

        appendLine(ok ? "[editor] === Build OK ==="
                   : cancelRequested_ ? "[editor] === Build CANCELLED ==="
                                      : "[editor] === Build FAILED ===");
    }

    if (ok && run && !cancelRequested_) ok = ps2 ? deployToPs2(p) : launchPCSX2(p);

    state_ = (ok && !cancelRequested_) ? State::Success : State::Failed;
}

bool Runner::ps2ClientAlive() const {
    std::shared_ptr<platform::Process> proc;
    {
        std::lock_guard<std::mutex> lock(ps2ClientMutex_);
        proc = ps2Client_;
    }
    return proc && proc->running();
}
