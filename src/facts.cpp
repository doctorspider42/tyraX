#include "facts.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace facts {
namespace {

// Floats compared for equality need a tolerance, and a fact's whole point is
// that a designer typed "3" somewhere and expects it to match. One epsilon,
// used by Equal and NotEqual and by nothing else.
constexpr float kEps = 1e-4f;

bool sameVec3(const float* a, const float* b) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

std::string trimNum(float v) {
    // %g without the exponent noise for the numbers a fact actually holds.
    char buf[32];
    if (v == (float)(long long)v && std::fabs(v) < 1e9f)
        std::snprintf(buf, sizeof(buf), "%lld", (long long)v);
    else
        std::snprintf(buf, sizeof(buf), "%.4g", (double)v);
    return buf;
}

bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

}  // namespace

// ----------------------------------------------------------------- tables ---

const std::vector<TypeInfo>& typeInfos() {
    static const std::vector<TypeInfo> v = {
        {Type::Bool, "bool", "Yes / no",
         "A flag: the generator is repaired, the player met Marta. The type "
         "most facts want - reach for an enum the moment you find yourself "
         "writing two bools that cannot both be true."},
        {Type::Int, "int", "Whole number",
         "A count or a score: how many parts were found, how many times the "
         "player was caught. Stored as a float and rounded on write, so it is "
         "exact up to 16 million."},
        {Type::Float, "float", "Number",
         "A continuous value - a fuel level, a percentage. Compare it with "
         "'at least' rather than 'is', the way you would any float."},
        {Type::Enum, "enum", "One of several",
         "A named state: Broken / Powered / Overloaded. The value is the index "
         "into the option list, so the editor and the blackboard print the "
         "NAME while the game compares an int - the cheapest thing on the "
         "console and the most readable thing in the editor."},
        {Type::Position, "position", "Position",
         "Three floats: a remembered spawn point, the place the player last "
         "died. Lives in its own runtime array and cannot be compared - wire "
         "it into a position input instead."},
    };
    return v;
}

const std::vector<PersistInfo>& persistInfos() {
    static const std::vector<PersistInfo> v = {
        {Persist::Session, "session", "Session",
         "Zeroed at boot, kept across scene switches, never written anywhere. "
         "What the undeclared Variables nodes have always done."},
        {Persist::Checkpoint, "checkpoint", "Checkpoint",
         "Rides the in-RAM checkpoint buffer, so Load Checkpoint puts it back. "
         "Lost when the game closes - the tier for 'progress within a life'."},
        {Persist::Save, "save", "Save game",
         "The checkpoint buffer AND the memory card slot, so it survives the "
         "console being switched off. Keyed by the fact's id, so renaming or "
         "reordering facts cannot corrupt a player's existing save."},
        {Persist::Profile, "profile", "Profile",
         "One file per card, outside the save slots and shared by all of them "
         "- unlocks, a best time, 'has seen the intro'. Written whenever it "
         "changes, not at a checkpoint."},
    };
    return v;
}

const std::vector<ScopeInfo>& scopeInfos() {
    static const std::vector<ScopeInfo> v = {
        {Scope::World, "world", "World",
         "One value for the whole game, kept across scene switches."},
        {Scope::Scene, "scene", "Scene",
         "Reset to its default every time a scene loads. For state that only "
         "means something inside the level it belongs to - a puzzle's step "
         "counter, whether this room's alarm is ringing."},
    };
    return v;
}

const std::vector<CmpInfo>& cmpInfos() {
    static const std::vector<CmpInfo> v = {
        {Cmp::Equal, "eq", "is", "=="},
        {Cmp::NotEqual, "ne", "is not", "!="},
        {Cmp::AtLeast, "ge", "at least", ">="},
        {Cmp::AtMost, "le", "at most", "<="},
        {Cmp::Greater, "gt", "more than", ">"},
        {Cmp::Less, "lt", "less than", "<"},
    };
    return v;
}

