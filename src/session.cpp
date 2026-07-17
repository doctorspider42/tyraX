#include "session.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "json.hpp"

namespace fs = std::filesystem;

namespace session {

namespace {

double nowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// Minimal JSON string escape for the protocol messages (paths, names). Same
// escapes json.cpp decodes.
std::string jsonEsc(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                out += ((unsigned char)c < 0x20) ? ' ' : c;
        }
    }
    return out;
}

std::string strField(const json::Value& v, const char* key, const char* def = "") {
    const json::Value* f = v.find(key);
    return f ? f->stringOr(def) : def;
}
int64_t intField(const json::Value& v, const char* key, int64_t def = 0) {
    const json::Value* f = v.find(key);
    return f ? (int64_t)f->numberOr((double)def) : def;
}
bool boolField(const json::Value& v, const char* key, bool def = false) {
    const json::Value* f = v.find(key);
    return f ? f->boolOr(def) : def;
}

std::string hex16(uint64_t v) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)v);
    return buf;
}

// A manifest path must stay inside the project dir: relative, no drive, no
// ".." segments. Anything else is a hostile/corrupt host - the client aborts.
bool safeRelPath(const std::string& p) {
    if (p.empty() || p.size() > 2048) return false;
    if (p[0] == '/' || p[0] == '\\') return false;
    if (p.find(':') != std::string::npos) return false;
    size_t i = 0;
    while (i <= p.size()) {
        const size_t j = p.find_first_of("/\\", i);
        const std::string seg =
            p.substr(i, j == std::string::npos ? std::string::npos : j - i);
        if (seg == "..") return false;
        if (j == std::string::npos) break;
        i = j + 1;
    }
    return true;
}

fs::path joinRel(const fs::path& root, const std::string& rel) {
    fs::path out = root;
    size_t i = 0;
    while (i <= rel.size()) {
        const size_t j = rel.find_first_of("/\\", i);
        const std::string seg =
            rel.substr(i, j == std::string::npos ? std::string::npos : j - i);
        if (!seg.empty() && seg != ".") out /= seg;
        if (j == std::string::npos) break;
        i = j + 1;
    }
    return out;
}

int64_t mtimeTicks(const fs::path& p) {
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    if (ec) return 0;
    return (int64_t)t.time_since_epoch().count();
}

// --- Persistent host-side hash memo ------------------------------------------
// (path, size, mtime) -> content hash, so hosting a 10 GB project does not
// rehash unchanged assets on every session. Machine-global, next to editor.ini.

struct MemoEntry {
    uint64_t size = 0;
    int64_t mtime = 0;
    uint64_t hash = 0;
};
using HashMemo = std::unordered_map<std::string, MemoEntry>;  // key = absolute path

fs::path hashMemoPath() {
    const char* base = getenv("LOCALAPPDATA");
    if (!base || !*base) base = getenv("USERPROFILE");
    if (!base || !*base) return {};
    return fs::path(base) / "tyra-editor" / "hash-cache.json";
}

HashMemo loadHashMemo() {
    HashMemo memo;
    const fs::path path = hashMemoPath();
    if (path.empty()) return memo;
    std::ifstream f(path, std::ios::binary);
    if (!f) return memo;
    std::stringstream ss;
    ss << f.rdbuf();
    json::Value root;
    if (!json::parse(ss.str(), root)) return memo;
    if (const auto* files = root.find("files");
        files && files->type == json::Value::Type::Array) {
        for (const auto& jf : files->arr) {
            const std::string p = strField(jf, "p");
            if (p.empty()) continue;
            MemoEntry e;
            e.size = (uint64_t)intField(jf, "s");
            e.mtime = intField(jf, "mt");
            e.hash = strtoull(strField(jf, "h").c_str(), nullptr, 16);
            memo[p] = e;
        }
    }
    return memo;
}

void saveHashMemo(const HashMemo& memo) {
    const fs::path path = hashMemoPath();
    if (path.empty()) return;
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return;
    f << "{\"files\":[";
    bool first = true;
    for (const auto& [p, e] : memo) {
        f << (first ? "" : ",") << "\n{\"p\":\"" << jsonEsc(p) << "\",\"s\":" << e.size
          << ",\"mt\":" << e.mtime << ",\"h\":\"" << hex16(e.hash) << "\"}";
        first = false;
    }
    f << "\n]}\n";
}

// --- Client-side per-project transfer cache -----------------------------------
// <cacheRoot>/<projectId>/cache.json: what the last sync materialized. Only
// files recorded here are ever deleted on a re-sync (never bin/, .git/ or
// anything the client built locally).

struct CacheEntry {
    uint64_t size = 0;
    uint64_t hash = 0;
    int64_t mtime = 0;  // of the local file right after it was written
};
using CacheMap = std::map<std::string, CacheEntry>;  // key = manifest rel path

