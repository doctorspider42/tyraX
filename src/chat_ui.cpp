// -------------------------------------------------------------------------
// The AI Assistant window (Tools > AI Assistant, docs/ai-chat.md): a chat with
// the configured AI backend that answers questions about the editor from its
// own documentation AND carries out operations in the project.
//
// This is the editor-side half of aichat.cpp. The split is the point: everything
// that is a pure function of the project (the prompt, the tool table, the reply
// parser, every read tool) lives there and is harness-testable; the EDIT and
// COMMAND tools live here, because they are the ones that need project_,
// commitChange(), the selection and the window flags.
//
// Three rules this file exists to keep:
//  - one tool call is one undo step: every edit ends in commitChange(), never in
//    saveAll() (see app.hpp - the editor does not autosave, and a chat that
//    wrote the project on every edit would take the choice away);
//  - a tool that cannot do what was asked REPORTS WHY to the model, with the
//    names that would have worked - the loop's next step is usually the fix;
//  - nothing here runs while a request is in flight: the backend runs on
//    aigen::Generator's worker thread and its reply is consumed on the UI
//    thread in aiChatTick(), which is the only place chat data meets project_.
//
// Its own translation unit for the reason every other *_ui.cpp is one (app.cpp
// used to be a 26k-line TU and therefore the whole build's critical path).
// These are still App:: members declared in app.hpp.
// -------------------------------------------------------------------------
#include "app.hpp"
#include "app_internal.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "aichat.hpp"
#include "aigen.hpp"
#include "theme.hpp"

#include <imgui.h>

namespace {

// An object type from the name the model used ("box", "Point Light", "pointlight").
bool parseObjectType(const std::string& text, PrimitiveType& out) {
    auto norm = [](const std::string& s) {
        std::string r;
        for (char c : s)
            if (c != ' ' && c != '-' && c != '_')
                r += (char)std::tolower((unsigned char)c);
        return r;
    };
    const std::string want = norm(text);
    if (want.empty()) return false;
    for (int i = 0; i < kPrimitiveTypeCount; ++i) {
        const PrimitiveType t = (PrimitiveType)i;
        if (norm(primitiveTypeName(t)) == want) {
            out = t;
            return true;
        }
    }
    return false;
}

std::string objectTypeNames() {
    std::string s;
    for (int i = 0; i < kPrimitiveTypeCount; ++i)
        s += std::string(i ? ", " : "") + primitiveTypeName((PrimitiveType)i);
    return s;
}

// "8.5k" / "312" - a token count at the width a status line can spare.
std::string tokText(long long n) {
    char buf[32];
    if (n < 10000)
        std::snprintf(buf, sizeof(buf), "%lld", n);
    else
        std::snprintf(buf, sizeof(buf), "%.1fk", (double)n / 1000.0);
    return buf;
}

std::string vecText(const float v[3]) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%g, %g, %g", v[0], v[1], v[2]);
    return buf;
}

}  // namespace

// ---------------------------------------------------------------------------
// The loop
// ---------------------------------------------------------------------------

// The current conversation to disk. Called after every turn rather than on a
// button, because a chat nobody pressed Save on is exactly the one worth having
// tomorrow - and because this is machine-global state next to editor.ini, not
// project data, so it has nothing to do with the project's dirty flag or
// saveAll() (see app.hpp).
void App::aiChatPersist() {
    if (chat_.messages.empty()) return;
    const std::string dir = aichat::chatDir(project_.projectId);
    if (dir.empty()) return;  // no config home: nowhere to keep it
    const std::string written =
        aichat::saveChat(dir, chatFile_, chat_, project_.name);
    if (written.empty()) return;
    chatFile_ = written;
    chatHistoryScanned_ = false;  // the list the popup shows just changed
    // The cap is stated rather than silent: history that quietly stops keeping
    // things is worse than history with a known limit.
    if (const int gone = aichat::pruneChats(dir, kChatHistoryKeep); gone > 0)
        statusMessage_ = "AI chat history: dropped " + std::to_string(gone) +
                         " oldest chat(s) past " +
                         std::to_string(kChatHistoryKeep);
}

void App::aiChatReset() {
    if (chatGen_.busy()) chatGen_.cancel();
    aiChatPersist();  // never lose the one being replaced
    chatInFlight_ = false;
    chatStep_ = 0;
    chat_.messages.clear();
    chatFile_.clear();
    chatError_.clear();
    chatCompactNote_.clear();
}

void App::aiChatOpen(const std::string& file) {
    if (chatGen_.busy()) chatGen_.cancel();
    aiChatPersist();  // the one being left keeps its own file
    aichat::Conversation loaded;
    if (!aichat::loadChat(file, loaded)) {
        chatError_ = "That saved chat could not be read: " + file;
        return;
    }
    chat_ = std::move(loaded);
    chatFile_ = file;
    chatInFlight_ = false;
    chatCompactNote_.clear();
    // A reopened chat starts a fresh turn budget: what it spent before it was
    // put down says nothing about what the next question needs.
    chatStep_ = 0;
    chatError_.clear();
    chatScrollPending_ = true;
}