const std::vector<PolicyInfo>& policyInfos() {
    static const std::vector<PolicyInfo> v = {
        {RulePolicy::OnBecomeTrue, "rising", "When it becomes true",
         "Only on the false -> true transition. The default, and what you want "
         "almost always: the reaction happens once per time the world enters "
         "that state."},
        {RulePolicy::WhileTrue, "while", "Every frame while true",
         "Runs every frame the condition holds. Correct for something that has "
         "to be re-asserted (a light that must stay on), a mistake for "
         "anything that counts or sends."},
        {RulePolicy::Once, "once", "Once per run",
         "The first transition and then never again, even if the condition "
         "goes false and true a second time. 'Per run' is exact: the spent "
         "flag lives in the engine and is cleared when the game restarts, so "
         "a rule that must stay spent across a save should write a fact and "
         "test it in its own condition."},
    };
    return v;
}

const std::vector<ActionKindInfo>& actionKindInfos() {
    static const std::vector<ActionKindInfo> v = {
        {RuleAction::Kind::SetFact, "set", "Set fact"},
        {RuleAction::Kind::AddFact, "add", "Add to fact"},
        {RuleAction::Kind::ToggleFact, "toggle", "Toggle fact"},
        {RuleAction::Kind::SendEvent, "event", "Send event"},
    };
    return v;
}

const char* typeKey(Type t) {
    for (const TypeInfo& i : typeInfos())
        if (i.type == t) return i.key;
    return "bool";
}
const char* typeLabel(Type t) {
    for (const TypeInfo& i : typeInfos())
        if (i.type == t) return i.label;
    return "Yes / no";
}
Type typeFromKey(const std::string& key) {
    for (const TypeInfo& i : typeInfos())
        if (key == i.key) return i.type;
    return Type::Bool;
}

const char* persistKey(Persist p) {
    for (const PersistInfo& i : persistInfos())
        if (i.persist == p) return i.key;
    return "session";
}
const char* persistLabel(Persist p) {
    for (const PersistInfo& i : persistInfos())
        if (i.persist == p) return i.label;
    return "Session";
}
Persist persistFromKey(const std::string& key) {
    for (const PersistInfo& i : persistInfos())
        if (key == i.key) return i.persist;
    return Persist::Session;
}

const char* scopeKey(Scope s) {
    for (const ScopeInfo& i : scopeInfos())
        if (i.scope == s) return i.key;
    return "world";
}
Scope scopeFromKey(const std::string& key) {
    for (const ScopeInfo& i : scopeInfos())
        if (key == i.key) return i.scope;
    return Scope::World;
}

const char* cmpKey(Cmp c) {
    for (const CmpInfo& i : cmpInfos())
        if (i.op == c) return i.key;
    return "ge";
}
const char* cmpLabel(Cmp c) {
    for (const CmpInfo& i : cmpInfos())
        if (i.op == c) return i.label;
    return "at least";
}
const char* cmpCpp(Cmp c) {
    for (const CmpInfo& i : cmpInfos())
        if (i.op == c) return i.cpp;
    return ">=";
}
Cmp cmpFromKey(const std::string& key) {
    for (const CmpInfo& i : cmpInfos())
        if (key == i.key) return i.op;
    return Cmp::AtLeast;
}

const char* policyKey(RulePolicy p) {
    for (const PolicyInfo& i : policyInfos())
        if (i.policy == p) return i.key;
    return "rising";
}
RulePolicy policyFromKey(const std::string& key) {
    for (const PolicyInfo& i : policyInfos())
        if (key == i.key) return i.policy;
    return RulePolicy::OnBecomeTrue;
}

const char* actionKindKey(RuleAction::Kind k) {
    for (const ActionKindInfo& i : actionKindInfos())
        if (i.kind == k) return i.key;
    return "set";
}
RuleAction::Kind actionKindFromKey(const std::string& key) {
    for (const ActionKindInfo& i : actionKindInfos())
        if (key == i.key) return i.kind;
    return RuleAction::Kind::SetFact;
}

const char* conditionKindKey(Condition::Kind k) {
    switch (k) {
        case Condition::Kind::All: return "all";
        case Condition::Kind::Any: return "any";
        case Condition::Kind::Not: return "not";
        case Condition::Kind::Compare: return "compare";
        case Condition::Kind::Query: return "query";
    }
    return "all";
}

Condition::Kind conditionKindFromKey(const std::string& key) {
    if (key == "any") return Condition::Kind::Any;
    if (key == "not") return Condition::Kind::Not;
    if (key == "compare") return Condition::Kind::Compare;
    if (key == "query") return Condition::Kind::Query;
    return Condition::Kind::All;
}

