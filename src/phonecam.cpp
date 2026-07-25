#include "phonecam.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <utility>

#include <stb_image_write.h>  // implementation lives in menubake.cpp

#include "json.hpp"
#include "wire.hpp"

namespace phonecam {

namespace {

// Stop pushing preview frames when this much is still queued for the socket:
// on a weak Wi-Fi link the right failure is a lower frame rate, not a growing
// backlog the phone sees seconds late.
constexpr size_t kSendBacklogLimit = 512 * 1024;

std::string jsonEscape(const std::string& s) {
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
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)(unsigned char)c);
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    return out;
}

wire::Frame jsonFrame(std::string json) {
    wire::Frame f;
    f.json = std::move(json);
    return f;
}

// Reads a 3- (or 4-) element float array member; false when absent/short.
bool readVec(const json::Value& v, const char* key, float* out, int n) {
    const json::Value* a = v.find(key);
    if (!a || a->type != json::Value::Type::Array || (int)a->arr.size() < n) return false;
    for (int i = 0; i < n; ++i) out[i] = (float)a->arr[i].numberOr(0.0);
    return true;
}

}  // namespace

std::string newPairCode() {
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> d(0, 999999);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%06d", d(rng));
    return buf;
}

Link::~Link() { stop(); }

void Link::start(Config cfg) {
    stop();
    const uint16_t port = cfg.port;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        cfg_ = std::move(cfg);
        errorText_.clear();
        device_ = DeviceInfo{};
        events_.clear();
        poses_.clear();
        previewQueue_.clear();
        statusPending_ = false;
        statusJson_.clear();
        dropRequested_ = false;
    }
    port_.store(port);
    poseCount_.store(0);
    previewWanted_.store(false);
    stopRequested_.store(false);
    state_.store(State::Listening);
    thread_ = std::thread([this] { workerMain(); });
}

void Link::stop() {
    stopRequested_.store(true);
    if (thread_.joinable()) thread_.join();
    state_.store(State::Idle);
    previewWanted_.store(false);
}

void Link::disconnectDevice() {
    std::lock_guard<std::mutex> lk(mutex_);
    // The worker owns the socket; ask it to drop the peer via a queued command
    // expressed as a status marker would be muddy, so keep it simple: a flag.
    dropRequested_ = true;
}

std::string Link::errorText() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return errorText_;
}

DeviceInfo Link::device() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return device_;
}

std::vector<Event> Link::drainEvents() {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<Event> out(events_.begin(), events_.end());
    events_.clear();
    return out;
}

std::vector<CamTakeSample> Link::drainPoses() {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<CamTakeSample> out(poses_.begin(), poses_.end());
    poses_.clear();
    return out;
}

PreviewPrefs Link::preview() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return preview_;
}

void Link::setPreviewDefaults(const PreviewPrefs& p) {
    std::lock_guard<std::mutex> lk(mutex_);
    preview_ = p;
}

void Link::pushPreview(int w, int h, const unsigned char* rgb) {
    if (w <= 0 || h <= 0 || !rgb) return;
    std::lock_guard<std::mutex> lk(mutex_);
    const size_t cap = (size_t)std::clamp(preview_.smoothing, 0, kMaxSmoothing) + 1;
    // Drop the OLDEST when full: latency stays bounded by the queue depth while
    // the freshest frame is always the one that survives.
    while (previewQueue_.size() >= cap) previewQueue_.pop_front();
    PendingFrame f;
    f.rgb.assign(rgb, rgb + (size_t)w * h * 3);
    f.w = w;
    f.h = h;
    previewQueue_.push_back(std::move(f));
    // Room left? Then the caller may grab again on its own cadence instead of
    // waiting for the worker - which is what keeps the delivery even.
    previewWanted_.store(previewQueue_.size() < cap);
}

void Link::setStatus(const Status& s) {
    std::string cams;
    for (size_t i = 0; i < s.cameras.size(); ++i)
        cams += (i ? ",\"" : "\"") + jsonEscape(s.cameras[i]) + "\"";
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "{\"t\":\"status\",\"rec\":%s,\"time\":%.3f,\"keys\":%d,"
                  "\"seq\":\"%s\",\"target\":\"%s\",\"dens\":%.2f,\"driving\":%s,"
                  "\"cams\":[",
                  s.recording ? "true" : "false", s.time, s.keys,
                  jsonEscape(s.sequence).c_str(), jsonEscape(s.target).c_str(),
                  s.density, s.driving ? "true" : "false");
    const std::string json = std::string(buf) + cams + "]}";
    std::lock_guard<std::mutex> lk(mutex_);
    if (statusJson_ == json) return;  // only changes go on the wire
    statusJson_ = json;
    statusPending_ = true;
}