void App::aiChatSend(const std::string& text) {
    if (text.empty() || chatGen_.busy()) return;
    aichat::Message m;
    m.role = aichat::Message::Role::User;
    m.text = text;
    chat_.messages.push_back(std::move(m));
    chatError_.clear();
    chatStep_ = 0;  // a fresh user turn gets the full tool-step budget
    // Compact FIRST when the conversation has outgrown its budget: the
    // alternative is the transcript silently dropping its oldest turns, which
    // is the same loss without the recap.
    const aichat::ContextStats st =
        aichat::contextStats(aiChatSystemPrompt(), chat_);
    if (st.overBudget && aichat::compactableCount(chat_) > 0) {
        chatCompactPending_ = true;
        chatCompactThenSend_ = true;
    }
    aiChatStart();
}

// The instruction text for a chat request. Split out because two things need
// it: the request itself, and the context meter, which has to measure exactly
// what would be sent rather than an approximation of it.
std::string App::aiChatSystemPrompt() {
    aichat::Context ctx;
    ctx.project = hasProject_ ? &project_ : nullptr;
    ctx.selectedObject = selectedObject_;
    ctx.allowEdits = chatAllowEdits_;
    ctx.dirty = dirty_;
    ctx.windows = chatWindowKeys();
    return aichat::systemPrompt(ctx);
}

void App::aiChatStart() {
    // A compaction is a request of its own, with its own small prompt: it reads
    // the old conversation and writes a recap. The answer to the user follows it
    // (chatCompactThenSend_) or does not (the manual Compact button).
    if (chatCompactPending_) {
        chatCompactPending_ = false;
        chatCompactCount_ = aichat::compactableCount(chat_);
        if (chatCompactCount_ > 0) {
            chatCompacting_ = true;
            chatGen_.start(globalAi_, aichat::compactSystemPrompt(),
                           aichat::compactUserPrompt(chat_, chatCompactCount_));
            chatInFlight_ = true;
            chatScrollPending_ = true;
            return;
        }
        // Nothing worth folding: fall through to the answer, if one is due.
        if (!chatCompactThenSend_) return;
    }
    chatCompacting_ = false;
    // The transcript rides in the "user prompt" slot of the same runner the
    // flow-graph generator uses - one backend execution path for the editor
    // (temp file + stdin for the CLIs, a curl config for the API), so a backend
    // that works for one works for the other.
    chatGen_.start(globalAi_, aiChatSystemPrompt(), aichat::transcript(chat_));
    chatInFlight_ = true;
    chatScrollPending_ = true;
}

