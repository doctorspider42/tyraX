// The update check's App side (docs/updates.md) - App:: methods declared in
// app.hpp, in their own TU (the credits_ui.cpp / chat_ui.cpp precedent).
// update.cpp is the host half: the comparison, the parser, curl. Everything
// here needs project-less App state, ImGui and the exit path, and nothing else
// in the editor needs it, so it does not belong in app.cpp.
//
// The shape is the one every worker-backed feature here uses: a thread writes
// members, an atomic says when it is done, and a tick called EVERY FRAME from
// drawUI collects the answer - never from the modal's body, because a check
// started at startup must land whether or not anything is on screen.

#include <imgui.h>
#include <imgui_internal.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>

#include "app.hpp"
#include "app_internal.hpp"
#include "platform.hpp"
#include "theme.hpp"
#include "toolkit.hpp"
#include "version.hpp"

namespace fs = std::filesystem;

namespace {

// "12.4 MB" - the one number a person actually weighs before agreeing to a
// download.
std::string sizeText(long long bytes) {
    if (bytes <= 0) return {};
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    return buf;
}

}  // namespace

void App::updateJoinWorker() {
    if (updateThread_.joinable()) updateThread_.join();
    updateDone_.store(false);
    updateBusy_.store(false);
}

void App::startUpdateCheck(bool manual) {
    if (updateBusy_.load()) return;  // one worker, and it is busy
    updateJoinWorker();
    updateManual_ = manual;
    updateChecking_ = true;
    updateJob_ = UpdateJob::Check;
    updateError_.clear();
    updateStatus_ = "Checking for updates...";
    updateBusy_.store(true);
    updateThread_ = std::thread([this] {
        update::Release r;
        std::string err;
        const bool ok = update::fetchLatest(r, err);
        updateRelease_ = ok ? r : update::Release{};
        updateError_ = ok ? std::string() : err;
        updateDone_.store(true);
    });
}

void App::updateDownload() {
    if (updateBusy_.load() || updateRelease_.assetUrl.empty()) return;
    updateJoinWorker();
    updateDownloading_ = true;
    updateJob_ = UpdateJob::Download;
    updateError_.clear();
    updateStatus_ = "Downloading " + updateRelease_.assetName + "...";
    updateBusy_.store(true);
    updateThread_ = std::thread([this] {
        const fs::path dir = update::downloadDir();
        std::string err;
        fs::path dest;
        if (dir.empty()) {
            err = "no place to download to (the editor has no config directory)";
        } else {
            dest = dir / (updateRelease_.assetName.empty()
                              ? std::string("TyraX-Setup.exe")
                              : updateRelease_.assetName);
            if (!update::download(updateRelease_.assetUrl, dest, err)) dest.clear();
        }
        updateFile_ = dest;
        updateError_ = err;
        updateDone_.store(true);
    });
}

void App::updateTick() {
    if (!updateDone_.load()) return;
    updateJoinWorker();
    const UpdateJob job = updateJob_;
    updateJob_ = UpdateJob::None;

    if (job == UpdateJob::Check) {
        updateChecking_ = false;
        if (!updateError_.empty()) {
            // "Update check: ..." rather than "failed": one of the answers
            // here is "the repository has no releases yet", which is a state
            // and not a failure, and both read correctly this way.
            updateStatus_ = "Update check: " + updateError_;
            // A failed startup check is not news - the machine may simply be
            // offline, and an editor that opens a dialog about that is an
            // editor people turn the check off in. Only an ASKED-FOR check
            // reports its failure, and even then in the modal, not a box.
            if (updateManual_) openUpdatePopup_ = true;
            return;
        }
        if (!update::isNewer(updateRelease_)) {
            updateStatus_ = std::string("TyraX ") + version::kEditorVersion +
                            " is the latest version.";
            if (updateManual_) openUpdatePopup_ = true;
            return;
        }
        updateStatus_ = "TyraX " + updateRelease_.version + " is available.";
        // The skip only silences the AUTOMATIC check: somebody who asks from
        // the menu is asking about this version too.
        if (!updateManual_ && updateRelease_.version == globalUpdateSkip_) return;
        openUpdatePopup_ = true;
        return;
    }

    if (job == UpdateJob::Download) {
        updateDownloading_ = false;
        if (updateDownloadCancelled_) {
            // Cancel kills curl, which reads as a failure - it is not one.
            updateDownloadCancelled_ = false;
            updateError_.clear();
            updateStatus_ = "Download cancelled";
            return;
        }
        if (!updateError_.empty() || updateFile_.empty()) {
            updateStatus_ = "Download failed: " +
                            (updateError_.empty() ? std::string("unknown error")
                                                  : updateError_);
            openUpdatePopup_ = true;
            return;
        }
        std::string err;
        if (!update::runInstaller(updateFile_, err)) {
            updateStatus_ = "Could not start the installer: " + err;
            openUpdatePopup_ = true;
            return;
        }
        // The installer is up and will replace the files this process is
        // running from, so leave - through the ordinary exit path, which is
        // what asks about unsaved work. If the user cancels that prompt, the
        // installer waits on the Restart Manager and the two settle it between
        // them; nothing here has to guess.
        requestExit();
    }
}

