# Embeds an arbitrary binary file into a generated header, so the editor ships
# it inside the executable instead of needing a data file next to the binary.
# Run as:
#   cmake -DSRC_FILE=<repo>/resources/x.png -DOUT_FILE=<build>/generated/x_gen.hpp
#         -DNAMESPACE=appicon -DSYMBOL=kIconPng -P embed_binary.cmake
#
# Two callers today, and the reason this is one script rather than two: the
# app icon (resources/icon.png - GLFW wants decoded pixels at runtime and the
# freedesktop desktop entry wants a PNG on disk, see platform::installDesktopEntry
# and App::run) and NASA's lunar colour map (resources/moon-lroc-color-1k.jpg,
# projected into the moon disc by menubake::bakeMoonPNG). A second near-identical
# copy of this file is exactly the kind of pair that drifts.

file(READ "${SRC_FILE}" hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
# Break the array every 16 bytes. CMake's regex has no {n} quantifier, so the
# group is built with string(REPEAT) instead of being written out sixteen times.
string(REPEAT "0x..," 16 group)
string(REGEX REPLACE "(${group})" "\\1\n    " bytes "${bytes}")

get_filename_component(src_name "${SRC_FILE}" NAME)
set(out "// Generated from resources/${src_name} by cmake/embed_binary.cmake - do not edit.\n")
string(APPEND out "#pragma once\n\n#include <cstddef>\n\nnamespace ${NAMESPACE} {\n\n")
string(APPEND out "inline const unsigned char ${SYMBOL}[] = {\n    ${bytes}\n};\n")
string(APPEND out "inline constexpr std::size_t ${SYMBOL}Size = sizeof(${SYMBOL});\n\n")
string(APPEND out "}  // namespace ${NAMESPACE}\n")
file(WRITE "${OUT_FILE}" "${out}")
