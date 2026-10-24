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
#include "theme.hpp"
#include "toolkit.hpp"
#include "widgets.hpp"

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

// Snapshots the track target's current static pose into a key at `time`, for
// the components in `bits` (0 = every channel the track animates). A key
// within kSeqKeySnap of that time takes them instead (its easing, its
// visibility flag and its other components stay); the key list stays sorted.
// Returns the key's index, or -1 when the object is not in this scene.
int App::cutsceneSnapshotObjectKey(SeqTrack& tr, float time, unsigned bits) {
    const SceneObject* src = nullptr;
    for (const SceneObject& o : project_.objects())
        if (o.name == tr.target) {
            src = &o;
            break;
        }
    if (!src) return -1;
    if (!bits) bits = seqTrackChannels(tr);
    if (!bits) {
        tr.animPos = true;  // a track that animates nothing starts with position
        bits = kSeqChPos;
    }
    SeqObjectKey k;
    k.time = time;
    for (int c = 0; c < 3; ++c) {
        k.position[c] = src->position[c];
        k.rotation[c] = src->rotation[c];
        k.scale[c] = src->scale[c];
        k.color[c] = src->color[c];
    }
    // visibility is not part of the pose: a re-keyed key keeps its flag
    for (const SeqObjectKey& e : tr.keys)
        if (std::fabs(e.time - time) < kSeqKeySnap) k.visible = e.visible;
    return seqSetKey(tr, time, bits, k);
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
                phoneWalked_ = 0.0f;
                phonePoseHz_ = 0.0f;
                phonePoseRateN_ = 0;
                phonePoseRateAt_ = ImGui::GetTime();
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
    // Device row figures: pose rate over ~1 s, metres walked (6DoF only - a
    // rotation-only device never moves).
    for (const CamTakeSample& smp : poses) {
        if (phoneHasPose_) {
            const float dx = smp.pos[0] - phoneLastPos_[0], dy = smp.pos[1] - phoneLastPos_[1],
                        dz = smp.pos[2] - phoneLastPos_[2];
            phoneWalked_ += std::sqrt(dx * dx + dy * dy + dz * dz);
        }
        for (int c = 0; c < 3; ++c) phoneLastPos_[c] = smp.pos[c];
    }
    phonePoseRateN_ += (int)poses.size();
    if (ImGui::GetTime() - phonePoseRateAt_ >= 1.0) {
        phonePoseHz_ = (float)(phonePoseRateN_ / (ImGui::GetTime() - phonePoseRateAt_));
        phonePoseRateN_ = 0;
        phonePoseRateAt_ = ImGui::GetTime();
    }
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

// Tools > Phone Camera: the link and the recorder in one window. Unlinked it is
// an explanation with ONE action (Start link) and what you need; waiting, it
// is the address and the pairing code; linked, it is the phone's view, where
// the shot starts, and Record. Machine-wide stream tuning folds away at the
// bottom - set once, rarely read.
void App::drawPhoneCamWindow() {
    if (!showPhoneCamWindow_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(420), scaled(760)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Phone Camera", &showPhoneCamWindow_)) {
        ImGui::End();
        return;
    }
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const bool up = phoneCam_.listening();
    const bool live = phoneCam_.connected();
    const bool failed = phoneCam_.state() == phonecam::Link::State::Error;
    const phonecam::DeviceInfo dev = phoneCam_.device();

    std::vector<std::string> cams;  // this scene's Camera entities
    if (hasProject_)
        for (const SceneObject& o : project_.objects())
            if (o.type == PrimitiveType::Camera) cams.push_back(o.name);

    // --- status line ----------------------------------------------------------
    {
        char pill[64];
        int kind = 4;
        if (failed) {
            std::snprintf(pill, sizeof(pill), "Link failed###phone_state");
            kind = 2;
        } else if (live) {
            if (phonePoseHz_ > 0.0f)
                std::snprintf(pill, sizeof(pill), "Live \xc2\xb7 %.0f Hz###phone_state", phonePoseHz_);
            else
                std::snprintf(pill, sizeof(pill), "Live###phone_state");
            kind = phoneRec_ ? 2 : 0;
        } else if (up) {
            std::snprintf(pill, sizeof(pill), "Waiting for a phone###phone_state");
            kind = 1;
        } else {
            std::snprintf(pill, sizeof(pill), "Not linked###phone_state");
        }
        const float stopW = up ? ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x : 0.0f;
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - ui::StatusPillWidth(pill) - stopW);
        ui::StatusPill(pill, kind);
        if (up) {
            ImGui::SameLine();
            // A square "stop" mark: ends the link (and a recording with it).
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float h = ImGui::GetFrameHeight();
            if (ImGui::InvisibleButton("Stop link", ImVec2(h, h))) stopPhoneCam();
            const float t = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (t > 0.0f)
                dl->AddRectFilled(p, ImVec2(p.x + h, p.y + h),
                                  ImGui::GetColorU32(ImVec4(sem.surface.x, sem.surface.y, sem.surface.z, t)),
                                  ImGui::GetStyle().FrameRounding);
            const float s = h * 0.28f;
            dl->AddRectFilled(ImVec2(p.x + h * 0.5f - s, p.y + h * 0.5f - s),
                              ImVec2(p.x + h * 0.5f + s, p.y + h * 0.5f + s),
                              ImGui::GetColorU32(t > 0.0f ? sem.danger : sem.textDim), 2.0f);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Stop link - the phone disconnects%s.",
                                  phoneRec_ ? " and the recording ends" : "");
        }
    }
    if (failed) {
        ImGui::PushStyleColor(ImGuiCol_Text, sem.danger);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextWrapped("%s", phoneCam_.errorText().c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }

    // What you need - shown until a phone is linked.
    auto youWillNeed = [&]() {
        ui::Caption("You will need");
        auto step = [&](int n, const std::string& text) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float r = fs * 0.62f;
            dl->AddCircleFilled(ImVec2(p.x + r, p.y + ImGui::GetTextLineHeight() * 0.5f), r,
                                ImGui::GetColorU32(sem.field));
            char num[4];
            std::snprintf(num, sizeof(num), "%d", n);
            const ImVec2 ns = ImGui::CalcTextSize(num);
            dl->AddText(ImVec2(p.x + r - ns.x * 0.5f, p.y), ImGui::GetColorU32(sem.textDim), num);
            ImGui::SetCursorScreenPos(ImVec2(p.x + r * 2.0f + fs * 0.6f, p.y));
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextWrapped("%s", text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
        };
        step(1, "The tyrax-cam app on an iPhone with ARKit, or any browser at the address "
                "shown after Start link (rotation only).");
        step(2, "Phone and computer on the same Wi-Fi.");
        std::string shot;
        if (cams.empty()) {
            shot = "A shot to start from: frame one in the viewport, or add a Camera to the "
                   "scene.";
        } else {
            shot = "A shot to start from: select a Camera (";
            for (size_t i = 0; i < cams.size() && i < 2; ++i) shot += (i ? ", " : "") + cams[i];
            if (cams.size() > 2) shot += " ... " + std::to_string(cams.size()) + " in this scene";
            shot += ") or frame one in the viewport.";
        }
        step(3, shot);
    };

    if (!up) {
        // --- connect: say what this is, one action -------------------------------
        const float avail = ImGui::GetContentRegionAvail().x;
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const ImVec2 c(p.x + avail * 0.5f, p.y + fs * 2.0f);
            // a phone, drawn: a rounded slab with a lens dot
            dl->AddRect(ImVec2(c.x - fs * 0.75f, c.y - fs * 1.3f), ImVec2(c.x + fs * 0.75f, c.y + fs * 1.3f),
                        ImGui::GetColorU32(sem.accent), fs * 0.3f, 0, 2.0f);
            dl->AddCircleFilled(ImVec2(c.x, c.y + fs * 0.95f), fs * 0.12f, ImGui::GetColorU32(sem.accent));
            ImGui::Dummy(ImVec2(avail, fs * 4.0f));
        }
        auto centred = [&](const char* text, bool bold, bool dim) {
            ui::FontScope f(bold ? ui::boldFont() : nullptr);
            const float w = ImMin(avail, ImGui::CalcTextSize(text).x);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - w) * 0.5f);
            if (dim) ImGui::TextDisabled("%s", text);
            else ImGui::TextUnformatted(text);
        };
        centred("Film with your phone", true, false);
        centred("Move the phone and the editor camera moves with it.", false, true);
        centred("Record the move into a cutscene from here.", false, true);
        ImGui::Spacing();
        {
            const float bw = ImGui::CalcTextSize("Start link").x + ImGui::GetStyle().FramePadding.x * 5.0f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - bw) * 0.5f);
            if (ui::PrimaryButton("Start link", bw)) startPhoneCam();
        }
        {
            char note[96];
            std::snprintf(note, sizeof(note), "Opens port %d on this machine", phoneCamPort_);
            centred(note, false, true);
            centred("The first time, allow it on private networks.", false, true);
        }
        ImGui::Spacing();
        youWillNeed();
        ImGui::Spacing();
        ui::InspectorRows rows(scaled(80.0f), scaled(140.0f));
        if (ui::Switch("Require pairing code (a 6-digit code the phone must type)",
                       &phoneCamRequireCode_))
            saveGlobalConfig();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Off accepts any device on this network - fine at a\n"
                              "desk of your own, wrong in a shared office.");
        // A caption-left row like the switch above (no InputInt row wrapper:
        // the field fills the row, which clips its own right-hand label).
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Port");
        ImGui::SameLine(ImClamp(ImGui::GetContentRegionAvail().x * 0.34f, scaled(80.0f), scaled(140.0f)));
        ImGui::SetNextItemWidth(-FLT_MIN);
        {
            // The field fills the row, so its own "Port" label would land past
            // the window's edge (a stray "P" over the panel next to it): clip
            // it to the row. The label stays the widget's ID.
            const ImVec2 c = ImGui::GetCursorScreenPos();
            ImGui::PushClipRect(ImVec2(c.x, c.y - 2.0f),
                                ImVec2(c.x + ImGui::GetContentRegionAvail().x, c.y + ImGui::GetFrameHeight() + 2.0f),
                                true);
            if (ImGui::InputInt("Port", &phoneCamPort_, 0, 0)) {
                phoneCamPort_ = std::clamp(phoneCamPort_, 1024, 65535);
                saveGlobalConfig();
            }
            ImGui::PopClipRect();
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Stream quality and smoothing appear once a phone is connected.");
        ImGui::End();
        return;
    }

    if (!live) {
        // --- waiting: what to type into the phone, big ------------------------------
        ui::Caption("Connect the phone to");
        const std::vector<std::string> ips = wire::localIPv4();
        if (ips.empty())
            ImGui::TextDisabled("No LAN address found - is this machine on Wi-Fi?");
        for (const std::string& ip : ips) {
            const std::string addr = ip + ":" + std::to_string(phoneCam_.port());
            ImGui::PushID(ip.c_str());
            {
                ui::FontScope mono(ui::monoFont());
                ImGui::PushFont(nullptr, fs * 1.35f);
                ImGui::TextUnformatted(addr.c_str());
                ImGui::PopFont();
            }
            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + fs * 0.2f);
            if (ui::GhostButton("Copy")) ImGui::SetClipboardText(addr.c_str());
            ImGui::SameLine();
            if (ui::GhostButton("Copy URL")) ImGui::SetClipboardText(("http://" + addr).c_str());
            ImGui::PopID();
        }
        if (phoneCamRequireCode_) {
            ImGui::Spacing();
            ui::Caption("Pairing code");
            {
                ui::FontScope mono(ui::monoFont());
                ImGui::PushFont(nullptr, fs * 1.8f);
                ImGui::TextUnformatted(phoneCamCode_.c_str());
                ImGui::PopFont();
            }
            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + fs * 0.4f);
            if (ui::GhostButton("New code")) {
                phoneCamCode_ = phonecam::newPairCode();
                saveGlobalConfig();
                statusMessage_ = "New pairing code - restart the link for it to take effect";
            }
        }
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Type the address%s into the tyrax-cam app "
                            "(github.com/doctorspider42/tyrax-cam). Any browser at the "
                            "http:// address gets a test client with the same stream.",
                            phoneCamRequireCode_ ? " and the code" : "");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        youWillNeed();
        ImGui::End();
        return;
    }

    // --- live: the device ------------------------------------------------------------
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = fs * 2.6f;
        const float w = ImGui::GetContentRegionAvail().x;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(p.x, p.y), ImVec2(p.x + h, p.y + h), ImGui::GetColorU32(sem.field),
                          ImGui::GetStyle().FrameRounding + 2.0f);
        ui::DrawGlyph(dl, ui::Glyph::Camera, ImVec2(p.x + h * 0.5f, p.y + h * 0.5f), h * 0.5f,
                      ImGui::GetColorU32(sem.accent));
        const std::string title =
            dev.name.empty() ? std::string("Phone") : dev.name + (dev.client.empty() ? "" : " \xc2\xb7 " + dev.client);
        {
            ui::FontScope bold(ui::boldFont());
            dl->AddText(ImVec2(p.x + h + fs * 0.6f, p.y + fs * 0.15f), ImGui::GetColorU32(sem.text),
                        title.c_str());
        }
        const std::string sub = (dev.address.empty() ? std::string() : dev.address + " \xc2\xb7 ") +
                                (dev.sixDof ? "6DoF tracking" : "rotation only");
        {
            ui::FontScope mono(ui::monoFont());
            dl->AddText(ImVec2(p.x + h + fs * 0.6f, p.y + fs * 1.35f), ImGui::GetColorU32(sem.textDim),
                        sub.c_str());
            if (dev.sixDof) {
                char walked[32];
                std::snprintf(walked, sizeof(walked), "%.2f m", phoneWalked_);
                const float ww = ImGui::CalcTextSize(walked).x;
                dl->AddText(ImVec2(p.x + w - ww, p.y + fs * 0.15f), ImGui::GetColorU32(sem.textDim), walked);
                const float lw = ImGui::CalcTextSize("walked").x;
                dl->AddText(ImVec2(p.x + w - lw, p.y + fs * 1.35f), ImGui::GetColorU32(sem.textDim), "walked");
            }
        }
        ImGui::Dummy(ImVec2(w, h));
        if (!dev.sixDof && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("This device reports no world position, so the camera\n"
                              "turns in place instead of walking.");
    }
    ImGui::Spacing();

    // --- the phone's view (the hero) ----------------------------------------------
    {
        const float w = ImGui::GetContentRegionAvail().x;
        const float aspect = ps2ViewportOutput().tvAspect > 0.1f ? ps2ViewportOutput().tvAspect : 4.0f / 3.0f;
        const float h = ImMin(w / aspect, scaled(190.0f));
        const float iw = h * aspect;
        const ImVec2 p(ImGui::GetCursorScreenPos().x + (w - iw) * 0.5f, ImGui::GetCursorScreenPos().y);
        const ImVec2 q(p.x + iw, p.y + h);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float r = ImGui::GetStyle().FrameRounding + 3.0f;
        const double age = ImGui::GetTime() - phonePoseAt_;
        if (phoneHasPose_ && age < 1.0) {
            // The camera the phone is driving, rendered here so the window shows
            // what the phone shows even when the viewport is someone else's.
            std::vector<std::string> hide;
            if (!phoneRecTarget_.empty()) hide.push_back(phoneRecTarget_);
            viewport_.setHiddenCameras(hide);
            const float sc = ImGui::GetIO().DisplayFramebufferScale.x;
            const uint32_t tex = viewport_.renderShot((int)(iw * sc), (int)(h * sc), project_.objects(),
                                                      phoneEye_, phoneTarget_, phoneFov_, phoneRoll_,
                                                      Viewport::GameViewPhone, modelEditSerial_);
            dl->AddImageRounded((ImTextureID)(intptr_t)tex, p, q, ImVec2(0, 1), ImVec2(1, 0),
                                IM_COL32_WHITE, r);
        } else {
            dl->AddRectFilled(p, q, ImGui::GetColorU32(sem.field), r);
            const char* msg = phoneHasPose_ ? "Stream stalled" : "Waiting for the first pose...";
            const ImVec2 ms = ImGui::CalcTextSize(msg);
            dl->AddText(ImVec2((p.x + q.x - ms.x) * 0.5f, (p.y + q.y - ms.y) * 0.5f),
                        ImGui::GetColorU32(sem.textDim), msg);
        }
        dl->AddRect(p, q, ImGui::GetColorU32(sem.border), r);
        // glass labels: what the phone receives, and where it is
        auto glass = [&](ImVec2 at, const char* text, bool right) {
            ui::FontScope mono(ui::monoFont());
            const ImVec2 ts = ImGui::CalcTextSize(text);
            const ImVec2 a(right ? at.x - ts.x - fs * 0.8f : at.x, at.y);
            dl->AddRectFilled(a, ImVec2(a.x + ts.x + fs * 0.8f, a.y + ts.y + fs * 0.4f),
                              IM_COL32(0, 0, 0, 170), fs * 0.3f);
            dl->AddText(ImVec2(a.x + fs * 0.4f, a.y + fs * 0.2f), IM_COL32(235, 235, 240, 255), text);
        };
        const phonecam::PreviewPrefs pv = phoneCam_.preview();
        char top[64];
        std::snprintf(top, sizeof(top), "what the phone shows \xc2\xb7 %d px \xc2\xb7 %d fps", pv.maxWidth, pv.fps);
        glass(ImVec2(p.x + fs * 0.5f, p.y + fs * 0.5f), top, false);
        if (phoneHasPose_) {
            char pos[64];
            std::snprintf(pos, sizeof(pos), "pos %.2f %.2f %.2f m", phonePose_.pos[0], phonePose_.pos[1],
                          phonePose_.pos[2]);
            glass(ImVec2(q.x - fs * 0.5f, q.y - fs * 1.9f), pos, true);
        }
        ImGui::Dummy(ImVec2(w, h));
    }
    ImGui::Spacing();

    // --- where the shot starts ---------------------------------------------------------
    ui::Caption("Start from");
    {
        ui::InspectorRows rows(scaled(80.0f), scaled(120.0f));
        // Which Camera entity the phone views from AND records into - one control,
        // because they are one intent. Picking one jumps the phone to where that
        // camera was placed, and Recentre returns there.
        const std::string label =
            phoneRecTarget_.empty() ? "Free shots (from the editor view)" : phoneRecTarget_;
        ImGui::BeginDisabled(phoneRec_);
        if (ui::BeginCombo("View from", label.c_str())) {
            if (ImGui::Selectable("Free shots (from the editor view)", phoneRecTarget_.empty()))
                selectPhoneCamera(std::string());
            if (!cams.empty()) ImGui::Separator();
            for (const std::string& c : cams)
                if (ImGui::Selectable(c.c_str(), phoneRecTarget_ == c)) selectPhoneCamera(c);
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip(phoneRec_ ? "Locked while recording."
                                        : "The phone starts from this camera's placed pose -\n"
                                          "position, aim and tilt - and Recentre returns there.\n"
                                          "The recording goes into the same camera's track.\n"
                                          "Also selectable on the phone.");
        if (!phoneRecTarget_.empty() && !phoneStartCamera())
            ImGui::TextColored(sem.warn, "\"%s\" is not in this scene - using the editor view.",
                               phoneRecTarget_.c_str());
        const float worldW = ImGui::CalcTextSize("World").x + ImGui::GetStyle().FramePadding.x * 2.6f;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - worldW - ImGui::GetStyle().ItemSpacing.x);
        ui::DragFloat("Scale", &phoneMap_.scale, 0.02f, 0.01f, 1000.0f, "%.2f u/m");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Game units per real metre. Seeded from the project's\n"
                              "world scale when the phone connects; raise it to cover\n"
                              "more map from the same room.");
        ImGui::SameLine();
        // The project's world scale is the right answer, so hand-tuning it
        // away needs one click back.
        if (ui::GhostButton("World", worldW, phoneMap_.scale != project_.settings.unitsPerMeter))
            phoneMap_.scale = project_.settings.unitsPerMeter;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Back to the project's %.3f units per meter\n"
                              "(Project Preferences > World).",
                              project_.settings.unitsPerMeter);
    }
    {
        const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ui::GhostButton("Recentre", half)) phoneCamRecenter();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip(phoneRecTarget_.empty()
                                  ? "Back to the editor's own camera, aimed where the\n"
                                    "viewport is looking. Everything the phone does\n"
                                    "is relative to this point."
                                  : "Back to where the selected camera was PLACED -\n"
                                    "its position, aim and tilt. Everything the phone\n"
                                    "does is relative to that pose.");
        ImGui::SameLine();
        // "Drive the viewport camera": the phone's motion moves the editor
        // camera (and so the image it streams); off keeps the link up while
        // the viewport is yours again.
        if (ui::GhostButton(phoneDrive_ ? "Give the viewport back###Drive the viewport camera"
                                        : "Drive the viewport###Drive the viewport camera",
                            half)) {
            phoneDrive_ = !phoneDrive_;
            if (!phoneDrive_) phoneCamPushed_ = false;
        }
        uiscript::markLastItemChecked(phoneDrive_);
    }
    {
        const float room = 3.0f;
        ImGui::TextDisabled("%.0f m in your room = %.0f units in the scene.", room, room * phoneMap_.scale);
    }
    if (ui::Section("Placement")) {
        ui::InspectorRows rows(scaled(80.0f), scaled(120.0f));
        // The start point, editable directly: Recentre snaps it to the viewport
        // camera, but a shot often wants an exact spot - and the phone can fly
        // it too (its Move mode), which writes the same three numbers.
        if (ui::AxisDrag3("Start point", phoneMap_.origin, 0.1f)) phoneMap_.hasAnchor = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Where the phone's motion starts from. Drag to place it\n"
                              "exactly, or fly it live from the app (its Move button).");
        if (ui::GhostButton("From view")) {
            float eye[3], at[3];
            viewport_.currentCamera(eye, at);
            for (int c = 0; c < 3; ++c) phoneMap_.origin[c] = eye[c];
            phoneMap_.hasAnchor = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Move the start point to the editor camera, leaving the\n"
                              "aim alone (Recentre does both).");
        ui::DragFloat("Yaw", &phoneMap_.yawDeg, 1.0f, -360.0f, 360.0f, "%.0f deg");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Which way the phone's forward points in the scene.\n"
                              "Recentre sets this from the viewport view.");
        char tiltFmt[48];
        std::snprintf(tiltFmt, sizeof(tiltFmt), "%%.2f  (roll %.1f deg)", phoneRoll_);
        ui::SliderFloat("Tilt", &phoneMap_.rollScale, 0.0f, 1.0f, tiltFmt);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("How much of the phone's own tilt about the lens axis\n"
                              "reaches the shot (the Dutch angle). 1 films the lean\n"
                              "as held, 0 pins the horizon level. Recentre makes the\n"
                              "way you are holding it right now count as level.");
    }
    ImGui::Spacing();

    // --- record into a cutscene ----------------------------------------------------------
    // The thing this feature exists for, so it lives here rather than in the
    // Director: the sequence is the Director's own selection.
    {
        ui::Caption("Record into a cutscene");
        const char* hint = "the phone's REC does this too";
        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(hint).x);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - fs * 0.15f);
        ImGui::TextDisabled("%s", hint);
    }
    const bool haveSeq = selectedSequence_ >= 0 && selectedSequence_ < (int)project_.sequences.size();
    {
        ui::InspectorRows rows(scaled(80.0f), scaled(120.0f));
        ImGui::BeginDisabled(phoneRec_);
        if (ui::BeginCombo("Sequence", haveSeq ? project_.sequences[selectedSequence_].name.c_str()
                                               : "<pick a cutscene>")) {
            for (int i = 0; i < (int)project_.sequences.size(); ++i) {
                ImGui::PushID(i);
                if (ImGui::Selectable(project_.sequences[i].name.c_str(), i == selectedSequence_)) {
                    selectedSequence_ = i;
                    selectedSeqTrack_ = -1;
                    selectedSeqKey_ = -1;
                }
                ImGui::PopID();
            }
            if (project_.sequences.empty()) ImGui::TextDisabled("No cutscenes yet.");
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        // Where the keys land follows View from (one intent, one control).
        {
            const char* cap = "Into";
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", cap);
            ImGui::SameLine(ImClamp(ImGui::GetContentRegionAvail().x * 0.34f, scaled(80.0f), scaled(120.0f)));
            if (phoneRecTarget_.empty()) {
                ImGui::TextUnformatted("free camera shots");
            } else {
                ImGui::Text("%s's track", phoneRecTarget_.c_str());
            }
            ImGui::SameLine(0.0f, fs * 0.3f);
            ImGui::TextDisabled("\xc2\xb7 follows View from");
        }
        // Keyframe density. Every frame is the sync-with-the-game option: one
        // key per rendered frame, so nothing is interpolated.
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Keys");
            ImGui::SameLine(ImClamp(ImGui::GetContentRegionAvail().x * 0.34f, scaled(80.0f), scaled(120.0f)));
            char rate[24];
            std::snprintf(rate, sizeof(rate), "%.0f / s##dens", phoneDensity_);
            const char* opts[] = {"Every frame", rate, "Optimize"};
            ImGui::BeginDisabled(phoneRec_);
            ui::Segmented("##phone_density", opts, 3, &phoneDensityMode_, -1.0f);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("How densely the move is sampled into keys.\n"
                                  "Every frame = one key per game frame (exact, the biggest table).\n"
                                  "N / s = keys per second. Optimize = drop keys the PS2's own\n"
                                  "interpolation reproduces within an error bound (the smallest).");
            if (phoneDensityMode_ == 1)
                ui::DragFloat("Rate", &phoneDensity_, 0.5f, 0.5f, 60.0f, "%.1f keys/s");
            else if (phoneDensityMode_ == 2)
                ui::SliderFloat("Tolerance", &phoneTolerance_, 0.005f, 1.0f, "%.3f u",
                                ImGuiSliderFlags_Logarithmic);
        }
        ui::Switch("Start at playhead", &phoneRecAtPlayhead_);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Recorded keys begin at the playhead (%.2f s) instead of\n"
                              "at t = 0, so a move can be dropped after an existing shot.",
                              seqPlayhead_);
    }
    ImGui::Spacing();
    {
        const bool canRec = haveSeq && phoneHasPose_;
        char lbl[64];
        if (phoneRec_)
            std::snprintf(lbl, sizeof(lbl), "Stop  %.1f s \xc2\xb7 %d keys###Record", (float)phoneTake_.duration(),
                          phoneRecStats_.keyCount);
        else
            std::snprintf(lbl, sizeof(lbl), "Record###Record");
        if (ui::RecordButton(lbl, phoneRec_, -1.0f, phoneRec_ || canRec)) {
            if (phoneRec_) stopPhoneRecording();
            else startPhoneRecording();
        }
        // what it costs, in one line
        float perSec = phoneDensityMode_ == 0 ? projectFrameRate() : phoneDensity_;
        char cost[96];
        if (!haveSeq)
            std::snprintf(cost, sizeof(cost), "Pick a cutscene to record into.");
        else if (!phoneHasPose_)
            std::snprintf(cost, sizeof(cost), "Waiting for a pose...");
        else if (phoneDensityMode_ == 2)
            std::snprintf(cost, sizeof(cost), "only the keys needed \xc2\xb7 warns past 600 \xc2\xb7 one undo step");
        else
            std::snprintf(cost, sizeof(cost), "~%.0f keys a second \xc2\xb7 warns past 600 \xc2\xb7 one undo step",
                          perSec);
        const float cw = ImGui::CalcTextSize(cost).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, (ImGui::GetContentRegionAvail().x - cw) * 0.5f));
        ImGui::TextDisabled("%s", cost);
        if (phoneRec_ && phoneRecStats_.keyCount > 600)
            ImGui::TextColored(sem.warn, "%d keys is a big table - consider a lower density.",
                               phoneRecStats_.keyCount);
        if (phoneRec_ && phoneRecSeq_ != selectedSequence_ && phoneRecSeq_ >= 0 &&
            phoneRecSeq_ < (int)project_.sequences.size())
            ImGui::TextDisabled("Recording into \"%s\".", project_.sequences[phoneRecSeq_].name.c_str());
    }
    ImGui::Spacing();

    // --- the stream: machine-wide, set once -----------------------------------------------
    char streamHdr[96];
    std::snprintf(streamHdr, sizeof(streamHdr), "Stream & smoothing  %d \xc2\xb7 %d fps \xc2\xb7 q%d \xc2\xb7 smooth %d###stream",
                  phoneCamPrefs_.maxWidth, phoneCamPrefs_.fps, phoneCamPrefs_.quality, phoneCamPrefs_.smoothing);
    if (ui::Section(streamHdr)) {
        ui::InspectorRows rows(scaled(80.0f), scaled(120.0f));
        bool prefsChanged = false;
        static const char* kSizes[] = {"256", "320", "480", "640", "960"};
        static const int kSizeVals[] = {256, 320, 480, 640, 960};
        int sizeIdx = 2;
        for (int i = 0; i < 5; ++i)
            if (phoneCamPrefs_.maxWidth == kSizeVals[i]) sizeIdx = i;
        if (ui::Combo("Max size", &sizeIdx, kSizes, 5)) {
            phoneCamPrefs_.maxWidth = phoneCamPrefs_.maxHeight = kSizeVals[sizeIdx];
            prefsChanged = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Long-edge cap of the streamed image. The phone can ask\n"
                              "for its own value, which then wins.");
        if (ui::SliderInt("fps", &phoneCamPrefs_.fps, 1, 30)) prefsChanged = true;
        if (ui::SliderInt("JPEG quality", &phoneCamPrefs_.quality, 20, 90)) prefsChanged = true;
        if (ui::SliderInt("Smoothing", &phoneCamPrefs_.smoothing, 0, phonecam::kMaxSmoothing,
                          phoneCamPrefs_.smoothing == 0 ? "%d (lowest latency)" : "%d"))
            prefsChanged = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip(
                "Frames allowed in flight beyond the one being sent.\n"
                "0 is the least delay but the most stutter; 1-2 keeps the\n"
                "cadence even for that many frames of delay (~%.0f ms each\n"
                "at %d fps). The oldest frame is dropped when full.",
                1000.0f / (float)(phoneCamPrefs_.fps > 0 ? phoneCamPrefs_.fps : 15), phoneCamPrefs_.fps);
        if (prefsChanged) {
            phoneCam_.setPreviewDefaults(phoneCamPrefs_);
            saveGlobalConfig();
        }
        if (ui::GhostButton("Disconnect the phone")) phoneCam_.disconnectDevice();
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
        // Each component interpolates between the keys that set it (per-channel
        // keys); a component no key sets keeps the object's own value.
        SceneObject& o = seqPosed_[idx];
        const unsigned on = seqTrackChannels(tr);
        for (int c = 0; c < 9; ++c) {
            if (!(on & (1u << c))) continue;
            float* dst = c < 3 ? &o.position[c] : c < 6 ? &o.rotation[c - 3] : &o.scale[c - 6];
            seqSampleComponent(keys.data(), n, c, t, [c](const SeqObjectKey& k) { return seqKeyFloat(k, c); },
                               *dst);
        }
        if (on & kSeqChColor)
            for (int c = 0; c < 3; ++c)
                seqSampleComponent(keys.data(), n, 9, t, [c](const SeqObjectKey& k) { return k.color[c]; },
                                   o.color[c]);
        if (on & kSeqChVis) {
            const int j = seqVisibilityKey(keys.data(), n, t);
            if (j >= 0 && !keys[j].visible) hidden[idx] = 1;  // steps between keys
        }
    }
    viewport_.setHiddenMask(std::move(hidden));

    // Camera track: fly the preview camera (or release it back to the orbit).
    // Each key is a shot - free (stored eye/at/fov) or bound to a Camera
    // entity, in which case eye/at/fov come from the entity's CURRENT pose in
    // seqPosed_ (object tracks already ran, so an animated camera entity gives
    // a dolly shot). Shots blend across the segment; Step easing = hard cut.
    // The exact same resolution runs in the generated PS2 player. The shot is
    // always resolved (the Director previews it in its own window); it drives
    // the viewport only with "Also drive the viewport" (seqPreview_).
    seqShotValid_ = false;
    seqShotCam_.clear();
    if (s.cameraEnabled && !s.cameraKeys.empty()) {
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
        seqShotValid_ = true;
        for (int c = 0; c < 3; ++c) seqShotEye_[c] = e0[c], seqShotAt_[c] = a0[c];
        seqShotFov_ = f0;
        seqShotRoll_ = r0;
        seqShotShake_ = shake;
        seqShotCam_ = ck[i].camera;
    }
    if (seqShotValid_ && seqPreview_) {
        viewport_.setCameraOverride(seqShotEye_, seqShotAt_, seqShotFov_, seqShotRoll_);
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

// Cutscene Director window (Tools > Cutscene Director). The shot at the
// playhead is the hero (rendered in the window - the viewport stays yours
// unless "Also drive the viewport" is picked), the inspector beside it edits
// the selected key or the sequence's own settings, and the timeline below
// shows camera shots as clips, the fades and bars as ramps, and one lane per
// animated object.
void App::drawCutsceneWindow() {
    if (!showCutsceneEditor_ || !hasProject_) {
        seqWasOpen_ = false;
        return;
    }
    if (!seqWasOpen_) {
        seqWasOpen_ = true;
        seqOpenSeek_ = true;
    }

    ImGui::SetNextWindowSize(ImVec2(scaled(1080), scaled(720)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Cutscene Director", &showCutsceneEditor_)) {
        ImGui::End();
        return;
    }
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();

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
    auto selectSequence = [&](int i) {
        selectedSequence_ = i;
        selectedSeqTrack_ = -1;
        selectedSeqKey_ = -1;
        seqPlayhead_ = 0.0f;
        seqOpenSeek_ = true;
    };
    auto newSequence = [&]() {
        Sequence s;
        s.name = uniqueSeqName("Cutscene");
        project_.sequences.push_back(std::move(s));
        selectSequence((int)project_.sequences.size() - 1);
        seqInspTab_ = 1;
        seqInspTabForce_ = true;
        changed = true;
    };
    if (selectedSequence_ >= (int)project_.sequences.size()) selectedSequence_ = -1;
    // Open on a sequence rather than on an empty pane.
    if (selectedSequence_ < 0 && !project_.sequences.empty()) selectSequence(0);
    const bool haveSeq = selectedSequence_ >= 0;

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

    // --- header: which sequence, who plays it, the phone, import -------------
    {
        const float h = ImGui::GetFrameHeight();
        const char* cur = haveSeq ? project_.sequences[selectedSequence_].name.c_str() : "No sequence";
        ImVec2 ts;
        {
            ui::FontScope bold(ui::boldFont());
            ts = ImGui::CalcTextSize(cur);
        }
        const float w = ts.x + fs * 2.4f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("Pick sequence", ImVec2(w, h))) ImGui::OpenPopup("##seq_switch");
        const float t = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("The cutscene being edited - click to switch or add one.");
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float r = ImGui::GetStyle().FrameRounding;
        ImVec4 fill = sem.surface;
        fill.w = 0.5f + 0.5f * t;
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(fill), r);
        dl->AddRect(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(sem.border), r);
        {
            ui::FontScope bold(ui::boldFont());
            dl->AddText(ImVec2(p.x + fs * 0.6f, p.y + (h - ts.y) * 0.5f), ImGui::GetColorU32(sem.text), cur);
        }
        const float cx = p.x + w - fs * 0.8f, cy = p.y + h * 0.5f;
        dl->AddTriangleFilled(ImVec2(cx - fs * 0.25f, cy - fs * 0.1f), ImVec2(cx + fs * 0.25f, cy - fs * 0.1f),
                              ImVec2(cx, cy + fs * 0.18f), ImGui::GetColorU32(sem.textDim));
    }
    if (ImGui::BeginPopup("##seq_switch")) {
        for (int i = 0; i < (int)project_.sequences.size(); ++i) {
            ImGui::PushID(i);
            if (ImGui::Selectable(project_.sequences[i].name.c_str(), selectedSequence_ == i))
                selectSequence(i);
            ImGui::PopID();
        }
        if (!project_.sequences.empty()) ImGui::Separator();
        if (ImGui::Selectable("+ New sequence")) newSequence();
        ImGui::EndPopup();
    }
    if (haveSeq) {
        // Who starts this cutscene: every Play Sequence naming it, any scene.
        const std::string& nm = project_.sequences[selectedSequence_].name;
        std::vector<std::string> by;
        for (size_t si = 0; si < project_.scenes.size(); ++si)
            for (const SceneObject& o : project_.scenes[si].objects)
                for (const FlowNode& fn : o.flowGraph.nodes) {
                    const FlowNodeType* ft = flowNodeType(fn.type);
                    if (!ft || ft->strKind != FlowParamKind::SequenceName || fn.str != nm) continue;
                    std::string who = o.name;
                    if ((int)si != project_.activeScene) who += " in " + project_.scenes[si].name;
                    by.push_back(who + " (" + ft->title + ")");
                }
        ImGui::SameLine(0.0f, fs * 0.8f);
        ImGui::AlignTextToFramePadding();
        if (by.empty()) {
            ImGui::TextDisabled("not started by any graph yet");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("A Play Sequence flow node starts it in the game.");
        } else {
            ImGui::TextDisabled("played by");
            ImGui::SameLine(0.0f, fs * 0.3f);
            ImGui::TextUnformatted(by.front().c_str());
            if (by.size() > 1) {
                ImGui::SameLine(0.0f, fs * 0.3f);
                ImGui::TextDisabled("+%d", (int)by.size() - 1);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                    std::string all;
                    for (const std::string& b : by) all += b + "\n";
                    ImGui::SetTooltip("%s", all.c_str());
                }
            }
        }
    }
    {
        // The phone link's state, and the way to it: recording lives in the
        // Phone Camera window, this is the one-glance answer to "is it on".
        char pill[64];
        int kind = 4;
        if (phoneRec_) {
            std::snprintf(pill, sizeof(pill), "Phone camera \xc2\xb7 REC###seq_phone");
            kind = 2;
        } else if (phoneCam_.connected()) {
            std::snprintf(pill, sizeof(pill), "Phone camera###seq_phone");
            kind = 0;
        } else if (phoneCam_.listening()) {
            std::snprintf(pill, sizeof(pill), "Phone camera \xc2\xb7 waiting###seq_phone");
            kind = 1;
        } else {
            std::snprintf(pill, sizeof(pill), "Phone camera###seq_phone");
        }
        const float impW = ImGui::CalcTextSize("Import take...").x + ImGui::GetStyle().FramePadding.x * 2.6f;
        const float total = ui::StatusPillWidth(pill) + ImGui::GetStyle().ItemSpacing.x + impW;
        ImGui::SameLine();
        const float right = ImGui::GetWindowContentRegionMax().x;
        if (ImGui::GetCursorPosX() < right - total) ImGui::SetCursorPosX(right - total);
        if (ui::StatusPill(pill, kind)) {
            showPhoneCamWindow_ = true;
            ImGui::SetWindowFocus("Phone Camera");
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Film with your phone and record the move into this\n"
                              "cutscene - opens Tools > Phone Camera.");
        ImGui::SameLine();
        if (ui::GhostButton("Import take...", impW, haveSeq)) beginTakeImport();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Import a phone-recorded 6DoF camera move (CamTrackAR\n"
                              ".hfcs or the CSV spec in docs/camera-takes.md).");
    }

    if (!haveSeq) {
        if (ui::EmptyState(ui::Glyph::Film, "No cutscenes yet",
                           "A sequence poses objects and the camera over time. The Play Sequence "
                           "flow node starts it in the game.",
                           "New sequence"))
            newSequence();
        ImGui::End();
        commitIfEdited();
        return;
    }
    Sequence& s = project_.sequences[selectedSequence_];
    bool deleteSeq = false;

    static const char* kBarsNames[] = {"None", "Cinema 2.39:1", "Wide 16:9", "Pillarbox", "Frame"};

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
        return cutsceneSnapshotObjectKey(tr, time) >= 0;
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

    // Playback: Space toggles while the Director is focused (not while typing
    // or dragging - Space also clicks a focused button).
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() &&
        ImGui::IsKeyPressed(ImGuiKey_Space, false))
        seqPlaying_ = !seqPlaying_;
    // Open on content, not on the black first frame of the fade-in (the
    // Credits preview does the same). Only an untouched playhead moves.
    if (seqOpenSeek_) {
        seqOpenSeek_ = false;
        if (seqPlayhead_ == 0.0f && !seqPlaying_ && s.fadeIn > 0.0f) seqPlayhead_ = std::min(s.fadeIn, s.duration);
    }
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

    // Per-camera and per-track colours: data tints from the theme's own
    // meanings, so a camera is the same colour in its clip and its lane.
    const ImVec4 kTints[] = {sem.accent, sem.ok, sem.warn, sem.special, sem.danger, sem.axisZ};
    auto tintOf = [&](const std::string& name) {
        uint32_t h = 2166136261u;
        for (char c : name) h = (h ^ (unsigned char)c) * 16777619u;
        return kTints[h % (sizeof(kTints) / sizeof(kTints[0]))];
    };
    auto withA = [](ImVec4 c, float a) {
        c.w = a;
        return c;
    };
    auto mixc = [](const ImVec4& a, const ImVec4& b, float t) {
        return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                      a.w + (b.w - a.w) * t);
    };

    // --- body: [transport + timeline] | [shot over inspector] -------------------
    // The timeline takes the whole height - a track opened down to its axes
    // needs rows - and the shot and the inspector share a side column.
    const float thick = ImMax(6.0f, fs * 0.45f);  // the toolkit splitter's
    const float bodyW = ImGui::GetContentRegionAvail().x;
    const float bodyH = ImGui::GetContentRegionAvail().y;
    if (seqInspW_ <= 0.0f) seqInspW_ = scaled(340.0f);
    seqInspW_ = ImClamp(seqInspW_, scaled(260.0f), ImMax(scaled(260.0f), bodyW * 0.5f));
    const float mainW = ImMax(scaled(240.0f), bodyW - seqInspW_ - thick);

    // The animatable channels of an object track, in row order. Position,
    // rotation and scale open into one row per axis.
    struct SeqChannelDef {
        const char* name;
        unsigned bits;
        int axes;
    };
    static const SeqChannelDef kChannels[] = {{"Position", kSeqChPos, 3},
                                              {"Rotation", kSeqChRot, 3},
                                              {"Scale", kSeqChScale, 3},
                                              {"Color", kSeqChColor, 1},
                                              {"Visibility", kSeqChVis, 1}};
    static const char* kAxisNames[] = {"X", "Y", "Z"};
    auto channelFlag = [](SeqTrack& tr, int ch) -> bool& {
        return ch == 0 ? tr.animPos : ch == 1 ? tr.animRot : ch == 2 ? tr.animScale : ch == 3 ? tr.animColor : tr.animVis;
    };
    // "Position X", "Position", "Position, Rotation Y" - what a key or a row
    // stands for, in the words the rows use.
    auto scopeName = [&](unsigned bits) -> std::string {
        if ((bits & kSeqChAll) == kSeqChAll) return "every channel";
        std::string out;
        int n = 0;
        for (int ch = 0; ch < 5; ++ch) {
            const unsigned b = bits & kChannels[ch].bits;
            if (!b) continue;
            if (++n > 2) continue;  // counted below
            std::string part = kChannels[ch].name;
            if (kChannels[ch].axes == 3 && b != kChannels[ch].bits)
                for (int a = 0; a < 3; ++a)
                    if (b & (1u << (ch * 3 + a))) part += std::string(" ") + kAxisNames[a];
            out += (out.empty() ? "" : ", ") + part;
        }
        if (n > 2) out += " +" + std::to_string(n - 2);
        return out;
    };

    int deleteTrack = -1;
    ImGui::BeginChild("##seq_main", ImVec2(mainW, 0));
    // --- transport -----------------------------------------------------------------
    {
        const float h = ImGui::GetFrameHeight();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // rewind: a bar and a left triangle
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("Rewind", ImVec2(h, h))) {
                seqPlayhead_ = 0.0f;
                seqPlaying_ = false;
            }
            const float t = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
            if (t > 0.0f)
                dl->AddRectFilled(p, ImVec2(p.x + h, p.y + h), ImGui::GetColorU32(withA(sem.surface, t)),
                                  ImGui::GetStyle().FrameRounding);
            const ImU32 ink = ImGui::GetColorU32(mixc(sem.textDim, sem.text, t));
            const float c = h * 0.5f, s2 = h * 0.2f;
            dl->AddRectFilled(ImVec2(p.x + c - s2 - 2.0f, p.y + c - s2), ImVec2(p.x + c - s2, p.y + c + s2), ink);
            dl->AddTriangleFilled(ImVec2(p.x + c + s2, p.y + c - s2), ImVec2(p.x + c + s2, p.y + c + s2),
                                  ImVec2(p.x + c - s2 + 1.0f, p.y + c), ink);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Back to 0");
        }
        ImGui::SameLine();
        // play / pause: a filled round button
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton(seqPlaying_ ? "Pause###Play" : "Play###Play", ImVec2(h, h)))
                seqPlaying_ = !seqPlaying_;
            const float t = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
            const ImVec2 c(p.x + h * 0.5f, p.y + h * 0.5f);
            dl->AddCircleFilled(c, h * 0.5f, ImGui::GetColorU32(mixc(sem.surface, sem.text, 0.08f + 0.1f * t)), 24);
            const ImU32 ink = ImGui::GetColorU32(sem.text);
            const float s2 = h * 0.18f;
            if (seqPlaying_) {
                dl->AddRectFilled(ImVec2(c.x - s2, c.y - s2), ImVec2(c.x - s2 * 0.3f, c.y + s2), ink);
                dl->AddRectFilled(ImVec2(c.x + s2 * 0.3f, c.y - s2), ImVec2(c.x + s2, c.y + s2), ink);
            } else {
                dl->AddTriangleFilled(ImVec2(c.x - s2 * 0.7f, c.y - s2), ImVec2(c.x - s2 * 0.7f, c.y + s2),
                                      ImVec2(c.x + s2 * 1.1f, c.y), ink);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Play / pause (Space)");
        }
        ImGui::SameLine(0.0f, fs * 0.8f);
        {
            auto tc = [](float sec, char* out, size_t n) {
                const int m = (int)(sec / 60.0f);
                std::snprintf(out, n, "%d:%05.2f", m, sec - (float)m * 60.0f);
            };
            char a[24], b[24];
            tc(seqPlayhead_, a, sizeof(a));
            tc(s.duration, b, sizeof(b));
            ui::FontScope mono(ui::monoFont());
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(a);
            ImGui::SameLine(0.0f, fs * 0.3f);
            ImGui::TextDisabled("/ %s", b);
        }
        ImGui::SameLine(0.0f, fs * 0.8f);
        if (ui::FilterChip("Auto-key", seqAutoKey_)) seqAutoKey_ = !seqAutoKey_;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Finishing a gizmo drag drops a keyframe at the\n"
                              "playhead for every selected object that has a\n"
                              "track in this sequence.");
        ImGui::SameLine();
        if (ui::FilterChip("Loop##transport", s.loop)) {
            s.loop = !s.loop;
            changed = true;
        }
        if (phoneRec_) {
            ImGui::SameLine();
            char rec[64];
            std::snprintf(rec, sizeof(rec), "REC %.1f s \xc2\xb7 %d keys", (float)phoneTake_.duration(),
                          phoneRecStats_.keyCount);
            ui::Badge(rec, 2);
        }
        // right end: the hint and the zoom
        const char* hint = "Double-click a lane to key \xc2\xb7 drag keys to retime";
        const float zw = ImGui::CalcTextSize("-").x + ImGui::CalcTextSize("+").x +
                         ImGui::GetStyle().FramePadding.x * 5.2f + ImGui::GetStyle().ItemSpacing.x;
        const float hw = ImGui::CalcTextSize(hint).x + fs;
        const float right = ImGui::GetWindowContentRegionMax().x;
        ImGui::SameLine();
        if (ImGui::GetCursorPosX() + hw + zw < right) {
            ImGui::SetCursorPosX(right - hw - zw);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", hint);
            ImGui::SameLine();
        } else if (ImGui::GetCursorPosX() < right - zw) {
            ImGui::SetCursorPosX(right - zw);
        }
        if (ui::GhostButton("-##seqzoom")) seqZoom_ = ImMax(1.0f, seqZoom_ / 1.25f);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Zoom out (Ctrl + wheel)");
        ImGui::SameLine();
        if (ui::GhostButton("+##seqzoom")) seqZoom_ = ImMin(8.0f, seqZoom_ * 1.25f);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Zoom in (Ctrl + wheel)");
    }

    // --- the timeline --------------------------------------------------------------
    // Rows: the camera's shots as clips (when the camera track is on), the
    // fades & bars as ramps, then a tree per object track - the track row
    // (every key), a row per animated channel and, opened, a row per axis. A
    // diamond stands for the components of the row it sits on: dragged or
    // deleted on the Position X row it moves or removes position X only. A
    // click/drag-scrubbed ruler and the playhead run across all of them; the
    // label column stays pinned while the lanes scroll horizontally.
    enum RowKind { RowCamera, RowFades, RowTrack, RowChannel, RowAxis, RowAdd };
    struct Row {
        RowKind kind;
        int ti;         // object track (-1 camera, -2 fades)
        int ch;         // kChannels index (channel / axis rows)
        int axis;       // 0-2 (axis rows)
        unsigned bits;  // the components the row shows
        int depth;
    };
    auto openKey = [&](const SeqTrack& tr, int ch) {
        return ch < 0 ? tr.target : tr.target + "/" + kChannels[ch].name;
    };
    std::vector<Row> rowList;
    if (s.cameraEnabled) rowList.push_back({RowCamera, -1, -1, -1, 0u, 0});
    rowList.push_back({RowFades, -2, -1, -1, 0u, 0});
    for (int ti = 0; ti < (int)s.tracks.size(); ++ti) {
        const SeqTrack& tr = s.tracks[ti];
        rowList.push_back({RowTrack, ti, -1, -1, (unsigned)kSeqChAll, 0});
        if (!seqOpenRows_.count(openKey(tr, -1))) continue;
        const unsigned on = seqTrackChannels(tr);
        bool anyOff = false;
        for (int ch = 0; ch < 5; ++ch) {
            if (!(on & kChannels[ch].bits)) {
                anyOff = true;
                continue;
            }
            rowList.push_back({RowChannel, ti, ch, -1, kChannels[ch].bits, 1});
            if (kChannels[ch].axes == 3 && seqOpenRows_.count(openKey(tr, ch)))
                for (int a = 0; a < 3; ++a) rowList.push_back({RowAxis, ti, ch, a, 1u << (ch * 3 + a), 2});
        }
        if (anyOff) rowList.push_back({RowAdd, ti, -1, -1, 0u, 1});
    }
    const float laneH = scaled(26.0f), rulerH = scaled(24.0f), labelW = scaled(250.0f);
    const int camLanes = s.cameraEnabled ? 1 : 0;
    const int fadeLane = camLanes;  // row index of "Fades & bars"
    const int laneCount = (int)rowList.size();
    // sanity: a deleted/toggled lane (or an undo) can strand the selection
    if (selectedSeqTrack_ >= (int)s.tracks.size() || (selectedSeqTrack_ < 0 && !s.cameraEnabled))
        selectedSeqKey_ = -1;
    if (selectedSeqTrack_ >= 0 && selectedSeqTrack_ < (int)s.tracks.size() &&
        selectedSeqKey_ >= (int)s.tracks[selectedSeqTrack_].keys.size())
        selectedSeqKey_ = -1;
    if (seqDragTrack_ >= (int)s.tracks.size() ||
        (seqDragTrack_ >= 0 && seqDragKey_ >= (int)s.tracks[seqDragTrack_].keys.size()))
        seqDragTrack_ = -1;
    // the value an object shows at the playhead (the posed copy)
    auto posedOf = [&](const std::string& name) -> const SceneObject* {
        for (const SceneObject& o : seqPosed_)
            if (o.name == name) return &o;
        return nullptr;
    };

    ImGui::BeginChild("##dopesheet", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    {
        // Ctrl + mouse wheel zooms the timeline (like the flow graph); a plain
        // wheel keeps scrolling the dopesheet.
        if (ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.0f) {
            seqZoom_ = ImClamp(seqZoom_ * ImPow(1.1f, ImGui::GetIO().MouseWheel), 1.0f, 8.0f);
            ImGui::GetIO().MouseWheel = 0.0f;  // consume: don't also scroll
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();  // content space
        const float visibleW = ImGui::GetWindowSize().x;
        float timeW = (visibleW - labelW - scaled(12.0f)) * seqZoom_;
        if (timeW < scaled(160.0f)) timeW = scaled(160.0f);
        const float pps = timeW / s.duration;  // pixels per second
        const float x0 = origin.x + labelW + scaled(4.0f);
        const float contentH = rulerH + laneCount * laneH;
        const float addRowH = ImGui::GetFrameHeight() + ImGui::GetStyle().ScrollbarSize + scaled(10.0f);
        ImGui::Dummy(ImVec2(labelW + timeW + scaled(12.0f), contentH + addRowH));
        const float laneY0 = origin.y + rulerH;
        const float labelX = ImGui::GetWindowPos().x;  // pinned label column x
        // The ruler is pinned to the top like the label column to the left:
        // however far the rows scroll, the time stays there to scrub. Rows
        // draw (and take the mouse) only below it - items outside the clip
        // rect are not hoverable, so a key scrolled under it stays out of reach.
        // (the cursor start already has the scroll taken off - add it back)
        const float rulerY = ImGui::GetWindowPos().y + ImGui::GetCursorStartPos().y + ImGui::GetScrollY();
        const ImVec2 winMax(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                            ImGui::GetWindowPos().y + ImGui::GetWindowSize().y);
        ImGui::PushClipRect(ImVec2(labelX, rulerY + rulerH), winMax, true);

        const ImU32 colGrid = ImGui::GetColorU32(ImGuiCol_Border);
        const ImU32 colText = ImGui::GetColorU32(sem.text);
        const ImU32 colDim = ImGui::GetColorU32(sem.textDim);
        const ImU32 colPlayhead = ImGui::GetColorU32(sem.danger);
        auto timeAtMouse = [&]() {
            float t = (ImGui::GetIO().MousePos.x - x0) / pps;
            t = t < 0.0f ? 0.0f : (t > s.duration ? s.duration : t);
            return std::round(t * 100.0f) / 100.0f;  // snap to 10 ms
        };
        auto xAt = [&](float t) { return x0 + t * pps; };

        auto selectRowKey = [&](int ti, int ki, unsigned bits) {
            selectedSeqTrack_ = ti;
            selectedSeqKey_ = ki;
            selectedSeqBits_ = bits;
            seqInspTab_ = 0;
            seqInspTabForce_ = true;
        };

        // lane backgrounds: hairlines; the camera lane and the track rows a
        // touch raised, so a track's channel rows read as its children
        for (int li = 0; li < laneCount; ++li) {
            const float y = laneY0 + li * laneH;
            const Row& row = rowList[li];
            if (row.kind == RowCamera || row.kind == RowTrack)
                dl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + timeW, y + laneH),
                                  ImGui::GetColorU32(withA(sem.surface, row.kind == RowCamera ? 0.35f : 0.22f)));
            dl->AddLine(ImVec2(labelX, y + laneH), ImVec2(x0 + timeW, y + laneH),
                        ImGui::GetColorU32(withA(sem.border, row.depth == 0 ? 0.6f : 0.3f)));
        }

        // lane hit areas: double-click an empty spot = key that row's
        // components there. AllowOverlap so the clips and keys submitted on
        // top still catch the mouse - without it the lane eats every click.
        for (int li = 0; li < laneCount; ++li) {
            const Row& row = rowList[li];
            if (row.kind == RowFades || row.kind == RowAdd) continue;
            ImGui::PushID(100 + li);
            ImGui::SetCursorScreenPos(ImVec2(x0, laneY0 + li * laneH));
            ImGui::SetNextItemAllowOverlap();
            ImGui::InvisibleButton("##lane", ImVec2(timeW, laneH));
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                const float t = timeAtMouse();
                if (row.kind == RowCamera) {
                    if (snapshotCameraKey(t)) {
                        selectedSeqTrack_ = -1;
                        selectedSeqKey_ = -1;  // select the dropped shot
                        for (int i = 0; i < (int)s.cameraKeys.size(); ++i)
                            if (s.cameraKeys[i].time == t) selectedSeqKey_ = i;
                        seqPlayhead_ = t;
                        seqInspTab_ = 0;
                        seqInspTabForce_ = true;
                        changed = true;
                    } else {
                        statusMessage_ = "Add a Camera entity first (+ Add object > Gameplay > Camera)";
                    }
                } else {
                    const bool whole = row.kind == RowTrack;
                    const int ki = cutsceneSnapshotObjectKey(s.tracks[row.ti], t, whole ? 0u : row.bits);
                    if (ki >= 0) {
                        selectRowKey(row.ti, ki, whole ? (unsigned)kSeqChAll : row.bits);
                        seqPlayhead_ = t;
                        changed = true;
                    }
                }
            }
            ImGui::PopID();
        }
        // the ruler's step (its ticks are drawn pinned, at the end); the major
        // ticks run down the lanes as grid lines
        static const float kSteps[] = {0.1f, 0.2f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 15.0f, 30.0f, 60.0f};
        float step = 60.0f;
        for (float c : kSteps)
            if (c * pps >= scaled(56.0f)) {
                step = c;
                break;
            }
        const float minor = step / 5.0f;
        for (float t = 0.0f; t <= s.duration + 1e-4f; t += step)
            dl->AddLine(ImVec2(xAt(t), laneY0), ImVec2(xAt(t), laneY0 + laneCount * laneH),
                        ImGui::GetColorU32(ImGuiCol_Border, 0.35f));

        // One key's hit box: click selects, drag retimes, right-click opens
        // easing/delete. Returns 0 untouched, 1 edited, 2 commit, 3 delete.
        static const char* kEaseNames[] = {"Linear", "Smooth", "Step (hold)"};
        auto keyHit = [&](int lane, int ki, float& time, int& easing, ImVec2 c, float r,
                          bool& hovered) -> int {
            int result = 0;
            const float pad = scaled(4.0f);
            ImGui::PushID((lane + 2) * 1000 + ki);
            ImGui::SetCursorScreenPos(ImVec2(c.x - r - pad, c.y - r - pad));
            ImGui::InvisibleButton("##key", ImVec2(2.0f * (r + pad), 2.0f * (r + pad)));
            hovered = ImGui::IsItemHovered();
            const bool dragging = ImGui::IsItemActive();
            if (hovered || dragging) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (hovered && !dragging)
                ImGui::SetTooltip("%.2f s - drag to retime,\nright-click for easing/delete", time);
            if (ImGui::IsItemActivated()) {
                selectedSeqTrack_ = lane;
                selectedSeqKey_ = ki;
                seqInspTab_ = 0;
                seqInspTabForce_ = true;
            }
            if (dragging && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f)) {
                time = timeAtMouse();
                seqPlayhead_ = time;
                result = 1;
            }
            // sort + commit outside - only when it was dragged: a click that
            // merely selects the key is not an edit
            if (ImGui::IsItemDeactivated() && ImGui::GetIO().MouseDragMaxDistanceSqr[0] > 1.0f) result = 2;
            if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) seqPlayhead_ = time;
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
            ImGui::PopID();
            return result;
        };

        // --- camera lane: shots as clips ----------------------------------------
        if (s.cameraEnabled) {
            const float y0 = laneY0 + scaled(3.0f), y1 = laneY0 + laneH - scaled(3.0f);
            const int n = (int)s.cameraKeys.size();
            int del = -1, resort = -1;
            for (int ki = 0; ki < n; ++ki) {
                SeqCameraKey& k = s.cameraKeys[ki];
                const float tEnd = ki + 1 < n ? s.cameraKeys[ki + 1].time : s.duration;
                const float cx0 = xAt(k.time), cx1 = ImMax(cx0 + scaled(4.0f), xAt(tEnd) - scaled(2.0f));
                const std::string name = k.camera.empty() ? std::string("free shot") : k.camera;
                const ImVec4 tint = tintOf(name);
                const bool sel = selectedSeqTrack_ == -1 && selectedSeqKey_ == ki;
                // the clip body: click selects the shot, double-click parks the
                // playhead on it
                ImGui::PushID(5000 + ki);
                ImGui::SetCursorScreenPos(ImVec2(cx0, y0));
                ImGui::SetNextItemAllowOverlap();
                ImGui::InvisibleButton("##clip", ImVec2(ImMax(1.0f, cx1 - cx0), y1 - y0));
                if (ImGui::IsItemClicked()) {
                    selectedSeqTrack_ = -1;
                    selectedSeqKey_ = ki;
                    seqInspTab_ = 0;
                    seqInspTabForce_ = true;
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    seqPlayhead_ = k.time;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("%s \xc2\xb7 %.2f - %.2f s \xc2\xb7 %s into the next", name.c_str(), k.time,
                                      tEnd, k.easing == 2 ? "cut" : k.easing == 1 ? "smooth" : "linear");
                ImGui::PopID();
                // fill, and a blend into the next shot's colour for Smooth/Linear
                const float rr = scaled(4.0f);
                const ImVec4 body = mixc(sem.surface, tint, 0.28f);
                dl->AddRectFilled(ImVec2(cx0, y0), ImVec2(cx1, y1), ImGui::GetColorU32(body), rr);
                if (ki + 1 < n && k.easing != 2) {
                    const std::string nextName =
                        s.cameraKeys[ki + 1].camera.empty() ? std::string("free shot") : s.cameraKeys[ki + 1].camera;
                    const ImVec4 nb = mixc(sem.surface, tintOf(nextName), 0.28f);
                    const float gx = cx0 + (cx1 - cx0) * (k.easing == 1 ? 0.55f : 0.0f);
                    dl->AddRectFilledMultiColor(ImVec2(gx, y0 + 1.0f), ImVec2(cx1, y1 - 1.0f),
                                                ImGui::GetColorU32(body), ImGui::GetColorU32(nb),
                                                ImGui::GetColorU32(nb), ImGui::GetColorU32(body));
                }
                dl->AddRect(ImVec2(cx0, y0), ImVec2(cx1, y1),
                            ImGui::GetColorU32(sel ? sem.text : withA(tint, 0.7f)), rr, 0, sel ? 2.0f : 1.0f);
                dl->PushClipRect(ImVec2(cx0, y0), ImVec2(cx1 - scaled(2.0f), y1), true);
                const float ty = (y0 + y1 - ImGui::GetTextLineHeight()) * 0.5f;
                dl->AddCircleFilled(ImVec2(cx0 + scaled(9.0f), (y0 + y1) * 0.5f), scaled(3.5f),
                                    ImGui::GetColorU32(tint));
                dl->AddText(ImVec2(cx0 + scaled(17.0f), ty), colText, name.c_str());
                dl->PopClipRect();
                // the cut handle at the clip's left edge: drag to retime
                bool hov = false;
                const int res = keyHit(-1, ki, k.time, k.easing, ImVec2(cx0, (y0 + y1) * 0.5f), scaled(4.0f), hov);
                if (hov) dl->AddRectFilled(ImVec2(cx0 - 1.5f, y0), ImVec2(cx0 + 1.5f, y1), colText);
                if (res == 2) resort = ki;
                if (res == 3) del = ki;
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

        // --- fades & bars: ramps at both ends -------------------------------------
        {
            const float y = laneY0 + fadeLane * laneH;
            const float y0 = y + scaled(7.0f), y1 = y + laneH - scaled(7.0f);
            ImGui::PushID("fades");
            ImGui::SetCursorScreenPos(ImVec2(x0, y));
            ImGui::InvisibleButton("##fadelane", ImVec2(timeW, laneH));
            if (ImGui::IsItemClicked()) {
                seqInspTab_ = 1;
                seqInspTabForce_ = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Fades and bars are set on the Sequence tab - click to open it.");
            ImGui::PopID();
            const ImU32 black = IM_COL32(0, 0, 0, 255), clear = IM_COL32(0, 0, 0, 0);
            const ImU32 barCol = ImGui::GetColorU32(withA(sem.textDim, 0.55f));
            const ImU32 barClear = ImGui::GetColorU32(withA(sem.textDim, 0.0f));
            const float ym = (y0 + y1) * 0.5f;
            if (s.fadeIn > 0.0f)
                dl->AddRectFilledMultiColor(ImVec2(xAt(0), y0), ImVec2(xAt(s.fadeIn), ym), black, clear, clear, black);
            if (s.fadeOut > 0.0f)
                dl->AddRectFilledMultiColor(ImVec2(xAt(s.duration - s.fadeOut), y0), ImVec2(xAt(s.duration), ym),
                                            clear, black, black, clear);
            if (s.bars != kSeqBarsNone) {
                const float bi = ImMax(s.barsSlideIn, 0.01f), bo = ImMax(s.barsSlideOut, 0.01f);
                dl->AddRectFilledMultiColor(ImVec2(xAt(0), ym + 1.0f), ImVec2(xAt(bi), y1), barClear, barCol, barCol,
                                            barClear);
                dl->AddRectFilled(ImVec2(xAt(bi), ym + 1.0f), ImVec2(xAt(s.duration - bo), y1),
                                  ImGui::GetColorU32(withA(sem.textDim, 0.18f)));
                dl->AddRectFilledMultiColor(ImVec2(xAt(s.duration - bo), ym + 1.0f), ImVec2(xAt(s.duration), y1),
                                            barCol, barClear, barClear, barCol);
            }
            const bool bars = s.bars != kSeqBarsNone;
            const float ty = y + (laneH - ImGui::GetTextLineHeight()) * 0.5f;
            if (!bars && s.fadeIn <= 0.0f && s.fadeOut <= 0.0f) {
                dl->AddText(ImVec2(x0 + scaled(8.0f), ty), colDim, "no fades or bars");
            } else {
                char a[64], b[64];
                if (bars) {
                    std::snprintf(a, sizeof(a), "fade %.1f s \xc2\xb7 bars %.1f s", s.fadeIn, s.barsSlideIn);
                    std::snprintf(b, sizeof(b), "fade %.1f s \xc2\xb7 bars %.1f s", s.fadeOut, s.barsSlideOut);
                } else {
                    std::snprintf(a, sizeof(a), "fade %.1f s", s.fadeIn);
                    std::snprintf(b, sizeof(b), "fade %.1f s", s.fadeOut);
                }
                const float aw = ImGui::CalcTextSize(a).x, bw = ImGui::CalcTextSize(b).x;
                const float ax = xAt(ImMax(s.fadeIn, s.bars != kSeqBarsNone ? s.barsSlideIn : 0.0f)) + scaled(8.0f);
                const float bx = xAt(s.duration - ImMax(s.fadeOut, s.bars != kSeqBarsNone ? s.barsSlideOut : 0.0f)) -
                                 scaled(8.0f) - bw;
                if (ax + aw < bx - scaled(12.0f)) {
                    dl->AddText(ImVec2(ax, ty), colDim, a);
                    dl->AddText(ImVec2(bx, ty), colDim, b);
                }
            }
        }

        // --- object rows: span bar, visibility on-spans, key diamonds -------------
        // A row shows the keys that set any of its components, and a diamond
        // stands for those components only (on the track row: the whole key).
        static const char* kObjEaseNames[] = {"Linear", "Smooth", "Hold"};
        for (int li = 0; li < laneCount; ++li) {
            const Row& row = rowList[li];
            if (row.kind != RowTrack && row.kind != RowChannel && row.kind != RowAxis) continue;
            const int ti = row.ti;
            SeqTrack& tr = s.tracks[ti];
            const float y = laneY0 + li * laneH, cy = y + laneH * 0.5f;
            const ImVec4 tint = tintOf(tr.target);
            float tFirst = -1.0f, tLast = -1.0f;
            for (const SeqObjectKey& k : tr.keys)
                if (k.mask & row.bits) {
                    if (tFirst < 0.0f || k.time < tFirst) tFirst = k.time;
                    tLast = ImMax(tLast, k.time);
                }
            if (tFirst >= 0.0f)
                dl->AddLine(ImVec2(xAt(tFirst), cy), ImVec2(xAt(tLast), cy),
                            ImGui::GetColorU32(withA(tint, row.depth == 2 ? 0.35f : 0.45f)), scaled(2.0f));
            // visibility on-spans: on the track row (when animated) and the Visibility row
            if ((row.kind == RowTrack && tr.animVis) || (row.kind == RowChannel && row.bits == kSeqChVis)) {
                std::vector<int> vis;
                for (int i = 0; i < (int)tr.keys.size(); ++i)
                    if (tr.keys[i].mask & kSeqChVis) vis.push_back(i);
                std::sort(vis.begin(), vis.end(), [&](int a, int b) { return tr.keys[a].time < tr.keys[b].time; });
                for (size_t j = 0; j < vis.size(); ++j) {
                    if (!tr.keys[vis[j]].visible) continue;
                    const float tEnd = j + 1 < vis.size() ? tr.keys[vis[j + 1]].time : s.duration;
                    dl->AddRectFilled(ImVec2(xAt(tr.keys[vis[j]].time), cy - scaled(5.0f)), ImVec2(xAt(tEnd), cy + scaled(5.0f)),
                                      ImGui::GetColorU32(withA(tint, 0.30f)), scaled(2.0f));
                }
            }
            const float r = scaled(row.depth == 0 ? 6.0f : row.depth == 1 ? 5.0f : 4.5f);
            const int nKeys = (int)tr.keys.size();
            int del = -1;
            unsigned delBits = 0;
            for (int ki = 0; ki < nKeys; ++ki) {
                const unsigned kb = tr.keys[ki].mask & row.bits;
                if (!kb) continue;
                const float kx = xAt(tr.keys[ki].time);
                const float pad = scaled(4.0f);
                ImGui::PushID((li + 2) * 1000 + ki);
                ImGui::SetCursorScreenPos(ImVec2(kx - r - pad, cy - r - pad));
                // named "<row> @ <time>" ("Position X @ 9.00") so a script can grab it
                char keyName[96];
                std::snprintf(keyName, sizeof(keyName), "%s @ %.2f",
                              row.kind == RowTrack ? tr.target.c_str() : scopeName(row.bits).c_str(),
                              tr.keys[ki].time);
                ImGui::InvisibleButton(keyName, ImVec2(2.0f * (r + pad), 2.0f * (r + pad)));
                const bool hov = ImGui::IsItemHovered();
                const bool dragging = seqDragTrack_ == ti && seqDragKey_ == ki && seqDragMoved_;
                if (hov || dragging) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                if (hov && seqDragTrack_ < 0)
                    ImGui::SetTooltip("%s \xc2\xb7 %.2f s - drag to retime,\nright-click for easing/delete",
                                      scopeName(kb).c_str(), tr.keys[ki].time);
                if (ImGui::IsItemActivated()) {
                    selectRowKey(ti, ki, kb);
                    seqDragTrack_ = ti;
                    seqDragKey_ = ki;
                    seqDragBits_ = kb;
                    seqDragMoved_ = false;
                }
                if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) seqPlayhead_ = tr.keys[ki].time;
                ImGui::OpenPopupOnItemClick("##okeyctx", ImGuiPopupFlags_MouseButtonRight);
                if (ImGui::BeginPopup("##okeyctx")) {
                    SeqObjectKey& k = tr.keys[ki];
                    selectedSeqTrack_ = ti;
                    selectedSeqKey_ = ki;
                    selectedSeqBits_ = kb;
                    ImGui::TextDisabled("%s \xc2\xb7 key @ %.2f s", scopeName(kb).c_str(), k.time);
                    ImGui::Separator();
                    for (int e = 0; e < 3; ++e)
                        if (ImGui::MenuItem(kObjEaseNames[e], nullptr, k.easing == e)) {
                            k.easing = e;
                            changed = true;
                        }
                    if (kb != k.mask && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                        ImGui::SetTooltip("The easing belongs to the whole key at this time.");
                    ImGui::Separator();
                    if (ImGui::MenuItem("Playhead here")) seqPlayhead_ = k.time;
                    const std::string delThis = "Delete key (" + scopeName(kb) + ")";
                    if (ImGui::MenuItem(delThis.c_str())) del = ki, delBits = kb;
                    if (row.kind == RowAxis) {
                        const unsigned chb = k.mask & kChannels[row.ch].bits;
                        if (chb != kb) {
                            const std::string delCh = std::string("Delete ") + kChannels[row.ch].name + " at this time";
                            if (ImGui::MenuItem(delCh.c_str())) del = ki, delBits = chb;
                        }
                    }
                    if (kb != k.mask && ImGui::MenuItem("Delete the whole key")) del = ki, delBits = k.mask;
                    ImGui::EndPopup();
                }
                ImGui::PopID();
                const bool sel = selectedSeqTrack_ == ti && selectedSeqKey_ == ki && (selectedSeqBits_ & kb) != 0;
                const ImVec2 pts[4] = {{kx, cy - r}, {kx + r, cy}, {kx, cy + r}, {kx - r, cy}};
                dl->AddConvexPolyFilled(pts, 4, ImGui::GetColorU32(row.depth == 2 ? mixc(tint, sem.text, 0.15f) : tint));
                dl->AddPolyline(pts, 4, ImGui::GetColorU32(sel ? sem.text : withA(sem.ground, 0.9f)), ImDrawFlags_Closed,
                                sel ? 2.0f : 1.0f);
                if (hov && !sel) dl->AddCircle(ImVec2(kx, cy), r + scaled(2.5f), colText, 0, 1.0f);
            }
            if (del >= 0) {
                seqClearKey(tr, del, delBits);
                selectedSeqKey_ = -1;
                seqDragTrack_ = -1;
                changed = true;
            }
        }

        // The key drag. Retiming part of a key first splits those components
        // into a key of their own (the rest stay where they were); on release
        // the keys are sorted and a key it landed on absorbs it.
        if (seqDragTrack_ >= 0) {
            SeqTrack& tr = s.tracks[seqDragTrack_];
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f)) {
                    if (!seqDragMoved_) {
                        seqDragKey_ = seqSplitKey(tr, seqDragKey_, seqDragBits_);
                        seqDragMoved_ = true;
                        if (selectedSeqTrack_ == seqDragTrack_) selectedSeqKey_ = seqDragKey_;
                    }
                    tr.keys[seqDragKey_].time = timeAtMouse();
                    seqPlayhead_ = tr.keys[seqDragKey_].time;
                }
            } else {
                if (seqDragMoved_) {
                    const int k = seqSettleKeys(tr, seqDragKey_);
                    if (selectedSeqTrack_ == seqDragTrack_) selectedSeqKey_ = k;
                    changed = true;
                }
                seqDragTrack_ = -1;
            }
        }
        // playhead across the lanes (its pill rides the pinned ruler)
        dl->AddLine(ImVec2(xAt(seqPlayhead_), laneY0), ImVec2(xAt(seqPlayhead_), laneY0 + laneCount * laneH),
                    colPlayhead, 1.5f);

        // pinned label column, drawn last so it occludes scrolled-under keys.
        // The chevron opens a track's channels and a channel's axes; a click
        // on the name selects the track; the diamond on the right keys (or
        // un-keys) that row's components at the playhead; right-click = the
        // row's settings.
        dl->AddRectFilled(ImVec2(labelX, origin.y), ImVec2(labelX + labelW, origin.y + contentH + addRowH),
                          ImGui::GetColorU32(ImGuiCol_WindowBg));
        dl->AddLine(ImVec2(labelX + labelW, origin.y), ImVec2(labelX + labelW, origin.y + contentH), colGrid);
        for (int li = 0; li < laneCount; ++li) {
            const Row& row = rowList[li];
            const int ti = row.kind == RowCamera ? -1 : row.kind == RowFades ? -2 : row.ti;
            const float y = laneY0 + li * laneH;
            const float ty = y + (laneH - ImGui::GetTextLineHeight()) * 0.5f;
            const float indent = labelX + scaled(4.0f) + row.depth * scaled(14.0f);
            const float chevW = scaled(16.0f);
            ImGui::PushID(500 + li);
            if (row.kind == RowFades) {
                dl->AddCircleFilled(ImVec2(indent + chevW + scaled(4.0f), y + laneH * 0.5f), scaled(3.5f), colDim);
                dl->AddText(ImVec2(indent + chevW + scaled(12.0f), ty), colDim, "Fades & bars");
                ImGui::PopID();
                continue;
            }
            if (row.kind == RowAdd) {
                // the channels this track does not animate yet: one chip each
                SeqTrack& tr = s.tracks[row.ti];
                const float bh = ImGui::GetFrameHeight();
                ImGui::SetCursorScreenPos(ImVec2(indent + chevW, y + (laneH - bh) * 0.5f));
                bool first = true;
                for (int ch = 0; ch < 5; ++ch) {
                    if (seqTrackChannels(tr) & kChannels[ch].bits) continue;
                    if (!first) ImGui::SameLine(0.0f, scaled(4.0f));
                    first = false;
                    char lbl[40];
                    std::snprintf(lbl, sizeof(lbl), "+ %s", kChannels[ch].name);
                    if (ui::GhostButton(lbl)) {
                        channelFlag(tr, ch) = true;
                        cutsceneSnapshotObjectKey(tr, seqPlayhead_, kChannels[ch].bits);
                        changed = true;
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                        ImGui::SetTooltip("Animate %s: keys it at the playhead\nfrom the object's pose.",
                                          kChannels[ch].name);
                }
                ImGui::PopID();
                continue;
            }

            // right edge: "+" on the camera row, the key-here diamond on object rows
            const float btn = scaled(22.0f);
            const float bx = labelX + labelW - btn - scaled(4.0f);
            if (row.kind == RowCamera) {
                ImGui::SetCursorScreenPos(ImVec2(bx, y + (laneH - ImGui::GetFrameHeight()) * 0.5f));
                if (ui::GhostButton("+", btn)) {
                    if (snapshotCameraKey(seqPlayhead_)) changed = true;
                    else statusMessage_ = "Add a Camera entity first (+ Add object > Gameplay > Camera)";
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("Add a shot at the playhead, filming from the\n"
                                      "active camera (looked-through / selected /\n"
                                      "first). Needs a Camera entity in the scene.");
            } else {
                SeqTrack& tr = s.tracks[row.ti];
                const bool whole = row.kind == RowTrack;
                const unsigned want = whole ? seqTrackChannels(tr) : row.bits;
                int at = -1;
                for (int i = 0; i < (int)tr.keys.size(); ++i)
                    if (std::fabs(tr.keys[i].time - seqPlayhead_) < kSeqKeySnap) at = i;
                const unsigned have = at >= 0 ? tr.keys[at].mask & want : 0u;
                const bool full = want && have == want;
                const ImVec2 bp(bx, y + (laneH - btn) * 0.5f);
                ImGui::SetCursorScreenPos(bp);
                const std::string scope = whole ? std::string("the pose") : scopeName(want);
                if (ImGui::InvisibleButton("Key at playhead", ImVec2(btn, btn))) {
                    if (full) {
                        if (seqClearKey(tr, at, want) && selectedSeqTrack_ == row.ti) selectedSeqKey_ = -1;
                    } else {
                        const int ki = cutsceneSnapshotObjectKey(tr, seqPlayhead_, whole ? 0u : want);
                        if (ki >= 0) selectRowKey(row.ti, ki, whole ? (unsigned)kSeqChAll : want);
                    }
                    changed = true;
                }
                const float ht = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip(full ? "Remove the %s key at the playhead"
                                           : "Key %s at the playhead\nfrom the object's pose",
                                      scope.c_str());
                const ImVec2 c(bp.x + btn * 0.5f, bp.y + btn * 0.5f);
                const float dr = scaled(5.0f);
                const ImVec2 pts[4] = {{c.x, c.y - dr}, {c.x + dr, c.y}, {c.x, c.y + dr}, {c.x - dr, c.y}};
                const ImVec4 tint = tintOf(tr.target);
                if (ht > 0.0f)
                    dl->AddRectFilled(bp, ImVec2(bp.x + btn, bp.y + btn), ImGui::GetColorU32(withA(sem.surface, ht)),
                                      ImGui::GetStyle().FrameRounding);
                if (full) dl->AddConvexPolyFilled(pts, 4, ImGui::GetColorU32(tint));
                else if (have) dl->AddConvexPolyFilled(pts, 4, ImGui::GetColorU32(withA(tint, 0.45f)));
                dl->AddPolyline(pts, 4, ImGui::GetColorU32(have ? tint : mixc(sem.textDim, sem.text, ht)), ImDrawFlags_Closed,
                                1.2f);
            }

            // the chevron: a track opens into channels, a 3-axis channel into axes
            const bool canOpen = row.kind == RowTrack || (row.kind == RowChannel && kChannels[row.ch].axes == 3);
            if (canOpen) {
                const std::string key = openKey(s.tracks[row.ti], row.kind == RowTrack ? -1 : row.ch);
                const bool open = seqOpenRows_.count(key) != 0;
                const ImVec2 cp(indent, y);
                ImGui::SetCursorScreenPos(cp);
                if (ImGui::InvisibleButton(open ? "Collapse row" : "Expand row", ImVec2(chevW, laneH))) {
                    if (open) seqOpenRows_.erase(key);
                    else seqOpenRows_.insert(key);
                }
                const bool ch = ImGui::IsItemHovered();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip(row.kind == RowTrack ? (open ? "Hide the channels" : "Show the channels")
                                                           : (open ? "Hide the axes" : "Show X, Y and Z"));
                const ImU32 ink = ImGui::GetColorU32(ch ? sem.text : sem.textDim);
                const ImVec2 cc(cp.x + chevW * 0.5f, y + laneH * 0.5f);
                const float a = scaled(3.5f);
                if (open)
                    dl->AddTriangleFilled(ImVec2(cc.x - a, cc.y - a * 0.5f), ImVec2(cc.x + a, cc.y - a * 0.5f),
                                          ImVec2(cc.x, cc.y + a * 0.7f), ink);
                else
                    dl->AddTriangleFilled(ImVec2(cc.x - a * 0.5f, cc.y - a), ImVec2(cc.x - a * 0.5f, cc.y + a),
                                          ImVec2(cc.x + a * 0.7f, cc.y), ink);
            }

            // the label: click selects the track, right-click its settings
            const float lx = indent + chevW;
            ImGui::SetCursorScreenPos(ImVec2(lx, y));
            ImGui::InvisibleButton("##label", ImVec2(ImMax(1.0f, bx - lx - scaled(2.0f)), laneH));
            if (ImGui::IsItemClicked()) {
                selectedSeqTrack_ = ti;
                selectedSeqKey_ = -1;
                seqInspTab_ = 0;
                seqInspTabForce_ = true;
            }
            const bool chanRow = row.kind == RowChannel || row.kind == RowAxis;
            ImGui::OpenPopupOnItemClick(chanRow ? "##chanctx" : "##trackctx", ImGuiPopupFlags_MouseButtonRight);
            if (row.kind == RowTrack && selectedSeqTrack_ == ti)
                dl->AddRectFilled(ImVec2(labelX, y), ImVec2(labelX + labelW, y + laneH),
                                  ImGui::GetColorU32(withA(sem.accent, 0.12f)));
            const float valueRight = bx - scaled(6.0f);
            if (row.kind == RowCamera) {
                dl->AddCircleFilled(ImVec2(lx + scaled(4.0f), y + laneH * 0.5f), scaled(3.5f), colText);
                char lbl[48];
                std::snprintf(lbl, sizeof(lbl), "Camera \xc2\xb7 %d shot%s", (int)s.cameraKeys.size(),
                              s.cameraKeys.size() == 1 ? "" : "s");
                ui::FontScope bold(ui::boldFont());
                dl->AddText(ImVec2(lx + scaled(12.0f), ty), colText, lbl);
            } else if (row.kind == RowTrack) {
                const SeqTrack& tr = s.tracks[ti];
                dl->AddCircleFilled(ImVec2(lx + scaled(4.0f), y + laneH * 0.5f), scaled(3.5f),
                                    ImGui::GetColorU32(tintOf(tr.target)));
                // closed: the animated channels as a dim summary
                std::string chs;
                if (!seqOpenRows_.count(openKey(tr, -1)))
                    for (int c = 0; c < 5; ++c)
                        if (seqTrackChannels(tr) & kChannels[c].bits)
                            chs += std::string(chs.empty() ? "" : " ") + kChannels[c].name[0];
                const float chW = ImGui::CalcTextSize(chs.c_str()).x;
                dl->PushClipRect(ImVec2(lx, y), ImVec2(valueRight - chW - scaled(4.0f), y + laneH), true);
                {
                    ui::FontScope bold(ui::boldFont());
                    dl->AddText(ImVec2(lx + scaled(12.0f), ty), colText, tr.target.empty() ? "<no object>" : tr.target.c_str());
                }
                dl->PopClipRect();
                dl->AddText(ImVec2(valueRight - chW, ty), colDim, chs.c_str());
            } else {
                const SeqTrack& tr = s.tracks[ti];
                const SceneObject* po = posedOf(tr.target);
                bool keyed = false;
                for (const SeqObjectKey& k : tr.keys) keyed |= (k.mask & row.bits) != 0;
                char val[32] = "";
                std::string name;
                ImU32 nameCol = colText;
                if (row.kind == RowAxis) {
                    name = kAxisNames[row.axis];
                    const ImVec4 axc[] = {sem.axisX, sem.axisY, sem.axisZ};
                    nameCol = ImGui::GetColorU32(keyed ? axc[row.axis] : sem.textDim);
                    if (po) {
                        const float* v = row.ch == 0 ? po->position : row.ch == 1 ? po->rotation : po->scale;
                        std::snprintf(val, sizeof(val), row.ch == 1 ? "%.1f\xc2\xb0" : "%.2f", v[row.axis]);
                    }
                } else {
                    name = kChannels[row.ch].name;
                    if (row.ch == 3 && po)
                        std::snprintf(val, sizeof(val), "#%02X%02X%02X", (int)(ImSaturate(po->color[0]) * 255.0f + 0.5f),
                                      (int)(ImSaturate(po->color[1]) * 255.0f + 0.5f),
                                      (int)(ImSaturate(po->color[2]) * 255.0f + 0.5f));
                    else if (kChannels[row.ch].axes == 3 && !seqOpenRows_.count(openKey(tr, row.ch))) {
                        // closed: which axes have keys at all
                        std::string ax;
                        for (int a = 0; a < 3; ++a) {
                            bool any = false;
                            for (const SeqObjectKey& k : tr.keys) any |= (k.mask & (1u << (row.ch * 3 + a))) != 0;
                            if (any) ax += kAxisNames[a];
                        }
                        std::snprintf(val, sizeof(val), "%s", ax.c_str());
                    }
                }
                if (!keyed) name += "  not keyed";
                dl->AddText(ImVec2(lx + scaled(2.0f), ty), nameCol, name.c_str());
                if (val[0]) {
                    ui::FontScope mono(ui::monoFont());
                    const float vw = ImGui::CalcTextSize(val).x;
                    dl->AddText(ImVec2(valueRight - vw, ty), colDim, val);
                }
            }
            if (chanRow && ImGui::BeginPopup("##chanctx")) {
                SeqTrack& tr = s.tracks[ti];
                const std::string scope = scopeName(row.bits);
                ImGui::TextDisabled("%s \xc2\xb7 %s", tr.target.c_str(), scope.c_str());
                ImGui::Separator();
                if (ImGui::MenuItem("Key at playhead")) {
                    const int ki = cutsceneSnapshotObjectKey(tr, seqPlayhead_, row.bits);
                    if (ki >= 0) selectRowKey(ti, ki, row.bits);
                    changed = true;
                }
                bool any = false;
                for (const SeqObjectKey& k : tr.keys) any |= (k.mask & row.bits) != 0;
                const std::string clearAll = "Delete every " + scope + " key";
                if (ImGui::MenuItem(clearAll.c_str(), nullptr, false, any)) {
                    for (int i = (int)tr.keys.size() - 1; i >= 0; --i) seqClearKey(tr, i, row.bits);
                    selectedSeqKey_ = -1;
                    changed = true;
                }
                if (row.kind == RowChannel) {
                    const std::string stop = std::string("Stop animating ") + kChannels[row.ch].name;
                    if (ImGui::MenuItem(stop.c_str())) {
                        channelFlag(tr, row.ch) = false;
                        changed = true;
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                        ImGui::SetTooltip("The keys stay; the object keeps its own %s.", kChannels[row.ch].name);
                }
                ImGui::EndPopup();
            }
            if (ImGui::BeginPopup("##trackctx")) {
                if (ti < 0) {
                    ImGui::TextDisabled("Camera track");
                    ImGui::Separator();
                    if (ImGui::MenuItem("Add shot @ playhead (active camera)")) {
                        if (snapshotCameraKey(seqPlayhead_)) changed = true;
                    }
                    if (ImGui::MenuItem("Clear all shots", nullptr, false, !s.cameraKeys.empty())) {
                        s.cameraKeys.clear();
                        selectedSeqKey_ = -1;
                        changed = true;
                    }
                    if (ImGui::MenuItem("Import take...")) beginTakeImport();
                } else {
                    SeqTrack& tr = s.tracks[ti];
                    ImGui::SetNextItemWidth(scaled(160.0f));
                    if (ImGui::BeginCombo("Object", tr.target.empty() ? "<pick>" : tr.target.c_str())) {
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
        // + Object track, under the last lane in the label column
        ImGui::SetCursorScreenPos(ImVec2(labelX + scaled(6.0f), laneY0 + laneCount * laneH + scaled(4.0f)));
        if (ui::GhostButton("+ Object track")) ImGui::OpenPopup("##addtrack");
        if (ImGui::BeginPopup("##addtrack")) {
            auto hasTrack = [&](const std::string& name) {
                for (const SeqTrack& tr : s.tracks)
                    if (tr.target == name) return true;
                return false;
            };
            // one track per object; a fresh track gets a starting key at the
            // playhead from the object's current pose, so it animates at once
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
                if (sel >= 0 && sel < (int)project_.objects().size() && !hasTrack(project_.objects()[sel].name))
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
                if (ImGui::MenuItem(o.name.c_str(), tracked ? "tracked" : nullptr, false, !tracked))
                    addTrackFor(o.name);
            }
            if (project_.objects().empty()) ImGui::TextDisabled("No objects in this scene.");
            ImGui::EndPopup();
        }
        ImGui::PopClipRect();

        // the pinned ruler: ticks + labels over a band of its own, the
        // playhead's pill on it; click/drag scrubs the playhead
        {
            const float rx0 = labelX + labelW;
            dl->AddRectFilled(ImVec2(labelX, rulerY), ImVec2(winMax.x, rulerY + rulerH),
                              ImGui::GetColorU32(ImGuiCol_WindowBg));
            dl->AddLine(ImVec2(labelX, rulerY + rulerH), ImVec2(winMax.x, rulerY + rulerH), colGrid);
            dl->AddLine(ImVec2(rx0, rulerY), ImVec2(rx0, rulerY + rulerH), colGrid);
            ImGui::PushClipRect(ImVec2(rx0, rulerY), ImVec2(winMax.x, rulerY + rulerH), true);
            ImGui::SetCursorScreenPos(ImVec2(x0, rulerY));
            ImGui::InvisibleButton("Timeline ruler", ImVec2(timeW, rulerH));
            if (ImGui::IsItemActive()) {
                seqPlayhead_ = timeAtMouse();
                seqPlaying_ = false;
            }
            for (float t = 0.0f; t <= s.duration + 1e-4f; t += minor) {
                const float x = xAt(t);
                const bool major = std::fabs(std::fmod(t + 1e-4f, step)) < 2e-3f;
                dl->AddLine(ImVec2(x, rulerY + (major ? scaled(14.0f) : scaled(18.0f))), ImVec2(x, rulerY + rulerH),
                            colGrid);
                if (major) {
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "%g s", t);
                    dl->AddText(ImVec2(x + scaled(3.0f), rulerY + scaled(2.0f)), colDim, buf);
                }
            }
            const float x = xAt(seqPlayhead_);
            dl->AddLine(ImVec2(x, rulerY + scaled(4.0f)), ImVec2(x, rulerY + rulerH), colPlayhead, 1.5f);
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.2f", seqPlayhead_);
            ui::FontScope mono(ui::monoFont());
            const ImVec2 ts = ImGui::CalcTextSize(buf);
            const ImVec2 a(x - ts.x * 0.5f - scaled(4.0f), rulerY + scaled(1.0f));
            dl->AddRectFilled(a, ImVec2(a.x + ts.x + scaled(8.0f), a.y + ts.y + scaled(2.0f)), colPlayhead,
                              scaled(3.0f));
            dl->AddText(ImVec2(a.x + scaled(4.0f), a.y + scaled(1.0f)), ImGui::GetColorU32(sem.ground), buf);
            ImGui::PopClipRect();
        }
    }
    ImGui::EndChild();

    if (deleteTrack >= 0) {
        s.tracks.erase(s.tracks.begin() + deleteTrack);
        if (selectedSeqTrack_ == deleteTrack) selectedSeqTrack_ = -1, selectedSeqKey_ = -1;
        seqDragTrack_ = -1;
        changed = true;
    }
    ImGui::EndChild();  // ##seq_main
    ImGui::SameLine(0.0f, 0.0f);
    {
        float mw = mainW;
        ui::VSplitter("##seq_split_side", &mw, scaled(240.0f), scaled(260.0f), bodyH);
        seqInspW_ += mainW - mw;
    }

    // --- side column: the shot (foldable) over the inspector --------------------
    ImGui::BeginChild("##seq_side", ImVec2(0, 0));
    {
        {
            ImGui::AlignTextToFramePadding();
            {
                ui::FontScope bold(ui::boldFont());
                ImGui::TextUnformatted("Shot");
            }
            ImGui::SameLine(0.0f, fs * 0.4f);
            ImGui::TextDisabled("at %.2f s", seqPlayhead_);
            const float fw = ImGui::GetFrameHeight();
            ImGui::SameLine(ImGui::GetContentRegionMax().x - fw);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton(seqShotFolded_ ? "Show shot" : "Fold shot", ImVec2(fw, fw)))
                seqShotFolded_ = !seqShotFolded_;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip(seqShotFolded_ ? "Show the shot preview" : "Fold the shot preview away\n(more room for the inspector)");
            const float t = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImU32 ink = ImGui::GetColorU32(mixc(sem.textDim, sem.text, t));
            const ImVec2 c(p.x + fw * 0.5f, p.y + fw * 0.5f);
            const float a = fw * 0.18f;
            if (seqShotFolded_)
                dl->AddTriangleFilled(ImVec2(c.x - a, c.y - a * 0.5f), ImVec2(c.x + a, c.y - a * 0.5f),
                                      ImVec2(c.x, c.y + a * 0.7f), ink);
            else
                dl->AddTriangleFilled(ImVec2(c.x - a, c.y + a * 0.5f), ImVec2(c.x + a, c.y + a * 0.5f),
                                      ImVec2(c.x, c.y - a * 0.7f), ink);
        }
        if (!seqShotFolded_) {
            const float aspect = ps2ViewportOutput().tvAspect > 0.1f ? ps2ViewportOutput().tvAspect : 4.0f / 3.0f;
            const float heroH = ImMin(ImGui::GetContentRegionAvail().x / aspect + ImGui::GetFrameHeightWithSpacing() + fs * 1.4f,
                                      ImGui::GetContentRegionAvail().y * 0.55f);
            ImGui::BeginChild("##seq_hero", ImVec2(0, heroH));
            {
            // --- the shot at the playhead ---------------------------------------
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            const float rowH = ImGui::GetFrameHeightWithSpacing();
            const float aspect = ps2ViewportOutput().tvAspect > 0.1f ? ps2ViewportOutput().tvAspect : 4.0f / 3.0f;
            float w = avail.x - fs, h = w / aspect;
            if (h > avail.y - rowH - fs) {
                h = avail.y - rowH - fs;
                w = h * aspect;
            }
            w = ImMax(w, 16.0f);
            h = ImMax(h, 16.0f);
            const ImVec2 c0 = ImGui::GetCursorScreenPos();
            const ImVec2 p(c0.x + (avail.x - w) * 0.5f, c0.y + fs * 0.4f);
            const ImVec2 q(p.x + w, p.y + h);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float r = ImGui::GetStyle().FrameRounding + 3.0f;
            // Keeps seqPosed_ and the shot current even while the viewport
            // panel is hidden behind another tab.
            cutscenePosedObjects();
            if (seqShotValid_) {
                std::vector<std::string> hide;
                for (const SeqCameraKey& k : s.cameraKeys)
                    if (!k.camera.empty()) hide.push_back(k.camera);
                viewport_.setHiddenCameras(std::move(hide));
                const float sc = ImGui::GetIO().DisplayFramebufferScale.x;
                // The scene as the game draws it, re-rendered when the shot,
                // the playhead or the project changes (Viewport::renderGameView).
                uint32_t playBits = 0;
                std::memcpy(&playBits, &seqPlayhead_, sizeof(playBits));
                const uint32_t tex = viewport_.renderShot((int)(w * sc), (int)(h * sc), seqPosed_, seqShotEye_,
                                                          seqShotAt_, seqShotFov_, seqShotRoll_,
                                                          Viewport::GameViewShot,
                                                          modelEditSerial_ * 31u + playBits);
                dl->AddImageRounded((ImTextureID)(intptr_t)tex, p, q, ImVec2(0, 1), ImVec2(1, 0),
                                    IM_COL32_WHITE, r);
                // bars and fade, the same coverage the PS2 composites
                const ImU32 black = IM_COL32(0, 0, 0, 255);
                if (s.bars != kSeqBarsNone) {
                    const float amt = seqBarsAmount(seqPlayhead_, s.duration, s.barsSlideIn, s.barsSlideOut);
                    float ft, fb, fl, fr;
                    seqBarsFractions(s.bars, aspect, ft, fb, fl, fr);
                    const float bt = ft * amt * h, bb = fb * amt * h, bl = fl * amt * w, br = fr * amt * w;
                    if (bt > 0) dl->AddRectFilled(p, ImVec2(q.x, p.y + bt), black, r, ImDrawFlags_RoundCornersTop);
                    if (bb > 0) dl->AddRectFilled(ImVec2(p.x, q.y - bb), q, black, r, ImDrawFlags_RoundCornersBottom);
                    if (bl > 0) dl->AddRectFilled(p, ImVec2(p.x + bl, q.y), black, r, ImDrawFlags_RoundCornersLeft);
                    if (br > 0) dl->AddRectFilled(ImVec2(q.x - br, p.y), q, black, r, ImDrawFlags_RoundCornersRight);
                }
                const float fade = seqFadeAlpha(seqPlayhead_, s.duration, s.fadeIn, s.fadeOut);
                if (fade > 0.0f) dl->AddRectFilled(p, q, IM_COL32(0, 0, 0, (int)(fade * 255.0f)), r);
                // glass label: which camera, its lens, its shake
                char lbl[96];
                if (!seqShotCam_.empty())
                    std::snprintf(lbl, sizeof(lbl), "%s \xc2\xb7 FOV %.0f \xc2\xb7 shake %.2f", seqShotCam_.c_str(),
                                  seqShotFov_, seqShotShake_);
                else
                    std::snprintf(lbl, sizeof(lbl), "free shot \xc2\xb7 FOV %.0f \xc2\xb7 shake %.2f", seqShotFov_,
                                  seqShotShake_);
                const ImVec2 ls = ImGui::CalcTextSize(lbl);
                const ImVec2 la(p.x + fs * 0.6f, p.y + fs * 0.6f);
                dl->AddRectFilled(la, ImVec2(la.x + ls.x + fs * 0.8f, la.y + ls.y + fs * 0.4f),
                                  IM_COL32(0, 0, 0, 170), fs * 0.3f);
                dl->AddText(ImVec2(la.x + fs * 0.4f, la.y + fs * 0.2f), IM_COL32(235, 235, 240, 255), lbl);
            } else {
                dl->AddRectFilled(p, q, ImGui::GetColorU32(sem.field), r);
                const char* msg = s.cameraEnabled ? "No camera shots yet"
                                                  : "Camera track off - the game keeps its own camera";
                const ImVec2 ms = ImGui::CalcTextSize(msg);
                dl->AddText(ImVec2((p.x + q.x - ms.x) * 0.5f, (p.y + q.y) * 0.5f - ms.y * 1.6f),
                            ImGui::GetColorU32(sem.textDim), msg);
                const char* act = s.cameraEnabled ? "Add a shot at the playhead" : "Turn on the camera track";
                const float bw = ImGui::CalcTextSize(act).x + ImGui::GetStyle().FramePadding.x * 2.6f;
                ImGui::SetCursorScreenPos(ImVec2((p.x + q.x - bw) * 0.5f, (p.y + q.y) * 0.5f));
                if (ui::PrimaryButton(act, bw)) {
                    if (!s.cameraEnabled) {
                        s.cameraEnabled = true;
                        changed = true;
                    } else if (snapshotCameraKey(seqPlayhead_)) {
                        changed = true;
                    } else {
                        statusMessage_ = "Add a Camera entity first (+ Add object > Gameplay > Camera)";
                    }
                }
            }
            dl->AddRect(p, q, ImGui::GetColorU32(sem.border), r);
            // what the preview drives, and what the sequence hides
            ImGui::SetCursorScreenPos(ImVec2(p.x, q.y + fs * 0.5f));
            const char* modes[] = {"Shot", "Also drive the viewport"};
            int mode = seqPreview_ ? 1 : 0;
            if (ui::Segmented("##seq_view", modes, 2, &mode)) seqPreview_ = mode == 1;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Shot: the preview is here and the viewport stays yours.\n"
                                  "Also drive the viewport: the viewport films through the\n"
                                  "sequence camera too, with its bars and fades.");
            ImGui::SameLine(0.0f, fs);
            std::string summary;
            if (s.bars != kSeqBarsNone) summary += std::string(kBarsNames[s.bars]) + " bars";
            if (s.hideHud) summary += std::string(summary.empty() ? "" : " \xc2\xb7 ") + "HUD hidden";
            if (s.hidePlayer) summary += std::string(summary.empty() ? "" : " \xc2\xb7 ") + "player hidden";
            if (!summary.empty()) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%s", summary.c_str());
            }
            }
            ImGui::EndChild();
        } else {
            cutscenePosedObjects();  // keeps seqPosed_ and the shot current while folded
        }
        ImGui::BeginChild("##seq_insp", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
        {
            if (ui::BeginQuietTabBar("##seq_insp_tabs")) {
                const ImGuiTabItemFlags force = seqInspTabForce_ ? ImGuiTabItemFlags_SetSelected : 0;
                const int want = seqInspTab_;  // while forcing, ImGui still reports the old tab
                if (ui::QuietTab("Key", want == 0, want == 0 ? force : 0) && !force) seqInspTab_ = 0;
                if (ui::QuietTab("Sequence", want == 1, want == 1 ? force : 0) && !force) seqInspTab_ = 1;
                ui::EndQuietTabBar();
            }
            seqInspTabForce_ = false;
            ImGui::BeginChild("##seq_insp_body", ImVec2(0, 0));
            if (seqInspTab_ == 1) {
                // --- the sequence's own settings ----------------------------------
                ui::InspectorRows rows(scaled(80.0f), scaled(120.0f));
                char nameBuf[64];
                std::snprintf(nameBuf, sizeof(nameBuf), "%s", s.name.c_str());
                if (ui::InputText("Name", nameBuf, sizeof(nameBuf))) {
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
                if (ui::DragFloat("Duration (s)", &s.duration, 0.1f, 0.1f, 600.0f, "%.2f s")) changed = true;
                if (s.duration < 0.1f) s.duration = 0.1f;
                if (ui::Switch("Loop", &s.loop)) changed = true;
                if (ui::Switch("Skippable", &s.skippable)) changed = true;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("The menu button (START) ends the cutscene early.\n"
                                      "While it plays that button is the cutscene's, not\n"
                                      "the pause menu's.");
                // How a skip is taken - only while skippable, a mode that decides
                // nothing reads as one that is broken.
                if (s.skippable) {
                    static const char* kSkipNames[] = {"Skip instantly", "Ask first"};
                    if (ui::Combo("On skip", &s.skipMode, kSkipNames, kSeqSkipModeCount)) changed = true;
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                        ImGui::SetTooltip("Ask first opens the project's skip screen and\n"
                                          "freezes the cutscene until the player answers.");
                    if (s.skipMode == kSeqSkipConfirm) {
                        const int sm = project::skipMenuIndex(project_);
                        if (sm < 0) {
                            ImGui::TextColored(sem.warn, "No skip screen - skips instantly.");
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                                ImGui::SetTooltip("Nothing to open, so the press just skips.\n"
                                                  "Tools > Menu Editor > + Skip screen.");
                        } else {
                            ImGui::TextDisabled("Asks with screen \"%s\".", project_.menus[sm].name.c_str());
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                                ImGui::SetTooltip("Edit it in Tools > Menu Editor.");
                        }
                    }
                }
                if (ui::Switch("Camera track", &s.cameraEnabled)) changed = true;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("Drive the game camera from the camera lane's shots\n"
                                      "for the duration of playback.");
                if (ui::Switch("Hide player", &s.hidePlayer)) changed = true;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("Hide the third-person player avatar while the cutscene\n"
                                      "plays (no effect in FPP/noclip - they have no body).");
                if (ui::Switch("Hide HUD", &s.hideHud)) changed = true;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("Hides the HUD, the USE prompt and USE itself.\n"
                                      "Display Text stays - subtitles keep working.\n"
                                      "Off for a cutscene the player keeps playing under.");
                ImGui::Spacing();
                ui::Caption("Bars & fades");
                // Cinematic dressing: widescreen masks + fades, composited over
                // the frame (and the HUD) on the PS2 and drawn on the preview.
                if (ui::Combo("Widescreen bars", &s.bars, kBarsNames, kSeqBarsStyleCount)) changed = true;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("Solid black masks while the cutscene plays; they\n"
                                      "slide in at the start and out before the end.");
                if (s.bars != kSeqBarsNone) {
                    if (ui::DragFloat("Bars in", &s.barsSlideIn, 0.05f, 0.0f, 10.0f, "%.2f s")) changed = true;
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                        ImGui::SetTooltip("How long the bars take to slide in at the start.\n"
                                          "0 = they are there from the first frame.");
                    if (ui::DragFloat("Bars out", &s.barsSlideOut, 0.05f, 0.0f, 10.0f, "%.2f s")) changed = true;
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                        ImGui::SetTooltip("How long the bars take to slide out before the\n"
                                          "end. 0 = they stay until the last frame.");
                    if (s.barsSlideIn < 0.0f) s.barsSlideIn = 0.0f;
                    if (s.barsSlideOut < 0.0f) s.barsSlideOut = 0.0f;
                }
                if (ui::DragFloat("Fade in", &s.fadeIn, 0.05f, 0.0f, 10.0f, "%.2f s")) changed = true;
                if (ui::DragFloat("Fade out", &s.fadeOut, 0.05f, 0.0f, 10.0f, "%.2f s")) changed = true;
                if (s.fadeIn < 0.0f) s.fadeIn = 0.0f;
                if (s.fadeOut < 0.0f) s.fadeOut = 0.0f;

                // --- Adjust imported take -----------------------------------------
                // After a take is imported the recording + mapping stay loaded, so
                // the whole path can be re-positioned and re-oriented in place
                // (start point, start yaw, scale) without re-importing.
                if (seqTakeActive_ && seqTakeSeqIdx_ == selectedSequence_ &&
                    !seqTake_.samples.empty()) {
                    const std::string what =
                        seqTakeTarget_.empty() ? std::string("free camera shots")
                                               : ("camera \"" + seqTakeTarget_ + "\"");
                    if (ui::Section("Adjust imported take", ImGuiTreeNodeFlags_DefaultOpen)) {
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
                ImGui::Spacing();
                if (ui::GhostButton("Duplicate")) {
                    Sequence copy = s;
                    copy.name = uniqueSeqName(s.name);
                    project_.sequences.push_back(std::move(copy));
                    changed = true;
                    selectSequence((int)project_.sequences.size() - 1);
                }
                ImGui::SameLine();
                if (ui::DangerButton("Delete sequence")) deleteSeq = true;
            } else {
                // --- the selected key ----------------------------------------------
                const bool camSel = selectedSeqTrack_ == -1 && s.cameraEnabled && selectedSeqKey_ >= 0 &&
                                    selectedSeqKey_ < (int)s.cameraKeys.size();
                const bool trackSel = selectedSeqTrack_ >= 0 && selectedSeqTrack_ < (int)s.tracks.size();
                const bool objSel = trackSel && selectedSeqKey_ >= 0 &&
                                    selectedSeqKey_ < (int)s.tracks[selectedSeqTrack_].keys.size();
                // The three easings by what they do to a camera.
                auto easingRow = [&](const char* caption, int& easing, bool camera) {
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("%s", caption);
                    ImGui::SameLine(ImClamp(ImGui::GetContentRegionAvail().x * 0.34f, scaled(80.0f), scaled(120.0f)));
                    const char* camOpts[] = {"Cut", "Smooth", "Linear"};
                    const char* objOpts[] = {"Hold", "Smooth", "Linear"};
                    static const int kToEasing[] = {2, 1, 0};
                    int sel = easing == 2 ? 0 : easing == 1 ? 1 : 2;
                    if (ui::Segmented(camera ? "##cam_ease" : "##obj_ease", camera ? camOpts : objOpts, 3, &sel, -1.0f)) {
                        easing = kToEasing[sel];
                        changed = true;
                    }
                };
                if (camSel) {
                    SeqCameraKey& k = s.cameraKeys[selectedSeqKey_];
                    const int n = (int)s.cameraKeys.size();
                    const float until = selectedSeqKey_ + 1 < n ? s.cameraKeys[selectedSeqKey_ + 1].time : s.duration;
                    const bool cut = selectedSeqKey_ == 0 || s.cameraKeys[selectedSeqKey_ - 1].easing == 2;
                    {
                        ui::FontScope bold(ui::boldFont());
                        ImGui::Text("Camera %s \xc2\xb7 %.2f s", cut ? "cut" : "blend", k.time);
                    }
                    ImGui::TextDisabled("shot %d of %d \xc2\xb7 holds until %.2f s", selectedSeqKey_ + 1, n, until);
                    ImGui::Spacing();
                    ui::InspectorRows rows(scaled(80.0f), scaled(120.0f));
                    // A shot either films from a Camera entity or is FREE - its
                    // own stored eye/look-at/FOV/roll. Free is what the take
                    // importer and the phone recorder write when pointed at the
                    // camera lane, so this has to be able to go back to it.
                    SceneObject* camObj = nullptr;
                    for (SceneObject& o : project_.active().objects)
                        if (o.name == k.camera && o.type == PrimitiveType::Camera) camObj = &o;
                    if (ui::BeginCombo("Shot from", k.camera.empty() ? "Free shot" : k.camera.c_str())) {
                        if (ImGui::Selectable("Free shot", k.camera.empty())) {
                            k.camera.clear();
                            changed = true;
                        }
                        bool anyCam = false;
                        for (const SceneObject& o : project_.objects()) {
                            if (o.type != PrimitiveType::Camera) continue;
                            if (!anyCam) ImGui::Separator();
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
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                        ImGui::SetTooltip("A Camera entity films from its own pose + FOV - animate\n"
                                          "it (or import a take into it) for a moving shot. Free\n"
                                          "shot uses the eye/look-at stored on this key instead.");
                    if (!k.camera.empty() && !camObj)
                        ImGui::TextColored(sem.danger, "Camera \"%s\" is not in this scene.", k.camera.c_str());
                    easingRow("Into next", k.easing, true);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                        ImGui::SetTooltip("How this shot becomes the next one: a hard cut, a\n"
                                          "smooth blend, or a constant-speed move.");
                    if (camObj) {
                        // A bound shot's lens IS the entity's: editing it here
                        // edits the camera (a scene edit, so it is undoable).
                        ui::DragFloat("FOV", &camObj->cameraFov, 0.5f, 5.0f, 150.0f, "%.0f\xc2\xb0");
                        changed |= ImGui::IsItemDeactivatedAfterEdit();
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                            ImGui::SetTooltip("%s's own field of view - every shot from it uses it.",
                                              camObj->name.c_str());
                    } else if (k.camera.empty()) {
                        ui::DragFloat("FOV", &k.fov, 0.5f, 5.0f, 150.0f, "%.0f\xc2\xb0");
                        changed |= ImGui::IsItemDeactivatedAfterEdit();
                    }
                    ui::DragFloat("Shake", &k.shake, 0.005f, 0.0f, 2.0f, "%.2f");
                    changed |= ImGui::IsItemDeactivatedAfterEdit();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                        ImGui::SetTooltip("Handheld camera shake amplitude in world units\n"
                                          "(interpolates between shots; 0 = steady).");
                    // Roll only means something for a free shot: a bound one
                    // takes its whole basis from the Camera entity.
                    if (k.camera.empty()) {
                        ui::DragFloat("Roll", &k.roll, 0.5f, -180.0f, 180.0f, "%.1f\xc2\xb0");
                        changed |= ImGui::IsItemDeactivatedAfterEdit();
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                            ImGui::SetTooltip("Dutch angle - rotation about the view axis\n"
                                              "(interpolates between shots; 0 = level).");
                    } else {
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextDisabled("Roll");
                        ImGui::SameLine(ImClamp(ImGui::GetContentRegionAvail().x * 0.34f, scaled(80.0f), scaled(120.0f)));
                        ImGui::TextDisabled("from %s's tilt", k.camera.c_str());
                    }
                    if (ui::DragFloat("Time", &k.time, 0.02f, 0.0f, s.duration, "%.2f s")) seqPlayhead_ = k.time;
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        const float t = k.time;
                        sortCamKeys();
                        for (int i = 0; i < (int)s.cameraKeys.size(); ++i)
                            if (s.cameraKeys[i].time == t) selectedSeqKey_ = i;
                        changed = true;
                    }
                    ImGui::Spacing();
                    if (!k.camera.empty() && camObj) {
                        const bool through = lookThroughCam_ == k.camera;
                        const std::string lt = (through ? "Stop looking through " : "Look through ") + k.camera;
                        if (ui::GhostButton((lt + "###lookthrough").c_str())) lookThroughCam_ = through ? "" : k.camera;
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                            ImGui::SetTooltip("The viewport films through this camera, so you can\n"
                                              "move it with the gizmo and see the shot change.");
                        ImGui::SameLine();
                    }
                    if (ui::DangerButton("Delete key")) {
                        s.cameraKeys.erase(s.cameraKeys.begin() + selectedSeqKey_);
                        selectedSeqKey_ = -1;
                        changed = true;
                    }
                } else if (objSel) {
                    SeqTrack& tr = s.tracks[selectedSeqTrack_];
                    // what the key was picked by: the whole key, or the
                    // components of the row it was clicked on
                    unsigned bits = selectedSeqBits_ & tr.keys[selectedSeqKey_].mask;
                    if (!bits) bits = tr.keys[selectedSeqKey_].mask;
                    const bool part = bits != tr.keys[selectedSeqKey_].mask;
                    const std::string scope = scopeName(bits);
                    const bool pose = bits == kSeqChAll;  // a whole-pose key (every older key)
                    {
                        ui::FontScope bold(ui::boldFont());
                        ImGui::Text("%s \xc2\xb7 %s", tr.target.empty() ? "?" : tr.target.c_str(),
                                    pose ? "key" : scope.c_str());
                    }
                    int nth = 0, count = 0;
                    for (int i = 0; i < (int)tr.keys.size(); ++i)
                        if (tr.keys[i].mask & bits) {
                            ++count;
                            if (i <= selectedSeqKey_) nth = count;
                        }
                    ImGui::TextDisabled("key %d of %d%s", nth, count, pose ? "" : (" on " + scope).c_str());
                    // the rest of the key, in the channels the track animates
                    const unsigned rest = tr.keys[selectedSeqKey_].mask & ~bits & seqTrackChannels(tr);
                    if (part && rest) ImGui::TextDisabled("Also keyed here: %s", scopeName(rest).c_str());
                    ImGui::Spacing();
                    ui::InspectorRows rows(scaled(80.0f), scaled(120.0f));
                    // retiming part of a key moves those components only
                    float tm = tr.keys[selectedSeqKey_].time;
                    if (ui::DragFloat("Time", &tm, 0.02f, 0.0f, s.duration, "%.2f s")) {
                        selectedSeqKey_ = seqSplitKey(tr, selectedSeqKey_, bits);
                        tr.keys[selectedSeqKey_].time = tm;
                        seqPlayhead_ = tm;
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        selectedSeqKey_ = seqSettleKeys(tr, selectedSeqKey_);
                        changed = true;
                    }
                    SeqObjectKey& k = tr.keys[selectedSeqKey_];
                    easingRow("Into next", k.easing, false);
                    if (part && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                        ImGui::SetTooltip("The easing belongs to the whole key at this time.");
                    // the values this key sets, for the components it was picked by
                    for (int ch = 0; ch < 3; ++ch) {
                        const unsigned cb = bits & kChannels[ch].bits;
                        if (!cb) continue;
                        float* v = ch == 0 ? k.position : ch == 1 ? k.rotation : k.scale;
                        if (cb == kChannels[ch].bits) {
                            if (ch == 0) ui::AxisDrag3("Position", v, 0.1f);
                            else if (ch == 1) ui::AxisDrag3("Rotation", v, 1.0f, -360.0f, 360.0f, "%.0f deg");
                            else ui::AxisDrag3("Scale", v, 0.05f, 0.01f, 1000.0f);
                            changed |= ImGui::IsItemDeactivatedAfterEdit();
                            continue;
                        }
                        for (int a = 0; a < 3; ++a) {
                            if (!(cb & (1u << (ch * 3 + a)))) continue;
                            const std::string lbl = std::string(kChannels[ch].name) + " " + kAxisNames[a];
                            if (ch == 0) ui::DragFloat(lbl.c_str(), &v[a], 0.1f, 0.0f, 0.0f, "%.3f");
                            else if (ch == 1) ui::DragFloat(lbl.c_str(), &v[a], 1.0f, -360.0f, 360.0f, "%.1f deg");
                            else ui::DragFloat(lbl.c_str(), &v[a], 0.05f, 0.01f, 1000.0f, "%.3f");
                            changed |= ImGui::IsItemDeactivatedAfterEdit();
                        }
                    }
                    if (bits & kSeqChColor) {
                        ui::ColorHex("Color", k.color);
                        changed |= ImGui::IsItemDeactivatedAfterEdit();
                    }
                    if (bits & kSeqChVis) {
                        if (ui::Switch("Visible (holds to the next key)", &k.visible)) changed = true;
                    }
                    ImGui::Spacing();
                    if (ui::GhostButton("Re-snapshot")) {
                        if (cutsceneSnapshotObjectKey(tr, k.time, bits) >= 0) changed = true;
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                        ImGui::SetTooltip("Set %s from the object's current pose.", part ? scope.c_str() : "this key");
                    ImGui::SameLine();
                    const std::string del = (part ? "Delete " + scope : std::string("Delete key")) + "###delkey";
                    if (ui::DangerButton(del.c_str())) {
                        seqClearKey(tr, selectedSeqKey_, bits);
                        selectedSeqKey_ = -1;
                        changed = true;
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                        ImGui::SetTooltip(part ? "Removes %s from this key; the rest of it stays."
                                               : "Removes the whole key (%s).",
                                          scope.c_str());
                }
                if (trackSel) {
                    // The selected lane: what it animates.
                    SeqTrack& tr = s.tracks[selectedSeqTrack_];
                    ImGui::Spacing();
                    ui::Caption("Track", tr.target.c_str());
                    ImGui::PushID("track");
                    ui::InspectorRows rows(scaled(80.0f), scaled(120.0f));
                    if (ui::Switch("Position", &tr.animPos)) changed = true;
                    if (ui::Switch("Rotation", &tr.animRot)) changed = true;
                    if (ui::Switch("Scale", &tr.animScale)) changed = true;
                    if (ui::Switch("Color", &tr.animColor)) changed = true;
                    if (ui::Switch("Visibility", &tr.animVis)) changed = true;
                    if (!objSel) {
                        if (ui::GhostButton("Key the playhead")) {
                            if (snapshotObjectKey(tr, seqPlayhead_)) changed = true;
                        }
                        ImGui::SameLine();
                        if (ui::DangerButton("Remove track")) {
                            s.tracks.erase(s.tracks.begin() + selectedSeqTrack_);
                            selectedSeqTrack_ = -1;
                            selectedSeqKey_ = -1;
                            changed = true;
                        }
                    }
                    ImGui::PopID();
                } else if (!camSel) {
                    ui::EmptyState(ui::Glyph::Film, "No key selected",
                                   "Click a shot or a key on the timeline. Double-click a lane "
                                   "to drop a key at that time.");
                }
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();  // ##seq_side

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

    if (deleteSeq) {
        for (SceneData& sc : project_.scenes)
            for (SceneObject& o : sc.objects)
                for (FlowNode& fn : o.flowGraph.nodes) {
                    const FlowNodeType* ft = flowNodeType(fn.type);
                    if (ft && ft->strKind == FlowParamKind::SequenceName && fn.str == s.name) fn.str.clear();
                }
        project_.sequences.erase(project_.sequences.begin() + selectedSequence_);
        selectSequence(-1);
        changed = true;
    }
    ImGui::End();

    commitIfEdited();
}
