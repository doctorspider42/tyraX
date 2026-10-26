#include "extension.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "json.hpp"

namespace fs = std::filesystem;

namespace extension {
namespace {

std::string readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool writeNew(const fs::path& p, const std::string& text) {
    std::error_code ec;
    if (fs::exists(p, ec)) return true;  // never clobber
    std::ofstream f(p, std::ios::binary);
    if (!f) return false;
    f << text;
    return (bool)f;
}

// TYRA_OBJECT_SCRIPT(Name) occurrences, the attach list's textual scan.
void grabObjectScripts(const std::string& src, std::vector<std::string>& out) {
    static const std::string kMacro = "TYRA_OBJECT_SCRIPT(";
    for (size_t pos = src.find(kMacro); pos != std::string::npos;
         pos = src.find(kMacro, pos + kMacro.size())) {
        if (pos > 0 && (std::isalnum((unsigned char)src[pos - 1]) || src[pos - 1] == '_')) continue;
        const size_t end = src.find(')', pos + kMacro.size());
        if (end == std::string::npos) break;
        std::string n = src.substr(pos + kMacro.size(), end - pos - kMacro.size());
        n.erase(0, n.find_first_not_of(" \t\r\n"));
        n.erase(n.find_last_not_of(" \t\r\n") + 1);
        if (!n.empty() && n != "ClassName" &&
            std::find(out.begin(), out.end(), n) == out.end())
            out.push_back(n);
    }
}

}  // namespace

std::string rootDir(const std::string& projectDir) {
    return (fs::path(projectDir) / "src" / "extensions").string();
}

std::vector<std::string> scriptDirs(const std::string& projectDir) {
    std::vector<std::string> dirs = {(fs::path(projectDir) / "src" / "scripts").string()};
    std::error_code ec;
    const fs::path root = rootDir(projectDir);
    if (!fs::is_directory(root, ec)) return dirs;
    std::vector<std::string> ext;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_directory(ec)) ext.push_back(it->path().string());
    std::sort(ext.begin(), ext.end());
    dirs.insert(dirs.end(), ext.begin(), ext.end());
    return dirs;
}

std::vector<Info> list(const std::string& projectDir) {
    std::vector<Info> out;
    std::error_code ec;
    const fs::path root = rootDir(projectDir);
    if (!fs::is_directory(root, ec)) return out;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec)) continue;
        Info info;
        info.folder = it->path().filename().string();
        info.name = info.folder;
        const fs::path manifest = it->path() / "extension.json";
        if (fs::exists(manifest, ec)) {
            json::Value root;
            if (json::parse(readAll(manifest), root)) {
                if (const auto* v = root.find("name")) info.name = v->stringOr(info.folder);
                if (const auto* v = root.find("version")) info.version = v->stringOr("");
                if (const auto* v = root.find("description")) info.description = v->stringOr("");
                if (const auto* v = root.find("author")) info.author = v->stringOr("");
            } else {
                info.problem = "extension.json is not valid JSON";
            }
        } else {
            info.problem = "no extension.json";
        }
        for (fs::recursive_directory_iterator f(it->path(), ec), fe; !ec && f != fe; f.increment(ec)) {
            if (!f->is_regular_file(ec)) continue;
            const std::string ext = f->path().extension().string();
            if (ext == ".cpp") {
                ++info.sources;
                grabObjectScripts(readAll(f->path()), info.objectScripts);
            } else if (ext == ".flownode") {
                ++info.flowNodes;
            }
        }
        out.push_back(std::move(info));
    }
    std::sort(out.begin(), out.end(), [](const Info& a, const Info& b) { return a.folder < b.folder; });
    return out;
}

std::string install(const std::string& projectDir, const std::string& sourceDir,
                    std::string* installedFolder) {
    std::error_code ec;
    const fs::path src = fs::weakly_canonical(fs::path(sourceDir), ec);
    if (ec || !fs::is_directory(src, ec)) return "not a folder: " + sourceDir;
    if (!fs::exists(src / "extension.json", ec))
        return "no extension.json in " + src.string() + " - not an extension folder";
    const fs::path project = fs::weakly_canonical(fs::path(projectDir), ec);
    const std::string rel = fs::relative(src, project, ec).generic_string();
    if (!ec && !rel.empty() && rel.rfind("..", 0) != 0)
        return "the folder is inside this project already";
    const std::string folder = src.filename().string();
    const fs::path dst = fs::path(rootDir(projectDir)) / folder;
    if (fs::exists(dst, ec)) return "an extension named '" + folder + "' is installed already";
    fs::create_directories(dst.parent_path(), ec);
    fs::copy(src, dst, fs::copy_options::recursive, ec);
    if (ec) return "copy failed: " + ec.message();
    if (installedFolder) *installedFolder = folder;
    return "";
}