// --- Release notes ---------------------------------------------------------
// The notes are markdown and used to be shown raw ("### A redesigned...",
// "**A new look**:" with the asterisks). This is the small subset releases
// actually use: #/##/### headings, "- "/"* " bullets, **bold**, `code`,
// [text](link) as text. Anything else is a paragraph. Laid out word by word,
// because ImGui's wrapped text cannot change font mid-line.
namespace {

struct Span {
    std::string text;
    bool bold = false, mono = false;
};

std::vector<Span> inlineSpans(const std::string& in) {
    std::vector<Span> out;
    Span cur;
    auto flush = [&] {
        if (!cur.text.empty()) out.push_back(cur);
        cur.text.clear();
    };
    for (size_t i = 0; i < in.size(); ++i) {
        if (in.compare(i, 2, "**") == 0) {
            flush();
            cur.bold = !cur.bold;
            ++i;
            continue;
        }
        if (in[i] == '`') {
            flush();
            cur.mono = !cur.mono;
            continue;
        }
        if (in[i] == '[') {
            const size_t close = in.find("](", i);
            const size_t end = close == std::string::npos ? close : in.find(')', close);
            if (end != std::string::npos) {
                cur.text += in.substr(i + 1, close - i - 1);
                i = end;
                continue;
            }
        }
        cur.text += in[i];
    }
    flush();
    return out;
}

// Draws `spans` flowed into [x0, x0 + width] from the cursor; advances it.
void flowSpans(const std::vector<Span>& spans, float x0, float width, ImU32 col, ImU32 dimCol) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float lineH = ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y * 0.5f;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    pos.x = x0;
    const float startY = pos.y;
    for (const Span& s : spans) {
        ImFont* f = s.bold && ui::boldFont() ? ui::boldFont() : s.mono && ui::monoFont() ? ui::monoFont() : nullptr;
        ui::FontScope scope(f);
        const float spaceW = ImGui::CalcTextSize(" ").x;
        size_t i = 0;
        while (i < s.text.size()) {
            size_t j = s.text.find(' ', i);
            if (j == std::string::npos) j = s.text.size();
            const std::string word = s.text.substr(i, j - i);
            const float ww = ImGui::CalcTextSize(word.c_str()).x;
            if (pos.x > x0 && pos.x + ww > x0 + width) {
                pos.x = x0;
                pos.y += lineH;
            }
            dl->AddText(pos, s.bold ? col : (s.mono ? dimCol : col), word.c_str());
            pos.x += ww;
            if (j < s.text.size()) pos.x += spaceW;
            i = j + 1;
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, startY));
    ImGui::Dummy(ImVec2(width, pos.y - startY + lineH));
}

