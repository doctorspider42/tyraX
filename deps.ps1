# Single source of truth for everything fetched into vendor/ and tools/.
#
# setup.ps1 fetches from these lists and build.ps1 reads the SAME lists to
# refuse to configure while a dependency the editor compiles is missing. That
# is the whole point of the file: a dependency added here is guarded
# automatically, so there is no second list to forget. When there were two
# lists, adding ufbx left build.ps1 checking the old four directories and a
# fresh clone / older worktree walked straight into cmake with half the sources
# on disk - which surfaces as the baffling pair of errors
# "Cannot find source file: vendor/<dep>/..." + "No SOURCES given to target".
#
# Fields:
#   Probe - a file the build actually needs, not just the directory, so an
#           interrupted or partial clone counts as missing.
#   Build - $true when CMake compiles it into tyrax-editor, i.e. build.ps1
#           blocks on it. vendor/tyra is the in-tree PS2 engine fork: its
#           sources are tracked in this repo and compiled inside Docker, never
#           by the editor build, so it is listed for provenance only.

$VendorDeps = @(
    @{ Url = 'https://github.com/ocornut/imgui.git'
       Branch = 'docking'; Dir = 'vendor/imgui'
       Probe = 'vendor/imgui/imgui.cpp'; Build = $true },
    @{ Url = 'https://github.com/glfw/glfw.git'
       Branch = '3.4'; Dir = 'vendor/glfw'
       Probe = 'vendor/glfw/CMakeLists.txt'; Build = $true },
    @{ Url = 'https://github.com/CedricGuillemet/ImGuizmo.git'
       Branch = 'master'; Dir = 'vendor/imguizmo'
       Probe = 'vendor/imguizmo/src/ImGuizmo.cpp'; Build = $true },
    @{ Url = 'https://github.com/Nelarius/imnodes.git'
       Branch = 'master'; Dir = 'vendor/imnodes'
       Probe = 'vendor/imnodes/imnodes.cpp'; Build = $true },
    @{ Url = 'https://github.com/nothings/stb.git'
       Branch = 'master'; Dir = 'vendor/stb'
       Probe = 'vendor/stb/stb_image.h'; Build = $true },
    @{ Url = 'https://github.com/ufbx/ufbx.git'
       Branch = 'master'; Dir = 'vendor/ufbx'
       Probe = 'vendor/ufbx/ufbx.c'; Build = $true },
    @{ Url = 'https://github.com/mackron/miniaudio.git'
       Branch = 'master'; Dir = 'vendor/miniaudio'
       Probe = 'vendor/miniaudio/miniaudio.h'; Build = $true },
    @{ Url = 'https://github.com/h4570/tyra.git'
       Branch = 'master'; Dir = 'vendor/tyra'
       Probe = 'vendor/tyra/Makefile.base'; Build = $false }
)

# stb ships single headers we #include directly; back-fill any that a stale or
# partial vendor/stb is missing (setup.ps1 does the fetching).
$StbHeaders = @('stb_image.h', 'stb_truetype.h', 'stb_image_write.h')

# Real-PS2 network deploy tools ("Run on PS2" in the editor): ps2client.exe
# talks to a console running ps2link. The Runner looks for it in
# tools/ps2client/bin; ps2link goes onto the console's memory card once
# (edit IPCONFIG.DAT for your LAN - format: "ip netmask gateway"). Not needed
# to build the editor, so build.ps1 never blocks on these.
$Ps2Tools = @(
    @{ Url = 'https://github.com/ps2dev/ps2client/releases/download/v1.3.0/ps2client-211df54b-windows-latest.tar.gz'
       Dir = 'tools/ps2client'; Probe = 'tools/ps2client/bin/ps2client.exe' },
    @{ Url = 'https://github.com/ps2dev/ps2link/releases/download/RenameMe/ps2link-0269a955-highloading.tar.gz'
       Dir = 'tools/ps2link';   Probe = 'tools/ps2link/ps2link/PS2LINK.ELF' }
)