// -------------------------------------------------------------- equality ---
// History::push() short-circuits on equality and the section-blob guard
// compares whole sections, so every field a user can edit must be in here.

bool operator==(const Fact& a, const Fact& b) {
    return a.id == b.id && a.name == b.name && a.type == b.type &&
           a.persist == b.persist && a.scope == b.scope && a.value == b.value &&
           sameVec3(a.pos, b.pos) && a.options == b.options &&
           a.desc == b.desc && a.computed == b.computed;
}

bool operator==(const Condition& a, const Condition& b) {
    return a.kind == b.kind && a.fact == b.fact && a.cmp == b.cmp &&
           a.value == b.value && a.rhsFact == b.rhsFact && a.query == b.query &&
           a.children == b.children;
}

bool operator==(const Query& a, const Query& b) {
    return a.name == b.name && a.desc == b.desc && a.root == b.root;
}

bool operator==(const RuleAction& a, const RuleAction& b) {
    return a.kind == b.kind && a.target == b.target && a.value == b.value;
}

bool operator==(const Rule& a, const Rule& b) {
    return a.name == b.name && a.desc == b.desc && a.enabled == b.enabled &&
           a.policy == b.policy && a.when == b.when && a.then == b.then;
}

bool operator==(const ScenarioValue& a, const ScenarioValue& b) {
    return a.fact == b.fact && a.value == b.value && sameVec3(a.pos, b.pos);
}

bool operator==(const Scenario& a, const Scenario& b) {
    return a.name == b.name && a.desc == b.desc && a.values == b.values;
}

// ----------------------------------------------------------------- store ---

Layout layoutOf(const std::vector<Fact>& facts) {
    Layout l;
    l.numOf.assign(facts.size(), -1);
    l.posOf.assign(facts.size(), -1);
    for (size_t i = 0; i < facts.size(); ++i) {
        const Fact& f = facts[i];
        // A computed fact is an expression, not storage. That is what makes it
        // free, and also why nothing may write to one.
        if (f.isComputed() || f.name.empty()) continue;
        if (f.type == Type::Position)
            l.posOf[i] = l.posCount++;
        else
            l.numOf[i] = l.numCount++;
    }
    return l;
}

int indexOf(const std::vector<Fact>& facts, const std::string& name) {
    if (name.empty()) return -1;
    for (size_t i = 0; i < facts.size(); ++i)
        if (facts[i].name == name) return (int)i;
    return -1;
}

int queryIndexOf(const std::vector<Query>& queries, const std::string& name) {
    if (name.empty()) return -1;
    for (size_t i = 0; i < queries.size(); ++i)
        if (queries[i].name == name) return (int)i;
    return -1;
}

float defaultNum(const Fact& f) {
    switch (f.type) {
        case Type::Bool: return f.value != 0.0f ? 1.0f : 0.0f;
        case Type::Int:
        case Type::Enum: return (float)(long long)std::lround(f.value);
        case Type::Float: return f.value;
        case Type::Position: return 0.0f;
    }
    return 0.0f;
}

std::string formatValue(const Fact& f, const float* v3) {
    if (!v3) return "-";
    switch (f.type) {
        case Type::Bool: return v3[0] != 0.0f ? "true" : "false";
        case Type::Int: return trimNum((float)std::lround(v3[0]));
        case Type::Float: return trimNum(v3[0]);
        case Type::Enum: {
            const long idx = std::lround(v3[0]);
            if (idx >= 0 && idx < (long)f.options.size())
                return f.options[(size_t)idx];
            return "#" + std::to_string(idx);
        }
        case Type::Position:
            return "(" + trimNum(v3[0]) + ", " + trimNum(v3[1]) + ", " +
                   trimNum(v3[2]) + ")";
    }
    return "-";
}

// ------------------------------------------------------------- evaluator ---

