// Tools > Weapon Editor (docs/weapons.md).
//
// Weapons are project-wide definitions, like fonts and menus - they are not
// placed in a scene, objects REFERENCE them by name (a Player object's
// inventory, an NPC's armament). So this window owns the whole list, and the
// per-object side of combat lives in Properties > Combat.
//
// The one genuinely unusual thing here is the "Model" tab: it drives the
// procedural weapon generator (weapongen.cpp) and can create the viewmodel
// asset AND the scene object that carries it in one click. That exists
// because a weapon system with no weapons to look at is a spreadsheet, and
// every gun model on the internet arrives with a licence attached.
//
// Everything is App:: methods declared in app.hpp; the file is separate only
// to keep app.cpp's panel section readable (the assetbrowser.cpp precedent).

#include "app.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include <imgui.h>

#include "animedit.hpp"
#include "app_internal.hpp"

namespace fs = std::filesystem;

namespace {

// Same rule as app.cpp's importer: asset names reach shell command lines,
// Makefiles and ISO9660 paths, so anything outside [A-Za-z0-9._-] folds to '_'.
std::string safeAssetName(const std::string& name) {
    std::string out = name;
    for (char& c : out) {
        const bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') || c == '.' || c == '_' ||
                          c == '-';
        if (!safe) c = '_';
    }
    return out;
}

const char* const kWeaponKinds[] = {"Firearm (hitscan)", "Firearm (projectile)",
                                    "Melee"};
// The burst kinds ScriptContext::spawnFx takes; index 0 is "no effect".
const char* const kFxKinds[] = {"None",  "Flash", "Sparks",
                                "Smoke", "Blood", "Debris"};

// A sound reference picker over Project::sounds. "" is always offered - most
// weapon sound slots are optional.
bool soundPicker(const char* label, const std::vector<std::string>& sounds,
                 std::string& ref) {
    bool changed = false;
    const std::string preview = ref.empty() ? "<none>" : ref;
    if (ImGui::BeginCombo(label, preview.c_str())) {
        if (ImGui::Selectable("<none>", ref.empty())) {
            ref.clear();
            changed = true;
        }
        for (const std::string& s : sounds)
            if (ImGui::Selectable(s.c_str(), s == ref)) {
                ref = s;
                changed = true;
            }
        ImGui::EndCombo();
    }
    return changed;
}

}  // namespace

// Picks a weapon by name. `allowEmpty` is not cosmetic: several Combat nodes
// read an empty reference as "the equipped weapon" or "any weapon", so the
// blank entry is a real value there and must not be offered where it is not.
bool App::weaponCombo(const char* label, std::string& weaponRef,
                      bool allowEmpty) {
    bool changed = false;
    const std::string preview =
        weaponRef.empty() ? (allowEmpty ? "<equipped / any>" : "<pick a weapon>")
                          : weaponRef;
    ImGui::SetNextItemWidth(scaled(200.0f));
    if (ImGui::BeginCombo(label, preview.c_str())) {
        if (allowEmpty && ImGui::Selectable("<equipped / any>", weaponRef.empty())) {
            weaponRef.clear();
            changed = true;
        }
        for (const WeaponDef& w : project_.weapons)
            if (ImGui::Selectable(w.name.c_str(), w.name == weaponRef)) {
                weaponRef = w.name;
                changed = true;
            }
        ImGui::Separator();
        if (ImGui::Selectable("Manage weapons...")) showWeaponEditor_ = true;
        ImGui::EndCombo();
    }
    return changed;
}

// A weapon is referenced by NAME from two places, and both must follow a
// rename or the reference silently becomes a dangling one that codegen drops.
//
// PREFAB members count. A Prefab holds real SceneObjects, serialized through
// the very same objectJson/readObjectsArray as a scene's, so a prefab member
// carries a loadout and a flow graph exactly like any other object - and
// nothing in the Properties panel would ever show you the dangling name.
static void retargetWeaponInObjects(std::vector<SceneObject>& objs,
                                    const std::string& from,
                                    const std::string& to) {
    for (SceneObject& o : objs) {
        for (std::string& w : o.weapons)
            if (w == from) w = to;
        for (FlowNode& n : o.flowGraph.nodes) {
            const FlowNodeType* t = flowNodeType(n.type);
            if (t && t->strKind == FlowParamKind::WeaponName && n.str == from)
                n.str = to;
        }
    }
}

void App::renameWeaponRefs(const std::string& from, const std::string& to) {
    if (from.empty() || from == to) return;
    for (SceneData& sc : project_.scenes)
        retargetWeaponInObjects(sc.objects, from, to);
    for (Prefab& pf : project_.prefabs)
        retargetWeaponInObjects(pf.objects, from, to);
}