void drawReleaseNotes(const std::string& md) {
    const theme::Semantics& sem = theme::semantics();
    const ImU32 text = ImGui::GetColorU32(sem.text);
    const ImU32 dim = ImGui::GetColorU32(sem.textDim);
    const float fs = ImGui::GetFontSize();
    std::istringstream in(md);
    std::string line, para;
    auto flushPara = [&] {
        if (para.empty()) return;
        const float x0 = ImGui::GetCursorScreenPos().x;
        flowSpans(inlineSpans(para), x0, ImGui::GetContentRegionAvail().x, text, dim);
        para.clear();
    };
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t lead = line.find_first_not_of(' ');
        if (lead == std::string::npos) {
            flushPara();
            continue;
        }
        const std::string t = line.substr(lead);
        if (t[0] == '#') {
            flushPara();
            size_t h = t.find_first_not_of('#');
            const std::string title = h == std::string::npos ? std::string() : t.substr(h + (t[h] == ' ' ? 1 : 0));
            ImGui::Dummy(ImVec2(0, fs * 0.3f));
            ui::FontScope bold(ui::boldFont());
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(title.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Dummy(ImVec2(0, fs * 0.15f));
            continue;
        }
        if ((t[0] == '-' || t[0] == '*') && t.size() > 1 && t[1] == ' ') {
            flushPara();
            const float indent = fs * 0.4f + (float)lead * fs * 0.3f;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(p.x + indent + fs * 0.3f, p.y + ImGui::GetTextLineHeight() * 0.55f), fs * 0.13f, dim);
            const float x0 = p.x + indent + fs * 1.0f;
            flowSpans(inlineSpans(t.substr(2)), x0, ImGui::GetContentRegionAvail().x - (x0 - p.x), text, dim);
            ImGui::Dummy(ImVec2(0, fs * 0.2f));
            continue;
        }
        para += (para.empty() ? "" : " ") + t;
    }
    flushPara();
}

// The release asset in words, the way the header names it: what kind of
// package for which machine, then its size - "macOS app, Apple silicon ·
// 188 MB" - instead of the file name the release workflow stamped.
std::string describeAsset(const std::string& name, long long bytes) {
    auto ends = [&](const char* suf) {
        const size_t n = std::strlen(suf);
        if (name.size() < n) return false;
        for (size_t i = 0; i < n; ++i)
            if (std::tolower((unsigned char)name[name.size() - n + i]) != suf[i]) return false;
        return true;
    };
    std::string what;
    if (ends("-macos-arm64.dmg")) what = "macOS app, Apple silicon";
    else if (ends("-macos-x86_64.dmg")) what = "macOS app, Intel";
    else if (ends(".dmg")) what = "macOS app";
    else if (ends(".exe")) what = "Windows installer";
    else if (ends("-linux-x86_64.tar.gz")) what = "Linux, x86-64";
    else if (ends(".tar.gz")) what = "Linux";
    else what = name;
    if (bytes > 0) {
        char mb[32];
        std::snprintf(mb, sizeof(mb), "%.0f MB", (double)bytes / 1048576.0);
        what += what.empty() ? std::string(mb) : std::string(" \xC2\xB7 ") + mb;
    }
    return what;
}