CacheMap loadCacheMap(const fs::path& cacheJson) {
    CacheMap map;
    std::ifstream f(cacheJson, std::ios::binary);
    if (!f) return map;
    std::stringstream ss;
    ss << f.rdbuf();
    json::Value root;
    if (!json::parse(ss.str(), root)) return map;
    if (const auto* files = root.find("files");
        files && files->type == json::Value::Type::Array) {
        for (const auto& jf : files->arr) {
            const std::string p = strField(jf, "p");
            if (p.empty() || !safeRelPath(p)) continue;
            CacheEntry e;
            e.size = (uint64_t)intField(jf, "s");
            e.hash = strtoull(strField(jf, "h").c_str(), nullptr, 16);
            e.mtime = intField(jf, "mt");
            map[p] = e;
        }
    }
    return map;
}

void saveCacheMap(const fs::path& cacheJson, const CacheMap& map) {
    std::error_code ec;
    fs::create_directories(cacheJson.parent_path(), ec);
    std::ofstream f(cacheJson, std::ios::binary | std::ios::trunc);
    if (!f) return;
    f << "{\"files\":[";
    bool first = true;
    for (const auto& [p, e] : map) {
        f << (first ? "" : ",") << "\n{\"p\":\"" << jsonEsc(p) << "\",\"s\":" << e.size
          << ",\"mt\":" << e.mtime << ",\"h\":\"" << hex16(e.hash) << "\"}";
        first = false;
    }
    f << "\n]}\n";
}

// --- Manifest -----------------------------------------------------------------

struct FileEntry {
    std::string path;   // forward-slash relative path
    uint64_t size = 0;
    uint64_t hash = 0;
    int mem = -1;  // >= 0: index into HostConfig::modelFiles (serve from memory)
};

// Directories/files that never travel: build outputs, git, bakes, undo
// history. Model files (.tyra / objects/ / *.heights) come from memory.
bool hostSkipsPath(const std::string& rel) {
    auto pre = [&](const char* p) { return rel.rfind(p, 0) == 0; };
    if (pre("bin/") || pre("obj/") || pre(".git/") || pre(".res-baked/")) return true;
    if (rel.size() > 8 && rel.compare(rel.size() - 8, 8, ".history") == 0) return true;
    return false;
}

constexpr size_t kChunkSize = 256 * 1024;
constexpr size_t kPeerBacklogCap = 2 * 1024 * 1024;
constexpr double kPingIdleSec = 5.0;
constexpr double kPeerTimeoutSec = 15.0;

}  // namespace

std::string defaultCacheRoot() {
    const char* base = getenv("LOCALAPPDATA");
    if (!base || !*base) base = getenv("USERPROFILE");
    if (!base || !*base) return "";
    return (fs::path(base) / "tyra-editor" / "remote-cache").string();
}

std::string newJoinCode() {
    static std::mt19937 rng(std::random_device{}());
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%06u", (unsigned)(rng() % 1000000u));
    return buf;
}

// --- Worker context -------------------------------------------------------------

// Everything one worker run needs. Methods lock the owning Session's mutex
// only for the shared UI-visible bits (events, peers, progress, error).
struct WorkerCtx {
    Session& s;
    std::unique_ptr<wire::Transport> transport = wire::makeTcpTransport();

    // shared push helpers -------------------------------------------------
    void setState(Session::State st) { s.state_.store(st); }
    bool stopping() const { return s.stopRequested_.load(); }
    void pushEvent(AppEvent e) {
        std::lock_guard<std::mutex> lk(s.mutex_);
        s.events_.push_back(std::move(e));
    }
    void publishPeers(const std::vector<PeerView>& p) {
        std::lock_guard<std::mutex> lk(s.mutex_);
        s.peers_ = p;
    }
    void setError(const std::string& text) {
        {
            std::lock_guard<std::mutex> lk(s.mutex_);
            s.errorText_ = text;
        }
        s.state_.store(Session::State::Error);
        AppEvent e;
        e.type = AppEvent::Type::Ended;
        e.text = text;
        pushEvent(std::move(e));
    }
    void setProgress(const Session::SyncProgress& p) {
        std::lock_guard<std::mutex> lk(s.mutex_);
        s.progress_ = p;
    }
    std::vector<Session::Cmd> drainCmds() {
        std::lock_guard<std::mutex> lk(s.cmdMutex_);
        std::vector<Session::Cmd> out(s.cmds_.begin(), s.cmds_.end());
        s.cmds_.clear();
        return out;
    }
    bool sendJson(wire::PeerId peer, const std::string& json,
                  const std::string& bin = "") {
        wire::Frame f;
        f.json = json;
        f.bin = bin;
        return transport->send(peer, f);
    }
};