namespace {

bool evalImpl(const Condition& c, const std::vector<Fact>& facts,
              const std::vector<Query>& queries, const ValueFn& read,
              std::vector<std::string>& path, Explain* out);

// Reads a fact through the value function, following a computed fact into its
// query. Returns false when the name resolves to nothing at all.
bool readFact(const std::string& name, const std::vector<Fact>& facts,
              const std::vector<Query>& queries, const ValueFn& read,
              std::vector<std::string>& path, float* out3) {
    out3[0] = out3[1] = out3[2] = 0.0f;
    const int fi = indexOf(facts, name);
    if (fi < 0) return false;
    const Fact& f = facts[(size_t)fi];
    if (f.isComputed()) {
        const int qi = queryIndexOf(queries, f.computed);
        if (qi < 0) return false;
        if (contains(path, "q:" + f.computed)) return false;  // cycle
        path.push_back("q:" + f.computed);
        const bool v =
            evalImpl(queries[(size_t)qi].root, facts, queries, read, path,
                     nullptr);
        path.pop_back();
        out3[0] = v ? 1.0f : 0.0f;
        return true;
    }
    return read(name, out3);
}

bool applyCmp(Cmp op, float a, float b) {
    switch (op) {
        case Cmp::Equal: return std::fabs(a - b) <= kEps;
        case Cmp::NotEqual: return std::fabs(a - b) > kEps;
        case Cmp::AtLeast: return a >= b;
        case Cmp::AtMost: return a <= b;
        case Cmp::Greater: return a > b;
        case Cmp::Less: return a < b;
    }
    return false;
}

std::string leafText(const Condition& c, const std::vector<Fact>& facts,
                     bool known, float lhs, bool rhsKnown, float rhs) {
    const int fi = indexOf(facts, c.fact);
    std::string s = c.fact.empty() ? "<no fact>" : c.fact;
    s += " ";
    s += cmpLabel(c.cmp);
    s += " ";
    if (!c.rhsFact.empty()) {
        s += c.rhsFact;
        if (rhsKnown) s += " (" + trimNum(rhs) + ")";
    } else if (fi >= 0) {
        const float v3[3] = {c.value, 0, 0};
        s += formatValue(facts[(size_t)fi], v3);
    } else {
        s += trimNum(c.value);
    }
    if (!known) {
        s += "   [unknown fact]";
    } else if (fi >= 0) {
        const float v3[3] = {lhs, 0, 0};
        s += "   (" + c.fact + " = " + formatValue(facts[(size_t)fi], v3) + ")";
    }
    return s;
}

bool evalImpl(const Condition& c, const std::vector<Fact>& facts,
              const std::vector<Query>& queries, const ValueFn& read,
              std::vector<std::string>& path, Explain* out) {
    switch (c.kind) {
        case Condition::Kind::All:
        case Condition::Kind::Any:
        case Condition::Kind::Not: {
            const bool isAll = c.kind == Condition::Kind::All;
            // ALL with nothing in it is true (nothing failed) and ANY is false
            // (nothing passed) - the same arithmetic every other fold uses, and
            // the reason an empty group never silently gates something open.
            bool v = isAll;
            if (c.kind != Condition::Kind::All) v = false;
            std::vector<bool> kids;
            kids.reserve(c.children.size());
            for (const Condition& ch : c.children) {
                Explain sub;
                const bool r =
                    evalImpl(ch, facts, queries, read, path, out ? &sub : nullptr);
                kids.push_back(r);
                if (out) out->children.push_back(std::move(sub));
                if (isAll)
                    v = v && r;
                else
                    v = v || r;
            }
            // NOT folds its children with OR and negates - one child is plain
            // negation, which is the shape it is used in; several read as
            // "none of these".
            if (c.kind == Condition::Kind::Not) v = !v;
            if (out) {
                out->value = v;
                out->text = c.kind == Condition::Kind::All   ? "ALL of"
                            : c.kind == Condition::Kind::Any ? "ANY of"
                                                             : "NONE of";
                if (c.children.empty())
                    out->text += " (empty)";
                // The decisive child: for ALL the first that failed, for ANY
                // the first that passed. That is the line a designer is
                // looking for and the reason this is a tree.
                for (size_t i = 0; i < kids.size() && i < out->children.size();
                     ++i) {
                    if (isAll ? !kids[i] : kids[i]) {
                        out->children[i].decisive = true;
                        break;
                    }
                }
            }
            return v;
        }
        case Condition::Kind::Compare: {
            float lhs[3] = {0, 0, 0};
            const bool known = readFact(c.fact, facts, queries, read, path, lhs);
            float rhs = c.value;
            bool rhsKnown = true;
            if (!c.rhsFact.empty()) {
                float r3[3] = {0, 0, 0};
                rhsKnown = readFact(c.rhsFact, facts, queries, read, path, r3);
                rhs = r3[0];
            }
            const bool v = known && rhsKnown && applyCmp(c.cmp, lhs[0], rhs);
            if (out) {
                out->value = v;
                out->text = leafText(c, facts, known, lhs[0], rhsKnown, rhs);
            }
            return v;
        }
        case Condition::Kind::Query: {
            const int qi = queryIndexOf(queries, c.query);
            if (qi < 0) {
                if (out) {
                    out->value = false;
                    out->text = "query '" + c.query + "'   [not found]";
                }
                return false;
            }
            const std::string key = "q:" + c.query;
            if (contains(path, key)) {
                if (out) {
                    out->value = false;
                    out->text = "query '" + c.query + "'   [cycle]";
                }
                return false;
            }
            path.push_back(key);
            Explain sub;
            const bool v = evalImpl(queries[(size_t)qi].root, facts, queries,
                                    read, path, out ? &sub : nullptr);
            path.pop_back();
            if (out) {
                out->value = v;
                out->text = "query '" + c.query + "'";
                out->children.push_back(std::move(sub));
            }
            return v;
        }
    }
    return false;
}

}  // namespace