void Link::pushEvent(Event e) {
    std::lock_guard<std::mutex> lk(mutex_);
    events_.push_back(std::move(e));
}

void Link::workerMain() {
    Config cfg;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        cfg = cfg_;
    }
    std::unique_ptr<wire::Transport> tp = wire::makeWebSocketTransport(testClientPage());
    const std::string err = tp->listen(cfg.port);
    if (!err.empty()) {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            errorText_ = "Cannot listen on port " + std::to_string(cfg.port) + ": " + err;
        }
        state_.store(State::Error);
        Event e;
        e.type = Event::Type::Error;
        e.text = errorText();
        pushEvent(std::move(e));
        return;
    }

    // One device at a time: `peer` is the accepted connection, `helloDone` says
    // whether it passed the handshake. An extra device is denied rather than
    // silently fighting the first one for the camera.
    wire::PeerId peer = -1;
    bool helloDone = false;
    uint64_t frameSeq = 0;
    std::vector<wire::Event> evs;

    while (!stopRequested_.load()) {
        // The UI asked to drop the device.
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (dropRequested_) {
                dropRequested_ = false;
                if (peer >= 0) {
                    tp->send(peer, jsonFrame("{\"t\":\"bye\",\"reason\":\"disconnected "
                                             "by the editor\"}"));
                    tp->kick(peer);
                    const DeviceInfo dev = device_;
                    peer = -1;
                    helloDone = false;
                    device_ = DeviceInfo{};
                    Event ev;
                    ev.type = Event::Type::Disconnected;
                    ev.device = dev;
                    ev.text = "disconnected by the editor";
                    events_.push_back(std::move(ev));
                    state_.store(State::Listening);
                    previewWanted_.store(false);
                }
            }
        }

        evs.clear();
        tp->poll(evs, 10);

        for (wire::Event& e : evs) {
            if (e.type == wire::Event::Type::Connected) {
                if (peer >= 0) {
                    tp->send(e.peer,
                             jsonFrame("{\"t\":\"deny\",\"reason\":\"another device is "
                                       "already connected to this editor\"}"));
                    tp->kick(e.peer);
                    continue;
                }
                peer = e.peer;
                helloDone = false;
                std::lock_guard<std::mutex> lk(mutex_);
                device_ = DeviceInfo{};
                device_.address = e.info;
                continue;
            }
            if (e.type == wire::Event::Type::Disconnected) {
                if (e.peer != peer) continue;
                DeviceInfo dev;
                {
                    std::lock_guard<std::mutex> lk(mutex_);
                    dev = device_;
                    device_ = DeviceInfo{};
                }
                peer = -1;
                const bool wasLive = helloDone;
                helloDone = false;
                state_.store(State::Listening);
                previewWanted_.store(false);
                if (wasLive) {
                    Event ev;
                    ev.type = Event::Type::Disconnected;
                    ev.device = dev;
                    ev.text = e.info;
                    pushEvent(std::move(ev));
                }
                continue;
            }
            if (e.peer != peer) continue;

            json::Value msg;
            if (!json::parse(e.frame.json, msg) ||
                msg.type != json::Value::Type::Object)
                continue;
            const std::string type =
                msg.find("t") ? msg.find("t")->stringOr(std::string()) : std::string();

            if (type == "hello") {
                if (helloDone) continue;
                const int proto = (int)(msg.find("proto") ? msg.find("proto")->numberOr(0)
                                                          : 0);
                if (proto != kProtoVersion) {
                    tp->send(peer, jsonFrame(
                                       "{\"t\":\"deny\",\"reason\":\"protocol mismatch: "
                                       "the app speaks v" + std::to_string(proto) +
                                       ", this editor speaks v" +
                                       std::to_string(kProtoVersion) + "\"}"));
                    tp->kick(peer);
                    peer = -1;
                    continue;
                }
                const std::string code =
                    msg.find("code") ? msg.find("code")->stringOr(std::string())
                                     : std::string();
                if (!cfg.pairCode.empty() && code != cfg.pairCode) {
                    tp->send(peer, jsonFrame("{\"t\":\"deny\",\"reason\":\"wrong pairing "
                                             "code\"}"));
                    tp->kick(peer);
                    peer = -1;
                    continue;
                }
                DeviceInfo dev;
                {
                    std::lock_guard<std::mutex> lk(mutex_);
                    dev.address = device_.address;  // captured at Connected
                }
                if (const json::Value* v = msg.find("name")) dev.name = v->stringOr("");
                if (const json::Value* v = msg.find("model")) dev.model = v->stringOr("");
                if (const json::Value* v = msg.find("client")) dev.client = v->stringOr("");
                if (const json::Value* v = msg.find("sixdof")) dev.sixDof = v->boolOr(false);
                if (dev.name.empty()) dev.name = dev.model.empty() ? "phone" : dev.model;
                helloDone = true;
                {
                    std::lock_guard<std::mutex> lk(mutex_);
                    device_ = dev;
                    statusPending_ = !statusJson_.empty();  // resend on (re)connect
                }
                state_.store(State::Connected);
                tp->send(peer, jsonFrame("{\"t\":\"welcome\",\"proto\":" +
                                         std::to_string(kProtoVersion) +
                                         ",\"editor\":\"TyraX\",\"project\":\"" +
                                         jsonEscape(cfg.projectName) + "\"}"));
                Event ev;
                ev.type = Event::Type::Connected;
                ev.device = dev;
                pushEvent(std::move(ev));
                continue;
            }
            if (!helloDone) continue;  // nothing else is honored before hello

            if (type == "pose") {
                CamTakeSample s;
                s.t = msg.find("ts") ? msg.find("ts")->numberOr(0.0) : 0.0;
                readVec(msg, "p", s.pos, 3);  // absent = orientation-only client
                if (!readVec(msg, "q", s.quat, 4)) continue;
                const float len = std::sqrt(s.quat[0] * s.quat[0] + s.quat[1] * s.quat[1] +
                                            s.quat[2] * s.quat[2] + s.quat[3] * s.quat[3]);
                if (len < 1e-6f) continue;
                for (int i = 0; i < 4; ++i) s.quat[i] /= len;
                if (const json::Value* v = msg.find("fov"))
                    s.fovDeg = (float)v->numberOr(0.0);
                std::lock_guard<std::mutex> lk(mutex_);
                poses_.push_back(s);
                while (poses_.size() > kMaxPendingPoses) poses_.pop_front();
                poseCount_.fetch_add(1);
                continue;
            }
            if (type == "cmd") {
                Event ev;
                ev.type = Event::Type::Command;
                ev.text = msg.find("cmd") ? msg.find("cmd")->stringOr("") : "";
                if (!ev.text.empty()) pushEvent(std::move(ev));
                continue;
            }
            if (type == "startcam") {
                Event ev;
                ev.type = Event::Type::SelectCamera;
                if (const json::Value* v = msg.find("name"))
                    ev.text = v->stringOr("");
                pushEvent(std::move(ev));
                continue;
            }
            if (type == "origin") {
                // Fly the start point: a delta in the camera's own
                // right/up/forward frame, in scene units. The editor resolves
                // the frame - the phone only knows "left a bit, forward a bit".
                Event ev;
                ev.type = Event::Type::MoveStart;
                if (const json::Value* v = msg.find("d"))
                    readVec(msg, "d", ev.vec, 3);
                if (ev.vec[0] != 0.0f || ev.vec[1] != 0.0f || ev.vec[2] != 0.0f)
                    pushEvent(std::move(ev));
                continue;
            }
            if (type == "cfg") {
                // The device knows its screen and its Wi-Fi better than we do.
                std::lock_guard<std::mutex> lk(mutex_);
                if (const json::Value* v = msg.find("maxw"))
                    preview_.maxWidth = std::clamp((int)v->numberOr(480), 64, 1280);
                if (const json::Value* v = msg.find("maxh"))
                    preview_.maxHeight = std::clamp((int)v->numberOr(480), 64, 1280);
                if (const json::Value* v = msg.find("fps"))
                    preview_.fps = std::clamp((int)v->numberOr(15), 1, 60);
                if (const json::Value* v = msg.find("quality"))
                    preview_.quality = std::clamp((int)v->numberOr(60), 5, 95);
                continue;
            }
            if (type == "bye") {
                tp->kick(peer);
                DeviceInfo dev;
                {
                    std::lock_guard<std::mutex> lk(mutex_);
                    dev = device_;
                    device_ = DeviceInfo{};
                }
                peer = -1;
                helloDone = false;
                state_.store(State::Listening);
                previewWanted_.store(false);
                Event ev;
                ev.type = Event::Type::Disconnected;
                ev.device = dev;
                ev.text = "left";
                pushEvent(std::move(ev));
                continue;
            }
        }

        if (peer < 0 || !helloDone) {
            previewWanted_.store(false);
            continue;
        }

        // Status first: it is tiny, and the phone's REC light should never wait
        // behind a video frame.
        {
            std::string statusJson;
            {
                std::lock_guard<std::mutex> lk(mutex_);
                if (statusPending_) {
                    statusJson = statusJson_;
                    statusPending_ = false;
                }
            }
            if (!statusJson.empty()) tp->send(peer, jsonFrame(std::move(statusJson)));
        }

        // Preview frame: encode outside the lock (JPEG of a 480x360 image is a
        // couple of milliseconds, but it must not block pushPreview).
        std::vector<unsigned char> rgb;
        int w = 0, h = 0, quality = 60;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (!previewQueue_.empty()) {
                rgb.swap(previewQueue_.front().rgb);
                w = previewQueue_.front().w;
                h = previewQueue_.front().h;
                previewQueue_.pop_front();
                quality = preview_.quality;
            }
        }
        if (!rgb.empty() && w > 0 && h > 0) {
            std::string jpeg;
            stbi_write_jpg_to_func(
                [](void* ctx, void* data, int size) {
                    static_cast<std::string*>(ctx)->append((const char*)data,
                                                           (size_t)size);
                },
                &jpeg, w, h, 3, rgb.data(), quality);
            if (!jpeg.empty()) {
                wire::Frame f;
                char hdr[96];
                std::snprintf(hdr, sizeof(hdr),
                              "{\"t\":\"frame\",\"w\":%d,\"h\":%d,\"seq\":%llu}", w, h,
                              (unsigned long long)frameSeq++);
                f.json = hdr;
                f.bin = std::move(jpeg);
                tp->send(peer, f);
            }
        }

        // Ask for another grab while the socket is keeping up AND the queue has
        // room. Both gates matter: the backlog one stops a weak link ballooning
        // memory, the queue one is what paces the grabs evenly.
        {
            std::lock_guard<std::mutex> lk(mutex_);
            const size_t cap =
                (size_t)std::clamp(preview_.smoothing, 0, kMaxSmoothing) + 1;
            previewWanted_.store(tp->sendBacklog(peer) < kSendBacklogLimit &&
                                 previewQueue_.size() < cap);
        }
    }

    if (peer >= 0) {
        tp->send(peer, jsonFrame("{\"t\":\"bye\",\"reason\":\"the editor closed the "
                                 "link\"}"));
        tp->kick(peer);
    }
    tp->close();
    previewWanted_.store(false);
}