// --- Host run --------------------------------------------------------------------

namespace {

struct HostPeer {
    bool helloDone = false;
    std::string name;
    int colorIdx = 0;
    std::string address;
    double lastRecv = 0, lastSend = 0;
    std::deque<int> sendQueue;  // manifest indices still to stream
    int streaming = -1;         // manifest index currently chunked, -1 = none
    uint64_t streamOff = 0;
    std::ifstream stream;       // open disk file for `streaming`
    bool syncDonePending = false;
};

struct HostRun {
    WorkerCtx& ctx;
    const HostConfig& cfg;
    std::vector<FileEntry> manifest;
    std::string manifestJson;  // pre-serialized (same for every joiner)
    std::map<wire::PeerId, HostPeer> peers;

    std::string buildManifest() {
        // Model files from memory (the live model wins over disk).
        std::unordered_set<std::string> modelPaths;
        for (size_t i = 0; i < cfg.modelFiles.size(); ++i) {
            const auto& vf = cfg.modelFiles[i];
            FileEntry e;
            e.path = vf.relativePath;
            e.size = vf.content.size();
            e.hash = wire::fnv1a64(vf.content.data(), vf.content.size());
            e.mem = (int)i;
            manifest.push_back(std::move(e));
            modelPaths.insert(vf.relativePath);
        }
        // Everything else from disk, content-hash memoized across sessions.
        HashMemo memo = loadHashMemo();
        bool memoDirty = false;
        std::error_code ec;
        const fs::path root = fs::path(cfg.projectDir);
        for (fs::recursive_directory_iterator it(root, ec), end; it != end;
             it.increment(ec)) {
            if (ec) return "cannot scan project directory: " + ec.message();
            if (!it->is_regular_file(ec)) continue;
            std::string rel = fs::relative(it->path(), root, ec).generic_string();
            if (ec || rel.empty()) continue;
            if (hostSkipsPath(rel) || modelPaths.count(rel)) continue;
            // Model-file shapes always come from memory even if the manifest
            // snapshot happens to miss them (defensive: stale disk copies of a
            // renamed scene's heights would otherwise resurrect).
            if (rel.rfind("objects/", 0) == 0) continue;
            if (rel.find('/') == std::string::npos) {
                if (rel.size() > 5 && rel.compare(rel.size() - 5, 5, ".tyra") == 0)
                    continue;
                if (rel.rfind("terrain-", 0) == 0 && rel.size() > 8 &&
                    rel.compare(rel.size() - 8, 8, ".heights") == 0)
                    continue;
            }
            const std::string abs = it->path().string();
            const uint64_t size = (uint64_t)fs::file_size(it->path(), ec);
            const int64_t mtime = mtimeTicks(it->path());
            FileEntry e;
            e.path = rel;
            e.size = size;
            auto mi = memo.find(abs);
            if (mi != memo.end() && mi->second.size == size &&
                mi->second.mtime == mtime) {
                e.hash = mi->second.hash;
            } else {
                uint64_t h = 0, sz = 0;
                if (!wire::hashFile(abs, h, sz)) continue;  // vanished mid-scan
                e.hash = h;
                e.size = sz;
                memo[abs] = {sz, mtime, h};
                memoDirty = true;
            }
            manifest.push_back(std::move(e));
        }
        if (memoDirty) saveHashMemo(memo);

        std::ostringstream mj;
        mj << "{\"t\":\"manifest\",\"files\":[";
        for (size_t i = 0; i < manifest.size(); ++i) {
            const auto& e = manifest[i];
            mj << (i ? "," : "") << "{\"p\":\"" << jsonEsc(e.path) << "\",\"s\":" << e.size
               << ",\"h\":\"" << hex16(e.hash) << "\"}";
        }
        mj << "]}";
        manifestJson = mj.str();
        return "";
    }

    std::vector<PeerView> peerViews() {
        std::vector<PeerView> out;
        out.push_back({0, cfg.displayName, 0, ""});
        for (const auto& [id, p] : peers)
            if (p.helloDone) out.push_back({(int)id, p.name, p.colorIdx, p.address});
        return out;
    }

    std::string peersJson() {
        std::ostringstream js;
        js << "[";
        bool first = true;
        for (const PeerView& pv : peerViews()) {
            js << (first ? "" : ",") << "{\"id\":" << pv.id << ",\"name\":\""
               << jsonEsc(pv.name) << "\",\"colorIdx\":" << pv.colorIdx << "}";
            first = false;
        }
        js << "]";
        return js.str();
    }

    int nextColorIdx() {
        for (int c = 1; c < kMaxPeers; ++c) {
            bool taken = false;
            for (const auto& [id, p] : peers) taken |= (p.helloDone && p.colorIdx == c);
            if (!taken) return c;
        }
        return 1;
    }

