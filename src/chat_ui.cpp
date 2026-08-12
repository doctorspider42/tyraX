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
#include <cctype>
#include <chrono>
#include <limits>
#include <filesystem>
#include <fstream>
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
    // A parked build: the tool started it and the turn is waiting for the
    // result rather than answering without it.
    if (chatBuildWaiting_) {
        if (runner_.state() == Runner::State::Running) return;
        chatBuildWaiting_ = false;
        const bool ok = runner_.state() == Runner::State::Success;
        std::string outcome = ok ? "The build SUCCEEDED."
                                 : "The build FAILED.";
        if (!ok) {
            // The compiler's own words, which is the whole point of waiting:
            // the tail of the build log, where the first error and the make
            // summary are.
            const std::string& log = logOut_.text;
            const size_t tail = log.size() > 4000 ? log.size() - 4000 : 0;
            outcome += " The end of the build log:\n" + log.substr(tail);
        }
        if (!chat_.messages.empty() &&
            chat_.messages.back().role == aichat::Message::Role::Tool &&
            !chat_.messages.back().calls.empty()) {
            aichat::ToolCall& c = chat_.messages.back().calls.back();
            c.result += "\n" + outcome;
            c.failed = !ok;
        }
        statusMessage_ = ok ? "AI: build succeeded" : "AI: build failed";
        // A run build is not done until the GAME is: wait for its debug channel
        // to appear, or say plainly that it never did.
        if (ok && chatBuildWasRun_ && project_.settings.liveDebug &&
            project_.settings.buildProfile == "debug") {
            chatGameWaiting_ = true;
            chatGameDeadline_ = ImGui::GetTime() + 300.0;
            statusMessage_ = "AI: waiting for the game to boot";
            aiChatPersist();
            return;
        }
        aiChatPersist();
        chatScrollPending_ = true;
        aiChatStart();
        return;
    }

    // Waiting for a launched game to say it is alive.
    if (chatGameWaiting_) {
        const bool alive = chatGameSignal() > chatGameMark_;
        if (!alive && ImGui::GetTime() < chatGameDeadline_) return;
        chatGameWaiting_ = false;
        const std::string note =
            alive ? " The game is up, in its scene and reporting - "
                    "graph_activity and game_log have something to say now."
                  : " The game never reported within five minutes: the emulator "
                    "may not have started, or it is still booting. Check "
                    "game_state before believing anything about it.";
        if (!chat_.messages.empty() &&
            chat_.messages.back().role == aichat::Message::Role::Tool &&
            !chat_.messages.back().calls.empty())
            chat_.messages.back().calls.back().result += note;
        statusMessage_ = alive ? "AI: game is up" : "AI: game did not report";
        aiChatPersist();
        chatScrollPending_ = true;
        aiChatStart();
        return;
    }
    // A parked pad script: the turn waits for it to play out, then reports what
    // the game logged while it did - that is the whole point of driving it.
    if (chatPadWaiting_) {
        if (padScriptRunning_) return;
        chatPadWaiting_ = false;
        std::string outcome = "The pad script finished.";
        const std::string path = project_.filePath("bin/log.txt");
        const size_t now = fileSizeOr0(path);
        if (now > chatPadLogMark_) {
            std::ifstream f(path, std::ios::binary);
            f.seekg((std::streamoff)chatPadLogMark_);
            std::ostringstream ss;
            ss << f.rdbuf();
            std::string added = ss.str();
            if (added.size() > 6000) added = added.substr(added.size() - 6000);
            outcome += " The game logged while it ran:\n" + added;
        } else {
            outcome +=
                " The game logged NOTHING while it ran - either nothing you were "
                "testing fired, or this build has no Log node on that path (or "
                "no game is running at all: check game_state).";
        }
        if (!chat_.messages.empty() &&
            chat_.messages.back().role == aichat::Message::Role::Tool &&
            !chat_.messages.back().calls.empty())
            chat_.messages.back().calls.back().result += "\n" + outcome;
        statusMessage_ = "AI: pad script done";
        aiChatPersist();
        chatScrollPending_ = true;
        aiChatStart();
        return;
    }
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
    // A build parks the turn: the branch at the top of this function resumes it
    // with the outcome, so the model answers knowing whether its work compiles.
    if (chatBuildWaiting_ || chatPadWaiting_ || chatGameWaiting_) return;
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
    if (key == "rings") { o.primRings = boolean(o.primRings); return true; }
    if (key == "drawDistance") {
        o.drawDistance = std::max(0.0f, num(o.drawDistance));
        return true;
    }
    if (key == "castShadow") { o.castShadow = boolean(o.castShadow); return true; }
    if (key == "footIk") { o.footIk = boolean(o.footIk); return true; }
    if (key == "bakedLighting") {
        o.bakedLighting = boolean(o.bakedLighting);
        return true;
    }
    if (key == "reflected") { o.reflected = boolean(o.reflected); return true; }
    if (key == "projShadow") { o.projShadow = boolean(o.projShadow); return true; }
    return false;
}

