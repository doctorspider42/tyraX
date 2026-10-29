#include "scriptprops.hpp"

#include "extension.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace scriptprops {
namespace {

bool identChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

// Comments blanked to spaces (positions kept); string and char literals kept
// - a tooltip lives in one.
std::string stripComments(const std::string& in) {
    std::string s = in;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '"' || c == '\'') {
            for (++i; i < s.size() && s[i] != c && s[i] != '\n'; ++i)
                if (s[i] == '\\') ++i;
        } else if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            while (i < s.size() && s[i] != '\n') s[i++] = ' ';
        } else if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
            while (i < s.size() && !(s[i] == '*' && i + 1 < s.size() && s[i + 1] == '/'))
                s[i] = s[i] == '\n' ? '\n' : ' ', ++i;
            if (i + 1 < s.size()) s[i] = s[i + 1] = ' ';
            ++i;
        }
    }
    return s;
}

// Index just past the literal starting at s[i] (a quote).
size_t skipLiteral(const std::string& s, size_t i) {
    const char q = s[i];
    for (++i; i < s.size() && s[i] != q; ++i)
        if (s[i] == '\\') ++i;
    return i < s.size() ? i + 1 : i;
}

// The index of the bracket closing the one at s[open], or npos.
size_t matchBracket(const std::string& s, size_t open) {
    const char o = s[open];
    const char c = o == '{' ? '}' : o == '(' ? ')' : o == '[' ? ']' : '>';
    int depth = 0;
    for (size_t i = open; i < s.size();) {
        if (s[i] == '"' || s[i] == '\'') {
            i = skipLiteral(s, i);
            continue;
        }
        if (s[i] == o) ++depth;
        else if (s[i] == c && --depth == 0) return i;
        ++i;
    }
    return std::string::npos;
}