    void broadcast(const std::string& json, const std::string& bin = "",
                   wire::PeerId except = -1) {
        for (auto& [id, p] : peers) {
            if (!p.helloDone || id == except) continue;
            ctx.sendJson(id, json, bin);
            p.lastSend = nowSec();
        }
    }

    void announceLeave(wire::PeerId id, const HostPeer& p, const std::string& reason) {
        AppEvent e;
        e.type = AppEvent::Type::PeerLeft;
        e.peer = {(int)id, p.name, p.colorIdx, p.address};
        e.text = reason;
        ctx.pushEvent(std::move(e));
        broadcast("{\"t\":\"peer-leave\",\"id\":" + std::to_string(id) +
                  ",\"reason\":\"" + reason + "\"}");
    }

    void handleHello(wire::PeerId id, HostPeer& p, const json::Value& msg) {
        const int proto = (int)intField(msg, "proto", 0);
        if (proto != kProtoVersion) {
            ctx.sendJson(id, "{\"t\":\"deny\",\"reason\":\"proto\",\"hostProto\":" +
                                 std::to_string(kProtoVersion) + "}");
            transportKick(id);
            return;
        }
        if (strField(msg, "code") != cfg.joinCode) {
            ctx.sendJson(id, "{\"t\":\"deny\",\"reason\":\"code\"}");
            transportKick(id);
            return;
        }
        int liveCount = 1;  // the host
        for (const auto& [pid, pp] : peers) liveCount += pp.helloDone ? 1 : 0;
        if (liveCount >= kMaxPeers) {
            ctx.sendJson(id, "{\"t\":\"deny\",\"reason\":\"full\"}");
            transportKick(id);
            return;
        }
        p.helloDone = true;
        p.name = strField(msg, "name", "peer");
        if (p.name.empty()) p.name = "peer";
        p.colorIdx = nextColorIdx();
        ctx.sendJson(id, "{\"t\":\"welcome\",\"proto\":" + std::to_string(kProtoVersion) +
                             ",\"projectId\":\"" + jsonEsc(cfg.projectId) +
                             "\",\"projectName\":\"" + jsonEsc(cfg.projectName) +
                             "\",\"you\":" + std::to_string(id) +
                             ",\"colorIdx\":" + std::to_string(p.colorIdx) +
                             ",\"peers\":" + peersJson() + "}");
        ctx.sendJson(id, manifestJson);
        p.lastSend = nowSec();
        broadcast("{\"t\":\"peer-join\",\"id\":" + std::to_string(id) + ",\"name\":\"" +
                      jsonEsc(p.name) + "\",\"colorIdx\":" + std::to_string(p.colorIdx) +
                      "}",
                  "", id);
        AppEvent e;
        e.type = AppEvent::Type::PeerJoined;
        e.peer = {(int)id, p.name, p.colorIdx, p.address};
        ctx.pushEvent(std::move(e));
        ctx.publishPeers(peerViews());
    }

    void handleNeed(wire::PeerId id, HostPeer& p, const json::Value& msg) {
        p.sendQueue.clear();
        if (const auto* paths = msg.find("paths");
            paths && paths->type == json::Value::Type::Array) {
            std::unordered_map<std::string, int> index;
            for (size_t i = 0; i < manifest.size(); ++i) index[manifest[i].path] = (int)i;
            for (const auto& jp : paths->arr) {
                auto it = index.find(jp.stringOr(""));
                if (it != index.end()) p.sendQueue.push_back(it->second);
            }
        }
        p.syncDonePending = true;
    }

    // Streams queued file chunks into the peer's send buffer, respecting the
    // backlog cap so one slow client cannot balloon host memory.
    void pumpPeer(wire::PeerId id, HostPeer& p) {
        if (!p.helloDone) return;
        while (p.syncDonePending &&
               ctx.transport->sendBacklog(id) < kPeerBacklogCap) {
            if (p.streaming < 0) {
                if (p.sendQueue.empty()) {
                    ctx.sendJson(id, "{\"t\":\"sync-done\"}");
                    p.lastSend = nowSec();
                    p.syncDonePending = false;
                    return;
                }
                p.streaming = p.sendQueue.front();
                p.sendQueue.pop_front();
                p.streamOff = 0;
                const FileEntry& e = manifest[p.streaming];
                if (e.mem < 0) {
                    p.stream.close();
                    p.stream.clear();
                    p.stream.open(joinRel(cfg.projectDir, e.path), std::ios::binary);
                    if (!p.stream) {  // vanished since the scan - skip it
                        p.streaming = -1;
                        continue;
                    }
                }
            }
            const FileEntry& e = manifest[p.streaming];
            std::string bin;
            if (e.mem >= 0) {
                const std::string& src = cfg.modelFiles[e.mem].content;
                const size_t n = std::min(kChunkSize, src.size() - (size_t)p.streamOff);
                bin.assign(src, (size_t)p.streamOff, n);
            } else {
                bin.resize(std::min<uint64_t>(kChunkSize, e.size - p.streamOff));
                if (!bin.empty()) {
                    p.stream.read(bin.data(), (std::streamsize)bin.size());
                    if ((size_t)p.stream.gcount() != bin.size()) {  // truncated on disk
                        p.streaming = -1;
                        p.stream.close();
                        continue;
                    }
                }
            }
            const uint64_t off = p.streamOff;
            p.streamOff += bin.size();
            const bool last = p.streamOff >= e.size;
            ctx.sendJson(id,
                         "{\"t\":\"file\",\"p\":\"" + jsonEsc(e.path) +
                             "\",\"off\":" + std::to_string(off) +
                             (last ? ",\"last\":true}" : "}"),
                         bin);
            p.lastSend = nowSec();
            if (last) {
                p.streaming = -1;
                p.stream.close();
            }
        }
    }

