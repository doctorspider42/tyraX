// World Facts - the project's central memory of game state (docs/world-facts.md).
//
// A fact is a NAMED, DECLARED value: "world.power.state", "marta.trust",
// "player.hasBasementKey". Graphs read and write them, queries combine them
// into named conditions, rules react to them, and the devkit shows every one
// of them live. It is the declared answer to what the "Variables" flow nodes
// do undeclared - those keep working exactly as they did and are a separate
// namespace; a fact carries a type, a default, a lifetime and documentation,
// and it exists because someone put it in the catalog.
//
// The load-bearing constraint is the console: there is no string-keyed store
// on the EE. Every reference resolves to an INDEX at codegen time, exactly the
// way object references and save values already do, and the runtime store is
// two flat arrays. Nothing here knows about that - `layoutOf` decides the
// slots and templates.cpp emits them.
//
// No GL, no ImGui, no project.hpp: the aobake/placement/livedbg shape, so the
// whole condition language (parse-free - it is a tree, not text), its
// evaluator, the cycle detection and the validator are exercisable from a
// 40-line harness. project.hpp includes THIS; facts.cpp is what may look back
// at a Project.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace facts {

// --------------------------------------------------------------- the fact ---

// What a fact holds. Every scalar type shares ONE runtime plane (a float
// array): a bool is != 0, an int is a whole number, an enum is an index into
// `Fact::options`. The type is what the EDITOR uses to pick widgets and what
// the validator checks writes against - it is not four storage layouts.
// Position is the exception and gets its own plane, because three floats do
// not fit in one.
enum class Type { Bool, Int, Float, Enum, Position };

// How long a fact lives. This is the only thing that decides which walk picks
// it up, so it is the whole persistence story in one field.
enum class Persist {
    Session,     // zeroed at boot, kept across scene switches, never written
    Checkpoint,  // rides the in-RAM checkpoint buffer (death/reload)
    Save,        // the above, plus written to the memory card slot
    Profile,     // outside the slots: one file per card, shared by every save
};

// Who owns the value. Deliberately TWO, not the six a design document reaches
// for: "graph" and "entity" scope already exist as a graph class's own members
// (docs/live-debugger.md's watch list shows them), and re-homing them here
// would be a second answer to a solved question that also costs N facts x M
// objects of EE memory. Scene is cheap and genuinely missing, so it is here.
enum class Scope {
    World,  // one value for the whole game
    Scene,  // reset to its default whenever a scene loads
};

// A fact. `id` is what a SAVE FILE stores - names and order may change, a
// player's card may not, so the payload is keyed by this and never by position
// (which is exactly the bug the legacy save values still carry).
struct Fact {
    std::string id;    // stable 16-hex, assigned once by ensureFactIds()
    std::string name;  // "world.power.state" - what graphs reference
    Type type = Type::Bool;
    Persist persist = Persist::Session;
    Scope scope = Scope::World;
    float value = 0.0f;        // default on a fresh game (scalar types)
    float pos[3] = {0, 0, 0};  // default for Type::Position
    std::vector<std::string> options;  // Enum labels; index IS the value
    std::string desc;                  // what this fact means, for humans
    // A COMPUTED fact has no storage: it is the named query below, evaluated
    // where it is read. "Marta.IsAlly" is the worked example - a thing derived
    // from other facts that nothing should ever be able to write.
    std::string computed;  // Query::name, "" = an ordinary stored fact

    bool isComputed() const { return !computed.empty(); }
    bool isPosition() const { return type == Type::Position && !isComputed(); }
};

bool operator==(const Fact& a, const Fact& b);
inline bool operator!=(const Fact& a, const Fact& b) { return !(a == b); }

// ---------------------------------------------------------- the condition ---

// How a leaf compares. `cpp` is the operator codegen emits, so the table below
// is the single source for the UI label, the serialized key and the generated
// expression.
enum class Cmp { Equal, NotEqual, AtLeast, AtMost, Greater, Less };

// One node of a condition tree. ALL/ANY/NOT group; Compare tests one fact
// against a constant OR against another fact; Query names a reusable one.
//
// A tree rather than an expression string: there is nothing to parse, the
// editor edits it directly, codegen folds it into one C++ expression with no
// runtime cost, and - the reason it is a tree and not a flow graph - the HOST
// can evaluate it against a blackboard snapshot and say which leaf decided the
// answer. That is the "Why?" diagnostic, and it is only affordable because the
// same structure serves all three.
struct Condition {
    enum class Kind { All, Any, Not, Compare, Query };
    Kind kind = Kind::All;
    std::vector<Condition> children;  // All / Any / Not

