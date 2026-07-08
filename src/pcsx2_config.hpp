#pragma once

#include <filesystem>

// PCSX2 boots an ELF even with "Host Filesystem" disabled, but every in-game
// fopen("host:...") then fails and Tyra asserts on the first asset load.
// These helpers flip the setting in PCSX2.ini before the emulator starts
// (PCSX2 rewrites the ini on exit, so only edit while it is not running).
namespace pcsx2 {

// Locates PCSX2.ini for the given emulator executable: portable builds keep
// inis/ next to the exe, installed builds use <Documents>\PCSX2\inis
// (Documents may be redirected, e.g. to OneDrive, so the shell is asked
// instead of assuming %USERPROFILE%). Returns an empty path if not found.
std::filesystem::path findIni(const std::filesystem::path& exePath);

enum class HostFsResult { AlreadyEnabled, Enabled, WriteFailed };

// Makes sure HostFs = true under [EmuCore]; a missing key means false
// (the PCSX2 default), so it is added when absent.
HostFsResult ensureHostFs(const std::filesystem::path& ini);

}  // namespace pcsx2