    void transportKick(wire::PeerId id) {
        ctx.transport->kick(id);
        peers.erase(id);
    }

    void run() {
        if (auto err = buildManifest(); !err.empty()) {
            ctx.setError(err);
            return;
        }
        if (auto err = ctx.transport->listen(cfg.port); !err.empty()) {
            ctx.setError(err);
            return;
        }
        ctx.setState(Session::State::Listening);
        ctx.publishPeers(peerViews());

        std::vector<wire::Event> events;
        while (!ctx.stopping()) {
            for (auto& cmd : ctx.drainCmds()) {
                if (cmd.type == Session::Cmd::Type::Kick) {
                    auto it = peers.find(cmd.peer);
                    if (it != peers.end()) {
                        ctx.sendJson(cmd.peer, "{\"t\":\"bye\",\"reason\":\"kicked\"}");
                        HostPeer left = std::move(it->second);
                        transportKick(cmd.peer);
                        announceLeave(cmd.peer, left, "kicked");
                        ctx.publishPeers(peerViews());
                    }
                } else if (cmd.type == Session::Cmd::Type::Broadcast) {
                    broadcast(cmd.frame.json, cmd.frame.bin, cmd.peer);
                }
            }

            events.clear();
            ctx.transport->poll(events, 50);
            const double now = nowSec();
            for (auto& ev : events) {
                switch (ev.type) {
                    case wire::Event::Type::Connected: {
                        HostPeer p;
                        p.address = ev.info;
                        p.lastRecv = p.lastSend = now;
                        peers[ev.peer] = std::move(p);
                        break;
                    }
                    case wire::Event::Type::Disconnected: {
                        auto it = peers.find(ev.peer);
                        if (it == peers.end()) break;
                        HostPeer left = std::move(it->second);
                        peers.erase(it);
                        if (left.helloDone) {
                            announceLeave(ev.peer, left, "left");
                            ctx.publishPeers(peerViews());
                        }
                        break;
                    }
                    case wire::Event::Type::Frame: {
                        auto it = peers.find(ev.peer);
                        if (it == peers.end()) break;
                        HostPeer& p = it->second;
                        p.lastRecv = now;
                        json::Value msg;
                        if (!json::parse(ev.frame.json, msg)) break;
                        const std::string t = strField(msg, "t");
                        if (!p.helloDone) {
                            if (t == "hello") handleHello(ev.peer, p, msg);
                            else transportKick(ev.peer);  // protocol violation
                        } else if (t == "need") {
                            handleNeed(ev.peer, p, msg);
                        } else if (t == "ping") {
                            ctx.sendJson(ev.peer, "{\"t\":\"pong\"}");
                            p.lastSend = now;
                        } else if (t == "pong") {
                            // lastRecv already refreshed
                        } else {
                            // Application frame (edit/presence) - main thread.
                            AppEvent e;
                            e.type = AppEvent::Type::Frame;
                            e.peer = {(int)ev.peer, p.name, p.colorIdx, p.address};
                            e.frame = std::move(ev.frame);
                            ctx.pushEvent(std::move(e));
                        }
                        break;
                    }
                }
            }

            // File streaming + keepalive.
            std::vector<wire::PeerId> timedOut;
            for (auto& [id, p] : peers) {
                pumpPeer(id, p);
                if (now - p.lastRecv > kPeerTimeoutSec) {
                    timedOut.push_back(id);
                } else if (p.helloDone && now - p.lastSend > kPingIdleSec) {
                    ctx.sendJson(id, "{\"t\":\"ping\"}");
                    p.lastSend = now;
                }
            }
            for (wire::PeerId id : timedOut) {
                auto it = peers.find(id);
                if (it == peers.end()) continue;
                HostPeer left = std::move(it->second);
                transportKick(id);
                if (left.helloDone) {
                    announceLeave(id, left, "timeout");
                    ctx.publishPeers(peerViews());
                }
            }
        }

        broadcast("{\"t\":\"bye\",\"reason\":\"closed\"}");
        ctx.transport->close();
    }
};

// --- Client run --------------------------------------------------------------------

struct ClientRun {
    WorkerCtx& ctx;
    const JoinConfig& cfg;

