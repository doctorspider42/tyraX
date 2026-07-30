#include "platform.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <mutex>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#else
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
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
#else
    std::error_code ec;
    // /proc/self/exe is the authoritative answer on Linux and survives a
    // renamed or deleted binary; canonical() resolves the symlink for us.
    const fs::path p = fs::canonical("/proc/self/exe", ec);
    if (!ec) return p.string();
    return "";
#endif
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

std::string killByName(const std::vector<std::string>& processNames) {
    std::string cmd;
    for (const std::string& raw : processNames) {
        if (raw.empty()) continue;
#ifdef _WIN32
        std::string name = raw;
        if (name.size() < 4 || name.compare(name.size() - 4, 4, ".exe") != 0)
            name += ".exe";
        cmd += "taskkill /F /IM " + name + " 2>nul & ";
#else
        // -x: exact process name. A wrapper (a PCSX2 AppImage, a flatpak
        // launcher) shows up under its own name, which is why callers pass the
        // basename of whatever they actually launched alongside the standard
        // names rather than relying on a fuzzy -f match that could just as
        // easily match the editor's own command line.
        cmd += "pkill -x " + shQuote(raw) + " >/dev/null 2>&1; ";
#endif
    }
    cmd += "exit 0";
    return cmd;
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

void revealInFileManager(const std::string& path) {
    if (path.empty()) return;
    std::error_code ec;
    const bool isFile = fs::is_regular_file(path, ec);
#ifdef _WIN32
    // explorer.exe wants '\' - it silently opens the default folder instead of
    // selecting anything when handed a mixed path the file APIs accept.
    const std::string native = fs::path(path).make_preferred().string();
    const std::string arg =
        isFile ? "/select,\"" + native + "\"" : "\"" + native + "\"";
    ShellExecuteA(nullptr, "open", "explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
#else
    // The freedesktop FileManager1 interface is what actually selects an entry
    // (Nautilus, Dolphin, Nemo and Thunar all implement it). Fall back to
    // xdg-open on the containing folder when nothing answers on the bus - the
    // folder still opens, just without the file highlighted.
    const std::string uri = "file://" + path;
    std::string cmd =
        "dbus-send --session --print-reply --dest=org.freedesktop.FileManager1 "
        "/org/freedesktop/FileManager1 org.freedesktop.FileManager1.ShowItems "
        "array:string:" + shQuote(uri) + " string:'' >/dev/null 2>&1 || "
        "xdg-open " +
        shQuote(isFile ? fs::path(path).parent_path().string() : path) + " >/dev/null 2>&1";
    Process::startDetached(cmd);
#endif
}

void installDesktopEntry(const std::string& appId, const std::string& appName,
                         const std::string& comment, const unsigned char* iconPng,
                         std::size_t iconPngSize) {
#ifdef _WIN32
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
        std::vector<fs::path> roots{"/usr/share/fonts", "/usr/local/share/fonts",
                                    "/run/host/fonts"};
        if (const fs::path home = homeDir(); !home.empty()) {
            roots.push_back(home / ".local" / "share" / "fonts");
            roots.push_back(home / ".fonts");
        }
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
#else
    static const std::vector<std::string> chain = {
        "DejaVuSans-Bold.ttf", "LiberationSans-Bold.ttf", "NotoSans-Bold.ttf",
        "Ubuntu-B.ttf",        "DejaVuSans.ttf",          "LiberationSans-Regular.ttf"};
#endif
    return chain;
}

const char* defaultFontLabel() {
#ifdef _WIN32
    return "Consolas Bold";
#else
    return "DejaVu Sans Bold";
#endif
}

const std::vector<SystemFont>& uiFontFiles() {
#ifdef _WIN32
    // Segoe UI is what the rest of the desktop is drawn in and has shipped
    // since Vista, so the first entry practically always resolves. The
    // variable-weight Segoe UI Variable is deliberately absent: stb_truetype
    // cannot select a weight axis and would rasterize it at the wrong one.
    static const std::vector<SystemFont> fonts = {
        {"Segoe UI", "segoeui.ttf"},
        {"Tahoma", "tahoma.ttf"},
        {"Verdana", "verdana.ttf"},
        {"Arial", "arial.ttf"},
    };
#else
    // Inter first where the user installed it, then the families the desktops
    // actually ship: GNOME (Cantarell), Ubuntu, and the two metric-compatible
    // sets that are on practically every distribution. Cantarell is an .otf -
    // stb_truetype reads its CFF outlines, which is why it is allowed here at
    // all - and it sits low in the chain for that reason.
    static const std::vector<SystemFont> fonts = {
        {"Inter", "Inter-Regular.ttf"},
        {"Noto Sans", "NotoSans-Regular.ttf"},
        {"Ubuntu", "Ubuntu-R.ttf"},
        {"Cantarell", "Cantarell-Regular.otf"},
        {"DejaVu Sans", "DejaVuSans.ttf"},
        {"Liberation Sans", "LiberationSans-Regular.ttf"},
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
