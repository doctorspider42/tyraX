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

# --- Same float results on every platform ---
# The host bakes write files that are committed (examples' .tmdl, AO atlases,
# physics hulls, light probes) and hash floats into cache signatures. clang on
# arm64 (every Mac) fuses a*b+c into one FMA by default, which rounds once
# instead of twice, so a Mac and a PC baked different bytes from the same
# project. Off everywhere: plain IEEE add/mul are then bit-identical on x86-64
# SSE and arm64. Transcendentals (sin, cos, pow) still differ between C
# libraries - code whose result is hashed or committed uses detmath.hpp.
if(NOT MSVC)
    add_compile_options(-ffp-contract=off)
endif()

# Compile the Windows resource script (app icon). Named GLFW_ICON so GLFW picks
# it up for the window/taskbar icon too - see resources/app.rc.
if(WIN32)
    enable_language(RC)
    set(TYRAX_EDITOR_RC resources/app.rc)
    # windres resolves the ICON path relative to its include dirs, not the .rc
    # file - point it at resources/ so it finds icon.ico.
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I${CMAKE_CURRENT_SOURCE_DIR}/resources")
endif()

# macOS: the merged title bar (platform.hpp) talks to NSWindow, which has no C
# API - src/platform_mac.mm is the editor's one Objective-C++ file.
if(APPLE)
    # OBJC as well: with only OBJCXX enabled, CMake compiles GLFW's own .m
    # files as Objective-C++ (and they do not build as C++).
    enable_language(OBJC OBJCXX)
    # gigpu_metal.mm: the GPU GI bake on Metal (macOS OpenGL has no compute
    # shaders) - the macOS half of gigpu.cpp. ARC for that file only.
    set(TYRAX_EDITOR_MAC_SOURCES src/platform_mac.mm src/gigpu_metal.mm)
    set_source_files_properties(src/gigpu_metal.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()

# --- GLFW ---
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/glfw)

# --- Dear ImGui (docking branch) ---
# Dear ImGui is tracked in the repo (patched - see vendor/imgui-tyrax/TYRAX-PATCHES.md),
# not fetched like the other vendor/ dependencies.
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/vendor/imgui-tyrax)
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