void App::aiChatTick() {
    if (!chatInFlight_ || chatGen_.busy()) return;
    chatInFlight_ = false;

    // What that request cost, from the backend itself where it says so. Counted
    // for compactions too - they are the same money.
    if (const aigen::Usage u = chatGen_.usage(); u.real) {
        chatTokensIn_ += u.inputTokens;
        chatTokensOut_ += u.outputTokens;
        chatCostUsd_ += u.costUsd;
        chatUsageReal_ = true;
    }

    if (chatGen_.state() != aigen::Generator::State::Success) {
        chatError_ = chatGen_.error();
        // A failed compaction must not strand the user's question: it is an
        // optimisation, so the answer goes ahead with the full transcript (the
        // trimming in transcript() still bounds it).
        if (chatCompacting_) {
            chatCompacting_ = false;
            chatCompactNote_ = "Could not compact the conversation, so it was "
                               "sent as it is.";
            if (chatCompactThenSend_) {
                chatCompactThenSend_ = false;
                chatError_.clear();
                aiChatStart();
            }
        }
        chatScrollPending_ = true;
        return;
    }

    if (chatCompacting_) {
        chatCompacting_ = false;
        const size_t before = chat_.messages.size();
        // The recap is prose, not an envelope - but a model that answered with
        // one anyway should not have its JSON pasted into the transcript.
        std::string recap;
        std::vector<aichat::ToolCall> ignored;
        aichat::parseReply(chatGen_.reply(), recap, ignored);
        aichat::applyCompaction(chat_, chatCompactCount_, recap);
        const size_t folded = before - chat_.messages.size() + 1;
        chatCompactNote_ = "Compacted " + std::to_string(folded) +
                           " earlier message(s) into a summary.";
        statusMessage_ = "AI chat: " + chatCompactNote_;
        aiChatPersist();
        chatScrollPending_ = true;
        if (chatCompactThenSend_) {
            chatCompactThenSend_ = false;
            aiChatStart();
        }
        return;
    }

    aichat::Message reply;
    reply.role = aichat::Message::Role::Assistant;
    aichat::parseReply(chatGen_.reply(), reply.text, reply.calls);
    if (reply.text.empty() && reply.calls.empty()) {
        chatError_ = "The backend returned an empty reply.";
        chatScrollPending_ = true;
        return;
    }
    const std::vector<aichat::ToolCall> calls = reply.calls;
    chat_.messages.push_back(std::move(reply));
    chatScrollPending_ = true;
    aiChatPersist();
    if (calls.empty()) return;  // prose only: the turn is over

    // A model that keeps calling tools has either lost the thread or is stuck in
    // a loop; either way the user is waiting on a backend that costs money and
    // minutes, so the step budget is per user turn and the model is TOLD it ran
    // out (it then answers with what it has).
    if (chatStep_ >= kChatMaxSteps) {
        // ...and the wrap-up round is itself budgeted. Telling the model to stop
        // and then re-sending is only safe once: a model that answers the note
        // with more tool calls would otherwise be asked again forever, one
        // backend request per round, with nothing on screen to say why.
        if (chatStep_ > kChatMaxSteps) {
            chatError_ = "Stopped: the assistant kept asking for tools after " +
                         std::to_string(kChatMaxSteps) +
                         " rounds. Send another message to continue.";
            chatScrollPending_ = true;
            return;
        }
        aichat::Message note;
        note.role = aichat::Message::Role::Tool;
        note.calls = calls;
        for (aichat::ToolCall& c : note.calls) {
            c.failed = true;
            c.result = "Step limit reached (" + std::to_string(kChatMaxSteps) +
                       " tool rounds this turn). No more tools will run - answer "
                       "the user with what you have, or ask them to continue.";
        }
        chat_.messages.push_back(std::move(note));
        aiChatStart();
        ++chatStep_;
        return;
    }

    aichat::Message results;
    results.role = aichat::Message::Role::Tool;
    results.calls = calls;
    for (aichat::ToolCall& c : results.calls) c.result = runChatTool(c);
    chat_.messages.push_back(std::move(results));
    aiChatPersist();  // what the tools did survives a crash mid-loop
    ++chatStep_;
    aiChatStart();  // hand the results back and let it continue
}

// ---------------------------------------------------------------------------
// Tool execution
// ---------------------------------------------------------------------------

// One set_object property. Returns false when `key` has no branch here, which
// is reported to the model as unhandled - aichat::objectProps() is the list both
// sides read, so a row added there needs a branch here.
bool App::applyChatObjectProp(SceneData& sc, SceneObject& o,
                              const std::string& key, const json::Value& v,
                              std::string& err) {
    // Through aichat's coercions, not json::Value's raw accessors: "4" and 4
    // must mean the same here as they do in a tool argument.
    auto num = [&v](float fallback) { return (float)aichat::numberOf(v, fallback); };
    auto boolean = [&v](bool fallback) { return aichat::boolOf(v, fallback); };
    auto text = [&v]() { return aichat::stringOf(v); };

    if (key == "name") {
        const std::string name = text();
        if (name.empty()) {
            err = "an object name cannot be empty";
            return true;
        }
        for (const SceneObject& other : sc.objects)
            if (&other != &o && other.name == name) {
                err = "another object is already called \"" + name + "\"";
                return true;
            }
        o.name = name;
        return true;
    }
    if (key == "type") {
        PrimitiveType t;
        if (!parseObjectType(text(), t)) {
            err = "unknown type \"" + text() + "\"; valid: " + objectTypeNames();
            return true;
        }
        o.type = t;
        // Box-like and curved shapes read primDetail differently, so a type
        // change has to re-clamp it or a sphere's 16 segments become 16
        // subdivisions per box edge (3072 triangles - the SavePoint bug).
        o.primDetail = clampPrimDetail(t, o.primDetail);
        return true;
    }
    if (key == "position" || key == "rotation" || key == "scale" ||
        key == "color") {
        float* dst = key == "position" ? o.position
                     : key == "rotation" ? o.rotation
                     : key == "scale"  ? o.scale
                                       : o.color;
        if (!aichat::vec3(v, dst)) {
            err = key + " must be [x, y, z]";
            return true;
        }
        return true;
    }
    if (key == "model") {
        o.modelPath = text();
        return true;
    }
    if (key == "material") {
        o.materialPath = text();
        return true;
    }
    if (key == "layer") {
        const std::string layer = text();
        if (!layer.empty()) {
            bool found = false;
            for (const SceneLayer& l : sc.layers) found |= (l.name == layer);
            if (!found) {
                err = "there is no streaming layer called \"" + layer + "\"";
                return true;
            }
        }
        o.layer = layer;
        return true;
    }
    if (key == "usable") { o.usable = boolean(o.usable); return true; }
    if (key == "pickable") { o.pickable = boolean(o.pickable); return true; }
    if (key == "physics") { o.physics = boolean(o.physics); return true; }
    if (key == "saveState") { o.saveState = boolean(o.saveState); return true; }
    if (key == "collision") {
        const std::string mode = text();
        if (mode == "box") o.collisionMode = 0;
        else if (mode == "mesh") o.collisionMode = 1;
        else if (mode == "none") o.collisionMode = 2;
        else err = "collision must be \"box\", \"mesh\" or \"none\"";
        return true;
    }
    if (key == "detail") {
        o.primDetail = clampPrimDetail(o.type, (int)num((float)o.primDetail));
        return true;
    }
    if (key == "drawDistance") {
        o.drawDistance = std::max(0.0f, num(o.drawDistance));
        return true;
    }
    if (key == "castShadow") { o.castShadow = boolean(o.castShadow); return true; }
    if (key == "bakedLighting") {
        o.bakedLighting = boolean(o.bakedLighting);
        return true;
    }
    if (key == "reflected") { o.reflected = boolean(o.reflected); return true; }
    if (key == "projShadow") { o.projShadow = boolean(o.projShadow); return true; }
    return false;
}

