cmake_minimum_required(VERSION 3.20)
project(tyra-editor CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
add_executable(tyra-editor
    src/main.cpp
    src/app.cpp
    src/gl_loader.cpp
    src/json.cpp
    src/project.cpp
    src/templates.cpp
    src/viewport.cpp
    src/runner.cpp
)
target_include_directories(tyra-editor PRIVATE src)
target_compile_definitions(tyra-editor PRIVATE GLFW_INCLUDE_NONE)
target_link_libraries(tyra-editor PRIVATE imgui glfw opengl32 ole32 uuid)

if(MINGW)
    # Self-contained exe: don't depend on libstdc++/libgcc/libwinpthread DLLs
    # (a mismatched libwinpthread-1.dll on PATH breaks startup otherwise)
    target_link_options(tyra-editor PRIVATE -static)
endif()

if(WIN32)
    # Console subsystem for now - useful to see logs during development
    set_target_properties(tyra-editor PROPERTIES WIN32_EXECUTABLE OFF)
endif()
