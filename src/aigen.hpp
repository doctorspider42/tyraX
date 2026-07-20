#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "flowgraph.hpp"

struct Project;

// AI flow-graph generation: turns a natural-language request into a FlowGraph
// by running an external model backend and parsing its JSON reply.
//
// Split in three independently testable stages:
//  1. systemPrompt() - the full instruction text: the JSON contract, the node
//     catalog derived live from flowNodeTypes() + the project's custom nodes,
//     and the project context (object/scene/asset names) so the model can
//     reference real entities. Regenerated per request - it always matches the
//     registry, so new node types need no prompt maintenance.
//  2. Generator - runs the configured backend (claude CLI / copilot CLI /
//     OpenAI API via curl) on a worker thread. The prompt travels via a temp
//     file + stdin (never the command line - prompts contain newlines and can
//     exceed the 32k command-line limit). Cancelable: the child runs inside a
//     kill-on-close Job Object so cancel() takes the whole process tree down,
//     not just the cmd.exe wrapper.
//  3. parseGraph() - extracts the first JSON object from the reply (models
//     love markdown fences and prose), validates every node type against the
//     registry, drops links that violate the pin rules (same checks the graph
//     editor runs), and auto-lays-out nodes the model left unpositioned.
//
// The same stages back the GUI modal (app.cpp) and the headless --ai-graph
// CLI command (main.cpp) - see docs/ai-flow-graph.md.
namespace aigen {

// Machine-global backend settings (editor.ini; edited in Edit > Preferences).
struct Config {
    std::string backend = "claude";  // "claude" | "copilot" | "openai"
    std::string model;               // "" = the backend's default model
    bool thinking = false;           // extended thinking / reasoning effort
};

// UI + validation helpers for the backend/model pickers.
const char* backendLabel(const std::string& backend);   // "Claude CLI", ...
std::vector<const char*> backendIds();                  // {"claude", ...}
std::vector<const char*> modelPresets(const std::string& backend);

// The instruction text sent as the first part of every request (the user's
// request is appended after it). ownerIndex = the object whose graph is being
// generated, in the ACTIVE scene of p. When `editing` is non-null the prompt
// switches to edit mode: it embeds that graph (in the reply schema) and
// instructs the model to return the COMPLETE updated graph - the caller then
// replaces the object's graph with the reply.
std::string systemPrompt(const Project& p, int ownerIndex,
                         const FlowGraph* editing = nullptr);

// Parses a model reply into `out` (which is REPLACED). Returns "" on success,
// a human-readable error otherwise. Non-fatal issues (dropped invalid links,
// auto-layout applied) are appended to *warnings when given. Accepts both the
// prompt's link schema ("kind": "exec"/"object"/"pos"/"bool"/"text") and the
// project-file schema ("data"/"pos"/"bool"/"text" bool flags), so it also
// reads graphs exported by the editor itself (--apply-graph round-trips).
std::string parseGraph(const std::string& reply, FlowGraph& out,
                       std::string* warnings = nullptr);

// Merges `add` into `dst`: node/link ids are shifted past dst's, positions
// are shifted below dst's lowest node. Used by --apply-graph --append (the
// AI paths instead put the current graph into the prompt and take the
// model's reply as the complete result).
void appendGraph(FlowGraph& dst, FlowGraph add);

// Runs one generation request on a worker thread.
class Generator {
public:
    enum class State { Idle, Running, Success, Failed };

    ~Generator();

    // No-op while busy. Resets state to Running and launches the backend.
    void start(const Config& cfg, const std::string& systemPrompt,
               const std::string& userPrompt);
    // Kills the whole backend process tree; state becomes Failed ("Cancelled").
    void cancel();

    State state() const { return state_.load(); }
    bool busy() const { return state_.load() == State::Running; }
    std::string reply() const;  // the model's raw text (Success)
    std::string error() const;  // failure reason (Failed)

private:
    void finish(State s, const std::string& reply, const std::string& error);

    std::thread thread_;
    std::atomic<State> state_{State::Idle};
    std::atomic<bool> cancelRequested_{false};
    mutable std::mutex mutex_;   // guards reply_/error_
    std::string reply_;
    std::string error_;
    std::mutex jobMutex_;        // orders cancel() vs job handle lifetime
    void* job_ = nullptr;        // HANDLE of the kill-on-close Job Object
};

}  // namespace aigen