    std::string projectId, projectName;
    int myId = -1, myColorIdx = 0;
    std::vector<PeerView> peers;  // includes the host (id 0)

    fs::path cacheDir;     // <cacheRoot>/<projectId>
    fs::path projectRoot;  // <cacheDir>/project
    CacheMap cacheMap;
    std::unordered_map<std::string, FileEntry> manifestByPath;

    Session::SyncProgress prog;
    std::string curPath;   // file currently being received
    std::ofstream curFile;
    uint64_t curOff = 0;

    double lastRecv = 0, lastSend = 0;
    bool live = false;

    void publish() { ctx.publishPeers(peers); }

    bool handleWelcome(const json::Value& msg, std::string& err) {
        if ((int)intField(msg, "proto", 0) != kProtoVersion) {
            err = "editor versions differ (protocol mismatch)";
            return false;
        }
        projectId = strField(msg, "projectId");
        projectName = strField(msg, "projectName", "project");
        myId = (int)intField(msg, "you", -1);
        myColorIdx = (int)intField(msg, "colorIdx", 1);
        if (projectId.empty() || myId < 0) {
            err = "malformed welcome from host";
            return false;
        }
        peers.clear();
        if (const auto* jp = msg.find("peers");
            jp && jp->type == json::Value::Type::Array) {
            for (const auto& p : jp->arr)
                peers.push_back({(int)intField(p, "id", -1), strField(p, "name"),
                                 (int)intField(p, "colorIdx", 0), ""});
        }
        peers.push_back({myId, cfg.displayName, myColorIdx, ""});
        publish();

        const std::string root =
            cfg.cacheRoot.empty() ? defaultCacheRoot() : cfg.cacheRoot;
        if (root.empty()) {
            err = "no cache directory available";
            return false;
        }
        cacheDir = fs::path(root) / projectId;
        projectRoot = cacheDir / "project";
        std::error_code ec;
        fs::create_directories(projectRoot, ec);
        if (ec) {
            err = "cannot create cache directory: " + cacheDir.string();
            return false;
        }
        cacheMap = loadCacheMap(cacheDir / "cache.json");
        ctx.setState(Session::State::Syncing);
        return true;
    }

    bool handleManifest(const json::Value& msg, std::string& err) {
        const auto* files = msg.find("files");
        if (!files || files->type != json::Value::Type::Array) {
            err = "malformed manifest from host";
            return false;
        }
        std::vector<std::string> need;
        prog = Session::SyncProgress{};
        std::error_code ec;
        for (const auto& jf : files->arr) {
            FileEntry e;
            e.path = strField(jf, "p");
            e.size = (uint64_t)intField(jf, "s");
            e.hash = strtoull(strField(jf, "h").c_str(), nullptr, 16);
            if (!safeRelPath(e.path)) {
                err = "host sent an unsafe path: " + e.path;
                return false;
            }
            manifestByPath[e.path] = e;

            bool have = false;
            auto ci = cacheMap.find(e.path);
            if (ci != cacheMap.end() && ci->second.hash == e.hash &&
                ci->second.size == e.size) {
                const fs::path abs = joinRel(projectRoot, e.path);
                if (fs::exists(abs, ec) && (uint64_t)fs::file_size(abs, ec) == e.size) {
                    // mtime unchanged since we wrote it = trust the recorded
                    // hash; a locally modified file gets rehashed.
                    if (mtimeTicks(abs) == ci->second.mtime) {
                        have = true;
                    } else {
                        uint64_t h = 0, sz = 0;
                        have = wire::hashFile(abs.string(), h, sz) && h == e.hash &&
                               sz == e.size;
                        if (have) ci->second.mtime = mtimeTicks(abs);
                    }
                }
            }
            if (!have) {
                need.push_back(e.path);
                prog.bytesTotal += e.size;
            }
        }
        // Remove files this cache materialized that the host no longer has.
        // Never touches paths cache.json doesn't know (local builds etc.).
        for (auto it = cacheMap.begin(); it != cacheMap.end();) {
            if (manifestByPath.count(it->first)) {
                ++it;
                continue;
            }
            fs::remove(joinRel(projectRoot, it->first), ec);
            it = cacheMap.erase(it);
        }
        prog.filesTotal = (int)need.size();
        setProg();

        std::ostringstream nj;
        nj << "{\"t\":\"need\",\"paths\":[";
        for (size_t i = 0; i < need.size(); ++i)
            nj << (i ? "," : "") << "\"" << jsonEsc(need[i]) << "\"";
        nj << "]}";
        ctx.sendJson(wire::kHostPeer, nj.str());
        lastSend = nowSec();
        return true;
    }

