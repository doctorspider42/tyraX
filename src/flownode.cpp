// Loads project-defined custom flow-graph nodes from <project>/flow-nodes/
// *.flownode text files into the global customFlowNodes() registry. See
// flowgraph.hpp for the data model and docs/custom-flow-nodes.md for the file
// format. Kept out of flowgraph.hpp (header-only, widely included) because the
// parsing needs <filesystem> / file IO.

#include "flowgraph.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

// True if `list` (a comma/space-separated value) contains the word `w`.
bool listHas(const std::string& list, const char* w) {
    std::string cur;
    auto flush = [&](const std::string& tok) { return tok == w; };
    for (size_t i = 0; i <= list.size(); ++i) {
        const char c = i < list.size() ? list[i] : ',';
        if (c == ',' || std::isspace((unsigned char)c)) {
            if (!cur.empty() && flush(cur)) return true;
            cur.clear();
        } else {
            cur += c;
        }
    }
    return false;
}

// A .flownode file: a `key = value` header, a line that is exactly `---`, then
// the raw C++ body to the end of file. Returns "" on success, else an error.
std::string parseFile(const fs::path& path, CustomFlowNode& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "cannot open";

    const std::string stem = path.stem().string();
    out.sourceFile = path.string();
    out.key = "custom:" + stem;
    out.title = stem;
    out.category = "Custom";
    FlowParamKind strKind = FlowParamKind::None;
    bool numSet[4] = {false, false, false, false};
    std::string inList, outList;
    bool execOut = false;

    std::string line;
    bool inCode = false;
    std::string code;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();  // CRLF
        if (inCode) {
            code += line;
            code += '\n';
            continue;
        }
        if (trim(line) == "---") {
            inCode = true;
            continue;
        }
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;  // blank / comment
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;  // ignore stray lines
        const std::string key = trim(t.substr(0, eq));
        const std::string val = trim(t.substr(eq + 1));
        if (key == "title") {
            if (!val.empty()) out.title = val;
        } else if (key == "category") {
            if (!val.empty()) out.category = val;
        } else if (key == "string") {
            if (val == "text")
                strKind = FlowParamKind::Text;
            else if (val == "object")
                strKind = FlowParamKind::ObjectName;
            else
                strKind = FlowParamKind::None;
        } else if (key == "call") {
            out.callFn = val;
        } else if (key == "in") {
            inList = val;
        } else if (key == "out") {
            outList = val;
        } else if (key == "exec_out") {
            execOut = (val == "true" || val == "1" || val == "yes");
        } else if (key.size() == 4 && key.rfind("num", 0) == 0 && key[3] >= '0' &&
                   key[3] <= '3') {
            const int i = key[3] - '0';
            out.numLabelStore[i] = val.empty() ? "Value" : val;
            numSet[i] = true;
        }
    }

    out.code = code;

    // Contiguous numeric params from num0 (a gap ends the run).
    int numCount = 0;
    while (numCount < 4 && numSet[numCount]) ++numCount;

    // Build the FlowNodeType, pointing char* fields at our own strings.
    FlowNodeType& ty = out.type;
    ty = FlowNodeType{};
    ty.key = out.key.c_str();
    ty.title = out.title.c_str();
    ty.category = out.category.c_str();
    ty.trigger = false;
    ty.strKind = strKind;
    ty.numCount = numCount;
    for (int i = 0; i < 4; ++i)
        ty.numLabels[i] = (i < numCount) ? out.numLabelStore[i].c_str() : "";
    ty.numKind = FlowParamKind::None;
    // Pins. `string = object` and `in = object` both give the object input pin
    // that resolveTarget reads (the node's "target"). Outputs are only
    // meaningful for `call` nodes (a value the C++ function writes); an inline
    // snippet has no way to return them.
    ty.idIn = (strKind == FlowParamKind::ObjectName) || listHas(inList, "object");
    ty.posIn = listHas(inList, "position");
    ty.boolIn = listHas(inList, "bool");
    ty.textIn = listHas(inList, "text");
    ty.idOut = listHas(outList, "object");
    ty.posOut = listHas(outList, "position");
    ty.boolOut = listHas(outList, "bool");
    ty.textOut = listHas(outList, "text");
    ty.execThrough = execOut;  // renders a follow-up exec output ("after >")
    return "";
}

