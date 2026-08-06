#pragma once

// The ONE place operating-system differences live.
//
// The editor used to be a Windows-only app: CreateProcess/Job Objects in the
// Runner and the AI generator, comdlg32/IFileOpenDialog pickers, ShellExecute
// "Reveal in Explorer", %LOCALAPPDATA% for the machine-global config,
// \Windows\Fonts for the bake fonts, GetModuleFileName wherever something had
// to be found relative to the exe. Every one of those has a POSIX answer that
// is *different in shape*, not just in spelling, so they are collected behind
// this header instead of being #ifdef'd at ~40 call sites.
//
// The rule that keeps it that way: if a feature needs to know which OS it is
// running on, it grows an entry HERE and the call site stays platform-blind.
// The only sanctioned exceptions are the two places where the difference is
// genuinely structural rather than incidental - the socket shims in wire.cpp
// and the emulator/PCSX2.ini discovery in runner.cpp / pcsx2_config.cpp, both
// of which say so in a comment.

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

namespace platform {

// --- identity and locations ------------------------------------------------

// Absolute path of the running editor executable. Several features resolve
// repo content relative to it (the bundled Tyra engine, tools/, the VS Code
// extension .vsix), so a wrong answer is a silently broken feature, not a
// crash - both implementations fall back to argv[0]-free OS queries.
std::string exePath();

// Per-user, per-machine editor config root: %LOCALAPPDATA%\tyra-editor on
// Windows, $XDG_CONFIG_HOME/tyra-editor (default ~/.config/tyra-editor)
// elsewhere. Holds editor.ini, the session remote-cache and the exported
// PS2SDK headers. Empty only if the OS has no home directory for us at all.
std::filesystem::path configDir();

std::filesystem::path homeDir();

// Display name proposed for collaboration sessions.
std::string userName();

// ".exe" on Windows, "" elsewhere. For building executable names and the
// file-dialog pattern that matches them.
const char* exeSuffix();

// --- small odds and ends ---------------------------------------------------

void sleepMs(int ms);

// This process's id. Only used to keep two editor instances' temp files apart,
// which is why it is an unsigned long long rather than a pid_t/DWORD.
unsigned long long processId();

// "HH:MM:SS " in local time - the Output panel stamps every line with it.
std::string logTimeStamp();

// --- shell command fragments -----------------------------------------------
// Command lines are written once and handed to Process below, which runs them
// through the platform shell. These cover the handful of spots where the two
// shells genuinely disagree, so the callers stay one string.

// Run `cmd`, discard its error output and report success regardless - the
// "do it if you can, never mind if you can't" idiom the Runner uses for
// cleanup steps.
std::string quiet(const std::string& cmd);

// Whether `name` resolves as a command on PATH (including the .cmd/.bat shims
// VS Code and the AI CLIs ship as on Windows). The command lines below run
// through a shell, which always starts successfully, so this is the only way
// to tell "tool is missing" from "tool ran and failed".
bool commandExists(const std::string& name);

// The full path `name` resolves to on PATH, or "" when it does not resolve.
// `commandExists` answers "can I run it"; this answers "where is it", which is
// what a config file has to write down - VS Code's C/C++ extension wants a
// `compilerPath`, not a command name.
std::string commandPath(const std::string& name);

// "<uid>:<gid>" of the user we run as, or "" where the concept does not apply
// (Windows). The build container runs as root, so on Linux everything it
// writes back into the bind-mounted project would land root-owned - the user
// then cannot delete their own build output without sudo. The copy-back rsync
// passes this to --chown; Docker Desktop maps ownership itself, hence the "".
std::string containerFileOwner();

// Quote one argument so the platform shell passes it through UNTOUCHED.
//
// This is not cosmetic. cmd.exe performs no `$(...)`/`${...}` substitution, so
// a nested `docker ... sh -c "<script>"` used to reach the container verbatim
// with nothing but double quotes around it. /bin/sh expands all of that inside
// double quotes, on the HOST - which silently emptied every variable in the
// build's in-container shell loops ("dirname: missing operand") and ran
// $(nproc) against the wrong machine. Wrap anything the shell must not touch -
// a nested script, a path that could contain $ or a backtick - in this.
std::string shellArg(const std::string& s);

// A "run the next command with this environment variable set" prefix for a
// shell command line: "set NAME=value&& " on cmd.exe, "NAME=value " on sh.
std::string envPrefix(const std::string& name, const std::string& value);

// Kill every process with one of these names (basenames; a ".exe" is appended
// on Windows when missing). Always succeeds. Matching is by exact process
// name, so callers that may be dealing with a wrapper - a PCSX2 AppImage, say
// - should pass the basename of the path they actually launched as well.
std::string killByName(const std::vector<std::string>& processNames);

// --- child processes -------------------------------------------------------

// A child process started through the platform shell (cmd.exe /S /C on
// Windows, /bin/sh -c elsewhere), so callers keep writing a single command
// line with quoting, redirects and && chains.
//
// kill() takes down the whole tree, not just the shell: a Job Object on
// Windows, a process group on POSIX. That matters - the shell wrapper is
// never the process doing the work (docker, node, curl, ps2client), and
// killing only the wrapper orphans a token-burning or port-holding child.
class Process {
public:
    struct Options {
        std::string cwd;         // empty = inherit ours
        bool capture = false;    // stdout (and stderr) into a pipe readLine() drains
        std::string stderrFile;  // non-empty: stderr goes here instead of the pipe
    };

