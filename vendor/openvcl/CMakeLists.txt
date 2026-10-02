cmake_minimum_required(VERSION 3.13)
project(openvcl VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default install prefix to $PS2DEV when available (mirrors old Makefile behaviour).
if(DEFINED ENV{PS2DEV} AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "$ENV{PS2DEV}" CACHE PATH
        "Installation prefix (defaults to \$PS2DEV)" FORCE)
endif()

# Generate the version header from the project() version field.
configure_file(src/OpenVclVersion.h.in ${CMAKE_CURRENT_BINARY_DIR}/OpenVclVersion.h @ONLY)

# All translation units that form the openvcl binary.
file(GLOB OPENVCL_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_executable(openvcl ${OPENVCL_SOURCES})

# Expose the generated header to all source files.
target_include_directories(openvcl PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_compile_options(openvcl PRIVATE
    -Wall
    -Werror
    -Wno-unused-but-set-variable
    -Wno-deprecated-declarations
    -pedantic
)

include(GNUInstallDirs)
install(TARGETS openvcl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Unit and integration tests (cmake -B build; ctest --test-dir build).
# Disable by passing -DBUILD_TESTING=OFF.
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(test)
endif()

# PS2 examples – require PS2SDK environment variable and the PS2 toolchain.
# Enabled automatically when $PS2SDK is set; override with -DBUILD_EXAMPLES=ON/OFF.
if(DEFINED ENV{PS2SDK})
    option(BUILD_EXAMPLES "Build PS2 examples (requires PS2SDK toolchain)" ON)
else()
    option(BUILD_EXAMPLES "Build PS2 examples (requires PS2SDK toolchain)" OFF)
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
