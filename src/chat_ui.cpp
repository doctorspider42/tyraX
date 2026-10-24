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
#include <cstring>
#include <chrono>
#include <limits>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "aichat.hpp"
#include "aigen.hpp"
#include "json.hpp"
#include "theme.hpp"
#include "toolkit.hpp"

#include <imgui.h>
#include <imgui_internal.h>

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
    chatTurns_.clear();
}

// A compaction folds the oldest messages into one summary, which renumbers
// every message after it. Only the newest turn's info is worth carrying over
// (it is the one an Undo can still apply to): re-key it to the last User.
void App::aiChatRekeyTurns() {
    ChatTurnInfo keep;
    bool have = false;
    if (auto t = chatTurns_.find(chatTurnUser_); t != chatTurns_.end()) {
        keep = t->second;
        have = true;
    }
    chatTurns_.clear();
    for (size_t i = chat_.messages.size(); i-- > 0;)
        if (chat_.messages[i].role == aichat::Message::Role::User) {
            chatTurnUser_ = i;
            if (have) chatTurns_[i] = keep;
            break;
        }
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
    chatTurns_.clear();
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
    // What this turn may do and where the undo history stood, for its change
    // card (session-only: a reopened chat shows no Undo for old turns).
    chatTurnUser_ = chat_.messages.size() - 1;
    ChatTurnInfo turn;
    turn.mode = !chatAllowEdits_ ? 0 : chatAllowBuild_ ? 2 : 1;
    turn.undoBase = turn.undoEnd = history_.index();
    chatTurns_[chatTurnUser_] = turn;
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
        aiChatRekeyTurns();
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
    if (auto t = chatTurns_.find(chatTurnUser_); t != chatTurns_.end())
        t->second.undoEnd = history_.index();
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
        else if (mode == "invisible" && o.type == PrimitiveType::Box) o.collisionMode = 3;
        else err = "collision must be \"box\", \"mesh\", \"none\", or \"invisible\" (Box only)";
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

        // All three insert paths leave the fresh object last and selected;
        // `commit = false` keeps this whole call one undo step (the addObject
        // convention - see app.hpp).
        const bool volume = type == PrimitiveType::Scatter;
        if (type == PrimitiveType::Model)
            addModelObject(model, placed ? pos : nullptr, /*commit=*/false);
        else if (volume)
            // A Procedural volume is a REGION, not a shape: the editor's own
            // verb sizes it over the terrain, turns its collision off and seeds
            // the starter graph. A plain addObject makes a 1x1x1 box with no
            // graph in it, which generates nothing and looks from the outside
            // exactly like a broken feature.
            addScatterVolume(/*commit=*/false);
        else
            addObject(type, /*commit=*/false);
        SceneObject& fresh = project_.objects().back();
        if (!name.empty()) fresh.name = name;
        if (placed && type != PrimitiveType::Model) {
            for (int i = 0; i < 3; ++i) fresh.position[i] = pos[i];
            // A volume is meant to STRADDLE the ground (its own Y clip is what
            // bounds the output), so resting it on the surface would lift the
            // whole region off the terrain it is supposed to scatter over.
            if (!volume) snapInsertedObject();
        }
        commitChange();
        if (volume) procVolumeId_ = fresh.id;
        statusMessage_ = "AI: added " + fresh.name;
        if (volume)
            return "Added the Procedural volume \"" + fresh.name + "\" at " +
                   vecText(fresh.position) + " in scene \"" +
                   project_.active().name +
                   "\", sized over the terrain and carrying the starter graph "
                   "(scatter on the surface, thin by slope, pick an asset, vary "
                   "the transform, output). Its asset pool is EMPTY, so it "
                   "generates nothing until get_proc_graph / set_proc_graph "
                   "fill one in and bake_volume bakes it.";
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

    if (c.name == "set_proc_graph") {
        const std::string target = aichat::argStr(c, "object");
        const int oi = aichat::findObject(sc, target);
        if (oi < 0) return fail(aichat::noSuchObject(sc, target));
        SceneObject& o = sc.objects[oi];
        if (o.type != PrimitiveType::Scatter)
            return fail(aichat::notAVolume(sc, oi));
        const json::Value* g = aichat::argValue(c, "graph");
        if (!g || g->type != json::Value::Type::Object)
            return fail("\"graph\" must be the graph object itself, not a string "
                        "or a list.");
        ProcGraph fresh;
        // The project's OWN reader, which is what makes this write and a
        // hand-edited .tyra the same thing: unknown node types and the links
        // that touched them are dropped exactly as they are on load.
        if (!project::parseProcGraph(json::write(*g), fresh))
            return fail("That is not a valid scatter graph object.");
        const size_t before = o.procGraph.nodes.size();
        const size_t sentNodes =
            g->find("nodes") && g->find("nodes")->type == json::Value::Type::Array
                ? g->find("nodes")->arr.size()
                : 0;
        // A manual per-instance edit is bound to a point of the OLD graph; the
        // new one generates different points, so carrying the overrides over
        // would silently move things nobody moved.
        fresh.overrides.clear();
        fresh.bakedHash = 0;  // whatever is in the scene was built from the old one
        o.procGraph = std::move(fresh);
        commitChange();
        // Show it: the Procedural window opens on the volume that was written.
        if (si == project_.activeScene) {
            selectOnly(oi);
            procVolumeId_ = o.id;
            showProcedural_ = true;
        }
        statusMessage_ = "AI: scatter graph -> " + o.name;
        std::string msg = "Wrote the scatter graph of \"" + o.name + "\" (" +
                          std::to_string(before) + " -> " +
                          std::to_string(o.procGraph.nodes.size()) +
                          " nodes, " + std::to_string(o.procGraph.links.size()) +
                          " links).";
        if (sentNodes > o.procGraph.nodes.size())
            msg += " " + std::to_string(sentNodes - o.procGraph.nodes.size()) +
                   " node(s) were dropped as unknown types - list_proc_nodes has "
                   "the exact keys.";
        const std::vector<procgraph::ProcIssue> issues =
            procgraph::validate(o.procGraph);
        if (issues.empty())
            msg += " It validates; nothing is generated until it is baked "
                   "(bake_volume).";
        else {
            msg += " It does NOT evaluate yet:";
            for (const procgraph::ProcIssue& p : issues) {
                msg += " ";
                if (p.nodeId) msg += "node " + std::to_string(p.nodeId) + ": ";
                msg += p.text;
            }
        }
        return msg;
    }

    if (c.name == "create_prefab") {
        // Named objects, or whatever the user has selected - "capture what I
        // have selected" is how the Prefabs window works and how a person
        // describes it, and the assistant is told the selection in its prompt.
        std::vector<int> members;
        if (const json::Value* list = aichat::argValue(c, "objects");
            list && list->type == json::Value::Type::Array) {
            for (const json::Value& v : list->arr) {
                const std::string want = aichat::stringOf(v);
                if (want.empty()) continue;
                const int oi = aichat::findObject(sc, want);
                if (oi < 0) return fail(aichat::noSuchObject(sc, want));
                members.push_back(oi);
            }
        } else if (si == project_.activeScene) {
            members = selection_;
        }
        if (members.empty())
            return fail(
                "Nothing to capture: name the objects in \"objects\", or ask "
                "the user to select them in the viewport first (nothing is "
                "selected right now).");
        std::sort(members.begin(), members.end());
        members.erase(std::unique(members.begin(), members.end()), members.end());
        std::string wanted = aichat::argStr(c, "name");
        if (wanted.empty()) wanted = sc.objects[members[0]].name;
        // uniqueName rather than a refusal: the capture is the expensive part
        // of the request, and a name collision is worth a note, not a retry.
        const std::string name = prefab::uniqueName(project_, wanted);
        Prefab pf = prefab::capture(sc, members, name);
        if (pf.objects.empty())
            return fail("Those objects captured to nothing - a prefab needs at "
                        "least one object with geometry or a marker.");
        const size_t count = pf.objects.size();
        project_.prefabs.push_back(std::move(pf));
        prefabSelected_ = (int)project_.prefabs.size() - 1;
        showPrefabs_ = true;  // land the user where the prefab now lives
        commitChange();
        statusMessage_ = "AI: prefab " + name;
        std::string msg = "Captured " + std::to_string(count) +
                          " object(s) into the prefab \"" + name + "\".";
        if (name != wanted)
            msg += " (\"" + wanted + "\" was taken, so it got a suffix - use "
                   "the new name in Pick Prefab rows.)";
        // The cap is the thing that decides the whole approach downstream, so
        // it is stated here rather than waited for.
        msg += " A Pick Prefab pool can place at most " +
               std::to_string(prefab::kMaxRuntimeInstances) +
               " prefab instances in the whole game; past that, "
               "bake_prefab_model turns it into a .obj for a Pick Asset pool.";
        return msg;
    }

    if (c.name == "insert_prefab") {
        const std::string name = aichat::argStr(c, "name");
        const Prefab* pf = prefab::find(project_, name);
        if (!pf) {
            std::string s = "No prefab named \"" + name + "\". Prefabs: ";
            if (project_.prefabs.empty())
                s += "(none yet - create_prefab captures scene objects into "
                     "one)";
            for (size_t i = 0; i < project_.prefabs.size(); ++i)
                s += std::string(i ? ", " : "") + "\"" + project_.prefabs[i].name +
                     "\"";
            return fail(s);
        }
        float pos[3] = {0.0f, 0.0f, 0.0f};
        if (const json::Value* v = aichat::argValue(c, "position"))
            aichat::vec3(*v, pos);
        std::vector<SceneObject> copies =
            prefab::instantiate(*pf, pos[0], pos[1], pos[2],
                                (float)aichat::argNum(c, "yaw", 0.0),
                                (float)aichat::argNum(c, "scale", 1.0), "");
        if (copies.empty()) return fail("That prefab is empty.");
        // Unique names within the scene, the same scheme "Insert into scene"
        // uses. Not cosmetic: a reference is a NAME here, so two objects called
        // "plaza" make every cutscene track, mirror list and flow node that
        // says "plaza" mean whichever one comes first.
        for (SceneObject& o : copies) {
            std::string want = o.name;
            for (int n = 2;; ++n) {
                bool taken = false;
                for (const SceneObject& other : sc.objects)
                    taken |= (other.name == want);
                for (const SceneObject& other : copies)
                    taken |= (&other != &o && other.name == want);
                if (!taken) break;
                want = o.name + " " + std::to_string(n);
            }
            o.name = want;
        }
        // ONE offset for the whole group, so a stacked prefab keeps its shape
        // instead of collapsing onto the floor (the paste path's rule).
        if (si == project_.activeScene) {
            const float dy = placement::restOffsetYGroup(
                copies, sc.objects, placementSkip(), placementModelAabb(),
                placementHeight(), FLT_MAX);
            for (SceneObject& o : copies) o.position[1] += dy;
        }
        const size_t added = copies.size();
        const size_t first = project_.objects().size();
        for (SceneObject& o : copies) project_.objects().push_back(std::move(o));
        if (si == project_.activeScene) {
            selection_.clear();
            for (size_t k = first; k < project_.objects().size(); ++k)
                selection_.push_back((int)k);
            selectedObject_ = (int)first;
        }
        commitChange();  // stamps the fresh ids the copies were left without
        statusMessage_ = "AI: inserted " + name;
        return "Inserted \"" + name + "\" (" + std::to_string(added) +
               " object(s)) at " + vecText(pos) + " in scene \"" + sc.name +
               "\". The copies are their own objects - editing the prefab from "
               "now on does not touch them.";
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

    if (c.name == "bake_volume") {
        // What the bake produced, in the terms that decide whether it fits: the
        // console pays for triangles and RAM, not for how many instances the
        // graph felt like making.
        auto report = [](const procbake::Report& r) {
            std::string s = std::to_string(r.instances) + " instance(s) merged "
                            "into " + std::to_string(r.chunks) +
                            " chunk mesh(es), " + std::to_string(r.triangles) +
                            " triangles, ~" +
                            std::to_string(r.vertexBytes / 1024) + " KB of PS2 "
                            "RAM.";
            if (r.overBudget)
                s += " That is OVER the budget for this machine - say so and "
                     "offer to thin it (lower density, fewer levels, a coarser "
                     "instance detail).";
            for (const std::string& w : r.warnings) s += " " + w;
            return s;
        };
        const std::string target = aichat::argStr(c, "object");
        if (target.empty()) {
            if (!procbake::anyStale(project_))
                return "Every Procedural volume in the project is already baked "
                       "- nothing to do.";
            const procbake::Report rep = bakeStaleProcVolumes();
            if (!rep.error.empty()) return fail("The bake failed: " + rep.error);
            statusMessage_ = "AI: baked procedural volumes";
            return "Baked " + std::to_string(rep.volumes) + " stale volume(s): " +
                   report(rep);
        }
        // One volume goes through the editor's own bake verb, which works on the
        // scene the editor is SHOWING - so a named volume elsewhere is refused
        // rather than silently baking the wrong one.
        if (si != project_.activeScene)
            return fail("\"bake_volume\" works on the scene the editor is "
                        "showing (\"" + project_.active().name +
                        "\"). Call set_scene first, then repeat this call.");
        const int oi = aichat::findObject(sc, target);
        if (oi < 0) return fail(aichat::noSuchObject(sc, target));
        if (sc.objects[oi].type != PrimitiveType::Scatter)
            return fail(aichat::notAVolume(sc, oi));
        if (sc.objects[oi].procGraph.empty())
            return fail("\"" + target +
                        "\" has no scatter graph, so there is nothing to bake. "
                        "set_proc_graph gives it one.");
        const procbake::Report rep = bakeProcVolume(oi);
        if (!rep.error.empty()) return fail("The bake failed: " + rep.error);
        statusMessage_ = "AI: baked " + target;
        return "Baked \"" + target + "\": " + report(rep) +
               " The generated chunk objects are in the scene now and the "
               "viewport shows them.";
    }

    if (c.name == "bake_prefab_model") {
        const std::string name = aichat::argStr(c, "name");
        const Prefab* pf = prefab::find(project_, name);
        if (!pf) {
            std::string s = "No prefab named \"" + name + "\". Prefabs: ";
            if (project_.prefabs.empty())
                s += "(none yet - create_prefab captures scene objects into "
                     "one)";
            for (size_t i = 0; i < project_.prefabs.size(); ++i)
                s += std::string(i ? ", " : "") + "\"" + project_.prefabs[i].name +
                     "\"";
            return fail(s);
        }
        const prefab::BakeReport r = prefab::bakeToModel(project_, *pf);
        if (!r.error.empty()) return fail("The bake failed: " + r.error);
        prefabBakeReport_ = r;
        prefabBakeFor_ = pf->id;
        prefabBakeDiskKey_.clear();  // the window's on-disk readout just changed
        statusMessage_ = "AI: baked " + name + " to a model";
        std::string msg = "Baked " + std::to_string(r.members) +
                          " member(s) of \"" + name + "\" into " + r.modelPath +
                          " - " + std::to_string(r.triangles) + " triangles, " +
                          std::to_string(r.materials) + " material(s). Put that "
                          "path in a Pick Asset row to scatter it without "
                          "touching the prefab instance pool.";
        // Named, never silent: a member that did not merge is geometry the
        // user can see in the prefab and will not see in the scatter.
        if (!r.skipped.empty()) {
            msg += " Not merged (" + std::to_string(r.skipped.size()) + "): ";
            for (size_t i = 0; i < r.skipped.size() && i < 8; ++i)
                msg += std::string(i ? ", " : "") + r.skipped[i];
            if (r.skipped.size() > 8) msg += ", ...";
            msg += ".";
        }
        for (const std::string& w : r.warnings) msg += " " + w;
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
        runner_.buildAndRun(projectForBuild(), run, false,
                            run ? project_.activeScene : -1);
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
                "(Project > Preferences > Build & debug). A release game carries no "
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
    const ImGuiStyle& st = ImGui::GetStyle();
    // Two COLUMNS measured over the whole list, because the rows used to lay
    // themselves out by flowing after each other's text: a title one word
    // longer pushed that row's age and its delete button further right than the
    // row above, and in an auto-sized popup the widest row set the width while
    // every narrower one had its `x` floating somewhere in the middle.
    auto metaText = [](const aichat::ChatRecord& r) {
        return "- " + aichat::chatAge(r.ageSeconds) + ", " +
               std::to_string(r.messages) + " message(s)";
    };
    float titleW = 0.0f, metaW = 0.0f;
    for (const aichat::ChatRecord& r : chatHistory_) {
        titleW = std::max(titleW, ImGui::CalcTextSize(r.title.c_str()).x);
        metaW = std::max(metaW, ImGui::CalcTextSize(metaText(r).c_str()).x);
    }
    const float btnW = ImGui::CalcTextSize("x").x + st.FramePadding.x * 2.0f;
    const float rowW = titleW + st.ItemSpacing.x + metaW + st.ItemSpacing.x * 2.0f +
                       btnW;
    std::string deleted;
    for (size_t i = 0; i < chatHistory_.size(); ++i) {
        const aichat::ChatRecord& r = chatHistory_[i];
        ImGui::PushID((int)i);
        const bool current = r.file == chatFile_;
        if (current) ImGui::PushStyleColor(ImGuiCol_Text, sem.accent);
        const float x0 = ImGui::GetCursorPosX();
        // Without this the Selectable OWNS its whole rect and the delete button
        // drawn on top of it is visible but dead - the click goes to the row
        // underneath and opens the chat you were trying to throw away.
        ImGui::SetNextItemAllowOverlap();
        // Selectable rather than a button: the row is the whole width, which is
        // what makes a list of titles clickable at the length titles run to.
        if (ImGui::Selectable(r.title.c_str(), current, ImGuiSelectableFlags_None,
                              ImVec2(rowW, 0.0f)) &&
            !current)
            aiChatOpen(r.file);
        if (current) ImGui::PopStyleColor();
        ImGui::SameLine(x0 + titleW + st.ItemSpacing.x);
        ImGui::TextDisabled("%s", metaText(r).c_str());
        ImGui::SameLine(x0 + rowW - btnW);
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

namespace {

// Click-to-copy over something already drawn: the body is drawn in a group, a
// full-width InvisibleButton named "Copy message" goes back over it (a "##"
// label is untargetable by --ui-script), and theme::hoverAnim paints UNDER it
// through a draw-list channel split. `body` is what the click copies.
template <class Draw>
void chatClickToCopy(const std::string& body, Draw&& draw, unsigned& copiedId, double& copiedAt,
                     float width, float pad) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);
    ImGui::BeginGroup();
    draw();
    ImGui::EndGroup();
    const ImVec2 mn = ImGui::GetItemRectMin();
    const ImVec2 mx = ImGui::GetItemRectMax();
    const ImVec2 resume = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(mn);
    ImGui::InvisibleButton("Copy message", ImVec2(std::max(width > 0 ? width : mx.x - mn.x, 1.0f),
                                                  std::max(mx.y - mn.y, 1.0f)));
    const bool hovered = ImGui::IsItemHovered();
    const ImGuiID id = ImGui::GetItemID();
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (ImGui::IsItemClicked()) {
        ImGui::SetClipboardText(body.c_str());
        copiedId = id;
        copiedAt = ImGui::GetTime();
    }
    const float t = theme::hoverAnim(id, hovered, 12.0f);
    ImGui::SetCursorScreenPos(resume);
    dl->ChannelsSetCurrent(0);
    if (t > 0.0f)
        dl->AddRectFilled(ImVec2(mn.x - pad, mn.y - pad * 0.5f),
                          ImVec2(mn.x + (width > 0 ? width : mx.x - mn.x) + pad, mx.y + pad * 0.5f),
                          ImGui::GetColorU32(ImGuiCol_ButtonHovered, t * 0.3f), ImGui::GetStyle().FrameRounding);
    dl->ChannelsMerge();
    if (!hovered) return;
    if (copiedId == id && ImGui::GetTime() - copiedAt < 1.6)
        ImGui::SetTooltip("Copied to the clipboard");
    else if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Click to copy");
}

ImU32 chatCol(const ImVec4& c, float a = 1.0f) {
    return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * a));
}

// One line of a turn's change card, in words: what an edit or command tool
// did, built from its arguments (the result is for the model). kind: 0 added,
// 1 changed, 2 deleted / failed, 3 graph, 4 checked / quiet.
struct ChangeRow {
    std::string verb, title, detail, link, object;
    int kind = 1;
    std::string padScript;  // press_pad: drawn as the pads it holds
};

// What a press_pad script holds, moment by moment: one Compact controller per
// distinct non-neutral state (pressed buttons + parked sticks), with the
// buttons and how long it is held under it - what the AI is pressing, in the
// same drawing as Remote Pad.
void chatPadSteps(const std::string& script, float fs) {
    std::vector<livepad::Step> steps;
    std::string err;
    if (!livepad::parseScript(script, steps, err)) return;
    struct Moment {
        livepad::State st;
        double seconds;
    };
    std::vector<Moment> ms;
    for (const livepad::Step& st : steps) {
        if (st.state.neutral()) continue;
        if (!ms.empty() && ms.back().st == st.state) {
            ms.back().seconds += st.seconds;
            continue;
        }
        ms.push_back({st.state, st.seconds});
    }
    // A moment held for no time is a transition, not something pressed.
    ms.erase(std::remove_if(ms.begin(), ms.end(), [](const Moment& m) { return m.seconds <= 0.0; }), ms.end());
    if (ms.empty()) return;
    const theme::Semantics& sem = theme::semantics();
    const float w = fs * 8.0f;  // 120 px at 100%
    const float gap = fs * 0.6f;
    const int kMax = 8;
    for (int i = 0; i < (int)ms.size() && i < kMax; ++i) {
        const livepad::State& st = ms[i].st;
        const int pi = (st.buttons[0] || st.axes[0][0] || st.axes[0][1] || st.axes[0][2] || st.axes[0][3]) ? 0 : 1;
        ui::PadState ps;
        ps.pressed = st.buttons[pi] & 0xFFFFu;
        ps.stick[0] = ImVec2(st.axes[pi][0] / 127.0f, st.axes[pi][1] / 127.0f);
        ps.stick[1] = ImVec2(st.axes[pi][2] / 127.0f, st.axes[pi][3] / 127.0f);
        if (st.buttons[1] || pi == 1) ps.port = pi + 1;
        std::string cap;
        for (int b = 0; b < 16; ++b)
            if (ps.pressed & (1u << b)) cap += (cap.empty() ? "" : " + ") + std::string(ui::PadItemName(b));
        for (int sd = 0; sd < 2; ++sd)
            if (st.axes[pi][sd * 2] || st.axes[pi][sd * 2 + 1]) cap += (cap.empty() ? "" : " + ") + std::string(sd ? "R stick" : "L stick");
        char sec[32];
        std::snprintf(sec, sizeof(sec), " \xc2\xb7 %.1f s", ms[i].seconds);
        cap += sec;
        if (i > 0) {
            const float next = ImGui::GetItemRectMax().x + gap + w;
            if (next < ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) ImGui::SameLine(0.0f, gap);
        }
        ImGui::BeginGroup();
        ImGui::PushID(i);
        ui::PadDiagram("##aipad", ui::PadMode::Compact, w, ps);
        ImGui::PopID();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
        ImGui::TextColored(sem.textDim, "%s", cap.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
    }
    if ((int)ms.size() > kMax) ImGui::TextColored(sem.textDim, "and %d more", (int)ms.size() - kMax);
}

std::string firstLine(const std::string& s, size_t max = 90) {
    std::string l = s.substr(0, s.find('\n'));
    if (l.size() > max) l = l.substr(0, max - 3) + "...";
    return l;
}

std::string posText(const aichat::ToolCall& c, const char* key) {
    const json::Value* v = c.args.find(key);
    if (!v || v->type != json::Value::Type::Array || v->arr.size() < 3) return "";
    char b[64];
    std::snprintf(b, sizeof(b), "%.2f, %.2f, %.2f", v->arr[0].numberOr(0), v->arr[1].numberOr(0),
                  v->arr[2].numberOr(0));
    return b;
}

bool chatChangeRow(const aichat::ToolCall& c, ChangeRow& r) {
    const aichat::Tool* t = aichat::tool(c.name);
    if (!t || t->kind == aichat::ToolKind::Read) return false;
    const std::string& n = c.name;
    if (n == "select_object" || n == "set_scene" || n == "open_window") return false;  // navigation
    if (c.failed) {
        r.verb = "Failed";
        r.kind = 2;
        r.title = n;
        r.detail = firstLine(c.result);
        return true;
    }
    if (n == "add_object" || n == "duplicate_object" || n == "insert_prefab") {
        r.verb = "Added";
        r.kind = 0;
        r.title = aichat::argStr(c, "name");
        if (n == "duplicate_object" && r.title.empty()) r.title = aichat::argStr(c, "object") + "-copy";
        if (n == "insert_prefab") r.title = "prefab " + aichat::argStr(c, "name");
        if (r.title.empty()) r.title = "new " + aichat::argStr(c, "type");
        const std::string pos = posText(c, "position");
        r.detail = aichat::argStr(c, "type");
        if (!pos.empty()) r.detail += (r.detail.empty() ? "" : " \xc2\xb7 ") + pos;
        if (n != "insert_prefab") {
            r.link = "Select";
            r.object = n == "add_object" ? aichat::argStr(c, "name") : "";
        }
        if (r.object.empty()) r.link.clear();
    } else if (n == "set_object" || n == "set_object_json") {
        r.verb = "Changed";
        r.kind = 1;
        r.title = aichat::argStr(c, "object");
        if (r.title.empty()) r.title = "several objects";
        if (const json::Value* p = c.args.find("props"); p && p->type == json::Value::Type::Object) {
            for (const auto& kv : p->obj) r.detail += (r.detail.empty() ? "" : ", ") + kv.first;
        } else if (n == "set_object_json") {
            r.detail = "whole object";
        }
        r.object = aichat::argStr(c, "object");
        if (!r.object.empty()) r.link = "Select";
    } else if (n == "delete_object" || n == "delete_scene") {
        r.verb = "Deleted";
        r.kind = 2;
        r.title = aichat::argStr(c, n == "delete_object" ? "object" : "name");
        if (n == "delete_scene") r.title = "scene " + r.title;
    } else if (n == "set_graph" || n == "set_proc_graph") {
        r.verb = "Graph";
        r.kind = 3;
        r.title = aichat::argStr(c, "object");
        if (const json::Value* g = c.args.find("graph")) {
            const json::Value* nodes = g->find("nodes");
            const size_t cnt = nodes && nodes->type == json::Value::Type::Array ? nodes->arr.size() : 0;
            r.detail = std::to_string(cnt) + " node(s)";
        }
        if (n == "set_proc_graph") r.detail += " \xc2\xb7 scatter graph - bake to see it";
        r.object = r.title;
        if (n == "set_graph") r.link = "Open";
    } else if (n == "set_section") {
        r.verb = "Section";
        r.kind = 1;
        r.title = aichat::argStr(c, "name");
        r.detail = "project-wide - not in Ctrl+Z";
    } else if (n == "add_scene") {
        r.verb = "Added";
        r.kind = 0;
        r.title = "scene " + aichat::argStr(c, "name");
    } else if (n == "create_prefab") {
        r.verb = "Prefab";
        r.kind = 0;
        r.title = aichat::argStr(c, "name");
        if (r.title.empty()) r.title = "from the selection";
    } else {
        // Commands that check or build: refresh_generated, bake_*, build_game,
        // press_pad, save_project.
        r.verb = n == "save_project" ? "Saved" : n == "build_game" ? "Built" : n == "press_pad" ? "Pad"
                 : n.rfind("bake", 0) == 0 ? "Baked" : "Checked";
        r.kind = 4;
        r.title = n == "press_pad" ? aichat::argStr(c, "script") : n;
        r.detail = firstLine(c.result);
        if (n == "press_pad") r.padScript = aichat::argStr(c, "script");
        // A parked command (pad script, build) answers "... Waiting..." first
        // and gets its outcome APPENDED when it settles (aiChatTick): once it
        // is there, the card says how it ended instead of still waiting.
        const size_t nl = c.result.find('\n');
        const bool settled = nl != std::string::npos ||
                             c.result.find("The game is up") != std::string::npos ||
                             c.result.find("never reported") != std::string::npos;
        if (settled && (n == "press_pad" || n == "build_game")) {
            std::string head = c.result.substr(0, nl);
            for (const char* w : {" Waiting for it...", " Waiting..."}) {
                const size_t at = head.rfind(w);
                if (at != std::string::npos && at + std::strlen(w) == head.size()) head.erase(at);
            }
            if (n == "press_pad") {
                const std::string pre = "Driving the pad: ";
                if (head.rfind(pre, 0) == 0) head = "Played " + head.substr(pre.size());
                if (!head.empty() && head.back() == '.') head.pop_back();
                const bool logged = c.result.find("The game logged while it ran") != std::string::npos;
                r.detail = firstLine(head + (logged ? " - the game logged while it ran" : " - the game logged nothing"));
            } else {
                const bool ok = c.result.find("The build SUCCEEDED") != std::string::npos;
                const bool failed = c.result.find("The build FAILED") != std::string::npos;
                if (ok || failed) r.detail = ok ? "Build succeeded" : "Build failed";
                if (ok && c.result.find("The game is up") != std::string::npos) r.detail += " - the game is up";
                else if (ok && c.result.find("never reported") != std::string::npos)
                    r.detail += " - the game never reported";
            }
        }
    }
    return true;
}

}  // namespace

// One user turn: the user's bubble, then what came back - the assistant's
// prose, the read tools as one quiet line, and the edits as a change card in
// words with Undo next to them. Raw tool calls stay one disclosure away.
void App::drawAiChatTurn(size_t userIdx, size_t from, size_t to, bool live) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const float fullW = ImGui::GetContentRegionAvail().x;

    // --- the user's bubble, on the right
    if (userIdx < chat_.messages.size() && chat_.messages[userIdx].role == aichat::Message::Role::User) {
        const aichat::Message& m = chat_.messages[userIdx];
        ImGui::PushID((int)userIdx);
        const float maxW = fullW * 0.86f;
        const float padX = fs * 0.8f, padY = fs * 0.55f;
        const ImVec2 ts = ImGui::CalcTextSize(m.text.c_str(), nullptr, false, maxW - padX * 2);
        const float bw = ImMin(maxW, ts.x + padX * 2);
        const float x0 = ImGui::GetCursorPosX() + fullW - bw;
        ImGui::SetCursorPosX(x0);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + bw, p.y + ts.y + padY * 2), chatCol(sem.surface), fs * 0.8f);
        dl->AddRect(p, ImVec2(p.x + bw, p.y + ts.y + padY * 2), chatCol(sem.border), fs * 0.8f);
        chatClickToCopy(
            aichat::messageText(m),
            [&] {
                ImGui::SetCursorScreenPos(ImVec2(p.x + padX, p.y + padY));
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + bw - padX * 2);
                ImGui::TextUnformatted(m.text.c_str());
                ImGui::PopTextWrapPos();
                ImGui::Dummy(ImVec2(0, padY - ImGui::GetStyle().ItemSpacing.y));
            },
            chatCopiedId_, chatCopiedAt_, bw - padX, 0.0f);
        ImGui::PopID();
        ImGui::Spacing();
    }
    if (from >= to && !live) return;

    // --- what came back
    int rounds = 0;
    std::vector<const aichat::ToolCall*> reads, edits, all;
    for (size_t i = from; i < to; ++i) {
        const aichat::Message& m = chat_.messages[i];
        if (m.role != aichat::Message::Role::Tool) continue;
        ++rounds;
        for (const aichat::ToolCall& c : m.calls) {
            all.push_back(&c);
            const aichat::Tool* t = aichat::tool(c.name);
            if (t && t->kind == aichat::ToolKind::Read) reads.push_back(&c);
            else edits.push_back(&c);
        }
    }
    const auto turnIt = chatTurns_.find(userIdx);
    ImGui::PushID((int)(userIdx + 100000));
    {
        // header: who, in which mode, how many rounds
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float s = fs * 1.2f;
        dl->AddRectFilled(p, ImVec2(p.x + s, p.y + s), chatCol(sem.accent, 0.14f), fs * 0.35f);
        ui::DrawRtIcon(dl, ui::RtIcon::Sparkle, ImVec2(p.x + s * 0.5f, p.y + s * 0.5f), s * 0.62f, chatCol(sem.accent));
        ImGui::SetCursorScreenPos(ImVec2(p.x + s + fs * 0.45f, p.y + (s - ImGui::GetTextLineHeight()) * 0.5f));
        std::string head = "Assistant";
        if (turnIt != chatTurns_.end()) {
            static const char* const kModes[3] = {"Ask only", "Edit project", "Edit + build & run"};
            head += std::string(" \xc2\xb7 ") + kModes[ImClamp(turnIt->second.mode, 0, 2)];
        }
        if (rounds) head += " \xc2\xb7 " + std::to_string(rounds) + (rounds == 1 ? " round" : " rounds");
        ImGui::TextColored(sem.textDim, "%s", head.c_str());
        if (live) {
            ImGui::SameLine();
            ImGui::TextColored(sem.accent, "\xc2\xb7 %s",
                               chatCompacting_      ? "compacting"
                               : chatBuildWaiting_  ? "building the game"
                               : chatGameWaiting_   ? "waiting for the game"
                               : chatPadWaiting_    ? "pad script running"
                                                    : "thinking");
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x, ImMax(ImGui::GetCursorScreenPos().y, p.y + s + 4.0f)));
    }

    // --- read tools: one quiet line, the raw calls under it
    if (!reads.empty()) {
        std::string line = "Read ";
        for (size_t i = 0; i < reads.size() && i < 4; ++i) {
            const aichat::ToolCall& c = *reads[i];
            std::string what = aichat::argStr(c, "name");
            if (what.empty()) what = aichat::argStr(c, "object");
            if (what.empty()) what = aichat::argStr(c, "query");
            if (what.empty()) what = c.name;
            else if (c.name == "read_doc") what += ".md";
            line += (i ? ", " : "") + what;
        }
        if (reads.size() > 4) line += " and " + std::to_string(reads.size() - 4) + " more";
        line += "##reads";
        ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
        const bool open = ImGui::TreeNodeEx(line.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
        ImGui::PopStyleColor();
        if (open) {
            for (size_t k = 0; k < reads.size(); ++k) {
                const aichat::ToolCall& c = *reads[k];
                ImGui::PushID((int)k);
                ImGui::PushStyleColor(ImGuiCol_Text, c.failed ? sem.warn : sem.textDim);
                const std::string label = c.name + " " + aichat::argsText(c) + (c.failed ? "  [failed]" : "");
                const bool o = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
                ImGui::PopStyleColor();
                if (o) {
                    chatClickToCopy(
                        c.result,
                        [&] {
                            ImGui::PushTextWrapPos(0.0f);
                            ImGui::TextUnformatted(c.result.c_str());
                            ImGui::PopTextWrapPos();
                        },
                        chatCopiedId_, chatCopiedAt_, ImGui::GetContentRegionAvail().x, scaled(4));
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
    }

    // --- the assistant's prose
    for (size_t i = from; i < to; ++i) {
        const aichat::Message& m = chat_.messages[i];
        if (m.role != aichat::Message::Role::Assistant || m.text.empty()) continue;
        ImGui::PushID((int)i);
        chatClickToCopy(
            aichat::messageText(m),
            [&] {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(m.text.c_str());
                ImGui::PopTextWrapPos();
            },
            chatCopiedId_, chatCopiedAt_, ImGui::GetContentRegionAvail().x, scaled(4));
        ImGui::PopID();
    }

    // --- the change card
    std::vector<ChangeRow> rows;
    for (const aichat::ToolCall* c : edits) {
        ChangeRow r;
        if (chatChangeRow(*c, r)) rows.push_back(std::move(r));
    }
    if (!rows.empty()) {
        ImGui::Spacing();
        int changes = 0, checks = 0, failed = 0;
        for (const ChangeRow& r : rows) (r.kind == 4 ? checks : r.verb == "Failed" ? failed : changes)++;
        ui::BeginCard("##changes", ImVec2(0, 0));
        char cap[96];
        std::snprintf(cap, sizeof(cap), "%d change%s%s%s", changes, changes == 1 ? "" : "s",
                      checks ? (", " + std::to_string(checks) + (checks == 1 ? " check" : " checks")).c_str()
                             : "",
                      failed ? (", " + std::to_string(failed) + " failed").c_str() : "");
        ui::Caption(cap, dirty_ ? "unsaved" : nullptr);
        for (size_t i = 0; i < rows.size(); ++i) {
            const ChangeRow& r = rows[i];
            ImGui::PushID((int)i);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec4 kc = r.kind == 0 ? sem.ok : r.kind == 2 ? sem.danger : r.kind == 3 ? sem.accent
                              : r.kind == 1 ? sem.warn : sem.textDim;
            const float bw = fs * 4.4f, bh = fs * 1.35f;
            dl->AddRectFilled(p, ImVec2(p.x + bw, p.y + bh), chatCol(kc, r.kind == 4 ? 0.1f : 0.14f), fs * 0.3f);
            {
                ui::FontScope bold(ui::boldFont());
                const float vfs = fs * 0.82f;
                const ImVec2 vs = ImGui::GetFont()->CalcTextSizeA(vfs, FLT_MAX, 0.0f, r.verb.c_str());
                dl->AddText(ImGui::GetFont(), vfs, ImVec2(p.x + (bw - vs.x) * 0.5f, p.y + (bh - vs.y) * 0.5f),
                            chatCol(kc), r.verb.c_str());
            }
            const float linkW = r.link.empty() ? 0.0f : ImGui::CalcTextSize(r.link.c_str()).x + fs;
            ImGui::SetCursorScreenPos(ImVec2(p.x + bw + fs * 0.6f, p.y));
            ImGui::BeginGroup();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - linkW);
            ImGui::TextUnformatted(r.title.c_str());
            if (!r.detail.empty()) ImGui::TextColored(sem.textDim, "%s", r.detail.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndGroup();
            if (!r.link.empty()) {
                ImGui::SameLine(ImGui::GetContentRegionMax().x - linkW + fs * 0.5f);
                ImGui::PushStyleColor(ImGuiCol_Text, sem.accent);
                if (ImGui::TextLink((r.link + "##" + std::to_string(i)).c_str())) {
                    SceneData& sc = project_.active();
                    const int oi = aichat::findObject(sc, r.object);
                    if (oi >= 0) {
                        selectOnly(oi);
                        if (r.link == "Open") {
                            flowGraphObject_ = oi;
                            flowPositionsApplied_ = false;
                            ImGui::SetWindowFocus("Flow Graph");
                        }
                    } else {
                        statusMessage_ = "\"" + r.object + "\" is not in the active scene any more";
                    }
                }
                ImGui::PopStyleColor();
            }
            if (!r.padScript.empty()) {
                ImGui::Indent(fs * 5.0f);
                chatPadSteps(r.padScript, fs);
                ImGui::Unindent(fs * 5.0f);
            }
            ImGui::Dummy(ImVec2(0, fs * 0.2f));
            ImGui::PopID();
        }
        // footer: Undo the turn's steps, the raw calls, the undo rule
        ImGui::Separator();
        const bool known = turnIt != chatTurns_.end();
        const int steps = known ? turnIt->second.undoEnd - turnIt->second.undoBase : 0;
        const bool canUndo = known && !turnIt->second.undone && steps > 0 && !live &&
                             history_.index() == turnIt->second.undoEnd;
        if (known && turnIt->second.undone) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(sem.textDim, "Undone.");
            ImGui::SameLine();
        } else if (steps > 0) {
            char u[48];
            std::snprintf(u, sizeof(u), "Undo %s##turnundo", steps == 1 ? "it" : steps == 2 ? "both" : "all");
            ImGui::BeginDisabled(!canUndo);
            if (ImGui::Button(u) && canUndo) {
                for (int s = 0; s < steps && history_.canUndo(); ++s) undo();
                turnIt->second.undone = true;
                statusMessage_ = "AI: undid " + std::to_string(steps) + " step(s)";
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip(canUndo ? "Step the undo history back to before this turn (%d step(s))."
                                          : "Something changed since - use Ctrl+Z (%d step(s) came from this turn).",
                                  steps);
            ImGui::SameLine();
        }
        ImGuiStorage* store = ImGui::GetStateStorage();
        const ImGuiID rawId = ImGui::GetID("##rawcalls");
        bool raw = store->GetBool(rawId, false);
        char rl[48];
        std::snprintf(rl, sizeof(rl), "%d tool call%s##rawtoggle", (int)edits.size(), edits.size() == 1 ? "" : "s");
        if (ui::GhostButton(rl)) store->SetBool(rawId, raw = !raw);
        const char* rule = steps > 0 && changes > steps ? "project-wide changes are not in Ctrl+Z"
                                                        : "each is one Ctrl+Z";
        if (changes > 0) {
            ImGui::SameLine(ImMax(ImGui::GetCursorPosX(),
                                  ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(rule).x));
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(sem.textDim, "%s", rule);
        }
        if (raw)
            for (size_t k = 0; k < edits.size(); ++k) {
                const aichat::ToolCall& c = *edits[k];
                ImGui::PushID((int)(k + 1000));
                ImGui::PushStyleColor(ImGuiCol_Text, c.failed ? sem.warn : sem.textDim);
                const std::string label = c.name + " " + aichat::argsText(c) + (c.failed ? "  [failed]" : "");
                const bool o = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
                ImGui::PopStyleColor();
                if (o) {
                    chatClickToCopy(
                        c.result,
                        [&] {
                            ImGui::PushTextWrapPos(0.0f);
                            ImGui::TextUnformatted(c.result.c_str());
                            ImGui::PopTextWrapPos();
                        },
                        chatCopiedId_, chatCopiedAt_, ImGui::GetContentRegionAvail().x, scaled(4));
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        ui::EndCard();
    }
    if (live) {
        ImGui::Spacing();
        ui::ProgressInline(-1.0f, nullptr, nullptr);
    }
    ImGui::PopID();
    ImGui::Spacing();
    ImGui::Spacing();
}

void App::drawAiChatWindow() {
    if (!showAiChat_) {
        chatWasOpen_ = false;
        return;
    }
    // A conversation beside the scene: the first time it opens it docks as a
    // tab of the Properties column instead of floating over the viewport -
    // and every time it is opened its tab comes forward (an unselected tab is
    // as invisible as a closed window).
    if (ImGuiWindow* props = ImGui::FindWindowByName("Properties"); props && props->DockId)
        ImGui::SetNextWindowDockID(props->DockId, ImGuiCond_FirstUseEver);
    if (!chatWasOpen_) ImGui::SetNextWindowFocus();
    chatWasOpen_ = true;
    ImGui::SetNextWindowSize(ImVec2(scaled(440), scaled(720)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("AI Assistant", &showAiChat_)) {
        ImGui::End();
        return;
    }
    const theme::Semantics& sem = theme::semantics();
    const bool busy = chatGen_.busy() || chatBuildWaiting_ || chatGameWaiting_ || chatPadWaiting_;
    const float fs = ImGui::GetFontSize();

    // --- header: who answers (a chip that opens the backend settings), and
    // the conversation actions as icons
    {
        ImGui::AlignTextToFramePadding();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ui::DrawRtIcon(ImGui::GetWindowDrawList(), ui::RtIcon::Sparkle,
                       ImVec2(p.x + fs * 0.55f, p.y + ImGui::GetFrameHeight() * 0.5f), fs * 1.05f, chatCol(sem.accent));
        ImGui::Dummy(ImVec2(fs * 1.1f, ImGui::GetFrameHeight()));
        ImGui::SameLine();
        {
            ui::FontScope bold(ui::boldFont());
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Assistant");
        }
        ImGui::SameLine();
        std::string chip = std::string(aigen::backendLabel(globalAi_.backend)) + " \xc2\xb7 " +
                           (globalAi_.model.empty() ? "default" : globalAi_.model) +
                           (globalAi_.thinking ? " \xc2\xb7 thinking" : "");
        const float iconsW = (ImGui::GetFrameHeight() + 2.0f) * 3;
        const float room = ImGui::GetContentRegionMax().x - ImGui::GetCursorPosX() - iconsW - fs;
        while (chip.size() > 8 && ui::FilterChipWidth((chip + " v").c_str()) > room) chip = chip.substr(0, chip.size() - 4) + "...";
        // The caret is drawn, not typed: the interface face has no U+25BE.
        if (ui::FilterChip((chip + "    ##aibackendchip").c_str(), false)) ImGui::OpenPopup("##aibackend");
        {
            const ImVec2 b = ImGui::GetItemRectMax(), a = ImGui::GetItemRectMin();
            ui::DrawRtIcon(ImGui::GetWindowDrawList(), ui::RtIcon::ChevronDown,
                           ImVec2(b.x - fs * 0.9f, (a.y + b.y) * 0.5f), fs * 0.6f, chatCol(sem.textDim));
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Backend and model - the machine-wide setting from Edit > Preferences >\n"
                              "AI assistant (the Flow Graph's \"Generate with AI\" uses it too).");
        if (ImGui::BeginPopup("##aibackend")) {
            ui::Caption("AI backend");
            const auto ids = aigen::backendIds();
            ImGui::SetNextItemWidth(scaled(220));
            if (ImGui::BeginCombo("Backend", aigen::backendLabel(globalAi_.backend))) {
                for (const char* id : ids)
                    if (ImGui::Selectable(aigen::backendLabel(id), globalAi_.backend == id)) {
                        globalAi_.backend = id;
                        globalAi_.model.clear();
                        saveGlobalConfig();
                    }
                ImGui::EndCombo();
            }
            ImGui::SetNextItemWidth(scaled(220));
            if (ImGui::BeginCombo("Model", globalAi_.model.empty() ? "default" : globalAi_.model.c_str())) {
                if (ImGui::Selectable("default", globalAi_.model.empty())) {
                    globalAi_.model.clear();
                    saveGlobalConfig();
                }
                for (const char* m : aigen::modelPresets(globalAi_.backend))
                    if (ImGui::Selectable(m, globalAi_.model == m)) {
                        globalAi_.model = m;
                        saveGlobalConfig();
                    }
                ImGui::EndCombo();
            }
            if (ui::Switch("Extended thinking", &globalAi_.thinking)) saveGlobalConfig();
            ImGui::TextDisabled("A custom model id is set in Edit > Preferences.");
            ImGui::EndPopup();
        }
        ImGui::SameLine(ImGui::GetContentRegionMax().x - iconsW + 2.0f);
        if (ui::IconButton("History", ui::RtIcon::History, 0.0f, "Saved chats for this project")) {
            chatHistoryScanned_ = false;  // re-scan on every open, never per frame
            ImGui::OpenPopup("##chathistory");
        }
        // The popup is submitted right after the button that opens it, with the
        // SAME string (an id mismatch is open-but-invisible and eats clicks).
        drawAiChatHistory();
        ImGui::SameLine(0.0f, 2.0f);
        if (ui::IconButton("Copy chat", ui::RtIcon::Copy, 0.0f,
                           "The whole conversation as plain text - what was asked, what was\n"
                           "answered and what every tool returned.",
                           !chat_.messages.empty())) {
            ImGui::SetClipboardText(aichat::conversationText(chat_).c_str());
            statusMessage_ = "AI chat copied to the clipboard";
        }
        ImGui::SameLine(0.0f, 2.0f);
        if (ui::IconButton("New chat", ui::RtIcon::Plus, 0.0f, "Start a new chat (this one is kept in History)"))
            aiChatReset();
    }
    {
        const ImVec2 a = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(a, ImVec2(a.x + ImGui::GetContentRegionAvail().x, a.y),
                                            chatCol(sem.border));
        ImGui::Dummy(ImVec2(0, 2));
    }

    // --- transcript, grouped into turns
    const float composerH = ImGui::GetFrameHeightWithSpacing() * 3.2f + ImGui::GetTextLineHeightWithSpacing() * 5.2f +
                            scaled(34);
    if (ImGui::BeginChild("##chatlog", ImVec2(0, -composerH))) {
        if (chat_.messages.empty()) {
            ImGui::Dummy(ImVec2(0, scaled(10)));
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(sem.textDim,
                               "Ask about the editor, or ask for something to be done. The assistant reads "
                               "the editor's own documentation and can act on the open project.");
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            struct Starter {
                const char* text;
                bool change;
            };
            static const Starter kExamples[] = {
                {"What is a Procedural volume for?", false},
                {"Add a usable box called lever at 4, 0, -6", true},
                {"Give the lever a graph: on Used, play a sound", true},
                {"How do menu stylesheets work?", false},
            };
            for (int i = 0; i < (int)IM_ARRAYSIZE(kExamples); ++i) {
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const float w = ImGui::GetContentRegionAvail().x;
                const float h = ImGui::GetFrameHeight() + scaled(12);
                ImGui::PushID(i);
                const bool clicked = ImGui::InvisibleButton(kExamples[i].text, ImVec2(w, h));
                const float t = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), chatCol(t > 0 ? sem.surface : sem.field, 1.0f),
                                  fs * 0.6f);
                dl->AddRect(p, ImVec2(p.x + w, p.y + h), chatCol(sem.border), fs * 0.6f);
                ui::DrawRtIcon(dl, kExamples[i].change ? ui::RtIcon::Pencil : ui::RtIcon::Question,
                               ImVec2(p.x + fs * 1.1f, p.y + h * 0.5f), fs * 0.95f,
                               chatCol(kExamples[i].change ? sem.ok : sem.accent));
                dl->AddText(ImVec2(p.x + fs * 2.1f, p.y + (h - ImGui::GetTextLineHeight()) * 0.5f),
                            chatCol(sem.text), kExamples[i].text);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip(kExamples[i].change ? "A change - needs Edit project" : "A question");
                if (clicked && !busy) aiChatSend(kExamples[i].text);
                ImGui::PopID();
            }
        }
        // Turns: everything from one User message to the next. Messages before
        // the first User (a compaction summary) are drawn as such.
        size_t i = 0;
        while (i < chat_.messages.size()) {
            const aichat::Message& m = chat_.messages[i];
            if (m.role == aichat::Message::Role::Summary) {
                // Collapsed: it stands in for messages that are gone.
                ImGui::PushID((int)i);
                ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
                const bool openSummary =
                    ImGui::TreeNodeEx("Earlier conversation, summarised", ImGuiTreeNodeFlags_SpanAvailWidth);
                ImGui::PopStyleColor();
                if (openSummary) {
                    chatClickToCopy(
                        m.text,
                        [&] {
                            ImGui::PushTextWrapPos(0.0f);
                            ImGui::TextUnformatted(m.text.c_str());
                            ImGui::PopTextWrapPos();
                        },
                        chatCopiedId_, chatCopiedAt_, ImGui::GetContentRegionAvail().x, scaled(4));
                    ImGui::TreePop();
                }
                ImGui::PopID();
                ++i;
                continue;
            }
            const size_t user = m.role == aichat::Message::Role::User ? i : (size_t)-1;
            size_t j = i + (user == i ? 1 : 0);
            while (j < chat_.messages.size() && chat_.messages[j].role != aichat::Message::Role::User &&
                   chat_.messages[j].role != aichat::Message::Role::Summary)
                ++j;
            const bool last = j >= chat_.messages.size();
            drawAiChatTurn(user, user == i ? i + 1 : i, j, last && busy);
            i = j;
        }
        if (!chatCompactNote_.empty()) ImGui::TextDisabled("%s", chatCompactNote_.c_str());
        if (!chatError_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, sem.danger);
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

    // --- composer: what it may do, the draft, the send button
    {
        const ImVec2 a = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(a, ImVec2(a.x + ImGui::GetContentRegionAvail().x, a.y),
                                            chatCol(sem.border));
        ImGui::Dummy(ImVec2(0, 2));
    }
    static const char* const kModes[3] = {"Ask only", "Edit project", "+ Build & run"};
    int mode = !chatAllowEdits_ ? 0 : chatAllowBuild_ ? 2 : 1;
    if (ui::Segmented("##chatmode", kModes, 3, &mode, -1.0f)) {
        chatAllowEdits_ = mode >= 1;
        chatAllowBuild_ = mode == 2;
        saveGlobalConfig();
    }
    static const char* const kConsequence[3] = {
        "Reads the project and the docs; changes nothing.",
        "Changes objects, graphs and sections. Every scene change is one Ctrl+Z; nothing is saved "
        "unless you ask.",
        "Also builds and runs the game and drives the Remote Pad - minutes, and not undone by Ctrl+Z.",
    };
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(sem.textDim, "%s", kConsequence[mode]);
    ImGui::PopTextWrapPos();

    // The draft in a card: a frameless multi-line field, then a row with what
    // "this" refers to, the key hint and the send / stop button.
    const ImVec2 cardMin = ImGui::GetCursorScreenPos();
    const float cardW = ImGui::GetContentRegionAvail().x;
    const float inputH = ImGui::GetTextLineHeightWithSpacing() * 2.6f;
    const float rowH = ImGui::GetFrameHeight();
    const float pad = fs * 0.55f;
    const float cardH = inputH + rowH + pad * 3;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(cardMin, ImVec2(cardMin.x + cardW, cardMin.y + cardH), chatCol(sem.field), fs * 0.8f);
    const bool inputFocused = ImGui::GetActiveID() == ImGui::GetID("##chatinput");
    dl->AddRect(cardMin, ImVec2(cardMin.x + cardW, cardMin.y + cardH),
                inputFocused ? chatCol(sem.accent, 0.6f) : chatCol(sem.border), fs * 0.8f, 0, 1.2f);
    ImGui::SetCursorScreenPos(ImVec2(cardMin.x + pad, cardMin.y + pad));
    if (chatFocusInput_) {  // the command palette handed over a draft
        ImGui::SetKeyboardFocusHere();
        chatFocusInput_ = false;
    }
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    const bool send = ImGui::InputTextMultiline("##chatinput", chatInputBuf_, sizeof(chatInputBuf_),
                                                ImVec2(cardW - pad * 2, inputH),
                                                ImGuiInputTextFlags_CtrlEnterForNewLine |
                                                    ImGuiInputTextFlags_EnterReturnsTrue) &&
                      !busy;
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    const bool empty = chatInputBuf_[0] == '\0';
    if (empty) {
        dl->AddText(ImVec2(cardMin.x + pad, cardMin.y + pad), chatCol(sem.textDim, 0.8f),
                    "Ask, or describe a change...");
    }
    ImGui::SetCursorScreenPos(ImVec2(cardMin.x + pad, cardMin.y + pad * 2 + inputH));
    {
        // What "this", "here" and "selected" mean to the assistant.
        const auto& objs = project_.objects();
        std::string sel = hasProject_ ? project_.active().name : "no project";
        if (hasProject_ && selectedObject_ >= 0 && selectedObject_ < (int)objs.size())
            sel = objs[selectedObject_].name + " \xc2\xb7 " + project_.active().name;
        ImGui::AlignTextToFramePadding();
        ui::Badge(sel.c_str(), 4);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("What \"this\", \"here\" and \"selected\" refer to: the selection and the\n"
                              "active scene, sent with every request.");
    }
    const float btn = rowH;
    const char* hint = "Enter sends \xc2\xb7 Ctrl+Enter new line";
    const float hintW = ImGui::CalcTextSize(hint).x * 0.85f;
    const float right = cardMin.x + cardW - pad;
    if (right - btn - hintW - fs > ImGui::GetItemRectMax().x) {
        dl->AddText(ImGui::GetFont(), fs * 0.85f,
                    ImVec2(right - btn - fs * 0.6f - hintW, cardMin.y + pad * 2 + inputH + (rowH - fs * 0.85f) * 0.5f),
                    chatCol(sem.textDim, 0.8f), hint);
    }
    ImGui::SetCursorScreenPos(ImVec2(right - btn, cardMin.y + pad * 2 + inputH));
    if (busy) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("Stop", ImVec2(btn, btn))) {
            chatGen_.cancel();
            chatError_ = "Cancelled.";
        }
        dl->AddRectFilled(p, ImVec2(p.x + btn, p.y + btn), chatCol(sem.danger), fs * 0.4f);
        ui::DrawRtIcon(dl, ui::RtIcon::Stop, ImVec2(p.x + btn * 0.5f, p.y + btn * 0.5f), btn * 0.45f,
                       chatCol(sem.ground));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Stop this request");
    } else {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::BeginDisabled(empty);
        const bool clicked = ImGui::InvisibleButton("Send", ImVec2(btn, btn));
        ImGui::EndDisabled();
        dl->AddRectFilled(p, ImVec2(p.x + btn, p.y + btn), chatCol(empty ? sem.surface : sem.accent), fs * 0.4f);
        ui::DrawRtIcon(dl, ui::RtIcon::Send, ImVec2(p.x + btn * 0.5f, p.y + btn * 0.5f), btn * 0.5f,
                       chatCol(empty ? sem.textDim : sem.ground));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Send (Enter)");
        if ((clicked || send) && !empty) {
            const std::string text = chatInputBuf_;
            chatInputBuf_[0] = '\0';
            aiChatSend(text);
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(cardMin.x, cardMin.y + cardH + ImGui::GetStyle().ItemSpacing.y));

    // --- what this is costing: the context of the NEXT request (an estimate)
    // against where it compacts, then what the backend reported for the session
    const aichat::ContextStats st = aichat::contextStats(aiChatSystemPrompt(), chat_);
    const size_t foldable = aichat::compactableCount(chat_);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(sem.textDim, "Context");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip(
            "What the next request carries, estimated at 4 bytes per token:\n"
            "  instructions  ~%s  (tools, the documentation index, your project)\n"
            "  conversation  ~%s  (%d message(s))\n"
            "The instructions are re-sent every time - none of the backends keeps\n"
            "state between calls. Past ~%s of conversation it is compacted: the\n"
            "model writes a recap of the older messages and that replaces them.%s",
            tokText(st.promptTokens).c_str(), tokText(st.transcriptTokens).c_str(), st.messages,
            tokText(st.compactAtTokens).c_str(),
            st.overBudget ? "\n\nIt is past that now - the next question compacts first." : "");
    ImGui::SameLine();
    {
        const float mw = scaled(80), mh = ImMax(3.0f, fs * 0.28f);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float y = p.y + (ImGui::GetFrameHeight() - mh) * 0.5f;
        const float frac = st.compactAtTokens > 0 ? (float)st.transcriptTokens / (float)st.compactAtTokens : 0.0f;
        dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + mw, y + mh), chatCol(sem.surface), mh);
        dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + mw * ImClamp(frac, 0.0f, 1.0f), y + mh),
                          chatCol(st.overBudget ? sem.warn : sem.ok), mh);
        ImGui::Dummy(ImVec2(mw, ImGui::GetFrameHeight()));
    }
    ImGui::SameLine();
    {
        ui::FontScope mono(ui::monoFont());
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(st.overBudget ? sem.warn : sem.textDim, "%s / %s", tokText(st.transcriptTokens).c_str(),
                           tokText(st.compactAtTokens).c_str());
    }
    std::string usage;
    if (chatUsageReal_) {
        usage = tokText(chatTokensIn_) + " in \xc2\xb7 " + tokText(chatTokensOut_) + " out";
        if (chatCostUsd_ > 0.0) {
            char c[32];
            std::snprintf(c, sizeof(c), " \xc2\xb7 $%.2f", chatCostUsd_);
            usage += c;
        }
    } else if (!chat_.messages.empty()) {
        usage = "no usage reported";
    }
    const char* compact = "Compact";
    const float compactW = ImGui::CalcTextSize(compact).x + ImGui::GetStyle().FramePadding.x * 2.6f;
    if (!usage.empty()) {
        ui::FontScope mono(ui::monoFont());
        const float uw = ImGui::CalcTextSize(usage.c_str()).x;
        const float x = ImGui::GetContentRegionMax().x - compactW - uw - fs * 0.6f;
        ImGui::SameLine();
        if (x > ImGui::GetCursorPosX()) {
            ImGui::SetCursorPosX(x);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(sem.textDim, "%s", usage.c_str());
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip(
                    "Reported by the backend itself, for every request this editor session\n"
                    "made (this chat and the ones before it). A cost is what the backend says\n"
                    "it billed - on a subscription, what it WOULD have cost on the API.");
        } else {
            ImGui::NewLine();
        }
    }
    ImGui::SameLine(ImGui::GetContentRegionMax().x - compactW);
    if (ui::GhostButton(compact, compactW, foldable > 0 && !busy)) {
        chatCompactPending_ = true;
        chatCompactThenSend_ = false;
        aiChatStart();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(foldable > 0 ? "Fold the %d oldest message(s) into a summary now, instead of\n"
                                         "waiting until the conversation outgrows its budget. Costs one\n"
                                         "backend request; the recent turns are kept word for word."
                                       : "Nothing to fold yet - the conversation is short.",
                          (int)foldable);
    ImGui::End();
}
