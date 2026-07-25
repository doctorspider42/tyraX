#pragma once

// Live phone camera link (docs/phone-camera.md): the editor HOSTS a session on
// the LAN, the companion iOS app joins (a separate public repo,
// github.com/doctorspider42/tyrax-cam - its own toolchain and release cycle),
// and from then on the phone IS a viewfinder - it shows a live JPEG stream of the editor's chosen
// camera and its 6DoF ARKit pose drives that camera. The Cutscene Director can
// record the move straight into camera keyframes while you watch the framing on
// the phone.
//
// This is the second acquisition source for camtake.hpp: a pose stream instead
// of a file. Everything downstream (mapping into the scene, keyframe baking) is
// the file importer's, unchanged - see the acquisition/bake split there.
//
// Threading follows the Runner/Session idiom: one worker thread owns the
// transport and does the JPEG encoding, the UI thread polls drainEvents() /
// drainPoses() once per frame and is the ONLY place link data meets the
// Project or ImGui. Sockets are never touched directly - the link speaks
// wire::Frames over wire::makeWebSocketTransport(), so the phone needs nothing
// but the WebSocket built into React Native (and a plain browser works too).

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "camtake.hpp"

namespace phonecam {

// Bumped on any incompatible message change; a mismatched app is denied at
// hello with a message naming both versions.
constexpr int kProtoVersion = 1;
constexpr uint16_t kDefaultPort = 7798;

// Ring capacity of the undrained pose feed. At 60 Hz this is ~34 s of motion -
// far more than a frame's worth, so the UI thread can stall (a Docker build,
// a modal) without the stream falling behind; older samples then drop.
constexpr size_t kMaxPendingPoses = 2048;

// What the phone told us about itself at hello. Shown in the Phone Camera
// window so it is obvious which device is driving the view.
struct DeviceInfo {
    std::string name;     // "Pawel's iPhone"
    std::string model;    // "iPhone15,3"
    std::string client;   // "TyraX Cam 1.0.0", "browser test client"
    std::string address;  // remote ip:port
    // ARKit world tracking (position + rotation). False = orientation only
    // (a browser, or a device without world tracking): the editor then keeps
    // the camera in place and only turns it.
    bool sixDof = false;
};

// Preview stream settings. The editor picks the defaults (Edit > Preferences);
// a connected phone may override them because only it knows its screen and how
// good its Wi-Fi is.
struct PreviewPrefs {
    int maxWidth = 480;   // long-edge cap of the streamed image
    int maxHeight = 480;
    int fps = 15;         // frames per second cap
    int quality = 60;     // JPEG quality, 1..100
};

// Surfaced to the main thread, drained once per frame.
struct Event {
    enum class Type {
        Connected,     // device joined (`device` filled)
        Disconnected,  // device left; text = reason
        Command,       // the phone pressed a button; text = the command below
        MoveStart,     // fly the start point; `vec` = a delta in the camera's
                       // own right/up/forward frame, scene units
        Error,         // the link died; text = why (state() is Error)
    };
    Type type = Type::Error;
    DeviceInfo device;
    std::string text;
    float vec[3] = {0.0f, 0.0f, 0.0f};
};

// Command strings a phone can send (Event::Type::Command). The editor owns
// what they mean - the phone only asks.
//   "record"   start recording into the selected sequence
//   "stop"     stop recording
//   "recenter" re-anchor the mapping on the current pose + editor view
constexpr const char* kCmdRecord = "record";
constexpr const char* kCmdStop = "stop";
constexpr const char* kCmdRecenter = "recenter";

struct Config {
    uint16_t port = kDefaultPort;
    // 6 digits, checked at hello. Empty accepts any device on the LAN - handy
    // for a solo desk, wrong for a shared office.
    std::string pairCode;
    std::string projectName;  // echoed to the phone so it can show what it is on
};

// What the phone shows on its own overlay. The editor rebuilds this every time
// something changes and the worker mirrors it to the device.
struct Status {
    bool recording = false;
    float time = 0.0f;      // playhead / recording time, seconds
    int keys = 0;           // keys captured so far
    std::string sequence;   // the sequence being recorded into
    std::string target;     // Camera entity name, "" = free camera shots
    float density = 0.0f;   // keyframes per second
    bool driving = false;   // is the pose actually moving the editor camera?
};

class Link {
  public:
    enum class State { Idle, Listening, Connected, Error };

