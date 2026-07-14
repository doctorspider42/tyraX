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

# --- Editor ---
add_executable(tyrax-editor
    src/main.cpp
    src/app.cpp
    src/camtake.cpp
    src/flownode.cpp
    src/gl_loader.cpp
    src/glbparser.cpp
    src/json.cpp
    src/objparser.cpp
    src/iso9660.cpp
    src/isoexport.cpp
    src/menubake.cpp
    src/pcsx2_config.cpp
    src/pngquant.cpp
    src/project.cpp
    src/screenfx.cpp
    src/texbake.cpp
    src/save_assets.cpp
    src/templates.cpp
    src/viewport.cpp
    src/wavconvert.cpp
    src/runner.cpp
    vendor/imguizmo/src/ImGuizmo.cpp
    vendor/imnodes/imnodes.cpp
    ${TYRAX_EDITOR_RC}
)
target_include_directories(tyrax-editor PRIVATE src vendor/imguizmo/src vendor/imnodes vendor/stb ${CMAKE_BINARY_DIR}/generated)
target_compile_definitions(tyrax-editor PRIVATE GLFW_INCLUDE_NONE)
target_link_libraries(tyrax-editor PRIVATE imgui glfw opengl32 shell32 ole32 uuid)

if(MINGW)
    # Self-contained exe: don't depend on libstdc++/libgcc/libwinpthread DLLs
    # (a mismatched libwinpthread-1.dll on PATH breaks startup otherwise)
    target_link_options(tyrax-editor PRIVATE -static)
endif()

if(WIN32)
    # Console subsystem for now - useful to see logs during development
    set_target_properties(tyrax-editor PROPERTIES WIN32_EXECUTABLE OFF)
endif()
