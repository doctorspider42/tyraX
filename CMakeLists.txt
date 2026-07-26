cmake_minimum_required(VERSION 3.20)
project(tyrax-editor CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
            -P ${CMAKE_SOURCE_DIR}/cmake/embed_icon.cmake
    DEPENDS ${CMAKE_SOURCE_DIR}/resources/icon.png
            ${CMAKE_SOURCE_DIR}/cmake/embed_icon.cmake
    COMMENT "Embedding resources/icon.png")

# --- Editor ---
add_executable(tyrax-editor
    ${AI_SUPPORT_GEN}
    ${APP_ICON_GEN}
    src/main.cpp
    src/aigen.cpp
    src/aisupport.cpp
    src/animedit.cpp
    src/aobake.cpp
    src/app.cpp
    src/audiopreview.cpp
    src/assetbrowser.cpp
    src/camtake.cpp
    src/flownode.cpp
    src/fbxparser.cpp
    src/gl_loader.cpp
    src/glbparser.cpp
    src/json.cpp
    src/matbake.cpp
    src/objparser.cpp
    src/iso9660.cpp
    src/isoexport.cpp
    src/menubake.cpp
    src/meshlod.cpp
    src/pcsx2_config.cpp
    src/phonecam.cpp
    src/placement.cpp
    src/platform.cpp
    src/pngquant.cpp
    src/primmesh.cpp
    src/decalproj.cpp
    src/dronegen.cpp
    src/droneui.cpp
    src/navmesh.cpp
    src/project.cpp
    src/screenfx.cpp
    src/session.cpp
    src/stochtile.cpp
    src/texatlas.cpp
    src/texbake.cpp
    src/tmdl.cpp
    src/treegen.cpp
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