// "2026-10-16" -> "Oct 16"; anything else as it came.
std::string shortDate(const std::string& iso) {
    static const char* const kMon[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int y = 0, m = 0, d = 0;
    if (std::sscanf(iso.c_str(), "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12) return iso;
    return std::string(kMon[m - 1]) + " " + std::to_string(d);
}

}  // namespace

void App::drawUpdateModal() {
    const char* kTitle = "Update##update";
    if (openUpdatePopup_) {
        ImGui::OpenPopup(kTitle);
        openUpdatePopup_ = false;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const bool haveUpdate = update::isNewer(updateRelease_);
    const bool failed = !updateError_.empty();
    const bool full = haveUpdate && !failed;
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ImMin(scaled(560.0f), vp->WorkSize.x * 0.95f), 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
        return;

    const theme::Semantics& sem = theme::semantics();
    const ImGuiStyle& st = ImGui::GetStyle();
    const float fs = ImGui::GetFontSize();
    const float w = ImMin(scaled(540.0f), vp->WorkSize.x * 0.9f);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // ---- header: icon tile, title, version jump ----
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float tile = fs * 2.6f;
        const ImVec4 tint = failed ? sem.warn : full ? sem.accent : sem.ok;
        dl->AddRectFilled(p, ImVec2(p.x + tile, p.y + tile),
                          ImGui::GetColorU32(ImVec4(tint.x, tint.y, tint.z, 0.16f)), scaled(9));
        if (full)
            ui::DrawGlyph(dl, ui::Glyph::Sparkle, ImVec2(p.x + tile * 0.5f, p.y + tile * 0.5f), tile * 0.45f,
                          ImGui::GetColorU32(tint));
        else if (failed)
            ui::DrawGlyph(dl, ui::Glyph::Flag, ImVec2(p.x + tile * 0.5f, p.y + tile * 0.5f), tile * 0.45f,
                          ImGui::GetColorU32(tint));
        else
            ui::CheckMark(dl, ImVec2(p.x + tile * 0.5f, p.y + tile * 0.5f), tile * 0.5f, ImGui::GetColorU32(tint));
        ImGui::Dummy(ImVec2(tile, tile));
        ImGui::SameLine(0.0f, scaled(14));
        ImGui::BeginGroup();
        {
            ui::FontScope bold(ui::boldFont());
            ImGui::PushFont(nullptr, fs * 1.2f);
            ImGui::TextUnformatted(failed ? "Could not check for updates"
                                   : full ? "A new TyraX is ready"
                                          : "TyraX is up to date");
            ImGui::PopFont();
        }
        if (full) {
            ui::FontScope mono(ui::monoFont());
            ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
            ImGui::TextUnformatted(version::kEditorVersion);
            ImGui::SameLine();
            ImGui::TextUnformatted("->");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, sem.ok);
            ImGui::TextUnformatted(updateRelease_.version.c_str());
            ImGui::PopStyleColor();
            std::string meta;
            if (!updateRelease_.published.empty()) meta += "\xC2\xB7 " + shortDate(updateRelease_.published);
            if (!updateRelease_.assetName.empty())
                meta += (meta.empty() ? "\xC2\xB7 " : " \xC2\xB7 ") +
                        describeAsset(updateRelease_.assetName, updateRelease_.assetBytes);
            if (!meta.empty()) {
                ImGui::SameLine();
                ui::FontScope words(ImGui::GetDefaultFont());  // words, not numbers: the UI face
                ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
                ImGui::TextUnformatted(meta.c_str());
                ImGui::PopStyleColor();
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - tile - scaled(14));
            ImGui::TextUnformatted(updateStatus_.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        ImGui::EndGroup();
    }
    ImGui::Dummy(ImVec2(w, scaled(6)));

    if (failed) {
        ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
        ImGui::TextUnformatted("You can always download the latest build by hand:");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, sem.accent);
        if (ImGui::TextLink("Releases on GitHub"))
            platform::openUrl(std::string("https://github.com/") + update::kRepo + "/releases");
        ImGui::PopStyleColor();
    }

    if (full) {
        ImGui::Separator();
        // The notes, rendered, in a bounded scroller: a release with fifty
        // bullets must not push the buttons off the screen.
        if (!updateRelease_.notes.empty()) {
            ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0.0f),
                                                ImVec2(w, ImMin(scaled(300.0f), vp->WorkSize.y * 0.45f)));
            ImGui::BeginChild("##notes", ImVec2(w, 0.0f), ImGuiChildFlags_AutoResizeY);
            drawReleaseNotes(updateRelease_.notes);
            ImGui::EndChild();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, sem.accent);
        if (ImGui::TextLink("Full release notes on GitHub"))
            platform::openUrl(updateRelease_.pageUrl.empty()
                                  ? std::string("https://github.com/") + update::kRepo + "/releases"
                                  : updateRelease_.pageUrl);
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, scaled(4)));

        // What installing does to THIS session, said before the click (it
        // used to be a tooltip).
        const std::string blocked = update::selfInstallBlocked();
        const bool canInstall = !updateRelease_.assetUrl.empty() && blocked.empty();
        if (ui::BeginCard("##install_info", ImVec2(w, 0))) {
            ImGui::PushTextWrapPos(0.0f);
            if (!blocked.empty()) {
                ImGui::TextUnformatted(blocked.c_str());
            } else if (updateRelease_.assetUrl.empty()) {
                ImGui::TextUnformatted("This release has no package for this system - download it from GitHub.");
            } else {
                std::string msg = "TyraX closes, installs the update and reopens.";
                const int unsaved = hasProject_ ? unsavedEditCount() : 0;
                if (hasProject_ && (unsaved > 0 || dirty_))
                    msg += " You have " + (unsaved > 0 ? std::to_string(unsaved) : std::string("some")) +
                           (unsaved == 1 ? " unsaved change" : " unsaved changes") + " in " + project_.name +
                           " - you will be asked to save first.";
                ImGui::TextUnformatted(msg.c_str());
            }
            ImGui::PopTextWrapPos();
        }
        ui::EndCard();
        ImGui::Dummy(ImVec2(0, scaled(4)));

        if (updateDownloading_) {
            // Determinate: curl writes the file, so its size on disk over the
            // asset size GitHub reported is an honest fraction.
            std::error_code ec;
            const std::filesystem::path dest = update::downloadDir() / updateRelease_.assetName;
            const long long have = (long long)std::filesystem::file_size(dest, ec);
            const long long got = ec ? 0 : have;
            const double now = ImGui::GetTime();
            if (now - updateRateAt_ > 0.5) {
                updateRate_ = updateRateAt_ > 0.0 ? (double)(got - updateRateBytes_) / (now - updateRateAt_) : 0.0;
                updateRateAt_ = now;
                updateRateBytes_ = got;
            }
            char detail[96];
            std::snprintf(detail, sizeof(detail), "%.1f / %.1f MB  %.1f MB/s", got / 1048576.0,
                          updateRelease_.assetBytes / 1048576.0, ImMax(0.0, updateRate_) / 1048576.0);
            ImGui::BeginChild("##dl", ImVec2(w, fs * 2.4f));
            ui::ProgressInline(updateRelease_.assetBytes > 0 ? (float)got / (float)updateRelease_.assetBytes : -1.0f,
                               "Downloading", detail);
            ImGui::EndChild();
            const float cw = ImGui::CalcTextSize("Cancel download").x + st.FramePadding.x * 2.0f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + w - cw);
            if (ImGui::Button("Cancel download")) {
                updateDownloadCancelled_ = true;
                update::cancel();
            }
        } else {
            // Tertiary on the left, the two choices on the right.
            const std::string skip = "Skip " + updateRelease_.version;
            if (ui::GhostButton(skip.c_str())) {
                globalUpdateSkip_ = updateRelease_.version;
                saveGlobalConfig();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Stop announcing this version at startup.\nHelp > Check for updates still shows it.");
            std::string install = "Install and restart";
            if (updateRelease_.assetBytes > 0) {
                char mb[32];
                std::snprintf(mb, sizeof(mb), "  (%.0f MB)", updateRelease_.assetBytes / 1048576.0);
                install += mb;
            }
            const float iw = ImGui::CalcTextSize(install.c_str()).x + st.FramePadding.x * 2.6f;
            const float lw = ImGui::CalcTextSize("Later").x + st.FramePadding.x * 2.0f;
            ImGui::SameLine(ImGui::GetCursorStartPos().x + w - iw - lw - st.ItemSpacing.x);
            if (ImGui::Button("Later")) ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            if (canInstall) {
                if (ui::PrimaryButton(install.c_str(), iw)) {
                    updateRateAt_ = 0.0;
                    updateRateBytes_ = 0;
                    updateDownloadCancelled_ = false;
                    updateDownload();
                }
            } else {
                if (ui::PrimaryButton("Open the release page", iw))
                    platform::openUrl(updateRelease_.pageUrl.empty()
                                          ? std::string("https://github.com/") + update::kRepo + "/releases"
                                          : updateRelease_.pageUrl);
            }
        }
    } else {
        ImGui::Dummy(ImVec2(0, scaled(4)));
        const float cw = ImGui::CalcTextSize("Close").x + st.FramePadding.x * 4.0f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + w - cw);
        if (ui::PrimaryButton("Close", cw) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    }

    // ---- footer: the startup check ----
    ImGui::Dummy(ImVec2(0, scaled(4)));
    ImGui::Separator();
    if (ui::Switch("Check at startup", &globalUpdateCheck_)) saveGlobalConfig();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
    ImGui::TextUnformatted("- asks GitHub once; nothing else leaves this machine.");
    ImGui::PopStyleColor();
    ImGui::EndPopup();
}
