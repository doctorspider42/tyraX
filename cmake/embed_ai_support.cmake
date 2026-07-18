# Embeds every file under ai-support/ into a generated header so the editor
# can install the AI assistant skills into projects without shipping loose
# files next to the exe. Run as:
#   cmake -DSRC_DIR=<repo>/ai-support -DOUT_FILE=<build>/generated/ai_support_gen.hpp
#         -P embed_ai_support.cmake
# The markdown in ai-support/ is the single source of truth - edit it there,
# rebuild, done. Raw string literals use a TYRAXAI delimiter; content must not
# contain the sequence )TYRAXAI" (no markdown ever should).

file(GLOB_RECURSE files RELATIVE "${SRC_DIR}" "${SRC_DIR}/*")
list(SORT files)

set(out "// Generated from ai-support/ by cmake/embed_ai_support.cmake - do not edit.\n")
string(APPEND out "#pragma once\n\nnamespace aisupport {\n\n")
string(APPEND out "struct EmbeddedFile {\n")
string(APPEND out "    const char* provider;  // \"claude\" | \"copilot\" (first ai-support/ dir)\n")
string(APPEND out "    const char* relPath;   // path below the provider dir, '/' separators\n")
string(APPEND out "    const char* content;\n};\n\n")
string(APPEND out "inline const EmbeddedFile kEmbeddedFiles[] = {\n")

foreach(f IN LISTS files)
    string(FIND "${f}" "/" slash)
    if(slash EQUAL -1)
        continue()  # stray file directly under ai-support/ - no provider
    endif()
    string(SUBSTRING "${f}" 0 ${slash} provider)
    math(EXPR rest "${slash} + 1")
    string(SUBSTRING "${f}" ${rest} -1 rel)
    file(READ "${SRC_DIR}/${f}" content)
    if(content MATCHES "\\)TYRAXAI\"")
        message(FATAL_ERROR "ai-support/${f} contains the raw-string delimiter )TYRAXAI\"")
    endif()
    string(APPEND out "    {\"${provider}\", \"${rel}\", R\"TYRAXAI(${content})TYRAXAI\"},\n")
endforeach()

string(APPEND out "};\n\n}  // namespace aisupport\n")
file(WRITE "${OUT_FILE}" "${out}")
