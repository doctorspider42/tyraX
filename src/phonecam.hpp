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
    // How many EXTRA frames may be in flight (0..kMaxSmoothing). 0 is the
    // lowest-latency setting: one frame at a time, and any hitch in encoding or
    // on the Wi-Fi costs a whole frame interval because the next grab window is
    // missed - which reads as stutter even though the average rate is fine.
    // Raising it buys an even cadence at the cost of that many frames of delay
    // (about 1/fps each), which is the trade a viewfinder can usually afford.
    int smoothing = 1;
};
// A deeper queue only adds latency; two extra frames already covers a hiccup.
constexpr int kMaxSmoothing = 3;

// Surfaced to the main thread, drained once per frame.
struct Event {
    enum class Type {
        Connected,     // device joined (`device` filled)
        Disconnected,  // device left; text = reason
        Command,       // the phone pressed a button; text = the command below
        MoveStart,     // fly the start point; `vec` = a delta in the camera's
                       // own right/up/forward frame, scene units
        SelectCamera,  // the phone picked which Camera entity to view from and
                       // record into; text = its name ("" = free shots)
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

// ---------------------------------------------------------------------------
// Body tracking (docs/character-generator.md). The SAME link carries it: one
// server, one port, one pairing code, and the phone says at hello which kind of
// client it is. Two links would mean two codes to type and a fight over 7798.
//
// The skeleton arrives ONCE, when a body-tracking device connects - it is
// ~90 joints of names and a rest pose, and a stream cannot afford to resend it.
// The rest pose is not decoration: retargeting is a delta against the source's
// own bind, so without it a streamed pose cannot move onto another body.

struct BodySkeleton {
    std::vector<std::string> joints;  // ARKit's own names
    std::vector<int> parents;         // index into `joints`, -1 = root
    std::vector<float> restPos;       // joints * 3
    std::vector<float> restRot;       // joints * 4 (x, y, z, w)
    bool valid() const {
        return !joints.empty() && parents.size() == joints.size() &&
               restPos.size() == joints.size() * 3 && restRot.size() == joints.size() * 4;
    }
};

// One frame of the performer. Rotations only, plus where the hips are in the
// phone's world - which is all a retarget consumes.
struct BodyFrame {
    double t = 0.0;                 // the phone's monotonic seconds
    std::vector<float> rot;         // joints * 4, same order as the skeleton
    float hips[3] = {0, 0, 0};
    bool haveHips = false;
    bool tracked = true;            // false = ARKit lost the body this frame
};

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
    // Every Camera entity in the active scene, so the phone can offer the
    // choice itself - picking the viewpoint belongs where the operator is.
    std::vector<std::string> cameras;
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

    // The connected device's skeleton, once it has sent one. Empty (invalid)
    // for a camera-only client, which is how the UI tells them apart.
    BodySkeleton bodySkeleton() const;
    bool hasBodySkeleton() const { return hasBody_.load(); }
    // Body frames received since the last call, oldest first - the newest is
    // the live pose, and a recording appends the whole batch so no motion is
    // lost between two UI frames.
    std::vector<BodyFrame> drainBodyFrames();
    uint64_t bodyFrameCount() const { return bodyCount_.load(); }

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
    // The body-tracking half of the stream, guarded by the same mutex.
    BodySkeleton bodySkel_;
    std::deque<BodyFrame> bodyFrames_;
    std::atomic<bool> hasBody_{false};
    std::atomic<uint64_t> bodyCount_{0};
    PreviewPrefs preview_;
    // Preview frames waiting to be encoded and sent, oldest first. Bounded by
    // PreviewPrefs::smoothing + 1: when full the OLDEST is dropped, so latency
    // stays capped while the worker always has something to send (an empty queue
    // is what produced the stutter - see the smoothing field).
    struct PendingFrame {
        std::vector<unsigned char> rgb;
        int w = 0, h = 0;
    };
    std::deque<PendingFrame> previewQueue_;
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