std::string App::runChatTool(aichat::ToolCall& c) {
    auto fail = [&c](const std::string& msg) {
        c.failed = true;
        return msg;
    };
    c.failed = false;

    if (const std::string err = aichat::validate(c); !err.empty()) return fail(err);
    const aichat::Tool* t = aichat::tool(c.name);

    // With no project, the two tools that ask nothing of one still work - that
    // is the difference between an assistant that can explain the editor on the
    // welcome screen and one that can only apologise.
    if (!hasProject_ && c.name != "read_doc" && c.name != "list_node_types")
        return fail(
            "No project is open in the editor, so only read_doc and "
            "list_node_types work. Ask the user to open or create one (the "
            "welcome screen, or File > Open Project).");

    if (t->kind != aichat::ToolKind::Read && !chatAllowEdits_)
        return fail(
            "Refused: the user has \"Allow project edits\" switched off in the "
            "AI Assistant window, so nothing may change the project or the "
            "editor. Answer with read-only tools and say what they would need "
            "to turn on.");

    if (t->kind == aichat::ToolKind::Read)
        return aichat::runReadTool(project_, c, c.failed);

    // Every edit/command below resolves its scene the same way the read tools
    // do: named, or the one the editor is showing.
    const std::string sceneArg = aichat::argStr(c, "scene");
    const int si = aichat::findScene(project_, sceneArg);
    if (si < 0) return fail(aichat::noSuchScene(project_, sceneArg));
    SceneData& sc = project_.scenes[si];
    // add / delete / select go through the editor's own verbs, which operate on
    // the scene the editor is SHOWING (that is what buys them the placement
    // snap, the volume cleanup and the selection). A "scene" argument naming
    // another one would be silently ignored, so it is refused instead.
    if (si != project_.activeScene &&
        (c.name == "add_object" || c.name == "delete_object" ||
         c.name == "select_object"))
        return fail("\"" + c.name +
                    "\" works on the scene the editor is showing (\"" +
                    project_.active().name +
                    "\"). Call set_scene first, then repeat this call.");

    // --- EDIT --------------------------------------------------------------
    if (c.name == "add_object") {
        PrimitiveType type;
        if (!parseObjectType(aichat::argStr(c, "type"), type))
            return fail("Unknown object type \"" + aichat::argStr(c, "type") +
                        "\". Valid types: " + objectTypeNames() + ".");
        const std::string name = aichat::argStr(c, "name");
        if (!name.empty())
            for (const SceneObject& o : project_.objects())
                if (o.name == name)
                    return fail("An object called \"" + name +
                                "\" already exists in the active scene - pick "
                                "another name.");
        const std::string model = aichat::argStr(c, "model");
        if (type == PrimitiveType::Model) {
            if (model.empty())
                return fail(
                    "type \"model\" needs a \"model\" argument: the "
                    "res/models/... path of an imported asset (project_summary "
                    "lists the ones already used; importing is the user's job, "
                    "through the Asset Browser).");
            std::error_code ec;
            if (!std::filesystem::exists(project_.filePath(model), ec))
                return fail("There is no asset at \"" + model +
                            "\" in this project. The user imports models with "
                            "the Asset Browser (Tools > Asset Browser).");
        } else if (!model.empty()) {
            return fail("Only type \"model\" takes a \"model\" argument.");
        }

        float pos[3] = {0.0f, 0.0f, 0.0f};
        bool placed = false;
        if (const json::Value* v = aichat::argValue(c, "position"))
            placed = aichat::vec3(*v, pos);

        // Both insert paths leave the fresh object last, selected and rested on
        // the surface under it; `commit = false` keeps this whole call one undo
        // step (the addObject convention - see app.hpp).
        if (type == PrimitiveType::Model)
            addModelObject(model, placed ? pos : nullptr, /*commit=*/false);
        else
            addObject(type, /*commit=*/false);
        SceneObject& fresh = project_.objects().back();
        if (!name.empty()) fresh.name = name;
        if (placed && type != PrimitiveType::Model) {
            for (int i = 0; i < 3; ++i) fresh.position[i] = pos[i];
            snapInsertedObject();  // re-rest it where it actually landed
        }
        commitChange();
        statusMessage_ = "AI: added " + fresh.name;
        return "Added \"" + fresh.name + "\" (" + primitiveTypeName(fresh.type) +
               ") at " + vecText(fresh.position) + " in scene \"" +
               project_.active().name +
               "\"; it rests on the surface under it and is now selected.";
    }

    if (c.name == "set_object") {
        const std::string target = aichat::argStr(c, "object");
        const int oi = aichat::findObject(sc, target);
        if (oi < 0) return fail(aichat::noSuchObject(sc, target));
        const json::Value* props = aichat::argValue(c, "props");
        if (!props || props->type != json::Value::Type::Object || props->obj.empty())
            return fail(
                "\"props\" must be a non-empty object of property: value pairs "
                "(see OBJECT PROPERTIES).");
        SceneObject& o = sc.objects[oi];
        const std::string was = o.name;
        std::vector<std::string> applied, problems;
        for (const auto& [key, value] : props->obj) {
            std::string err;
            if (!applyChatObjectProp(sc, o, key, value, err))
                problems.push_back("\"" + key +
                                   "\" is not a settable property (see OBJECT "
                                   "PROPERTIES)");
            else if (!err.empty())
                problems.push_back("\"" + key + "\": " + err);
            else
                applied.push_back(key);
        }
        if (applied.empty()) {
            std::string msg = "Nothing was changed. ";
            for (const std::string& p : problems) msg += p + ". ";
            return fail(msg);
        }
        // A rename retargets every by-name reference through the editor's own
        // remap (the Properties name field calls the same function) - a
        // reference is a NAME here, so skipping it would break cutscenes,
        // mirrors and area lookups silently.
        if (was != o.name) renameObjectRefs(sc, o, was);
        commitChange();
        std::string msg = "Updated \"" + was + "\": ";
        for (size_t i = 0; i < applied.size(); ++i)
            msg += (i ? ", " : "") + applied[i];
        msg += ".";
        if (was != o.name)
            msg += " Renamed to \"" + o.name +
                   "\"; references to it by name (cutscene tracks, mirror and "
                   "scroller lists, portal links, area lookups) were retargeted, "
                   "but a flow node naming it in free text was not.";
        for (const std::string& p : problems) msg += " Not applied: " + p + ".";
        statusMessage_ = "AI: edited " + o.name;
        return msg;
    }

    if (c.name == "delete_object") {
        const int oi = aichat::findObject(sc, aichat::argStr(c, "object"));
        if (oi < 0)
            return fail(aichat::noSuchObject(sc, aichat::argStr(c, "object")));
        const std::string name = sc.objects[oi].name;
        // Through the editor's own verb: it takes a procedural volume's baked
        // chunks and mesh files with it, and commits as one undo step.
        selectOnly(oi);
        deleteSelectedObjects();
        statusMessage_ = "AI: deleted " + name;
        return "Deleted \"" + name + "\" from scene \"" + sc.name + "\".";
    }

    if (c.name == "set_graph") {
        const std::string target = aichat::argStr(c, "object");
        const int oi = aichat::findObject(sc, target);
        if (oi < 0) return fail(aichat::noSuchObject(sc, target));
        const json::Value* g = aichat::argValue(c, "graph");
        if (!g || g->type != json::Value::Type::Object)
            return fail("\"graph\" must be the graph object itself, not a string "
                        "or a list.");
        FlowGraph fg;
        std::string warnings;
        // The same validation the Flow Graph editor and --apply-graph run:
        // unknown node types are an error, links that break the pin rules are
        // dropped with a warning, missing positions get an automatic layout.
        if (const std::string err = aigen::parseGraphJson(*g, fg, &warnings);
            !err.empty())
            return fail(err);
        SceneObject& o = sc.objects[oi];
        const bool append = aichat::argBool(c, "append");
        const size_t before = o.flowGraph.nodes.size();
        if (append) aigen::appendGraph(o.flowGraph, fg);
        else o.flowGraph = fg;
        commitChange();
        // Show it: focus that object's graph and let imnodes re-read the node
        // positions (they are only pushed into the canvas once per target).
        if (si == project_.activeScene) {
            flowGraphObject_ = oi;
            flowPositionsApplied_ = false;
        }
        statusMessage_ = "AI: graph -> " + o.name;
        return std::string(append ? "Appended to" : "Replaced") +
               " the flow graph of \"" + o.name + "\" (" +
               std::to_string(before) + " -> " +
               std::to_string(o.flowGraph.nodes.size()) + " nodes, " +
               std::to_string(o.flowGraph.links.size()) + " links)." +
               (warnings.empty() ? "" : " " + warnings);
    }

    // --- COMMAND -----------------------------------------------------------
    if (c.name == "select_object") {
        const int oi = aichat::findObject(sc, aichat::argStr(c, "object"));
        if (oi < 0)
            return fail(aichat::noSuchObject(sc, aichat::argStr(c, "object")));
        selectOnly(oi);
        statusMessage_ = "AI: selected " + sc.objects[oi].name;
        return "Selected \"" + sc.objects[oi].name +
               "\" - the Properties panel now shows it.";
    }

    if (c.name == "set_scene") {
        const std::string name = aichat::argStr(c, "name");
        const int target = aichat::findScene(project_, name);
        if (target < 0) return fail(aichat::noSuchScene(project_, name));
        if (target == project_.activeScene)
            return "Scene \"" + name + "\" is already the active one.";
        // The Project panel's own switch: the selection, a staged paste and the
        // graph canvas all belong to the scene being left, and terrain/lighting
        // are per scene. Which scene is active is view state, not an edit.
        project_.activeScene = target;
        clearSelection();
        cancelPastePlacement();
        flowGraphObject_ = -1;
        flowPositionsApplied_ = false;
        applyProjectToViewport();
        statusMessage_ = "AI: scene " + name;
        return "Switched the editor to scene \"" + name + "\" (" +
               std::to_string(project_.objects().size()) + " objects).";
    }

    if (c.name == "open_window") {
        const std::string key = aichat::argStr(c, "window");
        bool* flag = showFlagForKey(key);
        if (!flag) {
            std::string s = "No editor window keyed \"" + key + "\". Keys: ";
            const std::vector<std::string> keys = chatWindowKeys();
            for (size_t i = 0; i < keys.size(); ++i)
                s += std::string(i ? ", " : "") + keys[i];
            return fail(s);
        }
        *flag = true;
        // Two windows need their content staged the same way the Tools menu
        // stages it, or they open empty.
        if (key == "assets") scanAssetTree();
        if (key == "tree") treePreviewDirty_ = true;
        statusMessage_ = "AI: opened " + key;
        return "Opened the \"" + key + "\" window.";
    }

    if (c.name == "save_project") {
        if (!dirty_) return "There is nothing to save - the project is unchanged.";
        saveAll("Saved (AI Assistant)");
        return "Saved the project to " + project_.dir + ".";
    }

    return fail("\"" + c.name + "\" is not implemented in this editor build.");
}

