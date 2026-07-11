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
    ty.idIn = (strKind == FlowParamKind::ObjectName);  // object pin + link resolve
    ty.idOut = false;
    return "";
}

const char* kExampleTemplate =
    "# Custom flow node for tyra-editor. Copy this file (or write your own) into\n"
    "# <project>/flow-nodes/ and it appears in the Flow Graph add-menu under its\n"
    "# category. The file NAME (without .flownode) is the node's identity - keep\n"
    "# it identical when copying the node to another project, or graphs that use\n"
    "# it will not resolve. Header keys (before the --- line):\n"
    "#   title     display name in the add-menu / node title bar\n"
    "#   category  add-menu submenu (default: Custom)\n"
    "#   string    the string param: none | text | object (default: none)\n"
    "#   num0..3   labels for up to four numeric params (define them in order)\n"
    "# Everything after the --- line is C++ emitted verbatim into\n"
    "# src/scripts/flow_graph.gen.cpp when an exec link fires this (action) node.\n"
    "# `ctx` is the ScriptContext. Placeholders substituted at build:\n"
    "#   {obj}   resolved target object index (the Object pin/dropdown, or self)\n"
    "#   {self}  index of the object that owns this graph\n"
    "#   {num0}..{num3}  numeric params as float literals\n"
    "#   {int0}..{int3}  numeric params as integer literals\n"
    "#   {str}   string param as a quoted C string (use when string = text)\n"
    "title = Nudge Up\n"
    "category = Custom\n"
    "string = object\n"
    "num0 = Amount\n"
    "---\n"
    "ctx.objects[{obj}].data.position[1] += {num0};\n"
    "ctx.objects[{obj}].dirty = true;\n";

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
