#pragma once

// Live collaboration session (docs/collaboration.md): one editor HOSTS its
// open project, others JOIN over the LAN, everyone edits, the host is
// authoritative and the only one who saves/commits. This class owns the
// network side on a worker thread (Runner idiom: atomics + mutex-guarded
// data, the UI thread polls every frame); it never touches Project state -
// the main thread drains drainEvents() in App::sessionTick() and applies
// everything there.
//
// Phase map: host/join handshake + full project transfer with a content-hash
// cache live here; the live edit sync (diff/apply engine) is layered on top
// via sendFrameToHost()/broadcastFrame() and AppEvent::Frame.

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "project.hpp"
#include "wire.hpp"

namespace session {

// Bumped on any incompatible protocol/message change. Mismatch = deny at
// hello; both sides show "editor versions differ".
constexpr int kProtoVersion = 1;
constexpr int kMaxPeers = 8;  // host + 7 clients; also the peer-color count

// What the UI shows about one participant. id 0 is always the host.
struct PeerView {
    int id = 0;
    std::string name;
    int colorIdx = 0;      // stable per session; index into the peer palette
    std::string address;   // host side only; "" on clients
};

// Events surfaced to the main thread (drained once per frame).
struct AppEvent {
    enum class Type {
        PeerJoined,   // peer
        PeerLeft,     // peer; text = "left" | "kicked" | "timeout"
        SyncDone,     // client: text = the materialized project directory
        Ended,        // session is over; text = human-readable reason
        Frame,        // an application frame (edit/presence); peer.id = origin
    };
    Type type = Type::Ended;
    PeerView peer;
    std::string text;
    wire::Frame frame;
};

struct HostConfig {
    uint16_t port = 7797;
    std::string joinCode;     // 6 digits shown by the host UI, checked at hello
    std::string displayName;
    std::string projectDir;   // asset/source files are served from here
    std::string projectId;    // Project::projectId - the client cache key
    std::string projectName;
    // Byte images of the LIVE model (project::manifestFiles) - a dirty host
    // ships its in-memory state, which supersedes the same paths on disk.
    std::vector<project::VirtualFile> modelFiles;
};

struct JoinConfig {
    std::string address;
    uint16_t port = 7797;
    std::string joinCode;
    std::string displayName;
    // Remote projects materialize under <cacheRoot>/<projectId>/project.
    // "" = %LOCALAPPDATA%\tyra-editor\remote-cache.
    std::string cacheRoot;
};

class Session {
  public:
    enum class Role { None, Host, Client };
    // Starting: host is hashing/scanning the project before it can listen.
    // Listening: hosting (live for edits, 0+ peers). Connecting/Syncing/Live:
    // the client-side join pipeline. Error: dead, errorText() says why.
    enum class State { Idle, Starting, Listening, Connecting, Syncing, Live, Error };

    Session() = default;
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    void host(HostConfig cfg);
    void join(JoinConfig cfg);

    // Host: disconnect one peer (sends bye{kicked}).
    void kickPeer(int peerId);

    // Ends the session: hosting broadcasts bye{closed}, a client just leaves.
    // Blocks until the worker thread is joined. Safe to call when idle.
    void close();

    Role role() const { return role_.load(); }
    State state() const { return state_.load(); }
    bool active() const {
        const State s = state_.load();
        return s != State::Idle && s != State::Error;
    }

    std::string errorText() const;        // last fatal error (Error state)
    std::vector<PeerView> peers() const;  // everyone incl. the host entry (id 0)

    // Client sync progress (bytes/files still to fetch vs fetched).
    struct SyncProgress {
        uint64_t bytesDone = 0, bytesTotal = 0;
        int filesDone = 0, filesTotal = 0;
        std::string currentFile;
    };
    SyncProgress syncProgress() const;

    std::vector<AppEvent> drainEvents();

    // Edit/presence plumbing for the sync layer. Host: send one frame to every
    // Live peer (optionally excluding one). Client: send one frame to the host.
    // No-ops when the session is not up.
    void broadcastFrame(const wire::Frame& f, int exceptPeer = -1);
    void sendFrameToHost(const wire::Frame& f);

    // Main-thread -> worker commands (public only so the worker-side helpers
    // in session.cpp can name the type; not part of the App-facing API).
    struct Cmd {
        enum class Type { Kick, Broadcast, ToHost };
        Type type = Type::Kick;
        int peer = -1;        // Kick: target; Broadcast: excluded peer or -1
        wire::Frame frame;    // Broadcast / ToHost
    };

  private:
    void startWorker(Role role);
    void pushCmd(Cmd c);
    void workerMain();

    std::atomic<Role> role_{Role::None};
    std::atomic<State> state_{State::Idle};
    std::atomic<bool> stopRequested_{false};

    mutable std::mutex mutex_;  // guards everything below
    std::string errorText_;
    std::vector<PeerView> peers_;
    SyncProgress progress_;
    std::deque<AppEvent> events_;

    std::mutex cmdMutex_;
    std::deque<Cmd> cmds_;

    HostConfig hostCfg_;
    JoinConfig joinCfg_;
    std::thread thread_;

    friend struct WorkerCtx;
};

// The default client-side cache root: %LOCALAPPDATA%\tyra-editor\remote-cache.
std::string defaultCacheRoot();

// A fresh 6-digit join code.
std::string newJoinCode();

}  // namespace session
