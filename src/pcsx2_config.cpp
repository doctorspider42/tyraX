#include "pcsx2_config.hpp"

#include <fstream>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

namespace fs = std::filesystem;

namespace pcsx2 {

static std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return "";
    return s.substr(b, s.find_last_not_of(" \t") - b + 1);
}

fs::path findIni(const fs::path& exePath) {
    std::error_code ec;

    fs::path portable = exePath.parent_path() / "inis" / "PCSX2.ini";
    if (fs::exists(portable, ec)) return portable;

    PWSTR docs = nullptr;
    fs::path ini;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)))
        ini = fs::path(docs) / "PCSX2" / "inis" / "PCSX2.ini";
    CoTaskMemFree(docs);

    if (!ini.empty() && fs::exists(ini, ec)) return ini;
    return {};
}

HostFsResult ensureHostFs(const fs::path& ini) {
    std::vector<std::string> lines;
    {
        std::ifstream in(ini);
        if (!in) return HostFsResult::WriteFailed;
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
    }

    long emuCoreLine = -1;
    bool found = false, changed = false;
    std::string section;
    for (size_t i = 0; i < lines.size() && !found; ++i) {
        std::string t = trim(lines[i]);
        if (t.size() >= 2 && t.front() == '[' && t.back() == ']') {
            section = t;
            if (section == "[EmuCore]") emuCoreLine = (long)i;
            continue;
        }
        if (section != "[EmuCore]" || t.rfind("HostFs", 0) != 0) continue;
        std::string rest = trim(t.substr(6));
        if (rest.empty() || rest.front() != '=') continue;  // e.g. HostFsFoo
        found = true;
        if (trim(rest.substr(1)) != "true") {
            lines[i] = "HostFs = true";
            changed = true;
        }
    }

    if (!found) {
        if (emuCoreLine >= 0) {
            lines.insert(lines.begin() + emuCoreLine + 1, "HostFs = true");
        } else {
            lines.push_back("[EmuCore]");
            lines.push_back("HostFs = true");
        }
        changed = true;
    }
    if (!changed) return HostFsResult::AlreadyEnabled;

    std::ofstream out(ini, std::ios::trunc);
    if (!out) return HostFsResult::WriteFailed;
    for (const auto& l : lines) out << l << '\n';
    return out ? HostFsResult::Enabled : HostFsResult::WriteFailed;
}

}  // namespace pcsx2
