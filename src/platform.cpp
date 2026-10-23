#include "platform.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#include <dwmapi.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
// Guarded because MinGW only declares it at _WIN32_WINNT >= 0x0600 and this
// tree sets no target version; the value is fixed in the ABI.
#ifndef PROCESS_QUERY_LIMITED_INFORMATION
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#endif
#else
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
// macOS is the POSIX half with its own answers where Linux reads /proc or
// shells out to freedesktop tools: dyld for the executable path, libproc and
// KERN_PROCARGS2 for other processes, osascript for dialogs, `open` for URLs.
#include <libproc.h>
#include <mach-o/dyld.h>
#include <sys/sysctl.h>
#endif
#endif

namespace fs = std::filesystem;

namespace platform {

namespace {

#ifdef _WIN32

std::string wideToUtf8(const wchar_t* w) {
    std::string result;
    if (!w) return result;
    const int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len > 1) {
        result.resize((size_t)len - 1);
        WideCharToMultiByte(CP_UTF8, 0, w, -1, result.data(), len, nullptr, nullptr);
    }
    return result;
}

std::wstring utf8ToWide(const std::string& s) {
    std::wstring result;
    if (s.empty()) return result;
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len > 1) {
        result.resize((size_t)len - 1);
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, result.data(), len);
    }
    return result;
}

// Owner window for the native dialogs (see setDialogOwner).
HWND g_dialogOwner = nullptr;

#else

// SIGPIPE: a child that dies while we still hold the read end of its pipe, and
// a peer that vanishes mid-send in wire.cpp, both raise it - and the default
// disposition kills the editor outright. Every write site here checks its
// return value instead, so the signal buys us nothing but a lost session.
// Registered once, before main(), because both subsystems can start early.
const struct IgnoreSigPipe {
    IgnoreSigPipe() { ::signal(SIGPIPE, SIG_IGN); }
} g_ignoreSigPipe;

#ifdef __APPLE__
// An app started from Finder or the Dock inherits launchd's PATH
// (/usr/bin:/bin:/usr/sbin:/sbin), not the login shell's, so `code`, `docker`
// and a Homebrew PCSX2 - all in /usr/local/bin or /opt/homebrew/bin - would
// be "not installed" in TyraX.app while working from a terminal. Append (never
// prepend: what the user's PATH names first still wins) the two prefixes.
const struct AppendUserBinDirs {
    AppendUserBinDirs() {
        const char* cur = getenv("PATH");
        std::string path = cur && *cur ? cur : "/usr/bin:/bin:/usr/sbin:/sbin";
        for (const char* dir : {"/usr/local/bin", "/opt/homebrew/bin"}) {
            const std::string padded = ":" + path + ":";
            if (padded.find(std::string(":") + dir + ":") == std::string::npos)
                path += std::string(":") + dir;
        }
        ::setenv("PATH", path.c_str(), 1);
    }
} g_appendUserBinDirs;
#endif

// Single-quote for /bin/sh: wrap in '...' and escape embedded quotes the only
// way sh allows ('\'' closes, escapes, reopens).
std::string shQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