// ---------------------------------------------------------------------------
// The window
// ---------------------------------------------------------------------------

// Saved conversations for THIS project (grouped by its stable projectId), newest
// first. Read from disk when the popup opens, not every frame: a chat list is a
// directory scan plus one JSON parse per file.
void App::drawAiChatHistory() {
    if (!ImGui::BeginPopup("##chathistory")) return;
    if (!chatHistoryScanned_) {
        chatHistory_ = aichat::listChats(aichat::chatDir(project_.projectId));
        chatHistoryScanned_ = true;
    }
    ImGui::TextDisabled("Saved chats for this project");
    ImGui::Separator();
    if (chatHistory_.empty()) {
        ImGui::TextDisabled("Nothing yet - a chat is saved after its first reply.");
        ImGui::EndPopup();
        return;
    }
    const theme::Semantics& sem = theme::semantics();
    std::string deleted;
    for (size_t i = 0; i < chatHistory_.size(); ++i) {
        const aichat::ChatRecord& r = chatHistory_[i];
        ImGui::PushID((int)i);
        const bool current = r.file == chatFile_;
        if (current) ImGui::PushStyleColor(ImGuiCol_Text, sem.accent);
        // Selectable rather than a button: the row is the whole width, which is
        // what makes a list of titles clickable at the length titles run to.
        if (ImGui::Selectable(r.title.c_str(), current) && !current)
            aiChatOpen(r.file);
        if (current) ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("- %s, %d message(s)", aichat::chatAge(r.ageSeconds).c_str(),
                            r.messages);
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) deleted = r.file;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete this chat");
        ImGui::PopID();
    }
    if (!deleted.empty()) {
        aichat::deleteChat(deleted);
        // Deleting the open one leaves the transcript on screen but unowned: the
        // next turn writes a new file rather than resurrecting the deleted path.
        if (deleted == chatFile_) chatFile_.clear();
        chatHistoryScanned_ = false;
    }
    ImGui::EndPopup();
}

