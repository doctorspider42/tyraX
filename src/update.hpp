#pragma once

// Update check against the project's GitHub releases (docs/updates.md).
//
// Host-only: no ImGui, no GL, no Project - the livedbg/logview shape, so the
// version comparison and the release parser are pure functions of a string and
// can be exercised from a 40-line harness. The App side (the modal, the startup
// check, the preference) lives in update_ui.cpp.
//
// THE TRANSPORT IS curl, not a socket. wire.cpp speaks plain TCP and this needs
// TLS, so the editor shells out the way aigen.cpp already does for the OpenAI
// backend: curl.exe has shipped in Windows since 1803 and is on every Linux
// worth the name. A machine without it loses the update check and nothing else,
// which is why every failure here is a message and never a dialog.

#include <filesystem>
#include <string>

namespace update {

// The repository releases are read from. One string: the API call, the "open
// the release page" fallback and the docs all quote this, so a fork or a rename
// is one edit.
inline constexpr const char* kRepo = "doctorspider42/tyraX";

// Semantic-version compare over "1.51.0"-shaped strings: <0, 0, >0. Missing
// components read as 0 ("1.51" == "1.51.0") and a leading "v" is ignored, so a
// tag can be handed to it unpeeled. Anything non-numeric in a component stops
// that component at the digits it had.
int compareVersions(const std::string& a, const std::string& b);

struct Release {
    std::string version;    // "1.51.0" - the tag with any leading "v" removed
    std::string tag;
    std::string notes;      // the release body, as markdown
    std::string pageUrl;    // html_url: where "What's new" goes
    std::string assetUrl;   // this platform's package, "" when there is none
    std::string assetName;
    long long assetBytes = 0;
};

// Which asset of a release THIS build installs, matched by file-name suffix
// rather than by a name pattern: the release workflow stamps the version into
// every asset's name, so matching the whole name here would have to be kept in
// step with a string in a YAML file for no gain. A release carries all of them
// (docs/updates.md) - the Windows installer, the Linux tarball, the .deb and
// the .rpm - and this picks the one the running install can actually apply.
// That is the TARBALL on Linux even for a .deb or .rpm install: those are
// updated by the package manager, and `selfInstallBlocked` is what says so.
const char* platformAssetSuffix();

// Parses a GitHub /releases/latest response body. False + `error` on anything
// that is not one - including the {"message": "Not Found"} a repository with no
// releases yet answers with, which is a state and not a bug. The suffix is a
// parameter so the parser stays a pure function of two strings.
bool parseRelease(const std::string& body, Release& out, std::string& error,
                  const std::string& assetSuffix = platformAssetSuffix());

// Asks GitHub for the latest release. Blocking (call it from a worker thread) -
// runs curl with a hard timeout, so the worst case is bounded.
bool fetchLatest(Release& out, std::string& error);

// True when `r` names a version strictly newer than this build.
bool isNewer(const Release& r);

// Where a downloaded installer is parked: <configDir>/updates. Machine-global
// like everything else in editor.ini, and outside any project.
std::filesystem::path downloadDir();

// Downloads `url` to `dest` (blocking, resumable-not: a partial file is
// deleted). False + `error` on anything but a complete transfer.
bool download(const std::string& url, const std::filesystem::path& dest,
              std::string& error);

// Kills whatever curl is running right now, so a worker blocked on a slow
// network cannot hold the editor's exit for the request's whole timeout. Safe
// from another thread, and a no-op when nothing is in flight; the blocked call
// then returns false with "cancelled". Terminal - nothing re-arms after it.
void cancel();

// HOW THIS COPY OF TYRAX GOT ONTO THE MACHINE - the question the install half
// of the feature turns on, because only some of the answers can be updated
// from inside the editor. On Linux it is one word in a `.tyrax-package` marker
// file at the install root, written by installer/build-package.sh; its absence
// means a development checkout, which is the right answer for a repo built
// with ./build.sh.
enum class InstallKind {
    Source,   // a checkout: git pull + ./build.sh
    Windows,  // installer/tyrax.iss - updates itself silently, per user
    Tarball,  // the Linux tar.gz - updates itself by unpacking over its own tree
    Deb,      // /opt/tyrax owned by dpkg - the package manager's job
    Rpm,      // /opt/tyrax owned by rpm  - the package manager's job
};
InstallKind installKind();

// The directory the install's tree starts at: the editor's own binary lives in
// <root>/bin, and the engine, tools and VU sources beside it. Empty when the
// executable's path cannot be resolved.
std::filesystem::path installRoot();

// "" when this install can replace itself, otherwise ONE SENTENCE saying what
// to do instead (update through apt/dnf, rebuild the checkout). The UI offers
// the install button exactly when this is empty and prints it when it is not -
// so a refusal always comes with the alternative, and there is one place that
// decides.
std::string selfInstallBlocked();

// Applies the downloaded package and returns immediately; the caller then
// closes the editor so the update can replace the files it is running from.
// Windows runs the installer silently and it relaunches us (installer/
// tyrax.iss); Linux hands a detached shell script the tarball, which waits for
// this process to exit, unpacks over the install root and starts the editor
// again. Refuses with `selfInstallBlocked`'s sentence anywhere else.
bool runInstaller(const std::filesystem::path& file, std::string& error);

}  // namespace update