// --- Browser test client ---------------------------------------------------------
// Served to a plain GET on the link's port. Deliberately synthetic-pose only:
// drag to look, WASD/QE to fly. The real device path is ARKit in
// the tyrax-cam app, and a half-right DeviceOrientation conversion here would be
// a misleading reference for it. What this page IS good for is proving the
// whole chain - handshake, pairing, pose ingest, JPEG stream, commands - from
// any machine on the LAN, with no phone and no build.
std::string testClientPage() {
    return R"PAGE(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>TyraX Cam - test client</title>
<style>
 :root{color-scheme:dark}
 *{box-sizing:border-box}
 body{margin:0;background:#14161a;color:#e6e6e6;font:13px/1.45 system-ui,sans-serif;
      -webkit-user-select:none;user-select:none}
 header{display:flex;gap:8px;align-items:center;padding:8px;background:#1d2027;
        flex-wrap:wrap}
 input{background:#0e1013;border:1px solid #333;color:#eee;padding:5px 7px;border-radius:4px}
 input[type=text]{width:8em}
 button{background:#2b3a55;border:1px solid #3d5480;color:#eee;padding:6px 12px;
        border-radius:4px;cursor:pointer}
 button:hover{background:#365079}
 button.rec{background:#5b2029;border-color:#8a3542}
 button:disabled{opacity:.45;cursor:default}
 #stage{position:relative;background:#000;display:flex;align-items:center;
        justify-content:center;height:calc(100vh - 152px);touch-action:none;cursor:grab}
 #stage.drag{cursor:grabbing}
 #view{max-width:100%;max-height:100%;image-rendering:auto}
 #hint{position:absolute;color:#7a8290;text-align:center;padding:0 16px}
 #rec{position:absolute;top:10px;left:12px;display:none;align-items:center;gap:6px;
      color:#ff5a6a;font-weight:600;letter-spacing:.5px}
 #rec b{width:10px;height:10px;border-radius:50%;background:#ff3040;display:inline-block;
        animation:blink 1s steps(2,end) infinite}
 @keyframes blink{50%{opacity:.15}}
 footer{padding:8px;background:#1d2027;display:flex;gap:8px;align-items:center;
        flex-wrap:wrap}
 .stat{color:#8b93a1;font-variant-numeric:tabular-nums}
 .ok{color:#6ede8a}.bad{color:#ff7a86}
</style></head><body>
<header>
 <strong>TyraX Cam</strong>
 <label>code <input type="text" id="code" inputmode="numeric" placeholder="123456"></label>
 <button id="conn">Connect</button>
 <span id="state" class="stat">idle</span>
</header>
<div id="stage">
 <img id="view" alt="">
 <div id="hint">Connect, then drag here to look around.<br>WASD to walk, Q/E up &amp; down,
  Shift to move faster.</div>
 <div id="rec"><b></b>REC</div>
</div>
<footer>
 <button id="btnRec" disabled>Record</button>
 <button id="btnStop" disabled>Stop</button>
 <button id="btnCenter" disabled>Recentre</button>
 <span class="stat" id="stat"></span>
</footer>
<script>
// --- frame codec: [u32 jsonLen][u32 binLen][json][bin], little-endian --------
const enc = new TextEncoder(), dec = new TextDecoder();
function pack(obj, bin) {
  const j = enc.encode(JSON.stringify(obj)), b = bin || new Uint8Array(0);
  const out = new Uint8Array(8 + j.length + b.length);
  const dv = new DataView(out.buffer);
  dv.setUint32(0, j.length, true); dv.setUint32(4, b.length, true);
  out.set(j, 8); out.set(b, 8 + j.length);
  return out;
}
function unpack(buf) {
  const u = new Uint8Array(buf), dv = new DataView(u.buffer, u.byteOffset, u.byteLength);
  if (u.length < 8) return null;
  const jl = dv.getUint32(0, true), bl = dv.getUint32(4, true);
  if (u.length < 8 + jl + bl) return null;
  return { msg: JSON.parse(dec.decode(u.subarray(8, 8 + jl))),
           bin: u.subarray(8 + jl, 8 + jl + bl) };
}

const $ = id => document.getElementById(id);
let ws = null, url = null, frames = 0, lastFpsAt = 0, fps = 0, status = {};

// Redrawn on every frame as well as on every status: the editor only sends a
// status when something CHANGES, so a status-only readout would freeze the
// measured frame rate at whatever it was on connect.
function renderStat() {
  $('stat').textContent = (status.seq ? status.seq + ' | ' : '') +
    (status.target ? 'camera ' + status.target : 'free shots') +
    ' | t ' + (status.time||0).toFixed(2) + ' s | ' + (status.keys||0) + ' keys' +
    (status.dens ? ' @ ' + status.dens + '/s' : '') +
    ' | stream ' + fps.toFixed(1) + ' fps';
}

function setState(text, cls) { $('state').textContent = text; $('state').className = 'stat ' + (cls||''); }
function cmdEnabled(on) { for (const b of ['btnRec','btnStop','btnCenter']) $(b).disabled = !on; }

function connect() {
  if (ws) { ws.close(); return; }
  ws = new WebSocket('ws://' + location.host + '/');
  ws.binaryType = 'arraybuffer';
  setState('connecting...');
  ws.onopen = () => {
    ws.send(pack({ t: 'hello', proto: 1, code: $('code').value.trim(),
                   name: 'browser', model: navigator.platform || 'browser',
                   client: 'browser test client', sixdof: true }));
    ws.send(pack({ t: 'cfg', maxw: Math.min(960, Math.round(innerWidth)),
                   maxh: Math.min(960, Math.round(innerHeight)), fps: 15, quality: 60 }));
  };
  ws.onmessage = ev => {
    const f = unpack(ev.data); if (!f) return;
    if (f.msg.t === 'welcome') {
      setState('connected' + (f.msg.project ? ' - ' + f.msg.project : ''), 'ok');
      cmdEnabled(true); $('conn').textContent = 'Disconnect'; startPose();
    } else if (f.msg.t === 'deny' || f.msg.t === 'bye') {
      setState((f.msg.t === 'deny' ? 'denied: ' : 'closed: ') + (f.msg.reason||''), 'bad');
    } else if (f.msg.t === 'frame') {
      const blob = new Blob([f.bin], { type: 'image/jpeg' });
      const next = URL.createObjectURL(blob);
      $('view').src = next;
      if (url) URL.revokeObjectURL(url);
      url = next; $('hint').style.display = 'none';
      frames++;
      const now = performance.now();
      if (lastFpsAt && now - lastFpsAt > 1000) {
        fps = frames * 1000 / (now - lastFpsAt); frames = 0; lastFpsAt = now;
      } else if (!lastFpsAt) { lastFpsAt = now; frames = 0; }
      renderStat();
    } else if (f.msg.t === 'status') {
      status = f.msg;
      $('rec').style.display = f.msg.rec ? 'flex' : 'none';
      renderStat();
    }
  };
  ws.onclose = () => { ws = null; cmdEnabled(false); $('conn').textContent = 'Connect';
                       if (!$('state').classList.contains('bad')) setState('disconnected'); };
  ws.onerror = () => setState('connection failed', 'bad');
}
$('conn').onclick = connect;
for (const [id, cmd] of [['btnRec','record'],['btnStop','stop'],['btnCenter','recenter']])
  $(id).onclick = () => ws && ws.send(pack({ t: 'cmd', cmd }));

// --- synthetic 6DoF pose ----------------------------------------------------
// ARKit convention: metres, Y up, quaternion rotating camera-local axes into
// world axes, camera looking down its local -Z.
let yaw = 0, pitch = 0, pos = [0, 0, 0], keys = new Set(), poseTimer = null;
const stage = $('stage');
let dragging = false, lastX = 0, lastY = 0;
function down(x, y) { dragging = true; lastX = x; lastY = y; stage.classList.add('drag'); }
function move(x, y) {
  if (!dragging) return;
  yaw -= (x - lastX) * 0.005; pitch -= (y - lastY) * 0.005;
  pitch = Math.max(-1.5, Math.min(1.5, pitch));
  lastX = x; lastY = y;
}
function up() { dragging = false; stage.classList.remove('drag'); }
stage.addEventListener('pointerdown', e => { down(e.clientX, e.clientY); stage.setPointerCapture(e.pointerId); });
stage.addEventListener('pointermove', e => move(e.clientX, e.clientY));
stage.addEventListener('pointerup', up);
stage.addEventListener('pointercancel', up);
addEventListener('keydown', e => { keys.add(e.key.toLowerCase()); if (' wasdqe'.includes(e.key.toLowerCase())) e.preventDefault(); });
addEventListener('keyup', e => keys.delete(e.key.toLowerCase()));

function quat() {
  const cy = Math.cos(yaw/2), sy = Math.sin(yaw/2);
  const cx = Math.cos(pitch/2), sx = Math.sin(pitch/2);
  // Ry(yaw) * Rx(pitch)
  return [cy*sx, sy*cx, -sy*sx, cy*cx];
}
function axes() {
  const cy = Math.cos(yaw), sy = Math.sin(yaw), cp = Math.cos(pitch), sp = Math.sin(pitch);
  return { fwd: [-cp*sy, sp, -cp*cy], right: [cy, 0, -sy] };
}
function startPose() {
  if (poseTimer) return;
  let t0 = performance.now(), prev = t0;
  poseTimer = setInterval(() => {
    if (!ws || ws.readyState !== 1) return;
    const now = performance.now(), dt = (now - prev) / 1000; prev = now;
    const sp = (keys.has('shift') ? 3.0 : 1.0) * 1.2 * dt;  // metres/second
    const a = axes();
    const add = (v, s) => { for (let i = 0; i < 3; i++) pos[i] += v[i] * s; };
    if (keys.has('w')) add(a.fwd, sp);
    if (keys.has('s')) add(a.fwd, -sp);
    if (keys.has('d')) add(a.right, sp);
    if (keys.has('a')) add(a.right, -sp);
    if (keys.has('e')) pos[1] += sp;
    if (keys.has('q')) pos[1] -= sp;
    ws.send(pack({ t: 'pose', ts: (now - t0) / 1000, p: pos, q: quat(), fov: 60 }));
  }, 1000 / 30);
}
</script></body></html>
)PAGE";
}

}  // namespace phonecam
