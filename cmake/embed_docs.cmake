# Embeds docs/*.md into a generated header so the in-editor AI assistant
# (src/aichat.cpp, docs/ai-chat.md) can read the editor's own documentation at
# runtime - the editor is installed without the repo, so nothing on disk can be
# relied on. Run as:
#   cmake -DSRC_DIR=<repo>/docs -DOUT_FILE=<build>/generated/docs_gen.hpp
#         -P embed_docs.cmake
#
# The markdown in docs/ stays the single source of truth: it is what a human
# reads and what the assistant reads, so the standing "every change updates the
# docs in the same commit" rule keeps the assistant current for free. Only
# top-level *.md files are taken - docs/img/ is binary and has nothing to say.
# Raw string literals use a TYRAXDOC delimiter; content must not contain the
# sequence )TYRAXDOC" (no markdown ever should).

file(GLOB files RELATIVE "${SRC_DIR}" "${SRC_DIR}/*.md")
list(SORT files)

set(out "// Generated from docs/ by cmake/embed_docs.cmake - do not edit.\n")
string(APPEND out "#pragma once\n\nnamespace aichat {\n\n")
string(APPEND out "struct EmbeddedDoc {\n")
string(APPEND out "    const char* name;     // file stem, e.g. \"menu-styles\"\n")
string(APPEND out "    const char* content;  // the markdown, verbatim\n};\n\n")
string(APPEND out "inline const EmbeddedDoc kEmbeddedDocs[] = {\n")

foreach(f IN LISTS files)
    string(REGEX REPLACE "\\.md$" "" name "${f}")
    file(READ "${SRC_DIR}/${f}" content)
    if(content MATCHES "\\)TYRAXDOC\"")
        message(FATAL_ERROR "docs/${f} contains the raw-string delimiter )TYRAXDOC\"")
    endif()
    string(APPEND out "    {\"${name}\", R\"TYRAXDOC(${content})TYRAXDOC\"},\n")
endforeach()

string(APPEND out "};\n\n}  // namespace aichat\n")
file(WRITE "${OUT_FILE}" "${out}")