bool evaluate(const Condition& c, const std::vector<Fact>& facts,
              const std::vector<Query>& queries, const ValueFn& read,
              Explain* out) {
    std::vector<std::string> path;
    if (out) *out = Explain{};
    return evalImpl(c, facts, queries, read, path, out);
}

std::string explainText(const Explain& e, int indent) {
    std::ostringstream o;
    o << std::string((size_t)indent * 2, ' ') << (e.value ? "[x] " : "[ ] ")
      << e.text << (e.decisive ? "   <- decides it" : "") << "\n";
    for (const Explain& c : e.children) o << explainText(c, indent + 1);
    return o.str();
}

// ------------------------------------------------------------ dependencies ---

std::vector<bool> cyclicQueries(const std::vector<Query>& queries) {
    const size_t n = queries.size();
    std::vector<bool> bad(n, false);
    enum Colour { White, Grey, Black };
    std::vector<Colour> colour(n, White);

    std::function<void(size_t)> visit = [&](size_t i) {
        colour[i] = Grey;
        std::vector<std::string> direct;
        std::function<void(const Condition&)> scan = [&](const Condition& c) {
            if (c.kind == Condition::Kind::Query) {
                if (!c.query.empty() && !contains(direct, c.query))
                    direct.push_back(c.query);
                return;
            }
            for (const Condition& ch : c.children) scan(ch);
        };
        scan(queries[i].root);
        for (const std::string& nm : direct) {
            const int qi = queryIndexOf(queries, nm);
            if (qi < 0) continue;
            if (colour[(size_t)qi] == Grey) {
                // Both ends of the back edge are unusable: the one that closes
                // the loop and the one it points at.
                bad[i] = true;
                bad[(size_t)qi] = true;
            } else if (colour[(size_t)qi] == White) {
                visit((size_t)qi);
                if (bad[(size_t)qi]) bad[i] = true;
            } else if (bad[(size_t)qi]) {
                bad[i] = true;
            }
        }
        colour[i] = Black;
    };

    for (size_t i = 0; i < n; ++i)
        if (colour[i] == White) visit(i);
    return bad;
}

void conditionFacts(const Condition& c, const std::vector<Query>& queries,
                    std::vector<std::string>& out) {
    // Depth guard through a local visited list: a query cycle is a validation
    // error, not a reason for this walk to hang.
    struct Walk {
        const std::vector<Query>& q;
        std::vector<std::string> seen;
        std::vector<std::string>* out;
        void go(const Condition& c) {
            switch (c.kind) {
                case Condition::Kind::Compare:
                    if (!c.fact.empty() && !contains(*out, c.fact))
                        out->push_back(c.fact);
                    if (!c.rhsFact.empty() && !contains(*out, c.rhsFact))
                        out->push_back(c.rhsFact);
                    break;
                case Condition::Kind::Query: {
                    if (contains(seen, c.query)) break;
                    seen.push_back(c.query);
                    const int qi = queryIndexOf(q, c.query);
                    if (qi >= 0) go(q[(size_t)qi].root);
                    break;
                }
                default:
                    for (const Condition& ch : c.children) go(ch);
                    break;
            }
        }
    } w{queries, {}, &out};
    w.go(c);
}

