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

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "project.hpp"
#include "wire.hpp"

namespace session {

// --- Live model diff / apply engine ---------------------------------------------
// The last-broadcast view of the model, kept beside project_. diffModel()
// compares the live project against it and emits one edit frame per changed
// unit (object / scene layout / heightmap / project-wide section); applyEdit()
// folds an inbound frame into BOTH the project and the shadow, so an echo of
// our own edit produces no further diff. All pure (Project& in, frames out) -
// headless-testable, no ImGui / sockets.
struct ModelShadow {
    std::vector<SceneData> scenes;  // per-object + heightmap comparison
    std::string scenesLayout;       // structural (names/meta/membership) compare
    std::array<std::string, project::kSectionCount> sectionBlobs;
};

ModelShadow makeShadow(const Project& p);

// Emits an edit frame per changed unit, in an order safe to apply as a batch
// (object upserts before the scene layout before heights before sections),
// then advances `shadow` to match `p`. No-op when nothing changed.
void diffModel(const Project& p, ModelShadow& shadow,
               const std::function<void(wire::Frame)>& emit);

// Applies one inbound edit frame to `p` and mirrors it into `shadow`. Returns
// false when the frame is not an edit frame (a control message) - the caller
// then ignores it. Bounds-checked; a malformed frame is dropped, never fatal.
bool applyEdit(Project& p, ModelShadow& shadow, const wire::Frame& f);

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

    // Host: replace the model snapshot served to FUTURE joiners, so a peer
    // that connects after the host has edited still gets the current state
    // (already-Live peers track edits through the live sync instead). Called
    // from the main thread whenever the model changes; cheap (model files are
    // the .tyra + objects + heights, not res/).
    void setModelFiles(std::vector<project::VirtualFile> files);

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
    // Latest model snapshot served to new joiners (host). Guarded by mutex_;
    // seeded from hostCfg_ at host(), refreshed by setModelFiles().
    std::vector<project::VirtualFile> hostModelFiles_;
    std::thread thread_;

    friend struct WorkerCtx;
};

// The default client-side cache root: %LOCALAPPDATA%\tyra-editor\remote-cache.
std::string defaultCacheRoot();

// A fresh 6-digit join code.
std::string newJoinCode();

}  // namespace session