# --- Embedded documentation (docs/*.md -> generated header) ---
# The in-editor AI assistant answers from the editor's own docs (docs/ai-chat.md):
# it gets an index of every page in its system prompt and pulls one in with its
# read_doc tool. Baked into the exe because an installed editor has no repo next
# to it - and because docs/ is then the single source for humans and assistant
# alike, so the standing docs-in-the-same-commit rule keeps both current.
file(GLOB TYRAX_DOC_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/docs/*.md)
set(DOCS_GEN ${CMAKE_BINARY_DIR}/generated/docs_gen.hpp)
add_custom_command(
    OUTPUT ${DOCS_GEN}
    COMMAND ${CMAKE_COMMAND}
            -DSRC_DIR=${CMAKE_SOURCE_DIR}/docs
            -DOUT_FILE=${DOCS_GEN}
            -P ${CMAKE_SOURCE_DIR}/cmake/embed_docs.cmake
    DEPENDS ${TYRAX_DOC_SOURCES} ${CMAKE_SOURCE_DIR}/cmake/embed_docs.cmake
    COMMENT "Embedding docs/ pages")

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

# --- Embedded interface fonts (resources/fonts, Geist - SIL OFL 1.1) ---
# The editor's own typeface (docs/editor-theme.md, "The interface font"): Geist
# for the interface, Geist SemiBold for headings, Geist Mono for numbers and
# logs. Baked in like the icon, so every platform draws the same letters
# whatever the machine has installed; the system font chains stay as the
# fallback for glyphs Geist does not have.
#
# Geist Mono SemiBold is the GAME's default font (menubake::resolveFontPath):
# a "Default" font with no file used to be the OS's own (Consolas Bold, Arial
# Bold, DejaVu Sans Bold), so the same project baked a different atlas on
# every platform.
set(UI_FONT_GENS)
foreach(pair "Geist-Regular.ttf|kGeistRegular|geist_regular"
             "Geist-SemiBold.ttf|kGeistSemiBold|geist_semibold"
             "GeistMono-Regular.ttf|kGeistMonoRegular|geist_mono_regular"
             "GeistMono-SemiBold.ttf|kGeistMonoSemiBold|geist_mono_semibold")
    string(REPLACE "|" ";" parts "${pair}")
    list(GET parts 0 font_file)
    list(GET parts 1 font_symbol)
    list(GET parts 2 font_stem)
    set(font_gen ${CMAKE_BINARY_DIR}/generated/${font_stem}_gen.hpp)
    add_custom_command(
        OUTPUT ${font_gen}
        COMMAND ${CMAKE_COMMAND}
                -DSRC_FILE=${CMAKE_SOURCE_DIR}/resources/fonts/${font_file}
                -DOUT_FILE=${font_gen}
                -DNAMESPACE=uifonts -DSYMBOL=${font_symbol}
                -P ${CMAKE_SOURCE_DIR}/cmake/embed_binary.cmake
        DEPENDS ${CMAKE_SOURCE_DIR}/resources/fonts/${font_file}
                ${CMAKE_SOURCE_DIR}/cmake/embed_binary.cmake
        COMMENT "Embedding resources/fonts/${font_file}")
    list(APPEND UI_FONT_GENS ${font_gen})
endforeach()

# Own target for the generators. If the headers were listed straight in
# tyrax-editor's sources, CMake would attach their custom commands to that
# target and give them order-only deps on everything it LINKS - so the
# generators (0.1 s of work) waited for libimgui.a, and with them every editor
# .obj. That serialised ~17 s of imgui.cpp in front of the whole editor build.
# --- Embedded road streaming core (src/roadstream_core.inl) ---
# One source, two homes (docs/roads.md "Road streaming"): the editor compiles
# it for --vehicle-check, and the codegen pastes the same bytes into a
# generated game that streams its roads (roadstream::coreSource).
set(ROADSTREAM_CORE_GEN ${CMAKE_BINARY_DIR}/generated/roadstream_core_gen.hpp)
add_custom_command(
    OUTPUT ${ROADSTREAM_CORE_GEN}
    COMMAND ${CMAKE_COMMAND}
            -DSRC_FILE=${CMAKE_SOURCE_DIR}/src/roadstream_core.inl
            -DOUT_FILE=${ROADSTREAM_CORE_GEN}
            -DNAMESPACE=roadstreamsrc -DSYMBOL=kCore
            -P ${CMAKE_SOURCE_DIR}/cmake/embed_binary.cmake
    DEPENDS ${CMAKE_SOURCE_DIR}/src/roadstream_core.inl
            ${CMAKE_SOURCE_DIR}/cmake/embed_binary.cmake
    COMMENT "Embedding src/roadstream_core.inl")

# --- Embedded traffic core (src/traffic_core.inl) ---
# The road streaming core's arrangement (docs/traffic.md): the editor compiles
# it for --vehicle-check's host simulation, and the codegen pastes the same
# bytes into a generated game that runs traffic (roadlanes::coreSource).
set(TRAFFIC_CORE_GEN ${CMAKE_BINARY_DIR}/generated/traffic_core_gen.hpp)
add_custom_command(
    OUTPUT ${TRAFFIC_CORE_GEN}
    COMMAND ${CMAKE_COMMAND}
            -DSRC_FILE=${CMAKE_SOURCE_DIR}/src/traffic_core.inl
            -DOUT_FILE=${TRAFFIC_CORE_GEN}
            -DNAMESPACE=trafficsrc -DSYMBOL=kCore
            -P ${CMAKE_SOURCE_DIR}/cmake/embed_binary.cmake
    DEPENDS ${CMAKE_SOURCE_DIR}/src/traffic_core.inl
            ${CMAKE_SOURCE_DIR}/cmake/embed_binary.cmake
    COMMENT "Embedding src/traffic_core.inl")

# --- Embedded weather core (src/weather_core.inl) ---
# The same arrangement (docs/weather.md): the editor compiles it for the
# viewport preview and --vehicle-check, and the codegen pastes the same bytes
# into a generated game's daynight.gen.hpp (roadlight::weatherHeaderSource).
set(WEATHER_CORE_GEN ${CMAKE_BINARY_DIR}/generated/weather_core_gen.hpp)
add_custom_command(
    OUTPUT ${WEATHER_CORE_GEN}
    COMMAND ${CMAKE_COMMAND}
            -DSRC_FILE=${CMAKE_SOURCE_DIR}/src/weather_core.inl
            -DOUT_FILE=${WEATHER_CORE_GEN}
            -DNAMESPACE=weathersrc -DSYMBOL=kCore
            -P ${CMAKE_SOURCE_DIR}/cmake/embed_binary.cmake
    DEPENDS ${CMAKE_SOURCE_DIR}/src/weather_core.inl
            ${CMAKE_SOURCE_DIR}/cmake/embed_binary.cmake
    COMMENT "Embedding src/weather_core.inl")

add_custom_target(tyrax-generated
    DEPENDS ${AI_SUPPORT_GEN} ${DOCS_GEN} ${APP_ICON_GEN} ${MOON_MAP_GEN}
            ${UI_FONT_GENS} ${ROADSTREAM_CORE_GEN} ${TRAFFIC_CORE_GEN}
            ${WEATHER_CORE_GEN})

# --- Editor ---
add_executable(tyrax-editor
    src/main.cpp
    src/aichat.cpp
    src/aigen.cpp
    src/aisupport.cpp
    src/ambience.cpp
    src/ambience_simple_ui.cpp
    src/animedit.cpp
    src/animmerge.cpp
    src/aobake.cpp
    src/app.cpp
    src/audiopreview.cpp
    src/assetbrowser.cpp
    src/rail_ui.cpp
    src/blss_ui.cpp
    src/blobshadowbake.cpp
    src/camtake.cpp
    src/chat_ui.cpp
    src/credits_ui.cpp
    src/cutscene_ui.cpp
    src/devkit_ui.cpp
    src/hardware_timeline.cpp
    src/devsession.cpp
    src/facts.cpp
    src/facts_ui.cpp
    src/flowgraph_ui.cpp
    src/flownode.cpp
    src/hud_ui.cpp
    src/mateditor_ui.cpp
    src/prefab_ui.cpp
    src/particle_ui.cpp
    src/atlas_ui.cpp
    src/texfilter_ui.cpp
    src/batch_ui.cpp
    src/procui.cpp
    src/vu_ui.cpp
    src/props_ui.cpp
    src/junction_ui.cpp
    src/fbxparser.cpp
    src/gl_loader.cpp
    src/glbparser.cpp
    src/json.cpp
    src/elfsym.cpp
    src/livedbg.cpp
    src/livelogic.cpp
    src/livepad.cpp
    src/livereplay.cpp
    src/livetime.cpp
    src/logview.cpp
    src/vucap.cpp
    src/bvh.cpp
    src/gibake.cpp
    src/gigpu.cpp
    src/litbake.cpp
    src/vuir.cpp
    src/vuasm.cpp
    src/vusim.cpp
    src/vugen.cpp
    src/vushader.cpp
    src/vushader_sample.cpp
    src/vushader_snap_sample.cpp
    src/vushader_wobble_sample.cpp
    src/vushader_palette_sample.cpp
    src/vukernel_sample.cpp
    src/matbake.cpp
    src/modelao.cpp
    src/modelimport.cpp
    src/modelimport_ui.cpp
    src/objparser.cpp
    src/occlusionbake.cpp
    src/iso9660.cpp
    src/isoexport.cpp
    src/menubake.cpp
    src/menulayout.cpp
    src/menustyle.cpp
    src/menustyle_ui.cpp
    src/meshlod.cpp
    src/meshstrip.cpp
    src/migrations.cpp
    src/pcsx2_config.cpp
    src/phonecam.cpp
    src/placement.cpp
    src/platform.cpp
    src/pngquant.cpp
    src/prefab.cpp
    src/primmesh.cpp
    src/physhull.cpp
    src/particletex.cpp
    src/procgraph.cpp
    src/procgen.cpp
    src/procbake.cpp
    src/procrt.cpp
    src/reflscenery.cpp
    src/reflpano.cpp
    src/decalproj.cpp
    src/dronegen.cpp
    src/drone_simple_ui.cpp
    src/droneui.cpp
    src/navmesh.cpp
    src/project.cpp
    src/savebake.cpp
    src/scrollsim.cpp
    src/shadowbake.cpp
    src/skytex.cpp
    src/screenfx.cpp
    src/session.cpp
    src/starfield.cpp
    src/staticbatch.cpp
    src/staticbatchdisk.cpp
    src/stochtile.cpp
    src/texatlas.cpp
    src/texbake.cpp
    src/theme.cpp
    src/widgets.cpp
    src/shell_ui.cpp
    src/palette_ui.cpp
    src/menu_actions.cpp
    src/multiviewport.cpp
    src/toolkit.cpp
    src/pad_widget.cpp
    src/tmdl.cpp
    src/treegen.cpp
    src/treeimpostor.cpp
    src/impostorgpu.cpp
    src/modelimpostor.cpp
    src/modelproxy.cpp
    src/uiscript.cpp
    src/update.cpp
    src/update_ui.cpp
    src/uvunwrap.cpp
    src/vehbake.cpp
    src/vehicle_ui.cpp
    src/roadbridge.cpp
    src/roadgen.cpp
    src/roadrail.cpp
    src/roaddetail.cpp
    src/roaddraw.cpp
    src/roaddraw_ui.cpp
    src/roadfurniture.cpp
    src/roadfurnbreak.cpp
    src/roadfurniture_ui.cpp
    src/roadlight.cpp
    src/roadpresets.cpp
    src/roadstream.cpp
    src/roadlanes.cpp
    src/roadlanes_check.cpp
    src/roadlanes_gen.cpp
    src/traffic_ui.cpp
    src/roadfile.cpp
    src/roadtex.cpp
    src/roadtex_ui.cpp
    src/vehcheck.cpp
    src/vehiclesim.cpp
    src/save_assets.cpp
    src/templates.cpp
    src/viewport.cpp
    src/wavconvert.cpp
    src/wire.cpp
    src/runner.cpp
    src/sessionlog.cpp
    vendor/imguizmo/src/ImGuizmo.cpp
    vendor/imnodes/imnodes.cpp
    vendor/ufbx/ufbx.c

    ${TYRAX_EDITOR_RC}
    ${TYRAX_EDITOR_MAC_SOURCES}
)
target_include_directories(tyrax-editor PRIVATE src vendor/imguizmo/src vendor/imnodes vendor/stb vendor/ufbx vendor/miniaudio ${CMAKE_BINARY_DIR}/generated)
target_compile_definitions(tyrax-editor PRIVATE GLFW_INCLUDE_NONE)
add_dependencies(tyrax-editor tyrax-generated)

find_package(OpenGL REQUIRED)
target_link_libraries(tyrax-editor PRIVATE imgui glfw OpenGL::GL)

# Explicit GUI gate: requires a display (xvfb-run on headless Linux). Release
# packaging runs this after compilation; regular headless builds remain usable.
add_custom_target(check-ui
    COMMAND ${CMAKE_COMMAND}
        "-DEDITOR=$<TARGET_FILE:tyrax-editor>"
        "-DCHECK_DIR=${CMAKE_BINARY_DIR}/ui-check"
        -P "${CMAKE_SOURCE_DIR}/cmake/check_ui.cmake"
    DEPENDS tyrax-editor
    USES_TERMINAL
    VERBATIM
    COMMENT "Checking UI widget IDs and Assets actions")

if(WIN32)
    # shell32/ole32/uuid: the native file pickers and "Reveal in Explorer"
    # (src/platform.cpp). ws2_32: the socket transport (src/wire.cpp).
    # dwmapi: the merged title bar's DwmExtendFrameIntoClientArea (platform.cpp).
    target_link_libraries(tyrax-editor PRIVATE shell32 ole32 uuid ws2_32 dwmapi)
else()
    # Threads: std::thread needs an explicit -pthread with libstdc++, and
    # ${CMAKE_DL_LIBS} is what GLFW's runtime GL/Vulkan loading wants.
    find_package(Threads REQUIRED)
    target_link_libraries(tyrax-editor PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
    if(APPLE)
        # Cocoa: NSWindow / NSEvent in src/platform_mac.mm (the merged title bar).
        target_link_libraries(tyrax-editor PRIVATE "-framework Cocoa")
        # Metal: the GPU GI bake (src/gigpu_metal.mm).
        target_link_libraries(tyrax-editor PRIVATE "-framework Metal" "-framework Foundation")
    endif()
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
