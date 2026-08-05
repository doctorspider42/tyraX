cmake_minimum_required(VERSION 3.20)
project(tyrax-editor CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Compiler cache (optional) ---
# Nothing here depends on it, but a hit costs milliseconds instead of seconds
# and this repo is worked on in several git worktrees at once, each with its own
# build directory - so the same translation unit gets compiled from scratch over
# and over. Picked up if it is on PATH; absent, the build is exactly what it
# was. Override with -DTYRAX_COMPILER_CACHE=OFF (or =/path/to/ccache).
set(TYRAX_COMPILER_CACHE "AUTO" CACHE STRING
    "Compiler cache to use: AUTO (ccache then sccache, if on PATH), OFF, or a path")
if(NOT TYRAX_COMPILER_CACHE STREQUAL "OFF")
    if(TYRAX_COMPILER_CACHE STREQUAL "AUTO")
        find_program(TYRAX_CACHE_EXE NAMES ccache sccache)
    else()
        set(TYRAX_CACHE_EXE "${TYRAX_COMPILER_CACHE}")
    endif()
    if(TYRAX_CACHE_EXE)
        set(CMAKE_C_COMPILER_LAUNCHER "${TYRAX_CACHE_EXE}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${TYRAX_CACHE_EXE}")
        message(STATUS "Compiler cache: ${TYRAX_CACHE_EXE}")
    endif()
endif()

# --- Dev build type ---
# Optimizes for how long the BUILD takes, not for how fast the editor runs:
# -O3 is roughly two thirds of this project's compile time (measured on
# app.cpp: 21 s at -O0, 37 s at -O1, 66 s at -O3). -O1 is the compromise that
# keeps the CPU-heavy host bakes (gibake/matbake/aobake raytracing, texture
# quantization) usable - do NOT ship a Dev build or benchmark one.
#
# FORCE-if-empty, not a plain cache set: compiler detection inside project()
# already created an empty CMAKE_<LANG>_FLAGS_DEV cache entry for the requested
# build type, so a non-FORCE set here is silently a no-op and Dev compiles at
# -O0-with-no-flags. Guarding on "still empty" keeps a user's own
# -DCMAKE_CXX_FLAGS_DEV=... on the command line winning.
foreach(_devflag C_FLAGS_DEV CXX_FLAGS_DEV)
    if(NOT CMAKE_${_devflag})
        set(CMAKE_${_devflag} "-O1 -g0" CACHE STRING "Dev build type flags" FORCE)
    endif()
endforeach()

# Compile the Windows resource script (app icon). Named GLFW_ICON so GLFW picks
# it up for the window/taskbar icon too - see resources/app.rc.
if(WIN32)
    enable_language(RC)
    set(TYRAX_EDITOR_RC resources/app.rc)
    # windres resolves the ICON path relative to its include dirs, not the .rc
    # file - point it at resources/ so it finds icon.ico.
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I${CMAKE_CURRENT_SOURCE_DIR}/resources")
endif()

# --- GLFW ---
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/glfw)

# --- Dear ImGui (docking branch) ---
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/vendor/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw)
# UI scripting (docs/ui-scripting.md): ImGui's test-engine hooks are how the
# editor knows which widget is where, so a script can click by NAME. We
# implement the four hook functions ourselves in src/uiscript.cpp - the
# imgui_test_engine library is NOT vendored (its licence is not ours to take on),
# and we need none of it.
#
# PUBLIC, and that is load-bearing: the define adds fields to ImGuiContext and
# values to ImGuiItemStatusFlags_, so the library and every consumer must agree
# or they disagree about the layout of a struct they both touch. ImGui only calls
# the hooks when g.TestEngineHookItems is set, which happens only while a script
# runs - so a normal session pays one never-taken branch per widget.
target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_TEST_ENGINE)