    Link() = default;
    ~Link();
    Link(const Link&) = delete;
    Link& operator=(const Link&) = delete;

    // Starts listening. Any previous session is closed first. Failure to bind
    // surfaces as State::Error + errorText() (never throws).
    void start(Config cfg);
    // Stops listening and disconnects the device. Blocks until the worker is
    // joined. Safe when idle.
    void stop();
    // Drops the connected device but keeps listening (the UI's "Disconnect").
    void disconnectDevice();

    State state() const { return state_.load(); }
    bool listening() const {
        const State s = state_.load();
        return s == State::Listening || s == State::Connected;
    }
    bool connected() const { return state_.load() == State::Connected; }
    uint16_t port() const { return port_.load(); }
    std::string errorText() const;
    DeviceInfo device() const;
    std::vector<Event> drainEvents();

    // Poses received since the last call, oldest first. The newest is the live
    // camera; a recording appends the whole batch, so no motion is lost between
    // two UI frames. Timestamps are the phone's own monotonic seconds.
    std::vector<CamTakeSample> drainPoses();
    // Total poses received this session (a "the stream is alive" readout).
    uint64_t poseCount() const { return poseCount_.load(); }

    // The settings the stream currently runs at (editor defaults, possibly
    // overridden by the device).
    PreviewPrefs preview() const;
    void setPreviewDefaults(const PreviewPrefs& p);

    // True when a device is connected and the previous preview frame has been
    // handed to the OS - i.e. grabbing and pushing one now is worth doing. The
    // caller still paces itself by preview().fps.
    bool previewWanted() const { return previewWanted_.load(); }
    // Queues one preview frame: `rgb` is w*h*3 top-down bytes (what
    // Viewport::grabPreviewRgb produces). Copied here, JPEG-encoded on the
    // worker. A frame already waiting is REPLACED - a slow link must cost
    // frame rate, never latency.
    void pushPreview(int w, int h, const unsigned char* rgb);

    // Mirrors the recording state to the phone's overlay. Cheap to call every
    // frame: only a change is sent.
    void setStatus(const Status& s);

  private:
    void workerMain();
    void pushEvent(Event e);

    std::atomic<State> state_{State::Idle};
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> previewWanted_{false};
    std::atomic<uint64_t> poseCount_{0};
    std::atomic<uint16_t> port_{kDefaultPort};

    mutable std::mutex mutex_;  // guards everything below
    Config cfg_;
    std::string errorText_;
    DeviceInfo device_;
    std::deque<Event> events_;
    std::deque<CamTakeSample> poses_;
    PreviewPrefs preview_;
    // Pending preview frame (newest wins) + the status the worker still has to
    // send. `previewSeq_` only exists so the worker can tell a fresh frame from
    // the one it already sent.
    std::vector<unsigned char> previewRgb_;
    int previewW_ = 0, previewH_ = 0;
    bool previewPending_ = false;
    std::string statusJson_;
    bool statusPending_ = false;
    bool dropRequested_ = false;  // UI asked to drop the device (see disconnectDevice)

    std::thread thread_;
};

// A fresh 6-digit pairing code.
std::string newPairCode();

// The browser test client: a self-contained HTML page served on the link's own
// port (open http://<editor-ip>:<port> on any machine). It shows the live
// stream and can send synthetic poses, so the whole pipeline is verifiable
// without an iPhone - which is also how it is tested.
std::string testClientPage();

}  // namespace phonecam
