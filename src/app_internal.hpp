// Private helpers shared by app.cpp and the App:: subsystem TUs split out of
// it (props_ui.cpp, flowgraph_ui.cpp, hud_ui.cpp, cutscene_ui.cpp,
// mateditor_ui.cpp, devkit_ui.cpp - see the source map in the tyra-editor-dev
// skill). NOT an interface: App's own members are declared in app.hpp and the
// split TUs define them there, exactly like assetbrowser.cpp and
// save_assets.cpp already do.
//
// Everything here was a file-scope `static` in app.cpp that more than one of
// those TUs calls, so it needed one home instead of a copy per file. That is
// the ONLY reason for something to be in here - a helper used by a single TU
// belongs in that TU, and this header is compiled seven times, so keep it
// small and keep its includes cheap.
#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <imgui.h>

#include "platform.hpp"

// ---------------------------------------------------------------------------
// Native pickers, one kind per thing the editor imports. The dialogs
// themselves live in platform.cpp (comdlg32/IFileOpenDialog on Windows,
// zenity/kdialog elsewhere) - here we only name the filters.
// ---------------------------------------------------------------------------
enum class PickKind { Folder, Solution, ObjModel, Mtl, Png, Wav, Ttf, Executable, CamTake };

inline std::string pickPath(PickKind kind) {
    // Every filter list ends with All files, so a user whose asset carries an
    // unusual extension is never locked out of importing it.
    static const platform::FileFilter kAll{"All files (*)", {"*"}};
    switch (kind) {
        case PickKind::Solution:
            return platform::pickFile("Open Tyra project",
                                      {{"Tyra project (*.tyra)", {"*.tyra"}}, kAll});
        case PickKind::ObjModel:
            return platform::pickFile(
                "Import 3D model (.glb/.fbx = animated, .tmocap = phone take)",
                {{"3D model or take (*.obj, *.glb, *.fbx, *.tmocap)",
                  {"*.obj", "*.glb", "*.fbx", "*.tmocap"}},
                 {"Wavefront model (*.obj)", {"*.obj"}},
                 {"Animated glTF binary (*.glb)", {"*.glb"}},
                 {"Animated FBX (*.fbx)", {"*.fbx"}},
                 {"Phone mocap take (*.tmocap)", {"*.tmocap"}},
                 kAll});
        case PickKind::Mtl:
            return platform::pickFile("Import material library",
                                      {{"Material library (*.mtl)", {"*.mtl"}}, kAll});
        case PickKind::Png:
            return platform::pickFile("Import PNG image",
                                      {{"PNG image (*.png)", {"*.png"}}, kAll});
        case PickKind::Wav:
            return platform::pickFile("Import WAV (16-bit 22kHz recommended)",
                                      {{"WAV audio (*.wav)", {"*.wav"}}, kAll});
        case PickKind::Ttf:
            return platform::pickFile(
                "Import menu font",
                {{"TrueType font (*.ttf, *.otf)", {"*.ttf", "*.otf"}}, kAll});
        case PickKind::Executable: {
            // An executable has no extension outside Windows, so the "only
            // executables" filter would hide every candidate there.
            const std::string pat = std::string("*") + platform::exeSuffix();
            return platform::pickFile("Select PCSX2 executable",
                                      {{"Executable (" + pat + ")", {pat}}, kAll});
        }
        case PickKind::CamTake:
            return platform::pickFile(
                "Import camera take (phone AR recording)",
                {{"Camera take (*.hfcs, *.csv)", {"*.hfcs", "*.csv"}},
                 {"CamTrackAR composite shot (*.hfcs)", {"*.hfcs"}},
                 {"Camera take CSV (*.csv)", {"*.csv"}},
                 kAll});
        case PickKind::Folder:
            break;
    }
    return platform::pickFolder("Select folder");
}

inline std::string pickFolder() { return pickPath(PickKind::Folder); }
inline std::string pickSolutionFile() { return pickPath(PickKind::Solution); }
inline std::string pickModelFile() { return pickPath(PickKind::ObjModel); }
inline std::string pickMtlFile() { return pickPath(PickKind::Mtl); }
inline std::string pickPngFile() { return pickPath(PickKind::Png); }
inline std::string pickWavFile() { return pickPath(PickKind::Wav); }
inline std::string pickTtfFile() { return pickPath(PickKind::Ttf); }
inline std::string pickExeFile() { return pickPath(PickKind::Executable); }