void App::drawAiChatWindow() {
    if (!showAiChat_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(520), scaled(620)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("AI Assistant", &showAiChat_)) {
        ImGui::End();
        return;
    }
    const theme::Semantics& sem = theme::semantics();
    const bool busy = chatGen_.busy();

    // --- header: which backend answers, and whether it may act
    ImGui::TextDisabled("%s", aigen::backendLabel(globalAi_.backend));
    if (!globalAi_.model.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("- %s", globalAi_.model.c_str());
    }
    if (globalAi_.thinking) {
        ImGui::SameLine();
        ImGui::TextDisabled("- thinking");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "The backend is the machine-wide one in Edit > Preferences >\n"
            "AI assistant - the same setting the Flow Graph window's\n"
            "\"Generate with AI\" uses.");
    ImGui::SameLine(ImGui::GetContentRegionMax().x - scaled(160));
    if (ImGui::Button("History", ImVec2(scaled(70), 0))) {
        chatHistoryScanned_ = false;  // re-scan on every open, never per frame
        ImGui::OpenPopup("##chathistory");
    }
    ImGui::SameLine();
    if (ImGui::Button("New chat", ImVec2(scaled(80), 0))) aiChatReset();
    // The popup is submitted right after the button that opens it, with the SAME
    // string - ImGui hashes the label, and a popup opened under one id and drawn
    // under another is open-but-invisible AND swallows every click in the window.
    drawAiChatHistory();

    if (ImGui::Checkbox("Allow project edits", &chatAllowEdits_))
        saveGlobalConfig();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "On: the assistant may add, change and delete objects, write flow\n"
            "graphs, switch scenes and open windows. Every change is ONE undo\n"
            "step (Ctrl+Z) and nothing is written to disk until you save.\n"
            "Off: it can only read the project and the documentation.");
    ImGui::Separator();

    // --- transcript
    const float inputH = ImGui::GetFrameHeightWithSpacing() * 3.0f + scaled(30);
    if (ImGui::BeginChild("##chatlog", ImVec2(0, -inputH))) {
        if (chat_.messages.empty()) {
            ImGui::TextWrapped(
                "Ask about the editor, or ask for something to be done. The "
                "assistant reads the editor's own documentation and can act on "
                "the open project.");
            ImGui::Spacing();
            static const char* const kExamples[] = {
                "What is a Procedural volume for?",
                "Add a usable box called lever at 4, 0, -6",
                "Give the lever a graph: on Used, play a sound",
                "How do menu stylesheets work?",
            };
            for (int i = 0; i < (int)IM_ARRAYSIZE(kExamples); ++i) {
                ImGui::PushID(i);
                if (ImGui::Button(kExamples[i]) && !busy) aiChatSend(kExamples[i]);
                ImGui::PopID();
            }
        }
        for (size_t i = 0; i < chat_.messages.size(); ++i) {
            const aichat::Message& m = chat_.messages[i];
            ImGui::PushID((int)i);
            switch (m.role) {
                case aichat::Message::Role::User:
                    ImGui::TextColored(sem.accent,
                                       "You");
                    ImGui::TextWrapped("%s", m.text.c_str());
                    break;
                case aichat::Message::Role::Assistant:
                    ImGui::TextColored(sem.ok,
                                       "Assistant");
                    if (!m.text.empty()) ImGui::TextWrapped("%s", m.text.c_str());
                    for (const aichat::ToolCall& c : m.calls)
                        ImGui::TextDisabled("-> %s %s", c.name.c_str(),
                                            aichat::argsText(c).c_str());
                    break;
                case aichat::Message::Role::Summary:
                    // Collapsed: it stands in for messages that are gone, and
                    // the point of compacting was to stop reading them.
                    ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
                    if (ImGui::TreeNodeEx("Earlier conversation, summarised",
                                          ImGuiTreeNodeFlags_SpanAvailWidth)) {
                        ImGui::PushTextWrapPos(0.0f);
                        ImGui::TextUnformatted(m.text.c_str());
                        ImGui::PopTextWrapPos();
                        ImGui::TreePop();
                    }
                    ImGui::PopStyleColor();
                    break;
                case aichat::Message::Role::Tool:
                    // Collapsed by default: the results are for the model, and a
                    // 40 KB doc page would bury the conversation. Open one to see
                    // exactly what the assistant was told.
                    for (size_t k = 0; k < m.calls.size(); ++k) {
                        const aichat::ToolCall& c = m.calls[k];
                        ImGui::PushID((int)k);
                        ImGui::PushStyleColor(ImGuiCol_Text,
                                              c.failed ? sem.warn : sem.textDim);
                        const std::string label =
                            c.name + " " + aichat::argsText(c) +
                            (c.failed ? "  [failed]" : "");
                        const bool open = ImGui::TreeNodeEx(
                            label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
                        ImGui::PopStyleColor();
                        if (open) {
                            ImGui::PushTextWrapPos(0.0f);
                            ImGui::TextUnformatted(c.result.c_str());
                            ImGui::PopTextWrapPos();
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                    break;
            }
            ImGui::PopID();
            ImGui::Spacing();
        }
        if (!chatCompactNote_.empty()) ImGui::TextDisabled("%s", chatCompactNote_.c_str());
        if (busy) {
            // Three dots that fill up: the backend gives no progress, so the only
            // honest signal is "still running".
            const int dots = (int)(ImGui::GetTime() * 2.0) % 4;
            ImGui::TextColored(sem.accentMuted, "%s%s",
                               chatCompacting_ ? "Compacting the conversation"
                                               : "Thinking",
                               std::string(dots, '.').c_str());
        }
        if (!chatError_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  sem.danger);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(chatError_.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        if (chatScrollPending_) {
            ImGui::SetScrollHereY(1.0f);
            chatScrollPending_ = false;
        }
    }
    ImGui::EndChild();

    // --- input
    const bool send =
        ImGui::InputTextMultiline(
            "##chatinput", chatInputBuf_, sizeof(chatInputBuf_),
            ImVec2(-FLT_MIN, ImGui::GetFrameHeightWithSpacing() * 2.0f),
            ImGuiInputTextFlags_CtrlEnterForNewLine |
                ImGuiInputTextFlags_EnterReturnsTrue) &&
        !busy;
    if (ImGui::IsItemHovered() && chatInputBuf_[0] == '\0')
        ImGui::SetTooltip("Enter sends, Ctrl+Enter starts a new line.");
    const bool empty = chatInputBuf_[0] == '\0';
    if (busy) {
        if (ImGui::Button("Cancel", ImVec2(scaled(110), 0))) {
            chatGen_.cancel();
            chatError_ = "Cancelled.";
        }
    } else {
        ImGui::BeginDisabled(empty);
        const bool clicked = ImGui::Button("Send", ImVec2(scaled(110), 0));
        ImGui::EndDisabled();
        if ((clicked || send) && !empty) {
            const std::string text = chatInputBuf_;
            chatInputBuf_[0] = '\0';
            aiChatSend(text);
        }
    }
    if (!chatAllowEdits_) {
        ImGui::SameLine();
        ImGui::TextDisabled("read-only");
    } else if (chatStep_ > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("%d tool round(s) this turn", chatStep_);
    }

    // --- what this is costing. Two different kinds of number, kept apart on
    // purpose: the context is an ESTIMATE of a request not sent yet, the session
    // totals are what the backend actually reported.
    const aichat::ContextStats st = aichat::contextStats(aiChatSystemPrompt(), chat_);
    const size_t foldable = aichat::compactableCount(chat_);
    ImGui::PushStyleColor(ImGuiCol_Text, st.overBudget ? sem.warn : sem.textDim);
    ImGui::Text("ctx ~%s", tokText(st.totalTokens).c_str());
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "What the next request carries, estimated at 4 bytes per token:\n"
            "  instructions  ~%s  (tools, the documentation index, your project)\n"
            "  conversation  ~%s  (%d message(s))\n"
            "The instructions are re-sent every time - none of the backends keeps\n"
            "state between calls - so a question costs the instructions plus the\n"
            "conversation so far. Past ~%s of conversation it is compacted: the\n"
            "model writes a recap of the older messages and that replaces them.%s",
            tokText(st.promptTokens).c_str(), tokText(st.transcriptTokens).c_str(),
            st.messages, tokText(st.compactAtTokens).c_str(),
            st.overBudget
                ? "\n\nIt is past that now - the next question compacts first."
                : "");
    if (chatUsageReal_) {
        ImGui::SameLine();
        ImGui::TextDisabled("- session %s in / %s out", tokText(chatTokensIn_).c_str(),
                            tokText(chatTokensOut_).c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Reported by the backend itself, for every request this editor\n"
                "session made (this chat and the ones before it), input including\n"
                "what the prompt cache served.");
        if (chatCostUsd_ > 0.0) {
            ImGui::SameLine();
            ImGui::TextDisabled("- $%.2f", chatCostUsd_);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "What the backend says it billed this editor session.\n"
                    "On a subscription that is what it WOULD have cost on the API.");
        }
    } else if (!chat_.messages.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("- this backend reports no usage");
    }
    if (foldable > 0) {
        ImGui::SameLine(ImGui::GetContentRegionMax().x - scaled(78));
        ImGui::BeginDisabled(busy);
        if (ImGui::SmallButton("Compact")) {
            chatCompactPending_ = true;
            chatCompactThenSend_ = false;
            aiChatStart();
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Fold the %d oldest message(s) into a summary now, instead of\n"
                "waiting until the conversation outgrows its budget. Costs one\n"
                "backend request; the recent turns are kept word for word.",
                (int)foldable);
    }
    ImGui::End();
}