    // Returns null when the child could not be started.
    static std::unique_ptr<Process> start(const std::string& cmdline,
                                          const Options& opts);
    static std::unique_ptr<Process> start(const std::string& cmdline) {
        return start(cmdline, Options{});
    }

    // No pipes and no handle kept: the child outlives us and never gets a
    // console window. Returns false when it could not be started.
    static bool startDetached(const std::string& cmdline,
                              const std::string& cwd = std::string());

    ~Process();
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    // Next line of captured output with any trailing "\r" stripped; false at
    // EOF (which also yields the unterminated tail, if any). Blocks.
    bool readLine(std::string& line);

    // Everything the child writes, until EOF. Blocks.
    std::string readAll();

    // Exit code. Waits for the child; repeated calls return the same value.
    int wait();

    // Non-blocking liveness poll. Safe to call from another thread while a
    // pump thread is inside readLine().
    bool running();

    // Terminate the whole process tree. Safe to call more than once, and from
    // another thread than the one blocked in readLine()/wait().
    void kill();

private:
    Process();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// --- desktop integration ---------------------------------------------------

// A modal "something went wrong" box, for the few failures that happen
// OUTSIDE the ImGui frame loop (a failed save at shutdown, an open that fails
// before the first frame) where there is no panel left to print into. Blocks
// until dismissed; falls back to stderr when the platform has no dialog tool.
void errorBox(const std::string& title, const std::string& message);

// A modal yes/no question, for the few decisions that happen OUTSIDE the ImGui
// frame loop - currently the project-format migration prompt, which must be
// answered before the project is attached and any panel exists. Blocks until
// dismissed. Returns true ONLY on an explicit yes: when the platform has no
// dialog tool the answer is No, because the callers guard irreversible work and
// an unattended run must never silently consent to it.
bool confirmBox(const std::string& title, const std::string& message);

// Show `path` in the system file manager, selecting the entry itself when it
// is a file. Best-effort: silently does nothing when no file manager answers.
void revealInFileManager(const std::string& path);

// Open a project folder (and optionally jump to one file) in VS Code. Returns
// "" on success, or a message naming what went wrong.
std::string openInVSCode(const std::string& projectDir, const std::string& absFile);

// Register the running editor with the desktop environment, so its window and
// launcher entry get an icon. No-op on Windows, where the icon is a resource
// inside the .exe (resources/app.rc) and nothing has to be installed.
//
// Elsewhere this is the ONLY way a window gets an icon under Wayland: the
// compositor takes it from the .desktop file whose basename matches the
// surface's app id, never from the application itself (GLFW's
// glfwSetWindowIcon is X11/Win32-only and fails with GLFW_FEATURE_UNAVAILABLE
// there). So `appId` must be both the GLFW_WAYLAND_APP_ID window hint and the
// name of the file this writes. Idempotent and cheap: rewrites the entry only
// when its contents changed, which also keeps Exec= pointing at the binary
// after it moves.
void installDesktopEntry(const std::string& appId, const std::string& appName,
                         const std::string& comment, const unsigned char* iconPng,
                         std::size_t iconPngSize);

// --- fonts -----------------------------------------------------------------
// The PS2 has no font engine, so every piece of text is rasterized host-side
// from a TTF (menubake.cpp). "A font by bare file name" therefore has to mean
// something on each OS: \Windows\Fonts on Windows, the freedesktop font
// directories elsewhere.

// Absolute path of a system font given its bare file name ("impact.ttf"), or
// "" when this machine does not have it.
std::string systemFontPath(const std::string& fileName);

// A stock system font to offer in the editor's font pickers.
struct SystemFont {
    const char* label;  // "Impact"
    const char* file;   // "impact.ttf" - what gets stored in GameFont::fontPath
};
// The curated list for this OS, unfiltered (callers existence-check with
// systemFontPath). Same shape on every platform so the UI stays identical.
const std::vector<SystemFont>& systemFonts();

// Fallback chain for a font that is unset or unreadable, best first, as bare
// file names for systemFontPath(). Never empty.
const std::vector<std::string>& fallbackFontFiles();

// The label the UI shows for "no font chosen" - the head of the chain above,
// spelled for humans ("Consolas Bold", "DejaVu Sans Bold").
const char* defaultFontLabel();

// The EDITOR's own interface font, which is a different question from the two
// above: those pick a face to rasterize game text with, this one picks what
// the editor's windows are drawn in. ImGui otherwise falls back to its
// built-in bitmap font, which is the single loudest "this is a debug tool"
// signal an ImGui application gives off.
//
// Best first; the first entry systemFontPath() resolves wins, and a machine
// with none of them keeps the built-in font. Proportional UI faces only - a
// bold display face would be wrong here for the same reason the bake list is
// all bold: small text at regular weight is what an interface wants.
const std::vector<SystemFont>& uiFontFiles();

// --- native file dialogs ---------------------------------------------------

struct FileFilter {
    std::string label;                  // "3D model (*.obj, *.glb, *.fbx)"
    std::vector<std::string> patterns;  // {"*.obj", "*.glb", "*.fbx"}
};

// Modal open-file / pick-folder dialogs. Return "" when the user cancels (or
// when no dialog backend is available at all). filters are ignored by the
// folder picker.
std::string pickFile(const std::string& title, const std::vector<FileFilter>& filters);
std::string pickFolder(const std::string& title);

// The main window, so the dialogs come up owned. An unowned modal leaves the
// frozen GLFW window active behind it, and Windows then wedges the dialog when
// it interacts with the non-pumping app (grayed Open button). No-op where the
// platform's dialogs are separate processes.
void setDialogOwner(GLFWwindow* window);

}  // namespace platform
