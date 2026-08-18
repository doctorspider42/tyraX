#include "update.hpp"

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <mutex>

#include "json.hpp"
#include "platform.hpp"
#include "version.hpp"

namespace fs = std::filesystem;

namespace update {

namespace {

// The one curl in flight, so cancel() can reach it. A raw pointer under a
// mutex, held only for as long as curlRun's unique_ptr owns the process -
// Process::kill() is explicitly safe to call from another thread than the one
// blocked in readAll()/wait(), which is the whole reason this works.
std::mutex gProcMutex;
platform::Process* gProc = nullptr;
std::atomic<bool> gCancelled{false};

// One curl invocation, captured. `args` is already shell-quoted by the caller.
//
// -s and NOT -sS: with -S curl writes its error text to stderr, which this
// captures into the SAME pipe as the body - a failed request would then hand
// the JSON parser a sentence. The exit code is the whole diagnosis instead.
bool curlRun(const std::string& args, std::string* body, std::string& error) {
    if (!platform::commandExists("curl")) {
        error =
            "curl was not found on PATH - the update check needs it "
            "(it ships with Windows 10 1803 and with every Linux distribution).";
        return false;
    }
    if (gCancelled.load()) {
        error = "cancelled";
        return false;
    }
    platform::Process::Options opts;
    opts.capture = body != nullptr;
    auto proc = platform::Process::start("curl " + args, opts);
    if (!proc) {
        error = "could not run curl";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(gProcMutex);
        gProc = proc.get();
    }
    if (body) *body = proc->readAll();
    const int rc = proc->wait();
    {
        std::lock_guard<std::mutex> lock(gProcMutex);
        gProc = nullptr;
    }
    if (gCancelled.load()) {
        error = "cancelled";
        return false;
    }
    if (rc == 0) return true;
    switch (rc) {
        case 6: error = "cannot resolve api.github.com - no network?"; break;
        case 7: error = "cannot reach github.com - no network?"; break;
        case 22: error = "GitHub refused the request (HTTP error)"; break;
        case 28: error = "the request timed out"; break;
        case 35:
        case 60: error = "the TLS handshake failed"; break;
        default: error = "curl failed with exit code " + std::to_string(rc); break;
    }
    return false;
}

// Splits the "<body>\n<status>" curl -w writes off the end. Returns 0 when
// there is no status line to find, which reads as "ask the body instead".
int takeHttpStatus(std::string& out) {
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    const size_t nl = out.rfind('\n');
    const std::string tail = nl == std::string::npos ? out : out.substr(nl + 1);
    if (tail.size() != 3 || !std::isdigit((unsigned char)tail[0]) ||
        !std::isdigit((unsigned char)tail[1]) || !std::isdigit((unsigned char)tail[2]))
        return 0;
    out = nl == std::string::npos ? std::string() : out.substr(0, nl);
    return std::atoi(tail.c_str());
}

bool endsWithNoCase(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    for (size_t i = 0; i < suffix.size(); ++i) {
        const unsigned char a = (unsigned char)s[s.size() - suffix.size() + i];
        const unsigned char b = (unsigned char)suffix[i];
        if (std::tolower(a) != std::tolower(b)) return false;
    }
    return true;
}

}  // namespace

int compareVersions(const std::string& a, const std::string& b) {
    auto part = [](const std::string& s, size_t& i) {
        // A leading "v" (a tag handed over unpeeled) and any separator noise
        // before the digits are skipped; the number stops at the first
        // non-digit, so "1.51.0-rc1" reads as 1.51.0 rather than as garbage.
        while (i < s.size() && !std::isdigit((unsigned char)s[i])) {
            if (s[i] == '+' || s[i] == '-') return -1;  // build/pre-release tail
            ++i;
        }
        if (i >= s.size()) return 0;
        int n = 0;
        while (i < s.size() && std::isdigit((unsigned char)s[i]))
            n = n * 10 + (s[i++] - '0');
        return n;
    };
    size_t ia = 0, ib = 0;
    for (int k = 0; k < 3; ++k) {
        const int pa = part(a, ia), pb = part(b, ib);
        const int va = pa < 0 ? 0 : pa, vb = pb < 0 ? 0 : pb;
        if (va != vb) return va < vb ? -1 : 1;
    }
    return 0;
}

const char* platformAssetSuffix() {
#ifdef _WIN32
    return ".exe";
#else
    // The tarball, not the .deb or the .rpm: it is the one format an editor can
    // apply to itself without asking anybody for root (docs/updates.md), and a
    // package-managed install is refused by selfInstallBlocked() before the
    // asset is ever fetched. The architecture is in the name because a release
    // may one day carry more than one.
    return "-linux-x86_64.tar.gz";
#endif
}

bool parseRelease(const std::string& body, Release& out, std::string& error,
                  const std::string& assetSuffix) {
    json::Value root;
    if (!json::parse(body, root) || root.type != json::Value::Type::Object) {
        error = "GitHub's answer was not a JSON object";
        return false;
    }
    const json::Value* tag = root.find("tag_name");
    if (!tag || tag->type != json::Value::Type::String) {
        // The documented shape of "there is nothing here yet" and of every API
        // error alike, and it is worth relaying verbatim: rate limiting says so
        // in this field.
        if (const json::Value* msg = root.find("message"))
            error = "GitHub says: " + msg->stringOr("(no message)");
        else
            error = "no release information in GitHub's answer";
        return false;
    }
    out = Release{};
    out.tag = tag->str;
    out.version = out.tag;
    if (!out.version.empty() && (out.version[0] == 'v' || out.version[0] == 'V'))
        out.version.erase(0, 1);
    if (const json::Value* v = root.find("body")) out.notes = v->stringOr("");
    if (const json::Value* v = root.find("html_url")) out.pageUrl = v->stringOr("");
    // This platform's package among the several a release carries, picked by
    // suffix - see platformAssetSuffix() for why it is a suffix and not a name.
    if (const json::Value* assets = root.find("assets");
        assets && assets->type == json::Value::Type::Array) {
        for (const json::Value& a : assets->arr) {
            const json::Value* name = a.find("name");
            const json::Value* url = a.find("browser_download_url");
            if (!name || !url) continue;
            if (!endsWithNoCase(name->stringOr(""), assetSuffix)) continue;
            out.assetName = name->stringOr("");
            out.assetUrl = url->stringOr("");
            if (const json::Value* sz = a.find("size"))
                out.assetBytes = (long long)sz->numberOr(0);
            break;
        }
    }
    return true;
}

bool fetchLatest(Release& out, std::string& error) {
    const std::string url =
        std::string("https://api.github.com/repos/") + kRepo + "/releases/latest";
    // The API wants a User-Agent and answers politely to a named one. 20
    // seconds is the whole budget, connect included - this runs at startup and
    // must never be something the editor waits on.
    //
    // DELIBERATELY NO -f, and the status code comes back through -w instead:
    // the three answers that are not a release (no releases yet, rate limited,
    // anything else) are worth telling apart, and -f collapses all of them into
    // "exit code 22" with an empty body. A repository whose first release has
    // not happened yet is the state this feature ships in.
    const std::string args =
        "-sL --max-time 20 --connect-timeout 8 -w " + platform::shellArg("\\n%{http_code}") +
        " -H " + platform::shellArg("Accept: application/vnd.github+json") +
        " -A " + platform::shellArg(std::string("TyraX/") + version::kEditorVersion) +
        " " + platform::shellArg(url);
    std::string body;
    if (!curlRun(args, &body, error)) return false;
    switch (const int status = takeHttpStatus(body)) {
        case 200:
        case 0:  // no status line: let the body speak for itself
            break;
        case 404:
            error = "no releases have been published yet";
            return false;
        case 403:
        case 429:
            error = "GitHub is rate-limiting this machine - try again later";
            return false;
        default:
            error = "GitHub answered HTTP " + std::to_string(status);
            return false;
    }
    return parseRelease(body, out, error);
}

bool isNewer(const Release& r) {
    return !r.version.empty() &&
           compareVersions(r.version, version::kEditorVersion) > 0;
}

fs::path downloadDir() {
    const fs::path base = platform::configDir();
    if (base.empty()) return {};
    return base / "updates";
}

bool download(const std::string& url, const fs::path& dest, std::string& error) {
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    fs::remove(dest, ec);
    // 30 minutes, and a stall guard: a transfer that has been under 1 KB/s for
    // half a minute is a dead connection, not a slow one.
    const std::string args =
        "-sfL --max-time 1800 --connect-timeout 8 "
        "--speed-limit 1024 --speed-time 30 "
        "-A " + platform::shellArg(std::string("TyraX/") + version::kEditorVersion) +
        " -o " + platform::shellArg(dest.string()) + " " + platform::shellArg(url);
    if (!curlRun(args, nullptr, error)) {
        // A partial file is worse than none: the next run would find it and
        // could hand a truncated installer to the user.
        fs::remove(dest, ec);
        return false;
    }
    if (!fs::exists(dest, ec) || fs::file_size(dest, ec) == 0) {
        error = "the download produced no file";
        fs::remove(dest, ec);
        return false;
    }
    return true;
}

void cancel() {
    gCancelled.store(true);
    std::lock_guard<std::mutex> lock(gProcMutex);
    if (gProc) gProc->kill();
}

fs::path installRoot() {
    const std::string exe = platform::exePath();
    if (exe.empty()) return {};
    // <root>/bin/tyrax-editor. exePath() resolves /proc/self/exe through
    // canonical(), so a /usr/bin symlink into /opt lands on the real tree.
    std::error_code ec;
    const fs::path root = fs::path(exe).parent_path().parent_path();
    const fs::path canon = fs::weakly_canonical(root, ec);
    return ec ? root : canon;
}

InstallKind installKind() {
#ifdef _WIN32
    return InstallKind::Windows;
#else
    const fs::path root = installRoot();
    if (root.empty()) return InstallKind::Source;
    std::ifstream f(root / ".tyrax-package");
    std::string word;
    if (!(f >> word)) return InstallKind::Source;
    if (word == "tarball") return InstallKind::Tarball;
    if (word == "deb") return InstallKind::Deb;
    if (word == "rpm") return InstallKind::Rpm;
    return InstallKind::Source;
#endif
}

std::string selfInstallBlocked() {
    switch (installKind()) {
        case InstallKind::Windows:
            return {};
        case InstallKind::Deb:
            return "This TyraX was installed from a .deb, so its files belong to "
                   "your package manager. Download the new .deb from the release "
                   "page and install it with "
                   "'sudo apt install ./tyrax_<version>_amd64.deb'.";
        case InstallKind::Rpm:
            return "This TyraX was installed from an .rpm, so its files belong to "
                   "your package manager. Download the new .rpm from the release "
                   "page and install it with "
                   "'sudo dnf install ./tyrax-<version>-1.x86_64.rpm'.";
        case InstallKind::Source:
            return "This TyraX runs from a source checkout, which the editor will "
                   "not overwrite. Update it with 'git pull' and ./build.sh "
                   "(./build.ps1 on Windows).";
        case InstallKind::Tarball:
            break;
    }
    // A tarball anybody may have unpacked into a directory they cannot write
    // to. Probing beats guessing from ownership: root, a group, an ACL and a
    // read-only mount all answer the same question differently.
    const fs::path root = installRoot();
    const fs::path probe = root / ".tyrax-write-probe";
    std::error_code ec;
    {
        std::ofstream f(probe);
        if (!f) {
            return "TyraX cannot write to " + root.string() +
                   ", so it cannot update itself. Unpack the new tarball there "
                   "yourself, or reinstall somewhere you own.";
        }
    }
    fs::remove(probe, ec);
    return {};
}

bool runInstaller(const fs::path& file, std::string& error) {
    if (const std::string why = selfInstallBlocked(); !why.empty()) {
        error = why;
        return false;
    }
#ifdef _WIN32
    // /SILENT is a progress window and no questions; /RELAUNCH=1 is our own
    // parameter, read by the Check: in installer/tyrax.iss, and it is what
    // brings the editor back afterwards. The install is per-user by default, so
    // there is no UAC prompt in the middle of this.
    const std::string cmd = platform::shellArg(file.string()) +
                            " /SILENT /SUPPRESSMSGBOXES /NORESTART /RELAUNCH=1";
    if (!platform::Process::startDetached(cmd)) {
        error = "could not start the installer";
        return false;
    }
    return true;
#else
    // A TARBALL CANNOT UNPACK ITSELF OVER A RUNNING EDITOR, so the Linux half
    // of what tyrax.iss does is a small script we write and detach: it waits
    // for THIS process to exit (the editor closes itself right after this
    // returns, through the ordinary path that asks about unsaved work), lays
    // the new tree over the old one and starts the editor again. Detached
    // means setsid, so our own exit does not take it with us.
    //
    // `cp -a` OVERLAYS rather than replaces: a file that a later release
    // dropped is left behind. That is deliberate and it is what the Windows
    // installer does too (ignoreversion overwrites, it does not prune) - the
    // alternative is deleting a directory the user may have put their own
    // projects in.
    const fs::path root = installRoot();
    const std::string exe = platform::exePath();
    if (root.empty() || exe.empty()) {
        error = "could not work out where TyraX is installed";
        return false;
    }
    // A FIXED name beside the download, so the script cannot accumulate: it
    // outlives the update that ran it (a running /bin/sh reads its file as it
    // goes, so it must not delete itself) and the next update overwrites it.
    const fs::path script = file.parent_path() / "tyrax-update.sh";
    {
        std::ofstream f(script);
        if (!f) {
            error = "could not write the update script to " + script.string();
            return false;
        }
        f << "#!/bin/sh\n"
             "# Written by TyraX (update::runInstaller). Safe to delete.\n"
             "# Waits for the editor to exit, unpacks the new release over its\n"
             "# install directory and starts it again.\n"
             "pid=" << platform::processId() << "\n"
             "i=0\n"
             // 120 s, then go ahead anyway: an editor still up after two
             // minutes is one stuck in a dialog, and unpacking under it is
             // still better than an update that silently never happened.
             "while kill -0 \"$pid\" 2>/dev/null && [ \"$i\" -lt 600 ]; do\n"
             "    sleep 0.2\n"
             "    i=$((i + 1))\n"
             "done\n"
             "tmp=$(mktemp -d) || exit 1\n"
             "tar -xzf " << platform::shellArg(file.string()) << " -C \"$tmp\" || exit 1\n"
             // The archive holds exactly one top-level directory, named for
             // the version; its CONTENTS are what goes over the install root.
             "src=$(find \"$tmp\" -mindepth 1 -maxdepth 1 -type d | head -n 1)\n"
             "[ -n \"$src\" ] || exit 1\n"
             "cp -a \"$src/.\" " << platform::shellArg(root.string() + "/") << " || exit 1\n"
             "rm -rf \"$tmp\" " << platform::shellArg(file.string()) << "\n"
             "exec " << platform::shellArg(exe) << "\n";
    }
    std::error_code ec;
    fs::permissions(script, fs::perms::owner_all, ec);
    if (!platform::Process::startDetached("/bin/sh " + platform::shellArg(script.string()))) {
        error = "could not start the update script";
        return false;
    }
    return true;
#endif
}

}  // namespace update