std::string folderFor(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (std::isalnum((unsigned char)c))
            out += (char)std::tolower((unsigned char)c);
        else if (!out.empty() && out.back() != '-')
            out += '-';
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}

std::string scaffold(const std::string& projectDir, const std::string& name) {
    const std::string folder = folderFor(name);
    if (folder.empty()) return "the name needs at least one letter or digit";
    std::error_code ec;
    const fs::path dir = fs::path(rootDir(projectDir)) / folder;
    if (fs::exists(dir, ec)) return "an extension named '" + folder + "' exists already";
    fs::create_directories(dir / "flow-nodes", ec);
    if (ec) return "cannot create " + dir.string();
    // The class name: the folder in PascalCase.
    std::string cls;
    bool up = true;
    for (char c : folder) {
        if (c == '-') {
            up = true;
            continue;
        }
        cls += up ? (char)std::toupper((unsigned char)c) : c;
        up = false;
    }
    if (!cls.empty() && std::isdigit((unsigned char)cls[0])) cls = "_" + cls;
    const std::string manifest =
        "{\n"
        "  \"name\": \"" + name + "\",\n"
        "  \"version\": \"0.1.0\",\n"
        "  \"description\": \"What this extension adds, in one line.\",\n"
        "  \"author\": \"\"\n"
        "}\n";
    const std::string source =
        "// " + folder + ".cpp - part of the '" + name + "' extension (docs/script-extensions.md).\n"
        "// Everything in this folder is compiled with the game. Copy the folder to\n"
        "// another project's src/extensions/ (or Project > Scripts > Extensions >\n"
        "// Install) to reuse it.\n"
        "#include \"scripts/script.hpp\"\n"
        "\n"
        "// TYRAX_NAMESPACE_BEGIN/_END = this project's namespace, whichever\n"
        "// project the folder lands in.\n"
        "TYRAX_NAMESPACE_BEGIN\n"
        "\n"
        "class " + cls + " : public ObjectScript {\n"
        " public:\n"
        "  [[tyra::prop(min = 0, max = 360, tooltip = \"Degrees per second\")]]\n"
        "  float speed = 45.0F;\n"
        "\n"
        "  void onUpdate(ScriptContext& ctx) override {\n"
        "    (void)ctx;\n"
        "    object.rotateBy(tyrax::Vec3(0.0F, speed * tyrax::dt(), 0.0F));\n"
        "  }\n"
        "};\n"
        "\n"
        "TYRA_OBJECT_SCRIPT(" + cls + ");\n"
        "\n"
        "TYRAX_NAMESPACE_END\n";
    const std::string node =
        "# A custom flow node shipped with the '" + name + "' extension.\n"
        "title = Nudge Up\n"
        "category = " + name + "\n"
        "desc = Moves the target object up by Amount units.\n"
        "string = object\n"
        "num0 = Amount\n"
        "tip0 = How far up, in units.\n"
        "---\n"
        "tyrax::object(ctx, {obj}).moveBy(tyrax::Vec3(0.0F, {num0}, 0.0F));\n";
    if (!writeNew(dir / "extension.json", manifest) ||
        !writeNew(dir / (folder + ".cpp"), source) ||
        !writeNew(dir / "flow-nodes" / (folder + "-nudge-up.flownode"), node))
        return "cannot write into " + dir.string();
    return "";
}

std::string remove(const std::string& projectDir, const std::string& folder) {
    if (folder.empty() || folder.find("..") != std::string::npos ||
        folder.find('/') != std::string::npos || folder.find('\\') != std::string::npos)
        return "not an extension folder name";
    std::error_code ec;
    const fs::path dir = fs::path(rootDir(projectDir)) / folder;
    if (!fs::is_directory(dir, ec)) return "no such extension";
    fs::remove_all(dir, ec);
    return ec ? "delete failed: " + ec.message() : "";
}

}  // namespace extension
