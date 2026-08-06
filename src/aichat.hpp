#pragma once

#include <string>
#include <vector>

#include "json.hpp"

struct Project;
struct SceneData;

// The editor's built-in chat assistant (docs/ai-chat.md): a conversation with
// the configured AI backend (aigen::Config - the same Claude CLI / Copilot CLI /
// OpenAI setting the flow-graph generator uses) that can ANSWER questions about
// the editor and CARRY OUT operations in it.
//
// Three things it is built out of, and the reasons they are separate:
//
//  1. SKILLS = the editor's own documentation. Every docs/*.md page is embedded
//     into the exe (cmake/embed_docs.cmake) and the system prompt carries only
//     an INDEX of them; the model pulls a page in with the read_doc tool when it
//     needs one. So the assistant's knowledge is the repo's documentation rather
//     than a second corpus that would drift - the standing "docs change in the
//     same commit" rule keeps it current for free - and a 25 KB prompt does the
//     work of a 780 KB one.
//  2. TOOLS = one table (tools()) read by the prompt, the validator and both
//     executors. A tool documents itself: adding a row is what makes the model
//     aware of it, so there is no second list to forget. Read tools need nothing
//     but a const Project and live HERE (host-only, harness-testable, no ImGui);
//     Edit and Command tools mutate the model or the editor and therefore live in
//     chat_ui.cpp, where project_ and commitChange() are.
//  3. The LOOP is plain text over the existing backend runner (aigen::Generator):
//     the model answers with one JSON envelope carrying prose plus optional tool
//     calls, the editor runs them, appends the results to the transcript and asks
//     again. No streaming, no provider-specific tool-call protocol - the three
//     backends have three different ones (and two are CLIs), so the envelope is
//     the only thing all of them can do.
//
// Everything here is a pure function of its arguments (no GL, no ImGui, no App),
// which is what makes the prompt, the parser and every read tool exercisable
// from a small host harness - the aigen/livedbg/placement shape.
namespace aichat {

// ---------------------------------------------------------------------------
// Skills: the embedded documentation
// ---------------------------------------------------------------------------

struct DocInfo {
    std::string name;     // file stem, what read_doc takes ("menu-styles")
    std::string title;    // the page's H1
    std::string summary;  // its first sentence, trimmed for the index
};

// Every embedded page, in name order. Derived from the markdown itself, so a
// new docs/ page joins the assistant's index with no list to maintain.
const std::vector<DocInfo>& docIndex();

// One page's markdown, or "" when `name` is not a page. Truncated to maxBytes
// with a visible note (no current page comes close; a page that grows past it
// says so rather than ending mid-sentence in silence).
std::string readDoc(const std::string& name, size_t maxBytes = 48000);

// Full-text search across the embedded pages - the complement read_doc needs,
// because the index only carries each page's title and first sentence and plenty
// of questions ("why do my shadows swap at dusk") land in the MIDDLE of a page
// whose title does not say so. Deliberately grep and not vectors: 66 pages is
// small, an exact-substring hit is one a human can check, and there is no index
// to build, rebuild or ship. Terms are ANDed per line, case-insensitively; when
// nothing matches every term the search falls back to ANY term and says so. Each
// hit is reported with the heading it sits under (that is usually the answer to
// "which part of the page do I read"). `page` limits it to one page.
// Returns "" when there is no hit at all.
std::string searchDocs(const std::string& query, const std::string& page = "",
                       size_t maxHits = 40);

// ---------------------------------------------------------------------------
// Tools
// ---------------------------------------------------------------------------

enum class ToolKind {
    Read,     // answers a question; no side effects; always allowed
    Edit,     // mutates the project model - one undo step, gated by allowEdits
    Command,  // drives the editor (selection, windows, save) - gated the same
};

struct ToolArg {
    const char* name;
    const char* type;  // "string" | "number" | "bool" | "object"
    bool required;
    const char* desc;
};

struct Tool {
    const char* name;
    ToolKind kind;
    const char* desc;
    std::vector<ToolArg> args;
};

// THE table. Prompt catalog, argument validation and both dispatchers read it.
const std::vector<Tool>& tools();
const Tool* tool(const std::string& name);

struct ToolCall {
    std::string name;
    json::Value args;    // the "args" object as the model sent it
    std::string result;  // what the model gets back (filled by the executor)
    bool failed = false;
};

// The properties set_object accepts, and the ONE list of them: the prompt
// documents the table and App::applyChatObjectProp (chat_ui.cpp) switches on the
// same keys. A row added here needs a branch there - a key the executor does not
// handle is reported to the model as unhandled rather than silently ignored.
struct ObjectProp {
    const char* key;
    const char* type;  // "string" | "number" | "bool" | "[x,y,z]"
    const char* desc;
};
const std::vector<ObjectProp>& objectProps();

// Value coercions, shared by the argument readers below and by the set_object
// property executor: a model that sends 4 as "4" (or true as 1) means the same
// thing wherever the value sits, and one policy is what keeps the two paths from
// disagreeing about it.
std::string stringOf(const json::Value& v, const std::string& fallback = "");
double numberOf(const json::Value& v, double fallback = 0.0);
bool boolOf(const json::Value& v, bool fallback = false);

// Argument readers - one spelling for both executors, so a number the model
// sent as a string still works.
std::string argStr(const ToolCall& c, const char* key,
                   const std::string& fallback = "");
double argNum(const ToolCall& c, const char* key, double fallback = 0.0);
bool argBool(const ToolCall& c, const char* key, bool fallback = false);
bool hasArg(const ToolCall& c, const char* key);
const json::Value* argValue(const ToolCall& c, const char* key);

// [x,y,z] (or {x,y,z} / {r,g,b}) into `out`; false when `v` is neither, leaving
// `out` untouched. Every vector argument and property goes through it.
bool vec3(const json::Value& v, float out[3]);

// Name lookups, shared by the read tools here and the edit tools in chat_ui.cpp
// so a name resolves identically everywhere. Exact match first, then
// case-insensitive; an empty scene name means the project's active scene.
int findScene(const Project& p, const std::string& name);
int findObject(const SceneData& sc, const std::string& name);

// "No object named X in scene Y. Objects: ..." - a wrong name is the most common
// tool failure and the correction is worth more than the complaint, so both
// executors report it the same way.
std::string noSuchObject(const SceneData& sc, const std::string& name);
std::string noSuchScene(const Project& p, const std::string& name);

// "type=box, x=3" - the call's arguments on one line, for the transcript and
// the chat window's tool rows.
std::string argsText(const ToolCall& c);

// "" when the call names a known tool and carries its required arguments;
// otherwise the reason, worded for the MODEL to correct itself.
std::string validate(const ToolCall& c);

// ---------------------------------------------------------------------------
// Conversation
// ---------------------------------------------------------------------------

struct Message {
    // Summary is a COMPACTED stretch of earlier conversation: the model's own
    // recap of messages that were replaced by it. It reads like an assistant
    // message to the backend and is marked as a compaction in the window, so a
    // user can see why the assistant no longer remembers a detail.
    enum class Role { User, Assistant, Tool, Summary };
    Role role = Role::User;
    std::string text;             // user request / assistant prose / the recap
    std::vector<ToolCall> calls;  // Assistant: requested; Tool: the same calls
                                  // carrying their results
};

struct Conversation {
    std::vector<Message> messages;
};

// What the editor knows and the prompt needs. Filled by the App, because the
// project, the selection and the window list are its state.
struct Context {
    const Project* project = nullptr;  // null = no project open
    int selectedObject = -1;           // index in the active scene, -1 = none
    bool allowEdits = true;            // the window's "Allow project edits"
    bool dirty = false;                // unsaved model edits
    std::vector<std::string> windows;  // open_window keys the editor accepts
};

// The instruction text: what the assistant is, the reply envelope, the working
// rules, the tool catalog, the documentation index and the live project state.
// Rebuilt per request, so it never drifts from the tool table or the project.
std::string systemPrompt(const Context& ctx);

// The conversation as the text sent alongside the prompt, oldest first. Trimmed
// from the FRONT to `budget` bytes (with a note) - a long chat must cost the
// same as a short one, and the recent turns are the ones that matter. `trimmed`
// receives how many messages were dropped, which is the only honest way to ask
// "did the conversation still fit": the trimmed text is always just UNDER the
// budget, so comparing its size against the budget says nothing (that mistake is
// what made compaction never trigger).
std::string transcript(const Conversation& c, size_t budget = 60000,
                       size_t* trimmed = nullptr);

// Splits one reply into prose and tool calls. Never fails: a reply that is not
// an envelope at all is taken as prose, because a chat backend answering "the
// box is at 0,1,0" in plain English is being helpful, not broken.
void parseReply(const std::string& reply, std::string& say,
                std::vector<ToolCall>& calls);

// ---------------------------------------------------------------------------
// Saved conversations (docs/ai-chat.md "History")
// ---------------------------------------------------------------------------
//
// A conversation is machine-global state, not project data: it belongs to the
// person and their machine, so it lives next to editor.ini (never in the .tyra,
// which would put it in git and on the collaboration wire) - one file per chat,
// grouped by the project's stable `projectId` the way the session remote-cache
// groups its content.
//
// The file carries no timestamp of its own. Its mtime is when it was last
// touched and `chatAge` turns that into "12m ago", which is the only thing
// anyone asks of a chat list - and it needs no clock, no timezone and no format
// decision (the --debug-state precedent).

struct ChatRecord {
    std::string file;   // absolute path
    std::string title;  // derived from the first user message
    int messages = 0;
    long long ageSeconds = 0;  // from the file's mtime
};

// <configDir>/chats/<projectId>/ - created on demand. An empty projectId (no
// project open) groups under "no-project" rather than being dropped: a
// documentation question is worth keeping too.
std::string chatDir(const std::string& projectId);

// The conversation's own name: the first user message, one line, trimmed.
std::string chatTitle(const Conversation& c);

// Writes `c` to `file` (created when the path is empty - the caller keeps the
// path for the rest of the session so later turns overwrite the same file).
// Returns the path written, or "" on failure. `dir` is created if missing.
std::string saveChat(const std::string& dir, const std::string& file,
                     const Conversation& c, const std::string& projectName);

bool loadChat(const std::string& file, Conversation& out);

// Newest first. Files that cannot be parsed are skipped rather than reported:
// a chat list is not the place to explain a corrupt file.
std::vector<ChatRecord> listChats(const std::string& dir);

bool deleteChat(const std::string& file);

// Deletes the oldest files beyond `keep`. Returns how many went, so the caller
// can say so instead of silently losing history.
int pruneChats(const std::string& dir, size_t keep);

// "12m ago" / "3d ago" - no timezone, no format, answers the actual question.
std::string chatAge(long long seconds);

// ---------------------------------------------------------------------------
// Context accounting and compaction (docs/ai-chat.md "What a turn costs")
// ---------------------------------------------------------------------------

// Tokens for a piece of text, ESTIMATED - one token per 4 bytes, the usual
// rule of thumb for English prose with JSON mixed in. It exists for the two
// things a real count cannot answer: what the request you have not sent yet is
// going to cost, and what a backend that reports nothing (the Copilot CLI) just
// charged. Everything else prefers the backend's own numbers (aigen::Usage).
int estimateTokens(const std::string& text);

// What the NEXT request will carry, split the way the cost actually splits.
struct ContextStats {
    int promptTokens = 0;      // the instructions: tools, doc index, project
    int transcriptTokens = 0;  // the conversation, AS SENT (trimming included)
    int totalTokens = 0;       // what the next request carries
    int fullTokens = 0;        // the whole conversation, untrimmed
    int messages = 0;
    int trimmedMessages = 0;   // dropped from the front to fit the budget
    int compactAtTokens = 0;   // the size the conversation is compacted at
    bool overBudget = false;   // it no longer fits: compaction is due
};
ContextStats contextStats(const std::string& systemPrompt, const Conversation& c,
                          size_t transcriptBudget = 60000);

// How much of the conversation compaction would fold away: the number of
// messages from the start that are NOT part of the recent tail worth keeping,
// or 0 when there is nothing worth compacting. `keepTail` messages stay
// verbatim - the recent turns are the ones the next answer depends on.
size_t compactableCount(const Conversation& c, size_t keepTail = 6);

// The request that produces the recap: a system prompt and the transcript of the
// messages being folded. Deliberately its own tiny prompt rather than the chat's
// 21 KB one - summarising needs the conversation, not the tool catalog.
std::string compactSystemPrompt();
std::string compactUserPrompt(const Conversation& c, size_t count);

// Replaces the first `count` messages with one Summary carrying `recap`. An
// existing Summary at the front is folded into the new one, so a long session
// keeps exactly one.
void applyCompaction(Conversation& c, size_t count, const std::string& recap);

// ---------------------------------------------------------------------------
// Read-tool execution (the half that needs nothing but the project)
// ---------------------------------------------------------------------------

// Runs a ToolKind::Read call against `p` (`activeScene` = which scene an
// omitted "scene" argument means). Returns the result text; sets failed when
// the call could not be answered. Never mutates anything.
std::string runReadTool(const Project& p, const ToolCall& c, bool& failed);

// The project overview: scenes with their objects and layers, plus every name a
// flow-graph parameter can reference. Also what `--dump` prints - one answer to
// "describe this project to a model", asked by the CLI and by project_summary.
std::string projectSummaryJson(const Project& p);

}  // namespace aichat
