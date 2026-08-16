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

#include <cstdio>

#include "app.hpp"
#include "app_internal.hpp"
#include "platform.hpp"
#include "theme.hpp"
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

void App::drawUpdateModal() {
    const char* kTitle = "Update##update";
    if (openUpdatePopup_) {
        ImGui::OpenPopup(kTitle);
        openUpdatePopup_ = false;
    }
    ImGui::SetNextWindowSize(ImVec2(scaled(560.0f), 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    const bool haveUpdate = update::isNewer(updateRelease_);
    const bool failed = !updateError_.empty();

    if (failed) {
        ImGui::TextColored(theme::semantics().warn, "%s", updateStatus_.c_str());
        ImGui::Spacing();
        ImGui::TextDisabled("You can always download the latest build by hand.");
    } else if (!haveUpdate) {
        ImGui::TextColored(theme::semantics().ok, "%s", updateStatus_.c_str());
    } else {
        ImGui::Text("TyraX %s is available.", updateRelease_.version.c_str());
        ImGui::TextDisabled("You have %s.", version::kEditorVersion);
        if (!updateRelease_.notes.empty()) {
            ImGui::Spacing();
            ImGui::SeparatorText("What's new");
            // The notes are somebody else's markdown - shown as plain text,
            // wrapped, in a bounded scroller, because a release with fifty
            // commits in it must not push the buttons off the screen.
            ImGui::BeginChild("##notes", ImVec2(0.0f, scaled(220.0f)),
                              ImGuiChildFlags_Borders);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(updateRelease_.notes.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndChild();
        }
    }

    if (updateDownloading_) {
        ImGui::Spacing();
        ImGui::TextUnformatted(updateStatus_.c_str());
        // Indeterminate on purpose: curl writes the file, not us, so there is
        // no honest fraction to draw. (-1.0f animates.)
        ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime(),
                           ImVec2(-FLT_MIN, 0.0f), "");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::BeginDisabled(updateDownloading_);
    if (haveUpdate && !failed) {
        const bool installable = !updateRelease_.assetUrl.empty();
#ifndef _WIN32
        // There is no Linux package yet (docs/backlog.md); offering a button
        // that downloads a Windows installer would be worse than not offering
        // one at all.
        const bool canInstall = false;
#else
        const bool canInstall = installable;
#endif
        if (canInstall) {
            const std::string label =
                "Download and install" +
                (updateRelease_.assetBytes > 0
                     ? "  (" + sizeText(updateRelease_.assetBytes) + ")"
                     : std::string());
            if (ImGui::Button(label.c_str())) updateDownload();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Downloads the installer and runs it. TyraX closes first -\n"
                    "you will be asked about unsaved work - and comes back\n"
                    "when the update is in.");
            ImGui::SameLine();
        }
        if (ImGui::Button("What's new")) {
            platform::openUrl(updateRelease_.pageUrl.empty()
                                  ? std::string("https://github.com/") +
                                        update::kRepo + "/releases"
                                  : updateRelease_.pageUrl);
        }
        ImGui::SameLine();
        if (ImGui::Button("Skip this version")) {
            globalUpdateSkip_ = updateRelease_.version;
            saveGlobalConfig();
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Stop mentioning %s at startup. The next release after it\n"
                "is announced as usual, and Help > Check for updates still\n"
                "answers about this one.",
                updateRelease_.version.c_str());
        ImGui::SameLine();
    }
    if (ImGui::Button(haveUpdate && !failed ? "Later" : "Close"))
        ImGui::CloseCurrentPopup();
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::Dummy(ImVec2(scaled(8.0f), 0.0f));
    ImGui::SameLine();
    if (ImGui::Checkbox("Check at startup", &globalUpdateCheck_)) saveGlobalConfig();
    prefHelp(
        "Ask GitHub for the latest release when the editor starts. Off means\n"
        "nothing leaves this machine on its own - Help > Check for updates\n"
        "still works whenever you ask it to.");

    ImGui::EndPopup();
}