void conditionQueries(const Condition& c, const std::vector<Query>& queries,
                      std::vector<std::string>& out) {
    switch (c.kind) {
        case Condition::Kind::Query: {
            if (c.query.empty() || contains(out, c.query)) return;
            out.push_back(c.query);
            const int qi = queryIndexOf(queries, c.query);
            if (qi >= 0) conditionQueries(queries[(size_t)qi].root, queries, out);
            return;
        }
        case Condition::Kind::Compare: return;
        default:
            for (const Condition& ch : c.children)
                conditionQueries(ch, queries, out);
            return;
    }
}

// -------------------------------------------------------------- validator ---

namespace {

// A rule's write set: the facts it can change. Send Event is not in it - an
// event reaches a graph, and a graph is not part of this dependency picture
// (it has its own frame, and the runtime's pass cap is what bounds it).
std::vector<std::string> ruleWrites(const Rule& r) {
    std::vector<std::string> w;
    for (const RuleAction& a : r.then) {
        if (a.kind == RuleAction::Kind::SendEvent) continue;
        if (!a.target.empty() && !contains(w, a.target)) w.push_back(a.target);
    }
    return w;
}

void findQueryCycles(const std::vector<Query>& queries,
                     std::vector<Issue>& out) {
    // Standard three-colour DFS. A query that names itself, directly or
    // through a chain, would recurse forever in codegen - which is a hang at
    // BUILD time, so it must be an error and not a warning.
    enum Colour { White, Grey, Black };
    std::vector<Colour> colour(queries.size(), White);
    std::vector<std::string> stack;

    std::function<void(size_t)> visit = [&](size_t i) {
        colour[i] = Grey;
        stack.push_back(queries[i].name);
        std::vector<std::string> direct;
        // Only the DIRECT references - conditionQueries would flatten the
        // chain and lose the edge that closes the loop.
        std::function<void(const Condition&)> scan = [&](const Condition& c) {
            if (c.kind == Condition::Kind::Query) {
                if (!c.query.empty() && !contains(direct, c.query))
                    direct.push_back(c.query);
                return;
            }
            for (const Condition& ch : c.children) scan(ch);
        };
        scan(queries[i].root);
        for (const std::string& n : direct) {
            const int qi = queryIndexOf(queries, n);
            if (qi < 0) continue;
            if (colour[(size_t)qi] == Grey) {
                std::string chain;
                for (const std::string& s : stack) chain += s + " -> ";
                chain += n;
                out.push_back({"query '" + queries[i].name + "'",
                               "Cycle: " + chain +
                                   ". A query cannot depend on itself.",
                               true});
            } else if (colour[(size_t)qi] == White) {
                visit((size_t)qi);
            }
        }
        stack.pop_back();
        colour[i] = Black;
    };

    for (size_t i = 0; i < queries.size(); ++i)
        if (colour[i] == White) visit(i);
}

void findRuleCycles(const std::vector<Fact>& facts,
                    const std::vector<Query>& queries,
                    const std::vector<Rule>& rules, std::vector<Issue>& out) {
    // Rule A depends on rule B when B writes a fact A reads. A cycle is a
    // potentially endless reaction - the runtime survives it (the pass cap),
    // but the author almost never meant it, so it is a WARNING with the chain
    // spelled out rather than a refusal to build.
    const size_t n = rules.size();
    std::vector<std::vector<std::string>> reads(n), writes(n);
    for (size_t i = 0; i < n; ++i) {
        conditionFacts(rules[i].when, queries, reads[i]);
        // A computed fact read by a rule really reads the facts behind it.
        std::vector<std::string> expanded = reads[i];
        for (const std::string& fname : reads[i]) {
            const int fi = indexOf(facts, fname);
            if (fi < 0 || !facts[(size_t)fi].isComputed()) continue;
            const int qi = queryIndexOf(queries, facts[(size_t)fi].computed);
            if (qi >= 0) conditionFacts(queries[(size_t)qi].root, queries, expanded);
        }
        reads[i] = expanded;
        writes[i] = ruleWrites(rules[i]);
    }

    enum Colour { White, Grey, Black };
    std::vector<Colour> colour(n, White);
    std::vector<std::string> stack;

    std::function<void(size_t)> visit = [&](size_t i) {
        colour[i] = Grey;
        stack.push_back(rules[i].name);
        for (size_t j = 0; j < n; ++j) {
            if (j == i) continue;
            bool edge = false;
            for (const std::string& w : writes[i])
                if (contains(reads[j], w)) edge = true;
            if (!edge) continue;
            if (colour[j] == Grey) {
                std::string chain;
                for (const std::string& s : stack) chain += s + " -> ";
                chain += rules[j].name;
                out.push_back(
                    {"rule '" + rules[i].name + "'",
                     "Reaction cycle: " + chain +
                         ". The engine stops after " +
                         std::to_string(kMaxRulePasses) +
                         " passes per frame, so this settles rather than "
                         "hanging - but check it is what you meant.",
                     false});
            } else if (colour[j] == White) {
                visit(j);
            }
        }
        stack.pop_back();
        colour[i] = Black;
    };

    for (size_t i = 0; i < n; ++i)
        if (colour[i] == White) visit(i);
}

void validateCondition(const Condition& c, const std::vector<Fact>& facts,
                       const std::vector<Query>& queries,
                       const std::string& where, std::vector<Issue>& out) {
    switch (c.kind) {
        case Condition::Kind::Compare: {
            if (c.fact.empty()) {
                out.push_back({where, "A comparison names no fact.", true});
                break;
            }
            const int fi = indexOf(facts, c.fact);
            if (fi < 0) {
                out.push_back(
                    {where, "Unknown fact '" + c.fact + "'.", true});
                break;
            }
            const Fact& f = facts[(size_t)fi];
            if (f.type == Type::Position)
                out.push_back({where,
                               "'" + c.fact +
                                   "' is a position and cannot be compared.",
                               true});
            if (f.type == Type::Enum && c.rhsFact.empty()) {
                const long idx = std::lround(c.value);
                if (idx < 0 || idx >= (long)f.options.size())
                    out.push_back({where,
                                   "'" + c.fact + "' has no option #" +
                                       std::to_string(idx) + ".",
                                   false});
            }
            if (!c.rhsFact.empty() && indexOf(facts, c.rhsFact) < 0)
                out.push_back(
                    {where, "Unknown fact '" + c.rhsFact + "'.", true});
            break;
        }
        case Condition::Kind::Query:
            if (c.query.empty())
                out.push_back({where, "A query slot names nothing.", true});
            else if (queryIndexOf(queries, c.query) < 0)
                out.push_back(
                    {where, "Unknown query '" + c.query + "'.", true});
            break;
        default:
            if (c.children.empty())
                out.push_back({where,
                               "An empty group always answers the same way - "
                               "ALL is true, ANY is false.",
                               false});
            for (const Condition& ch : c.children)
                validateCondition(ch, facts, queries, where, out);
            break;
    }
}

}  // namespace