// Runs `cmdline` and returns its trimmed stdout - the small-answer helper the
// dialog and desktop-integration code needs (`command -v`, zenity's reply).
// Empty on any failure, so callers treat "no answer" and "no tool" alike.
std::string capture(const std::string& cmdline, int* exitCode = nullptr) {
    if (exitCode) *exitCode = -1;
    FILE* p = ::popen(cmdline.c_str(), "r");
    if (!p) return "";
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
    const int rc = ::pclose(p);
    if (exitCode) *exitCode = (rc == -1 || !WIFEXITED(rc)) ? -1 : WEXITSTATUS(rc);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

#ifdef __APPLE__
// An AppleScript string literal: double quotes, backslash-escaped.
std::string asString(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

// Runs an AppleScript through osascript and returns its trimmed stdout. A
// cancelled dialog exits 1 ("User canceled"), which callers read as "no".
std::string appleScript(const std::string& script, int* exitCode = nullptr) {
    return capture("osascript -e " + shQuote(script) + " 2>/dev/null", exitCode);
}
#endif

#endif

}  // namespace

// ---------------------------------------------------------------------------
// Identity and locations
// ---------------------------------------------------------------------------

std::string exePath() {
#ifdef _WIN32
    char buf[MAX_PATH] = {};
    if (GetModuleFileNameA(nullptr, buf, MAX_PATH) == 0) return "";
    return buf;
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return "";
    buf.resize(std::strlen(buf.c_str()));
    std::error_code ec;
    const fs::path p = fs::canonical(buf, ec);
    return ec ? buf : p.string();
#else
    std::error_code ec;
    // /proc/self/exe is the authoritative answer on Linux and survives a
    // renamed or deleted binary; canonical() resolves the symlink for us.
    const fs::path p = fs::canonical("/proc/self/exe", ec);
    if (!ec) return p.string();
    return "";
#endif
}

fs::path resourceBinDir() {
    const std::string exe = exePath();
    if (exe.empty()) return {};
    const fs::path dir = fs::path(exe).parent_path();
#ifdef __APPLE__
    // <name>.app/Contents/MacOS/<exe>: answer <name>.app/Contents/Resources/bin.
    const fs::path contents = dir.parent_path();
    std::error_code ec;
    if (dir.filename() == "MacOS" && contents.filename() == "Contents" &&
        fs::is_directory(contents / "Resources", ec))
        return contents / "Resources" / "bin";
#endif
    return dir;
}

fs::path configDir() {
#ifdef _WIN32
    const char* base = getenv("LOCALAPPDATA");
    if (!base || !*base) base = getenv("USERPROFILE");
    if (!base || !*base) return {};
    return fs::path(base) / "tyra-editor";
#else
    if (const char* xdg = getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return fs::path(xdg) / "tyra-editor";
    const fs::path home = homeDir();
    if (home.empty()) return {};
    return home / ".config" / "tyra-editor";
#endif
}

fs::path homeDir() {
#ifdef _WIN32
    if (const char* p = getenv("USERPROFILE"); p && *p) return fs::path(p);
    return {};
#else
    if (const char* p = getenv("HOME"); p && *p) return fs::path(p);
    if (const passwd* pw = ::getpwuid(::getuid()); pw && pw->pw_dir) return fs::path(pw->pw_dir);
    return {};
#endif
}

std::string userName() {
#ifdef _WIN32
    char buf[256] = {};
    DWORD n = sizeof(buf);
    if (GetUserNameA(buf, &n) && n > 0) return std::string(buf, n - 1);
    if (const char* u = getenv("USERNAME"); u && *u) return u;
#else
    if (const passwd* pw = ::getpwuid(::getuid()); pw && pw->pw_name && *pw->pw_name)
        return pw->pw_name;
    if (const char* u = getenv("USER"); u && *u) return u;
#endif
    return "";
}

const char* firewallNote() {
#ifdef _WIN32
    return "Windows Firewall asks once to allow TyraX on this network.";
#elif defined(__APPLE__)
    return "macOS asks once to allow incoming connections for TyraX.";
#else
    return "If a firewall runs here (ufw, firewalld), allow incoming TCP on the session port.";
#endif
}

const char* exeSuffix() {
#ifdef _WIN32
    return ".exe";
#else
    return "";
#endif
}

// ---------------------------------------------------------------------------
// Small odds and ends
// ---------------------------------------------------------------------------

void sleepMs(int ms) {
    if (ms <= 0) return;
#ifdef _WIN32
    ::Sleep((DWORD)ms);
#else
    // usleep() caps at a second; loop rather than silently truncating.
    while (ms > 0) {
        const int chunk = ms > 900 ? 900 : ms;
        ::usleep((useconds_t)chunk * 1000);
        ms -= chunk;
    }
#endif
}

unsigned long long processId() {
#ifdef _WIN32
    return (unsigned long long)GetCurrentProcessId();
#else
    return (unsigned long long)::getpid();
#endif
}

std::string logTimeStamp() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    ::localtime_r(&t, &tm);
#endif
    char stamp[16];
    std::snprintf(stamp, sizeof(stamp), "%02d:%02d:%02d ", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return stamp;
}

std::string fileTimeStamp() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    ::localtime_r(&t, &tm);
#endif
    char stamp[24];
    std::snprintf(stamp, sizeof(stamp), "%04d%02d%02d-%02d%02d%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                  tm.tm_min, tm.tm_sec);
    return stamp;
}

// ---------------------------------------------------------------------------
// Shell command fragments
// ---------------------------------------------------------------------------

std::string quiet(const std::string& cmd) {
#ifdef _WIN32
    return cmd + " 2>nul & exit 0";
#else
    return cmd + " 2>/dev/null; exit 0";
#endif
}

std::string containerFileOwner() {
#ifdef _WIN32
    return "";
#else
    return std::to_string((unsigned)::getuid()) + ":" + std::to_string((unsigned)::getgid());
#endif
}

std::string shellArg(const std::string& s) {
#ifdef _WIN32
    // cmd.exe expands nothing inside double quotes, and `docker.exe` unquotes
    // its own argv - the pre-existing arrangement the build commands are
    // written against (see runner.cpp). Embedded double quotes cannot survive
    // it, which is why those scripts avoid them.
    return "\"" + s + "\"";
#else
    return shQuote(s);
#endif
}

std::string envPrefix(const std::string& name, const std::string& value) {
#ifdef _WIN32
    // No space before && - cmd.exe would make it part of the value.
    return "set " + name + "=" + value + "&& ";
#else
    return name + "=" + value + " ";
#endif
}

// ---------------------------------------------------------------------------
// Looking at other processes
// ---------------------------------------------------------------------------

namespace {

#ifdef _WIN32
// Windows compares both process names and paths case-insensitively; nothing
// off it does, which is why this is not defined there.
std::string lowerAscii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}
#endif

// The basename with the platform's executable suffix removed, lowercased on
// Windows (where process names are compared case-insensitively) and left alone
// elsewhere - so "PCSX2.exe", "pcsx2" and "pcsx2.EXE" are one name on Windows
// while "Pcsx2" and "pcsx2" stay two on Linux, which is what those file systems
// mean.
std::string processKey(const std::string& raw) {
#ifdef _WIN32
    std::string s = lowerAscii(raw);
    if (s.size() > 4 && s.compare(s.size() - 4, 4, ".exe") == 0) s.resize(s.size() - 4);
    return s;
#else
    return raw;
#endif
}

#ifdef _WIN32

// Another process's command line, straight from the kernel:
// NtQueryInformationProcess(ProcessCommandLineInformation), which needs only
// PROCESS_QUERY_LIMITED_INFORMATION and reads no remote memory. Windows 8.1 and
// up; anything older (or a process we may not open) answers "" and the caller
// treats it as unidentified rather than as a target.
std::string remoteCommandLine(DWORD pid) {
    struct UniStr {  // UNICODE_STRING, spelled locally so winternl stays out
        USHORT Length;
        USHORT MaximumLength;
        PWSTR Buffer;
    };
    using NtQip = LONG(WINAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    static NtQip query = [] {
        HMODULE nt = GetModuleHandleW(L"ntdll.dll");
        return nt ? (NtQip)(void*)GetProcAddress(nt, "NtQueryInformationProcess")
                  : nullptr;
    }();
    if (!query) return "";
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return "";
    constexpr ULONG kProcessCommandLineInformation = 60;
    constexpr LONG kInfoLengthMismatch = (LONG)0xC0000004L;
    std::vector<unsigned char> buf(1024);
    ULONG need = 0;
    LONG st = query(h, kProcessCommandLineInformation, buf.data(),
                    (ULONG)buf.size(), &need);
    if (st == kInfoLengthMismatch && need > buf.size()) {
        buf.assign(need, 0);
        st = query(h, kProcessCommandLineInformation, buf.data(),
                   (ULONG)buf.size(), &need);
    }
    std::string out;
    if (st >= 0) {
        const UniStr* s = (const UniStr*)buf.data();
        if (s->Buffer && s->Length) {
            const std::wstring w(s->Buffer, s->Length / sizeof(wchar_t));
            out = wideToUtf8(w.c_str());
        }
    }
    CloseHandle(h);
    return out;
}

#elif defined(__APPLE__)

// Another process's argv joined with spaces, from KERN_PROCARGS2: an argc
// word, the exec path, NUL padding, then argc NUL-terminated arguments.
// Empty for a process we may not inspect (another user's), which the caller
// treats as "not mine".
std::string remoteCommandLine(pid_t pid) {
    int mib[3] = {CTL_KERN, KERN_PROCARGS2, (int)pid};
    size_t size = 0;
    if (::sysctl(mib, 3, nullptr, &size, nullptr, 0) != 0 || size < sizeof(int)) return "";
    std::vector<char> buf(size);
    if (::sysctl(mib, 3, buf.data(), &size, nullptr, 0) != 0 || size < sizeof(int)) return "";
    int argc = 0;
    std::memcpy(&argc, buf.data(), sizeof(int));
    size_t i = sizeof(int);
    while (i < size && buf[i] != '\0') ++i;  // exec path
    while (i < size && buf[i] == '\0') ++i;  // padding
    std::string out;
    for (int a = 0; a < argc && i < size; ++a) {
        const size_t start = i;
        while (i < size && buf[i] != '\0') ++i;
        if (!out.empty()) out += ' ';
        out.append(buf.data() + start, i - start);
        ++i;
    }
    return out;
}

#else

std::string readProcFile(const fs::path& p) {
    std::string out;
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return out;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return out;
}

#endif

}  // namespace

std::vector<RunningProcess> processesNamed(const std::string& name) {
    std::vector<RunningProcess> out;
    if (name.empty()) return out;
    const std::string want = processKey(name);
#ifdef _WIN32
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W e{};
    e.dwSize = sizeof(e);
    if (Process32FirstW(snap, &e)) {
        do {
            const std::string key = processKey(wideToUtf8(e.szExeFile));
            if (key != want) continue;
            RunningProcess p;
            p.pid = e.th32ProcessID;
            p.name = key;
            p.commandLine = remoteCommandLine(e.th32ProcessID);
            out.push_back(std::move(p));
        } while (Process32NextW(snap, &e));
    }
    CloseHandle(snap);
#elif defined(__APPLE__)
    const int bytes = ::proc_listallpids(nullptr, 0);
    if (bytes <= 0) return out;
    std::vector<pid_t> pids((size_t)bytes / sizeof(pid_t) + 64);
    const int n = ::proc_listallpids(pids.data(), (int)(pids.size() * sizeof(pid_t)));
    for (int i = 0; i < n; ++i) {
        char path[PROC_PIDPATHINFO_MAXSIZE] = {};
        if (::proc_pidpath(pids[i], path, sizeof(path)) <= 0) continue;
        const std::string exe = fs::path(path).filename().string();
        if (processKey(exe) != want) continue;
        RunningProcess p;
        p.pid = (unsigned long long)pids[i];
        p.name = exe;
        p.commandLine = remoteCommandLine(pids[i]);
        out.push_back(std::move(p));
    }
#else
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator("/proc", ec)) {
        const std::string leaf = entry.path().filename().string();
        if (leaf.empty() || leaf.find_first_not_of("0123456789") != std::string::npos)
            continue;
        // The exe symlink first: /proc/<pid>/comm truncates at 15 characters,
        // which a PCSX2 AppImage's file name passes comfortably.
        std::string exe;
        std::error_code lec;
        const fs::path target = fs::read_symlink(entry.path() / "exe", lec);
        if (!lec) exe = target.filename().string();
        if (exe.empty()) {
            exe = readProcFile(entry.path() / "comm");
            while (!exe.empty() && (exe.back() == '\n' || exe.back() == '\r'))
                exe.pop_back();
        }
        if (processKey(exe) != want) continue;
        RunningProcess p;
        p.pid = (unsigned long long)std::strtoull(leaf.c_str(), nullptr, 10);
        p.name = exe;
        // argv, NUL-separated. Joined with spaces so both platforms hand the
        // caller one string to look through.
        std::string cmd = readProcFile(entry.path() / "cmdline");
        for (char& c : cmd)
            if (c == '\0') c = ' ';
        while (!cmd.empty() && cmd.back() == ' ') cmd.pop_back();
        p.commandLine = cmd;
        out.push_back(std::move(p));
    }
#endif
    return out;
}

bool commandLineNamesPath(const std::string& commandLine, const std::string& path) {
    if (commandLine.empty() || path.empty()) return false;
#ifdef _WIN32
    // Windows path comparison is case-insensitive, and the launcher quotes the
    // argument, so a plain substring test over both lowercased is exactly the
    // question - the quotes sit outside the path, never inside it.
    return lowerAscii(commandLine).find(lowerAscii(path)) != std::string::npos;
#else
    return commandLine.find(path) != std::string::npos;
#endif
}

bool killProcess(unsigned long long pid) {
    if (pid == 0) return false;
#ifdef _WIN32
    HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, (DWORD)pid);
    // ERROR_INVALID_PARAMETER is what OpenProcess says about a pid that no
    // longer exists, which is success as far as the caller is concerned.
    if (!h) return GetLastError() == ERROR_INVALID_PARAMETER;
    const BOOL ok = TerminateProcess(h, 1);
    // Wait for it to actually go: on Windows the handles it holds (the project's
    // bin\ as a working directory, above all) are released on exit, not on the
    // TerminateProcess call, and the Clean path deletes exactly those files.
    if (ok) WaitForSingleObject(h, 3000);
    CloseHandle(h);
    return ok != 0;
#else
    if (::kill((pid_t)pid, SIGKILL) == 0) return true;
    return errno == ESRCH;  // already gone
#endif
}

