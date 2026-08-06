// -------------------------------------------------------------------------
// The Cutscene Director: dopesheet, camera shots, the phone-camera link
// (docs/phone-camera.md) and camera-take import (docs/camera-takes.md).
//
// Split out of app.cpp so the editor builds in parallel: it was one 26k-line
// translation unit and therefore the whole build's critical path. These are
// still App:: members declared in app.hpp - the assetbrowser.cpp precedent.
// -------------------------------------------------------------------------
#include "app.hpp"
#include "app_internal.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "aisupport.hpp"
#include "animedit.hpp"
#include "decalproj.hpp"
#include "devsession.hpp"
#include "editorcfg.hpp"
#include "gl_loader.h"
#include "fbxparser.hpp"
#include "glbparser.hpp"
#include "json.hpp"
#include "menubake.hpp"
#include "objparser.hpp"
#include "pngquant.hpp"
#include "uvunwrap.hpp"
#include "stochtile.hpp"
#include "templates.hpp"
#include "wavconvert.hpp"

#include <stb_image.h>
#include <stb_image_write.h>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <ImGuizmo.h>
#include <imnodes.h>

#include "platform.hpp"

// --- Cutscene Director -------------------------------------------------------

// Snapshots the track target's current static pose into a key at `time`.
// A key within 1/60 s of that time is replaced instead (keeping its easing
// and visibility flag); the key list stays sorted.
bool App::cutsceneSnapshotObjectKey(SeqTrack& tr, float time) {
    const SceneObject* src = nullptr;
    for (const SceneObject& o : project_.objects())
        if (o.name == tr.target) {
            src = &o;
            break;
        }
    if (!src) return false;
    SeqObjectKey k;
    k.time = time;
    for (int c = 0; c < 3; ++c) {
        k.position[c] = src->position[c];
        k.rotation[c] = src->rotation[c];
        k.scale[c] = src->scale[c];
        k.color[c] = src->color[c];
    }
    int repl = -1;
    for (int i = 0; i < (int)tr.keys.size(); ++i)
        if (std::fabs(tr.keys[i].time - k.time) < 0.017f) repl = i;
    if (repl >= 0) {
        k.easing = tr.keys[repl].easing;
        k.visible = tr.keys[repl].visible;
        tr.keys[repl] = k;
    } else {
        tr.keys.push_back(k);
    }
    std::sort(tr.keys.begin(), tr.keys.end(),
              [](const SeqObjectKey& a, const SeqObjectKey& b) {
                  return a.time < b.time;
              });
    return true;
}

// Auto-key: called when a gizmo drag ends, just before its commitChange(), so
// the dropped keys ride the same undo snapshot as the transform edit itself.
void App::cutsceneAutoKey() {
    if (!seqAutoKey_ || !showCutsceneEditor_ || !seqPreview_ || !hasProject_) return;
    if (selectedSequence_ < 0 || selectedSequence_ >= (int)project_.sequences.size())
        return;
    Sequence& s = project_.sequences[selectedSequence_];
    for (int sel : selection_) {
        if (sel < 0 || sel >= (int)project_.objects().size()) continue;
        const std::string& name = project_.objects()[sel].name;
        for (SeqTrack& tr : s.tracks)
            if (tr.target == name) cutsceneSnapshotObjectKey(tr, seqPlayhead_);
    }
}

// Applies a camera take to a sequence. Free target -> camera-lane shots
// (replace or append). A Camera-entity target -> the take is baked into that
// entity's transform track (position = eye, rotation = the Euler whose +Z lens
// points along the recorded view), the entity's FOV is set from the take, and a
// bound camera key is ensured so the shot dollies along the path.
// Returns the first key time, or -1 on no-op.
float App::applyCamTake(Sequence& s, bool replace, const CamTake& take,
                        const CamTakeMapping& map, const std::string& target,
                        CamTakeBakeStats& stats) {
    std::vector<SeqCameraKey> baked = bakeCamTake(take, map, &stats);
    if (baked.empty()) return -1.0f;
    auto sortCam = [&]() {
        std::sort(s.cameraKeys.begin(), s.cameraKeys.end(),
                  [](const SeqCameraKey& a, const SeqCameraKey& b) {
                      return a.time < b.time;
                  });
    };

    if (target.empty()) {
        // free camera shots on the camera lane
        if (replace)
            s.cameraKeys = baked;
        else {
            s.cameraKeys.insert(s.cameraKeys.end(), baked.begin(), baked.end());
            sortCam();
        }
        s.cameraEnabled = true;
        return baked.front().time;
    }

    // Camera-entity target: bake into the entity's transform track. Euler
    // angles wrap at +-180, so unwrap each channel to stay continuous with the
    // previous key - otherwise a pan crossing 180 deg (e.g. 170 -> -175) makes
    // the linear rotation interp spin the long way round: the sudden 180/360
    // whip after import.
    std::vector<SeqObjectKey> objKeys;
    objKeys.reserve(baked.size());
    float prevRot[3] = {0.0f, 0.0f, 0.0f};
    for (size_t i = 0; i < baked.size(); ++i) {
        const SeqCameraKey& k = baked[i];
        SeqObjectKey o;
        o.time = k.time;
        for (int c = 0; c < 3; ++c) o.position[c] = k.eye[c];
        const float dir[3] = {k.target[0] - k.eye[0], k.target[1] - k.eye[1],
                              k.target[2] - k.eye[2]};
        // Carry the recorded roll: a Camera entity has no separate roll channel,
        // so the tilt has to live inside its Euler. seqEulerFromBasis inverts the
        // entity's own Rz*Ry*Rx, so the baked rotation reproduces the filmed
        // basis exactly - lens direction AND lean.
        if (k.roll != 0.0f) {
            float up[3];
            seqCameraUp(dir, k.roll, up);
            seqEulerFromBasis(dir, up, o.rotation);
        } else {
            seqEulerFromForward(dir, o.rotation);
        }
        if (i > 0)
            for (int c = 0; c < 3; ++c) {
                while (o.rotation[c] - prevRot[c] > 180.0f) o.rotation[c] -= 360.0f;
                while (o.rotation[c] - prevRot[c] < -180.0f) o.rotation[c] += 360.0f;
            }
        for (int c = 0; c < 3; ++c) prevRot[c] = o.rotation[c];
        o.easing = 0;  // the take IS the ease
        objKeys.push_back(o);
    }
    // find (or create) the object track that drives this camera entity
    SeqTrack* track = nullptr;
    for (SeqTrack& tr : s.tracks)
        if (tr.target == target) {
            track = &tr;
            break;
        }
    if (!track) {
        SeqTrack tr;
        tr.target = target;
        s.tracks.push_back(std::move(tr));
        track = &s.tracks.back();
    }
    track->animPos = true;
    track->animRot = true;
    track->animScale = false;
    track->animColor = false;
    track->animVis = false;
    track->keys = std::move(objKeys);
    // set the entity's FOV from the take (active-scene object of that name)
    for (SceneObject& o : project_.objects())
        if (o.name == target && o.type == PrimitiveType::Camera) {
            if (stats.fovDeg > 0.0f) o.cameraFov = stats.fovDeg;
            break;
        }
    // ensure a bound camera key so the entity is actually filmed
    bool bound = false;
    for (const SeqCameraKey& k : s.cameraKeys)
        if (k.camera == target) {
            bound = true;
            break;
        }
    if (!bound) {
        SeqCameraKey k;
        k.time = track->keys.front().time;
        k.camera = target;
        k.easing = 0;
        s.cameraKeys.push_back(k);
        sortCam();
    }
    s.cameraEnabled = true;
    return track->keys.front().time;
}

float App::applyCamTake(Sequence& s, bool replace) {
    return applyCamTake(s, replace, seqTake_, seqTakeMap_, seqTakeTarget_, seqTakeStats_);
}

// "From view" for take import: drop the take's first sample at the preview
// camera AND rotate the whole path so its first sample looks the way the
// editor camera does - so you frame the shot in the viewport, hit From view,
// and the recording is aimed there.
void App::takeOriginAimFromView() {
    float eye[3], at[3];
    viewport_.currentCamera(eye, at);
    for (int c = 0; c < 3; ++c) seqTakeMap_.origin[c] = eye[c];
    const float viewYaw =
        std::atan2(at[0] - eye[0], at[2] - eye[2]) * 180.0f / 3.14159265f;
    float y = viewYaw - camTakeInitialYawDeg(seqTake_);
    while (y > 180.0f) y -= 360.0f;
    while (y < -180.0f) y += 360.0f;
    seqTakeMap_.yawDeg = y;
}

// --- Phone camera link -----------------------------------------------------------
// The phone as a viewfinder (docs/phone-camera.md): phonecam::Link runs the
// network side on a worker thread, this block is the UI half - the per-frame
// drain, the live camera override, the preview stream and the recording that
// turns phone motion into Cutscene Director camera keys.

float App::projectFrameRate() const {
    // Fixed display modes pin the refresh regardless of the video system; only
    // the region-following ones follow it (see ProjectSettings::displayMode).
    const std::string& dm = project_.settings.displayMode;
    if (dm == "progressive" || dm == "1080i") return 60.0f;
    if (dm == "pal576") return 50.0f;
    return project_.settings.videoSystem == "pal" ? 50.0f : 60.0f;
}

void App::startPhoneCam() {
    if (phoneCamCode_.empty()) phoneCamCode_ = phonecam::newPairCode();
    phonecam::Config cfg;
    cfg.port = (uint16_t)phoneCamPort_;
    cfg.pairCode = phoneCamRequireCode_ ? phoneCamCode_ : std::string();
    cfg.projectName = hasProject_ ? project_.name : std::string();
    phoneCam_.setPreviewDefaults(phoneCamPrefs_);
    phoneCam_.start(cfg);
    phoneHasPose_ = false;
    saveGlobalConfig();
    statusMessage_ = "Phone camera link listening on port " +
                     std::to_string(phoneCamPort_);
}

void App::stopPhoneCam() {
    if (phoneRec_) stopPhoneRecording();
    phoneCam_.stop();
    phoneHasPose_ = false;
    phoneCamPushed_ = false;
    statusMessage_ = "Phone camera link stopped";
}

// The Camera entity the phone starts from, or nullptr when the target is "free
// camera shots" (then the editor's own viewpoint is the start).
const SceneObject* App::phoneStartCamera() const {
    if (phoneRecTarget_.empty()) return nullptr;
    for (const SceneObject& o : project_.objects())
        if (o.name == phoneRecTarget_ && o.type == PrimitiveType::Camera) return &o;
    return nullptr;  // stale name (renamed/deleted): fall back to the view
}

// Puts the phone camera back at its start and aims it there. With a Camera entity
// selected THAT is the start - the pose it was placed at, aim and tilt included -
// so recentring returns to where you put the camera in the scene, not to wherever
// the editor's orbit camera happens to be. Without one, the editor's viewpoint is
// the start (currentCamera() deliberately reads the ORBIT camera, ignoring the
// override the phone itself installed, so it means "the vantage point I framed").
void App::phoneCamRecenter() {
    if (!phoneHasPose_) return;
    float eye[3], at[3], targetRoll = 0.0f;
    if (const SceneObject* cam = phoneStartCamera()) {
        float fwd[3], eu[3];
        seqCameraForward(cam->rotation, fwd);
        seqCameraUpFromEuler(cam->rotation, eu);
        for (int c = 0; c < 3; ++c) {
            eye[c] = cam->position[c];
            at[c] = cam->position[c] + fwd[c];
        }
        // Start tilted exactly as the camera was placed, so the phone's own lean
        // is measured from there rather than from level.
        targetRoll = seqRollFromUp(fwd, eu);
    } else {
        viewport_.currentCamera(eye, at);
    }
    for (int c = 0; c < 3; ++c) {
        phoneMap_.origin[c] = eye[c];
        phoneMap_.anchor[c] = phonePose_.pos[c];
    }
    phoneMap_.hasAnchor = true;
    // However you happen to be holding the phone now maps to the start's tilt.
    phoneMap_.anchorRoll = camSampleRollDeg(phonePose_) - targetRoll;
    const float viewYaw =
        std::atan2(at[0] - eye[0], at[2] - eye[2]) * 180.0f / 3.14159265f;
    float y = viewYaw - camSampleYawDeg(phonePose_);
    while (y > 180.0f) y -= 360.0f;
    while (y < -180.0f) y += 360.0f;
    phoneMap_.yawDeg = y;
}

