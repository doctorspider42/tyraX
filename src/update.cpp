#include "update.hpp"

#include <atomic>
#include <cctype>
#include <cstdlib>
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

bool parseRelease(const std::string& body, Release& out, std::string& error) {
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
    // The Windows installer, picked by extension rather than by a name pattern:
    // the release workflow stamps the version into the file name, so anything
    // matching "TyraX-Setup-<version>.exe" here would have to be kept in step
    // with a string in a YAML file for no gain.
    if (const json::Value* assets = root.find("assets");
        assets && assets->type == json::Value::Type::Array) {
        for (const json::Value& a : assets->arr) {
            const json::Value* name = a.find("name");
            const json::Value* url = a.find("browser_download_url");
            if (!name || !url) continue;
            if (!endsWithNoCase(name->stringOr(""), ".exe")) continue;
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

bool runInstaller(const fs::path& file, std::string& error) {
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
    (void)file;
    // Deliberate: there is no Linux package yet (docs/backlog.md). The UI never
    // offers the button that would land here, and if something ever does, it
    // gets a sentence rather than a half-working install.
    error = "TyraX has no Linux installer yet - update from the release page";
    return false;
#endif
}

}  // namespace update