const char* kExampleTemplate =
    "# Custom flow node for TyraX. Copy this file (or write your own) into\n"
    "# <project>/flow-nodes/ and it appears in the Flow Graph add-menu under its\n"
    "# category. The file NAME (without .flownode) is the node's identity - keep\n"
    "# it identical when copying the node to another project, or graphs that use\n"
    "# it will not resolve. Header keys (before the --- line):\n"
    "#   title     display name in the add-menu / node title bar\n"
    "#   category  add-menu submenu (default: Custom)\n"
    "#   string    the string param: none | text | object (default: none)\n"
    "#   num0..3   labels for up to four numeric params (define them in order)\n"
    "#   in        input pins besides params: any of  object position bool text\n"
    "#   out       output pins:               any of  object position bool text\n"
    "#   exec_out  true = a follow-up exec output that fires downstream after\n"
    "#             this node runs (chain custom nodes into more actions)\n"
    "#   call      name of a C++ function in inc/scripts/flow_nodes.hpp to run\n"
    "#             (needed for outputs / real logic). Without it, everything\n"
    "#             after --- is an inline C++ snippet with {placeholders}:\n"
    "#             {obj} target object index, {self} owner index,\n"
    "#             {num0}..{num3} float / {int0}..{int3} int params,\n"
    "#             {str} the string param as a quoted C string.\n"
    "#\n"
    "# This example is the powerful form: it OUTPUTS an object (the one nearest\n"
    "# the player) picked at runtime by flowExampleNearest() in flow_nodes.hpp,\n"
    "# and exec_out lets you chain it into e.g. a built-in Hide Object whose\n"
    "# object input you wire from this node's object output.\n"
    "title = Nearest Object\n"
    "category = Custom\n"
    "out = object\n"
    "exec_out = true\n"
    "call = flowExampleNearest\n"
    "---\n"
    "# (body ignored when `call` is set - the C++ lives in flow_nodes.hpp)\n";

}  // namespace

namespace flownode {

std::string dirForProject(const std::string& projectDir) {
    return (fs::path(projectDir) / "flow-nodes").string();
}

std::string loadForProject(const std::string& projectDir) {
    auto& reg = customFlowNodes();
    reg.clear();

    const fs::path dir = dirForProject(projectDir);
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return "";  // no folder = no custom nodes

    int loaded = 0;
    std::vector<std::string> errors;
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        if (e.path().extension() == ".flownode") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());  // stable add-menu order

    for (const fs::path& path : files) {
        auto node = std::make_unique<CustomFlowNode>();
        const std::string err = parseFile(path, *node);
        if (!err.empty()) {
            errors.push_back(path.filename().string() + ": " + err);
            continue;
        }
        // A duplicate key (same file stem) or a collision with a built-in key
        // would shadow silently - reject the later one.
        bool dup = false;
        for (const auto& c : reg) dup |= (c->key == node->key);
        for (const auto& b : flowNodeTypes()) dup |= (node->key == b.key);
        if (dup) {
            errors.push_back(path.filename().string() + ": duplicate node id");
            continue;
        }
        reg.push_back(std::move(node));
        ++loaded;
    }

    std::ostringstream msg;
    if (loaded > 0 || !errors.empty())
        msg << loaded << " custom flow node" << (loaded == 1 ? "" : "s") << " loaded";
    for (const std::string& e : errors) msg << "; " << e;
    return msg.str();
}

std::string writeExample(const std::string& projectDir) {
    const fs::path dir = dirForProject(projectDir);
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return "error: cannot create " + dir.string();
    const fs::path file = dir / "example.flownode";
    if (fs::exists(file)) return file.string();  // never clobber
    std::ofstream f(file, std::ios::binary);
    if (!f) return "error: cannot write " + file.string();
    f << kExampleTemplate;
    return file.string();
}

}  // namespace flownode