// Picks the Camera entity to view from / record into, and immediately jumps there.
// One selection on purpose: "I want the view from cam-1" and "the recording goes
// into cam-1" are the same intent, and two controls would only let them disagree.
void App::selectPhoneCamera(const std::string& name) {
    if (phoneRec_) return;  // never move the target out from under a recording
    phoneRecTarget_ = name;
    phoneCamRecenter();
    statusMessage_ = name.empty() ? "Phone camera: free shots from the editor view"
                                  : "Phone camera starts from \"" + name + "\"";
}

// Flies the mapping's start point along the CURRENT view basis, so a nudge the
// phone describes as "left a bit" means left as seen on its screen. The anchor
// is deliberately untouched: moving the origin moves the camera bodily while the
// phone's own motion stays relative to wherever it now is.
void App::movePhoneStart(const float delta[3]) {
    float fwd[3] = {phoneTarget_[0] - phoneEye_[0], phoneTarget_[1] - phoneEye_[1],
                    phoneTarget_[2] - phoneEye_[2]};
    const float fl = std::sqrt(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
    if (fl > 1e-6f)
        for (int c = 0; c < 3; ++c) fwd[c] /= fl;
    float up[3];
    seqCameraUp(fwd, phoneRoll_, up);
    // right = cross(fwd, up)
    const float right[3] = {fwd[1] * up[2] - fwd[2] * up[1],
                            fwd[2] * up[0] - fwd[0] * up[2],
                            fwd[0] * up[1] - fwd[1] * up[0]};
    for (int c = 0; c < 3; ++c)
        phoneMap_.origin[c] +=
            right[c] * delta[0] + up[c] * delta[1] + fwd[c] * delta[2];
}

bool App::startPhoneRecording() {
    if (phoneRec_) return true;
    if (!hasProject_) {
        statusMessage_ = "Open a project before recording a camera move";
        return false;
    }
    if (selectedSequence_ < 0 || selectedSequence_ >= (int)project_.sequences.size()) {
        statusMessage_ = "Select a cutscene in the Cutscene Director first";
        return false;
    }
    if (!phoneCam_.connected() || !phoneHasPose_) {
        statusMessage_ = "No phone is streaming a pose yet";
        return false;
    }
    // Validate the target: a stale name (renamed/deleted camera) would bake a
    // track nothing films from.
    if (!phoneRecTarget_.empty()) {
        bool ok = false;
        for (const SceneObject& o : project_.objects())
            if (o.name == phoneRecTarget_ && o.type == PrimitiveType::Camera) ok = true;
        if (!ok) phoneRecTarget_.clear();
    }
    phoneRecSeq_ = selectedSequence_;
    Sequence& s = project_.sequences[phoneRecSeq_];
    phoneRecBaseCam_ = s.cameraKeys;
    phoneRecBaseDuration_ = s.duration;
    phoneRecPlayhead_ = phoneRecAtPlayhead_ ? seqPlayhead_ : 0.0f;
    phoneTake_ = CamTake{};
    phoneTake_.source = "Phone camera link";
    phoneTake_.fps = 0.0f;  // irregular by nature - it is a network stream
    phoneTake_.samples.push_back(phonePose_);  // t = 0 is where the phone is now
    phoneRecStats_ = CamTakeBakeStats{};
    phoneRecBakedAt_ = 0.0;
    phoneRec_ = true;
    seqPlaying_ = false;  // the phone owns the clock while it records
    seqPlayhead_ = phoneRecPlayhead_;
    statusMessage_ = "Recording camera from " + phoneCam_.device().name;
    return true;
}

void App::stopPhoneRecording() {
    if (!phoneRec_) return;
    bakePhoneRecording();
    phoneRec_ = false;
    if (phoneRecStats_.keyCount > 0) {
        // One undo step for the whole recording - every live re-bake before
        // this deliberately left the history alone.
        commitChange();
        char buf[160];
        std::snprintf(buf, sizeof(buf), "Recorded %d camera keys over %.2f s",
                      phoneRecStats_.keyCount, phoneRecStats_.duration);
        statusMessage_ = buf;
        seqPlayhead_ = phoneRecPlayhead_;
    } else {
        statusMessage_ = "Recording produced no keys (too short?)";
    }
}

void App::bakePhoneRecording() {
    if (!phoneRec_ || !hasProject_) return;
    if (phoneRecSeq_ < 0 || phoneRecSeq_ >= (int)project_.sequences.size()) return;
    if (phoneTake_.samples.size() < 2) return;
    Sequence& s = project_.sequences[phoneRecSeq_];
    CamTakeMapping map = phoneMap_;
    map.timeOffset = phoneRecPlayhead_;
    switch (phoneDensityMode_) {
        case 0:  // every game frame gets a key
            map.keyRate = projectFrameRate();
            break;
        case 1:
            map.keyRate = phoneDensity_ > 0.1f ? phoneDensity_ : 0.1f;
            break;
        default:  // no density: decimate by the take importer's tolerance
            map.keyRate = 0.0f;
            map.tolerance = phoneTolerance_;
            break;
    }
    // Restore the pre-recording lane, then lay this take on top (see
    // phoneRecBaseCam_) - a live re-bake must be idempotent.
    s.cameraKeys = phoneRecBaseCam_;
    applyCamTake(s, false, phoneTake_, map, phoneRecTarget_, phoneRecStats_);
    float lastT = phoneRecBaseDuration_;
    for (const SeqCameraKey& k : s.cameraKeys) lastT = std::max(lastT, k.time);
    for (const SeqTrack& tr : s.tracks)
        if (tr.target == phoneRecTarget_ && !tr.keys.empty())
            lastT = std::max(lastT, tr.keys.back().time);
    s.duration = lastT;
    setDirty(true);  // transient edit: no undo snapshot until the recording ends
}

void App::phoneCamTick() {
    for (phonecam::Event& e : phoneCam_.drainEvents()) {
        switch (e.type) {
            case phonecam::Event::Type::Connected:
                statusMessage_ = e.device.name + " connected" +
                                 (e.device.sixDof ? "" : " (orientation only)");
                phoneHasPose_ = false;  // re-anchor on the first pose
                // ARKit reports metres, so the project's world scale IS the
                // mapping scale (docs/world-scale.md) - exactly what opening a
                // take file seeds. A connection is this path's "open": seeding
                // it later would fight a hand-tuned value, seeding it never
                // would make a metre walked one unit in a project where a metre
                // is ten. The tolerance is a world-unit distance, so it follows.
                phoneMap_.scale = project_.settings.unitsPerMeter;
                phoneTolerance_ = 0.05f * project_.settings.unitsPerMeter;
                break;
            case phonecam::Event::Type::Disconnected:
                if (phoneRec_) stopPhoneRecording();
                phoneHasPose_ = false;
                phoneCamPushed_ = false;
                statusMessage_ = (e.device.name.empty() ? std::string("Phone")
                                                        : e.device.name) +
                                 " disconnected" +
                                 (e.text.empty() ? "" : " (" + e.text + ")");
                break;
            case phonecam::Event::Type::Command:
                if (e.text == phonecam::kCmdRecord) startPhoneRecording();
                else if (e.text == phonecam::kCmdStop) stopPhoneRecording();
                else if (e.text == phonecam::kCmdRecenter) phoneCamRecenter();
                break;
            case phonecam::Event::Type::MoveStart:
                movePhoneStart(e.vec);
                break;
            case phonecam::Event::Type::SelectCamera:
                selectPhoneCamera(e.text);
                break;
            case phonecam::Event::Type::Error:
                statusMessage_ = "Phone camera link: " + e.text;
                break;
        }
    }

    const std::vector<CamTakeSample> poses = phoneCam_.drainPoses();
    if (!poses.empty()) {
        phonePose_ = poses.back();
        phonePoseAt_ = ImGui::GetTime();
        if (!phoneHasPose_) {
            // First pose of a connection: pin the anchor here and aim the path
            // along the current view, so the phone starts framed on what the
            // editor camera was already looking at.
            phoneHasPose_ = true;
            phoneCamRecenter();
        }
        if (phoneRec_)
            phoneTake_.samples.insert(phoneTake_.samples.end(), poses.begin(),
                                      poses.end());
    }
    if (phoneHasPose_) {
        float anchor[3];
        camTakeAnchor(phoneTake_, phoneMap_, anchor);
        mapCamSample(phonePose_, phoneMap_, anchor, phoneEye_, phoneTarget_,
                     &phoneRoll_);
        phoneFov_ = phonePose_.fovDeg > 1.0f ? phonePose_.fovDeg : 60.0f;
    }

    // Live re-bake at 15 Hz: enough for the dopesheet to visibly fill up while
    // you move, cheap enough not to matter (a few hundred samples).
    if (phoneRec_) {
        const double now = ImGui::GetTime();
        if (now - phoneRecBakedAt_ > 1.0 / 15.0) {
            phoneRecBakedAt_ = now;
            bakePhoneRecording();
        }
    }

    if (phoneCam_.connected()) {
        phonecam::Status st;
        st.recording = phoneRec_;
        st.time = phoneRec_ ? (float)phoneTake_.duration() : seqPlayhead_;
        st.keys = phoneRec_ ? phoneRecStats_.keyCount : 0;
        if (hasProject_ && selectedSequence_ >= 0 &&
            selectedSequence_ < (int)project_.sequences.size())
            st.sequence = project_.sequences[selectedSequence_].name;
        st.target = phoneRecTarget_;
        for (const SceneObject& o : project_.objects())
            if (o.type == PrimitiveType::Camera) st.cameras.push_back(o.name);
        st.density = phoneDensityMode_ == 0 ? projectFrameRate()
                     : phoneDensityMode_ == 1 ? phoneDensity_
                                              : 0.0f;
        st.driving = phoneCamPushed_;
        phoneCam_.setStatus(st);
    }
}

// Tools > Phone Camera: hosting controls, what the phone has to be told to
// connect, and the live mapping. Recording lives in the Cutscene Director -
// this window is the link itself.
void App::drawPhoneCamWindow() {
    if (!showPhoneCamWindow_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(430), scaled(520)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Phone Camera", &showPhoneCamWindow_)) {
        ImGui::End();
        return;
    }

    const bool up = phoneCam_.listening();
    const bool live = phoneCam_.connected();
    const phonecam::DeviceInfo dev = phoneCam_.device();

    // --- state line ---------------------------------------------------------
    if (phoneCam_.state() == phonecam::Link::State::Error) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "Error");
        ImGui::TextWrapped("%s", phoneCam_.errorText().c_str());
    } else if (live) {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Connected");
        ImGui::SameLine();
        ImGui::Text("- %s", dev.name.c_str());
    } else if (up) {
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), "Waiting for a phone");
    } else {
        ImGui::TextDisabled("Not hosting");
    }

    if (!up) {
        if (ImGui::Button("Start link", ImVec2(scaled(120), 0))) startPhoneCam();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(80.0f));
        if (ImGui::InputInt("Port", &phoneCamPort_, 0, 0))
            phoneCamPort_ = std::clamp(phoneCamPort_, 1024, 65535);
        ImGui::SameLine();
        if (ImGui::Checkbox("Require code", &phoneCamRequireCode_)) saveGlobalConfig();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Off accepts any device on this network - fine at a\n"
                              "desk of your own, wrong in a shared office.");
    } else {
        if (ImGui::Button("Stop link", ImVec2(scaled(120), 0))) stopPhoneCam();
        if (live) {
            ImGui::SameLine();
            if (ImGui::Button("Disconnect")) phoneCam_.disconnectDevice();
        }
    }

    // --- what to type into the phone ---------------------------------------
    if (up) {
        ImGui::SeparatorText("Connect the phone to");
        const std::vector<std::string> ips = wire::localIPv4();
        if (ips.empty()) {
            ImGui::TextDisabled("No LAN address found - is this machine on Wi-Fi?");
        }
        for (const std::string& ip : ips) {
            const std::string addr = ip + ":" + std::to_string(phoneCam_.port());
            ImGui::PushID(ip.c_str());
            ImGui::Text("%s", addr.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy")) ImGui::SetClipboardText(addr.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy URL"))
                ImGui::SetClipboardText(("http://" + addr).c_str());
            ImGui::PopID();
        }
        if (phoneCamRequireCode_) {
            ImGui::Text("Pairing code: %s", phoneCamCode_.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("New code")) {
                phoneCamCode_ = phonecam::newPairCode();
                saveGlobalConfig();
                statusMessage_ =
                    "New pairing code - restart the link for it to take effect";
            }
        }
        ImGui::TextDisabled("The phone app is github.com/doctorspider42/tyrax-cam\n"
                            "(sideloaded). Opening the http:// address in any\n"
                            "browser gives a test client that shows the same\n"
                            "stream and can fake a pose.");
    }

    // --- the connected device ----------------------------------------------
    if (live) {
        ImGui::SeparatorText("Device");
        if (!dev.model.empty()) ImGui::Text("Model: %s", dev.model.c_str());
        if (!dev.client.empty()) ImGui::Text("App: %s", dev.client.c_str());
        if (!dev.address.empty()) ImGui::Text("Address: %s", dev.address.c_str());
        ImGui::Text("Tracking: %s",
                    dev.sixDof ? "6DoF (position + rotation)" : "rotation only");
        if (!dev.sixDof && ImGui::IsItemHovered())
            ImGui::SetTooltip("This device reports no world position, so the camera\n"
                              "turns in place instead of walking.");
        const double age = ImGui::GetTime() - phonePoseAt_;
        if (phoneHasPose_ && age < 1.0) {
            ImGui::Text("Stream: %llu poses", (unsigned long long)phoneCam_.poseCount());
            ImGui::Text("Pose: %.2f %.2f %.2f m", phonePose_.pos[0], phonePose_.pos[1],
                        phonePose_.pos[2]);
            ImGui::Text("Camera: %.1f %.1f %.1f", phoneEye_[0], phoneEye_[1],
                        phoneEye_[2]);
        } else {
            ImGui::TextDisabled(phoneHasPose_ ? "Stream stalled" : "No pose yet");
        }
    }

    // --- mapping ------------------------------------------------------------
    ImGui::SeparatorText("Camera");
    // Which Camera entity the phone views from AND records into - one control,
    // because they are one intent. Picking one jumps the phone to where that
    // camera was placed, and Recentre returns there.
    {
        const std::string label =
            phoneRecTarget_.empty() ? "Free shots (from the editor view)"
                                    : phoneRecTarget_;
        ImGui::BeginDisabled(phoneRec_);
        ImGui::SetNextItemWidth(scaled(230.0f));
        if (ImGui::BeginCombo("View from", label.c_str())) {
            if (ImGui::Selectable("Free shots (from the editor view)",
                                  phoneRecTarget_.empty()))
                selectPhoneCamera(std::string());
            bool any = false;
            for (const SceneObject& o : project_.objects())
                if (o.type == PrimitiveType::Camera) any = true;
            if (any) ImGui::Separator();
            for (const SceneObject& o : project_.objects())
                if (o.type == PrimitiveType::Camera)
                    if (ImGui::Selectable(o.name.c_str(), phoneRecTarget_ == o.name))
                        selectPhoneCamera(o.name);
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(
                phoneRec_ ? "Locked while recording."
                          : "The phone starts from this camera's placed pose -\n"
                            "position, aim and tilt - and Recentre returns there.\n"
                            "The recording goes into the same camera's track.\n"
                            "Also selectable on the phone.");
        if (!phoneRecTarget_.empty() && !phoneStartCamera())
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f),
                               "\"%s\" is not in this scene - using the editor view.",
                               phoneRecTarget_.c_str());
    }
    if (ImGui::Checkbox("Drive the viewport camera", &phoneDrive_)) {
        if (!phoneDrive_) phoneCamPushed_ = false;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The phone's motion moves the editor camera (and so the\n"
                          "image it is streaming). Off leaves the viewport under\n"
                          "your own control while the link stays up.");
    ImGui::SetNextItemWidth(scaled(140.0f));
    ImGui::DragFloat("Scale", &phoneMap_.scale, 0.02f, 0.01f, 1000.0f, "%.2f u/m");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Game units per real metre. Seeded from the project's\n"
                          "world scale when the phone connects; raise it to cover\n"
                          "more map from the same room.");
    // Same affordance as the take-import modal: the project's world scale is the
    // right answer, so hand-tuning it away needs one click back.
    if (phoneMap_.scale != project_.settings.unitsPerMeter) {
        ImGui::SameLine();
        if (ImGui::SmallButton("World scale"))
            phoneMap_.scale = project_.settings.unitsPerMeter;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Back to the project's %.3f units per meter\n"
                              "(Project Preferences > World).",
                              project_.settings.unitsPerMeter);
    }
    ImGui::SameLine(0.0f, scaled(14.0f));
    if (ImGui::Button("Recentre")) phoneCamRecenter();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(phoneRecTarget_.empty()
                              ? "Back to the editor's own camera, aimed where the\n"
                                "viewport is looking. Everything the phone does\n"
                                "is relative to this point."
                              : "Back to where the selected camera was PLACED -\n"
                                "its position, aim and tilt. Everything the phone\n"
                                "does is relative to that pose, and Move only\n"
                                "offsets from it.");
    // The start point, editable directly: Recentre snaps it to the viewport
    // camera, but a shot often wants an exact spot - and the phone can fly it
    // too (its Move mode), which writes the same three numbers.
    if (ImGui::DragFloat3("Start point", phoneMap_.origin, 0.1f))
        phoneMap_.hasAnchor = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Where the phone's motion starts from. Drag to place it\n"
                          "exactly, or fly it live from the app (its Move button).\n"
                          "Moving this slides the camera bodily; the phone's own\n"
                          "motion stays relative to wherever it ends up.");
    ImGui::SameLine();
    if (ImGui::SmallButton("From view")) {
        float eye[3], at[3];
        viewport_.currentCamera(eye, at);
        for (int c = 0; c < 3; ++c) phoneMap_.origin[c] = eye[c];
        phoneMap_.hasAnchor = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Move the start point to the editor camera, leaving the\n"
                          "aim alone (Recentre does both).");
    ImGui::SetNextItemWidth(scaled(140.0f));
    ImGui::DragFloat("Yaw", &phoneMap_.yawDeg, 1.0f, -360.0f, 360.0f, "%.0f deg");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Which way the phone's forward points in the scene.\n"
                          "Recentre sets this from the viewport view.");
    ImGui::SetNextItemWidth(scaled(140.0f));
    ImGui::SliderFloat("Tilt", &phoneMap_.rollScale, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("How much of the phone's own tilt about the lens axis\n"
                          "reaches the shot (the Dutch angle). 1 films the lean\n"
                          "as held, 0 pins the horizon level and throws hand\n"
                          "tremble away. Recentre makes the way you are holding\n"
                          "it right now count as level.");
    ImGui::SameLine();
    ImGui::Text("roll %.1f deg", phoneRoll_);

    // --- preview stream -----------------------------------------------------
    ImGui::SeparatorText("Preview stream");
    bool prefsChanged = false;
    static const char* kSizes[] = {"256", "320", "480", "640", "960"};
    static const int kSizeVals[] = {256, 320, 480, 640, 960};
    int sizeIdx = 2;
    for (int i = 0; i < 5; ++i)
        if (phoneCamPrefs_.maxWidth == kSizeVals[i]) sizeIdx = i;
    ImGui::SetNextItemWidth(scaled(90.0f));
    if (ImGui::Combo("Max size", &sizeIdx, kSizes, 5)) {
        phoneCamPrefs_.maxWidth = phoneCamPrefs_.maxHeight = kSizeVals[sizeIdx];
        prefsChanged = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Long-edge cap of the streamed image. The phone can ask\n"
                          "for its own value, which then wins.");
    ImGui::SameLine(0.0f, scaled(14.0f));
    ImGui::SetNextItemWidth(scaled(110.0f));
    if (ImGui::SliderInt("fps", &phoneCamPrefs_.fps, 1, 30)) prefsChanged = true;
    ImGui::SetNextItemWidth(scaled(140.0f));
    if (ImGui::SliderInt("JPEG quality", &phoneCamPrefs_.quality, 20, 90))
        prefsChanged = true;
    ImGui::SetNextItemWidth(scaled(140.0f));
    if (ImGui::SliderInt("Smoothing", &phoneCamPrefs_.smoothing, 0,
                         phonecam::kMaxSmoothing,
                         phoneCamPrefs_.smoothing == 0 ? "%d (lowest latency)" : "%d"))
        prefsChanged = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Frames allowed in flight beyond the one being sent.\n"
            "0 is the least delay but the most stutter: one hitch in\n"
            "encoding or on the Wi-Fi costs a whole frame interval,\n"
            "because the next grab window is missed. 1-2 keeps the\n"
            "cadence even for that many frames of delay (~%.0f ms each\n"
            "at %d fps). The oldest frame is dropped when full, so the\n"
            "delay never grows past this.",
            1000.0f / (float)(phoneCamPrefs_.fps > 0 ? phoneCamPrefs_.fps : 15),
            phoneCamPrefs_.fps);
    if (prefsChanged) {
        phoneCam_.setPreviewDefaults(phoneCamPrefs_);
        saveGlobalConfig();
    }

    ImGui::Separator();
    if (phoneRec_) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.4f, 1.0f), "RECORDING");
        ImGui::SameLine();
        ImGui::Text("%.2f s, %d keys", (float)phoneTake_.duration(),
                    phoneRecStats_.keyCount);
        ImGui::SameLine();
        if (ImGui::SmallButton("Stop")) stopPhoneRecording();
    } else {
        ImGui::TextDisabled("Record the move into keyframes in\n"
                            "Tools > Cutscene Director > Phone camera.");
    }
    ImGui::End();
}