// Splits on commas that are not inside brackets or literals.
std::vector<std::string> splitTop(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '"' || c == '\'') {
            const size_t e = skipLiteral(s, i);
            cur += s.substr(i, e - i);
            i = e - 1;
            continue;
        }
        if (c == '(' || c == '{' || c == '[') ++depth;
        if (c == ')' || c == '}' || c == ']') --depth;
        if (c == ',' && depth == 0) {
            out.push_back(trim(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!trim(cur).empty()) out.push_back(trim(cur));
    return out;
}

std::string unquote(const std::string& v) {
    const std::string t = trim(v);
    if (t.size() < 2 || t.front() != '"' || t.back() != '"') return t;
    std::string out;
    for (size_t i = 1; i + 1 < t.size(); ++i) {
        if (t[i] == '\\' && i + 2 < t.size()) {
            const char n = t[++i];
            out += n == 'n' ? '\n' : n == 't' ? '\t' : n;
        } else {
            out += t[i];
        }
    }
    return out;
}

bool parseNumber(const std::string& text, float* out) {
    std::string t = trim(text);
    while (!t.empty() && (t.back() == 'f' || t.back() == 'F' || t.back() == 'u' ||
                          t.back() == 'U' || t.back() == 'l' || t.back() == 'L'))
        t.pop_back();
    if (t.empty()) return false;
    char* end = nullptr;
    const float v = std::strtof(t.c_str(), &end);
    if (!end || *end != '\0') return false;
    *out = v;
    return true;
}

Kind kindOfType(std::string type, bool color, bool hasOptions) {
    // Normalise "tyrax::SoundId", "::Ns::tyrax::SoundId", "const  char *".
    std::string t;
    for (char c : type)
        if (!std::isspace((unsigned char)c)) t += c;
    const size_t ns = t.rfind("::");
    const std::string base = ns == std::string::npos ? t : t.substr(ns + 2);
    if (t == "bool") return Kind::Bool;
    if (t == "int" || t == "short" || t == "long" || t == "unsignedint" || t == "unsigned")
        return hasOptions ? Kind::Enum : Kind::Int;
    if (t == "float" || t == "double") return Kind::Float;
    if (t == "constchar*" || t == "charconst*") return Kind::Text;
    if (base == "Vec3") return color ? Kind::Color : Kind::Vec3;
    if (base == "ObjectRef") return Kind::Object;
    if (base == "SoundId") return Kind::Sound;
    if (base == "MusicId") return Kind::Music;
    if (base == "EventId") return Kind::Event;
    if (base == "SceneId") return Kind::Scene;
    if (base == "SaveValueId") return Kind::SaveValue;
    if (base == "PrefabId") return Kind::Prefab;
    if (base == "ActionId") return Kind::Action;
    if (base == "HudTextId") return Kind::HudText;
    if (base == "HudBarId") return Kind::HudBar;
    if (base == "SequenceId") return Kind::Sequence;
    if (base == "MenuId") return Kind::Menu;
    return Kind::Unknown;
}

// The default value a declaration's initializer spells, as value text, or ""
// when it is not a plain literal the editor can read.
std::string defaultOf(Kind k, const std::string& initRaw) {
    std::string init = trim(initRaw);
    if (!init.empty() && init.front() == '{' && init.back() == '}')
        init = trim(init.substr(1, init.size() - 2));
    switch (k) {
        case Kind::Bool:
            if (init.empty() || init == "false") return "false";
            if (init == "true") return "true";
            return "";
        case Kind::Int:
        case Kind::Enum: {
            if (init.empty()) return "0";
            float v = 0.0f;
            if (!parseNumber(init, &v)) return "";
            return std::to_string((long)v);
        }
        case Kind::Float: {
            if (init.empty()) return "0";
            float v = 0.0f;
            return parseNumber(init, &v) ? formatFloat(v) : "";
        }
        case Kind::Vec3:
        case Kind::Color: {
            if (init.empty()) return "0 0 0";
            // tyrax::Vec3(1, 2, 3) or {1, 2, 3}
            const size_t paren = init.find('(');
            if (paren != std::string::npos && init.back() == ')')
                init = init.substr(paren + 1, init.size() - paren - 2);
            const std::vector<std::string> parts = splitTop(init);
            if (parts.size() != 3) return "";
            float v[3];
            for (int i = 0; i < 3; ++i)
                if (!parseNumber(parts[(size_t)i], &v[i])) return "";
            return formatFloat(v[0]) + " " + formatFloat(v[1]) + " " + formatFloat(v[2]);
        }
        case Kind::Text:
            if (init.empty() || init == "nullptr") return "";
            if (init.front() == '"') return unquote(init);
            return "";
        default:
            return "";  // references: a code default is an identifier, not a name
    }
}

void parseAttribute(const std::string& args, Field& f, bool& color) {
    for (const std::string& a : splitTop(args)) {
        const size_t eq = a.find('=');
        const std::string key = trim(eq == std::string::npos ? a : a.substr(0, eq));
        const std::string val = eq == std::string::npos ? "" : trim(a.substr(eq + 1));
        if (key == "color") {
            color = true;
        } else if (key == "min") {
            f.hasMin = parseNumber(val, &f.min);
        } else if (key == "max") {
            f.hasMax = parseNumber(val, &f.max);
        } else if (key == "tooltip") {
            f.tooltip = unquote(val);
        } else if (key == "header") {
            f.header = unquote(val);
        } else if (key == "options") {
            const std::string o = unquote(val);
            std::string cur;
            for (char c : o) {
                if (c == '|') {
                    f.options.push_back(trim(cur));
                    cur.clear();
                } else {
                    cur += c;
                }
            }
            f.options.push_back(trim(cur));
        }
    }
}

// The fields of one class body (the text between its braces).
std::vector<Field> scanBody(const std::string& body) {
    std::vector<Field> fields;
    static const std::string kAttr = "tyra::prop";
    size_t i = 0;
    int depth = 0;
    while (i < body.size()) {
        const char c = body[i];
        if (c == '"' || c == '\'') {
            i = skipLiteral(body, i);
            continue;
        }
        if (c == '{') ++depth;
        if (c == '}') --depth;
        if (depth == 0 && c == '[' && i + 1 < body.size() && body[i + 1] == '[') {
            const size_t close = body.find("]]", i + 2);
            if (close == std::string::npos) break;
            const std::string attr = body.substr(i + 2, close - i - 2);
            const size_t at = attr.find(kAttr);
            if (at == std::string::npos ||
                (at + kAttr.size() < attr.size() && identChar(attr[at + kAttr.size()]))) {
                i = close + 2;
                continue;
            }
            Field f;
            bool color = false;
            const size_t open = attr.find('(', at);
            if (open != std::string::npos) {
                const size_t shut = matchBracket(attr, open);
                if (shut != std::string::npos)
                    parseAttribute(attr.substr(open + 1, shut - open - 1), f, color);
            }
            // The declaration: up to the ';' at this level.
            size_t j = close + 2, end = std::string::npos;
            for (int d = 0; j < body.size(); ++j) {
                if (body[j] == '"' || body[j] == '\'') {
                    j = skipLiteral(body, j) - 1;
                    continue;
                }
                if (body[j] == '{' || body[j] == '(') ++d;
                if (body[j] == '}' || body[j] == ')') --d;
                if (body[j] == ';' && d == 0) {
                    end = j;
                    break;
                }
            }
            if (end == std::string::npos) break;
            std::string decl = trim(body.substr(close + 2, end - close - 2));
            // Further attributes on the same declaration.
            while (decl.rfind("[[", 0) == 0) {
                const size_t e = decl.find("]]");
                if (e == std::string::npos) break;
                decl = trim(decl.substr(e + 2));
            }
            // Split off the initializer: the first top-level '=' or '{'.
            std::string head = decl, init;
            for (size_t k = 0; k < decl.size(); ++k) {
                if (decl[k] == '"') break;
                if (decl[k] == '=' || decl[k] == '{') {
                    head = trim(decl.substr(0, k));
                    init = trim(decl[k] == '=' ? decl.substr(k + 1) : decl.substr(k));
                    break;
                }
            }
            size_t ne = head.size();
            while (ne > 0 && !identChar(head[ne - 1])) --ne;
            size_t nb = ne;
            while (nb > 0 && identChar(head[nb - 1])) --nb;
            f.name = head.substr(nb, ne - nb);
            f.type = trim(head.substr(0, nb));
            if (f.type.rfind("static ", 0) == 0 || f.type.rfind("constexpr ", 0) == 0) {
                f.problem = "static members belong to the class, not to an attachment";
            } else if (f.name.empty() || f.type.empty()) {
                f.problem = "not a member declaration the editor can read";
            } else {
                f.kind = kindOfType(f.type, color, !f.options.empty());
                if (f.kind == Kind::Unknown)
                    f.problem = "type '" + f.type + "' cannot be set in the editor";
                else
                    f.defaultValue = defaultOf(f.kind, init);
            }
            fields.push_back(f);
            i = end + 1;
            continue;
        }
        ++i;
    }
    return fields;
}

// The first sentence of the comment block ending right above source[at] -
// `/** ... */` or a run of `//` lines (ruler lines of dashes are skipped). A
// leading "B2. Name - " label is dropped: the class name is shown beside it.
std::string commentAbove(const std::string& source, size_t at, const std::string& name) {
    // The lines above the one holding `at`, nearest first.
    std::vector<std::string> above;
    {
        std::istringstream in(source.substr(0, std::min(at, source.size())));
        std::string ln;
        while (std::getline(in, ln)) above.push_back(ln);
        if (!above.empty()) above.pop_back();  // the class line itself
        std::reverse(above.begin(), above.end());
    }
    std::vector<std::string> lines;  // bottom-up
    for (const std::string& raw : above) {
        const std::string ln = trim(raw);
        const bool comment = ln.rfind("//", 0) == 0 || ln.rfind("/*", 0) == 0 ||
                             ln.rfind("*", 0) == 0 ||
                             (ln.size() >= 2 && ln.compare(ln.size() - 2, 2, "*/") == 0);
        if (!comment) break;
        lines.push_back(ln);
    }
    std::string text;
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        std::string ln = *it;
        size_t k = 0;
        while (k < ln.size() && (ln[k] == '/' || ln[k] == '*')) ++k;
        ln = ln.substr(k);
        while (!ln.empty() && (ln.back() == '/' || ln.back() == '*')) ln.pop_back();
        ln = trim(ln);
        if (ln.find_first_not_of("-=_ ") == std::string::npos) continue;  // ruler
        if (!text.empty()) text += ' ';
        text += ln;
    }
    // "B2. " - a section label in front.
    {
        size_t k = 0;
        if (k < text.size() && std::isupper((unsigned char)text[k])) ++k;
        const size_t digits = k;
        while (k < text.size() && std::isdigit((unsigned char)text[k])) ++k;
        if (k > digits && k + 1 < text.size() && text[k] == '.' && text[k + 1] == ' ') text = trim(text.substr(k + 1));
    }
    // "Name - " / "Name + Other - ": the class name is shown beside it.
    const size_t dash = text.find(" - ");
    if (dash != std::string::npos && dash < 48 && text.substr(0, dash).find(name) != std::string::npos)
        text = text.substr(dash + 3);
    size_t stop = std::string::npos;
    for (size_t i = 0; i + 1 < text.size(); ++i)
        if ((text[i] == '.' || text[i] == '!' || text[i] == '?') && text[i + 1] == ' ') {
            stop = i + 1;
            break;
        }
    if (stop != std::string::npos) text = text.substr(0, stop);
    // Capitalise a plain first word ("press USE"), never an identifier
    // ("tyrax::Timer", "onStart").
    {
        size_t k = 0;
        while (k < text.size() && std::islower((unsigned char)text[k])) ++k;
        if (k > 0 && (k == text.size() || text[k] == ' ' || text[k] == ',' || text[k] == ':'))
            if (!(k + 1 < text.size() && text[k] == ':' && text[k + 1] == ':'))
                text[0] = (char)std::toupper((unsigned char)text[0]);
    }
    if (text.size() > 140) text = text.substr(0, 137) + "...";
    return text;
}

}  // namespace