    // Compare
    std::string fact;   // left-hand side: a fact name
    Cmp cmp = Cmp::AtLeast;
    float value = 0.0f;   // right-hand side when `rhsFact` is empty
    std::string rhsFact;  // right-hand side as another fact ("" = use `value`)

    // Query
    std::string query;  // Query::name

    bool empty() const {
        return (kind == Kind::All || kind == Kind::Any || kind == Kind::Not) &&
               children.empty();
    }
};

bool operator==(const Condition& a, const Condition& b);
inline bool operator!=(const Condition& a, const Condition& b) {
    return !(a == b);
}

// A named, reusable condition - "CanEnterBasement". The same asset gates a
// door, a dialogue line, an NPC's behaviour and a flow graph, which is the
// entire point: one place to change when the design does.
struct Query {
    std::string name;
    std::string desc;
    Condition root;
};

bool operator==(const Query& a, const Query& b);
inline bool operator!=(const Query& a, const Query& b) { return !(a == b); }

// -------------------------------------------------------------- the rules ---

// What a rule does when it fires. Deliberately small: a rule reacts, it does
// not orchestrate - anything with steps, timing or branching is a flow graph,
// and Send Event is the door to one.
struct RuleAction {
    enum class Kind { SetFact, AddFact, ToggleFact, SendEvent };
    Kind kind = Kind::SetFact;
    std::string target;  // fact name, or event name for SendEvent
    float value = 0.0f;
};

bool operator==(const RuleAction& a, const RuleAction& b);

// When a rule that evaluates true actually runs.
enum class RulePolicy {
    OnBecomeTrue,  // the false -> true transition only (the sane default)
    WhileTrue,     // every frame the condition holds
    Once,          // the first transition, ever, and then never again
};

struct Rule {
    std::string name;
    std::string desc;
    bool enabled = true;
    RulePolicy policy = RulePolicy::OnBecomeTrue;
    Condition when;
    std::vector<RuleAction> then;
};

bool operator==(const Rule& a, const Rule& b);
inline bool operator!=(const Rule& a, const Rule& b) { return !(a == b); }

// A saved set of fact values - "generator repaired, Marta saved, no key". The
// editor pushes one into the running game so a situation twenty minutes deep
// can be looked at now.
struct ScenarioValue {
    std::string fact;
    float value = 0.0f;
    float pos[3] = {0, 0, 0};
};

bool operator==(const ScenarioValue& a, const ScenarioValue& b);

struct Scenario {
    std::string name;
    std::string desc;
    std::vector<ScenarioValue> values;
};

bool operator==(const Scenario& a, const Scenario& b);
inline bool operator!=(const Scenario& a, const Scenario& b) {
    return !(a == b);
}

// -------------------------------------------------------- the ONE tables ---
// Every enum above has exactly one table describing it, and the serializer,
// the codegen, the widgets and their tooltips all read it. A new type / tier /
// operator is one row here plus wherever it genuinely behaves differently -
// the menustyle::propSpecs() arrangement.

struct TypeInfo {
    Type type;
    const char* key;    // serialized in the .tyra - never change one
    const char* label;  // what the UI calls it
    const char* desc;
};
const std::vector<TypeInfo>& typeInfos();
const char* typeKey(Type t);
const char* typeLabel(Type t);
Type typeFromKey(const std::string& key);

struct PersistInfo {
    Persist persist;
    const char* key;
    const char* label;
    const char* desc;
};
const std::vector<PersistInfo>& persistInfos();
const char* persistKey(Persist p);
const char* persistLabel(Persist p);
Persist persistFromKey(const std::string& key);

struct ScopeInfo {
    Scope scope;
    const char* key;
    const char* label;
    const char* desc;
};
const std::vector<ScopeInfo>& scopeInfos();
const char* scopeKey(Scope s);
Scope scopeFromKey(const std::string& key);

struct CmpInfo {
    Cmp op;
    const char* key;    // serialized
    const char* label;  // "at least"
    const char* cpp;    // ">=" - what codegen emits
};
const std::vector<CmpInfo>& cmpInfos();
const char* cmpKey(Cmp c);
const char* cmpLabel(Cmp c);
const char* cmpCpp(Cmp c);
Cmp cmpFromKey(const std::string& key);

struct PolicyInfo {
    RulePolicy policy;
    const char* key;
    const char* label;
    const char* desc;
};
const std::vector<PolicyInfo>& policyInfos();
const char* policyKey(RulePolicy p);
RulePolicy policyFromKey(const std::string& key);