// "Create viewmodel": generates the .obj/.mtl, drops a Model object into the
// scene and points the weapon's viewModel at it. One click from "I have a
// weapon definition" to "I can see it in my hands".
void App::addWeaponModelToScene(int weaponIndex) {
    if (weaponIndex < 0 || weaponIndex >= (int)project_.weapons.size()) return;
    WeaponDef& w = project_.weapons[weaponIndex];

    std::string base = safeAssetName(weaponModelName_);
    if (base.empty()) base = "weapon";
    // A distinct file per generated weapon: two weapons in one project must
    // never share (and overwrite) each other's .obj.
    std::string name = base;
    for (int n = 2; fs::exists(fs::path(project_.dir) / "res" / "models" /
                               "weapons" / (name + ".obj"));
         ++n)
        name = base + "-" + std::to_string(n);

    const weapongen::Mesh mesh = weapongen::generate(weaponGen_);
    std::string objRel, err;
    if (!weapongen::writeAssets(project_.dir, name, weaponGen_, mesh, &objRel,
                                &err)) {
        statusMessage_ = "Weapon model export failed: " + err;
        return;
    }
    addModelObject(objRel);  // creates the Model object + commitChange()
    if (project_.objects().empty()) return;
    SceneObject& o = project_.objects().back();
    // Size the viewmodel so it reads on screen. A weapon in the hands is
    // half a unit from the eye, and the game has ONE field of view for the
    // world and the weapon (no separate viewmodel FOV on a PS2), so a
    // life-size rifle at that distance covers a quarter of the screen.
    // Every FPS solves this by rendering a miniature; this aims each weapon
    // at the same ~0.38-unit apparent length, which is what the default
    // offsets below were tuned against. A pistol is already small enough and
    // clamps to 1.
    const float len = mesh.max[2] - mesh.min[2];
    if (len > 0.001f) {
        float s = 0.38f / len;
        if (s > 1.0f) s = 1.0f;
        if (s < 0.2f) s = 0.2f;
        w.viewScale = s;
        // Put the muzzle at the far end of the scaled model, so the flash and
        // the tracer leave the barrel rather than the middle of the frame.
        w.muzzleOffset[0] = w.viewOffset[0];
        w.muzzleOffset[1] = w.viewOffset[1] + 0.10f;
        w.muzzleOffset[2] = w.viewOffset[2] + mesh.max[2] * s + 0.05f;
    }
    // A generated weapon is a static .obj and can never carry clips, so
    // procedural motion is the only animation it will ever have - give it the
    // one that matches the silhouette instead of leaving it dead still.
    w.animMode = 0;
    switch (weaponGen_.kind) {
        case weapongen::Params::Revolver:
        case weapongen::Params::Shotgun:
            applyWeaponAnimPreset(w, WeaponAnimHeavy);
            break;
        case weapongen::Params::Smg:
        case weapongen::Params::Rifle:
            applyWeaponAnimPreset(w, WeaponAnimChatter);
            break;
        case weapongen::Params::Knife:
        case weapongen::Params::Sword:
        case weapongen::Params::Axe:
        case weapongen::Params::Crowbar:
            applyWeaponAnimPreset(w, WeaponAnimBlade);
            break;
        default:
            applyWeaponAnimPreset(w, WeaponAnimSnap);
            break;
    }
    // A viewmodel is scenery in the ordinary sense: it must not cast contact
    // shadows onto the world from half a unit in front of the camera, and the
    // runtime forces its collision off anyway - saying so here keeps the
    // editor's own preview honest.
    o.castShadow = false;
    o.collisionMode = 2;
    w.viewModel = o.name;
    weaponSel_ = weaponIndex;
    saveAll("Saved");
    statusMessage_ = "Created viewmodel '" + o.name + "' (" +
                     std::to_string(mesh.triangles()) + " tris) for " + w.name;
}

// --------------------------------------------------------------------------
// The Viewmodel tab's live preview.
//
// The camera sits where the player's does - at the origin, looking down +Z -
// because that is the only place the numbers on this tab mean anything: an
// Offset of (0.14, -0.13, 0.42) is a position on SCREEN, not in the world.
//
// weaponPreviewPose below is the HOST TWIN of wpnPinViewModels + the sway /
// bob / kick / reload / swing block in templates.cpp (search for
// "The equipped weapon in the player's hands"). Change either and the other
// must follow, or the preview stops predicting the console - which is the
// only thing that makes it worth having.
// --------------------------------------------------------------------------
namespace {

// The engine's vertical field of view (RendererCore3D's own default). A
// project cannot change it, so it is a constant here rather than a setting.
constexpr float kEngineFovDeg = 60.0f;

// The runtime's per-frame viewmodel state, advanced by the panel's clock.
struct WeaponAnimState {
    float phase = 0.0f;   // vmPhase
    float speed = 0.0f;   // vmSpeed
    float kick = 0.0f;    // viewKick
    float reload = 0.0f;  // WpnActor::reloadLeft
    float swing = 0.0f;   // WpnActor::swingLeft
};

// Twin of wpnPinViewModels with the player standing at the origin looking
// down +Z. That collapses the runtime's basis to right = (-1, 0, 0),
// up = (0, 1, 0), forward = (0, 0, 1), which is why an Offset X of +1 lands
// at world -1: the offsets are SCREEN directions.
void weaponPreviewPose(const WeaponDef& w, const WeaponAnimState& s,
                       float outPos[3], float outRot[3]) {
    float ox = w.viewOffset[0], oy = w.viewOffset[1], oz = w.viewOffset[2];
    float extraPitch = 0.0f, extraYaw = 0.0f, extraRoll = 0.0f;
    const float pi = 3.14159265f;

    // Sway and bob stay on in BOTH animation modes - a baked clip cannot know
    // how fast the player is walking.
    if (w.animSway > 0.0f) {
        ox += std::sin(s.phase * 0.73f) * w.animSway;
        oy += std::sin(s.phase * 1.11f) * w.animSway * 0.7f;
    }
    if (w.animBob > 0.0f && s.speed > 0.01f) {
        const float amp = w.animBob * (s.speed < 1.0f ? s.speed : 1.0f);
        oy += std::sin(s.phase * 5.0f) * amp;
        ox += std::sin(s.phase * 2.5f) * amp * 0.6f;
    }
    if (w.animMode == 0) {
        if (s.kick > 0.0f) {
            oz -= w.animKickBack * s.kick * w.viewScale;
            oy -= w.animKickBack * 0.2f * s.kick * w.viewScale;
            extraPitch -= w.animKickPitch * s.kick;
        }
        if (s.reload > 0.0f && w.reloadTime > 0.0001f) {
            const float t = 1.0f - s.reload / w.reloadTime;
            const float dip = std::sin(t * pi);
            oy -= w.animReloadDip * dip * w.viewScale;
            oz -= w.animReloadDip * 0.4f * dip * w.viewScale;
            extraRoll += w.animReloadRoll * dip;
        }
        if (s.swing > 0.0f && w.swingTime > 0.0001f) {
            const float t = 1.0f - s.swing / w.swingTime;
            const float arc = std::sin(t * pi);
            oz += w.animSwingReach * arc * w.viewScale;
            oy += w.animSwingReach * 0.3f * arc * w.viewScale;
            extraPitch += w.animSwingPitch * arc;
            extraYaw -= w.animSwingPitch * 0.55f * arc;
        }
    }
    outPos[0] = -ox;
    outPos[1] = oy;
    outPos[2] = oz;
    // The aim contributes nothing when the player looks down +Z, so what is
    // left is the authored rotation plus this frame's animation.
    outRot[0] = w.viewRot[0] + extraPitch;
    outRot[1] = w.viewRot[1] + extraYaw;
    outRot[2] = w.viewRot[2] + extraRoll;
}

}  // namespace