    bool handleFile(const json::Value& msg, const std::string& bin, std::string& err) {
        const std::string p = strField(msg, "p");
        const uint64_t off = (uint64_t)intField(msg, "off", 0);
        auto mi = manifestByPath.find(p);
        if (!safeRelPath(p) || mi == manifestByPath.end()) {
            err = "host sent an unexpected file: " + p;
            return false;
        }
        const fs::path abs = joinRel(projectRoot, p);
        const fs::path part = fs::path(abs.string() + ".part");
        if (p != curPath) {
            if (curFile.is_open()) {  // previous file never finished
                err = "transfer interleaved unexpectedly";
                return false;
            }
            std::error_code ec;
            fs::create_directories(abs.parent_path(), ec);
            curFile.open(part, std::ios::binary | std::ios::trunc);
            if (!curFile) {
                err = "cannot write " + part.string();
                return false;
            }
            curPath = p;
            curOff = 0;
            prog.currentFile = p;
        }
        if (off != curOff) {
            err = "transfer corrupted (offset mismatch on " + p + ")";
            return false;
        }
        if (!bin.empty()) curFile.write(bin.data(), (std::streamsize)bin.size());
        curOff += bin.size();
        prog.bytesDone += bin.size();
        if (boolField(msg, "last", false)) {
            curFile.close();
            if (curOff != mi->second.size) {
                err = "transfer corrupted (size mismatch on " + p + ")";
                return false;
            }
            std::error_code ec;
            fs::remove(abs, ec);
            fs::rename(part, abs, ec);
            if (ec) {
                err = "cannot finalize " + abs.string();
                return false;
            }
            cacheMap[p] = {mi->second.size, mi->second.hash, mtimeTicks(abs)};
            curPath.clear();
            ++prog.filesDone;
        }
        setProg();
        return true;
    }

    void setProg() { ctx.setProgress(prog); }

    void run() {
        ctx.setState(Session::State::Connecting);
        if (auto err = ctx.transport->connect(cfg.address, cfg.port, 4000);
            !err.empty()) {
            ctx.setError(err);
            return;
        }
        ctx.sendJson(wire::kHostPeer,
                     "{\"t\":\"hello\",\"proto\":" + std::to_string(kProtoVersion) +
                         ",\"name\":\"" + jsonEsc(cfg.displayName) + "\",\"code\":\"" +
                         jsonEsc(cfg.joinCode) + "\"}");
        lastRecv = lastSend = nowSec();

        std::vector<wire::Event> events;
        while (!ctx.stopping()) {
            for (auto& cmd : ctx.drainCmds()) {
                if (cmd.type == Session::Cmd::Type::ToHost && live) {
                    ctx.transport->send(wire::kHostPeer, cmd.frame);
                    lastSend = nowSec();
                }
            }
            events.clear();
            ctx.transport->poll(events, 50);
            const double now = nowSec();
            for (auto& ev : events) {
                if (ev.type == wire::Event::Type::Disconnected) {
                    ctx.setError("Connection to the host was lost.");
                    ctx.transport->close();
                    return;
                }
                if (ev.type != wire::Event::Type::Frame) continue;
                lastRecv = now;
                json::Value msg;
                if (!json::parse(ev.frame.json, msg)) continue;
                const std::string t = strField(msg, "t");
                std::string err;
                if (t == "welcome") {
                    if (!handleWelcome(msg, err)) {
                        ctx.setError(err);
                        ctx.transport->close();
                        return;
                    }
                } else if (t == "deny") {
                    const std::string reason = strField(msg, "reason");
                    ctx.setError(reason == "code"    ? "The host rejected the join code."
                                 : reason == "proto" ? "Editor versions differ - update "
                                                       "one side and retry."
                                 : reason == "full"  ? "The session is full."
                                                     : "The host denied the connection.");
                    ctx.transport->close();
                    return;
                } else if (t == "manifest") {
                    if (!handleManifest(msg, err)) {
                        ctx.setError(err);
                        ctx.transport->close();
                        return;
                    }
                } else if (t == "file") {
                    if (!handleFile(msg, ev.frame.bin, err)) {
                        ctx.setError(err);
                        ctx.transport->close();
                        return;
                    }
                } else if (t == "sync-done") {
                    saveCacheMap(cacheDir / "cache.json", cacheMap);
                    live = true;
                    ctx.setState(Session::State::Live);
                    AppEvent e;
                    e.type = AppEvent::Type::SyncDone;
                    e.text = projectRoot.string();
                    ctx.pushEvent(std::move(e));
                } else if (t == "peer-join") {
                    PeerView pv{(int)intField(msg, "id", -1), strField(msg, "name"),
                                (int)intField(msg, "colorIdx", 0), ""};
                    peers.push_back(pv);
                    publish();
                    AppEvent e;
                    e.type = AppEvent::Type::PeerJoined;
                    e.peer = pv;
                    ctx.pushEvent(std::move(e));
                } else if (t == "peer-leave") {
                    const int id = (int)intField(msg, "id", -1);
                    PeerView pv;
                    for (auto it = peers.begin(); it != peers.end(); ++it)
                        if (it->id == id) {
                            pv = *it;
                            peers.erase(it);
                            break;
                        }
                    publish();
                    AppEvent e;
                    e.type = AppEvent::Type::PeerLeft;
                    e.peer = pv;
                    e.text = strField(msg, "reason", "left");
                    ctx.pushEvent(std::move(e));
                } else if (t == "bye") {
                    const std::string reason = strField(msg, "reason");
                    ctx.setError(reason == "kicked"
                                     ? "You were removed from the session by the host."
                                     : "The host closed the session.");
                    ctx.transport->close();
                    return;
                } else if (t == "ping") {
                    ctx.sendJson(wire::kHostPeer, "{\"t\":\"pong\"}");
                    lastSend = now;
                } else if (t == "pong") {
                    // lastRecv already refreshed
                } else {
                    AppEvent e;
                    e.type = AppEvent::Type::Frame;
                    e.peer = {0, "host", 0, ""};
                    e.frame = std::move(ev.frame);
                    ctx.pushEvent(std::move(e));
                }
            }
            if (now - lastRecv > kPeerTimeoutSec) {
                ctx.setError("Connection to the host timed out.");
                ctx.transport->close();
                return;
            }
            if (now - lastSend > kPingIdleSec) {
                ctx.sendJson(wire::kHostPeer, "{\"t\":\"ping\"}");
                lastSend = now;
            }
        }
        ctx.transport->close();
    }
};

}  // namespace