bool commandExists(const std::string& name) {
    if (name.empty()) return false;
#ifdef _WIN32
    // `where` covers PATH plus PATHEXT, so the .cmd/.bat shims count.
    std::unique_ptr<Process> p =
        Process::start("where " + name + " >nul 2>nul", Process::Options{});
    return p && p->wait() == 0;
#else
    int code = -1;
    capture("command -v " + shQuote(name) + " 2>/dev/null", &code);
    return code == 0;
#endif
}

std::string commandPath(const std::string& name) {
    if (name.empty()) return "";
    std::string out;
#ifdef _WIN32
    // `capture` above is the POSIX half's helper (it is inside the #else), so
    // the Windows branch reads the pipe itself. `where` prints one match per
    // line and may find several - a shim plus the real binary - and the first
    // is the one the shell would run.
    if (FILE* p = ::_popen(("where " + name + " 2>nul").c_str(), "r")) {
        char buf[1024];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
        if (::_pclose(p) != 0) return "";
    }
#else
    int code = -1;
    out = capture("command -v " + shQuote(name) + " 2>/dev/null", &code);
    if (code != 0) return "";
#endif
    const size_t nl = out.find_first_of("\r\n");
    if (nl != std::string::npos) out.resize(nl);
    while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) out.pop_back();
    return out;
}

// ---------------------------------------------------------------------------
// Child processes
// ---------------------------------------------------------------------------

struct Process::Impl {
    std::mutex mutex;   // guards exited/code - running() polls from another thread
    bool exited = false;
    int code = -1;
    std::string pending;  // readLine() buffer
    bool eof = false;
#ifdef _WIN32
    HANDLE proc = nullptr;
    HANDLE thread = nullptr;
    HANDLE job = nullptr;
    HANDLE readPipe = nullptr;
#else
    pid_t pid = -1;
    int fd = -1;
#endif
};

Process::Process() : impl_(std::make_unique<Impl>()) {}

Process::~Process() {
    // A Process still alive at destruction is one nobody is waiting for any
    // more - take its tree down rather than leaking it (and, on POSIX, rather
    // than leaving a zombie behind).
    if (running()) kill();
    wait();
#ifdef _WIN32
    if (impl_->readPipe) CloseHandle(impl_->readPipe);
    if (impl_->thread) CloseHandle(impl_->thread);
    if (impl_->proc) CloseHandle(impl_->proc);
    if (impl_->job) CloseHandle(impl_->job);
#else
    if (impl_->fd >= 0) ::close(impl_->fd);
#endif
}

std::unique_ptr<Process> Process::start(const std::string& cmdline, const Options& opts) {
    std::unique_ptr<Process> p(new Process());
    Impl& s = *p->impl_;

#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE writePipe = nullptr;
    if (opts.capture) {
        if (!CreatePipe(&s.readPipe, &writePipe, &sa, 0)) return nullptr;
        SetHandleInformation(s.readPipe, HANDLE_FLAG_INHERIT, 0);
    }
    HANDLE errHandle = INVALID_HANDLE_VALUE;
    if (!opts.stderrFile.empty())
        errHandle = CreateFileA(opts.stderrFile.c_str(), GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = INVALID_HANDLE_VALUE;
    si.hStdOutput = opts.capture ? writePipe : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = errHandle != INVALID_HANDLE_VALUE
                       ? errHandle
                       : (opts.capture ? writePipe : GetStdHandle(STD_ERROR_HANDLE));

    // Kill-on-close job: closing it terminates every process inside, so kill()
    // reaches the real worker (docker, node, curl, ps2client) and not just the
    // cmd.exe wrapper. Created suspended so nothing escapes before it is
    // assigned to the job.
    s.job = CreateJobObjectA(nullptr, nullptr);
    if (s.job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(s.job, JobObjectExtendedLimitInformation, &info,
                                sizeof(info));
    }

    PROCESS_INFORMATION pi{};
    // /S: strip only the outermost quotes, regardless of quotes inside.
    std::string full = "cmd.exe /S /C \"" + cmdline + "\"";
    const BOOL ok = CreateProcessA(nullptr, full.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                                   opts.cwd.empty() ? nullptr : opts.cwd.c_str(), &si, &pi);
    if (writePipe) CloseHandle(writePipe);
    if (errHandle != INVALID_HANDLE_VALUE) CloseHandle(errHandle);
    if (!ok) return nullptr;
    if (s.job) AssignProcessToJobObject(s.job, pi.hProcess);
    ResumeThread(pi.hThread);
    s.proc = pi.hProcess;
    s.thread = pi.hThread;
#else
    int pipefd[2] = {-1, -1};
    if (opts.capture && ::pipe(pipefd) != 0) return nullptr;

    const pid_t pid = ::fork();
    if (pid < 0) {
        if (pipefd[0] >= 0) ::close(pipefd[0]);
        if (pipefd[1] >= 0) ::close(pipefd[1]);
        return nullptr;
    }
    if (pid == 0) {
        // Own session/process group, so kill() can take the whole tree down.
        ::setsid();
        if (!opts.cwd.empty() && ::chdir(opts.cwd.c_str()) != 0) ::_exit(127);
        const int devnull = ::open("/dev/null", O_RDWR);
        if (devnull >= 0) ::dup2(devnull, STDIN_FILENO);
        if (opts.capture) {
            ::close(pipefd[0]);
            ::dup2(pipefd[1], STDOUT_FILENO);
            if (opts.stderrFile.empty()) ::dup2(pipefd[1], STDERR_FILENO);
            ::close(pipefd[1]);
        }
        if (!opts.stderrFile.empty()) {
            const int ef = ::open(opts.stderrFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (ef >= 0) {
                ::dup2(ef, STDERR_FILENO);
                ::close(ef);
            }
        }
        if (devnull > STDERR_FILENO) ::close(devnull);
        ::execl("/bin/sh", "sh", "-c", cmdline.c_str(), (char*)nullptr);
        ::_exit(127);
    }
    if (pipefd[1] >= 0) ::close(pipefd[1]);
    s.pid = pid;
    s.fd = pipefd[0];
#endif
    return p;
}

bool Process::startDetached(const std::string& cmdline, const std::string& cwd) {
#ifdef _WIN32
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::string full = "cmd.exe /S /C \"" + cmdline + "\"";
    if (!CreateProcessA(nullptr, full.data(), nullptr, nullptr, FALSE,
                        DETACHED_PROCESS | CREATE_NO_WINDOW, nullptr,
                        cwd.empty() ? nullptr : cwd.c_str(), &si, &pi))
        return false;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
#else
    // Double fork: the intermediate child exits immediately and we reap it, so
    // the grandchild is re-parented to init and never becomes our zombie.
    const pid_t pid = ::fork();
    if (pid < 0) return false;
    if (pid == 0) {
        ::setsid();
        if (::fork() != 0) ::_exit(0);
        if (!cwd.empty() && ::chdir(cwd.c_str()) != 0) ::_exit(127);
        const int devnull = ::open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            ::dup2(devnull, STDIN_FILENO);
            ::dup2(devnull, STDOUT_FILENO);
            ::dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO) ::close(devnull);
        }
        ::execl("/bin/sh", "sh", "-c", cmdline.c_str(), (char*)nullptr);
        ::_exit(127);
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    return true;
#endif
}

bool Process::readLine(std::string& line) {
    Impl& s = *impl_;
    for (;;) {
        const size_t nl = s.pending.find('\n');
        if (nl != std::string::npos) {
            line = s.pending.substr(0, nl);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            s.pending.erase(0, nl + 1);
            return true;
        }
        if (s.eof) {
            if (s.pending.empty()) return false;
            line = s.pending;  // unterminated tail
            if (!line.empty() && line.back() == '\r') line.pop_back();
            s.pending.clear();
            return true;
        }
        char buf[4096];
#ifdef _WIN32
        DWORD n = 0;
        if (!s.readPipe || !ReadFile(s.readPipe, buf, sizeof(buf), &n, nullptr) || n == 0) {
            s.eof = true;
            continue;
        }
        s.pending.append(buf, n);
#else
        ssize_t n;
        do {
            n = ::read(s.fd, buf, sizeof(buf));
        } while (n < 0 && errno == EINTR);
        if (s.fd < 0 || n <= 0) {
            s.eof = true;
            continue;
        }
        s.pending.append(buf, (size_t)n);
#endif
    }
}

std::string Process::readAll() {
    Impl& s = *impl_;
    std::string out = std::move(s.pending);
    s.pending.clear();
    // Reuse the read loop but keep the newlines: readLine() strips them, and
    // a JSON reply must come back byte-for-byte.
    char buf[4096];
    while (!s.eof) {
#ifdef _WIN32
        DWORD n = 0;
        if (!s.readPipe || !ReadFile(s.readPipe, buf, sizeof(buf), &n, nullptr) || n == 0) {
            s.eof = true;
            break;
        }
        out.append(buf, n);
#else
        ssize_t n;
        do {
            n = ::read(s.fd, buf, sizeof(buf));
        } while (n < 0 && errno == EINTR);
        if (s.fd < 0 || n <= 0) {
            s.eof = true;
            break;
        }
        out.append(buf, (size_t)n);
#endif
    }
    return out;
}

int Process::wait() {
    Impl& s = *impl_;
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        if (s.exited) return s.code;
    }
#ifdef _WIN32
    if (!s.proc) return -1;
    WaitForSingleObject(s.proc, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(s.proc, &code);
    std::lock_guard<std::mutex> lock(s.mutex);
    s.exited = true;
    s.code = (int)code;
    return s.code;
#else
    if (s.pid < 0) return -1;
    int status = 0;
    pid_t r;
    do {
        r = ::waitpid(s.pid, &status, 0);
    } while (r < 0 && errno == EINTR);
    std::lock_guard<std::mutex> lock(s.mutex);
    // r < 0 means running() already reaped it on another thread; its cached
    // code is the real one, so don't overwrite it with the failed wait.
    if (r > 0) {
        s.exited = true;
        s.code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    }
    return s.code;
#endif
}

bool Process::running() {
    Impl& s = *impl_;
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.exited) return false;
#ifdef _WIN32
    if (!s.proc) return false;
    if (WaitForSingleObject(s.proc, 0) == WAIT_TIMEOUT) return true;
    DWORD code = 0;
    GetExitCodeProcess(s.proc, &code);
    s.exited = true;
    s.code = (int)code;
    return false;
#else
    if (s.pid < 0) return false;
    int status = 0;
    const pid_t r = ::waitpid(s.pid, &status, WNOHANG);
    if (r == 0) return true;
    if (r > 0) {
        s.exited = true;
        s.code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    } else {
        s.exited = true;  // gone (ECHILD): nothing left to wait for
    }
    return false;
#endif
}

