#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "flowgraph.hpp"
#include "json.hpp"
#include "platform.hpp"

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

// The node catalog alone: one self-documenting line per registered node type
// (pins, params with their tips, semantics), derived live from the registry.
// `category` limits it to one add-menu category ("" = every type). Split out
// of systemPrompt() because the in-editor assistant (aichat) hands it over as a
// tool result instead of carrying all ~190 lines in every prompt.
std::string nodeCatalog(const std::string& category = "");

// One graph in the REPLY schema ("kind" link strings) - what the prompt asks
// for, so it is also what a graph must be shown to a model AS. The project
// file's own writer (project::flowGraphToJson) uses bool flags instead;
// parseGraph reads both.
std::string graphJson(const FlowGraph& fg);

// The first balanced top-level {...} in `text` (string-aware), or "". Models
// wrap JSON in markdown fences and prose; every reply parser here starts here.
std::string extractJsonObject(const std::string& text);

// `doc` with the two mistakes a language model makes inside a JSON string
// repaired, so a strict parse of the result usually succeeds. It is a fixup for
// text WE DID NOT AUTHOR and belongs nowhere near the project reader:
//   - a bare " inside a string ("the „main" scene", a quoted word in prose).
//     A " is taken as the string's terminator only when the next non-space
//     character is one of , } ] : - which is what a real terminator is always
//     followed by, and what an interrupted sentence practically never is;
//   - an invalid escape (a lone \ before an ordinary letter), which our parser
//     rejects outright.
// Returns `doc` unchanged when it finds nothing to repair. It is a heuristic on
// purpose: the alternative for a reply that will not parse is showing the raw
// JSON to a human, which is what this exists to stop.
std::string repairJson(const std::string& doc);

// Parses a model reply into `out` (which is REPLACED). Returns "" on success,
// a human-readable error otherwise. Non-fatal issues (dropped invalid links,
// auto-layout applied) are appended to *warnings when given. Accepts both the
// prompt's link schema ("kind": "exec"/"object"/"pos"/"bool"/"text") and the
// project-file schema ("data"/"pos"/"bool"/"text" bool flags), so it also
// reads graphs exported by the editor itself (--apply-graph round-trips).
std::string parseGraph(const std::string& reply, FlowGraph& out,
                       std::string* warnings = nullptr);

// The same thing from an ALREADY PARSED document, for a caller that got the
// graph as a nested object inside a larger reply (the chat assistant's
// set_graph tool call) - re-serializing it just to parse it again would mean a
// second JSON writer, and this validation must exist exactly once.
std::string parseGraphJson(const json::Value& root, FlowGraph& out,
                           std::string* warnings = nullptr);

// Merges `add` into `dst`: node/link ids are shifted past dst's, positions
// are shifted below dst's lowest node. Used by --apply-graph --append (the
// AI paths instead put the current graph into the prompt and take the
// model's reply as the complete result).
void appendGraph(FlowGraph& dst, FlowGraph add);

// What one request cost. The CLI and the API both report this; the numbers are
// the backend's own, not an estimate, which is why `real` exists - a backend
// that says nothing (the Copilot CLI) leaves it false and the caller falls back
// to estimating from the text it sent.
struct Usage {
    long long inputTokens = 0;   // everything the model read, cache included
    long long outputTokens = 0;
    double costUsd = 0.0;        // 0 = not reported
    bool real = false;           // false = the backend told us nothing
};

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
    Usage usage() const;        // what the backend charged for it

private:
    void finish(State s, const std::string& reply, const std::string& error,
                const Usage& usage = Usage{});

    std::thread thread_;
    std::atomic<State> state_{State::Idle};
    std::atomic<bool> cancelRequested_{false};
    mutable std::mutex mutex_;   // guards reply_/error_
    std::string reply_;
    std::string error_;
    Usage usage_;
    // Orders cancel() against the backend process's lifetime. Process::kill()
    // takes down the whole tree - the shell wrapper AND the node/curl child
    // that is actually burning tokens.
    std::mutex procMutex_;
    std::shared_ptr<platform::Process> proc_;
};

}  // namespace aigen