std::string formatFloat(float v) {
    char b[32];
    for (int prec = 6; prec <= 9; ++prec) {
        std::snprintf(b, sizeof(b), "%.*g", prec, (double)v);
        if (std::strtof(b, nullptr) == v) break;
    }
    return b;
}

bool parseVec3(const std::string& text, float out[3]) {
    std::istringstream ss(text);
    return (bool)(ss >> out[0] >> out[1] >> out[2]);
}

std::vector<ClassInfo> scanSource(const std::string& source, const std::string& file) {
    std::vector<ClassInfo> out;
    const std::string s = stripComments(source);
    static const std::string kMacro = "TYRA_OBJECT_SCRIPT(";
    for (size_t pos = s.find(kMacro); pos != std::string::npos;
         pos = s.find(kMacro, pos + kMacro.size())) {
        if (pos > 0 && identChar(s[pos - 1])) continue;  // the #define, or a longer name
        const size_t shut = s.find(')', pos + kMacro.size());
        if (shut == std::string::npos) break;
        const std::string name = trim(s.substr(pos + kMacro.size(), shut - pos - kMacro.size()));
        if (name.empty() || name == "ClassName") continue;
        ClassInfo ci;
        ci.name = name;
        ci.file = file;
        // `class Name` / `struct Name` followed (bases allowed) by its body.
        for (const char* kw : {"class", "struct"}) {
            const std::string key = std::string(kw);
            for (size_t c = s.find(key); c != std::string::npos; c = s.find(key, c + 1)) {
                if (c > 0 && identChar(s[c - 1])) continue;
                size_t k = c + key.size();
                if (k >= s.size() || identChar(s[k])) continue;
                while (k < s.size() && std::isspace((unsigned char)s[k])) ++k;
                if (s.compare(k, name.size(), name) != 0) continue;
                const size_t after = k + name.size();
                if (after < s.size() && identChar(s[after])) continue;
                // A forward declaration ends in ';' before any '{'.
                const size_t brace = s.find('{', after);
                const size_t semi = s.find(';', after);
                if (brace == std::string::npos || (semi != std::string::npos && semi < brace))
                    continue;
                const size_t endBody = matchBracket(s, brace);
                if (endBody == std::string::npos) continue;
                ci.fields = scanBody(s.substr(brace + 1, endBody - brace - 1));
                ci.line = 1 + (int)std::count(s.begin(), s.begin() + (long)c, '\n');
                ci.description = commentAbove(source, c, name);
                break;
            }
            if (!ci.fields.empty()) break;
        }
        bool seen = false;
        for (const ClassInfo& e : out) seen |= (e.name == ci.name);
        if (!seen) out.push_back(ci);
    }
    // TYRA_SCRIPT_METHOD(Class, method) -> that class's method list.
    static const std::string kMethod = "TYRA_SCRIPT_METHOD(";
    for (size_t pos = s.find(kMethod); pos != std::string::npos;
         pos = s.find(kMethod, pos + kMethod.size())) {
        if (pos > 0 && identChar(s[pos - 1])) continue;
        const size_t shut = s.find(')', pos + kMethod.size());
        if (shut == std::string::npos) break;
        const std::vector<std::string> args =
            splitTop(s.substr(pos + kMethod.size(), shut - pos - kMethod.size()));
        if (args.size() != 2 || args[0] == "ClassName") continue;
        for (ClassInfo& c : out)
            if (c.name == args[0] &&
                std::find(c.methods.begin(), c.methods.end(), args[1]) == c.methods.end())
                c.methods.push_back(args[1]);
    }
    return out;
}