void Process::kill() {
    Impl& s = *impl_;
#ifdef _WIN32
    if (s.job) TerminateJobObject(s.job, 1);
    else if (s.proc) TerminateProcess(s.proc, 1);
#else
    if (s.pid < 0) return;
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        if (s.exited) return;
    }
    // The child called setsid(), so its pid is its process-group id: signal
    // the group to take the shell and everything it spawned.
    ::killpg(s.pid, SIGKILL);
    ::kill(s.pid, SIGKILL);
#endif
}

// ---------------------------------------------------------------------------
// Desktop integration
// ---------------------------------------------------------------------------

void errorBox(const std::string& title, const std::string& message) {
#ifdef _WIN32
    MessageBoxA(g_dialogOwner, message.c_str(), title.c_str(), MB_ICONERROR | MB_OK);
#elif defined(__APPLE__)
    appleScript("display alert " + asString(title) + " message " + asString(message) +
                " as critical buttons {\"OK\"} default button \"OK\"");
#else
    if (commandExists("zenity")) {
        capture("zenity --error --title=" + shQuote(title) + " --text=" +
                shQuote(message) + " >/dev/null 2>&1");
        return;
    }
    if (commandExists("kdialog")) {
        capture("kdialog --title " + shQuote(title) + " --error " + shQuote(message) +
                " >/dev/null 2>&1");
        return;
    }
    std::fprintf(stderr, "[editor] %s: %s\n", title.c_str(), message.c_str());
#endif
}

bool confirmBox(const std::string& title, const std::string& message) {
#ifdef _WIN32
    // Default button is No: the caller is about to do something irreversible,
    // so a stray Enter must not be the one that agrees to it.
    return MessageBoxA(g_dialogOwner, message.c_str(), title.c_str(),
                       MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) == IDYES;
#elif defined(__APPLE__)
    // Default button is No, as on Windows.
    return appleScript("button returned of (display alert " + asString(title) +
                       " message " + asString(message) +
                       " as warning buttons {\"No\", \"Yes\"} default button \"No\")") ==
           "Yes";
#else
    int code = -1;
    // zenity/kdialog both exit 0 for yes and non-zero for no/closed.
    if (commandExists("zenity")) {
        capture("zenity --question --title=" + shQuote(title) + " --text=" +
                    shQuote(message) + " >/dev/null 2>&1",
                &code);
        return code == 0;
    }
    if (commandExists("kdialog")) {
        capture("kdialog --title " + shQuote(title) + " --yesno " +
                    shQuote(message) + " >/dev/null 2>&1",
                &code);
        return code == 0;
    }
    // No dialog tool: answer No rather than assume consent (see the header).
    std::fprintf(stderr, "[editor] %s: %s\n  -> no dialog available, answering No\n",
                 title.c_str(), message.c_str());
    return false;
#endif
}

void openUrl(const std::string& url) {
    // Only http(s), and that is a security decision rather than tidiness: this
    // is handed URLs that came off the network (a release's html_url), and the
    // shells below would just as happily launch a "file:" or an executable.
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) return;
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    Process::startDetached("open " + shQuote(url) + " >/dev/null 2>&1");
#else
    Process::startDetached("xdg-open " + shQuote(url) + " >/dev/null 2>&1");
#endif
}