// Asset filenames flow into shell command lines (e.g. the adpenc wav->adpcm
// loop in runner.cpp), Makefiles and ISO9660 paths - none of which reliably
// tolerate spaces or shell-special characters. Fold anything outside
// [A-Za-z0-9._-] to '_' at import time so the file we copy into res/ and the
// relative path we store always match and stay pipeline-safe.
inline std::string sanitizeAssetName(const std::string& fileName) {
    std::string out = fileName;
    for (char& c : out) {
        const bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!safe) c = '_';
    }
    return out;
}

// Size of a file in bytes, or 0 when it is absent/unreadable (a deleted or
// not-yet-written log reads as 0 - which is exactly the "shrank" signal the
// game-error catcher wants).
inline size_t fileSizeOr0(const std::string& path) {
    std::error_code ec;
    const auto n = std::filesystem::file_size(path, ec);
    return ec ? 0 : (size_t)n;
}

// Reads at most maxBytes from the end of a text file (emulog.txt can grow
// across long sessions). Returns "" when the file is absent or unreadable.
inline std::string readTextFileTail(const std::string& path, size_t maxBytes) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return "";
    const std::streamoff size = in.tellg();
    if (size <= 0) return "";
    const size_t want = (size_t)size < maxBytes ? (size_t)size : maxBytes;
    in.seekg(size - (std::streamoff)want, std::ios::beg);
    std::string data(want, '\0');
    in.read(data.data(), (std::streamsize)want);
    data.resize((size_t)in.gcount());
    // Drop a partial first line when we started mid-file.
    if ((size_t)size > maxBytes) {
        const size_t nl = data.find('\n');
        if (nl != std::string::npos) data.erase(0, nl + 1);
    }
    return data;
}

// Compact help marker: a dimmed "(?)" on the same line as the preceding widget
// that reveals its explanation on hover, instead of unrolling a multi-paragraph
// description inline. Keeps the dense Preferences dialogs from running several
// screens tall - the same idiom the Layers list already uses.
inline void prefHelp(const char* tip) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
}

// Walk speed is STORED as movement per 1/50 s - the generated game's step
// unit - which is a miserable thing to type: every sane value is a fraction
// down in the first few percent of the field. Both places that edit it (the
// project default and a Player object's own) show units per SECOND and
// convert, with the metric equivalent alongside so the number means
// something. `ups` is the project's world scale (docs/world-scale.md).
// Returns true while the value is being changed; `committed` (when given) is
// OR-ed with "the edit just ended" - the drag is followed by the metric label,
// so a caller cannot ask ImGui about the field itself afterwards.
inline bool walkSpeedDrag(const char* label, float& stored, float ups,
                          bool* committed = nullptr) {
    float perSec = stored * 50.0f;
    // 0.01 .. 500 units/s: the top end is what the stored field always allowed
    // (0.05..10 per step = 2.5..500 units/s), the bottom is a crawl - a
    // cutscene shuffle or a metric 1.4 m/s walk both live below the old 2.5
    // floor. Such a span cannot be dragged linearly (a step big enough to
    // cross it makes every adjustment near 5 jump), so the drag is
    // LOGARITHMIC: it moves by a fraction of the current value, which is the
    // same feel at 0.5 and at 50. Ctrl+click still types an exact number.
    const bool changed =
        ImGui::DragFloat(label, &perSec, 0.05f, 0.01f, 500.0f, "%.2f units/s",
                         ImGuiSliderFlags_Logarithmic);
    if (changed) stored = perSec / 50.0f;
    if (committed) *committed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    ImGui::TextDisabled("= %.2f m/s", perSec / (ups > 0.0001f ? ups : 1.0f));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Metres from Preferences > World > Units per meter (now %.3f).\n"
            "For reference: a person walks at 1.4 m/s, sprints at ~6 m/s;\n"
            "most first-person games run the player at 4-6 m/s.\n"
            "Drag adjusts proportionally (0.01 .. 500 units/s); Ctrl+click\n"
            "types an exact value. Stored in the project as %g (movement\n"
            "per 1/50 s).",
            ups, stored);
    return changed;
}
