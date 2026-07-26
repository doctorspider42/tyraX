# Single source of truth for everything fetched into vendor/ and tools/ on
# Linux/macOS - the POSIX twin of deps.ps1. setup.sh fetches from these lists
# and build.sh reads the SAME lists to refuse to configure while a dependency
# the editor compiles is missing. Keep the two files in step: a dependency
# added to one and not the other leaves that platform's build guard blind.
#
# Each VENDOR_DEPS entry is  url|branch|dir|probe|build
#   probe - a file the build actually needs, not just the directory, so an
#           interrupted or partial clone counts as missing.
#   build - "1" when CMake compiles it into tyrax-editor, i.e. build.sh blocks
#           on it. vendor/tyra is the in-tree PS2 engine fork: its sources are
#           tracked in this repo and compiled inside Docker, never by the
#           editor build, so it is listed for provenance only.

VENDOR_DEPS=(
    "https://github.com/ocornut/imgui.git|docking|vendor/imgui|vendor/imgui/imgui.cpp|1"
    "https://github.com/glfw/glfw.git|3.4|vendor/glfw|vendor/glfw/CMakeLists.txt|1"
    "https://github.com/CedricGuillemet/ImGuizmo.git|master|vendor/imguizmo|vendor/imguizmo/src/ImGuizmo.cpp|1"
    "https://github.com/Nelarius/imnodes.git|master|vendor/imnodes|vendor/imnodes/imnodes.cpp|1"
    "https://github.com/nothings/stb.git|master|vendor/stb|vendor/stb/stb_image.h|1"
    "https://github.com/ufbx/ufbx.git|master|vendor/ufbx|vendor/ufbx/ufbx.c|1"
    "https://github.com/h4570/tyra.git|master|vendor/tyra|vendor/tyra/Makefile.base|0"
)

# stb ships single headers we #include directly; back-fill any that a stale or
# partial vendor/stb is missing (setup.sh does the fetching).
STB_HEADERS=(stb_image.h stb_truetype.h stb_image_write.h)

# Real-PS2 network deploy tools ("Run on PS2" in the editor): ps2client talks
# to a console running ps2link. The Runner looks for it in tools/ps2client/bin;
# ps2link goes onto the console's memory card once (edit IPCONFIG.DAT for your
# LAN - format: "ip netmask gateway"). Not needed to build the editor, so
# build.sh never blocks on these. Note the ps2client tarball is per-OS - this
# is the Linux build; deps.ps1 names the Windows one.
#   url|dir|probe
PS2_TOOLS=(
    "https://github.com/ps2dev/ps2client/releases/download/v1.3.0/ps2client-211df54b-ubuntu-latest.tar.gz|tools/ps2client|tools/ps2client/bin/ps2client"
    "https://github.com/ps2dev/ps2link/releases/download/RenameMe/ps2link-0269a955-highloading.tar.gz|tools/ps2link|tools/ps2link/ps2link/PS2LINK.ELF"
)

# Distro packages the editor needs to configure and link (X11/Wayland/GL
# development headers plus the toolchain). build.sh names the missing ones in
# an apt/dnf/pacman-shaped hint rather than trying to install anything itself.
APT_PACKAGES="build-essential cmake ninja-build git pkg-config libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxkbcommon-dev libwayland-dev wayland-protocols"