void revealInFileManager(const std::string& path) {
    if (path.empty()) return;
    std::error_code ec;
    // A path that does not exist must never reach the file manager. explorer
    // answers one by opening the user's DEFAULT folder - Documents - which
    // reads as the button having gone somewhere random rather than as "what
    // you asked for is not there any more" (reported against the Debugger's
    // *Show file*, whose frame.tga the Runner deletes on every launch). Walk up
    // to the nearest ancestor that does exist and open THAT instead; a caller
    // that wants to say more should check the file itself first.
    fs::path target(path);
    for (int guard = 0; guard < 64 && !fs::exists(target, ec); ++guard) {
        const fs::path up = target.parent_path();
        if (up.empty() || up == target) return;
        target = up;
    }
    if (!fs::exists(target, ec)) return;
    const bool isFile = fs::is_regular_file(target, ec);
#ifdef _WIN32
    // explorer.exe wants '\' - it silently opens the default folder instead of
    // selecting anything when handed a mixed path the file APIs accept.
    const std::string native = target.make_preferred().string();
    const std::string arg =
        isFile ? "/select,\"" + native + "\"" : "\"" + native + "\"";
    ShellExecuteA(nullptr, "open", "explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    // -R reveals (selects) the entry in Finder; a folder just opens.
    Process::startDetached(std::string(isFile ? "open -R " : "open ") +
                           shQuote(target.string()) + " >/dev/null 2>&1");
#else
    // The freedesktop FileManager1 interface is what actually selects an entry
    // (Nautilus, Dolphin, Nemo and Thunar all implement it). Fall back to
    // xdg-open on the containing folder when nothing answers on the bus - the
    // folder still opens, just without the file highlighted.
    const std::string uri = "file://" + target.string();
    std::string cmd =
        "dbus-send --session --print-reply --dest=org.freedesktop.FileManager1 "
        "/org/freedesktop/FileManager1 org.freedesktop.FileManager1.ShowItems "
        "array:string:" + shQuote(uri) + " string:'' >/dev/null 2>&1 || "
        "xdg-open " +
        shQuote(isFile ? target.parent_path().string() : target.string()) +
        " >/dev/null 2>&1";
    Process::startDetached(cmd);
#endif
}

bool copyImageToClipboard(const unsigned char* rgba, int width, int height,
                          const std::string& pngPath) {
    if (!rgba || width <= 0 || height <= 0) return false;
#ifdef _WIN32
    // Two formats, the pair Paint and the browsers publish: "PNG" (the saved
    // file's bytes - what Chromium/Electron apps read first) and a classic
    // bottom-up BI_RGB CF_DIB for everything older. A lone top-down
    // BI_BITFIELDS CF_DIBV5 (what this used to publish) pastes into Paint but
    // Electron apps - Claude among them - read the synthesized DIB as a broken
    // image and fail the paste, while the same PNG copied from Explorer works.
    const size_t row = (size_t)width * 4;
    const size_t pixels = row * (size_t)height;
    HGLOBAL dib = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + pixels);
    if (!dib) return false;
    unsigned char* dst = static_cast<unsigned char*>(GlobalLock(dib));
    if (!dst) {
        GlobalFree(dib);
        return false;
    }
    BITMAPINFOHEADER* head = reinterpret_cast<BITMAPINFOHEADER*>(dst);
    std::memset(head, 0, sizeof(*head));
    head->biSize = sizeof(*head);
    head->biWidth = width;
    head->biHeight = height;  // bottom-up: the layout every reader handles
    head->biPlanes = 1;
    head->biBitCount = 32;
    head->biCompression = BI_RGB;
    head->biSizeImage = (DWORD)pixels;
    for (int y = 0; y < height; ++y) {
        const unsigned char* s = rgba + (size_t)(height - 1 - y) * row;
        unsigned char* d = dst + sizeof(*head) + (size_t)y * row;
        for (int x = 0; x < width; ++x, s += 4, d += 4) {
            d[0] = s[2];
            d[1] = s[1];
            d[2] = s[0];
            d[3] = s[3];
        }
    }
    GlobalUnlock(dib);

    HGLOBAL png = nullptr;
    std::error_code ec;
    if (!pngPath.empty() && fs::is_regular_file(pngPath, ec)) {
        std::ifstream f(fs::path(pngPath), std::ios::binary);
        std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (!data.empty() && (png = GlobalAlloc(GMEM_MOVEABLE, data.size()))) {
            if (void* p = GlobalLock(png)) {
                std::memcpy(p, data.data(), data.size());
                GlobalUnlock(png);
            } else {
                GlobalFree(png);
                png = nullptr;
            }
        }
    }

    if (!OpenClipboard(g_dialogOwner)) {
        GlobalFree(dib);
        if (png) GlobalFree(png);
        return false;
    }
    EmptyClipboard();
    // Ownership transfers only on success.
    if (png && !SetClipboardData(RegisterClipboardFormatW(L"PNG"), png)) GlobalFree(png);
    const bool ok = SetClipboardData(CF_DIB, dib) != nullptr;
    CloseClipboard();
    if (!ok) GlobalFree(dib);
    return ok;
#elif defined(__APPLE__)
    if (pngPath.empty()) return false;
    std::error_code ec;
    if (!fs::is_regular_file(pngPath, ec)) return false;
    int code = -1;
    appleScript("set the clipboard to (read (POSIX file " + asString(pngPath) +
                    ") as \u00abclass PNGf\u00bb)",
                &code);
    return code == 0;
#else
    if (pngPath.empty()) return false;
    std::error_code ec;
    if (!fs::is_regular_file(pngPath, ec)) return false;
    if (commandExists("wl-copy")) {
        Process::startDetached("wl-copy --type image/png < " + shQuote(pngPath));
        return true;
    }
    if (commandExists("xclip")) {
        Process::startDetached("xclip -selection clipboard -t image/png -i " +
                               shQuote(pngPath));
        return true;
    }
    return false;
#endif
}

void installDesktopEntry(const std::string& appId, const std::string& appName,
                         const std::string& comment, const unsigned char* iconPng,
                         std::size_t iconPngSize) {
#ifdef _WIN32
    (void)appId; (void)appName; (void)comment; (void)iconPng; (void)iconPngSize;
#elif defined(__APPLE__)
    // No desktop-entry mechanism: a Dock icon comes from an .app bundle's
    // Info.plist, and a bare binary run from a checkout has none.
    (void)appId; (void)appName; (void)comment; (void)iconPng; (void)iconPngSize;
#else
    const std::string exe = exePath();
    if (exe.empty() || appId.empty()) return;

    fs::path dataHome;
    if (const char* xdg = getenv("XDG_DATA_HOME"); xdg && *xdg)
        dataHome = xdg;
    else if (const fs::path home = homeDir(); !home.empty())
        dataHome = home / ".local" / "share";
    if (dataHome.empty()) return;

    // hicolor/256x256 is what the icon.png actually is; a theme lookup for
    // "tyrax-editor" finds it there without an index rebuild.
    const fs::path iconPath =
        dataHome / "icons" / "hicolor" / "256x256" / "apps" / (appId + ".png");
    const fs::path entryPath = dataHome / "applications" / (appId + ".desktop");

    // Writes only when the bytes differ. Rewriting on every start would be
    // harmless but keeps re-stamping mtimes the desktop's file monitors watch.
    auto writeIfChanged = [](const fs::path& p, const char* data, std::size_t n) {
        std::error_code ec;
        if (fs::file_size(p, ec) == n && !ec) {
            std::string have(n, '\0');
            if (FILE* f = fopen(p.string().c_str(), "rb")) {
                const bool same = fread(have.data(), 1, n, f) == n &&
                                  memcmp(have.data(), data, n) == 0;
                fclose(f);
                if (same) return;
            }
        }
        fs::create_directories(p.parent_path(), ec);
        if (FILE* f = fopen(p.string().c_str(), "wb")) {
            fwrite(data, 1, n, f);
            fclose(f);
        }
    };

    if (iconPng && iconPngSize)
        writeIfChanged(iconPath, (const char*)iconPng, iconPngSize);

    // Exec is quoted by the desktop-entry spec's own rules, not the shell's:
    // double quotes, with a backslash before the four characters a shell would
    // still expand. shQuote's single quotes would be taken literally here.
    std::string quotedExe = "\"";
    for (const char c : exe) {
        if (c == '"' || c == '\\' || c == '$' || c == '`') quotedExe += '\\';
        quotedExe += c;
    }
    quotedExe += '"';

    // StartupWMClass is the X11 half of the same job the app id does on
    // Wayland: it is how a taskbar matches an existing window back to this
    // entry. %f lets a file manager hand us a .tyra project to open.
    const std::string entry =
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=" + appName + "\n"
        "Comment=" + comment + "\n"
        "Exec=" + quotedExe + " %f\n"
        "Icon=" + appId + "\n"
        "Terminal=false\n"
        "Categories=Development;IDE;Graphics;\n"
        "StartupWMClass=" + appId + "\n";
    writeIfChanged(entryPath, entry.data(), entry.size());
#endif
}