std::vector<Issue> validate(const std::vector<Fact>& facts,
                            const std::vector<Query>& queries,
                            const std::vector<Rule>& rules,
                            const std::vector<Scenario>& scenarios) {
    std::vector<Issue> out;

    // ---- facts
    for (size_t i = 0; i < facts.size(); ++i) {
        const Fact& f = facts[i];
        const std::string where = "fact '" + f.name + "'";
        if (f.name.empty()) {
            out.push_back({"fact #" + std::to_string(i), "Has no name.", true});
            continue;
        }
        for (size_t j = 0; j < i; ++j)
            if (facts[j].name == f.name)
                out.push_back({where, "Duplicate name.", true});
        if (f.name.find(' ') != std::string::npos)
            out.push_back({where,
                           "Names carry no spaces - use dots for the "
                           "hierarchy (world.power.state).",
                           false});
        if (f.isComputed()) {
            if (queryIndexOf(queries, f.computed) < 0)
                out.push_back({where,
                               "Computed from unknown query '" + f.computed +
                                   "'.",
                               true});
            if (f.persist != Persist::Session)
                out.push_back({where,
                               "A computed fact has no storage, so it cannot "
                               "be saved. Its persistence is ignored.",
                               false});
            if (f.type != Type::Bool)
                out.push_back({where,
                               "A computed fact is a condition, so it is "
                               "always a yes/no.",
                               false});
        }
        if (f.type == Type::Enum && f.options.empty())
            out.push_back({where, "An option list with nothing in it.", true});
        if (f.type == Type::Enum && !f.options.empty()) {
            const long idx = std::lround(f.value);
            if (idx < 0 || idx >= (long)f.options.size())
                out.push_back({where, "Default is not one of the options.", true});
        }
        if (f.scope == Scope::Scene && f.persist != Persist::Session)
            out.push_back({where,
                           "A scene-scoped fact is reset on every scene load, "
                           "so saving it stores something that is about to be "
                           "overwritten.",
                           false});
        if (f.type == Type::Position && f.persist == Persist::Profile)
            out.push_back({where,
                           "Profile storage holds single numbers; a position "
                           "fact needs Save.",
                           false});
    }
    if (facts.size() > (size_t)kMaxFacts)
        out.push_back({"catalog",
                       "More than " + std::to_string(kMaxFacts) +
                           " facts - the generated tables stop there.",
                       true});

    // ---- queries
    for (size_t i = 0; i < queries.size(); ++i) {
        const Query& q = queries[i];
        const std::string where = "query '" + q.name + "'";
        if (q.name.empty()) {
            out.push_back({"query #" + std::to_string(i), "Has no name.", true});
            continue;
        }
        for (size_t j = 0; j < i; ++j)
            if (queries[j].name == q.name)
                out.push_back({where, "Duplicate name.", true});
        validateCondition(q.root, facts, queries, where, out);
    }
    findQueryCycles(queries, out);

    // ---- rules
    for (size_t i = 0; i < rules.size(); ++i) {
        const Rule& r = rules[i];
        const std::string where = "rule '" + r.name + "'";
        if (r.name.empty()) {
            out.push_back({"rule #" + std::to_string(i), "Has no name.", true});
            continue;
        }
        for (size_t j = 0; j < i; ++j)
            if (rules[j].name == r.name)
                out.push_back({where, "Duplicate name.", true});
        validateCondition(r.when, facts, queries, where, out);
        if (r.then.empty())
            out.push_back({where, "Fires nothing.", false});
        for (const RuleAction& a : r.then) {
            if (a.kind == RuleAction::Kind::SendEvent) {
                if (a.target.empty())
                    out.push_back({where, "Sends an event with no name.", true});
                continue;
            }
            const int fi = indexOf(facts, a.target);
            if (fi < 0) {
                out.push_back(
                    {where, "Writes unknown fact '" + a.target + "'.", true});
                continue;
            }
            const Fact& f = facts[(size_t)fi];
            if (f.isComputed())
                out.push_back({where,
                               "'" + a.target +
                                   "' is computed - it is derived from other "
                                   "facts and nothing can write to it.",
                               true});
            if (f.type == Type::Position)
                out.push_back({where,
                               "'" + a.target +
                                   "' is a position; rules write single "
                                   "numbers. Use a flow graph's Set Fact.",
                               true});
            if (a.kind == RuleAction::Kind::ToggleFact && f.type != Type::Bool)
                out.push_back({where,
                               "Toggling '" + a.target +
                                   "' only makes sense for a yes/no fact.",
                               false});
        }
    }
    findRuleCycles(facts, queries, rules, out);
    if (rules.size() > (size_t)kMaxRules)
        out.push_back({"rules",
                       "More than " + std::to_string(kMaxRules) +
                           " rules - the generated engine stops there.",
                       true});

    // ---- scenarios
    for (const Scenario& s : scenarios) {
        const std::string where = "scenario '" + s.name + "'";
        if (s.name.empty()) {
            out.push_back({"scenario", "Has no name.", true});
            continue;
        }
        for (const ScenarioValue& v : s.values) {
            const int fi = indexOf(facts, v.fact);
            if (fi < 0)
                out.push_back(
                    {where, "Unknown fact '" + v.fact + "'.", false});
            else if (facts[(size_t)fi].isComputed())
                out.push_back({where,
                               "'" + v.fact +
                                   "' is computed and cannot be set.",
                               false});
        }
    }

    return out;
}

}  // namespace facts
