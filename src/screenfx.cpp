// Loads project-defined custom screen effects from <project>/screen-effects/
// *.screenfx text files into the global customScreenEffects() registry. See
// screenfx.hpp for the data model and docs/custom-screen-effects.md for the
// file format. Mirrors flownode.cpp (custom flow nodes); kept out of the
// header because parsing needs <filesystem> / file IO.

#include "screenfx.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
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

// A .screenfx file: a `key = value` header, a line that is exactly `---`, then
// the raw C++ effect body to the end of file. Returns "" on success, else an
// error string.
std::string parseFile(const fs::path& path, CustomScreenFx& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "cannot open";

    const std::string stem = path.stem().string();
    out.sourceFile = path.string();
    out.key = "custom:" + stem;
    out.title = stem;
    bool paramSet[4] = {false, false, false, false};

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
            // reserved for future add-menu grouping; ignored for now
        } else if (key.size() == 6 && key.rfind("param", 0) == 0 &&
                   key[5] >= '0' && key[5] <= '3') {
            const int i = key[5] - '0';
            // "Label" or "Label, default, min, max" (default 0, range 0..1).
            std::vector<std::string> parts;
            std::string cur;
            for (size_t j = 0; j <= val.size(); ++j) {
                if (j == val.size() || val[j] == ',') {
                    parts.push_back(trim(cur));
                    cur.clear();
                } else {
                    cur += val[j];
                }
            }
            out.paramLabel[i] = parts[0].empty() ? "Value" : parts[0];
            out.paramDefault[i] = parts.size() > 1 && !parts[1].empty()
                                      ? (float)std::atof(parts[1].c_str())
                                      : 0.0f;
            out.paramMin[i] = parts.size() > 2 && !parts[2].empty()
                                  ? (float)std::atof(parts[2].c_str())
                                  : 0.0f;
            out.paramMax[i] = parts.size() > 3 && !parts[3].empty()
                                  ? (float)std::atof(parts[3].c_str())
                                  : 1.0f;
            paramSet[i] = true;
        }
    }

    out.code = code;

    // Contiguous numeric params from param0 (a gap ends the run).
    out.paramCount = 0;
    while (out.paramCount < 4 && paramSet[out.paramCount]) ++out.paramCount;
    return "";
}

const char* kExampleTemplate =
    "# Custom screen effect for TyraX. Copy this file (or write your own)\n"
    "# into <project>/screen-effects/ and it appears in Tools > UI Editor as a\n"
    "# reorderable entry in the screen stack - place it where it should\n"
    "# composite (e.g. under the HUD, or over everything). The file NAME\n"
    "# (without .screenfx) is the effect's identity - keep it identical when\n"
    "# copying the effect to another project, or its placement will not\n"
    "# resolve. See docs/custom-screen-effects.md for the full reference.\n"
    "#\n"
    "# Header keys (before the --- line):\n"
    "#   title      display name in the UI Editor stack (default: file name)\n"
    "#   param0..3  a slider: `Label` or `Label, default, min, max`\n"
    "#              (default 0, range 0..1). Define them in order from param0.\n"
    "#\n"
    "# Everything after --- is the raw C++ effect body, emitted into\n"
    "# src/scripts/screen_fx.gen.cpp. In scope: `fx` (Tyra::RendererCorePostFx\n"
    "# - blit()/flatQuad()/currentFbVram()/currentFbBufW()/screenW()/screenH()/\n"
    "# noiseTexVram()/lowBuf0()/nextRand()), `q` (the qword_t* GS packet cursor\n"
    "# - append primitives and advance it), and `param` (const float* - the\n"
    "# runtime param values). {p0}..{p3} expand to param[0]..param[3]. Return q.\n"
    "#\n"
    "# There are no pixel shaders on the PS2: an effect is GS framebuffer blits,\n"
    "# exactly like the built-in bloom / film grain. Keep it to ~a dozen sprites\n"
    "# (the packet holds ~200 qwords after setup).\n"
    "#\n"
    "# This example washes the whole frame toward a color (a fade / tint), the\n"
    "# same GS mix sprite the color grader uses: (Cs - Cd)*As>>7 + Cd.\n"
    "title = Color Wash\n"
    "param0 = Amount, 0.35, 0, 1\n"
    "param1 = Red, 0.1, 0, 1\n"
    "param2 = Green, 0.0, 0, 1\n"
    "param3 = Blue, 0.2, 0, 1\n"
    "---\n"
    "u8 a = (u8)({p0} * 128.0F);\n"
    "if (a == 0) return q;\n"
    "u8 r = (u8)({p1} * 255.0F), g = (u8)({p2} * 255.0F), b = (u8)({p3} * 255.0F);\n"
    "// Plain alpha blend toward (r,g,b). The 0xFF000000 FBMSK keeps the\n"
    "// framebuffer alpha byte (the blits read it back for the decal path).\n"
    "q = fx.flatQuad(q, fx.currentFbVram(), fx.currentFbBufW(), 0xFF000000u,\n"
    "                r, g, b, a, GS_SET_ALPHA(0, 1, 0, 1, 0));\n";

}  // namespace

namespace screenfx {

std::string dirForProject(const std::string& projectDir) {
    return (fs::path(projectDir) / "screen-effects").string();
}

std::string loadForProject(const std::string& projectDir) {
    auto& reg = customScreenEffects();
    reg.clear();

    const fs::path dir = dirForProject(projectDir);
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return "";  // no folder = no custom effects

    int loaded = 0;
    std::vector<std::string> errors;
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        if (e.path().extension() == ".screenfx") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());  // stable stack / list order

    for (const fs::path& path : files) {
        auto fxEntry = std::make_unique<CustomScreenFx>();
        const std::string err = parseFile(path, *fxEntry);
        if (!err.empty()) {
            errors.push_back(path.filename().string() + ": " + err);
            continue;
        }
        // A duplicate key (same file stem) would shadow silently - reject the
        // later one.
        bool dup = false;
        for (const auto& c : reg) dup |= (c->key == fxEntry->key);
        if (dup) {
            errors.push_back(path.filename().string() + ": duplicate effect id");
            continue;
        }
        reg.push_back(std::move(fxEntry));
        ++loaded;
    }

    std::ostringstream msg;
    if (loaded > 0 || !errors.empty())
        msg << loaded << " custom screen effect" << (loaded == 1 ? "" : "s")
            << " loaded";
    for (const std::string& e : errors) msg << "; " << e;
    return msg.str();
}

std::string writeExample(const std::string& projectDir) {
    const fs::path dir = dirForProject(projectDir);
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return "error: cannot create " + dir.string();
    const fs::path file = dir / "example.screenfx";
    if (fs::exists(file)) return file.string();  // never clobber
    std::ofstream f(file, std::ios::binary);
    if (!f) return "error: cannot write " + file.string();
    f << kExampleTemplate;
    return file.string();
}

}  // namespace screenfx