std::string openInVSCode(const std::string& projectDir, const std::string& absFile) {
    if (!commandExists("code"))
        return "Could not launch VS Code - is the 'code' CLI on PATH? "
               "(VS Code: Shell Command: Install 'code' in PATH)";
    // Passing the project dir opens (or reuses) that workspace; an extra file
    // path opens it in the same window (-g = goto), so we can jump straight to
    // a script or a custom-node file while keeping the whole project in view.
    std::string cmd = "code " + shellArg(projectDir);
    if (!absFile.empty()) cmd += " -g " + shellArg(absFile);
    if (!Process::startDetached(cmd, projectDir)) return "Could not launch VS Code.";
    return "";
}

// ---------------------------------------------------------------------------
// Fonts
// ---------------------------------------------------------------------------

#ifndef _WIN32
namespace {
// filename -> absolute path, built once from the freedesktop font roots. A
// bare "DejaVuSans-Bold.ttf" says nothing about which family subdirectory it
// lives in, so the index is the only way to resolve one without shelling out
// to fontconfig on every bake.
const std::map<std::string, std::string>& fontIndex() {
    static const std::map<std::string, std::string> index = [] {
        std::map<std::string, std::string> m;
#ifdef __APPLE__
        std::vector<fs::path> roots{"/System/Library/Fonts", "/Library/Fonts"};
        if (const fs::path home = homeDir(); !home.empty())
            roots.push_back(home / "Library" / "Fonts");
#else
        std::vector<fs::path> roots{"/usr/share/fonts", "/usr/local/share/fonts",
                                    "/run/host/fonts"};
        if (const fs::path home = homeDir(); !home.empty()) {
            roots.push_back(home / ".local" / "share" / "fonts");
            roots.push_back(home / ".fonts");
        }
#endif
        for (const fs::path& root : roots) {
            std::error_code ec;
            if (!fs::exists(root, ec)) continue;
            for (fs::recursive_directory_iterator it(
                     root, fs::directory_options::skip_permission_denied, ec), end;
                 it != end; it.increment(ec)) {
                if (ec) { ec.clear(); continue; }
                if (!it->is_regular_file(ec)) continue;
                std::string ext = it->path().extension().string();
                for (char& c : ext) c = (char)tolower((unsigned char)c);
                if (ext != ".ttf" && ext != ".otf" && ext != ".ttc") continue;
                m.emplace(it->path().filename().string(), it->path().string());
            }
        }
        return m;
    }();
    return index;
}
}  // namespace
#endif

std::string systemFontPath(const std::string& fileName) {
    if (fileName.empty()) return "";
#ifdef _WIN32
    char windir[MAX_PATH] = {};
    if (GetWindowsDirectoryA(windir, MAX_PATH) == 0) return "";
    const fs::path p = fs::path(windir) / "Fonts" / fileName;
    std::error_code ec;
    return fs::exists(p, ec) ? p.string() : std::string();
#else
    const auto& index = fontIndex();
    const auto it = index.find(fileName);
    return it == index.end() ? std::string() : it->second;
#endif
}

const std::vector<SystemFont>& systemFonts() {
#ifdef _WIN32
    static const std::vector<SystemFont> fonts = {
        {"Arial Bold", "arialbd.ttf"},
        {"Comic Sans MS Bold", "comicbd.ttf"},
        {"Courier New Bold", "courbd.ttf"},
        {"Georgia Bold", "georgiab.ttf"},
        {"Impact", "impact.ttf"},
        {"Segoe UI Bold", "segoeuib.ttf"},
        {"Times New Roman Bold", "timesbd.ttf"},
        {"Trebuchet MS Bold", "trebucbd.ttf"},
        {"Verdana Bold", "verdanab.ttf"},
    };
#elif defined(__APPLE__)
    // The same families Windows offers, from /System/Library/Fonts/Supplemental.
    static const std::vector<SystemFont> fonts = {
        {"Arial Bold", "Arial Bold.ttf"},
        {"Arial Black", "Arial Black.ttf"},
        {"Comic Sans MS Bold", "Comic Sans MS Bold.ttf"},
        {"Courier New Bold", "Courier New Bold.ttf"},
        {"Georgia Bold", "Georgia Bold.ttf"},
        {"Impact", "Impact.ttf"},
        {"Tahoma Bold", "Tahoma Bold.ttf"},
        {"Times New Roman Bold", "Times New Roman Bold.ttf"},
        {"Trebuchet MS Bold", "Trebuchet MS Bold.ttf"},
        {"Verdana Bold", "Verdana Bold.ttf"},
    };
#else
    // The metric-compatible and near-ubiquitous free families. Bold entries
    // throughout, like the Windows list: baked game text is small and reads
    // badly at regular weight.
    static const std::vector<SystemFont> fonts = {
        {"DejaVu Sans Bold", "DejaVuSans-Bold.ttf"},
        {"DejaVu Sans Mono Bold", "DejaVuSansMono-Bold.ttf"},
        {"DejaVu Serif Bold", "DejaVuSerif-Bold.ttf"},
        {"Liberation Sans Bold", "LiberationSans-Bold.ttf"},
        {"Liberation Mono Bold", "LiberationMono-Bold.ttf"},
        {"Liberation Serif Bold", "LiberationSerif-Bold.ttf"},
        {"Noto Sans Bold", "NotoSans-Bold.ttf"},
        {"Ubuntu Bold", "Ubuntu-B.ttf"},
    };
#endif
    return fonts;
}

const std::vector<std::string>& fallbackFontFiles() {
#ifdef _WIN32
    static const std::vector<std::string> chain = {"consolab.ttf", "arialbd.ttf",
                                                   "arial.ttf"};
#elif defined(__APPLE__)
    static const std::vector<std::string> chain = {"Arial Bold.ttf", "Verdana Bold.ttf",
                                                   "Arial.ttf"};
#else
    static const std::vector<std::string> chain = {
        "DejaVuSans-Bold.ttf", "LiberationSans-Bold.ttf", "NotoSans-Bold.ttf",
        "Ubuntu-B.ttf",        "DejaVuSans.ttf",          "LiberationSans-Regular.ttf"};
#endif
    return chain;
}

const char* defaultFontLabel() {
    // The game's default font is built in (menubake::builtinFont) and the same
    // on every OS; fallbackFontFiles() above only backs it up.
    return "Geist Mono SemiBold";
}

const std::vector<UiFontFamily>& uiFontFiles() {
#ifdef _WIN32
    // Segoe UI is what the rest of the desktop is drawn in and has shipped
    // since Vista, so the first entry practically always resolves. The
    // variable-weight Segoe UI Variable is deliberately absent: stb_truetype
    // cannot select a weight axis and would rasterize it at the wrong one.
    // Semibold (segoeuisb) rather than Bold for headings - it is what the
    // desktop itself uses for them.
    static const std::vector<UiFontFamily> fonts = {
        {"Segoe UI", "segoeui.ttf", "segoeuisb.ttf"},
        {"Tahoma", "tahoma.ttf", "tahomabd.ttf"},
        {"Verdana", "verdana.ttf", "verdanab.ttf"},
        {"Arial", "arial.ttf", "arialbd.ttf"},
    };
#elif defined(__APPLE__)
    // Helvetica Neue is a collection; face 0 is the regular weight and face
    // 10 the Medium the headings use. SF Pro (SFNS.ttf) is deliberately
    // absent - variable weight, like Segoe UI Variable on Windows.
    static const std::vector<UiFontFamily> fonts = {
        {"Helvetica Neue", "HelveticaNeue.ttc", "HelveticaNeue.ttc", 10},
        {"Verdana", "Verdana.ttf", "Verdana Bold.ttf"},
        {"Arial", "Arial.ttf", "Arial Bold.ttf"},
        {"Geneva", "Geneva.ttf", ""},
    };
#else
    // Inter first where the user installed it, then the families the desktops
    // actually ship: GNOME (Cantarell), Ubuntu, and the two metric-compatible
    // sets that are on practically every distribution. Cantarell is an .otf -
    // stb_truetype reads its CFF outlines, which is why it is allowed here at
    // all - and it sits low in the chain for that reason.
    static const std::vector<UiFontFamily> fonts = {
        {"Inter", "Inter-Regular.ttf", "Inter-SemiBold.ttf"},
        {"Noto Sans", "NotoSans-Regular.ttf", "NotoSans-SemiBold.ttf"},
        {"Ubuntu", "Ubuntu-R.ttf", "Ubuntu-M.ttf"},
        {"Cantarell", "Cantarell-Regular.otf", "Cantarell-Bold.otf"},
        {"DejaVu Sans", "DejaVuSans.ttf", "DejaVuSans-Bold.ttf"},
        {"Liberation Sans", "LiberationSans-Regular.ttf", "LiberationSans-Bold.ttf"},
    };
#endif
    return fonts;
}