// The last time the running game's DEBUG CHANNEL was written - the moment the
// editor can start believing anything about it.
//
// Deliberately not bin/log.txt, which was tried first: the log's first lines are
// the engine initialising ("Audio initialized", "Pad is ready"), so a wait that
// ended there handed control back while the game was still loading its scene,
// and buttons pressed at a loading screen go nowhere. livedbg.bin appears when
// the scene is live and the graphs are being watched, which is what "the game is
// up" has to mean for anything the assistant does next.
//
// "Absent" is LLONG_MIN, NOT 0, and that is not fussiness: std::filesystem's
// file_clock epoch is not the system clock's - on this libstdc++ it sits in the
// FUTURE, so every real file time is a large NEGATIVE number. With 0 as the
// absent sentinel, "nothing here yet" compared as newer than every file the game
// could possibly write, and the wait for a launched game timed out every single
// time while the game was demonstrably running.
long long App::chatGameSignal() const {
    long long newest = std::numeric_limits<long long>::min();
    for (const char* rel : {"bin/livedbg.bin"}) {
        std::error_code ec;
        const auto t = std::filesystem::last_write_time(
            std::filesystem::path(project_.filePath(rel)), ec);
        if (ec) continue;
        const long long secs = std::chrono::duration_cast<std::chrono::seconds>(
                                   t.time_since_epoch())
                                   .count();
        if (secs > newest) newest = secs;
    }
    return newest;
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
        // One object or many: the batch form is what makes "lower all the trees"
        // a single call and a single undo step instead of twenty of each.
        std::vector<std::string> targets;
        if (const json::Value* list = aichat::argValue(c, "objects");
            list && list->type == json::Value::Type::Array)
            for (const json::Value& v : list->arr)
                if (!v.stringOr("").empty()) targets.push_back(v.str);
        if (const std::string one = aichat::argStr(c, "object"); !one.empty())
            targets.push_back(one);
        if (targets.empty())
            return fail("Name the object in \"object\", or several in "
                        "\"objects\".");
        const json::Value* props = aichat::argValue(c, "props");
        if (!props || props->type != json::Value::Type::Object || props->obj.empty())
            return fail(
                "\"props\" must be a non-empty object of property: value pairs "
                "(see OBJECT PROPERTIES).");
        std::vector<int> indices;
        for (const std::string& t : targets) {
            const int oi = aichat::findObject(sc, t);
            if (oi < 0) return fail(aichat::noSuchObject(sc, t));
            indices.push_back(oi);
        }
        const std::string was = sc.objects[indices[0]].name;
        std::vector<std::string> applied, problems;
        for (const int oi : indices) {
            SceneObject& o = sc.objects[oi];
            const std::string before = o.name;
            std::vector<std::string> mine;
            for (const auto& [key, value] : props->obj) {
                std::string err;
                if (!applyChatObjectProp(sc, o, key, value, err))
                    problems.push_back("\"" + key +
                                       "\" is not a settable property (see OBJECT "
                                       "PROPERTIES)");
                else if (!err.empty())
                    problems.push_back("\"" + key + "\" on \"" + before +
                                       "\": " + err);
                else
                    mine.push_back(key);
            }
            if (before != o.name) renameObjectRefs(sc, o, before);
            if (applied.empty()) applied = mine;
        }
        SceneObject& o = sc.objects[indices[0]];
        if (applied.empty()) {
            std::string msg = "Nothing was changed. ";
            for (const std::string& p : problems) msg += p + ". ";
            return fail(msg);
        }
        if (indices.size() > 1) {
            commitChange();
            std::string msg = "Updated " + std::to_string(indices.size()) +
                              " object(s): ";
            for (size_t i = 0; i < applied.size(); ++i)
                msg += (i ? ", " : "") + applied[i];
            msg += ".";
            for (const std::string& p : problems) msg += " Not applied: " + p + ".";
            statusMessage_ = "AI: edited " + std::to_string(indices.size()) +
                             " objects";
            return msg;
        }
        // The rename remap already ran per object above (the Properties name
        // field calls the same function) - a reference is a NAME here, so
        // skipping it would break cutscenes, mirrors and area lookups silently.
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

    if (c.name == "set_section") {
        const std::string name = aichat::argStr(c, "name");
        const int si2 = aichat::findSection(name);
        if (si2 < 0) {
            std::string s = "No section named \"" + name + "\". Sections: ";
            for (const std::string& n : aichat::sectionNames()) s += n + " ";
            return fail(s);
        }
        const json::Value* body = aichat::argValue(c, "json");
        if (!body || body->type != json::Value::Type::Object)
            return fail("\"json\" must be the section object itself.");
        const project::Section sec = (project::Section)si2;
        const std::string before = project::sectionJson(project_, sec);
        const std::string after = json::write(*body);
        // A section blob is TOTAL. A model that sends back only the entry it was
        // thinking about deletes every other one - so a write that shrinks any
        // list in it has to be meant, not implied.
        if (const std::string lost = aichat::shrinkReport(before, after);
            !lost.empty() && !aichat::argBool(c, "confirm_replace"))
            return fail("Refused: that would remove entries (" + lost +
                        "). A section is replaced whole, so send the complete "
                        "section (get_section it first), or pass "
                        "confirm_replace=true if the removal is what you mean.");
        if (!project::applySectionJson(project_, sec, after))
            return fail("That is not a valid \"" + name + "\" section object.");
        commitChange();
        statusMessage_ = "AI: section " + name;
        return "Wrote the \"" + name + "\" section." +
               (project::sectionJson(project_, sec) == before
                    ? " (It is identical to what was there - nothing changed.)"
                    : "");
    }

    if (c.name == "set_object_json") {
        const std::string target = aichat::argStr(c, "object");
        const int oi = aichat::findObject(sc, target);
        if (oi < 0) return fail(aichat::noSuchObject(sc, target));
        const json::Value* body = aichat::argValue(c, "json");
        if (!body || body->type != json::Value::Type::Object)
            return fail("\"json\" must be the object body itself, as "
                        "describe_object returned it.");
        SceneObject fresh;
        if (!project::parseObject(json::write(*body), fresh))
            return fail("That is not a valid object body.");
        SceneObject& o = sc.objects[oi];
        const std::string was = o.name;
        // Identity is not the model's to change: an object IS its id (the merge
        // and per-file storage key), and a fresh one here would orphan its file
        // and every live-link record of it.
        if (fresh.id != o.id) fresh.id = o.id;
        if (fresh.name.empty()) fresh.name = was;
        for (const SceneObject& other : sc.objects)
            if (&other != &o && other.name == fresh.name)
                return fail("Another object is already called \"" + fresh.name +
                            "\".");
        o = std::move(fresh);
        if (was != o.name) renameObjectRefs(sc, o, was);
        commitChange();
        if (si == project_.activeScene) flowPositionsApplied_ = false;
        statusMessage_ = "AI: replaced " + o.name;
        return "Replaced the stored state of \"" + was + "\"" +
               (was != o.name ? ", now called \"" + o.name + "\"" : "") + ".";
    }

    if (c.name == "duplicate_object") {
        const int oi = aichat::findObject(sc, aichat::argStr(c, "object"));
        if (oi < 0)
            return fail(aichat::noSuchObject(sc, aichat::argStr(c, "object")));
        SceneObject copy = sc.objects[oi];
        // A copy is a NEW object: an empty id makes ensureObjectIds (in
        // commitChange) issue one, which is what stops two objects sharing an
        // identity - the same rule the editor's own paste follows.
        copy.id.clear();
        std::string name = aichat::argStr(c, "name");
        if (name.empty()) {
            name = copy.name + "-copy";
            for (int n = 2;; ++n) {
                bool taken = false;
                for (const SceneObject& o : sc.objects) taken |= (o.name == name);
                if (!taken) break;
                name = copy.name + "-copy" + std::to_string(n);
            }
        } else {
            for (const SceneObject& o : sc.objects)
                if (o.name == name)
                    return fail("An object called \"" + name +
                                "\" already exists.");
        }
        copy.name = name;
        float pos[3];
        if (const json::Value* v = aichat::argValue(c, "position"))
            if (aichat::vec3(*v, pos))
                for (int i = 0; i < 3; ++i) copy.position[i] = pos[i];
        sc.objects.push_back(std::move(copy));
        if (si == project_.activeScene) {
            selectOnly((int)sc.objects.size() - 1);
            snapInsertedObject();
        }
        commitChange();
        statusMessage_ = "AI: duplicated -> " + name;
        return "Copied \"" + sc.objects[oi].name + "\" to \"" + name +
               "\" (its flow graph came along).";
    }

    if (c.name == "add_scene") {
        const std::string name = aichat::argStr(c, "name");
        bool valid = !name.empty();
        for (char ch : name)
            if (!std::isalnum((unsigned char)ch) && ch != '_' && ch != '-')
                valid = false;
        for (const SceneData& s : project_.scenes)
            if (s.name == name) valid = false;
        if (!valid)
            return fail("A scene name must be unique and made of letters, "
                        "digits, '-' and '_'.");
        SceneData fresh;
        fresh.name = name;
        fresh.terrain.width = (int)aichat::argNum(c, "width", 64);
        fresh.terrain.depth = (int)aichat::argNum(c, "depth", 64);
        fresh.terrain.enabled = aichat::argBool(c, "terrain", true);
        project_.scenes.push_back(std::move(fresh));
        // The same switch the New Scene modal does - the selection, the graph
        // canvas and the viewport all belong to the scene being left.
        project_.activeScene = (int)project_.scenes.size() - 1;
        clearSelection();
        cancelPastePlacement();
        flowGraphObject_ = -1;
        flowPositionsApplied_ = false;
        applyProjectToViewport();
        commitChange();
        statusMessage_ = "AI: scene " + name;
        return "Created scene \"" + name + "\" (" +
               std::to_string(project_.scenes.back().terrain.width) + "x" +
               std::to_string(project_.scenes.back().terrain.depth) +
               ") and switched the editor to it.";
    }

    if (c.name == "delete_scene") {
        if (!aichat::argBool(c, "confirm"))
            return fail("Refused: pass confirm=true. Deleting a scene takes "
                        "every object and flow graph in it.");
        const std::string name = aichat::argStr(c, "name");
        const int target = aichat::findScene(project_, name);
        if (target < 0) return fail(aichat::noSuchScene(project_, name));
        if (project_.scenes.size() <= 1)
            return fail("Refused: a project needs at least one scene.");
        const size_t objects = project_.scenes[target].objects.size();
        project_.scenes.erase(project_.scenes.begin() + target);
        // Scene indices are baked into generated tables, so delete is the only
        // motion there is - and everything that INDEXES the list has to shift
        // with it, exactly as the Project panel's own delete does.
        if (project_.activeScene > target) --project_.activeScene;
        else if (project_.activeScene == target) project_.activeScene = 0;
        if (project_.startScene > target) --project_.startScene;
        else if (project_.startScene == target) project_.startScene = 0;
        clearSelection();
        cancelPastePlacement();
        flowGraphObject_ = -1;
        flowPositionsApplied_ = false;
        applyProjectToViewport();
        commitChange();
        statusMessage_ = "AI: deleted scene " + name;
        return "Deleted scene \"" + name + "\" and the " +
               std::to_string(objects) +
               " object(s) in it. References to it by name (Switch Scene nodes, "
               "the start scene) were NOT rewritten.";
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

    if (c.name == "refresh_generated") {
        // Exactly what a build does first, procedural bake included - a check
        // that regenerated something DIFFERENT from what the build will compile
        // would be worth nothing.
        const std::string err = project::refreshGenerated(projectForBuild());
        if (!err.empty()) return fail("Could not regenerate: " + err);
        // The generated flow-graph TU is where a dangling reference lands: not
        // an error anywhere, just a comment saying the node was skipped.
        std::vector<std::string> unknowns;
        {
            std::ifstream f(project_.filePath("src/gen/flow_graph.gen.cpp"),
                            std::ios::binary);
            std::string line;
            while (std::getline(f, line))
                if (line.find("unknown") != std::string::npos) {
                    while (!line.empty() && (line[0] == ' ' || line[0] == '\t'))
                        line.erase(line.begin());
                    unknowns.push_back(line);
                    if (unknowns.size() >= 20) break;
                }
        }
        statusMessage_ = "AI: regenerated sources";
        if (unknowns.empty())
            return "Regenerated the game sources. No unresolved references in "
                   "the flow graphs.";
        std::string msg = "Regenerated the game sources, but " +
                          std::to_string(unknowns.size()) +
                          " node(s) reference something that does not exist - "
                          "they compile to nothing:\n";
        for (const std::string& u : unknowns) msg += "  " + u + "\n";
        return msg;
    }

    if (c.name == "build_game") {
        if (!chatAllowBuild_)
            return fail(
                "Refused: the user has not switched on \"Allow build & run\" in "
                "the AI Assistant window. Building takes minutes and a Docker "
                "container, so it is off unless they ask for it. Say what you "
                "would build and let them start it, or turn it on.");
        if (runner_.state() == Runner::State::Running)
            return fail("A build is already running - wait for it to finish.");
        const bool run = aichat::argBool(c, "run");
        // The channel's age BEFORE the launch: what makes "the game reported"
        // mean this run rather than a file left over from the last one.
        chatGameMark_ = chatGameSignal();
        runner_.buildAndRun(projectForBuild(), run);
        chatBuildWasRun_ = run;
        chatBuildWaiting_ = true;  // the loop parks until it settles
        statusMessage_ = "AI: building";
        return std::string("Build started") +
               (run ? " (the emulator will launch if it succeeds)." : ".") +
               " Waiting for it...";
    }

    if (c.name == "press_pad") {
        if (project_.settings.buildProfile != "debug" ||
            !project_.settings.remotePad)
            return fail(
                "Refused: driving the pad needs a DEBUG build with Remote Pad on "
                "(Project > Preferences > Build). A release game carries no "
                "channel to drive - that is the devkit's zero-cost promise.");
        if (padScriptRunning_) return fail("A pad script is already playing.");
        std::vector<livepad::Step> steps;
        std::string err;
        if (!livepad::parseScript(aichat::argStr(c, "script"), steps, err))
            return fail("That pad script does not parse: " + err);
        if (steps.empty()) return fail("That script does nothing.");
        double seconds = 0.0;
        for (const livepad::Step& st : steps) seconds += st.seconds;
        // What the game logs DURING the script is the observation, so remember
        // where its log ended before the first button goes down.
        chatPadLogMark_ = fileSizeOr0(project_.filePath("bin/log.txt"));
        padScript_ = std::move(steps);
        padScriptStep_ = 0;
        padScriptUntil_ = 0.0;
        padScriptRunning_ = true;
        chatPadWaiting_ = true;  // the loop parks until the script plays out
        statusMessage_ = "AI: driving the pad";
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%zu step(s), %.1f s", padScript_.size(),
                      seconds);
        return std::string("Driving the pad: ") + buf + ". Waiting...";
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
    ImGui::SameLine();
    ImGui::BeginDisabled(!chatAllowEdits_);
    if (ImGui::Checkbox("build & run", &chatAllowBuild_)) saveGlobalConfig();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Let it build the game in Docker (and launch PCSX2) when it decides\n"
            "that is what the request needs. The chat WAITS for the build and\n"
            "the assistant is given the result - including the compiler's errors,\n"
            "which is the only way it can check its own work properly.\n"
            "Off by default: a build costs minutes and a container, and unlike\n"
            "every other thing it does, that is not one Ctrl+Z away.");
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
