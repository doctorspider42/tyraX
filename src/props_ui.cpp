// -------------------------------------------------------------------------
// The object inspector: the single- and multi-selection Properties windows,
// the per-object LOD overrides and the area/script pickers they use.
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
#include "scrollsim.hpp"
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

// Display names for the Type field (primitiveTypeName() is the serialized
// lowercase form, e.g. "spawn-point").
static const char* typeLabel(PrimitiveType t) {
    switch (t) {
        case PrimitiveType::Box: return "Box";
        case PrimitiveType::Sphere: return "Sphere";
        case PrimitiveType::Cylinder: return "Cylinder";
        case PrimitiveType::Cone: return "Cone";
        case PrimitiveType::Plane: return "Plane";
        case PrimitiveType::Decal: return "Decal";
        case PrimitiveType::SpawnPoint: return "Spawn point";
        case PrimitiveType::Model: return "3D model";
        case PrimitiveType::Player: return "Player";
        case PrimitiveType::Emitter: return "Particle emitter";
        case PrimitiveType::SoundEmitter: return "Sound emitter";
        case PrimitiveType::PointLight: return "Point light";
        case PrimitiveType::SavePoint: return "Save point";
        case PrimitiveType::Empty: return "Empty";
        case PrimitiveType::Camera: return "Camera";
        case PrimitiveType::Mirror: return "Mirror";
        case PrimitiveType::Portal: return "Portal";
        case PrimitiveType::Area: return "Area";
        // The type is serialized as "scatter" (and the enum is Scatter), but
        // the UI says PROCEDURAL: the object is the region a whole graph works
        // in, and scattering is only one of the things that graph can do -
        // naming it after one source node is what made people expect the
        // object to choose the generation mode.
        case PrimitiveType::Scatter: return "Procedural volume";
        case PrimitiveType::Scroller: return "Scroller";
    }
    return "Object";
}

// Area reference picker (docs/areas.md): the scene's Area objects plus
// <none>. Used for a catch area (Mirror / Portal / feed Camera) and for a
// streaming layer's zone; a dangling name shows in red so a deleted area is
// obvious instead of silently catching nothing.
bool App::areaCombo(const char* label, std::string& ref) {
    bool changed = false;
    const bool dangling =
        !ref.empty() && !project::findArea(project_.objects(), ref);
    const std::string current = ref.empty() ? "<none>" : ref;
    if (dangling) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.3f, 1.0f));
    if (ImGui::BeginCombo(label, dangling ? (current + " (missing)").c_str()
                                          : current.c_str())) {
        if (ImGui::Selectable("<none>", ref.empty()) && !ref.empty()) {
            ref.clear();
            changed = true;
        }
        int areas = 0;
        for (const SceneObject& t : project_.objects()) {
            if (t.type != PrimitiveType::Area) continue;
            ++areas;
            if (ImGui::Selectable(t.name.c_str(), t.name == ref) && ref != t.name) {
                ref = t.name;
                changed = true;
            }
        }
        if (!areas)
            ImGui::TextDisabled("No areas in this scene -\nAdd object > Gameplay > Area.");
        ImGui::EndCombo();
    }
    if (dangling) ImGui::PopStyleColor();
    return changed;
}

// The whole catch-area block of a Mirror / Portal / feed Camera: the picker,
// the "update every frame" switch and the resolved counts. `verb` names what
// the caught objects get ("re-drawn", "shown", "in the feed") so the same
// widget reads right in all three panels.
bool App::catchAreaControls(SceneObject& o, const char* verb) {
    bool committed = false;
    if (areaCombo("Catch area", o.catchArea)) committed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Catch every object inside an Area's box (docs/areas.md)\n"
            "instead of listing them one by one. Resolved at build, so\n"
            "the geometry cost stays visible; the list below still adds\n"
            "objects from outside the area.");
    if (o.catchArea.empty()) return committed;
    if (ImGui::Checkbox("Update every frame", &o.catchAreaLive)) committed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Off: the volume is emptied into a fixed list at build - an\n"
            "object that walks in later stays out.\n"
            "On: objects that can MOVE are re-tested every frame, so they\n"
            "join and leave the list as they cross the boundary. Only\n"
            "movable objects pay for it (the count below); the immovable\n"
            "rest still resolves at build. Watch that count - each one\n"
            "inside is a second full submission of its geometry.");
    const std::vector<int> caught = project::areaCaughtObjects(
        project_.objects(), o.catchArea, selectedObject_);
    if (!o.catchAreaLive) {
        ImGui::TextDisabled("Area holds %d object%s (%s once each)",
                            (int)caught.size(), caught.size() == 1 ? "" : "s",
                            verb);
        return committed;
    }
    const std::set<std::string> refs =
        project::runtimeRefNames(project_, project_.objects());
    const std::vector<int> cands = project::areaLiveCandidates(
        project_.objects(), selectedObject_, refs);
    // Movable objects leave the fixed list even when they sit inside right
    // now - the per-frame test owns them, or they would be drawn twice.
    int fixed = 0, inside = 0;
    for (int ci : caught) {
        bool movable = false;
        for (int m : cands)
            if (m == ci) { movable = true; break; }
        if (movable) ++inside; else ++fixed;
    }
    ImGui::TextDisabled("%d fixed + %d of %d movable inside now (%s once each)",
                        fixed, inside, (int)cands.size(), verb);
    if (ImGui::IsItemHovered() && !cands.empty()) {
        std::string list;
        for (size_t i = 0; i < cands.size(); ++i)
            list += (i ? "\n" : "") + project_.objects()[cands[i]].name;
        ImGui::SetTooltip("Re-tested every frame:\n%s", list.c_str());
    }
    return committed;
}