const std::vector<SystemFont>& monoFontFiles() {
#ifdef _WIN32
    // Cascadia Mono ships with Windows 11 (and Terminal); Consolas since Vista.
    static const std::vector<SystemFont> fonts = {
        {"Cascadia Mono", "CascadiaMono.ttf"},
        {"Consolas", "consola.ttf"},
        {"Courier New", "cour.ttf"},
    };
#elif defined(__APPLE__)
    // Menlo is a collection whose face 0 is Regular. SF Mono (SFNSMono.ttf)
    // is a variable font - absent for the same reason SF Pro is.
    static const std::vector<SystemFont> fonts = {
        {"Menlo", "Menlo.ttc"},
        {"Courier New", "Courier New.ttf"},
    };
#else
    static const std::vector<SystemFont> fonts = {
        {"JetBrains Mono", "JetBrainsMono-Regular.ttf"},
        {"Noto Sans Mono", "NotoSansMono-Regular.ttf"},
        {"Ubuntu Mono", "UbuntuMono-R.ttf"},
        {"DejaVu Sans Mono", "DejaVuSansMono.ttf"},
        {"Liberation Mono", "LiberationMono-Regular.ttf"},
    };
#endif
    return fonts;
}

// ---------------------------------------------------------------------------
// Native file dialogs
// ---------------------------------------------------------------------------

void setDialogOwner(GLFWwindow* window) {
#ifdef _WIN32
    g_dialogOwner = window ? glfwGetWin32Window(window) : nullptr;
#else
    (void)window;  // the pickers below are separate processes
#endif
}

// ---------------------------------------------------------------------------
// Merged title bar (platform.hpp). The macOS half is Objective-C and lives in
// platform_mac.mm; Windows is below; Linux keeps the desktop's decorations.
// ---------------------------------------------------------------------------

#ifdef _WIN32
// UNVERIFIED ON WINDOWS: written and compile-checked on macOS, never run. The
// approach is the one Windows Terminal and Chromium use - verify drag, Aero
// Snap, double-click, the resize edges (the top one included), a maximized
// window's top row and a DPI change between monitors before trusting it.
namespace {

struct WinTitleBar {
    HWND hwnd = nullptr;
    WNDPROC orig = nullptr;
    float height = 0.0f;     // our bar's height, client pixels (App, per frame)
    bool overEmpty = false;  // App: the pointer is over a bare part of the bar
    DWORD lastDownTime = 0;  // our own double-click detection: GLFW's window
    POINT lastDownPt{};      // class has no CS_DBLCLKS, so no WM_LBUTTONDBLCLK
};
WinTitleBar g_titleBar;

// The sizing frame's thickness at this window's DPI. GetDpiForWindow and
// GetSystemMetricsForDpi are Windows 10 1607+; resolved at runtime so the
// editor still starts (with system-DPI metrics) on anything older.
int frameThickness(HWND hwnd, bool vertical) {
    using DpiForWindowFn = UINT(WINAPI*)(HWND);
    using MetricsForDpiFn = int(WINAPI*)(int, UINT);
    static const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    static const auto dpiForWindow =
        user32 ? (DpiForWindowFn)(void*)GetProcAddress(user32, "GetDpiForWindow") : nullptr;
    static const auto metricsForDpi =
        user32 ? (MetricsForDpiFn)(void*)GetProcAddress(user32, "GetSystemMetricsForDpi")
               : nullptr;
    const int frame = vertical ? SM_CYFRAME : SM_CXFRAME;
    const int padded = 92;  // SM_CXPADDEDBORDER (MinGW hides it below 0x0600)
    if (dpiForWindow && metricsForDpi) {
        const UINT dpi = dpiForWindow(hwnd);
        return metricsForDpi(frame, dpi) + metricsForDpi(padded, dpi);
    }
    return GetSystemMetrics(frame) + GetSystemMetrics(padded);
}

POINT lparamPoint(LPARAM lp) {
    return POINT{(LONG)(short)LOWORD(lp), (LONG)(short)HIWORD(lp)};
}

bool onEmptyBar(const WinTitleBar& t, POINT client) {
    return t.overEmpty && client.y >= 0 && (float)client.y < t.height;
}

LRESULT CALLBACK titleBarProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    WinTitleBar& t = g_titleBar;
    switch (msg) {
        case WM_NCCALCSIZE: {
            // Let the default proposal shrink the rect by the whole frame, then
            // give the caption row back to the client: left / right / bottom
            // keep their (invisible, DWM-drawn) sizing border, so resizing and
            // the shadow work as before, and the top edge is ours.
            if (!wp) break;
            auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
            const LONG top = p->rgrc[0].top;
            const LRESULT r = CallWindowProcW(t.orig, hwnd, msg, wp, lp);
            if (r != 0) return r;
            p->rgrc[0].top = top;
            // A maximized window hangs its frame off every monitor edge; without
            // this its top frame-width of client would be off-screen.
            if (IsZoomed(hwnd)) p->rgrc[0].top += frameThickness(hwnd, true);
            return 0;
        }
        case WM_NCHITTEST: {
            // The default answers the three edges that still have a frame. The
            // top one is client now, so its resize strip is answered here.
            const LRESULT hit = CallWindowProcW(t.orig, hwnd, msg, wp, lp);
            if (hit != HTCLIENT || IsZoomed(hwnd)) return hit;
            POINT pt = lparamPoint(lp);
            ScreenToClient(hwnd, &pt);
            if (pt.y < frameThickness(hwnd, true) / 2) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                const int corner = frameThickness(hwnd, false) * 2;
                if (pt.x < corner) return HTTOPLEFT;
                if (pt.x >= rc.right - corner) return HTTOPRIGHT;
                return HTTOP;
            }
            return HTCLIENT;
        }
        case WM_LBUTTONDOWN: {
            // A press on a bare part of the bar moves the window: hand it to
            // the default caption handling (move loop, Aero Snap, shake), and
            // never pass it on to GLFW - the move loop eats the release, so
            // ImGui would be left holding a pressed button.
            const POINT pt = lparamPoint(lp);
            if (!onEmptyBar(t, pt)) break;
            const DWORD now = (DWORD)GetMessageTime();
            const bool dbl =
                t.lastDownTime != 0 && now - t.lastDownTime <= GetDoubleClickTime() &&
                std::abs(pt.x - t.lastDownPt.x) <= GetSystemMetrics(SM_CXDOUBLECLK) / 2 &&
                std::abs(pt.y - t.lastDownPt.y) <= GetSystemMetrics(SM_CYDOUBLECLK) / 2;
            if (dbl) {
                t.lastDownTime = 0;
                ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
                return 0;
            }
            t.lastDownTime = now;
            t.lastDownPt = pt;
            POINT screen = pt;
            ClientToScreen(hwnd, &screen);
            ReleaseCapture();
            SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(screen.x, screen.y));
            return 0;
        }
        case WM_RBUTTONUP: {
            // The caption's right-click menu (Restore / Move / Size / ... / Close).
            const POINT pt = lparamPoint(lp);
            if (!onEmptyBar(t, pt)) break;
            POINT screen = pt;
            ClientToScreen(hwnd, &screen);
            const HMENU menu = GetSystemMenu(hwnd, FALSE);
            if (!menu) break;
            const UINT cmd = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                                  screen.x, screen.y, 0, hwnd, nullptr);
            if (cmd) PostMessageW(hwnd, WM_SYSCOMMAND, cmd, 0);
            return 0;
        }
        default:
            break;
    }
    return CallWindowProcW(t.orig, hwnd, msg, wp, lp);
}

}  // namespace
#endif

bool titleBarMergeSupported() {
#if defined(_WIN32) || defined(__APPLE__)
    return true;
#else
    return false;
#endif
}