# --- Embedded AI-support skills (ai-support/ -> generated header) ---
# The markdown under ai-support/ is the source of truth for the assistant
# guidance the editor installs into projects ("Add AI support"); it is baked
# into the exe so nothing has to ship next to it. CONFIGURE_DEPENDS re-globs
# on build, so adding a skill file only needs a rebuild, not a reconfigure.
file(GLOB_RECURSE AI_SUPPORT_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/ai-support/*)
set(AI_SUPPORT_GEN ${CMAKE_BINARY_DIR}/generated/ai_support_gen.hpp)
add_custom_command(
    OUTPUT ${AI_SUPPORT_GEN}
    COMMAND ${CMAKE_COMMAND}
            -DSRC_DIR=${CMAKE_SOURCE_DIR}/ai-support
            -DOUT_FILE=${AI_SUPPORT_GEN}
            -P ${CMAKE_SOURCE_DIR}/cmake/embed_ai_support.cmake
    DEPENDS ${AI_SUPPORT_SOURCES} ${CMAKE_SOURCE_DIR}/cmake/embed_ai_support.cmake
    COMMENT "Embedding ai-support/ skills")

# --- Embedded app icon (resources/icon.png -> generated header) ---
# Windows gets its icon from the GLFW_ICON resource in resources/app.rc above.
# Every other platform needs the same image at RUNTIME - GLFW wants decoded
# pixels for the window icon and the freedesktop desktop entry wants a PNG on
# disk - so the file is baked in rather than shipped loose next to the binary.
set(APP_ICON_GEN ${CMAKE_BINARY_DIR}/generated/icon_gen.hpp)
add_custom_command(
    OUTPUT ${APP_ICON_GEN}
    COMMAND ${CMAKE_COMMAND}
            -DSRC_FILE=${CMAKE_SOURCE_DIR}/resources/icon.png
            -DOUT_FILE=${APP_ICON_GEN}
            -DNAMESPACE=appicon -DSYMBOL=kIconPng
            -P ${CMAKE_SOURCE_DIR}/cmake/embed_binary.cmake
    DEPENDS ${CMAKE_SOURCE_DIR}/resources/icon.png
            ${CMAKE_SOURCE_DIR}/cmake/embed_binary.cmake
    COMMENT "Embedding resources/icon.png")

# --- Embedded lunar colour map (resources/moon-lroc-color-1k.jpg) ---
# NASA SVS "CGI Moon Kit", LRO/LROC 2019 colour map (1024x512, public domain -
# see THIRD-PARTY-LICENSES.md). menubake::bakeMoonPNG projects it into the small
# moon disc a day/night cycle ships; the map itself never reaches the PS2, so it
# is baked into the editor rather than copied into every project.
set(MOON_MAP_GEN ${CMAKE_BINARY_DIR}/generated/moonmap_gen.hpp)
add_custom_command(
    OUTPUT ${MOON_MAP_GEN}
    COMMAND ${CMAKE_COMMAND}
            -DSRC_FILE=${CMAKE_SOURCE_DIR}/resources/moon-lroc-color-1k.jpg
            -DOUT_FILE=${MOON_MAP_GEN}
            -DNAMESPACE=moonmap -DSYMBOL=kMoonJpg
            -P ${CMAKE_SOURCE_DIR}/cmake/embed_binary.cmake
    DEPENDS ${CMAKE_SOURCE_DIR}/resources/moon-lroc-color-1k.jpg
            ${CMAKE_SOURCE_DIR}/cmake/embed_binary.cmake
    COMMENT "Embedding resources/moon-lroc-color-1k.jpg")

# Own target for the generators. If the headers were listed straight in
# tyrax-editor's sources, CMake would attach their custom commands to that
# target and give them order-only deps on everything it LINKS - so the
# generators (0.1 s of work) waited for libimgui.a, and with them every editor
# .obj. That serialised ~17 s of imgui.cpp in front of the whole editor build.
add_custom_target(tyrax-generated
    DEPENDS ${AI_SUPPORT_GEN} ${APP_ICON_GEN} ${MOON_MAP_GEN})

# --- Editor ---
add_executable(tyrax-editor
    src/main.cpp
    src/aigen.cpp
    src/aisupport.cpp
    src/ambience.cpp
    src/animedit.cpp
    src/aobake.cpp
    src/app.cpp
    src/audiopreview.cpp
    src/assetbrowser.cpp
    src/camtake.cpp
    src/credits_ui.cpp
    src/cutscene_ui.cpp
    src/devkit_ui.cpp
    src/devsession.cpp
    src/flowgraph_ui.cpp
    src/flownode.cpp
    src/hud_ui.cpp
    src/mateditor_ui.cpp
    src/prefab_ui.cpp
    src/procui.cpp
    src/props_ui.cpp
    src/fbxparser.cpp
    src/gl_loader.cpp
    src/glbparser.cpp
    src/json.cpp
    src/elfsym.cpp
    src/livedbg.cpp
    src/livelogic.cpp
    src/livepad.cpp
    src/livetime.cpp
    src/logview.cpp
    src/vucap.cpp
    src/bvh.cpp
    src/gibake.cpp
    src/vuir.cpp
    src/vuasm.cpp
    src/vusim.cpp
    src/vugen.cpp
    src/matbake.cpp
    src/objparser.cpp
    src/iso9660.cpp
    src/isoexport.cpp
    src/menubake.cpp
    src/meshlod.cpp
    src/migrations.cpp
    src/pcsx2_config.cpp
    src/phonecam.cpp
    src/placement.cpp
    src/platform.cpp
    src/pngquant.cpp
    src/prefab.cpp
    src/primmesh.cpp
    src/procgraph.cpp
    src/procgen.cpp
    src/procbake.cpp
    src/procrt.cpp
    src/decalproj.cpp
    src/dronegen.cpp
    src/droneui.cpp
    src/navmesh.cpp
    src/project.cpp
    src/savebake.cpp
    src/scrollsim.cpp
    src/screenfx.cpp
    src/session.cpp
    src/starfield.cpp
    src/stochtile.cpp
    src/texatlas.cpp
    src/texbake.cpp
    src/theme.cpp
    src/tmdl.cpp
    src/treegen.cpp
    src/uiscript.cpp
    src/uvunwrap.cpp
    src/save_assets.cpp
    src/templates.cpp
    src/viewport.cpp
    src/wavconvert.cpp
    src/wire.cpp
    src/runner.cpp
    vendor/imguizmo/src/ImGuizmo.cpp
    vendor/imnodes/imnodes.cpp
    vendor/ufbx/ufbx.c

    ${TYRAX_EDITOR_RC}
)
target_include_directories(tyrax-editor PRIVATE src vendor/imguizmo/src vendor/imnodes vendor/stb vendor/ufbx vendor/miniaudio ${CMAKE_BINARY_DIR}/generated)
target_compile_definitions(tyrax-editor PRIVATE GLFW_INCLUDE_NONE)
add_dependencies(tyrax-editor tyrax-generated)

find_package(OpenGL REQUIRED)
target_link_libraries(tyrax-editor PRIVATE imgui glfw OpenGL::GL)

if(WIN32)
    # shell32/ole32/uuid: the native file pickers and "Reveal in Explorer"
    # (src/platform.cpp). ws2_32: the socket transport (src/wire.cpp).
    target_link_libraries(tyrax-editor PRIVATE shell32 ole32 uuid ws2_32)
else()
    # Threads: std::thread needs an explicit -pthread with libstdc++, and
    # ${CMAKE_DL_LIBS} is what GLFW's runtime GL/Vulkan loading wants.
    find_package(Threads REQUIRED)
    target_link_libraries(tyrax-editor PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
endif()

if(MINGW)
    # Self-contained exe: don't depend on libstdc++/libgcc/libwinpthread DLLs
    # (a mismatched libwinpthread-1.dll on PATH breaks startup otherwise)
    target_link_options(tyrax-editor PRIVATE -static)
endif()

if(WIN32)
    # Console subsystem for now - useful to see logs during development
    set_target_properties(tyrax-editor PROPERTIES WIN32_EXECUTABLE OFF)
endif()
