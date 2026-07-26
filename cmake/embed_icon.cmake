# Embeds resources/icon.png into a generated header so the app icon ships
# inside the binary. Run as:
#   cmake -DSRC_FILE=<repo>/resources/icon.png -DOUT_FILE=<build>/generated/icon_gen.hpp
#         -P embed_icon.cmake
#
# Windows takes its icon from resources/app.rc (the GLFW_ICON resource), which
# also gives Explorer the file icon. Everything else has no such mechanism:
# GLFW wants the decoded pixels at runtime and the freedesktop desktop entry
# wants a PNG on disk, so both are fed from these bytes - see
# platform::installDesktopEntry and App::run.

file(READ "${SRC_FILE}" hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
# Break the array every 16 bytes. CMake's regex has no {n} quantifier, so the
# group is built with string(REPEAT) instead of being written out sixteen times.
string(REPEAT "0x..," 16 group)
string(REGEX REPLACE "(${group})" "\\1\n    " bytes "${bytes}")

set(out "// Generated from resources/icon.png by cmake/embed_icon.cmake - do not edit.\n")
string(APPEND out "#pragma once\n\n#include <cstddef>\n\nnamespace appicon {\n\n")
string(APPEND out "inline const unsigned char kIconPng[] = {\n    ${bytes}\n};\n")
string(APPEND out "inline constexpr std::size_t kIconPngSize = sizeof(kIconPng);\n\n")
string(APPEND out "}  // namespace appicon\n")
file(WRITE "${OUT_FILE}" "${out}")