bool titleBarHasCaptionButtons() {
#ifdef _WIN32
    return true;
#else
    return false;  // macOS keeps AppKit's traffic lights; Linux is not merged
#endif
}

#ifndef __APPLE__  // the macOS half of these three is in platform_mac.mm
bool mergeTitleBar(GLFWwindow* window) {
#ifdef _WIN32
    const HWND hwnd = window ? glfwGetWin32Window(window) : nullptr;
    if (!hwnd) return false;
    if (g_titleBar.hwnd) return g_titleBar.hwnd == hwnd;  // one main window
    g_titleBar.hwnd = hwnd;
    g_titleBar.orig =
        (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)&titleBarProc);
    if (!g_titleBar.orig) {
        g_titleBar.hwnd = nullptr;
        return false;
    }
    // One pixel of DWM frame into the client keeps the thin top border and
    // the shadow Windows 10 draws only around a frame it owns.
    const MARGINS margins{0, 0, 1, 0};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    // Re-run WM_NCCALCSIZE now rather than at the next resize.
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return true;
#else
    // Linux: the native decorations stay. Merging would mean client-side
    // decorations (libdecor on Wayland, _MOTIF_WM_HINTS + _NET_WM_MOVERESIZE
    // on X11) - a separate project with its own compositor matrix.
    (void)window;
    return false;
#endif
}

void updateTitleBar(GLFWwindow* window, const TitleBarFrame& frame) {
    (void)window;
#ifdef _WIN32
    g_titleBar.height = frame.height;
    g_titleBar.overEmpty = frame.overEmpty;
#else
    (void)frame;  // Linux: never merged
#endif
}

float titleBarInsetLeft(GLFWwindow* window) {
    (void)window;  // Windows: the caption buttons are App's, on the RIGHT end
    return 0.0f;
}

void ownToolWindow(GLFWwindow* owner, GLFWwindow* tool) {
#ifdef _WIN32
    // UNVERIFIED ON WINDOWS. An owned window stays above its owner, hides
    // when the owner is minimized and gets no Alt+Tab entry of its own.
    // GWLP_HWNDPARENT sets the OWNER of a top-level window (not a parent).
    const HWND o = owner ? glfwGetWin32Window(owner) : nullptr;
    const HWND t = tool ? glfwGetWin32Window(tool) : nullptr;
    if (o && t) SetWindowLongPtrW(t, GWLP_HWNDPARENT, (LONG_PTR)o);
#else
    // Linux: the GLFW backend types the window _NET_WM_WINDOW_TYPE_DIALOG,
    // which most window managers keep above the application's other windows.
    // (A transient-for hint would need the X11 headers here.)
    (void)owner;
    (void)tool;
#endif
}
#endif  // !__APPLE__

bool multiViewportsByDefault() {
#ifdef _WIN32
    return true;
#else
#ifdef __APPLE__
    return true;
#else
    const char* display = getenv("WAYLAND_DISPLAY");
    const char* session = getenv("XDG_SESSION_TYPE");
    if (display && *display) return false;
    if (session && std::strcmp(session, "wayland") == 0) return false;
    return true;  // X11
#endif
#endif
}

std::string pickFile(const std::string& title, const std::vector<FileFilter>& filters) {
#ifdef _WIN32
    // Classic comdlg32 file dialog. The shell-based IFileOpenDialog wedges on
    // some setups (grayed Open button, dialog never returns) - the legacy
    // dialog carries far less shell machinery. The filter is the double-NUL
    // comdlg format: "Label\0*.a;*.b\0...\0\0".
    std::wstring filter;
    for (const FileFilter& f : filters) {
        std::string patterns;
        for (const std::string& p : f.patterns) {
            if (!patterns.empty()) patterns += ';';
            patterns += p;
        }
        filter += utf8ToWide(f.label);
        filter.push_back(L'\0');
        filter += utf8ToWide(patterns);
        filter.push_back(L'\0');
    }
    filter.push_back(L'\0');

    wchar_t buf[MAX_PATH] = L"";
    const std::wstring wTitle = utf8ToWide(title);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_dialogOwner;
    ofn.lpstrFilter = filter.c_str();
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = wTitle.c_str();
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return "";
    return wideToUtf8(buf);
#elif defined(__APPLE__)
    // NSOpenPanel through osascript, the zenity of macOS. `of type` takes
    // bare extensions; a filter that is a catch-all ("*", "*.*") drops the
    // restriction altogether.
    std::string types;
    bool any = filters.empty();
    for (const FileFilter& f : filters)
        for (const std::string& pat : f.patterns) {
            if (pat == "*" || pat == "*.*") any = true;
            else if (pat.rfind("*.", 0) == 0) {
                if (!types.empty()) types += ", ";
                types += asString(pat.substr(2));
            }
        }
    std::string script = "POSIX path of (choose file with prompt " + asString(title);
    if (!any && !types.empty()) script += " of type {" + types + "}";
    script += ")";
    int code = -1;
    const std::string out = appleScript(script, &code);
    return code == 0 ? out : std::string();
#else
    // No portable native dialog exists on Linux, so shell out to whichever
    // desktop helper is installed. zenity ships with GNOME (and is a
    // dependency of a great many things elsewhere); kdialog is the KDE twin.
    // Both print the chosen path on stdout and exit non-zero on cancel.
    if (commandExists("zenity")) {
        std::string cmd = "zenity --file-selection --title=" + shQuote(title);
        for (const FileFilter& f : filters) {
            std::string spec = f.label + " |";
            for (const std::string& p : f.patterns) spec += " " + p;
            cmd += " --file-filter=" + shQuote(spec);
        }
        int code = -1;
        const std::string out = capture(cmd + " 2>/dev/null", &code);
        return code == 0 ? out : std::string();
    }
    if (commandExists("kdialog")) {
        std::string spec;
        for (const FileFilter& f : filters) {
            if (!spec.empty()) spec += "\n";
            std::string patterns;
            for (const std::string& p : f.patterns) {
                if (!patterns.empty()) patterns += ' ';
                patterns += p;
            }
            spec += patterns + "|" + f.label;
        }
        int code = -1;
        const std::string out =
            capture("kdialog --title " + shQuote(title) + " --getopenfilename " +
                        shQuote(homeDir().string()) + " " + shQuote(spec) + " 2>/dev/null",
                    &code);
        return code == 0 ? out : std::string();
    }
    std::fprintf(stderr,
                 "[editor] No file dialog available - install zenity (or kdialog) "
                 "to browse for files.\n");
    return "";
#endif
}

std::string pickFolder(const std::string& title) {
#ifdef _WIN32
    // Folders need the shell dialog: comdlg32 has no folder mode.
    std::string result;
    const HRESULT hrInit =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileOpenDialog* dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                                   IID_PPV_ARGS(&dialog)))) {
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        const std::wstring wTitle = utf8ToWide(title);
        if (!wTitle.empty()) dialog->SetTitle(wTitle.c_str());
        if (SUCCEEDED(dialog->Show(g_dialogOwner))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    result = wideToUtf8(path);
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
    }
    if (SUCCEEDED(hrInit)) CoUninitialize();
    return result;
#elif defined(__APPLE__)
    int code = -1;
    std::string out =
        appleScript("POSIX path of (choose folder with prompt " + asString(title) + ")", &code);
    if (code != 0) return "";
    // POSIX path of a folder ends in '/'; the zenity answer this mirrors does not.
    while (out.size() > 1 && out.back() == '/') out.pop_back();
    return out;
#else
    if (commandExists("zenity")) {
        int code = -1;
        const std::string out =
            capture("zenity --file-selection --directory --title=" + shQuote(title) +
                        " 2>/dev/null",
                    &code);
        return code == 0 ? out : std::string();
    }
    if (commandExists("kdialog")) {
        int code = -1;
        const std::string out = capture("kdialog --title " + shQuote(title) +
                                            " --getexistingdirectory " +
                                            shQuote(homeDir().string()) + " 2>/dev/null",
                                        &code);
        return code == 0 ? out : std::string();
    }
    std::fprintf(stderr,
                 "[editor] No folder dialog available - install zenity (or kdialog) "
                 "to browse for folders.\n");
    return "";
#endif
}

}  // namespace platform