// --- Session public API ---------------------------------------------------------

Session::~Session() { close(); }

void Session::startWorker(Role role) {
    close();
    stopRequested_.store(false);
    role_.store(role);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        errorText_.clear();
        peers_.clear();
        progress_ = SyncProgress{};
        events_.clear();
    }
    {
        std::lock_guard<std::mutex> lk(cmdMutex_);
        cmds_.clear();
    }
    state_.store(State::Starting);
    thread_ = std::thread([this] { workerMain(); });
}

void Session::host(HostConfig cfg) {
    hostCfg_ = std::move(cfg);
    startWorker(Role::Host);
}

void Session::join(JoinConfig cfg) {
    joinCfg_ = std::move(cfg);
    startWorker(Role::Client);
}

void Session::workerMain() {
    WorkerCtx ctx{*this};
    if (role_.load() == Role::Host) {
        HostRun run{ctx, hostCfg_};
        run.run();
    } else {
        ClientRun run{ctx, joinCfg_};
        run.run();
    }
    // A run that ended on its own (error/bye) keeps its Error state + events;
    // a stop-requested run winds down to Idle in close().
}

void Session::kickPeer(int peerId) {
    pushCmd({Cmd::Type::Kick, peerId, {}});
}

void Session::broadcastFrame(const wire::Frame& f, int exceptPeer) {
    if (role_.load() != Role::Host) return;
    pushCmd({Cmd::Type::Broadcast, exceptPeer, f});
}

void Session::sendFrameToHost(const wire::Frame& f) {
    if (role_.load() != Role::Client) return;
    pushCmd({Cmd::Type::ToHost, -1, f});
}

void Session::pushCmd(Cmd c) {
    std::lock_guard<std::mutex> lk(cmdMutex_);
    cmds_.push_back(std::move(c));
}

void Session::close() {
    stopRequested_.store(true);
    if (thread_.joinable()) thread_.join();
    role_.store(Role::None);
    state_.store(State::Idle);
    std::lock_guard<std::mutex> lk(mutex_);
    peers_.clear();
    progress_ = SyncProgress{};
}

std::string Session::errorText() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return errorText_;
}

std::vector<PeerView> Session::peers() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return peers_;
}

Session::SyncProgress Session::syncProgress() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return progress_;
}

std::vector<AppEvent> Session::drainEvents() {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<AppEvent> out(std::make_move_iterator(events_.begin()),
                              std::make_move_iterator(events_.end()));
    events_.clear();
    return out;
}

}  // namespace session