void App::phoneCamPushPreview() {
    if (!phoneCam_.connected() || !phoneCam_.previewWanted()) return;
    const phonecam::PreviewPrefs p = phoneCam_.preview();
    const double now = ImGui::GetTime();
    const double period = 1.0 / (double)(p.fps > 0 ? p.fps : 15);
    if (now - phonePreviewAt_ < period) return;
    phonePreviewAt_ = now;
    std::vector<unsigned char> rgb;
    int w = 0, h = 0;
    if (viewport_.grabPreviewRgb(p.maxWidth, p.maxHeight, rgb, w, h))
        phoneCam_.pushPreview(w, h, rgb.data());
}

// Poses a copy of the active scene's objects at the playhead using the SAME
// interpolation the PS2 runtime uses (sequence.hpp seqSample/seqEase), and -
// for a sequence with a camera track - flies the viewport camera along it. So
// scrubbing the timeline shows exactly what the console will render.
const std::vector<SceneObject>& App::cutscenePosedObjects() {
    // Pose the scene whenever the Director is open on a valid sequence. The
    // "Preview in viewport" toggle only controls whether the sequence drives
    // the VIEWPORT CAMERA (and the bars/fade overlay) - with it off, objects
    // (including a Camera entity dollying along its track) still animate, so
    // you can watch the move from a free vantage point.
    const bool active = showCutsceneEditor_ && hasProject_ &&
                        selectedSequence_ >= 0 &&
                        selectedSequence_ < (int)project_.sequences.size();
    if (!active) {
        if (seqCameraPushed_) {
            viewport_.clearCameraOverride();
            seqCameraPushed_ = false;
        }
        seqBarsStyleNow_ = 0;
        seqBarsNow_ = 0.0f;
        seqFadeNow_ = 0.0f;
        return project_.objects();
    }

    const Sequence& s = project_.sequences[selectedSequence_];
    const float t = seqPlayhead_;
    seqPosed_ = project_.objects();  // copy the active scene's objects

    // Editor-hidden layers stay hidden; cutscene visibility keys add to that.
    std::vector<char> hidden(seqPosed_.size(), 0);
    for (size_t i = 0; i < seqPosed_.size(); ++i)
        hidden[i] = isObjectHiddenInEditor(seqPosed_[i]) ? 1 : 0;

    // While paused, SELECTED objects keep their real (static) transform so
    // the gizmo edits what you see - otherwise posing an object between two
    // keys is blind (the track keeps snapping the preview back). Playback
    // poses everything. Bound camera shots read seqPosed_, so aiming a
    // selected Camera entity updates its shot live too.
    std::vector<char> editing(seqPosed_.size(), 0);
    if (!seqPlaying_)
        for (int sel : selection_)
            if (sel >= 0 && sel < (int)editing.size()) editing[sel] = 1;

    for (const SeqTrack& tr : s.tracks) {
        int idx = -1;
        for (size_t i = 0; i < seqPosed_.size(); ++i)
            if (seqPosed_[i].name == tr.target) {
                idx = (int)i;
                break;
            }
        if (idx < 0 || tr.keys.empty()) continue;
        if (editing[idx]) continue;  // selected: leave it editable

        std::vector<SeqObjectKey> keys = tr.keys;
        std::sort(keys.begin(), keys.end(),
                  [](const SeqObjectKey& a, const SeqObjectKey& b) {
                      return a.time < b.time;
                  });
        const int n = (int)keys.size();
        std::vector<float> times(n);
        std::vector<int> eas(n);
        for (int i = 0; i < n; ++i) times[i] = keys[i].time, eas[i] = keys[i].easing;
        auto samp = [&](std::function<float(const SeqObjectKey&)> g) {
            std::vector<float> v(n);
            for (int i = 0; i < n; ++i) v[i] = g(keys[i]);
            return seqSample(times.data(), v.data(), eas.data(), n, t);
        };

        SceneObject& o = seqPosed_[idx];
        if (tr.animPos)
            for (int c = 0; c < 3; ++c)
                o.position[c] = samp([c](const SeqObjectKey& k) { return k.position[c]; });
        if (tr.animRot)
            for (int c = 0; c < 3; ++c)
                o.rotation[c] = samp([c](const SeqObjectKey& k) { return k.rotation[c]; });
        if (tr.animScale)
            for (int c = 0; c < 3; ++c)
                o.scale[c] = samp([c](const SeqObjectKey& k) { return k.scale[c]; });
        if (tr.animColor)
            for (int c = 0; c < 3; ++c)
                o.color[c] = samp([c](const SeqObjectKey& k) { return k.color[c]; });
        if (tr.animVis) {
            int j = 0;
            while (j < n - 1 && t >= keys[j + 1].time) ++j;
            if (!keys[j].visible) hidden[idx] = 1;  // steps between keys
        }
    }
    viewport_.setHiddenMask(std::move(hidden));

    // Camera track: fly the preview camera (or release it back to the orbit).
    // Each key is a shot - free (stored eye/at/fov) or bound to a Camera
    // entity, in which case eye/at/fov come from the entity's CURRENT pose in
    // seqPosed_ (object tracks already ran, so an animated camera entity gives
    // a dolly shot). Shots blend across the segment; Step easing = hard cut.
    // The exact same resolution runs in the generated PS2 player.
    if (seqPreview_ && s.cameraEnabled && !s.cameraKeys.empty()) {
        std::vector<SeqCameraKey> ck = s.cameraKeys;
        std::sort(ck.begin(), ck.end(),
                  [](const SeqCameraKey& a, const SeqCameraKey& b) {
                      return a.time < b.time;
                  });
        const int n = (int)ck.size();
        auto shot = [&](int i, float eye[3], float at[3], float& fov, float& roll) {
            const SeqCameraKey& k = ck[i];
            const SceneObject* cam = nullptr;
            if (!k.camera.empty())
                for (const SceneObject& o : seqPosed_)
                    if (o.name == k.camera && o.type == PrimitiveType::Camera) {
                        cam = &o;
                        break;
                    }
            if (cam) {
                float fwd[3];
                seqCameraForward(cam->rotation, fwd);
                for (int c = 0; c < 3; ++c) {
                    eye[c] = cam->position[c];
                    at[c] = cam->position[c] + fwd[c];
                }
                fov = cam->cameraFov;
                // A bound shot's tilt is the entity's own orientation, turned
                // into the same scalar channel free shots interpolate - exactly
                // what the generated player's rollOf does.
                float eu[3];
                seqCameraUpFromEuler(cam->rotation, eu);
                roll = seqRollFromUp(fwd, eu);
            } else {
                for (int c = 0; c < 3; ++c) eye[c] = k.eye[c], at[c] = k.target[c];
                fov = k.fov;
                roll = k.roll;
            }
        };
        int i = 0;
        while (i < n - 1 && t >= ck[i + 1].time) ++i;
        float e0[3], a0[3], f0, r0, shake;
        shot(i, e0, a0, f0, r0);
        shake = ck[i].shake;
        if (t > ck[i].time && i < n - 1) {
            const float span = ck[i + 1].time - ck[i].time;
            const float u = span > 1e-6f ? (t - ck[i].time) / span : 0.0f;
            const float w = seqEase(ck[i].easing, u);
            float e1[3], a1[3], f1, r1;
            shot(i + 1, e1, a1, f1, r1);
            for (int c = 0; c < 3; ++c) {
                e0[c] += (e1[c] - e0[c]) * w;
                a0[c] += (a1[c] - a0[c]) * w;
            }
            f0 += (f1 - f0) * w;
            shake += (ck[i + 1].shake - shake) * w;
            // Short way round, like the generated player.
            float dr = r1 - r0;
            while (dr > 180.0f) dr -= 360.0f;
            while (dr < -180.0f) dr += 360.0f;
            r0 += dr * w;
        }
        if (shake > 0.0f) {
            float off[3];
            seqShakeOffset(t, shake, off);
            for (int c = 0; c < 3; ++c) e0[c] += off[c], a0[c] += off[c];
        }
        viewport_.setCameraOverride(e0, a0, f0, r0);
        seqCameraPushed_ = true;
    } else if (seqCameraPushed_) {
        viewport_.clearCameraOverride();
        seqCameraPushed_ = false;
    }

    // Widescreen bars + fades preview, overlaid on the viewport image where it
    // is drawn (same envelope math as the PS2 player). Only while the sequence
    // drives the viewport camera - with preview off you watch from outside.
    if (seqPreview_) {
        seqBarsStyleNow_ = s.bars;
        seqBarsNow_ = s.bars != kSeqBarsNone
                          ? seqBarsAmount(t, s.duration, s.barsSlideIn, s.barsSlideOut)
                          : 0.0f;
        seqFadeNow_ = seqFadeAlpha(t, s.duration, s.fadeIn, s.fadeOut);
    } else {
        seqBarsStyleNow_ = 0;
        seqBarsNow_ = 0.0f;
        seqFadeNow_ = 0.0f;
    }

    return seqPosed_;
}