std::vector<ClassInfo> scanProject(const std::string& projectDir) {
    namespace fs = std::filesystem;
    std::vector<ClassInfo> out;
    std::error_code ec;
    if (projectDir.empty()) return out;
    std::vector<fs::path> files;
    // src/scripts and every installed extension (src/extensions/<name>).
    for (const std::string& d : extension::scriptDirs(projectDir)) {
        ec.clear();
        if (!fs::is_directory(d, ec)) continue;
        for (fs::recursive_directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const std::string ext = it->path().extension().string();
            if (ext == ".cpp" || ext == ".hpp" || ext == ".h") files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());  // stable order across file systems
    for (const fs::path& f : files) {
        std::ifstream in(f, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string rel = fs::relative(f, fs::path(projectDir), ec).generic_string();
        for (ClassInfo& ci : scanSource(ss.str(), rel)) {
            bool seen = false;
            for (const ClassInfo& e : out) seen |= (e.name == ci.name);
            if (!seen) out.push_back(std::move(ci));
        }
    }
    return out;
}

const ClassInfo* findClass(const std::vector<ClassInfo>& classes, const std::string& name) {
    for (const ClassInfo& c : classes)
        if (c.name == name) return &c;
    return nullptr;
}

const char* kindLabel(Kind k) {
    switch (k) {
        case Kind::Bool: return "bool";
        case Kind::Int: return "int";
        case Kind::Enum: return "choice";
        case Kind::Float: return "float";
        case Kind::Vec3: return "vector";
        case Kind::Color: return "colour";
        case Kind::Text: return "text";
        case Kind::Object: return "object";
        case Kind::Sound: return "sound";
        case Kind::Music: return "music";
        case Kind::Event: return "event";
        case Kind::Scene: return "scene";
        case Kind::SaveValue: return "save value";
        case Kind::Prefab: return "prefab";
        case Kind::Action: return "input action";
        case Kind::HudText: return "HUD text";
        case Kind::HudBar: return "HUD bar";
        case Kind::Sequence: return "sequence";
        case Kind::Menu: return "menu";
        default: return "?";
    }
}

}  // namespace scriptprops