// Edits the object selected in the Project panel / viewport. Only fields the
// game actually reads for the object's type are shown: markers (spawn point,
// player, emitters, lights) have no geometry in the game, so texture,
// rotation, physics and "usable" would be dead settings on them.
void App::drawPropertiesWindow() {
    ImGui::Begin("Properties");
    if (!hasProject_) {
        ImGui::TextDisabled("No project open.");
        ImGui::End();
        return;
    }
    if (selectedObject_ < 0 || selectedObject_ >= (int)project_.objects().size()) {
        ImGui::TextDisabled("No object selected.\nPick one in the Project panel or in "
                            "the viewport.");
        ImGui::End();
        return;
    }
    if (selection_.size() > 1) {
        drawMultiProperties();
        ImGui::End();
        return;
    }
    SceneObject& o = project_.objects()[selectedObject_];

    // Edits apply live; a history snapshot is committed once per finished
    // interaction (slider released, text field defocused...).
    bool committed = false;

    // Real geometry in the game: rendered, collidable, texturable.
    const bool isShape =
        o.type == PrimitiveType::Box || o.type == PrimitiveType::Sphere ||
        o.type == PrimitiveType::Cylinder || o.type == PrimitiveType::Cone ||
        o.type == PrimitiveType::Plane;
    const bool isSolid =
        isShape || o.type == PrimitiveType::Model || o.type == PrimitiveType::SavePoint;

    char nameBuf[128];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", o.name.c_str());
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) o.name = nameBuf;
    // Cutscene Director tracks and camera-shot bindings reference objects by
    // name - remap them when the rename edit ends so cutscenes don't go stale.
    if (ImGui::IsItemActivated()) {
        objRenameFrom_ = o.name;
        objRenameIdx_ = selectedObject_;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        committed = true;
        const std::string from =
            objRenameIdx_ == selectedObject_ ? objRenameFrom_ : std::string();
        objRenameIdx_ = -1;
        if (!from.empty() && from != o.name) {
            for (Sequence& s : project_.sequences) {
                for (SeqTrack& tr : s.tracks)
                    if (tr.target == from) tr.target = o.name;
                for (SeqCameraKey& k : s.cameraKeys)
                    if (k.camera == from) k.camera = o.name;
            }
            if (lookThroughCam_ == from) lookThroughCam_ = o.name;
            // Mirror target lists reference objects by name too.
            for (SceneObject& m : project_.objects())
                if (m.type == PrimitiveType::Mirror)
                    for (std::string& t : m.mirrorObjects)
                        if (t == from) t = o.name;
            // Scroller segment member lists reference objects by name.
            for (SceneObject& sc : project_.objects())
                if (sc.type == PrimitiveType::Scroller)
                    for (ScrollSegment& seg : sc.scrollSegments)
                        for (ScrollMember& t : seg.objects)
                            if (t.name == from) t.name = o.name;
            // Camera feed view lists + per-object texture-feed refs
            // ("camera:<name>" / "mirror:<name>").
            for (SceneObject& m : project_.objects()) {
                if (m.type == PrimitiveType::Camera)
                    for (std::string& t : m.camFeedObjects)
                        if (t == from) t = o.name;
                if (m.textureFeed == "camera:" + from)
                    m.textureFeed = "camera:" + o.name;
                else if (m.textureFeed == "mirror:" + from)
                    m.textureFeed = "mirror:" + o.name;
            }
            // Portal links + view lists likewise.
            for (SceneObject& m : project_.objects())
                if (m.type == PrimitiveType::Portal) {
                    if (m.portalTarget == from) m.portalTarget = o.name;
                    for (std::string& t : m.portalObjects)
                        if (t == from) t = o.name;
                }
            // Area references (docs/areas.md): catch areas, streaming-layer
            // zones and In Area nodes all point at an area by name.
            if (o.type == PrimitiveType::Area) {
                for (SceneObject& m : project_.objects()) {
                    if (m.catchArea == from) m.catchArea = o.name;
                    for (FlowNode& fn : m.flowGraph.nodes) {
                        const FlowNodeType* t = flowNodeType(fn.type);
                        if (t && t->strKind == FlowParamKind::AreaName &&
                            fn.str == from)
                            fn.str = o.name;
                    }
                }
                for (SceneLayer& l : project_.active().layers)
                    if (l.streamArea == from) l.streamArea = o.name;
            }
        }
    }

    // Provenance. Stated once, right under the name, because "where did this
    // come from" is the first thing asked about an object in a scene built out
    // of prefabs - and because the answer includes what it is NOT (a live link
    // back to the prefab).
    if (!o.prefabSource.empty()) {
        ImGui::TextDisabled("From prefab: %s", o.prefabSource.c_str());
        ImGui::SameLine();
        prefHelp(
            "This object was stamped from that prefab and is an ordinary, "
            "independent scene object now - editing the prefab later does not "
            "change it, and editing it does not change the prefab. The Project "
            "panel groups everything carrying this mark under one node.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Open in Prefabs")) {
            for (size_t pi = 0; pi < project_.prefabs.size(); ++pi)
                if (project_.prefabs[pi].name == o.prefabSource)
                    prefabSelected_ = (int)pi;
            showPrefabs_ = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Forget")) {
            // The escape hatch: a member reworked into something else should
            // stop claiming a lineage it no longer has.
            o.prefabSource.clear();
            committed = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip(
                "Drop the prefab mark from this object - it leaves the group in\n"
                "the Project panel and becomes a plain hand-authored object.");
    }

    // Streaming layer (Project panel > Layers). Shown as soon as the scene
    // has layers - or when the object still references a deleted-scene name.
    if (!project_.active().layers.empty() || !o.layer.empty()) {
        const std::string current = o.layer.empty() ? "<none>" : o.layer;
        if (ImGui::BeginCombo("Layer", current.c_str())) {
            if (ImGui::Selectable("<none>", o.layer.empty()) && !o.layer.empty()) {
                o.layer.clear();
                committed = true;
            }
            for (const SceneLayer& l : project_.active().layers) {
                if (ImGui::Selectable(l.name.c_str(), l.name == o.layer) &&
                    o.layer != l.name) {
                    o.layer = l.name;
                    committed = true;
                }
            }
            ImGui::EndCombo();
        }
    }

    if (isShape) {
        // Plane's enum value isn't contiguous with the other shapes, so map
        // combo indices through an explicit list instead of casting directly.
        static const PrimitiveType kShapeTypes[] = {
            PrimitiveType::Box, PrimitiveType::Sphere, PrimitiveType::Cylinder,
            PrimitiveType::Cone, PrimitiveType::Plane};
        const char* typeNames[] = {"Box", "Sphere", "Cylinder", "Cone", "Plane"};
        int typeIdx = 0;
        for (int i = 0; i < IM_ARRAYSIZE(kShapeTypes); ++i)
            if (kShapeTypes[i] == o.type) typeIdx = i;
        if (ImGui::Combo("Type", &typeIdx, typeNames, IM_ARRAYSIZE(typeNames))) {
            o.type = kShapeTypes[typeIdx];
            // Detail means different things (segments vs box subdivisions) and
            // has different ranges per shape - re-fit the value to the new one.
            o.primDetail = clampPrimDetail(o.type, o.primDetail);
            committed = true;
        }
    } else {
        ImGui::Text("Type:");
        ImGui::SameLine();
        ImGui::TextUnformatted(typeLabel(o.type));
    }
    // Geometry primitives: how many segments (curved) or edge subdivisions
    // (box-like) the mesh is built from. Editable any time, updates live.
    if (o.type == PrimitiveType::Box || o.type == PrimitiveType::Sphere ||
        o.type == PrimitiveType::Cylinder || o.type == PrimitiveType::Cone ||
        o.type == PrimitiveType::SavePoint) {
        const bool box = primDetailIsBoxLike(o.type);
        int detail = o.primDetail;
        if (ImGui::DragInt("Detail", &detail, 0.2f, primDetailMin(o.type),
                           primDetailMax(o.type), box ? "%d subdivisions"
                                                      : "%d segments"))
            o.primDetail = clampPrimDetail(o.type, detail);
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SameLine();
        ImGui::TextDisabled("(%d tris)", primTriangleCount(o.type, o.primDetail));
    }
    if (o.type == PrimitiveType::Model) {
        // model file: pick among the project's res/models assets
        const std::string current = o.modelPath.empty()
                                        ? "<none>"
                                        : std::filesystem::path(o.modelPath)
                                              .filename()
                                              .string();
        if (ImGui::BeginCombo("Model", current.c_str())) {
            const std::vector<std::string> models = listAssetFiles("models", ".obj");
            for (const std::string& m : models) {
                const std::string rel = "res/models/" + m;
                if (ImGui::Selectable(m.c_str(), rel == o.modelPath) &&
                    rel != o.modelPath) {
                    o.modelPath = rel;
                    committed = true;
                }
            }
            const std::vector<std::string> anim = listAnimatedModelFiles();
            for (const std::string& m : anim) {
                const std::string rel = "res/models/" + m;
                if (ImGui::Selectable((m + " (animated)").c_str(),
                                      rel == o.modelPath) &&
                    rel != o.modelPath) {
                    o.modelPath = rel;
                    o.animClip.clear();  // clip names belong to the old file
                    committed = true;
                }
            }
            if (models.empty() && anim.empty())
                ImGui::TextDisabled("No models - Import one in Project > Assets.");
            ImGui::EndCombo();
        }
        if (isAnimatedModelPath(o.modelPath)) {
            const GlbInfo& info = glbInfo(o.modelPath);
            if (info.ok) {
                ImGui::TextDisabled("%d verts, %d baked frames, %d clip(s)",
                                    info.vertexCount, info.frameCount,
                                    (int)info.clips.size());
                for (const std::string& w : info.warnings)
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%s",
                                       w.c_str());
                // (The model's built-in materials aren't listed here: they're
                // authored in the modelling tool and the Material picker below
                // is how you override/edit them - see drawMaterialCombo.)
                ImGui::SeparatorText("Animation");
                // Clip references store EFFECTIVE names (the Animation
                // Editor's renames), which is what the .tskl carries and the
                // game resolves against.
                const std::vector<std::string> clips =
                    effectiveClips(o.modelPath);
                const std::string clipLabel =
                    o.animClip.empty()
                        ? (clips.empty() ? "<none>" : clips.front() + " (first)")
                        : o.animClip;
                if (ImGui::BeginCombo("Start clip", clipLabel.c_str())) {
                    for (const std::string& c : clips) {
                        const bool selected =
                            c == o.animClip ||
                            (o.animClip.empty() && c == clips.front());
                        if (ImGui::Selectable(c.c_str(), selected) &&
                            o.animClip != c) {
                            o.animClip = c;
                            // Seed Loop from the clip's "Loop by default"
                            // (Tools > Animation Editor) - picking a one-shot
                            // like a door opening should not silently loop.
                            if (const AnimClipEdit* e = animedit::findEdit(
                                    project_, o.modelPath,
                                    animedit::sourceName(project_, o.modelPath,
                                                         c)))
                                o.animLoop = e->loop;
                            committed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::Checkbox("Autoplay at scene start", &o.animAutoplay))
                    committed = true;
                if (ImGui::Checkbox("Loop", &o.animLoop)) committed = true;
                ImGui::DragFloat("Speed", &o.animSpeed, 0.02f, 0.05f, 10.0f,
                                 "%.2fx");
                committed |= ImGui::IsItemDeactivatedAfterEdit();
                committed |= drawLodOverrides(o);
                ImGui::TextDisabled(
                    "Scripts/flow graph: Play Animation, Stop Animation,\n"
                    "On Animation Finished.");
            } else if (!o.modelPath.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                                   "Unusable model: %s", info.error.c_str());
            }
        } else {
        // materials come from the .obj's MTL file (or the assigned override)
        // - read-only summary
        const ModelInfo& info = modelInfo(o.modelPath, o.materialPath);
        if (info.ok) {
            // Both vertex counts: the one the PS2 pays for and the one the
            // modelling tool showed you. Animated models report theirs a few
            // lines up, so static ones saying only "triangles" was the odd
            // one out.
            if (info.positions && info.positions * 3 != info.verts)
                ImGui::TextDisabled("%d triangles, %d vertices (%d unique "
                                    "positions)",
                                    info.tris, info.verts, info.positions);
            else
                ImGui::TextDisabled("%d triangles, %d vertices", info.tris,
                                    info.verts);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Vertices are what reaches VU1: three per triangle, with a "
                    "corner split\nwherever its normal, UV or material "
                    "differs - so this is above the\nmodelling tool's count, "
                    "and it is the number the pipeline cuts into\nVU1-sized "
                    "chunks (Debugger > Stats says how big those are).");
            ImGui::TextDisabled("materials (from .mtl):");
            for (const ModelInfo::MaterialLine& m : info.materials) {
                if (m.missing)
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                                       "  - %s - MISSING", m.text.c_str());
                else
                    ImGui::TextDisabled("  - %s", m.text.c_str());
            }
            if (info.anyMissing)
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                                   "Missing textures render as plain color - put the\n"
                                   "files next to the .obj (paths are relative to it).");
        } else if (!o.modelPath.empty()) {
            ImGui::TextDisabled("Model file missing/unparseable - renders as a box.");
        }
        // Static models take distance mesh LOD too (docs/model-pipeline.md),
        // so they get the same per-object override row animated models have.
        committed |= drawLodOverrides(o, false);
        }
    }

    // Empties are pure transforms - scripts read the whole transform (and the
    // color, as a free per-object parameter), so every field stays editable.
    const bool isEmpty = o.type == PrimitiveType::Empty;
    // Decal: a textured quad. Transform + color + material stay editable, but
    // it carries no physics/collision/usable game state (pure visual overlay).
    const bool isDecal = o.type == PrimitiveType::Decal;
    // Camera entity: position + rotation aim the shot, color tints the marker.
    const bool isCamera = o.type == PrimitiveType::Camera;
    // Mirror: transform places the glass rectangle (+Z = the reflective
    // face), color tints it; the mirror-specific block sits further down.
    const bool isMirror = o.type == PrimitiveType::Mirror;
    // Portal: transform places the surface (+Z = the visible/entry face),
    // color tints an inactive surface; the portal block sits further down.
    const bool isPortal = o.type == PrimitiveType::Portal;
    // Area: the transform IS the volume (scale = the box size), color tints
    // its wireframe. Nothing else applies - it has no geometry in the game.
    const bool isArea = o.type == PrimitiveType::Area;
    // Scatter volume: position/scale ARE the region its graph works in, and
    // the Y rotation yaws the footprint. Everything else about it lives in the
    // graph (Tools > Procedural), so no game-state fields apply.
    const bool isScatter = o.type == PrimitiveType::Scatter;
    // Scroller: position is the belt origin, rotation aims the belt axis
    // (local +Z); the scroller-specific block (segments, speed) sits below.
    // Scale/color are the marker's own - it has no geometry in the game.
    const bool isScroller = o.type == PrimitiveType::Scroller;
    if (isScatter) {
        ImGui::TextDisabled(
            "Procedural region: position and scale are the box the graph fills.");
        if (ImGui::Button("Open the graph")) {
            showProcedural_ = true;
            procVolume_ = selectedObject_;
            procVolumeId_ = o.id;
            procPositionsApplied_ = false;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%d nodes, seed %u", (int)o.procGraph.nodes.size(),
                            (unsigned)o.procGraph.seed);
    }

    ImGui::DragFloat3("Position", o.position, 0.1f);
    committed |= ImGui::IsItemDeactivatedAfterEdit();
    // custom emitters rotate too - the rotation aims the emission direction
    if (isSolid || isEmpty || isDecal || isCamera || isMirror || isPortal || isArea ||
        isScatter || isScroller ||
        (o.type == PrimitiveType::Emitter && o.emitterKind == 5)) {
        ImGui::DragFloat3("Rotation", o.rotation, 1.0f, -360.0f, 360.0f, "%.0f deg");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
    }
    if (isSolid || isEmpty || isDecal || isMirror || isPortal || isArea ||
        isScatter || o.type == PrimitiveType::Emitter) {
        ImGui::DragFloat3(isArea ? "Size" : "Scale", o.scale, 0.05f, 0.01f, 1000.0f);
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        // How big that actually is. A primitive is a UNIT shape, so its scale
        // is its size in world units - and the world scale turns that into
        // something you can picture (docs/world-scale.md).
        float size[3];
        if (objectWorldSize(o, size)) {
            const float ups = project_.settings.unitsPerMeter;
            ImGui::TextDisabled("Size: %.2f x %.2f x %.2f units  =  "
                                "%.2f x %.2f x %.2f m",
                                size[0], size[1], size[2], size[0] / ups,
                                size[1] / ups, size[2] / ups);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "The object's real size, from its unit shape (or the\n"
                    "model's own bounds) times the scale above. Metres come\n"
                    "from Preferences > World > Units per meter (now %.3f).\n"
                    "The Measure tool (7) does the same between two points.",
                    ups);
        }
    }
    // Color: mesh tint for solids, particle tint for emitters, light color
    // for point lights, marker tint + free per-object parameter for empties,
    // texture tint for decals, marker/frustum tint for camera entities, glass
    // tint for mirrors, inactive-surface tint for portals. The remaining
    // markers draw in fixed colors.
    if (isSolid || isEmpty || isDecal || isCamera || isMirror || isPortal || isArea ||
        o.type == PrimitiveType::Emitter || o.type == PrimitiveType::PointLight) {
        ImGui::ColorEdit3("Color", o.color);
        committed |= ImGui::IsItemDeactivatedAfterEdit();
    }

    const bool animatedModel =
        o.type == PrimitiveType::Model && isAnimatedModelPath(o.modelPath);
    // Material picker: solids texture their surface with it; a decal uses its
    // map_Kd (with alpha) as the decal image.
    if (isSolid || isDecal) {
        // Material (.mtl asset): primitives take the file's first material
        // (Kd + map_Kd on their UVs, modulated by the object color), models
        // (static .obj AND animated .glb/.fbx) use it as an OVERRIDE replacing
        // their own libraries - usemtl/material names resolve against it. Empty
        // = the model's own (built-in) materials, so it is an extra option, not
        // a replacement. An animated override is resolved into the .tskl at
        // build time (docs/animated-models.md).
        if (drawMaterialCombo(o)) committed = true;
        if (!o.materialPath.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Edit..."))
                // preview the material on the object's own mesh (static .obj
                // AND animated .glb/.fbx); primitives leave the hint off
                openMaterialEditor(o.materialPath,
                                   o.type == PrimitiveType::Model ? o.modelPath
                                                                  : "");
        }
        if (!o.materialPath.empty() && o.type != PrimitiveType::Model) {
            const ModelInfo& mat = materialInfo(o.materialPath);
            if (mat.ok && !mat.materials.empty()) {
                const ModelInfo::MaterialLine& line = mat.materials.front();
                if (line.missing)
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                                       "%s - texture MISSING", line.text.c_str());
                else
                    ImGui::TextDisabled("%s", line.text.c_str());
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                                   "Material file missing/empty - plain color.");
            }
        }
        if (isDecal) {
            ImGui::TextDisabled(
                "Assign a material whose map_Kd PNG has transparency.\n"
                "Sits just in front of its origin; place it on a surface.");
            if (ImGui::Checkbox("Project onto surfaces", &o.decalProject))
                committed = true;
            if (o.decalProject)
                ImGui::TextDisabled(
                    "Wraps onto terrain + overlapping objects instead of a flat\n"
                    "quad. Scale = projection box: X/Y footprint, Z depth into the\n"
                    "surface; aim +Z at the wall/floor. Baked at build (no PS2 cost).");
        }
    }
    if (isSolid) {
        if (ImGui::Checkbox("Physics (rigid body)", &o.physics)) committed = true;
        if (o.physics) {
            ImGui::Indent();
            ImGui::DragFloat("Mass", &o.physMass, 0.05f, 0.05f, 100.0f, "%.2f");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::DragFloat("Bounciness", &o.physBounce, 0.01f, 0.0f, 1.0f, "%.2f");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::DragFloat("Friction", &o.physFriction, 0.01f, 0.0f, 1.0f, "%.2f");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ImGui::Checkbox("Tumble (impacts add spin)", &o.physTumble))
                committed = true;
            ImGui::DragFloat("Sleep after (s)", &o.physSleep, 0.1f, 0.1f, 60.0f,
                             "%.1f");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::TextDisabled(
                "Falls, bounces off slopes and objects, slides with friction\n"
                "and can be shoved by the player / Apply Impulse nodes.\n"
                "Mass is relative - it matters only against other bodies.\n"
                "Sleep after: seconds of near-rest before the body freezes\n"
                "(a sleeping body costs nothing until something wakes it).");
            ImGui::Unindent();
        }
        if (o.type == PrimitiveType::SavePoint) {
            ImGui::TextDisabled("Always usable - USE opens the save menu.");
        } else if (ImGui::Checkbox("Usable (USE prompt + On Used trigger)", &o.usable)) {
            committed = true;
        }
        if (o.type != PrimitiveType::SavePoint) {
            if (ImGui::Checkbox("Pickable (USE picks it up)", &o.pickable))
                committed = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "The player carries it a short reach in front of the face,\n"
                    "swept against the world - it cannot be pushed through or\n"
                    "left behind other geometry. USE again drops it.");
            if (o.pickable) {
                ImGui::SameLine();
                if (ImGui::Checkbox("Can throw", &o.pickThrow)) committed = true;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("BTN_THROW (Circle, see controls.hpp) launches\n"
                                      "the carried object. Experimental.");
                if (!o.physics)
                    ImGui::TextDisabled(
                        "Tip: enable Physics so it falls when dropped.");
            }
        }
    }
    // Show/Hide Object nodes toggle emitters/sounds too - their on/off state
    // is worth saving; empties can be moved around by scripts. Lights are
    // baked at build, the remaining markers have no game state.
    if (isSolid || isEmpty || o.type == PrimitiveType::Emitter ||
        o.type == PrimitiveType::SoundEmitter) {
        if (ImGui::Checkbox("Save state (position/color/visibility in saves)",
                            &o.saveState))
            committed = true;
    }

    // Player collision. Solid geometry only - markers/emitters never collide.
    if (isSolid) {
        if (animatedModel) {
            // mesh collision is a static-model feature; animated models
            // collide as their baked all-clips AABB or not at all
            bool solid = o.collisionMode != 2;
            if (ImGui::Checkbox("Collision (blocks the player, animation AABB)",
                                &solid)) {
                o.collisionMode = solid ? 0 : 2;
                committed = true;
            }
        } else if (o.type == PrimitiveType::Model) {
            const char* modes[] = {"Box (mesh AABB)", "Mesh (walkable triangles)",
                                   "None"};
            if (ImGui::Combo("Collision", &o.collisionMode, modes, 3)) committed = true;
            if (o.collisionMode == 1)
                ImGui::TextDisabled("Player walks the model's surface (ramps, stairs).");
        } else {
            // primitives collide as their scale box or not at all
            bool solid = o.collisionMode != 2;
            if (ImGui::Checkbox("Collision (blocks the player)", &solid)) {
                o.collisionMode = solid ? 0 : 2;
                committed = true;
            }
        }
    }

    // The four numbers this mesh hands the project's own VU1 microprogram.
    // Shown ONLY when the project has such a program: otherwise they are four
    // sliders that do nothing, on every object, forever. Which stage reads
    // which slot is a property of the program, so the labels come from the
    // stage list rather than being named here.
    if (isSolid && !project_.vu.programs.empty()) {
        ImGui::SeparatorText("VU program");
        // A program is installed over a material CLASS, so the only thing that
        // decides whether it touches this object is which class the object is
        // in. Labelling the four slots from every program in the project - as
        // this did at first - is worse than saying nothing: an untextured box
        // would show "Scroll UV Speed U" on its X slot, from a program that
        // will never draw it.
        const unsigned cls = project::vuClassOfObject(project_, o);
        const VuProgram* mine = nullptr;
        for (const VuProgram& pr : project_.vu.programs)
            if (pr.enabled && (pr.classes & cls) != 0) { mine = &pr; break; }

        ImGui::Text("Class: %s%s%s", project::vuClassName(cls),
                    mine ? "   look: " : "", mine ? mine->name.c_str() : "");
        prefHelp(
            "Which VU1 microprogram draws this object, decided by what it\n"
            "carries: a texture puts it in Textured, a material with a refl\n"
            "map in Reflective, Dynamic lighting in one of the lit classes.\n"
            "A program is installed over a CLASS, so it reaches this object\n"
            "only if it was built on this one.");

        if (!mine) {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::semantics().warn);
            ImGui::TextWrapped(
                "No look covers this class, so these numbers do nothing here.");
            ImGui::PopStyleColor();
            const bool set = o.vuParams[0] || o.vuParams[1] || o.vuParams[2] ||
                             o.vuParams[3];
            if (set)
                ImGui::TextDisabled(
                    "It carries values anyway - either tick %s on a look in\n"
                    "Tools > VU Programs, or clear them.",
                    project::vuClassName(cls));
            ImGui::TextDisabled(
                "Objects merged into one static batch share a bag, and\n"
                "therefore share these numbers.");
        } else {
            std::string uses[4];
            for (const VuStage& st : mine->stages) {
                const vugen::StageDef* def = vugen::stageDef(st.kind);
                if (!def || !st.enabled) continue;
                for (int i = 0; i < def->paramCount; ++i)
                    if (st.bind[i] >= 0 && st.bind[i] < 4) {
                        std::string& u = uses[st.bind[i]];
                        if (!u.empty()) u += ", ";
                        u += std::string(def->title) + " " + def->params[i].name;
                    }
            }
            static const char* kAxis[4] = {"X", "Y", "Z", "W"};
            for (int i = 0; i < 4; ++i) {
                ImGui::PushID(i);
                const std::string label =
                    uses[i].empty()
                        ? std::string(kAxis[i]) + " (nothing reads this)"
                        : uses[i] + "##vu" + kAxis[i];
                ImGui::BeginDisabled(uses[i].empty());
                ImGui::DragFloat(label.c_str(), &o.vuParams[i], 0.01f);
                committed |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            // An object is drawn by several bags and they are NOT all in the
            // same material class: a baked lightmap pass carries the AO atlas,
            // so it is a TEXTURED bag even on an untextured mesh. Displace the
            // main bag and not that one and the lightmap stays behind as a
            // translucent ghost of the undeformed shape - which is exactly what
            // it looked like on the console before anyone worked out why.
            // A baked lightmap on a mesh that moves is wrong anyway: it was
            // baked for a shape the mesh no longer has.
            std::vector<vugen::Stage> probe;
            for (const VuStage& st : mine->stages) {
                vugen::Stage g = vugen::makeStage(st.kind);
                g.enabled = st.enabled;
                for (int i = 0; i < 4; ++i) {
                    g.params[i].value = st.params[i];
                    g.params[i].meshSlot = st.bind[i];
                }
                probe.push_back(g);
            }
            const unsigned texCls = 1u << 3;
            bool texCovered = false;
            for (const VuProgram& pr : project_.vu.programs)
                if (pr.enabled && (pr.classes & texCls) && &pr == mine)
                    texCovered = true;
            if (o.bakedLighting && vugen::stagesMoveGeometry(probe) &&
                !texCovered && cls != texCls) {
                ImGui::PushStyleColor(ImGuiCol_Text, theme::semantics().warn);
                ImGui::TextWrapped(
                    "This look MOVES the geometry, and this object has a baked "
                    "lightmap. That pass carries the AO atlas, so it is a "
                    "Textured bag - a different class, drawn by a different "
                    "program - and it will stay behind as a ghost of the "
                    "undeformed shape. Turn Baked lighting off here (a lightmap "
                    "baked for a shape the mesh no longer has is wrong anyway), "
                    "or give the look the Textured class too.");
                ImGui::PopStyleColor();
            }
            ImGui::TextDisabled(
                "All zero = this mesh renders exactly as it would with no\n"
                "custom program at all. Objects merged into one static batch\n"
                "share a bag, and therefore share these numbers.");
        }
    }

    // Rendering cut-off - the cheapest LOD. Only drawing stops beyond the
    // distance; collision, sounds and scripts keep running. For a mirror it
    // gates the glass AND every reflected copy - the whole illusion.
    if (isSolid || isMirror) {
        ImGui::DragFloat("Draw distance", &o.drawDistance, 0.5f, 0.0f, 2000.0f,
                         o.drawDistance > 0.0f ? "%.0f units" : "unlimited");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        if (o.drawDistance > 0.0f)
            ImGui::TextDisabled(
                "Skipped at draw time when the camera is farther than this;\n"
                "collision and logic still run. 0 = always drawn.");

        // Rendered into the dynamic ("@sky") environment map, so reflective
        // materials mirror this object - costs a second small render per frame.
        if (ImGui::Checkbox("Show in reflections", &o.reflected)) committed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Materials with a <dynamic - live sky> sphere map will mirror\n"
                "this object (rendered into the reflection map every frame).\n"
                "Mark the few props that sell the effect - each one costs a\n"
                "second small render per frame. Editor preview shows the sky\n"
                "only; check reflections in the game.");

        // Real-shape projected shadow - the RUNTIME one, distinct from the
        // baked ambient-occlusion "Cast shadow" below: a silhouette
        // rendered from the sun into a small VRAM target and projected onto
        // the terrain. The caster pays a second render, hence opt-in.
        if (ImGui::Checkbox("Projected shadow (live)", &o.projShadow))
            committed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Real silhouette shadow on the terrain: the object renders a\n"
                "second time each frame (64x64, from the sun) and the shape\n"
                "is projected under it. The 4 casters nearest the camera are\n"
                "active at a time - mark hero objects, not everything.\n"
                "Follows animation and movement; game-only (no preview).\n"
                "'Cast shadow' below is the baked, static one.");
        // Baked ambient occlusion: whether this object darkens nearby
        // terrain/objects (docs/ambient-occlusion.md; global strength in
        // the Ambience Editor).
        if (ImGui::Checkbox("Cast shadow", &o.castShadow)) committed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Bakes a soft contact shadow onto nearby terrain and objects\n"
                "(ambient occlusion - needs it enabled in the scene's\n"
                "ambience preset). Off = this object casts nothing; it still\n"
                "receives shadows from others. Rebuild to see it in-game.");
        // Baked global illumination: may this object take a per-texel
        // lightmap, which glues the result to its surface?
        if (ImGui::Checkbox("Baked lighting", &o.bakedLighting))
            committed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Lets baked global illumination paint this object's light per\n"
                "pixel - the best-looking route, and one that GLUES the result\n"
                "to the surface: tip the object over at runtime and it carries\n"
                "a shadow that matches nothing.\n"
                "Off = it reads the light probes instead and relights when it\n"
                "moves. The bake already excludes anything it can prove moves\n"
                "(physics, pickable, usable, save-state, streamed, or moved by\n"
                "a flow graph) - this is for the rest.");
    }

    if (isArea) {
        ImGui::SeparatorText("Area");
        ImGui::TextDisabled(
            "An invisible box: this wireframe is its whole appearance and the\n"
            "game draws nothing. Nobody collides with it. Point things at it\n"
            "by name instead of typing distances:");
        ImGui::BulletText("A streaming layer's zone (Project panel > Layers)");
        ImGui::BulletText("A mirror / portal / camera feed's target list");
        ImGui::BulletText("The In Area flow trigger (Triggers > In Area)");
        ImGui::BulletText("A reverb room for the sound effects (below)");
        // What references it, so deleting/resizing one is not a guess.
        std::vector<std::string> users;
        for (const SceneObject& t : project_.objects())
            if (t.catchArea == o.name)
                users.push_back(t.name + (t.catchAreaLive ? " (live)" : ""));
        for (const SceneLayer& l : project_.active().layers)
            if (l.streamArea == o.name) users.push_back("layer " + l.name);
        for (const SceneObject& t : project_.objects())
            for (const FlowNode& n : t.flowGraph.nodes)
                if (n.type == "InArea" && n.str == o.name)
                    users.push_back(t.name + " (In Area)");
        if (users.empty()) {
            ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.3f, 1.0f),
                               "Nothing references this area yet.");
        } else {
            std::string list;
            for (size_t i = 0; i < users.size(); ++i)
                list += (i ? ", " : "") + users[i];
            ImGui::TextDisabled("Used by: %s", list.c_str());
        }
        // Catch preview: the same call codegen bakes with.
        const std::vector<int> caught =
            project::areaCaughtObjects(project_.objects(), o.name, -1);
        ImGui::TextDisabled("Catches %d object%s as a target list",
                            (int)caught.size(), caught.size() == 1 ? "" : "s");
        if (ImGui::IsItemHovered() && !caught.empty()) {
            std::string list;
            for (size_t i = 0; i < caught.size(); ++i)
                list += (i ? "\n" : "") + project_.objects()[caught[i]].name;
            ImGui::SetTooltip("%s", list.c_str());
        }

        ImGui::SeparatorText("Reverb zone");
        if (ImGui::Checkbox("This area is a room for the sound effects",
                            &o.reverbZone))
            committed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Sound effects played while the player stands inside this box\n"
                "go through the SPU2's hardware reverb unit. Costs no EE time -\n"
                "the sound chip does the mixing. Music stays dry.");
        if (o.reverbZone) {
            {
                const std::vector<ReverbPresetInfo>& presets = reverbPresets();
                if (o.reverbPreset < 0 || o.reverbPreset >= (int)presets.size())
                    o.reverbPreset = 1;
                if (ImGui::BeginCombo("Preset",
                                      presets[o.reverbPreset].label)) {
                    for (int i = 0; i < (int)presets.size(); ++i) {
                        if (ImGui::Selectable(presets[i].label,
                                              i == o.reverbPreset)) {
                            o.reverbPreset = i;
                            committed = true;
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("%s", presets[i].desc);
                    }
                    ImGui::EndCombo();
                }
            }
            ImGui::SliderFloat("Amount", &o.reverbAmount, 0.0f, 1.0f, "%.2f");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "How wet the effects get, 0..1. This is the one value that\n"
                    "moves smoothly - entering and leaving the box ramps it, so\n"
                    "two overlapping zones sharing a preset cross-fade.");
            if (reverbUsesEcho(o.reverbPreset)) {
                ImGui::SliderInt("Delay", &o.reverbDelay, 0, 127);
                committed |= ImGui::IsItemDeactivatedAfterEdit();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Time between repeats. Echo/Delay only.");
                ImGui::SliderInt("Feedback", &o.reverbFeedback, 0, 127);
                committed |= ImGui::IsItemDeactivatedAfterEdit();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "How much of each repeat feeds the next - how many times\n"
                        "it echoes before dying. Echo/Delay only.");
            } else {
                ImGui::TextDisabled(
                    "Delay and feedback apply to the Echo and Delay presets\n"
                    "only - the room presets have their own fixed geometry.");
            }
            ImGui::DragInt("Priority", &o.reverbPriority, 0.1f, -100, 100);
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Overlapping zones do not mix - the console has ONE reverb\n"
                    "unit. The highest priority inside wins, so a closet placed\n"
                    "inside a hall needs a higher number than the hall.");

            // The console has TWO reverb units and the game cross-fades
            // between them, so mixing presets is no longer a warning - but
            // only two rooms can be live at once, which is worth saying where
            // an author is stacking them.
            int zones = 0;
            for (const SceneObject& t : project_.objects())
                if (t.type == PrimitiveType::Area && t.reverbZone) ++zones;
            ImGui::TextDisabled("%d reverb zone%s in this scene", zones,
                                zones == 1 ? "" : "s");
            if (zones > 1)
                ImGui::TextDisabled(
                    "Crossing into another zone cross-fades, whatever presets\n"
                    "the two use - the console has two reverb units and the\n"
                    "game hands the incoming room the free one. Only TWO can\n"
                    "be live at once, so a third room entered mid-fade waits\n"
                    "for the first to finish leaving.");
        }
    }

    if (isMirror) {
        ImGui::SeparatorText("Mirror");
        ImGui::TextDisabled(
            "Re-draws the listed objects mirrored across this rectangle\n"
            "(+Z face). The copies are real geometry behind the plane -\n"
            "build the mirror into a wall so only the glass shows them.");
        ImGui::SliderFloat("Glass opacity", &o.mirrorOpacity, 0.0f, 1.0f, "%.2f");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::Checkbox("Reflect player", &o.mirrorReflectPlayer)) committed = true;
        if (o.mirrorReflectPlayer)
            ImGui::TextDisabled(
                "Reflects the third-person avatar. An FPP player has no\n"
                "body to reflect (vampire rules).");
        if (ImGui::Checkbox("Raytraced (VU0, experimental)", &o.mirrorRaytraced))
            committed = true;
        if (o.mirrorRaytraced) {
            ImGui::TextDisabled(
                "Real per-pixel ray tracing on a VU0 microprogram: listed\n"
                "static models reflect as decimated TEXTURED triangle meshes\n"
                "(up to 2, 36 tris shared), boxes/planes/decals as\n"
                "axis-aligned slabs, everything else as spheres - against\n"
                "the sky gradient, traced into a texture on the glass.");
            // Cost scales with the square of the edge (VU0 traces every
            // texel) - 256/512 are photo modes, not frame rates.
            const char* rtSizes[] = {"32 x 32 (cheap)", "64 x 64",
                                     "128 x 128 (~4x cost)",
                                     "256 x 256 (slideshow)",
                                     "512 x 512 (photo mode, 1MB VRAM)"};
            const int rtVals[] = {32, 64, 128, 256, 512};
            int rtIdx = 1;
            for (int i = 0; i < 5; ++i)
                if (o.mirrorRtSize == rtVals[i]) { rtIdx = i; break; }
            ImGui::SetNextItemWidth(scaled(210));
            if (ImGui::Combo("Reflection resolution", &rtIdx, rtSizes, 5)) {
                o.mirrorRtSize = rtVals[rtIdx];
                committed = true;
            }
        }
        bool solid = o.collisionMode != 2;
        if (ImGui::Checkbox("Collision (blocks the player)", &solid)) {
            o.collisionMode = solid ? 0 : 2;
            committed = true;
        }
        // Area instead of (or on top of) the hand-built list: everything the
        // volume holds reflects. The count is what the game re-draws - fixed
        // at build unless "Update every frame" is on.
        if (catchAreaControls(o, "re-drawn")) committed = true;
        ImGui::TextUnformatted("Reflected objects:");
        int removeAt = -1;
        for (size_t i = 0; i < o.mirrorObjects.size(); ++i) {
            ImGui::PushID((int)i);
            if (ImGui::SmallButton("x")) removeAt = (int)i;
            ImGui::SameLine();
            bool exists = false;
            for (const SceneObject& t : project_.objects())
                if (t.name == o.mirrorObjects[i]) { exists = true; break; }
            if (exists)
                ImGui::TextUnformatted(o.mirrorObjects[i].c_str());
            else
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                                   "%s (missing)", o.mirrorObjects[i].c_str());
            ImGui::PopID();
        }
        if (removeAt >= 0) {
            o.mirrorObjects.erase(o.mirrorObjects.begin() + removeAt);
            committed = true;
        }
        if (ImGui::BeginCombo("##mirrorAdd", "+ Add object...")) {
            for (const SceneObject& t : project_.objects()) {
                // only types the game draws as static/animated geometry can
                // show up in the glass; the player has its own checkbox
                const bool reflectable =
                    t.type == PrimitiveType::Box || t.type == PrimitiveType::Sphere ||
                    t.type == PrimitiveType::Cylinder ||
                    t.type == PrimitiveType::Cone || t.type == PrimitiveType::Plane ||
                    t.type == PrimitiveType::SavePoint ||
                    t.type == PrimitiveType::Model || t.type == PrimitiveType::Decal;
                if (!reflectable || t.name == o.name) continue;
                bool listed = false;
                for (const std::string& n : o.mirrorObjects)
                    if (n == t.name) { listed = true; break; }
                if (listed) continue;
                if (ImGui::Selectable(t.name.c_str())) {
                    o.mirrorObjects.push_back(t.name);
                    committed = true;
                }
            }
            ImGui::EndCombo();
        }
        if (o.mirrorObjects.empty() && !o.mirrorReflectPlayer)
            ImGui::TextDisabled("Nothing listed - the mirror shows only glass.");
    }

    if (isScroller) {
        const SceneData& scene = project_.active();
        ImGui::SeparatorText("Endless scroller");
        ImGui::TextDisabled(
            "Tiles named segments of scene objects forever along the belt\n"
            "axis (this object's local +Z - use Rotation to aim it). The\n"
            "segment members are templates: they are hidden in-game and\n"
            "cloned down the belt. Great for tunnels, roads, terrain strips.");

        ImGui::DragFloat("Speed (units/s)", &o.scrollSpeed, 0.1f, -200.0f, 200.0f,
                         "%.1f");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetItemTooltip("Belt speed along +Z. Negative reverses the flow.");
        ImGui::DragFloat("Populate ahead", &o.scrollAhead, 0.5f, 0.0f, 2000.0f,
                         "%.1f");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat("Keep behind", &o.scrollBehind, 0.5f, 0.0f, 2000.0f,
                         "%.1f");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::Checkbox("Run at start", &o.scrollAutostart)) committed = true;
        ImGui::SetItemTooltip(
            "Off = the belt is frozen until a Start Scroller flow node runs.");
        ImGui::DragInt("Max clones", &o.scrollMaxClones, 1.0f, 1, 2000);
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetItemTooltip(
            "Safety cap on baked clone objects. Past it the belt recycles\n"
            "fewer copies (a visible gap may appear).");
        ImGui::DragFloat("Seam overlap", &o.scrollOverlap, 0.005f, 0.0f, 1.0f,
                         "%.3f");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetItemTooltip(
            "Each clone is stretched this much along the belt so consecutive\n"
            "pieces interpenetrate slightly - exactly-coplanar end faces\n"
            "z-fight (flickering seams). 0 = exact tiling.");
        ImGui::DragInt("Variation seed", &o.scrollVarySeed, 1.0f, 0, 1000000);
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetItemTooltip(
            "Seeds the per-cell variation set up on each segment member below\n"
            "(Appears in / Variant group / the jitters). Changing it deals a\n"
            "different infinite level from the same pieces.");
        // THIS belt's ghosts only - a checkbox inside one scroller's properties
        // that silenced every scroller in the scene reads as a bug. View >
        // Scroller preview is the all-at-once switch, and it wins while it is
        // off. Editor-only and deliberately NOT part of the object: no
        // commitChange(), because hiding a preview is not an edit to the scene.
        bool beltGhosts = !scrollGhostsOff_.count(o.id);
        ImGui::BeginDisabled(!showScrollerPreview_);
        if (ImGui::Checkbox("Show belt preview", &beltGhosts)) {
            if (beltGhosts) scrollGhostsOff_.erase(o.id);
            else scrollGhostsOff_.insert(o.id);
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip(
            showScrollerPreview_
                ? "Draw THIS belt's sliding ghost copies in the viewport. Off\n"
                  "leaves the belt marker and every readout below alone - it\n"
                  "only stops the copies from covering the member objects you\n"
                  "are editing. An editor setting, not project data.\n"
                  "View > Scroller preview does the same for every belt."
                : "View > Scroller preview is off, which already hides every\n"
                  "belt. Turn it back on to hide belts one at a time.");

        // Cost readout: how many clone objects this belt bakes into the scene.
        if (!o.scrollSegments.empty()) {
            const int clones = scrollsim::cloneCount(scene.objects, o);
            const int cells = scrollsim::cellsPerSegment(scene.objects, o);
            const float pat = scrollsim::patternLength(scene.objects, o);
            ImGui::Text("Belt: %d clone objects (%d copies/segment, period %.1f)",
                        clones, cells, pat);
            if (scrollsim::hasVariation(o))
                ImGui::TextColored(ImVec4(0.5f, 0.85f, 0.55f, 1.0f),
                                   "Per-cell variation on - the belt does not repeat.");
            else
                ImGui::TextDisabled(
                    "Plain tiling: the belt repeats every %.1f units. Fold a member "
                    "open to vary it.",
                    cells * pat);
            if (scrollsim::cloneCapped(scene.objects, o))
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                                   "Clone cap hit - raise Max clones or shorten the "
                                   "window to close the gap.");
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("Segments (tiled in order):");

        int removeSeg = -1;
        for (size_t s = 0; s < o.scrollSegments.size(); ++s) {
            ScrollSegment& seg = o.scrollSegments[s];
            ImGui::PushID((int)s);
            // Reorder + delete controls
            const bool canUp = s > 0;
            const bool canDown = s + 1 < o.scrollSegments.size();
            ImGui::BeginDisabled(!canUp);
            if (ImGui::ArrowButton("##segup", ImGuiDir_Up)) {
                std::swap(o.scrollSegments[s], o.scrollSegments[s - 1]);
                committed = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine(0.0f, 2.0f);
            ImGui::BeginDisabled(!canDown);
            if (ImGui::ArrowButton("##segdown", ImGuiDir_Down)) {
                std::swap(o.scrollSegments[s], o.scrollSegments[s + 1]);
                committed = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete segment")) removeSeg = (int)s;

            char segName[96];
            std::snprintf(segName, sizeof(segName), "%s", seg.name.c_str());
            ImGui::SetNextItemWidth(scaled(180));
            if (ImGui::InputText("Name", segName, sizeof(segName))) seg.name = segName;
            committed |= ImGui::IsItemDeactivatedAfterEdit();

            ImGui::SetNextItemWidth(scaled(120));
            ImGui::DragFloat("Length", &seg.length, 0.1f, 0.0f, 2000.0f,
                             seg.length > 0.0f ? "%.1f" : "auto");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SameLine();
            ImGui::TextDisabled("(0 = auto: %.1f)",
                                scrollsim::segmentLength(scene.objects, o, seg));

            // Member object list (add / remove by name, like Mirror). Each row
            // folds open into that member's per-cell variation.
            int removeAt = -1;
            for (size_t i = 0; i < seg.objects.size(); ++i) {
                ScrollMember& m = seg.objects[i];
                ImGui::PushID((int)i);
                if (ImGui::SmallButton("x")) removeAt = (int)i;
                ImGui::SameLine();
                bool exists = false;
                for (const SceneObject& t : scene.objects)
                    if (t.name == m.name) { exists = true; break; }
                // Folded rows still have to say what varies, or a belt's whole
                // behavior hides behind closed triangles.
                std::string summary;
                if (m.variant > 0)
                    summary += "  [variant " + std::to_string(m.variant) + "]";
                else if (m.chance < 1.0f) {
                    char pct[16];
                    std::snprintf(pct, sizeof(pct), "%.0f%%", m.chance * 100.0f);
                    summary += std::string("  [") + pct + "]";
                }
                if (m.yawVary != 0.0f || m.offsetVary != 0.0f || m.scaleVary != 0.0f)
                    summary += "  [jitter]";
                if (!exists) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.4f, 0.3f, 1));
                const bool open = ImGui::TreeNodeEx(
                    "##member", ImGuiTreeNodeFlags_SpanAvailWidth, "%s%s%s",
                    m.name.c_str(), exists ? "" : " (missing)", summary.c_str());
                if (!exists) ImGui::PopStyleColor();
                if (open) {
                    ImGui::SetNextItemWidth(scaled(120));
                    ImGui::BeginDisabled(m.variant > 0);
                    ImGui::SliderFloat("Appears in", &m.chance, 0.0f, 1.0f, "%.2f");
                    committed |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::EndDisabled();
                    ImGui::SetItemTooltip(
                        "Fraction of belt cells this member shows up in.\n"
                        "1 = every cell (the plain tiling). 0.35 = roughly a\n"
                        "third of them, drawn from the cell's own hash - so the\n"
                        "belt stops repeating without any extra geometry.");
                    ImGui::SetNextItemWidth(scaled(120));
                    ImGui::DragInt("Variant group", &m.variant, 0.1f, 0, 16);
                    committed |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::SetItemTooltip(
                        "0 = off. Members of this segment sharing a group number\n"
                        "are alternatives: exactly ONE of them shows per cell\n"
                        "(three obstacle shapes in one lane, say). A grouped\n"
                        "member ignores Appears in - the group always fills.");
                    ImGui::SetNextItemWidth(scaled(120));
                    ImGui::DragFloat("Yaw jitter", &m.yawVary, 0.5f, 0.0f, 180.0f,
                                     "+-%.0f deg");
                    committed |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::SetItemTooltip(
                        "Random spin around Y, per cell. Free variety for rocks,\n"
                        "trees and debris; leave at 0 for anything that has to\n"
                        "line up with its neighbour (floors, rails, walls).");
                    ImGui::SetNextItemWidth(scaled(120));
                    ImGui::DragFloat("Side jitter", &m.offsetVary, 0.05f, 0.0f, 50.0f,
                                     "+-%.2f");
                    committed |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::SetItemTooltip(
                        "Random offset across the belt (world units,\n"
                        "perpendicular to the axis and horizontal), per cell.");
                    ImGui::SetNextItemWidth(scaled(120));
                    ImGui::DragFloat("Scale jitter", &m.scaleVary, 0.005f, 0.0f, 0.9f,
                                     "+-%.2f");
                    committed |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::SetItemTooltip(
                        "Random uniform scale, per cell, as a fraction of the\n"
                        "authored size (0.20 = +-20%%). Keep it off for tiling\n"
                        "surfaces - a resized floor slab opens a gap.");
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            if (removeAt >= 0) {
                seg.objects.erase(seg.objects.begin() + removeAt);
                committed = true;
            }
            ImGui::SetNextItemWidth(scaled(200));
            if (ImGui::BeginCombo("##segAdd", "+ Add object...")) {
                for (const SceneObject& t : scene.objects) {
                    // scenery only - not markers or the scroller itself
                    const bool ok =
                        t.type != PrimitiveType::Scroller &&
                        t.type != PrimitiveType::Player &&
                        t.type != PrimitiveType::Camera &&
                        t.type != PrimitiveType::SpawnPoint && t.name != o.name;
                    if (!ok) continue;
                    bool listed = false;
                    for (const ScrollMember& n : seg.objects)
                        if (n.name == t.name) { listed = true; break; }
                    if (listed) continue;
                    if (ImGui::Selectable(t.name.c_str())) {
                        seg.objects.push_back(ScrollMember{t.name});
                        committed = true;
                    }
                }
                ImGui::EndCombo();
            }
            if (seg.objects.empty())
                ImGui::TextDisabled("Empty segment - add scene objects above.");
            ImGui::Separator();
            ImGui::PopID();
        }
        if (removeSeg >= 0) {
            o.scrollSegments.erase(o.scrollSegments.begin() + removeSeg);
            committed = true;
        }
        if (ImGui::Button("+ Add segment")) {
            ScrollSegment seg;
            seg.name = "segment-" + std::to_string(o.scrollSegments.size() + 1);
            o.scrollSegments.push_back(seg);
            committed = true;
        }
        if (o.scrollSegments.empty())
            ImGui::TextDisabled("No segments yet - add one, then assign objects.");
    }

    if (isPortal) {
        ImGui::SeparatorText("Portal");
        // Destination link: another Portal in this scene. One-way by design -
        // point both portals at each other for a two-way door.
        const std::string current =
            o.portalTarget.empty() ? "<none>" : o.portalTarget;
        bool targetExists = false;
        for (const SceneObject& t : project_.objects())
            if (t.type == PrimitiveType::Portal && t.name == o.portalTarget)
                targetExists = true;
        if (ImGui::BeginCombo("Target portal", current.c_str())) {
            if (ImGui::Selectable("<none>", o.portalTarget.empty()) &&
                !o.portalTarget.empty()) {
                o.portalTarget.clear();
                committed = true;
            }
            for (const SceneObject& t : project_.objects()) {
                if (t.type != PrimitiveType::Portal || t.name == o.name) continue;
                if (ImGui::Selectable(t.name.c_str(), t.name == o.portalTarget) &&
                    o.portalTarget != t.name) {
                    o.portalTarget = t.name;
                    committed = true;
                }
            }
            ImGui::EndCombo();
        }
        if (o.portalTarget.empty())
            ImGui::TextDisabled(
                "No target - the surface just shows the tint color.");
        else if (!targetExists)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                               "Target portal missing - surface inactive.");
        else {
            // convenience: make the pair two-way with one click
            SceneObject* tgt = nullptr;
            for (SceneObject& t : project_.objects())
                if (t.type == PrimitiveType::Portal && t.name == o.portalTarget)
                    tgt = &t;
            if (tgt && tgt->portalTarget != o.name) {
                if (ImGui::SmallButton("Link back (make two-way)")) {
                    tgt->portalTarget = o.name;
                    committed = true;
                }
            } else {
                ImGui::TextDisabled("Two-way pair (target links back).");
            }
        }
        if (ImGui::Checkbox("Terrain + sky in view", &o.portalShowTerrain))
            committed = true;
        if (ImGui::Checkbox("Teleport physics objects", &o.portalTeleportObjects))
            committed = true;
        if (ImGui::Checkbox("All objects in view (experimental)",
                            &o.portalViewAll))
            committed = true;
        if (o.portalViewAll) {
            ImGui::TextDisabled(
                "Every scene object renders in the through-view (the list\n"
                "below is ignored). The virtual camera's frustum culling and\n"
                "draw distances trim the cost, but big scenes pay a second\n"
                "submission pass - watch the FPS/profiler before shipping.");
        } else {
        if (catchAreaControls(o, "shown")) committed = true;
        ImGui::TextUnformatted("Objects visible through:");
        int removePortalAt = -1;
        for (size_t i = 0; i < o.portalObjects.size(); ++i) {
            ImGui::PushID(1000 + (int)i);
            if (ImGui::SmallButton("x")) removePortalAt = (int)i;
            ImGui::SameLine();
            bool exists = false;
            for (const SceneObject& t : project_.objects())
                if (t.name == o.portalObjects[i]) { exists = true; break; }
            if (exists)
                ImGui::TextUnformatted(o.portalObjects[i].c_str());
            else
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                                   "%s (missing)", o.portalObjects[i].c_str());
            ImGui::PopID();
        }
        if (removePortalAt >= 0) {
            o.portalObjects.erase(o.portalObjects.begin() + removePortalAt);
            committed = true;
        }
        if (ImGui::BeginCombo("##portalAdd", "+ Add object...")) {
            for (const SceneObject& t : project_.objects()) {
                // same set the mirror can reflect: types the game draws as
                // static geometry (animated models re-pose in the main view
                // only; through a portal they would show a stale pose)
                const bool viewable =
                    t.type == PrimitiveType::Box || t.type == PrimitiveType::Sphere ||
                    t.type == PrimitiveType::Cylinder ||
                    t.type == PrimitiveType::Cone || t.type == PrimitiveType::Plane ||
                    t.type == PrimitiveType::SavePoint ||
                    t.type == PrimitiveType::Model || t.type == PrimitiveType::Decal;
                if (!viewable || t.name == o.name) continue;
                bool listed = false;
                for (const std::string& n : o.portalObjects)
                    if (n == t.name) { listed = true; break; }
                if (listed) continue;
                if (ImGui::Selectable(t.name.c_str())) {
                    o.portalObjects.push_back(t.name);
                    committed = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled(
            "The view renders listed objects (+ terrain/sky above) from the\n"
            "target's side every frame - keep the list short. One portal\n"
            "view renders per frame; the other surfaces show the tint.");
        }  // !portalViewAll
    }

    if (o.type == PrimitiveType::Emitter) {
        ImGui::SeparatorText("Particle emitter");
        const char* kinds[] = {"Fire", "Smoke", "Fog", "Sparks", "Rain", "Custom"};
        if (ImGui::Combo("Effect", &o.emitterKind, kinds, 6)) committed = true;
        if (ImGui::DragInt("Density (count)", &o.emitterCount, 1.0f, 1, 256)) {}
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat("Particle size", &o.emitterSize, 0.02f, 0.05f, 8.0f, "%.2f");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        // optional texture: the material's map_Kd, tinted by the color
        if (drawMaterialCombo(o)) committed = true;
        if (!o.materialPath.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Edit...")) openMaterialEditor(o.materialPath);
        }
        if (o.emitterKind == 2) {  // fog density
            ImGui::DragFloat("Opacity", &o.emitterOpacity, 0.01f, 0.0f, 1.0f,
                             "%.2f");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::TextDisabled(
                "Slowly swirling puffs. For a thick rolling fog: big spawn\n"
                "area, Follow player on, a soft-alpha texture, and match the\n"
                "color to the distance fog color (Preferences > Distance fog).");
        }
        if (o.emitterKind == 5) {  // custom physics knobs
            ImGui::DragFloat("Speed", &o.emitterSpeed, 0.05f, 0.0f, 50.0f,
                             "%.2f u/s");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::DragFloat("Spread", &o.emitterSpread, 0.5f, 0.0f, 90.0f,
                             "%.0f deg");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::DragFloat("Gravity", &o.emitterGravity, 0.1f, -30.0f, 50.0f,
                             "%.1f u/s2");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::DragFloat("Weight", &o.emitterWeight, 0.02f, 0.05f, 10.0f,
                             "%.2f");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::DragFloat("Lifetime", &o.emitterLife, 0.05f, 0.1f, 10.0f,
                             "%.2f s");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::DragFloat("Grow", &o.emitterGrow, 0.02f, 0.1f, 4.0f, "%.2f x");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::DragFloat("Opacity", &o.emitterOpacity, 0.01f, 0.0f, 1.0f,
                             "%.2f");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ImGui::Checkbox("Die on terrain", &o.emitterDieOnGround))
                committed = true;
            ImGui::TextDisabled(
                "Particles shoot along the emitter's +Y axis - use\n"
                "Rotation to aim (90 deg X = a horizontal pipe leak).\n"
                "Negative gravity rises (steam); low weight = air drag.");
        }
        if (ImGui::Checkbox("Enabled", &o.emitterEnabled)) committed = true;
        if (ImGui::Checkbox("Follow player", &o.emitterFollowPlayer)) committed = true;
        if (o.emitterFollowPlayer)
            ImGui::TextDisabled("Position is an offset from the player - keep\n"
                                "X/Z near 0 and Y = height above the player.");
        ImGui::TextDisabled("Color tints the particles; scale X/Z = spawn area.\n"
                            "Rain falls from the emitter down to the terrain.\n"
                            "Show/Hide Object nodes switch the emitter on/off.");
    }

    if (o.type == PrimitiveType::SoundEmitter) {
        ImGui::SeparatorText("Sound emitter");
        if (project_.sounds.empty()) {
            ImGui::TextDisabled("No sounds - import WAVs in the Sounds section first.");
        } else {
            int current = -1;
            for (int i = 0; i < (int)project_.sounds.size(); ++i)
                if (project_.sounds[i] == o.soundPath) current = i;
            const std::string preview =
                current >= 0 ? project_.sounds[current] : "<pick a sound>";
            if (ImGui::BeginCombo("Sound", preview.c_str())) {
                for (int i = 0; i < (int)project_.sounds.size(); ++i) {
                    if (ImGui::Selectable(project_.sounds[i].c_str(), i == current)) {
                        o.soundPath = project_.sounds[i];
                        committed = true;
                    }
                }
                ImGui::EndCombo();
            }
        }
        if (ImGui::Checkbox("Autoplay (while the player is in range)", &o.soundAuto))
            committed = true;
        if (ImGui::Checkbox("Play on player (plain stereo, ignores position)",
                            &o.soundOnPlayer))
            committed = true;
        if (!o.soundOnPlayer) {
            ImGui::DragFloat("Range", &o.soundRange, 0.5f, 0.5f, 200.0f, "%.1f units");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        ImGui::DragFloat("Interval", &o.soundInterval, 0.1f, 0.0f, 60.0f, "%.1f s");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::Checkbox("Reverb (rooms affect this sound)", &o.soundReverb))
            committed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "On: this emitter is heard through whatever reverb zone the\n"
                "player is standing in (docs/reverb.md).\n"
                "Off: it stays dry everywhere - a UI beep, a voice line, or a\n"
                "sample that was recorded with its own room already on it.\n"
                "The send is one bit per voice in hardware, so there is no\n"
                "per-emitter wet amount - only the zone's own.");
        if (o.soundOnPlayer) {
            ImGui::TextDisabled("Plays centered at full volume everywhere -\n"
                                "no distance falloff, no panning (dialogs,\n"
                                "narration). Hide Object mutes.");
        } else {
            ImGui::TextDisabled("Volume fades with distance to the player.\n"
                                "Interval 0 loops the sample seamlessly; > 0\n"
                                "retriggers it every N seconds. Hide Object mutes.");
        }
    }

    if (o.type == PrimitiveType::PointLight) {
        ImGui::SeparatorText("Point light");
        ImGui::TextDisabled("The \"Color\" field above sets the light color.");
        ImGui::DragFloat("Brightness", &o.lightBright, 0.02f, 0.0f, 4.0f, "%.2f");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat("Radius", &o.lightRadius, 0.1f, 0.1f, 100.0f, "%.1f units");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        committed |= ImGui::Checkbox("Dynamic (live)", &o.lightDynamic);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Off: baked into nearby vertex colors at build\n"
                "(static, zero runtime cost).\n"
                "On: registered every frame - can flicker, move with its\n"
                "object and be switched by the Set Light flow node.\n"
                "The engine lights each mesh with its strongest dynamic\n"
                "light (one slot per mesh; max 8 per scene).");
        if (o.lightDynamic) {
            ImGui::DragFloat("Flicker", &o.lightFlicker, 0.01f, 0.0f, 1.0f, "%.2f");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Torch-like brightness wobble. 0 = steady.");
        }
        {
            const char* beams[] = {"None", "Glow (corona)", "Glow + cone shaft"};
            committed |= ImGui::Combo("Beam", &o.lightBeam, beams, 3);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Draws the light source itself: an additive camera-facing\n"
                    "halo, optionally with a cone shaft pointing down (street\n"
                    "lamp look). Follows flicker / Set Light on dynamic\n"
                    "lights; steady on baked ones. Game-only (not previewed\n"
                    "in the viewport).");
        }
        if (!o.lightDynamic)
            ImGui::TextDisabled("Previewed live in the viewport; in the game it is\n"
                                "baked into nearby terrain & object vertex colors\n"
                                "at build (static light, zero runtime cost).");
    }

    if (o.type == PrimitiveType::SavePoint) {
        ImGui::SeparatorText("Save point");
        ImGui::TextDisabled("Renders as a solid box in the game. Pressing USE\n"
                            "on it opens the save menu: 3 slots on the memory\n"
                            "card (mc0:), saving flagged objects, custom save\n"
                            "values, the player position and the scene.");
    }

    if (o.type == PrimitiveType::Camera) {
        ImGui::SeparatorText("Camera");
        ImGui::DragFloat("FOV (deg)", &o.cameraFov, 0.5f, 20.0f, 110.0f, "%.0f");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        if (o.cameraFov < 20.0f) o.cameraFov = 20.0f;
        if (o.cameraFov > 110.0f) o.cameraFov = 110.0f;
        const bool looking = lookThroughCam_ == o.name;
        if (ImGui::Button(looking ? "Stop looking through" : "Look through")) {
            if (looking)
                lookThroughCam_.clear();
            else
                lookThroughCam_ = o.name;  // editor view state - no undo entry
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Render the viewport from this camera (also in the\n"
                              "\"View:\" control in the viewport corner).");
        ImGui::TextDisabled("A Cutscene Director shot marker - invisible in the\n"
                            "game. Bind a camera-track keyframe to it (Tools >\n"
                            "Cutscene Director) and the shot films from here,\n"
                            "looking down the +Z wedge, with this FOV. Animate\n"
                            "this object in the same sequence for dolly shots.");
        if (ImGui::Checkbox("Render to texture (CCTV feed)", &o.camFeed))
            committed = true;
        if (o.camFeed) {
            ImGui::TextDisabled(
                "Renders this camera's view (sky + the list below) into a\n"
                "128x128 live texture every frame. Put it on any object via\n"
                "Properties > Texture feed. ONE active feed camera per scene\n"
                "(the first enabled one wins).");
            if (ImGui::Checkbox("Show terrain in feed", &o.camFeedTerrain))
                committed = true;
            if (catchAreaControls(o, "in the feed")) committed = true;
            ImGui::TextUnformatted("Objects in feed:");
            int removeAt = -1;
            for (size_t i = 0; i < o.camFeedObjects.size(); ++i) {
                ImGui::PushID((int)(i + 700));
                if (ImGui::SmallButton("x")) removeAt = (int)i;
                ImGui::SameLine();
                bool exists = false;
                for (const SceneObject& t : project_.objects())
                    if (t.name == o.camFeedObjects[i]) { exists = true; break; }
                if (exists)
                    ImGui::TextUnformatted(o.camFeedObjects[i].c_str());
                else
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                                       "%s (missing)", o.camFeedObjects[i].c_str());
                ImGui::PopID();
            }
            if (removeAt >= 0) {
                o.camFeedObjects.erase(o.camFeedObjects.begin() + removeAt);
                committed = true;
            }
            if (ImGui::BeginCombo("##feedAdd", "+ Add object...")) {
                for (const SceneObject& t : project_.objects()) {
                    const bool feedable =
                        t.type == PrimitiveType::Box || t.type == PrimitiveType::Sphere ||
                        t.type == PrimitiveType::Cylinder ||
                        t.type == PrimitiveType::Cone || t.type == PrimitiveType::Plane ||
                        t.type == PrimitiveType::SavePoint ||
                        t.type == PrimitiveType::Model || t.type == PrimitiveType::Decal;
                    if (!feedable || t.name == o.name) continue;
                    bool listed = false;
                    for (const std::string& n : o.camFeedObjects)
                        if (n == t.name) { listed = true; break; }
                    if (listed) continue;
                    if (ImGui::Selectable(t.name.c_str())) {
                        o.camFeedObjects.push_back(t.name);
                        committed = true;
                    }
                }
                ImGui::EndCombo();
            }
        }
    }

    if (isEmpty) {
        ImGui::SeparatorText("Empty");
        ImGui::TextDisabled("Pure transform - invisible in the game, no collision.\n"
                            "An anchor for attached scripts, a waypoint for flow\n"
                            "graphs; scripts read position/rotation/scale/color.");
    }

    if (o.type == PrimitiveType::Player) {
        ImGui::SeparatorText("Player");
        const char* modes[] = {"Walk (FPP)", "Noclip (fly)", "Third person"};
        if (ImGui::Combo("Mode", &o.playerMode, modes, 3)) committed = true;
        walkSpeedDrag("Walk speed", o.playerWalkSpeed,
                      project_.settings.unitsPerMeter, &committed);
        ImGui::DragFloat("Look speed", &o.playerLookSpeed, 0.05f, 0.1f, 5.0f, "%.2f");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat(o.playerMode == 2 ? "Body height" : "Eye height",
                         &o.playerEyeHeight, 0.05f, 0.2f, 50.0f, "%.2f");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        committed |= ImGui::Checkbox("Can jump (X)", &o.playerCanJump);
        if (o.playerCanJump) {
            ImGui::DragFloat("Jump speed", &o.playerJumpSpeed, 0.1f, 0.0f, 50.0f, "%.1f");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        {
            // Which player slot this object fills: scene order decides - the
            // first Player object is P1, the second is P2 (two-player modes,
            // Preferences > Multiplayer). Any further ones are ignored.
            int slot = 0, seen = 0;
            for (const auto& other : project_.objects()) {
                if (other.type != PrimitiveType::Player) continue;
                ++seen;
                if (&other == &o) slot = seen;
            }
            if (slot == 1)
                ImGui::TextDisabled(
                    "Player 1 (first in the scene) - drives the camera.");
            else if (slot == 2)
                ImGui::TextDisabled(
                    project_.settings.multiplayer != "off"
                        ? "Player 2 - joins in the two-player modes."
                        : "Player 2 - inactive until Preferences > Multiplayer "
                          "is enabled.");
            else if (slot > 2)
                ImGui::TextDisabled(
                    "Extra Player object - the game uses only the first two.");
        }
        if (o.playerMode == 2)
            ImGui::TextDisabled(o.playerFaceCamera
                                    ? "Third person: X jumps. The avatar faces the camera."
                                    : "Third person: X jumps. The avatar faces where it walks.");
        else
            ImGui::TextDisabled("Noclip: X up, Square down. Walk: X jumps.");

        // Third-person avatar: the Player's OWN animated .glb model, with its
        // clips mapped to locomotion states. The same .glb/anim pipeline as
        // regular animated models - the runtime just drives the transform and
        // auto-picks the clip from the player's real speed (walk/run/idle),
        // cross-faded. Scripts/flow-graph can still force any clip.
        if (o.playerMode == 2) {
            ImGui::SeparatorText("Avatar model");
            const std::string current =
                o.modelPath.empty()
                    ? "<none>"
                    : std::filesystem::path(o.modelPath).filename().string();
            if (ImGui::BeginCombo("Model", current.c_str())) {
                const std::vector<std::string> anim = listAnimatedModelFiles();
                for (const std::string& m : anim) {
                    const std::string rel = "res/models/" + m;
                    if (ImGui::Selectable((m + " (animated)").c_str(),
                                          rel == o.modelPath) &&
                        rel != o.modelPath) {
                        o.modelPath = rel;
                        o.playerIdleClip.clear();
                        o.playerWalkClip.clear();
                        o.playerRunClip.clear();
                        o.playerJumpClip.clear();
                        o.playerBackClip.clear();
                        o.playerStrafeLeftClip.clear();
                        o.playerStrafeRightClip.clear();
                        committed = true;
                    }
                }
                if (anim.empty())
                    ImGui::TextDisabled(
                        "No animated models (.glb/.fbx) - Import one in Project > Assets.");
                ImGui::EndCombo();
            }
            if (o.modelPath.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                                   "Pick an animated .glb/.fbx - the avatar is invisible\n"
                                   "without one (only the camera moves).");
            } else if (!isAnimatedModelPath(o.modelPath)) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                                   "Third-person bodies must be an animated model (.glb/.fbx).");
            } else {
                const GlbInfo& info = glbInfo(o.modelPath);
                if (!info.ok) {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                                       "Unusable model: %s", info.error.c_str());
                } else {
                    ImGui::TextDisabled("%d verts, %d clip(s)", info.vertexCount,
                                        (int)info.clips.size());
                    // Locomotion clip mapping. Idle/Walk are required (fall back
                    // to the first clip); Run/Jump are optional (<none>).
                    // Effective (post-rename) names, like every other clip ref.
                    const std::vector<std::string> clips =
                        effectiveClips(o.modelPath);
                    auto clipCombo = [&](const char* label, std::string& clip,
                                         bool optional) {
                        const std::string cur =
                            clip.empty()
                                ? (optional ? "<none>"
                                            : (clips.empty()
                                                   ? "<none>"
                                                   : clips.front() + " (first)"))
                                : clip;
                        if (ImGui::BeginCombo(label, cur.c_str())) {
                            if (optional && ImGui::Selectable("<none>", clip.empty())) {
                                if (!clip.empty()) {
                                    clip.clear();
                                    committed = true;
                                }
                            }
                            for (const std::string& c : clips) {
                                if (ImGui::Selectable(c.c_str(), c == clip) &&
                                    clip != c) {
                                    clip = c;
                                    committed = true;
                                }
                            }
                            ImGui::EndCombo();
                        }
                    };
                    clipCombo("Idle clip", o.playerIdleClip, false);
                    clipCombo("Walk clip", o.playerWalkClip, false);
                    clipCombo("Run clip", o.playerRunClip, true);
                    clipCombo("Jump clip", o.playerJumpClip, true);
                    ImGui::DragFloat("Run at", &o.playerRunThreshold, 0.01f, 0.1f,
                                     1.0f, "%.2f of walk speed");
                    committed |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::TextDisabled(
                        "Clip auto-selected from real speed; a script/flow\n"
                        "\"Play Animation\" one-shot plays to the end first.");
                    // Directional locomotion: only meaningful with the avatar
                    // facing the camera - otherwise it turns into the movement
                    // and every step is a forward step.
                    committed |= ImGui::Checkbox("Face camera (strafe)",
                                                 &o.playerFaceCamera);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(
                            "The avatar keeps facing the camera instead of turning\n"
                            "into the movement direction; sideways/backward steps\n"
                            "play the directional clips below.");
                    if (o.playerFaceCamera) {
                        clipCombo("Back clip", o.playerBackClip, true);
                        clipCombo("Strafe left clip", o.playerStrafeLeftClip, true);
                        clipCombo("Strafe right clip", o.playerStrafeRightClip, true);
                        ImGui::TextDisabled(
                            "<none> = the walk clip covers that direction.");
                    }
                    // Each Player object carries its own LOD overrides - in a
                    // two-player scene that gives P1 and P2 independent
                    // avatar LOD settings.
                    committed |= drawLodOverrides(o);
                }
            }

            ImGui::SeparatorText("Third-person camera");
            // Style: Orbit is the classic free-look rig; Top-down / Isometric
            // are Fixed-angle presets (picking them seeds pitch/yaw, which
            // stay editable), for camera-locked games. The left stick always
            // moves the avatar relative to the camera heading.
            const char* camStyles[] = {"Orbit (behind)", "Top-down", "Isometric",
                                       "Fixed angle"};
            if (ImGui::Combo("Style", &o.playerCamStyle, camStyles, 4)) {
                if (o.playerCamStyle == 1) {  // top-down preset
                    o.playerCamPitch = 80.0f;
                    o.playerCamYaw = 0.0f;
                } else if (o.playerCamStyle == 2) {  // isometric preset
                    o.playerCamPitch = 35.0f;
                    o.playerCamYaw = 45.0f;
                }
                committed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Orbit: free look - the right stick orbits the camera\n"
                    "behind the avatar. Top-down / Isometric / Fixed angle\n"
                    "pin the camera to a set angle (top-down and isometric\n"
                    "just seed it - tune Angle/Direction freely after).");
            if (o.playerCamStyle != 0) {
                ImGui::DragFloat("Angle (deg)", &o.playerCamPitch, 0.5f, 10.0f,
                                 85.0f, "%.1f");
                committed |= ImGui::IsItemDeactivatedAfterEdit();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Camera elevation above the horizon:\n"
                                      "85 = nearly straight down (top-down),\n"
                                      "~35 = the classic isometric slant.");
                ImGui::DragFloat("Direction (deg)", &o.playerCamYaw, 1.0f,
                                 -180.0f, 180.0f, "%.0f");
                committed |= ImGui::IsItemDeactivatedAfterEdit();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("World heading the camera looks along\n"
                                      "(which way is \"up\" on screen).");
                committed |=
                    ImGui::Checkbox("Right stick rotates", &o.playerCamYawRotate);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Let the player orbit the view with the\n"
                                      "right stick (the pitch stays pinned).");
            }
            // Distance / Height / Shoulder are the rig offset in the camera's
            // own frame: back, up, sideways.
            ImGui::DragFloat("Distance", &o.playerCamDist, 0.1f, 1.0f, 40.0f, "%.1f");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("How far back the camera sits. The spring arm may\n"
                                  "shorten it, so this is the maximum.");
            ImGui::DragFloat("Height", &o.playerCamHeight, 0.05f, 0.0f, 20.0f, "%.2f");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::DragFloat("Shoulder", &o.playerCamShoulder, 0.02f, -3.0f, 3.0f, "%.2f");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Slides the camera sideways for an over-the-shoulder\n"
                                  "shot: 0 = centered behind, ~0.6 = right shoulder,\n"
                                  "negative = left. The avatar moves off-center in\n"
                                  "frame (eye and aim point shift together).");
            ImGui::DragFloat("Turn rate", &o.playerTurnRate, 0.01f, 0.02f, 1.0f, "%.2f");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
        }

        ImGui::SeparatorText("Flashlight");
        committed |= ImGui::Checkbox("Enabled", &o.flashlightEnabled);
        if (o.flashlightEnabled) {
            ImGui::ColorEdit3("Light color", o.flashlightColor);
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::DragFloat("Reach (units)", &o.flashlightRange, 0.5f, 1.0f, 200.0f,
                             "%.1f");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::DragFloat("Cone half-angle (deg)", &o.flashlightAngle, 0.5f, 2.0f,
                             80.0f, "%.1f");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        // Optional pad button the player presses to turn the beam on/off. The
        // on/off state only shows while Enabled (it respects Enabled), and the
        // flow graph can flip Enabled with the Set Flashlight node.
        const char* toggleBtns[] = {"<none>",   "Cross",    "Circle",    "Square",
                                    "Triangle", "DpadUp",   "DpadDown",  "DpadLeft",
                                    "DpadRight", "L1",      "L2",        "L3",
                                    "R1",       "R2",       "R3",        "Start",
                                    "Select"};
        const std::string cur =
            o.flashlightToggleButton.empty() ? "<none>" : o.flashlightToggleButton;
        if (ImGui::BeginCombo("Toggle button", cur.c_str())) {
            for (const char* b : toggleBtns) {
                const bool isNone = std::strcmp(b, "<none>") == 0;
                const bool selected =
                    isNone ? o.flashlightToggleButton.empty() : o.flashlightToggleButton == b;
                if (ImGui::Selectable(b, selected)) {
                    o.flashlightToggleButton = isNone ? std::string() : std::string(b);
                    committed = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("Enabled is the master switch (Set Flashlight flow node\n"
                            "can change it). The toggle button gates the beam on/off\n"
                            "at runtime, but only while Enabled.");

        // Ground-pool texture. Per-vertex lighting cannot draw a spot
        // smaller than the mesh tessellation, so the beam paints its
        // ground pool with this sprite - which is also the knob for the
        // beam's SHAPE (a gobo, a cross, a cracked-lens blob...).
        ImGui::Spacing();
        if (o.flashlightTexture.empty())
            ImGui::TextDisabled("Pool texture: built-in soft circle");
        else
            ImGui::TextDisabled("Pool texture: %s", o.flashlightTexture.c_str());
        if (ImGui::Button(o.flashlightTexture.empty() ? "Pool texture (PNG)..."
                                                      : "Replace pool texture...")) {
            const std::string src = pickPngFile();
            if (!src.empty()) {
                const std::filesystem::path srcPath(src);
                const std::string fileName =
                    sanitizeAssetName(srcPath.filename().string());
                const std::filesystem::path destDir =
                    std::filesystem::path(project_.dir) / "res" / "hud";
                std::error_code ec;
                std::filesystem::create_directories(destDir, ec);
                std::filesystem::copy_file(
                    srcPath, destDir / fileName,
                    std::filesystem::copy_options::overwrite_existing, ec);
                if (!ec) {
                    o.flashlightTexture = "res/hud/" + fileName;
                    committed = true;
                } else {
                    statusMessage_ = "Flashlight texture import failed: " + ec.message();
                }
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "The sprite the beam's ground pool is drawn with - swap it to\n"
                "reshape the light (gobo, cross, cracked lens). It draws\n"
                "ADDITIVELY, so put the shape in the RGB channels: black is\n"
                "transparent, alpha is ignored. Power-of-two sizes only.");
        if (!o.flashlightTexture.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("Reset##flashtex")) {
                o.flashlightTexture.clear();
                committed = true;
            }
        }
    }

    // Attached scripts (Unity-style components): class names registered in
    // src/scripts/*.cpp with TYRA_OBJECT_SCRIPT(Name). The game creates one
    // instance per attachment at scene load - the same class on five objects
    // runs as five independent instances, each seeing its object as `self`.
    ImGui::SeparatorText("Scripts");
    {
        const std::vector<std::string> registered = objectScriptNames();
        auto isRegistered = [&](const std::string& n) {
            for (const std::string& r : registered)
                if (r == n) return true;
            return false;
        };
        for (int i = 0; i < (int)o.scripts.size();) {
            ImGui::PushID(i);
            const bool removed = ImGui::SmallButton("x");
            ImGui::SameLine();
            if (isRegistered(o.scripts[i])) {
                ImGui::TextUnformatted(o.scripts[i].c_str());
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s - not found",
                                   o.scripts[i].c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("No TYRA_OBJECT_SCRIPT(%s) in src/scripts/*.cpp\n"
                                      "- the game skips it (with a log line).",
                                      o.scripts[i].c_str());
            }
            ImGui::PopID();
            if (removed) {
                o.scripts.erase(o.scripts.begin() + i);
                committed = true;
            } else {
                ++i;
            }
        }
        ImGui::SetNextItemWidth(ImGui::CalcItemWidth());
        if (ImGui::BeginCombo("##attach_script", "Attach script...")) {
            bool any = false;
            for (const std::string& r : registered) {
                bool attached = false;
                for (const std::string& s : o.scripts) attached |= (s == r);
                if (attached) continue;
                any = true;
                if (ImGui::Selectable(r.c_str(), false)) {
                    o.scripts.push_back(r);
                    committed = true;
                }
            }
            if (!any)
                ImGui::TextDisabled(registered.empty()
                                        ? "No object scripts in src/scripts yet."
                                        : "Every script is already attached.");
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("New script...")) {
            openNewScriptPopup_ = true;
            newScriptError_.clear();
            newScriptAttachTo_ = selectedObject_;
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Delete object")) {
        if (committed) commitChange();  // flush any pending field edit first
        deleteSelectedObjects();        // commits + clears on its own
        ImGui::End();
        return;
    }

    if (committed) commitChange();
    ImGui::End();
}

// Properties body for a multi-selection: only the fields common to every
// selected object. Transforms apply relatively (a drag nudges the whole group
// by the same delta, keeping its arrangement); other shared fields are set to
// one value for all and render a "mixed" dash while they differ. Inherently
// per-object fields (name, model/material/sound, scripts) are omitted - they
// are edited by selecting a single object. Called with the Properties window
// already open (drawPropertiesWindow handles Begin/End).
void App::drawMultiProperties() {
    std::vector<SceneObject*> objs;
    for (int i : selection_)
        if (i >= 0 && i < (int)project_.objects().size())
            objs.push_back(&project_.objects()[i]);
    if (objs.size() < 2) return;
    SceneObject& primary = *objs.back();  // anchor: seeds the transform values
    bool committed = false;

    // Header + per-type tally ("2 Box, 1 Sphere").
    ImGui::Text("%d objects selected", (int)objs.size());
    {
        std::string tally;
        for (int t = 0; t < kPrimitiveTypeCount; ++t) {
            int n = 0;
            for (auto* p : objs)
                if ((int)p->type == t) ++n;
            if (!n) continue;
            if (!tally.empty()) tally += ", ";
            tally += std::to_string(n) + " " + typeLabel((PrimitiveType)t);
        }
        ImGui::TextDisabled("%s", tally.c_str());
    }
    // Provenance, same as the single-object view - and this is the case that
    // actually happens, because clicking a prefab group in the Project panel
    // selects the whole instance at once.
    {
        std::string src = objs.front()->prefabSource;
        for (auto* p : objs)
            if (p->prefabSource != src) src.clear();
        if (!src.empty()) {
            ImGui::TextDisabled("All from prefab: %s", src.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Open in Prefabs")) {
                for (size_t pi = 0; pi < project_.prefabs.size(); ++pi)
                    if (project_.prefabs[pi].name == src) prefabSelected_ = (int)pi;
                showPrefabs_ = true;
            }
        }
    }
    ImGui::Separator();

    // Turn the selection into one endless scroller: a new Scroller object at the
    // group's centroid whose first segment lists every selected object. (Creates
    // and re-selects the scroller, so we return right after - `objs` is stale.)
    if (ImGui::Button("Make endless scroller from selection")) {
        float c[3] = {0, 0, 0};
        std::vector<std::string> names;
        for (auto* p : objs) {
            if (p->type == PrimitiveType::Scroller) continue;
            names.push_back(p->name);
            for (int a = 0; a < 3; ++a) c[a] += p->position[a];
        }
        if (!names.empty()) {
            for (int a = 0; a < 3; ++a) c[a] /= (float)names.size();
            addScroller();  // appends + selects the new scroller, commits
            SceneObject& sc = project_.objects().back();
            for (int a = 0; a < 3; ++a) sc.position[a] = c[a];
            ScrollSegment seg;
            seg.name = "segment-1";
            for (const std::string& n : names) seg.objects.push_back(ScrollMember{n});
            sc.scrollSegments.push_back(seg);
            commitChange();
        }
        // Selection is now the single new scroller; the caller (drawProperties-
        // Window) owns the Properties Begin/End, so just stop drawing this frame.
        return;
    }
    ImGui::SetItemTooltip(
        "Groups the selected objects into a new Scroller's first segment.");
    ImGui::Separator();

    // Which field groups apply = the intersection over the whole selection.
    // Same type predicates the single-object view uses (isShape includes Plane;
    // Decal is a textured quad with transform/color/material but no game state).
    bool allShape = true, allSolid = true, allSaveable = true, allRot = true,
         allScale = true, allColor = true, allSameType = true, allModel = true,
         allEmitter = true, allLight = true, allDetail = true, anyModel = false,
         anySavePoint = false;
    for (auto* p : objs) {
        const SceneObject& o = *p;
        const bool shape = o.type == PrimitiveType::Box || o.type == PrimitiveType::Sphere ||
                           o.type == PrimitiveType::Cylinder ||
                           o.type == PrimitiveType::Cone || o.type == PrimitiveType::Plane;
        const bool solid =
            shape || o.type == PrimitiveType::Model || o.type == PrimitiveType::SavePoint;
        const bool empty = o.type == PrimitiveType::Empty;
        const bool decal = o.type == PrimitiveType::Decal;
        const bool mirror =
            o.type == PrimitiveType::Mirror || o.type == PrimitiveType::Portal;
        // Areas: transform + color are the whole object (the box and its wire).
        const bool area = o.type == PrimitiveType::Area;
        // Detail (segments/subdivisions) exists for the curved/box-like
        // primitives (SavePoint tessellates as a Box), not for the flat Plane.
        const bool hasDetail = o.type == PrimitiveType::Box ||
                               o.type == PrimitiveType::Sphere ||
                               o.type == PrimitiveType::Cylinder ||
                               o.type == PrimitiveType::Cone ||
                               o.type == PrimitiveType::SavePoint;
        allShape = allShape && shape;
        allSolid = allSolid && solid;
        allDetail = allDetail && hasDetail;
        allSameType = allSameType && (o.type == primary.type);
        allModel = allModel && (o.type == PrimitiveType::Model);
        allEmitter = allEmitter && (o.type == PrimitiveType::Emitter);
        allLight = allLight && (o.type == PrimitiveType::PointLight);
        anyModel = anyModel || (o.type == PrimitiveType::Model);
        anySavePoint = anySavePoint || (o.type == PrimitiveType::SavePoint);
        allRot = allRot && (solid || empty || decal || mirror || area ||
                            o.type == PrimitiveType::Camera ||
                            (o.type == PrimitiveType::Emitter && o.emitterKind == 5));
        allScale = allScale && (solid || empty || decal || mirror || area ||
                                o.type == PrimitiveType::Emitter);
        allColor = allColor && (solid || empty || decal || mirror || area ||
                                o.type == PrimitiveType::Emitter ||
                                o.type == PrimitiveType::PointLight ||
                                o.type == PrimitiveType::Camera);
        allSaveable = allSaveable && (solid || empty || o.type == PrimitiveType::Emitter ||
                                      o.type == PrimitiveType::SoundEmitter);
    }

    // --- edit helpers ---
    // Relative transform: seed from the anchor, apply the drag delta to all.
    auto relDrag3 = [&](const char* label, float* (*get)(SceneObject&), float speed,
                        float lo, float hi, const char* fmt) {
        float* pv = get(primary);
        float v[3] = {pv[0], pv[1], pv[2]};
        ImGui::DragFloat3(label, v, speed, lo, hi, fmt);
        for (int k = 0; k < 3; ++k) {
            const float d = v[k] - pv[k];
            if (d != 0.0f)
                for (auto* p : objs) get(*p)[k] += d;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) committed = true;
    };
    // Set-all scalar/bool/combo with a "mixed" indicator while values differ.
    auto multiCheck = [&](const char* label, bool SceneObject::* field) {
        bool mixed = false;
        for (auto* p : objs) mixed = mixed || (p->*field != primary.*field);
        bool v = primary.*field;
        if (mixed) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
        const bool changed = ImGui::Checkbox(label, &v);
        if (mixed) ImGui::PopItemFlag();
        if (changed) {
            for (auto* p : objs) p->*field = v;
            committed = true;
        }
    };
    auto multiDragF = [&](const char* label, float SceneObject::* field, float speed,
                          float lo, float hi, const char* fmt) {
        bool mixed = false;
        for (auto* p : objs) mixed = mixed || (p->*field != primary.*field);
        float v = primary.*field;
        if (mixed) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
        ImGui::DragFloat(label, &v, speed, lo, hi, fmt);
        if (mixed) ImGui::PopItemFlag();
        if (v != primary.*field)
            for (auto* p : objs) p->*field = v;
        if (ImGui::IsItemDeactivatedAfterEdit()) committed = true;
    };
    auto multiCombo = [&](const char* label, int SceneObject::* field,
                          const char* const items[], int count) {
        const int v0 = primary.*field;
        bool mixed = false;
        for (auto* p : objs) mixed = mixed || (p->*field != v0);
        const char* preview =
            mixed ? "(multiple)" : (v0 >= 0 && v0 < count ? items[v0] : "");
        if (ImGui::BeginCombo(label, preview)) {
            for (int i = 0; i < count; ++i)
                if (ImGui::Selectable(items[i], !mixed && v0 == i)) {
                    for (auto* p : objs) p->*field = i;
                    committed = true;
                }
            ImGui::EndCombo();
        }
    };

    // --- transforms (relative) ---
    relDrag3("Position", [](SceneObject& o) -> float* { return o.position; }, 0.1f, 0.0f,
             0.0f, "%.3f");
    if (allRot)
        relDrag3("Rotation", [](SceneObject& o) -> float* { return o.rotation; }, 1.0f,
                 -360.0f, 360.0f, "%.0f deg");
    if (allScale) {
        relDrag3("Scale", [](SceneObject& o) -> float* { return o.scale; }, 0.05f, 0.01f,
                 1000.0f, "%.3f");
        for (auto* p : objs)
            for (float& s : p->scale)
                if (s < 0.01f) s = 0.01f;  // additive delta must not go non-positive
    }
    ImGui::TextDisabled("Transforms apply to all - the arrangement is kept.");

    // --- color ---
    if (allColor) {
        float c[3] = {primary.color[0], primary.color[1], primary.color[2]};
        bool mixed = false;
        for (auto* p : objs)
            mixed = mixed || p->color[0] != c[0] || p->color[1] != c[1] ||
                    p->color[2] != c[2];
        if (mixed) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
        const bool changed = ImGui::ColorEdit3("Color", c);
        if (mixed) ImGui::PopItemFlag();
        if (changed)
            for (auto* p : objs) {
                p->color[0] = c[0], p->color[1] = c[1], p->color[2] = c[2];
            }
        if (ImGui::IsItemDeactivatedAfterEdit()) committed = true;
    }

    // --- shape type / detail ---
    if (allShape) {
        // Shape enum values aren't contiguous (Plane = 12), so map combo indices
        // through an explicit list - same as the single-object view.
        static const PrimitiveType kShapeTypes[] = {
            PrimitiveType::Box, PrimitiveType::Sphere, PrimitiveType::Cylinder,
            PrimitiveType::Cone, PrimitiveType::Plane};
        const char* typeNames[] = {"Box", "Sphere", "Cylinder", "Cone", "Plane"};
        int t0 = 0;
        for (int i = 0; i < IM_ARRAYSIZE(kShapeTypes); ++i)
            if (kShapeTypes[i] == primary.type) t0 = i;
        bool mixedT = false;
        for (auto* p : objs) mixedT = mixedT || p->type != primary.type;
        const char* preview = mixedT ? "(multiple)" : typeNames[t0];
        if (ImGui::BeginCombo("Type", preview)) {
            for (int i = 0; i < IM_ARRAYSIZE(kShapeTypes); ++i)
                if (ImGui::Selectable(typeNames[i], !mixedT && t0 == i)) {
                    for (auto* p : objs) {
                        p->type = kShapeTypes[i];
                        p->primDetail = clampPrimDetail(p->type, p->primDetail);
                    }
                    committed = true;
                }
            ImGui::EndCombo();
        }
    }
    if (allDetail && allSameType) {
        const bool box = primDetailIsBoxLike(primary.type);
        int d = primary.primDetail;
        bool mixedD = false;
        for (auto* p : objs) mixedD = mixedD || p->primDetail != primary.primDetail;
        if (mixedD) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
        ImGui::DragInt("Detail", &d, 0.2f, primDetailMin(primary.type),
                       primDetailMax(primary.type), box ? "%d subdivisions" : "%d segments");
        if (mixedD) ImGui::PopItemFlag();
        if (d != primary.primDetail)
            for (auto* p : objs) p->primDetail = clampPrimDetail(p->type, d);
        if (ImGui::IsItemDeactivatedAfterEdit()) committed = true;
    }

    // --- solid geometry fields ---
    if (allSolid) {
        multiDragF("Draw distance", &SceneObject::drawDistance, 0.5f, 0.0f, 2000.0f,
                   "%.0f units");
        multiCheck("Show in reflections", &SceneObject::reflected);
        multiCheck("Projected shadow (live)", &SceneObject::projShadow);
        multiCheck("Cast shadow", &SceneObject::castShadow);
        multiCheck("Physics (rigid body)", &SceneObject::physics);
        if (!anySavePoint) {
            multiCheck("Usable (USE prompt + On Used)", &SceneObject::usable);
            multiCheck("Pickable (USE picks it up)", &SceneObject::pickable);
        }
        if (allModel) {
            const char* modes[] = {"Box (mesh AABB)", "Mesh (walkable triangles)", "None"};
            multiCombo("Collision", &SceneObject::collisionMode, modes, 3);
        } else if (!anyModel) {
            // primitives / save points: solid box or none
            bool mixed = false;
            for (auto* p : objs)
                mixed = mixed ||
                        (p->collisionMode != 2) != (primary.collisionMode != 2);
            bool solid = primary.collisionMode != 2;
            if (mixed) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
            const bool changed = ImGui::Checkbox("Collision (blocks the player)", &solid);
            if (mixed) ImGui::PopItemFlag();
            if (changed) {
                for (auto* p : objs) p->collisionMode = solid ? 0 : 2;
                committed = true;
            }
        }
    }
    if (allSaveable)
        multiCheck("Save state (position/color/visibility in saves)",
                   &SceneObject::saveState);

    // --- emitter / light groups (only when the whole selection is that type) ---
    if (allEmitter) {
        ImGui::SeparatorText("Particle emitter");
        const char* kinds[] = {"Fire", "Smoke", "Fog", "Sparks", "Rain", "Custom"};
        multiCombo("Effect", &SceneObject::emitterKind, kinds, 6);
        multiDragF("Particle size", &SceneObject::emitterSize, 0.02f, 0.05f, 8.0f, "%.2f");
        multiDragF("Opacity", &SceneObject::emitterOpacity, 0.01f, 0.0f, 1.0f, "%.2f");
        multiCheck("Enabled", &SceneObject::emitterEnabled);
        multiCheck("Follow player", &SceneObject::emitterFollowPlayer);
    }
    if (allLight) {
        ImGui::SeparatorText("Point light");
        multiDragF("Brightness", &SceneObject::lightBright, 0.02f, 0.0f, 4.0f, "%.2f");
        multiDragF("Radius", &SceneObject::lightRadius, 0.1f, 0.1f, 100.0f, "%.1f units");
    }

    ImGui::Separator();
    ImGui::TextDisabled("Name, model, materials, sounds and scripts are edited\n"
                        "one object at a time - select a single object for those.");
    ImGui::Separator();
    const std::string delLabel =
        "Delete " + std::to_string((int)objs.size()) + " objects";
    if (ImGui::Button(delLabel.c_str())) {
        if (committed) commitChange();
        deleteSelectedObjects();  // commits + clears on its own
        return;
    }
    if (committed) commitChange();
}

// Per-object LOD override rows (animated models + player avatars). Each
// checkbox flips between "use the project preference" (-1, the default) and
// an explicit per-object distance; dragging the value to 0 turns that LOD
// off for this object entirely.
bool App::drawLodOverrides(SceneObject& o, bool animated) {
    bool committed = false;
    auto row = [&](const char* label, float& v, float projectDefault) {
        bool ov = v >= 0.0f;
        const std::string cb = std::string("Override ") + label;
        if (ImGui::Checkbox(cb.c_str(), &ov)) {
            v = ov ? (projectDefault > 0.0f ? projectDefault : 30.0f) : -1.0f;
            committed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Unchecked = the project preference applies\n"
                              "(Preferences > Rendering). Checked = this\n"
                              "object uses its own distance; 0 disables the\n"
                              "LOD for it.");
        if (ov) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(scaled(110));
            float shown = v;
            if (ImGui::DragFloat((std::string("##ovr") + label).c_str(), &shown,
                                 0.5f, 0.0f, 2000.0f,
                                 shown <= 0.0f ? "off" : "%.0f units")) {
                v = shown < 0.0f ? 0.0f : shown;
            }
            committed |= ImGui::IsItemDeactivatedAfterEdit();
        }
    };
    if (animated)
        row("animation LOD", o.animLodOverride, project_.settings.animLodDistance);
    row("mesh LOD", o.meshLodOverride, project_.settings.meshLodDistance);
    if (!animated) return committed;  // yaw offset drives the skeletal path

    // Content-forward correction: a model authored facing +-X (instead of
    // the +Z the avatar drive / AI turn-to-face expect) renders turned by
    // this many degrees while every logic yaw stays pure. Applied between
    // scale and rotation, mirrored in the viewport preview.
    ImGui::SetNextItemWidth(scaled(110));
    ImGui::DragFloat("Model yaw offset", &o.modelYawOffset, 1.0f, -180.0f,
                     180.0f, "%.0f deg");
    committed |= ImGui::IsItemDeactivatedAfterEdit();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Model faces sideways in game? The content was authored\n"
            "X-forward (common Blender habit: facing the red axis).\n"
            "Set +90 or -90 - the mesh turns, facing logic stays intact.");
    return committed;
}

// Class names registered with TYRA_OBJECT_SCRIPT(...) across src/scripts,
// sorted and deduplicated. The directory is walked on every call (the
// Scripts panel already pays that price per frame); file contents are
// cached by write time so files are only re-read after edits.
std::vector<std::string> App::objectScriptNames() {
    std::vector<std::string> names;
    const std::filesystem::path dir =
        std::filesystem::path(project_.dir) / "src" / "scripts";
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) return names;
    for (std::filesystem::recursive_directory_iterator walk(dir, ec), end;
         walk != end && !ec; walk.increment(ec)) {
        const auto& entry = *walk;
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().extension() != ".cpp") continue;
        const std::string key = entry.path().string();
        const auto mtime = std::filesystem::last_write_time(entry.path(), ec);
        auto it = scriptScanCache_.find(key);
        if (it == scriptScanCache_.end() || it->second.mtime != mtime) {
            ScriptFileScan scan;
            scan.mtime = mtime;
            std::ifstream f(entry.path(), std::ios::binary);
            std::stringstream ss;
            ss << f.rdbuf();
            const std::string src = ss.str();
            static const std::string kMacro = "TYRA_OBJECT_SCRIPT(";
            for (size_t pos = src.find(kMacro); pos != std::string::npos;
                 pos = src.find(kMacro, pos + kMacro.size())) {
                const size_t end = src.find(')', pos + kMacro.size());
                if (end == std::string::npos) break;
                std::string n =
                    src.substr(pos + kMacro.size(), end - pos - kMacro.size());
                while (!n.empty() && isspace((unsigned char)n.front())) n.erase(n.begin());
                while (!n.empty() && isspace((unsigned char)n.back())) n.pop_back();
                if (!n.empty()) scan.names.push_back(n);
            }
            it = scriptScanCache_.insert_or_assign(key, std::move(scan)).first;
        }
        for (const std::string& n : it->second.names) {
            bool seen = false;
            for (const std::string& e : names) seen |= (e == n);
            if (!seen) names.push_back(n);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<std::string> App::flowVarNames(const std::string& nodeType) const {
    // int / bool / position variables live in separate namespaces
    auto ns = [](const std::string& t) {
        if (t == "SetVarInt" || t == "VarAtLeast" || t == "GetVarIntText") return 0;
        if (t == "SetVarBool" || t == "GetVarBool") return 1;
        if (t == "SetVarPos" || t == "GetVarPos") return 2;
        // Graph events are a fourth namespace with the same "exists by being
        // named" rule, so the same Pick... list serves them.
        if (t == "SendEvent" || t == "OnEvent") return 3;
        return -1;
    };
    const int want = ns(nodeType);
    std::vector<std::string> names;
    if (want < 0) return names;
    for (const SceneData& sc : project_.scenes)
        for (const SceneObject& o : sc.objects)
            for (const FlowNode& n : o.flowGraph.nodes) {
                if (ns(n.type) != want || n.str.empty()) continue;
                bool seen = false;
                for (const std::string& e : names) seen |= (e == n.str);
                if (!seen) names.push_back(n.str);
            }
    return names;
}