struct ActionKindInfo {
    RuleAction::Kind kind;
    const char* key;
    const char* label;
};
const std::vector<ActionKindInfo>& actionKindInfos();
const char* actionKindKey(RuleAction::Kind k);
RuleAction::Kind actionKindFromKey(const std::string& key);

const char* conditionKindKey(Condition::Kind k);
Condition::Kind conditionKindFromKey(const std::string& key);

// -------------------------------------------------------------- the store ---

// Which runtime slot every fact lives in. Scalars share one float array and
// positions another; a computed fact has no slot at all, which is the whole
// reason it costs nothing. Built once by codegen AND by the editor's
// blackboard, so the index a graph was compiled against and the index the
// debugger reads are the same number by construction.
struct Layout {
    std::vector<int> numOf;  // fact index -> factNum slot, or -1
    std::vector<int> posOf;  // fact index -> factPos slot, or -1
    int numCount = 0;
    int posCount = 0;
};

Layout layoutOf(const std::vector<Fact>& facts);

/** Index into `facts` of the fact with this name, or -1. */
int indexOf(const std::vector<Fact>& facts, const std::string& name);
/** Index into `queries` of the query with this name, or -1. */
int queryIndexOf(const std::vector<Query>& queries, const std::string& name);

/** The value a fresh game starts a fact at, as the runtime stores it. */
float defaultNum(const Fact& f);

/** A fact's value rendered for a human ("true", "3", "Repaired", "1.5"). */
std::string formatValue(const Fact& f, const float* v3);

// ---------------------------------------------------------- the evaluator ---

/** Reads the current value of a fact, into three floats (only [0] is
 * meaningful for scalars). Returns false when the fact is unknown - the
 * evaluator then treats the leaf as false rather than inventing a zero. */
using ValueFn = std::function<bool(const std::string& factName, float* out3)>;

/** One node of a "Why?" explanation - the condition tree with the value each
 * leaf actually read and the verdict it produced. */
struct Explain {
    std::string text;   // "marta.trust >= 5  (marta.trust = 3)"
    bool value = false;
    bool decisive = false;  // this child is why the parent answered as it did
    std::vector<Explain> children;
};

/** Evaluates a condition against live values. `out` may be null when only the
 * answer is wanted; a cyclic or unknown query evaluates false and says so in
 * the explanation rather than recursing. */
bool evaluate(const Condition& c, const std::vector<Fact>& facts,
              const std::vector<Query>& queries, const ValueFn& read,
              Explain* out = nullptr);

/** Renders an Explain tree as indented lines, newest-first parents. */
std::string explainText(const Explain& e, int indent = 0);

// ---------------------------------------------------------- the validator ---

struct Issue {
    std::string where;  // "fact 'marta.trust'", "rule 'PowerRestored'"
    std::string text;
    bool error = false;  // false = warning (builds), true = must be fixed
};

/** Everything the editor can know is wrong without running the game: duplicate
 * or empty names, writes to computed facts, unknown references, enum values
 * out of range, scene-scoped facts asking to be saved, and cycles - both
 * query -> query and rule -> rule (a rule whose actions feed a rule whose
 * actions feed it back is an infinite reaction, and the console is where you
 * do not want to find that out). */
std::vector<Issue> validate(const std::vector<Fact>& facts,
                            const std::vector<Query>& queries,
                            const std::vector<Rule>& rules,
                            const std::vector<Scenario>& scenarios);

/** Which queries take part in a reference cycle. `validate` reports these as
 * errors, but codegen must not depend on the author having fixed them: a
 * cyclic query is emitted as a constant false instead of recursing forever,
 * so a broken catalog produces a build with a wrong answer rather than a
 * compiler that never returns. */
std::vector<bool> cyclicQueries(const std::vector<Query>& queries);

/** The facts a condition reads, appended to `out` (deduplicated by caller). */
void conditionFacts(const Condition& c, const std::vector<Query>& queries,
                    std::vector<std::string>& out);
/** The queries a condition names, directly or through other queries. */
void conditionQueries(const Condition& c, const std::vector<Query>& queries,
                      std::vector<std::string>& out);

// The rule engine's per-frame cascade guard. A rule may set a fact another
// rule watches, so the generated tick re-evaluates until nothing changes -
// bounded by this, because "until nothing changes" is otherwise a hang on a
// console with no way to break in. Mirrored by the generated runtime.
constexpr int kMaxRulePasses = 8;

// Ceilings the generated tables are sized by. Generous - a fact is four bytes
// and a rule is an `if` - but a project that walks past one gets a build-time
// message instead of a silently truncated world.
constexpr int kMaxFacts = 512;
constexpr int kMaxQueries = 256;
constexpr int kMaxRules = 256;

}  // namespace facts