// Cutscene Director window (Tools > Cutscene Director): sequence list on the
// left, the selected sequence's timeline on the right - object tracks (each a
// list of pose keyframes) plus an optional camera track. The playhead scrubs
// the whole scene live in the viewport; keys are authored by posing an object
// (or the view) and snapshotting it at the playhead.
void App::drawCutsceneWindow() {
    if (!showCutsceneEditor_ || !hasProject_) return;

    ImGui::SetNextWindowSize(ImVec2(scaled(880), scaled(620)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Cutscene Director", &showCutsceneEditor_)) {
        ImGui::End();
        return;
    }

    bool changed = false;
    // Belt and braces (the Save/Menu Editor idiom): `changed` is set by hand at
    // each widget, so the next one added to this very long window can forget
    // it. The section comparison cannot be forgotten. Track/shot edits that
    // move OBJECTS live in project_.scenes, which the undo snapshot covers.
    const std::string beforeSection =
        project::sectionJson(project_, project::Section::Sequences);
    auto commitIfEdited = [&] {
        if (changed ||
            project::sectionJson(project_, project::Section::Sequences) != beforeSection)
            commitChange();
    };
    auto uniqueSeqName = [&](std::string base) {
        std::string n = base;
        for (int k = 2;; ++k) {
            bool taken = false;
            for (const auto& s : project_.sequences) taken |= (s.name == n);
            if (!taken) return n;
            n = base + "-" + std::to_string(k);
        }
    };

    // --- left: sequence list ------------------------------------------------
    ImGui::BeginChild("##seq_list", ImVec2(scaled(170), 0), ImGuiChildFlags_Borders);
    if (ImGui::Button("+ New sequence", ImVec2(-1, 0))) {
        Sequence s;
        s.name = uniqueSeqName("Cutscene");
        project_.sequences.push_back(std::move(s));
        selectedSequence_ = (int)project_.sequences.size() - 1;
        selectedSeqTrack_ = -1;
        seqPlayhead_ = 0.0f;
        changed = true;
    }
    ImGui::Separator();
    for (int i = 0; i < (int)project_.sequences.size(); ++i) {
        ImGui::PushID(i);
        if (ImGui::Selectable(project_.sequences[i].name.c_str(), selectedSequence_ == i)) {
            selectedSequence_ = i;
            selectedSeqTrack_ = -1;
            seqPlayhead_ = 0.0f;
        }
        ImGui::PopID();
    }
    if (project_.sequences.empty())
        ImGui::TextDisabled("No cutscenes yet.\nA sequence poses objects\n"
                            "+ the camera over time,\nfired by the Play\n"
                            "Sequence flow node.");
    ImGui::EndChild();
    ImGui::SameLine();

    // --- right: selected sequence -------------------------------------------
    ImGui::BeginChild("##seq_edit", ImVec2(0, 0));
    if (selectedSequence_ < 0 || selectedSequence_ >= (int)project_.sequences.size()) {
        ImGui::TextDisabled("Select a cutscene on the left (or create one).");
        ImGui::TextDisabled("\nPlay it in the game with the Play Sequence flow\n"
                            "node (category \"Scene\"); Stop Sequence ends it.");
        ImGui::EndChild();
        ImGui::End();
        commitIfEdited();
        return;
    }
    Sequence& s = project_.sequences[selectedSequence_];

    char nameBuf[64];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", s.name.c_str());
    ImGui::SetNextItemWidth(scaled(180.0f));
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
        for (SceneData& sc : project_.scenes)
            for (SceneObject& o : sc.objects)
                for (FlowNode& fn : o.flowGraph.nodes) {
                    const FlowNodeType* ft = flowNodeType(fn.type);
                    if (ft && ft->strKind == FlowParamKind::SequenceName && fn.str == s.name)
                        fn.str = nameBuf;
                }
        s.name = nameBuf;
    }
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    if (ImGui::SmallButton("Duplicate")) {
        Sequence copy = s;
        copy.name = uniqueSeqName(s.name);
        project_.sequences.push_back(std::move(copy));
        selectedSequence_ = (int)project_.sequences.size() - 1;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete")) {
        for (SceneData& sc : project_.scenes)
            for (SceneObject& o : sc.objects)
                for (FlowNode& fn : o.flowGraph.nodes) {
                    const FlowNodeType* ft = flowNodeType(fn.type);
                    if (ft && ft->strKind == FlowParamKind::SequenceName && fn.str == s.name)
                        fn.str.clear();
                }
        project_.sequences.erase(project_.sequences.begin() + selectedSequence_);
        selectedSequence_ = -1;
        selectedSeqTrack_ = -1;
        commitChange();
        ImGui::EndChild();
        ImGui::End();
        return;
    }

    ImGui::SetNextItemWidth(scaled(110.0f));
    if (ImGui::DragFloat("Duration (s)", &s.duration, 0.1f, 0.1f, 600.0f, "%.2f"))
        changed = true;
    if (s.duration < 0.1f) s.duration = 0.1f;
    ImGui::SameLine(0.0f, scaled(14.0f));
    if (ImGui::Checkbox("Loop", &s.loop)) changed = true;
    ImGui::SameLine(0.0f, scaled(14.0f));
    if (ImGui::Checkbox("Skippable", &s.skippable)) changed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Pressing START in the game ends the cutscene early.");
    ImGui::SameLine(0.0f, scaled(14.0f));
    if (ImGui::Checkbox("Camera track", &s.cameraEnabled)) changed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Drive the game camera from the camera lane's shots\n"
                          "for the duration of playback.");
    ImGui::SameLine(0.0f, scaled(14.0f));
    if (ImGui::Checkbox("Hide player", &s.hidePlayer)) changed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Hide the third-person player avatar while the cutscene\n"
                          "plays (no effect in FPP/noclip - they have no body).");

    // Cinematic dressing: widescreen masks + fades, composited over the frame
    // (and the HUD) on the PS2 and previewed on the viewport image.
    static const char* kBarsNames[] = {"None", "Cinema 2.39:1", "Wide 16:9",
                                       "Pillarbox", "Frame"};
    ImGui::SetNextItemWidth(scaled(130.0f));
    if (ImGui::Combo("Widescreen bars", &s.bars, kBarsNames, kSeqBarsStyleCount))
        changed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Solid black masks while the cutscene plays; they\n"
                          "slide in at the start and out before the end (times\n"
                          "below; 0 = they appear/vanish instantly).");
    // Bars slide-in/out times, only meaningful when bars are on. Authored
    // just like the fades, right next to them.
    if (s.bars != kSeqBarsNone) {
        ImGui::SameLine(0.0f, scaled(14.0f));
        ImGui::SetNextItemWidth(scaled(76.0f));
        if (ImGui::DragFloat("Bars in", &s.barsSlideIn, 0.05f, 0.0f, 10.0f, "%.2f s"))
            changed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How long the bars take to slide in at the start.\n"
                              "0 = they are there from the first frame.");
        ImGui::SameLine(0.0f, scaled(10.0f));
        ImGui::SetNextItemWidth(scaled(76.0f));
        if (ImGui::DragFloat("Bars out", &s.barsSlideOut, 0.05f, 0.0f, 10.0f, "%.2f s"))
            changed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How long the bars take to slide out before the\n"
                              "end. 0 = they stay until the last frame.");
        if (s.barsSlideIn < 0.0f) s.barsSlideIn = 0.0f;
        if (s.barsSlideOut < 0.0f) s.barsSlideOut = 0.0f;
    }
    ImGui::SetNextItemWidth(scaled(76.0f));
    if (ImGui::DragFloat("Fade in", &s.fadeIn, 0.05f, 0.0f, 10.0f, "%.2f s"))
        changed = true;
    ImGui::SameLine(0.0f, scaled(10.0f));
    ImGui::SetNextItemWidth(scaled(76.0f));
    if (ImGui::DragFloat("Fade out", &s.fadeOut, 0.05f, 0.0f, 10.0f, "%.2f s"))
        changed = true;
    if (s.fadeIn < 0.0f) s.fadeIn = 0.0f;
    if (s.fadeOut < 0.0f) s.fadeOut = 0.0f;

    // --- Adjust imported take -----------------------------------------------
    // After a take is imported the recording + mapping stay loaded, so the
    // whole path can be re-positioned and re-oriented in place (start point,
    // start yaw, scale) without re-importing. Re-bakes the same target.
    if (seqTakeActive_ && seqTakeSeqIdx_ == selectedSequence_ &&
        !seqTake_.samples.empty()) {
        const std::string what =
            seqTakeTarget_.empty() ? std::string("free camera shots")
                                   : ("camera \"" + seqTakeTarget_ + "\"");
        if (ImGui::CollapsingHeader("Adjust imported take",
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("Re-positions the imported %s.", what.c_str());
            bool rebake = false;
            if (ImGui::DragFloat3("Start point", seqTakeMap_.origin, 0.1f)) rebake = true;
            ImGui::SameLine();
            if (ImGui::SmallButton("From view")) {
                takeOriginAimFromView();  // position + aim
                rebake = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Move the start point to the editor camera AND\n"
                                  "aim the path where you are looking.");
            ImGui::SetNextItemWidth(scaled(140.0f));
            if (ImGui::DragFloat("Start yaw", &seqTakeMap_.yawDeg, 1.0f, -360.0f,
                                 360.0f, "%.0f deg"))
                rebake = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Rotates the whole path about the up axis,\n"
                                  "pivoting on the start point.");
            ImGui::SameLine(0.0f, scaled(14.0f));
            ImGui::SetNextItemWidth(scaled(120.0f));
            if (ImGui::DragFloat("Scale", &seqTakeMap_.scale, 0.02f, 0.01f, 1000.0f,
                                 "%.2f u/m"))
                rebake = true;
            ImGui::SameLine(0.0f, scaled(14.0f));
            if (ImGui::SmallButton("Done")) {
                seqTakeActive_ = false;  // stop tracking; keys stay
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Finish adjusting - the keys become ordinary\n"
                                  "editable keyframes.");
            if (rebake) {
                applyCamTake(s, true);
                const float lastT =
                    s.cameraKeys.empty() ? 0.0f : s.cameraKeys.back().time;
                if (lastT > s.duration) s.duration = lastT;
                changed = true;
            }
        }
    }

    // --- Phone camera (live recording) --------------------------------------
    // The other half of the phone story: instead of importing a finished file,
    // record the move as it happens (docs/phone-camera.md). The link itself
    // lives in Tools > Phone Camera; this is where a take lands in a sequence.
    if (ImGui::CollapsingHeader("Phone camera",
                                phoneRec_ ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        if (!phoneCam_.listening()) {
            ImGui::TextDisabled("Hold your phone, watch the shot on its screen and\n"
                                "record the camera move straight into this cutscene.");
            if (ImGui::Button("Open Phone Camera...")) showPhoneCamWindow_ = true;
        } else {
            // Target: a Camera entity (a real dolly the game films from) or the
            // camera lane's free shots - same choice the take importer offers.
            bool anyCam = false;
            for (const SceneObject& o : project_.objects())
                if (o.type == PrimitiveType::Camera) anyCam = true;
            const std::string targetLabel =
                phoneRecTarget_.empty() ? "Free camera shots" : phoneRecTarget_;
            ImGui::SetNextItemWidth(scaled(190.0f));
            if (ImGui::BeginCombo("Into", targetLabel.c_str())) {
                if (ImGui::Selectable("Free camera shots", phoneRecTarget_.empty()))
                    phoneRecTarget_.clear();
                if (anyCam) ImGui::Separator();
                for (const SceneObject& o : project_.objects())
                    if (o.type == PrimitiveType::Camera)
                        if (ImGui::Selectable(o.name.c_str(), phoneRecTarget_ == o.name))
                            phoneRecTarget_ = o.name;
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("A Camera entity records into its transform track, so\n"
                                  "the game films from a camera that dollies along your\n"
                                  "path. Free shots write the camera lane directly.");

            // Keyframe density. "Project frame rate" is the sync-with-the-game
            // option: one key per rendered frame, so nothing is interpolated.
            static const char* kDensNames[] = {"Project frame rate", "Custom rate",
                                               "Optimize (tolerance)"};
            ImGui::SetNextItemWidth(scaled(190.0f));
            ImGui::Combo("Keyframes", &phoneDensityMode_, kDensNames, 3);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("How densely the move is sampled into keys.\n"
                                  "Project frame rate = one key per game frame (exact,\n"
                                  "biggest table). Custom rate = keys per second.\n"
                                  "Optimize = no fixed rate; drop keys the PS2's own\n"
                                  "interpolation already reproduces within an error\n"
                                  "bound (the smallest table).");
            ImGui::SameLine(0.0f, scaled(12.0f));
            if (phoneDensityMode_ == 0) {
                ImGui::Text("%.0f keys/s", projectFrameRate());
            } else if (phoneDensityMode_ == 1) {
                ImGui::SetNextItemWidth(scaled(120.0f));
                ImGui::DragFloat("##dens", &phoneDensity_, 0.5f, 0.5f, 60.0f,
                                 "%.1f keys/s");
            } else {
                ImGui::SetNextItemWidth(scaled(120.0f));
                ImGui::SliderFloat("##tol", &phoneTolerance_, 0.005f, 1.0f, "%.3f u",
                                   ImGuiSliderFlags_Logarithmic);
            }
            ImGui::Checkbox("Start at playhead", &phoneRecAtPlayhead_);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Recorded keys begin at the playhead instead of at\n"
                                  "t = 0, so a move can be dropped after an existing\n"
                                  "shot.");

            if (phoneRec_) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.13f, 0.17f, 1.0f));
                if (ImGui::Button("Stop recording", ImVec2(scaled(140), 0)))
                    stopPhoneRecording();
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.4f, 1.0f),
                                   "REC  %.2f s  %d keys", (float)phoneTake_.duration(),
                                   phoneRecStats_.keyCount);
                if (phoneRecSeq_ != selectedSequence_)
                    ImGui::TextDisabled("(recording into \"%s\")",
                                        phoneRecSeq_ >= 0 &&
                                                phoneRecSeq_ < (int)project_.sequences.size()
                                            ? project_.sequences[phoneRecSeq_].name.c_str()
                                            : "?");
                if (phoneRecStats_.keyCount > 600)
                    ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f),
                                       "%d keys is a big table - consider a lower "
                                       "density.", phoneRecStats_.keyCount);
            } else {
                const bool canRec = phoneCam_.connected() && phoneHasPose_;
                ImGui::BeginDisabled(!canRec);
                if (ImGui::Button("Record", ImVec2(scaled(140), 0)))
                    startPhoneRecording();
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (!phoneCam_.connected())
                    ImGui::TextDisabled("Waiting for the phone to connect...");
                else if (!phoneHasPose_)
                    ImGui::TextDisabled("Waiting for a pose...");
                else
                    ImGui::TextDisabled("The phone's REC button does this too.");
            }
        }
    }

    // "Import take...": pick a phone-recorded 6DoF camera take (CamTrackAR
    // .hfcs or the canonical CSV - docs/camera-takes.md) and stage it for the
    // mapping modal below. The default landing point is the preview camera,
    // so the path starts where the user is looking.
    auto beginTakeImport = [&]() {
        const std::string path = pickPath(PickKind::CamTake);
        if (path.empty()) return;
        seqTakePath_ = path;
        seqTakeError_.clear();
        if (!loadCamTakeAuto(path, seqTake_, seqTakeError_)) seqTake_ = CamTake{};
        seqTakeMap_.yawDeg = 0.0f;
        // A take is recorded in meters, so the project's world scale IS the
        // mapping scale (docs/world-scale.md). The decimation tolerance is a
        // world-unit distance, so it follows the same factor - otherwise a
        // big-scale project decimates a path it should have kept.
        seqTakeMap_.scale = project_.settings.unitsPerMeter;
        seqTakeMap_.tolerance = 0.05f * project_.settings.unitsPerMeter;
        takeOriginAimFromView();  // land + aim at the current view by default
        seqTakeMap_.timeOffset = 0.0f;
        // default the target to the camera you're looking through, or the
        // first Camera entity (a take always bakes into a camera now)
        auto isCam = [&](const std::string& n) {
            for (const SceneObject& o : project_.objects())
                if (o.name == n && o.type == PrimitiveType::Camera) return true;
            return false;
        };
        if (seqTakeTarget_.empty() || !isCam(seqTakeTarget_)) {
            seqTakeTarget_.clear();
            if (!lookThroughCam_.empty() && isCam(lookThroughCam_))
                seqTakeTarget_ = lookThroughCam_;
            else
                for (const SceneObject& o : project_.objects())
                    if (o.type == PrimitiveType::Camera) {
                        seqTakeTarget_ = o.name;
                        break;
                    }
        }
        seqTakeDirty_ = true;
        seqTakeOpen_ = true;
    };

    // --- transport -----------------------------------------------------------
    ImGui::SeparatorText("Timeline");
    // Space toggles play/stop while the Cutscene Director is focused (but not
    // while typing in a field or dragging a widget - Space also clicks a
    // focused button, so skip when an item is active to avoid a double toggle).
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() &&
        ImGui::IsKeyPressed(ImGuiKey_Space, false))
        seqPlaying_ = !seqPlaying_;
    if (ImGui::Button(seqPlaying_ ? "Pause" : "Play")) seqPlaying_ = !seqPlaying_;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play / pause (Space)");
    ImGui::SameLine();
    if (ImGui::Button("Rewind")) {
        seqPlayhead_ = 0.0f;
        seqPlaying_ = false;
    }
    ImGui::SameLine(0.0f, scaled(14.0f));
    ImGui::Checkbox("Preview in viewport", &seqPreview_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Pose the scene at the playhead. Objects you have\n"
                          "SELECTED stay at their real transform while paused,\n"
                          "so the gizmo edits what you see - snapshot or\n"
                          "auto-key to turn that pose into a keyframe.");
    ImGui::SameLine(0.0f, scaled(14.0f));
    ImGui::Checkbox("Auto-key", &seqAutoKey_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Finishing a gizmo drag drops a keyframe at the\n"
                          "playhead for every selected object that has a\n"
                          "track in this sequence.");
    ImGui::SameLine(0.0f, scaled(14.0f));
    ImGui::SetNextItemWidth(scaled(100.0f));
    ImGui::SliderFloat("Zoom", &seqZoom_, 1.0f, 8.0f, "%.1fx");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Timeline horizontal zoom (also Ctrl + mouse wheel\n"
                          "over the dopesheet).");
    ImGui::SameLine(0.0f, scaled(14.0f));
    ImGui::Text("t = %.2f s", seqPlayhead_);
    ImGui::SameLine(0.0f, 14.0f);
    if (ImGui::SmallButton("Import take...")) beginTakeImport();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Import a phone-recorded 6DoF camera move (CamTrackAR\n"
                          ".hfcs or the CSV spec in docs/camera-takes.md) as free\n"
                          "camera shots on the camera lane.");
    if (seqPlaying_) {
        seqPlayhead_ += ImGui::GetIO().DeltaTime;
        if (seqPlayhead_ >= s.duration) {
            if (s.loop)
                seqPlayhead_ = std::fmod(seqPlayhead_, s.duration);
            else {
                seqPlayhead_ = s.duration;
                seqPlaying_ = false;
            }
        }
    }
    if (seqPlayhead_ < 0.0f) seqPlayhead_ = 0.0f;
    if (seqPlayhead_ > s.duration) seqPlayhead_ = s.duration;

    static const char* kEaseNames[] = {"Linear", "Smooth", "Step (hold)"};

    // Snapshot helpers. A key within 1/60 s of the requested time is replaced
    // (keeping its easing and, for camera keys, the shot binding/fov/shake).
    auto sortObjKeys = [](SeqTrack& tr) {
        std::sort(tr.keys.begin(), tr.keys.end(),
                  [](const SeqObjectKey& a, const SeqObjectKey& b) {
                      return a.time < b.time;
                  });
    };
    auto sortCamKeys = [&]() {
        std::sort(s.cameraKeys.begin(), s.cameraKeys.end(),
                  [](const SeqCameraKey& a, const SeqCameraKey& b) {
                      return a.time < b.time;
                  });
    };
    auto snapshotObjectKey = [&](SeqTrack& tr, float time) {
        return cutsceneSnapshotObjectKey(tr, time);
    };
    // The camera to film a new shot from: the one you're looking through, else
    // a selected Camera, else the only Camera in the scene, else "" (none).
    auto activeCameraName = [&]() -> std::string {
        auto isCam = [&](const std::string& n) {
            for (const SceneObject& o : project_.objects())
                if (o.name == n && o.type == PrimitiveType::Camera) return true;
            return false;
        };
        if (!lookThroughCam_.empty() && isCam(lookThroughCam_)) return lookThroughCam_;
        if (selectedObject_ >= 0 && selectedObject_ < (int)project_.objects().size() &&
            project_.objects()[selectedObject_].type == PrimitiveType::Camera)
            return project_.objects()[selectedObject_].name;
        std::string first;
        for (const SceneObject& o : project_.objects())
            if (o.type == PrimitiveType::Camera) {
                first = o.name;
                break;
            }
        return first;
    };
    // Adds/updates a camera-lane shot at `time`, bound to the active camera.
    // Returns false when the scene has no Camera entity to film from.
    auto snapshotCameraKey = [&](float time) -> bool {
        const std::string cam = activeCameraName();
        if (cam.empty()) return false;
        SeqCameraKey k;
        k.time = time;
        k.camera = cam;
        int repl = -1;
        for (int i = 0; i < (int)s.cameraKeys.size(); ++i)
            if (std::fabs(s.cameraKeys[i].time - k.time) < 0.017f) repl = i;
        if (repl >= 0) {
            k.easing = s.cameraKeys[repl].easing;
            k.shake = s.cameraKeys[repl].shake;
            s.cameraKeys[repl] = k;
        } else {
            s.cameraKeys.push_back(k);
        }
        sortCamKeys();
        return true;
    };

    // --- the dopesheet ---------------------------------------------------
    // One lane per track (the camera lane first), keys as draggable diamonds,
    // a click/drag-scrubbed time ruler and a playhead line across all lanes.
    // The label column stays pinned while the lanes scroll horizontally.
    const float laneH = scaled(26.0f), rulerH = scaled(22.0f), labelW = scaled(170.0f);
    const int laneCount = (s.cameraEnabled ? 1 : 0) + (int)s.tracks.size();
    const float sheetH =
        rulerH + laneCount * laneH + ImGui::GetStyle().ScrollbarSize + scaled(8.0f);
    // sanity: a deleted/toggled lane can strand the key selection
    if (selectedSeqTrack_ >= (int)s.tracks.size() ||
        (selectedSeqTrack_ < 0 && !s.cameraEnabled))
        selectedSeqKey_ = -1;

    int deleteTrack = -1;
    ImGui::BeginChild("##dopesheet", ImVec2(0, sheetH), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    {
        // Ctrl + mouse wheel zooms the timeline (like the flow graph); a plain
        // wheel keeps scrolling the dopesheet.
        if (ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl &&
            ImGui::GetIO().MouseWheel != 0.0f) {
            seqZoom_ *= ImPow(1.1f, ImGui::GetIO().MouseWheel);
            if (seqZoom_ < 1.0f) seqZoom_ = 1.0f;
            if (seqZoom_ > 8.0f) seqZoom_ = 8.0f;
            ImGui::GetIO().MouseWheel = 0.0f;  // consume: don't also scroll
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();  // content space
        const float visibleW = ImGui::GetWindowSize().x;
        float timeW = (visibleW - labelW - scaled(8.0f)) * seqZoom_;
        if (timeW < scaled(160.0f)) timeW = scaled(160.0f);
        const float pps = timeW / s.duration;  // pixels per second
        const float x0 = origin.x + labelW;    // timeline left edge
        const float contentH = rulerH + laneCount * laneH;
        ImGui::Dummy(ImVec2(labelW + timeW + scaled(4.0f), contentH));
        const float laneY0 = origin.y + rulerH;
        const ImVec2 winPos = ImGui::GetWindowPos();  // pinned label column x
        const float labelX = winPos.x;

        const ImU32 colRuler = ImGui::GetColorU32(ImGuiCol_TableHeaderBg);
        const ImU32 colLaneA = ImGui::GetColorU32(ImGuiCol_TableRowBg);
        const ImU32 colLaneB = ImGui::GetColorU32(ImGuiCol_TableRowBgAlt);
        const ImU32 colGrid = ImGui::GetColorU32(ImGuiCol_Border);
        const ImU32 colText = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32 colDim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const ImU32 colPlayhead = IM_COL32(255, 80, 80, 255);
        const ImU32 colSelected = IM_COL32(255, 200, 70, 255);
        // key fill encodes the outgoing easing
        auto keyColor = [&](int easing) {
            if (easing == 2) return IM_COL32(255, 176, 80, 255);   // step
            if (easing == 0) return IM_COL32(200, 200, 200, 255);  // linear
            return IM_COL32(120, 200, 255, 255);                   // smooth
        };
        auto timeAtMouse = [&]() {
            float t = (ImGui::GetIO().MousePos.x - x0) / pps;
            t = t < 0.0f ? 0.0f : (t > s.duration ? s.duration : t);
            return std::round(t * 100.0f) / 100.0f;  // snap to 10 ms
        };

        // lane backgrounds (before the interactive items on them)
        dl->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x0 + timeW, origin.y + rulerH),
                          colRuler);
        for (int li = 0; li < laneCount; ++li) {
            const float y = laneY0 + li * laneH;
            dl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + timeW, y + laneH),
                              (li & 1) ? colLaneB : colLaneA);
        }

        // lane hit areas: double-click an empty spot = drop a key there.
        // AllowOverlap so the keyframe buttons submitted on top of the lane
        // still catch the mouse (drag a key to retime) - without it the lane
        // eats every click over it.
        for (int li = 0; li < laneCount; ++li) {
            const int ti = s.cameraEnabled ? li - 1 : li;  // -1 = camera lane
            ImGui::PushID(100 + li);
            ImGui::SetCursorScreenPos(ImVec2(x0, laneY0 + li * laneH));
            ImGui::SetNextItemAllowOverlap();
            ImGui::InvisibleButton("##lane", ImVec2(timeW, laneH));
            if (ImGui::IsItemHovered() &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                const float t = timeAtMouse();
                bool did = false;
                if (ti < 0) {
                    did = snapshotCameraKey(t);
                } else {
                    did = snapshotObjectKey(s.tracks[ti], t);
                }
                if (did) {
                    selectedSeqTrack_ = ti;
                    selectedSeqKey_ = -1;  // select the dropped key below
                    if (ti < 0) {
                        for (int i = 0; i < (int)s.cameraKeys.size(); ++i)
                            if (s.cameraKeys[i].time == t) selectedSeqKey_ = i;
                    } else {
                        for (int i = 0; i < (int)s.tracks[ti].keys.size(); ++i)
                            if (s.tracks[ti].keys[i].time == t) selectedSeqKey_ = i;
                    }
                    seqPlayhead_ = t;
                    changed = true;
                }
            }
            ImGui::PopID();
        }

        // ruler: ticks + labels, click/drag scrubs the playhead
        ImGui::SetCursorScreenPos(ImVec2(x0, origin.y));
        ImGui::InvisibleButton("##ruler", ImVec2(timeW, rulerH));
        if (ImGui::IsItemActive()) {
            seqPlayhead_ = timeAtMouse();
            seqPlaying_ = false;
        }
        {
            static const float kSteps[] = {0.1f, 0.2f, 0.5f, 1.0f,  2.0f,
                                           5.0f, 10.0f, 15.0f, 30.0f, 60.0f};
            float step = 60.0f;
            for (float c : kSteps)
                if (c * pps >= scaled(56.0f)) {
                    step = c;
                    break;
                }
            const float minor = step / 5.0f;
            for (float t = 0.0f; t <= s.duration + 1e-4f; t += minor) {
                const float x = x0 + t * pps;
                const bool major = std::fabs(std::fmod(t + 1e-4f, step)) < 2e-3f;
                dl->AddLine(ImVec2(x, origin.y + (major ? scaled(4.0f) : scaled(13.0f))),
                            ImVec2(x, origin.y + rulerH), colGrid);
                if (major) {
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "%g s", t);
                    dl->AddText(ImVec2(x + scaled(3.0f), origin.y + scaled(2.0f)), colDim, buf);
                    // faint grid line down the lanes
                    dl->AddLine(ImVec2(x, laneY0), ImVec2(x, laneY0 + laneCount * laneH),
                                ImGui::GetColorU32(ImGuiCol_Border, 0.4f));
                }
            }
        }

        // keys: diamonds (free camera shots and object keys) / circles (shots
        // bound to a Camera entity). Click selects, drag retimes, right-click
        // opens easing/delete.
        auto keyWidget = [&](int lane, int ki, float& time, int& easing,
                             bool bound) -> int {
            // returns 0 = untouched, 1 = edited (uncommitted), 2 = committed,
            // 3 = delete me
            int result = 0;
            const int li = s.cameraEnabled ? lane + 1 : lane;
            const float cx = x0 + time * pps;
            const float cy = laneY0 + li * laneH + laneH * 0.5f;
            const float r = scaled(6.0f);
            const float pad = scaled(4.0f);  // hit-area margin around the diamond
            ImGui::PushID((lane + 2) * 1000 + ki);
            ImGui::SetCursorScreenPos(ImVec2(cx - r - pad, cy - r - pad));
            ImGui::InvisibleButton("##key", ImVec2(2.0f * (r + pad), 2.0f * (r + pad)));
            const bool hovered = ImGui::IsItemHovered();
            const bool dragging = ImGui::IsItemActive();
            // the horizontal-resize cursor + tooltip make retiming discoverable
            if (hovered || dragging)
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (hovered && !dragging)
                ImGui::SetTooltip("%.2f s - drag to retime,\nright-click for easing/delete",
                                  time);
            if (ImGui::IsItemActivated()) {
                selectedSeqTrack_ = lane;
                selectedSeqKey_ = ki;
            }
            if (dragging && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f)) {
                time = timeAtMouse();
                seqPlayhead_ = time;
                result = 1;
            }
            if (ImGui::IsItemDeactivated()) result = 2;  // sort + commit outside
            if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                seqPlayhead_ = time;
            ImGui::OpenPopupOnItemClick("##keyctx", ImGuiPopupFlags_MouseButtonRight);
            if (ImGui::BeginPopup("##keyctx")) {
                selectedSeqTrack_ = lane;
                selectedSeqKey_ = ki;
                ImGui::TextDisabled("Key @ %.2f s", time);
                ImGui::Separator();
                for (int e = 0; e < 3; ++e)
                    if (ImGui::MenuItem(kEaseNames[e], nullptr, easing == e)) {
                        easing = e;
                        result = 2;
                    }
                ImGui::Separator();
                if (ImGui::MenuItem("Playhead here")) seqPlayhead_ = time;
                if (ImGui::MenuItem("Delete key")) result = 3;
                ImGui::EndPopup();
            }
            const bool sel = selectedSeqTrack_ == lane && selectedSeqKey_ == ki;
            const ImU32 fill = keyColor(easing);
            if (bound) {
                dl->AddCircleFilled(ImVec2(cx, cy), r - 1.0f, fill);
                dl->AddCircle(ImVec2(cx, cy), r - 1.0f, IM_COL32(20, 20, 20, 255));
            } else {
                const ImVec2 pts[4] = {{cx, cy - r}, {cx + r, cy}, {cx, cy + r},
                                       {cx - r, cy}};
                dl->AddConvexPolyFilled(pts, 4, fill);
                dl->AddPolyline(pts, 4, IM_COL32(20, 20, 20, 255),
                                ImDrawFlags_Closed, 1.0f);
            }
            if (sel || hovered)
                dl->AddCircle(ImVec2(cx, cy), r + scaled(2.5f),
                              sel ? colSelected : ImGui::GetColorU32(ImGuiCol_Text, 0.6f),
                              0, sel ? 2.0f : 1.0f);
            ImGui::PopID();
            return result;
        };

        // camera lane keys
        if (s.cameraEnabled) {
            int del = -1, resort = -1;
            for (int ki = 0; ki < (int)s.cameraKeys.size(); ++ki) {
                SeqCameraKey& k = s.cameraKeys[ki];
                const int r =
                    keyWidget(-1, ki, k.time, k.easing, !k.camera.empty());
                if (r == 2) resort = ki;
                if (r == 3) del = ki;
            }
            if (del >= 0) {
                s.cameraKeys.erase(s.cameraKeys.begin() + del);
                selectedSeqKey_ = -1;
                changed = true;
            } else if (resort >= 0) {
                const float t = s.cameraKeys[resort].time;
                sortCamKeys();
                if (selectedSeqTrack_ == -1)
                    for (int i = 0; i < (int)s.cameraKeys.size(); ++i)
                        if (s.cameraKeys[i].time == t) {
                            selectedSeqKey_ = i;
                            break;
                        }
                changed = true;
            }
        }
        // object lane keys
        for (int ti = 0; ti < (int)s.tracks.size(); ++ti) {
            SeqTrack& tr = s.tracks[ti];
            int del = -1, resort = -1;
            for (int ki = 0; ki < (int)tr.keys.size(); ++ki) {
                const int r = keyWidget(ti, ki, tr.keys[ki].time,
                                        tr.keys[ki].easing, false);
                if (r == 2) resort = ki;
                if (r == 3) del = ki;
            }
            if (del >= 0) {
                tr.keys.erase(tr.keys.begin() + del);
                selectedSeqKey_ = -1;
                changed = true;
            } else if (resort >= 0) {
                const float t = tr.keys[resort].time;
                sortObjKeys(tr);
                if (selectedSeqTrack_ == ti)
                    for (int i = 0; i < (int)tr.keys.size(); ++i)
                        if (tr.keys[i].time == t) {
                            selectedSeqKey_ = i;
                            break;
                        }
                changed = true;
            }
        }

        // playhead: a line across ruler + lanes with a grabber triangle
        {
            const float x = x0 + seqPlayhead_ * pps;
            dl->AddLine(ImVec2(x, origin.y), ImVec2(x, laneY0 + laneCount * laneH),
                        colPlayhead, 1.5f);
            dl->AddTriangleFilled(ImVec2(x - scaled(5.0f), origin.y),
                                  ImVec2(x + scaled(5.0f), origin.y),
                                  ImVec2(x, origin.y + scaled(9.0f)), colPlayhead);
        }

        // pinned label column, drawn last so it occludes scrolled-under keys.
        // Right-click a label = track settings; [+] = snapshot key @ playhead.
        dl->AddRectFilled(ImVec2(labelX, origin.y),
                          ImVec2(labelX + labelW, origin.y + contentH),
                          ImGui::GetColorU32(ImGuiCol_ChildBg));
        dl->AddRectFilled(ImVec2(labelX, origin.y),
                          ImVec2(labelX + labelW, origin.y + rulerH), colRuler);
        dl->AddText(ImVec2(labelX + scaled(8.0f), origin.y + scaled(3.0f)), colDim, "Track");
        dl->AddLine(ImVec2(labelX + labelW, origin.y),
                    ImVec2(labelX + labelW, origin.y + contentH), colGrid);
        for (int li = 0; li < laneCount; ++li) {
            const int ti = s.cameraEnabled ? li - 1 : li;
            const float y = laneY0 + li * laneH;
            dl->AddLine(ImVec2(labelX, y), ImVec2(labelX + labelW, y),
                        ImGui::GetColorU32(ImGuiCol_Border, 0.5f));
            ImGui::PushID(500 + li);
            // snapshot button on the right edge of the label cell
            ImGui::SetCursorScreenPos(ImVec2(labelX + labelW - scaled(24.0f), y + scaled(3.0f)));
            if (ImGui::SmallButton("+")) {
                if (ti < 0) {
                    if (snapshotCameraKey(seqPlayhead_)) changed = true;
                } else if (snapshotObjectKey(s.tracks[ti], seqPlayhead_)) {
                    changed = true;
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(ti < 0 ? "Add a shot at the playhead, filming from the\n"
                                           "active camera (looked-through / selected /\n"
                                           "first). Needs a Camera entity in the scene."
                                         : "Snapshot the object's pose as a key\n"
                                           "at the playhead.");
            // the rest of the cell: click selects the lane, right-click = setup
            ImGui::SetCursorScreenPos(ImVec2(labelX, y));
            ImGui::InvisibleButton("##label", ImVec2(labelW - scaled(26.0f), laneH));
            if (ImGui::IsItemClicked()) {
                selectedSeqTrack_ = ti;
                selectedSeqKey_ = -1;
            }
            ImGui::OpenPopupOnItemClick("##trackctx", ImGuiPopupFlags_MouseButtonRight);
            if (ti < 0) {
                dl->AddText(ImVec2(labelX + scaled(8.0f), y + scaled(5.0f)), colText, "[*] Camera");
                dl->AddText(ImVec2(labelX + scaled(86.0f), y + scaled(5.0f)), colDim,
                            ("(" + std::to_string(s.cameraKeys.size()) + ")").c_str());
            } else {
                const SeqTrack& tr = s.tracks[ti];
                const std::string label =
                    (tr.target.empty() ? "<no object>" : tr.target);
                dl->AddText(ImVec2(labelX + scaled(8.0f), y + scaled(5.0f)), colText, label.c_str());
                // animated-channel letters, dimmed when off
                const char* chs[] = {"P", "R", "S", "C", "V"};
                const bool on[] = {tr.animPos, tr.animRot, tr.animScale,
                                   tr.animColor, tr.animVis};
                float cxs = labelX + labelW - scaled(88.0f);
                for (int c = 0; c < 5; ++c) {
                    dl->AddText(ImVec2(cxs, y + scaled(5.0f)),
                                on[c] ? colText : ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.4f),
                                chs[c]);
                    cxs += scaled(11.0f);
                }
            }
            if (ImGui::BeginPopup("##trackctx")) {
                if (ti < 0) {
                    ImGui::TextDisabled("Camera track");
                    ImGui::Separator();
                    if (ImGui::MenuItem("Add shot @ playhead (active camera)")) {
                        if (snapshotCameraKey(seqPlayhead_)) changed = true;
                    }
                    if (ImGui::MenuItem("Clear all shots", nullptr, false,
                                        !s.cameraKeys.empty())) {
                        s.cameraKeys.clear();
                        selectedSeqKey_ = -1;
                        changed = true;
                    }
                    if (ImGui::MenuItem("Import take...")) beginTakeImport();
                } else {
                    SeqTrack& tr = s.tracks[ti];
                    ImGui::SetNextItemWidth(scaled(160.0f));
                    if (ImGui::BeginCombo("Object",
                                          tr.target.empty() ? "<pick>" : tr.target.c_str())) {
                        for (const SceneObject& o : project_.objects())
                            if (ImGui::Selectable(o.name.c_str(), o.name == tr.target)) {
                                tr.target = o.name;
                                changed = true;
                            }
                        ImGui::EndCombo();
                    }
                    ImGui::TextDisabled("Animated channels:");
                    if (ImGui::Checkbox("Position", &tr.animPos)) changed = true;
                    if (ImGui::Checkbox("Rotation", &tr.animRot)) changed = true;
                    if (ImGui::Checkbox("Scale", &tr.animScale)) changed = true;
                    if (ImGui::Checkbox("Color", &tr.animColor)) changed = true;
                    if (ImGui::Checkbox("Visibility", &tr.animVis)) changed = true;
                    ImGui::Separator();
                    if (ImGui::MenuItem("Snapshot key @ playhead")) {
                        if (snapshotObjectKey(tr, seqPlayhead_)) changed = true;
                    }
                    if (ImGui::MenuItem("Remove track")) deleteTrack = ti;
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    if (deleteTrack >= 0) {
        s.tracks.erase(s.tracks.begin() + deleteTrack);
        if (selectedSeqTrack_ == deleteTrack) selectedSeqTrack_ = -1, selectedSeqKey_ = -1;
        changed = true;
    }
    if (ImGui::SmallButton("+ Add object track")) ImGui::OpenPopup("##addtrack");
    if (ImGui::BeginPopup("##addtrack")) {
        auto hasTrack = [&](const std::string& name) {
            for (const SeqTrack& tr : s.tracks)
                if (tr.target == name) return true;
            return false;
        };
        // one track per object; a fresh track gets a starting key at the
        // playhead from the object's current pose, so it animates immediately
        auto addTrackFor = [&](const std::string& name) {
            SeqTrack tr;
            tr.target = name;
            cutsceneSnapshotObjectKey(tr, seqPlayhead_);
            s.tracks.push_back(std::move(tr));
            selectedSeqTrack_ = (int)s.tracks.size() - 1;
            selectedSeqKey_ = -1;
            changed = true;
        };
        int freshSelected = 0;
        for (int sel : selection_)
            if (sel >= 0 && sel < (int)project_.objects().size() &&
                !hasTrack(project_.objects()[sel].name))
                ++freshSelected;
        char selLabel[48];
        std::snprintf(selLabel, sizeof(selLabel), "Add selected (%d)", freshSelected);
        if (ImGui::MenuItem(selLabel, nullptr, false, freshSelected > 0)) {
            for (int sel : selection_)
                if (sel >= 0 && sel < (int)project_.objects().size() &&
                    !hasTrack(project_.objects()[sel].name))
                    addTrackFor(project_.objects()[sel].name);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("One track per selected object (objects that\n"
                              "already have a track are skipped).");
        ImGui::Separator();
        for (const SceneObject& o : project_.objects()) {
            const bool tracked = hasTrack(o.name);
            if (ImGui::MenuItem(o.name.c_str(), tracked ? "tracked" : nullptr,
                                false, !tracked))
                addTrackFor(o.name);
        }
        if (project_.objects().empty())
            ImGui::TextDisabled("No objects in this scene.");
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Double-click a lane to drop a key - right-click keys & labels.");

    // --- selected key inspector ----------------------------------------------
    ImGui::SeparatorText("Key");
    const bool camSel = selectedSeqTrack_ == -1 && s.cameraEnabled &&
                        selectedSeqKey_ >= 0 &&
                        selectedSeqKey_ < (int)s.cameraKeys.size();
    const bool objSel = selectedSeqTrack_ >= 0 &&
                        selectedSeqTrack_ < (int)s.tracks.size() &&
                        selectedSeqKey_ >= 0 &&
                        selectedSeqKey_ < (int)s.tracks[selectedSeqTrack_].keys.size();
    if (camSel) {
        SeqCameraKey& k = s.cameraKeys[selectedSeqKey_];
        ImGui::SetNextItemWidth(scaled(90.0f));
        if (ImGui::DragFloat("Time", &k.time, 0.02f, 0.0f, s.duration, "%.2f s")) {
            seqPlayhead_ = k.time;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            const float t = k.time;
            sortCamKeys();
            for (int i = 0; i < (int)s.cameraKeys.size(); ++i)
                if (s.cameraKeys[i].time == t) selectedSeqKey_ = i;
            changed = true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(110.0f));
        if (ImGui::Combo("Easing", &k.easing, kEaseNames, 3)) changed = true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(90.0f));
        if (ImGui::DragFloat("Shake", &k.shake, 0.005f, 0.0f, 2.0f, "%.2f")) {}
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Handheld camera shake amplitude in world units\n"
                              "(interpolates between shots; 0 = steady).");
        // Roll only means something for a free shot: a bound one takes its whole
        // basis from the Camera entity's orientation.
        if (k.camera.empty()) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(scaled(100.0f));
            if (ImGui::DragFloat("Roll", &k.roll, 0.5f, -180.0f, 180.0f, "%.1f deg")) {}
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Dutch angle - rotation about the view axis\n"
                                  "(interpolates between shots; 0 = level\n"
                                  "horizon). A shot bound to a Camera entity\n"
                                  "tilts with that entity instead.");
        }

        // Every shot films from a Camera entity. (Legacy free shots from older
        // projects still play - their stored eye/look-at is the fallback - but
        // new shots always pick a camera; add them with + Add object > Camera.)
        const char* shotLabel = k.camera.empty() ? "<pick a camera>" : k.camera.c_str();
        ImGui::SetNextItemWidth(scaled(160.0f));
        bool anyCam = false;
        if (ImGui::BeginCombo("Shot from", shotLabel)) {
            for (const SceneObject& o : project_.objects()) {
                if (o.type != PrimitiveType::Camera) continue;
                anyCam = true;
                if (ImGui::Selectable(o.name.c_str(), o.name == k.camera)) {
                    k.camera = o.name;
                    changed = true;
                }
            }
            if (!anyCam)
                ImGui::TextDisabled("No Camera entities - add one with\n"
                                    "+ Add object > Gameplay > Camera.");
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The shot films from this Camera entity's pose + FOV.\n"
                              "Animate the entity (or import a take into it) for a\n"
                              "moving shot; Step easing between two cameras = a cut.");
        {
            bool found = false;
            for (const SceneObject& o : project_.objects())
                if (o.name == k.camera && o.type == PrimitiveType::Camera) {
                    ImGui::TextDisabled("Films from \"%s\" (FOV %.0f deg).",
                                        o.name.c_str(), o.cameraFov);
                    found = true;
                    break;
                }
            if (k.camera.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                                   "Pick a camera above for this shot.");
            else if (!found)
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                                   "Camera \"%s\" is not in this scene.",
                                   k.camera.c_str());
        }
        if (ImGui::SmallButton("Delete key")) {
            s.cameraKeys.erase(s.cameraKeys.begin() + selectedSeqKey_);
            selectedSeqKey_ = -1;
            changed = true;
        }
    } else if (objSel) {
        SeqTrack& tr = s.tracks[selectedSeqTrack_];
        SeqObjectKey& k = tr.keys[selectedSeqKey_];
        ImGui::SetNextItemWidth(scaled(90.0f));
        if (ImGui::DragFloat("Time", &k.time, 0.02f, 0.0f, s.duration, "%.2f s")) {
            seqPlayhead_ = k.time;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            const float t = k.time;
            sortObjKeys(tr);
            for (int i = 0; i < (int)tr.keys.size(); ++i)
                if (tr.keys[i].time == t) selectedSeqKey_ = i;
            changed = true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(110.0f));
        if (ImGui::Combo("Easing", &k.easing, kEaseNames, 3)) changed = true;
        ImGui::SameLine();
        if (ImGui::SmallButton("Re-snapshot")) {
            if (snapshotObjectKey(tr, k.time)) changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Replace this key with the object's current pose.");
        // pose fields for the channels this track animates
        if (tr.animPos) {
            ImGui::DragFloat3("Position", k.position, 0.1f);
            changed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        if (tr.animRot) {
            ImGui::DragFloat3("Rotation", k.rotation, 1.0f, -360.0f, 360.0f, "%.0f deg");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        if (tr.animScale) {
            ImGui::DragFloat3("Scale", k.scale, 0.05f, 0.01f, 1000.0f);
            changed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        if (tr.animColor) {
            ImGui::ColorEdit3("Color", k.color);
            changed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        if (tr.animVis) {
            if (ImGui::Checkbox("Visible (holds to the next key)", &k.visible))
                changed = true;
        }
        if (!tr.animPos && !tr.animRot && !tr.animScale && !tr.animColor && !tr.animVis)
            ImGui::TextDisabled("No channels enabled - right-click the track label.");
        if (ImGui::SmallButton("Delete key")) {
            tr.keys.erase(tr.keys.begin() + selectedSeqKey_);
            selectedSeqKey_ = -1;
            changed = true;
        }
    } else {
        ImGui::TextDisabled("No key selected. Double-click a lane to drop one, or use\n"
                            "the [+] on a track label to snapshot at the playhead.\n"
                            "Play the cutscene in the game with the Play Sequence node.");
    }

    // --- Import take modal ----------------------------------------------------
    // Maps a loaded phone take (beginTakeImport) into the scene and bakes it
    // to free camera shots. The bake is pure (src/camtake.cpp) and cheap, but
    // only recomputed when a control changes; the readout shows the resulting
    // key count live so the tolerance slider can be tuned by eye.
    if (seqTakeOpen_) {
        ImGui::OpenPopup("Import camera take");
        seqTakeOpen_ = false;
    }
    if (ImGui::BeginPopupModal("Import camera take", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::string file =
            std::filesystem::path(seqTakePath_).filename().string();
        if (!seqTakeError_.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Failed to load take:");
            ImGui::TextWrapped("%s", seqTakeError_.c_str());
            ImGui::Separator();
            if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        } else {
            ImGui::Text("%s", file.c_str());
            ImGui::TextDisabled("%s - %d samples @ %.0f Hz, %.2f s",
                                seqTake_.source.c_str(), (int)seqTake_.samples.size(),
                                seqTake_.fps, (float)seqTake_.duration());
            ImGui::Separator();

            // Target: which Camera entity the move bakes into (position +
            // rotation track + FOV + a bound shot, so it dollies along the
            // path). Two cameras in one scene each carry their own recording.
            bool anyCam = false;
            for (const SceneObject& o : project_.objects())
                if (o.type == PrimitiveType::Camera) anyCam = true;
            const std::string targetLabel =
                seqTakeTarget_.empty() ? "<pick a camera>" : seqTakeTarget_;
            ImGui::SetNextItemWidth(200.0f);
            if (ImGui::BeginCombo("Into camera", targetLabel.c_str())) {
                for (const SceneObject& o : project_.objects())
                    if (o.type == PrimitiveType::Camera)
                        if (ImGui::Selectable(o.name.c_str(), seqTakeTarget_ == o.name))
                            seqTakeTarget_ = o.name;
                if (!anyCam)
                    ImGui::TextDisabled("No Camera entities in this scene\n"
                                        "(+ Add object > Gameplay > Camera).");
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("The move bakes into this Camera entity's transform\n"
                                  "track (position + rotation) and its FOV, plus a\n"
                                  "bound shot on the camera lane - so it dollies along\n"
                                  "the path. Two cameras each take their own recording.");
            if (!anyCam)
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
                                   "Add a Camera entity first (+ Add object >\n"
                                   "Gameplay > Camera), then re-import.");
            ImGui::Separator();

            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::DragFloat("Scale (units per meter)", &seqTakeMap_.scale, 0.02f,
                                 0.01f, 1000.0f, "%.2f"))
                seqTakeDirty_ = true;
            {
                // Seeded from the project's world scale; say so, and show what
                // the recording covers at the current setting - "5 m of walking
                // became 5 units" is the mistake this whole readout exists for.
                const float ups = project_.settings.unitsPerMeter;
                if (seqTakeMap_.scale != ups) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("World scale")) {
                        seqTakeMap_.scale = ups;
                        seqTakeDirty_ = true;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Back to the project's %.3f units per meter\n"
                                          "(Project Preferences > World).", ups);
                }
                float walked = 0.0f;
                for (size_t i = 1; i < seqTake_.samples.size(); ++i) {
                    const float* a = seqTake_.samples[i - 1].pos;
                    const float* b = seqTake_.samples[i].pos;
                    const float dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
                    walked += std::sqrt(dx * dx + dy * dy + dz * dz);
                }
                ImGui::TextDisabled("Recorded path: %.2f m walked -> %.1f units",
                                    walked, walked * seqTakeMap_.scale);
            }
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::DragFloat("Extra yaw", &seqTakeMap_.yawDeg, 1.0f, -360.0f,
                                 360.0f, "%.0f deg"))
                seqTakeDirty_ = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Rotates the whole path about the up axis, pivoting\n"
                                  "on the take's first sample.");
            if (ImGui::DragFloat3("Origin", seqTakeMap_.origin, 0.1f))
                seqTakeDirty_ = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Where the take's first sample lands in the scene.");
            ImGui::SameLine();
            if (ImGui::SmallButton("From view")) {
                takeOriginAimFromView();  // position + aim
                seqTakeDirty_ = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Drop the start point at the editor camera AND\n"
                                  "aim the path where you are looking.");
            if (ImGui::Checkbox("Start at playhead", &seqTakeAtPlayhead_))
                seqTakeDirty_ = true;
            ImGui::SameLine();
            ImGui::TextDisabled("(t = %.2f s)", seqPlayhead_);
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::SliderFloat("Tolerance", &seqTakeMap_.tolerance, 0.005f, 1.0f,
                                   "%.3f units", ImGuiSliderFlags_Logarithmic))
                seqTakeDirty_ = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Decimation error bound in world units: dropping a\n"
                                  "sample never moves the interpolated camera by more\n"
                                  "than this. Lower = more keys, bigger PS2 tables.");

            const float wantOffset = seqTakeAtPlayhead_ ? seqPlayhead_ : 0.0f;
            if (wantOffset != seqTakeMap_.timeOffset) {
                seqTakeMap_.timeOffset = wantOffset;
                seqTakeDirty_ = true;
            }
            if (seqTakeDirty_) {
                seqTakeBaked_ = bakeCamTake(seqTake_, seqTakeMap_, &seqTakeStats_);
                seqTakeDirty_ = false;
            }
            ImGui::Separator();
            ImGui::Text("%d samples  ->  %d keys   (%.2f s, FOV %.0f deg)",
                        seqTakeStats_.sampleCount, seqTakeStats_.keyCount,
                        seqTakeStats_.duration, seqTakeStats_.fovDeg);
            if (seqTakeStats_.keyCount > 300)
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                                   "That is a lot of keys - the PS2 keyframe table\n"
                                   "grows with every key. Raise the tolerance.");

            if (!seqTakeTarget_.empty())
                ImGui::TextDisabled("Rewrites \"%s\" transform track + FOV, and adds a\n"
                                    "bound shot if the camera lane has none yet.",
                                    seqTakeTarget_.c_str());

            ImGui::Separator();
            ImGui::BeginDisabled(seqTakeBaked_.empty() || seqTakeTarget_.empty());
            if (ImGui::Button("Import", ImVec2(120.0f, 0.0f))) {
                const float firstT = applyCamTake(s, true);
                if (firstT >= 0.0f) {
                    const float lastT =
                        s.cameraKeys.empty() ? 0.0f : s.cameraKeys.back().time;
                    if (lastT > s.duration) s.duration = lastT;
                    selectedSeqTrack_ = -1;
                    selectedSeqKey_ = -1;
                    seqPlayhead_ = firstT;
                    // keep the take loaded so the path can be re-positioned /
                    // re-oriented afterwards (Adjust imported take section)
                    seqTakeActive_ = true;
                    seqTakeSeqIdx_ = selectedSequence_;
                    changed = true;
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild();
    ImGui::End();

    commitIfEdited();
}