void App::drawWeaponViewmodelPreview(const WeaponDef& w) {
    const SceneObject* vm = nullptr;
    for (const SceneObject& o : project_.objects())
        if (o.name == w.viewModel) vm = &o;

    // --- the clock ---------------------------------------------------------
    // One dt for the whole preview, taken from ImGui rather than a fixed step
    // so a slow editor frame does not slow the weapon down.
    const double now = ImGui::GetTime();
    float dt = wpnPrevClock_ > 0.0 ? (float)(now - wpnPrevClock_) : 0.0f;
    wpnPrevClock_ = now;
    if (dt > 0.1f) dt = 0.1f;  // a stalled frame must not teleport the state
    if (!wpnPrevPlay_) dt = 0.0f;

    const bool melee = w.kind == 2;
    const std::string modelRel = (vm && vm->type == PrimitiveType::Model)
                                     ? vm->modelPath
                                     : std::string();
    const bool animatedModel = !modelRel.empty() && isAnimatedModelPath(modelRel);
    const bool clipMode = w.animMode == 1 && animatedModel;

    // --- advance the runtime twin ------------------------------------------
    {
        const float sway = w.animSwaySpeed > 0.0f ? w.animSwaySpeed : 1.0f;
        wpnPrevPhase_ += dt * 6.2831853f * sway;
        if (wpnPrevPhase_ > 6283.0f) wpnPrevPhase_ -= 6283.0f;
        // The runtime derives vmSpeed from how far the player actually moved;
        // here the Walk toggle IS that input, smoothed with the same rate.
        const float target = wpnPrevWalk_ ? 1.0f : 0.0f;
        const float k = std::min(dt * 8.0f, 1.0f);
        wpnPrevSpeed_ += (target - wpnPrevSpeed_) * k;
        if (wpnPrevKick_ > 0.0f) {
            const float rate = w.animRecover > 0.2f ? w.animRecover : 0.2f;
            wpnPrevKick_ = std::max(0.0f, wpnPrevKick_ - dt * rate);
        }
        wpnPrevReload_ = std::max(0.0f, wpnPrevReload_ - dt);
        wpnPrevSwing_ = std::max(0.0f, wpnPrevSwing_ - dt);
        if (wpnPrevNextShot_ > 0.0f) wpnPrevNextShot_ -= dt;
    }

    // A shot: the same two things wpnTryFire does to the viewmodel - the kick
    // spring is charged and, in clip mode, the fire clip is requested.
    auto fire = [&]() {
        if (melee) {
            wpnPrevSwing_ = w.swingTime > 0.0001f ? w.swingTime : 0.25f;
            if (clipMode && !w.clipFire.empty()) {
                wpnPrevClipState_ = 1;
                wpnPrevClipTime_ = 0.0f;
            }
            return;
        }
        wpnPrevKick_ = 1.0f;
        if (clipMode && !w.clipFire.empty()) {
            wpnPrevClipState_ = 1;
            wpnPrevClipTime_ = 0.0f;
        }
    };
    if (wpnPrevAuto_ && wpnPrevPlay_ && wpnPrevNextShot_ <= 0.0f) {
        fire();
        wpnPrevNextShot_ = w.fireRate > 0.01f ? 1.0f / w.fireRate : 0.5f;
    }

    // --- clip playback (clip mode only) ------------------------------------
    // The states are the runtime's: a one-shot plays once and hands back to
    // the idle loop. Times are OUTPUT seconds, like the Animation Editor's
    // playhead, and only converted to source seconds for the pose.
    std::string srcClip;
    float clipSrcTime = 0.0f, trimA = 0.0f, trimB = 0.0f;
    if (clipMode) {
        // -1 = "a one-shot finished last frame": hand back to idle from the
        // start of the idle clip rather than from the one-shot's end time.
        if (wpnPrevClipState_ < 0) {
            wpnPrevClipState_ = 0;
            wpnPrevClipTime_ = 0.0f;
        }
        auto effClipFor = [&](int state) {
            switch (state) {
                case 1: return w.clipFire;
                case 2: return w.clipReload;
                case 3: return w.clipEquip;
                default: return w.clipIdle;
            }
        };
        std::string eff = effClipFor(wpnPrevClipState_);
        if (eff.empty()) {
            wpnPrevClipState_ = 0;
            eff = w.clipIdle;
        }
        srcClip = eff.empty() ? std::string()
                              : animedit::sourceName(project_, modelRel, eff);
        if (!srcClip.empty()) {
            const float srcDur =
                viewport_.animClipDuration(modelRel, std::string(), srcClip);
            const AnimClipEdit* edit =
                animedit::findEdit(project_, modelRel, srcClip);
            trimB = srcDur;
            animedit::trimWindow(edit, srcDur, trimA, trimB);
            const float scale =
                animedit::totalTimeScale(project_, modelRel, srcClip);
            const float outDur = scale > 0.001f ? (trimB - trimA) / scale : 0.0f;
            wpnPrevClipTime_ += dt;
            if (outDur > 0.0001f) {
                if (wpnPrevClipState_ == 0) {
                    wpnPrevClipTime_ = std::fmod(wpnPrevClipTime_, outDur);
                } else if (wpnPrevClipTime_ >= outDur) {
                    // Back to idle, exactly like the animFinished handoff.
                    // This frame still shows the one-shot's last pose - the
                    // idle clip has its own trim, and re-resolving it here
                    // would pose it at a time taken from another clip.
                    wpnPrevClipTime_ = outDur;
                    wpnPrevClipState_ = -1;  // -> idle on the next frame
                }
            } else {
                wpnPrevClipTime_ = 0.0f;
            }
            clipSrcTime = wpnPrevClipTime_ * scale;
        }
    }

    // --- the image ----------------------------------------------------------
    // Sized at the game's own aspect: an eye view at the wrong aspect frames
    // the weapon differently from the console, which is the one thing this
    // preview exists to answer.
    const float aspect = project_.settings.widescreen ? 16.0f / 9.0f : 4.0f / 3.0f;
    float pw = std::max(scaled(200.0f), ImGui::GetContentRegionAvail().x);
    float ph = pw / aspect;
    const float phMax = scaled(230.0f);
    if (ph > phMax) {
        ph = phMax;
        pw = ph * aspect;
    }

    Viewport::WeaponPreviewDesc d;
    d.modelRel = modelRel;
    d.materialRel = vm ? vm->materialPath : std::string();
    // A type with no geometry of its own (a marker used as a placeholder)
    // falls back to a box rather than to whichever unit shape it numbers next
    // to - a cone standing in for a Player marker would just be confusing.
    d.shape = (vm && (int)vm->type >= 0 && (int)vm->type <= 3) ? (int)vm->type : 0;
    if (vm)
        for (int i = 0; i < 3; ++i) d.tint[i] = vm->color[i];
    d.yawOffset = vm ? vm->modelYawOffset : 0.0f;
    const WeaponAnimState st{wpnPrevPhase_, wpnPrevSpeed_, wpnPrevKick_,
                             wpnPrevReload_, wpnPrevSwing_};
    weaponPreviewPose(w, st, d.position, d.rotation);
    const float vs = w.viewScale > 0.0001f ? w.viewScale : 1.0f;
    for (int i = 0; i < 3; ++i) d.scale[i] = (vm ? vm->scale[i] : 1.0f) * vs;
    d.clip = srcClip;
    d.time = clipSrcTime;
    d.trimStart = trimA;
    d.trimEnd = trimB;
    d.muzzle[0] = -w.muzzleOffset[0];
    d.muzzle[1] = w.muzzleOffset[1];
    d.muzzle[2] = w.muzzleOffset[2];
    d.showMuzzle = wpnPrevMuzzle_;
    d.eyeView = wpnPrevEye_;
    d.fovDeg = kEngineFovDeg;
    d.angleDeg = wpnPrevYaw_;
    d.pitchDeg = wpnPrevPitch_;
    d.zoom = wpnPrevZoom_;
    d.wireframe = wpnPrevWire_;

    const uint32_t tex = viewport_.renderWeaponPreview((int)pw, (int)ph, d);
    if (tex) {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(pw, ph), ImVec2(0, 1),
                     ImVec2(1, 0));
        // The crosshair: without something marking the screen centre there is
        // no way to judge an offset by eye. Drawn here rather than in GL - it
        // is a 2D overlay, not part of the scene.
        if (wpnPrevEye_) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 c(origin.x + pw * 0.5f, origin.y + ph * 0.5f);
            const float r = scaled(6.0f);
            const ImU32 col = IM_COL32(255, 255, 255, 110);
            dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), col);
            dl->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r), col);
        }
        // The Material/Animation Editor gesture: drag to orbit, wheel to
        // dolly. Only in the turntable view - the eye view IS the framing.
        if (!wpnPrevEye_) {
            if (ImGui::IsItemHovered()) {
                const float wheel = ImGui::GetIO().MouseWheel;
                if (wheel != 0.0f)
                    wpnPrevZoom_ =
                        std::clamp(wpnPrevZoom_ * (1.0f + wheel * 0.1f), 0.1f, 8.0f);
            }
            if (ImGui::IsItemActive() &&
                ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                const ImVec2 dlt = ImGui::GetIO().MouseDelta;
                wpnPrevYaw_ += dlt.x * 0.4f;
                wpnPrevPitch_ =
                    std::clamp(wpnPrevPitch_ - dlt.y * 0.3f, -80.0f, 80.0f);
            }
        }
    }

    // --- what is (not) being shown -----------------------------------------
    if (!vm) {
        ImGui::TextDisabled(
            "No viewmodel object - the weapon is invisible in the hands.");
    } else if (vm->type != PrimitiveType::Model) {
        ImGui::TextDisabled("\"%s\" is a placeholder shape, not a model.",
                            vm->name.c_str());
    } else if (w.animMode == 1 && !animatedModel) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f),
                           "Clip mode, but \"%s\" is a static model - nothing "
                           "will animate.",
                           vm->name.c_str());
    } else if (clipMode) {
        ImGui::TextDisabled("clip: %s",
                            srcClip.empty() ? "<none set>" : srcClip.c_str());
    } else {
        ImGui::TextDisabled("procedural motion (Animation tab)");
    }

    // --- transport ----------------------------------------------------------
    if (ImGui::Button(wpnPrevPlay_ ? "Pause" : "Play", ImVec2(scaled(58.0f), 0)))
        wpnPrevPlay_ = !wpnPrevPlay_;
    ImGui::SameLine();
    if (ImGui::Button(melee ? "Swing" : "Fire", ImVec2(scaled(58.0f), 0))) {
        wpnPrevPlay_ = true;
        fire();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Runs one shot through the same kick spring the game uses.");
    ImGui::SameLine();
    if (ImGui::Button("Reload", ImVec2(scaled(62.0f), 0))) {
        wpnPrevPlay_ = true;
        wpnPrevReload_ = w.reloadTime > 0.0001f ? w.reloadTime : 1.0f;
        if (clipMode && !w.clipReload.empty()) {
            wpnPrevClipState_ = 2;
            wpnPrevClipTime_ = 0.0f;
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(scaled(110.0f));
    int view = wpnPrevEye_ ? 0 : 1;
    if (ImGui::Combo("##wpnprevview", &view, "Eye view\0Turntable\0"))
        wpnPrevEye_ = view == 0;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Eye view: the player's own camera, at the engine's field of\n"
            "view - where the weapon will actually sit on screen.\n"
            "Turntable: orbit the model to inspect it (drag / wheel).");

    ImGui::Checkbox("Walk", &wpnPrevWalk_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Drives the walk bob, which scales with the player's "
                          "real speed in game.");
    ImGui::SameLine();
    ImGui::Checkbox("Auto", &wpnPrevAuto_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Keeps firing at the weapon's Rate (Firing tab).");
    ImGui::SameLine();
    ImGui::Checkbox("Muzzle", &wpnPrevMuzzle_);
    ImGui::SameLine();
    ImGui::Checkbox("Wire", &wpnPrevWire_);
    ImGui::Separator();
}

void App::drawWeaponEditorWindow() {
    if (!showWeaponEditor_ || !hasProject_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(700.0f), scaled(560.0f)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Weapon Editor", &showWeaponEditor_)) {
        ImGui::End();
        return;
    }

    bool changed = false;

    // --- the list ---------------------------------------------------------
    ImGui::BeginChild("weaponlist", ImVec2(scaled(180.0f), 0), true);
    for (size_t i = 0; i < project_.weapons.size(); ++i) {
        ImGui::PushID((int)i);
        if (ImGui::Selectable(project_.weapons[i].name.c_str(),
                              weaponSel_ == (int)i))
            weaponSel_ = (int)i;
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginGroup();
    if (ImGui::Button("+ Weapon")) {
        // 32 is the hard ceiling: the runtime's carried-weapon set is a
        // 32-bit mask, and the generated code static_asserts on it.
        if (project_.weapons.size() >= 32) {
            statusMessage_ = "A project can define at most 32 weapons";
        } else {
            WeaponDef w;
            std::string name = "Weapon";
            for (int n = 2; project_.findWeapon(name); ++n)
                name = "Weapon " + std::to_string(n);
            w.name = name;
            project_.weapons.push_back(w);
            weaponSel_ = (int)project_.weapons.size() - 1;
            changed = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate") && weaponSel_ >= 0 &&
        weaponSel_ < (int)project_.weapons.size()) {
        // The same 32-weapon ceiling "+ Weapon" enforces above. Without this
        // the copy goes in regardless and the project only fails much later,
        // inside Docker, on the generated static_assert.
        if (project_.weapons.size() >= 32) {
            statusMessage_ = "A project can define at most 32 weapons";
        } else {
            WeaponDef w = project_.weapons[weaponSel_];
            std::string name = w.name + " copy";
            for (int n = 2; project_.findWeapon(name); ++n)
                name = w.name + " copy " + std::to_string(n);
            w.name = name;
            project_.weapons.push_back(w);
            weaponSel_ = (int)project_.weapons.size() - 1;
            changed = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete") && weaponSel_ >= 0 &&
        weaponSel_ < (int)project_.weapons.size()) {
        // Loadout entries pointing at the deleted weapon go with it -
        // codegen would drop them anyway, and a stale name in the Properties
        // list reads as a bug. Prefab members carry loadouts too (see
        // retargetWeaponInObjects), and flow nodes naming the weapon have to
        // be cleared as well: left alone, codegen resolves the dead name to
        // -1, which the runtime reads as "any weapon / the equipped one" - so
        // `Has Weapon "Shotgun"` would quietly become `Has Weapon <anything>`.
        const std::string gone = project_.weapons[weaponSel_].name;
        auto dropWeapon = [&](std::vector<SceneObject>& objs) {
            for (SceneObject& o : objs) {
                o.weapons.erase(
                    std::remove(o.weapons.begin(), o.weapons.end(), gone),
                    o.weapons.end());
                for (FlowNode& n : o.flowGraph.nodes) {
                    const FlowNodeType* t = flowNodeType(n.type);
                    if (t && t->strKind == FlowParamKind::WeaponName &&
                        n.str == gone)
                        n.str.clear();
                }
            }
        };
        for (SceneData& sc : project_.scenes) dropWeapon(sc.objects);
        for (Prefab& pf : project_.prefabs) dropWeapon(pf.objects);
        project_.weapons.erase(project_.weapons.begin() + weaponSel_);
        if (weaponSel_ >= (int)project_.weapons.size())
            weaponSel_ = (int)project_.weapons.size() - 1;
        changed = true;
    }

    if (project_.weapons.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped(
            "No weapons yet. Add one, then give it to a Player object under "
            "Properties > Combat (or with a Give Weapon flow node).");
        ImGui::TextWrapped(
            "A project with no weapons and no damageable objects generates no "
            "combat code at all - see docs/weapons.md.");
        ImGui::EndGroup();
        if (changed) saveAll("Saved");
        ImGui::End();
        return;
    }
    if (weaponSel_ < 0 || weaponSel_ >= (int)project_.weapons.size())
        weaponSel_ = 0;
    WeaponDef& w = project_.weapons[weaponSel_];

    ImGui::Separator();
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s", w.name.c_str());
        ImGui::SetNextItemWidth(scaled(200.0f));
        if (ImGui::InputText("Name", buf, sizeof(buf))) {
            std::string want = buf;
            // The name IS the reference key, so it must stay unique and
            // non-empty; a clash would silently redirect other references.
            bool clash = want.empty();
            for (size_t i = 0; i < project_.weapons.size(); ++i)
                if ((int)i != weaponSel_ && project_.weapons[i].name == want)
                    clash = true;
            if (!clash) {
                renameWeaponRefs(w.name, want);
                w.name = want;
            }
        }
        changed |= ImGui::IsItemDeactivatedAfterEdit();
    }
    ImGui::SetNextItemWidth(scaled(200.0f));
    if (ImGui::Combo("Kind", &w.kind, kWeaponKinds, 3)) changed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Hitscan: the shot arrives the frame it is fired (every PS2-era\n"
            "gun). Projectile: a visible body flies out and drops.\n"
            "Melee: an arc swept in front of the attacker.");

    // Every tab below is label-on-the-right rows; without a width cap the
    // drags eat the whole pane and the labels clip off the edge.
    ImGui::PushItemWidth(scaled(220.0f));
    if (ImGui::BeginTabBar("weapontabs")) {
        if (ImGui::BeginTabItem("Damage")) {
            changed |= ImGui::DragFloat("Damage", &w.damage, 0.5f, 0.0f, 10000.0f);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Hit points per hit - per PELLET when Pellets > 1, so a\n"
                    "shotgun's real damage is Damage x Pellets at point blank.");
            changed |= ImGui::DragFloat(
                w.kind == 2 ? "Reach" : "Range", &w.range, 0.5f, 0.1f, 10000.0f);
            changed |= ImGui::SliderFloat("Falloff", &w.falloff, 0.0f, 1.0f);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Fraction of the damage lost at maximum range.\n"
                    "0 = a hit is a hit at any distance.");
            changed |= ImGui::DragFloat("Impulse", &w.impulse, 0.1f, 0.0f, 500.0f);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Physics shove pushed into a rigid body that is hit\n"
                    "(units/s). Only affects objects with Physics on.");
            if (w.kind == 1) {
                ImGui::SeparatorText("Projectile");
                changed |= ImGui::DragFloat("Speed", &w.projSpeed, 0.5f, 0.5f,
                                            500.0f);
                changed |= ImGui::DragFloat("Gravity", &w.projGravity, 0.1f,
                                            -50.0f, 100.0f);
                changed |= ImGui::DragFloat("Size", &w.projSize, 0.01f, 0.01f,
                                            5.0f);
                changed |= ImGui::ColorEdit3("Color##proj", w.projColor,
                                             ImGuiColorEditFlags_NoInputs);
                changed |= ImGui::DragFloat("Blast radius", &w.blastRadius, 0.1f,
                                            0.0f, 100.0f);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "0 = the projectile damages only what it touches.\n"
                        "Above 0 the impact damages everything within, weaker\n"
                        "toward the edge.");
            }
            if (w.kind == 2) {
                ImGui::SeparatorText("Swing");
                changed |= ImGui::DragFloat("Arc", &w.meleeArc, 1.0f, 1.0f, 180.0f);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Half-angle of the sweep around the aim. Everything\n"
                        "damageable inside it and within Reach is hit once.");
                changed |= ImGui::DragFloat("Swing time", &w.swingTime, 0.01f,
                                            0.05f, 5.0f);
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Firing")) {
            changed |= ImGui::DragFloat("Rate (shots/s)", &w.fireRate, 0.1f,
                                        0.05f, 60.0f);
            changed |= ImGui::Checkbox("Automatic (hold to fire)", &w.automatic);
            changed |= ImGui::DragFloat("Spread", &w.spread, 0.1f, 0.0f, 45.0f);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Cone half-angle of a shot, in degrees.");
            changed |= ImGui::SliderInt("Pellets", &w.pellets, 1, 16);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Rays per shot: 1 = a bullet, 8 = buckshot.");
            changed |= ImGui::DragFloat("Recoil", &w.recoil, 0.05f, 0.0f, 30.0f);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Degrees the view kicks up per shot. It settles back on a\n"
                    "spring, so the player can fight it with the stick.");
            changed |= ImGui::SliderFloat("Rumble", &w.rumble, 0.0f, 1.0f);
            ImGui::SeparatorText("Ammunition");
            changed |= ImGui::DragInt("Magazine", &w.magSize, 1.0f, 0, 999);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("0 = no magazine at all: never reloads.");
            changed |= ImGui::DragInt("Reserve", &w.reserve, 1.0f, -1, 9999);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Spare rounds carried. -1 = bottomless.");
            changed |= ImGui::DragFloat("Reload time", &w.reloadTime, 0.05f, 0.0f,
                                        20.0f);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Viewmodel")) {
            drawWeaponViewmodelPreview(w);
            {
                const std::string preview =
                    w.viewModel.empty() ? "<none - invisible weapon>" : w.viewModel;
                ImGui::SetNextItemWidth(scaled(220.0f));
                if (ImGui::BeginCombo("Object", preview.c_str())) {
                    if (ImGui::Selectable("<none>", w.viewModel.empty())) {
                        w.viewModel.clear();
                        changed = true;
                    }
                    for (const SceneObject& o : project_.objects())
                        if (ImGui::Selectable(o.name.c_str(), o.name == w.viewModel)) {
                            w.viewModel = o.name;
                            changed = true;
                        }
                    ImGui::EndCombo();
                }
            }
            prefHelp(
                "The weapon in the player's hands is an ordinary SCENE OBJECT,\n"
                "pinned in front of the camera while this weapon is equipped\n"
                "and hidden otherwise - so it lights, takes materials and\n"
                "previews like anything else you place in the world.\n\n"
                "Nothing else in the game needs to know: the runtime writes\n"
                "the object's transform every frame, there is no attachment\n"
                "system and no parenting.");
            changed |= ImGui::DragFloat3("Offset", w.viewOffset, 0.01f);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Camera space: X right, Y up, Z forward (units).");
            changed |= ImGui::DragFloat("Scale", &w.viewScale, 0.01f, 0.01f, 20.0f);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Multiplies the object's own scale. The game has ONE field\n"
                    "of view for the world and the weapon (no separate\n"
                    "viewmodel FOV on a PS2), so a life-size rifle half a unit\n"
                    "from the eye covers a quarter of the screen - every FPS\n"
                    "renders a miniature instead. 'Create viewmodel' below\n"
                    "sizes new models for you.");
            changed |= ImGui::DragFloat3("Rotation", w.viewRot, 0.5f);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Degrees on top of the aim, which already points the\n"
                    "model's +Z down the view ray.");
            changed |= ImGui::DragFloat3("Muzzle", w.muzzleOffset, 0.01f);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Where the shot leaves the weapon, in the same camera\n"
                    "frame - the muzzle flash and the tracer start here.\n"
                    "The preview marks it in orange.");

            ImGui::SeparatorText("Generate a model");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Procedural, licence-free and sized in the project's own\n"
                    "units - a weapon system with no weapon to look at is a\n"
                    "spreadsheet, and every gun model on the internet arrives\n"
                    "with a licence attached.\n\n"
                    "+Z is the muzzle and the origin is the grip, which is what\n"
                    "the offsets above assume. The result is a static .obj, so\n"
                    "it can only ever carry PROCEDURAL animation.");
            ImGui::SetNextItemWidth(scaled(200.0f));
            if (ImGui::Combo("Type", &weaponGen_.kind,
                             "Pistol\0Revolver\0SMG\0Rifle\0Shotgun\0Knife\0"
                             "Sword\0Axe\0Crowbar\0")) {
                // Selecting a type loads its tuned preset - the sliders are
                // for deviating from a good starting point, not for finding
                // one from scratch.
                const auto& ps = weapongen::presets();
                if (weaponGen_.kind >= 0 && weaponGen_.kind < (int)ps.size())
                    weaponGen_ = ps[weaponGen_.kind];
                std::string n = weapongen::kindName(weaponGen_.kind);
                for (char& c : n) c = (char)tolower((unsigned char)c);
                std::snprintf(weaponModelName_, sizeof(weaponModelName_), "%s",
                              n.c_str());
            }
            ImGui::SetNextItemWidth(scaled(200.0f));
            ImGui::DragFloat("Length", &weaponGen_.length, 0.01f, 0.05f, 5.0f);
            ImGui::SetNextItemWidth(scaled(200.0f));
            ImGui::DragFloat("Bulk", &weaponGen_.bulk, 0.01f, 0.2f, 3.0f);
            ImGui::SetNextItemWidth(scaled(200.0f));
            ImGui::SliderInt("Sides", &weaponGen_.sides, 3, 16);
            ImGui::SetNextItemWidth(scaled(200.0f));
            ImGui::SliderFloat("Wear", &weaponGen_.wear, 0.0f, 1.0f);
            ImGui::ColorEdit3("Metal", weaponGen_.metal, ImGuiColorEditFlags_NoInputs);
            ImGui::SameLine();
            ImGui::ColorEdit3("Grip", weaponGen_.grip, ImGuiColorEditFlags_NoInputs);
            ImGui::SameLine();
            ImGui::ColorEdit3("Accent", weaponGen_.accent,
                              ImGuiColorEditFlags_NoInputs);
            ImGui::SetNextItemWidth(scaled(200.0f));
            ImGui::InputText("Asset name", weaponModelName_,
                             sizeof(weaponModelName_));
            {
                const weapongen::Mesh preview = weapongen::generate(weaponGen_);
                ImGui::TextDisabled("%d triangles, %.2f x %.2f x %.2f units",
                                    preview.triangles(),
                                    preview.max[0] - preview.min[0],
                                    preview.max[1] - preview.min[1],
                                    preview.max[2] - preview.min[2]);
            }
            if (ImGui::Button("Create viewmodel + add to scene"))
                addWeaponModelToScene(weaponSel_);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Animation")) {
            const char* modes[] = {"Procedural (no animated model needed)",
                                   "Clips (from an animated viewmodel)"};
            ImGui::SetNextItemWidth(scaled(300.0f));
            if (ImGui::Combo("Mode", &w.animMode, modes, 2)) changed = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Procedural: the runtime animates the viewmodel's\n"
                    "transform from the numbers below - the only animation a\n"
                    "GENERATED weapon can have, since a static .obj carries no\n"
                    "clips.\n\n"
                    "Clips: the viewmodel is your own animated .glb/.fbx and\n"
                    "the runtime plays its clips on fire / reload / equip. The\n"
                    "procedural kick and swing then step aside - but the idle\n"
                    "sway and walk bob stay on, because a baked clip cannot\n"
                    "know how fast the player is walking.");

            // Which viewmodel object this weapon points at, and whether it can
            // actually carry clips - the answer decides which half of this tab
            // is real, so say it out loud rather than letting the user guess.
            const SceneObject* vm = nullptr;
            for (const SceneObject& o : project_.objects())
                if (o.name == w.viewModel) vm = &o;
            const bool animated =
                vm && vm->type == PrimitiveType::Model &&
                isAnimatedModelPath(vm->modelPath);

            if (w.animMode == 1) {
                if (!vm) {
                    ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f),
                                       "No viewmodel object - pick one in the "
                                       "Viewmodel tab.");
                } else if (!animated) {
                    ImGui::TextColored(
                        ImVec4(1.0f, 0.55f, 0.35f, 1.0f),
                        "\"%s\" is not an animated model, so no clip will\n"
                        "resolve (harmless - the weapon just will not move).\n"
                        "Give it a .glb/.fbx, or switch back to Procedural.",
                        w.viewModel.c_str());
                }
                const std::vector<std::string> clips =
                    animated ? effectiveClips(vm->modelPath)
                             : std::vector<std::string>();
                auto clipPick = [&](const char* label, std::string& ref,
                                    const char* tip) {
                    const std::string cur = ref.empty() ? "<none>" : ref;
                    ImGui::SetNextItemWidth(scaled(220.0f));
                    if (ImGui::BeginCombo(label, cur.c_str())) {
                        if (ImGui::Selectable("<none>", ref.empty())) {
                            ref.clear();
                            changed = true;
                        }
                        for (const std::string& c : clips)
                            if (ImGui::Selectable(c.c_str(), c == ref)) {
                                ref = c;
                                changed = true;
                            }
                        if (clips.empty())
                            ImGui::TextDisabled("The viewmodel has no clips.");
                        ImGui::EndCombo();
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
                };
                ImGui::SeparatorText("Clips");
                clipPick("Idle", w.clipIdle,
                         "Loops whenever nothing else is playing.");
                clipPick("Fire", w.clipFire,
                         "One shot per shot. A multi-pellet blast restarts it\n"
                         "once, not once per pellet.");
                clipPick("Reload", w.clipReload,
                         "Played when a reload starts. Author it to the\n"
                         "weapon's Reload time or they will disagree.");
                clipPick("Equip", w.clipEquip,
                         "Played once when the weapon is drawn, then Idle\n"
                         "takes over.");
            }

            ImGui::SeparatorText("Motion");
            if (w.animMode == 1)
                ImGui::TextDisabled(
                    "In clip mode only Sway and Bob apply - the clip owns the "
                    "rest.");
            // The preset row is the "just make it feel like a gun" button.
            ImGui::TextUnformatted("Preset");
            for (int i = 0; i < WeaponAnimPresetCount; ++i) {
                if (i % 3 != 0) ImGui::SameLine();
                ImGui::PushID(i);
                if (ImGui::SmallButton(weaponAnimPresetName(i))) {
                    applyWeaponAnimPreset(w, i);
                    changed = true;
                }
                ImGui::PopID();
            }
            changed |= ImGui::DragFloat("Sway", &w.animSway, 0.001f, 0.0f, 0.2f,
                                        "%.3f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "The hands never being quite still, in world units.\n"
                    "0 = the weapon is welded to the camera.");
            changed |= ImGui::DragFloat("Sway speed", &w.animSwaySpeed, 0.05f,
                                        0.1f, 8.0f);
            changed |= ImGui::DragFloat("Walk bob", &w.animBob, 0.002f, 0.0f,
                                        0.3f, "%.3f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Amplitude at a full-speed walk; it scales with the\n"
                    "player's actual planar speed, so it stops when they do.");
            if (w.animMode == 0) {
                changed |= ImGui::DragFloat("Kick back", &w.animKickBack, 0.005f,
                                            0.0f, 1.0f, "%.3f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Units the weapon drives into the screen per shot.\n"
                        "This is the WEAPON moving; 'Recoil' on the Firing tab\n"
                        "is the AIM moving. They are deliberately separate.");
                changed |= ImGui::DragFloat("Kick pitch", &w.animKickPitch, 0.2f,
                                            0.0f, 60.0f);
                changed |= ImGui::DragFloat("Recovery", &w.animRecover, 0.1f,
                                            0.2f, 30.0f);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Decay rate of the kick, 1/s. Higher = snappier.");
                changed |= ImGui::DragFloat("Reload dip", &w.animReloadDip, 0.01f,
                                            0.0f, 1.0f);
                changed |= ImGui::DragFloat("Reload roll", &w.animReloadRoll, 1.0f,
                                            0.0f, 180.0f);
                if (w.kind == 2) {
                    changed |= ImGui::DragFloat("Swing reach", &w.animSwingReach,
                                                0.01f, 0.0f, 2.0f);
                    changed |= ImGui::DragFloat("Swing chop", &w.animSwingPitch,
                                                1.0f, 0.0f, 180.0f);
                }
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Effects")) {
            auto fxBlock = [&](const char* title, const char* tip, WeaponFx& f) {
                ImGui::PushID(title);
                ImGui::SeparatorText(title);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
                ImGui::SetNextItemWidth(scaled(160.0f));
                changed |= ImGui::Combo("Kind", &f.kind, kFxKinds, 6);
                if (f.kind > 0) {
                    changed |= ImGui::ColorEdit3("Color", f.color,
                                                 ImGuiColorEditFlags_NoInputs);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(scaled(110.0f));
                    changed |= ImGui::DragFloat("Size", &f.size, 0.005f, 0.01f,
                                                5.0f);
                    ImGui::SetNextItemWidth(scaled(110.0f));
                    changed |= ImGui::SliderInt("Count", &f.count, 1, 32);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(scaled(110.0f));
                    changed |= ImGui::DragFloat("Life", &f.life, 0.01f, 0.02f,
                                                8.0f);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(scaled(110.0f));
                    changed |= ImGui::DragFloat("Speed", &f.speed, 0.1f, 0.0f,
                                                60.0f);
                }
                ImGui::PopID();
            };
            fxBlock("Muzzle", "Thrown at the muzzle when the shot leaves.",
                    w.muzzleFx);
            fxBlock("Impact", "Thrown where the shot lands on scenery.",
                    w.impactFx);
            fxBlock("Blood",
                    "Thrown where the shot lands on a DAMAGEABLE object - or "
                    "on anything whose Hit effect is set to Blood.",
                    w.bloodFx);
            ImGui::SeparatorText("Tracer");
            changed |= ImGui::Checkbox("Draw a tracer streak", &w.tracer);
            if (w.tracer)
                changed |= ImGui::ColorEdit3("Tracer color", w.tracerColor,
                                             ImGuiColorEditFlags_NoInputs);
            ImGui::TextDisabled(
                "All bursts come out of one shared 128-particle pool, so a "
                "firefight costs the same memory as a quiet room.");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Sounds")) {
            if (project_.sounds.empty()) {
                ImGui::TextDisabled(
                    "No sounds imported - add WAVs in the Sounds panel first.");
            } else {
                ImGui::SetNextItemWidth(scaled(260.0f));
                changed |= soundPicker("Fire", project_.sounds, w.fireSound);
                ImGui::SetNextItemWidth(scaled(260.0f));
                changed |= soundPicker("Reload", project_.sounds, w.reloadSound);
                ImGui::SetNextItemWidth(scaled(260.0f));
                changed |= soundPicker("Empty (dry click)", project_.sounds,
                                       w.emptySound);
                ImGui::SetNextItemWidth(scaled(260.0f));
                changed |= soundPicker("Impact", project_.sounds, w.impactSound);
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::PopItemWidth();

    ImGui::EndGroup();
    // Like the other project-wide collections (fonts, menus, sequences),
    // weapons are saved directly rather than pushed onto the undo stack -
    // undo covers the scene model.
    if (changed) saveAll("Saved");
    ImGui::End();
}
