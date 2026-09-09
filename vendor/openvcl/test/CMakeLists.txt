# openvcl unit tests
#
# Self-contained CMake build that lives alongside the existing Makefile build.
# Builds a single test binary that links openvcl's source files (minus main.cpp)
# against a small in-tree test harness (test/include/test_harness.h).
#
# Usage:
#   cmake -B test/build -S test
#   cmake --build test/build
#   ctest --test-dir test/build --output-on-failure
#
# Or from inside test/build:
#   make check          (builds + runs all tests with verbose failure output)

cmake_minimum_required(VERSION 3.13)
project(openvcl_tests CXX)

# Tests can use C++11+; the library sources are written in plain C++98-ish
# style and compile cleanly under C++11 too.
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

# Repo layout: test/CMakeLists.txt lives one level below the source root.
set(OPENVCL_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# All openvcl translation units that should be linked into the test binary.
# main.cpp is excluded because the test runner provides its own main().
set(OPENVCL_LIB_SOURCES
    ${OPENVCL_ROOT}/src/Alias.cpp
    ${OPENVCL_ROOT}/src/BranchState.cpp
    ${OPENVCL_ROOT}/src/CodeGenerator.cpp
    ${OPENVCL_ROOT}/src/CommandLine.cpp
    ${OPENVCL_ROOT}/src/Dependency.cpp
    ${OPENVCL_ROOT}/src/Error.cpp
    ${OPENVCL_ROOT}/src/Expression.cpp
    ${OPENVCL_ROOT}/src/Math.cpp
    ${OPENVCL_ROOT}/src/Parser.cpp
    ${OPENVCL_ROOT}/src/RegisterAllocator.cpp
    ${OPENVCL_ROOT}/src/Token.cpp
    ${OPENVCL_ROOT}/src/Tokenizer.cpp
    ${OPENVCL_ROOT}/src/VuLatencyTracker.cpp
    ${OPENVCL_ROOT}/src/VuModuloReservationTable.cpp
    ${OPENVCL_ROOT}/src/VuKernelLayout.cpp
    ${OPENVCL_ROOT}/src/VuSchedulerAnalysis.cpp
    ${OPENVCL_ROOT}/src/VuSchedulingRules.cpp
    ${OPENVCL_ROOT}/src/VuInstructionInfo.cpp
    ${OPENVCL_ROOT}/src/VuTokenResourceAccess.cpp
    ${OPENVCL_ROOT}/src/VsmCostAnalyzer.cpp
    ${OPENVCL_ROOT}/src/stlhelp.cpp
)

# Test translation units.
set(OPENVCL_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/test_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_smoke.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_alias.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_expression.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_tokenizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_vu_modulo_reservation_table.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_vu_kernel_layout.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_vu_scheduler_analysis.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_vu_scheduling_rules.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_vu_instruction_info.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_codegen_instruction_literals.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_vu_token_resource_access.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_vsm_cost_analyzer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/integration/openvcl_runner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_cli.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_init_vf_range.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_clip_validation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_loi_hex.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_parser_families.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_instruction_info_dump.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_pairing.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_literal_register_conflict.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_flag_latency.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_continuation_lifetime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_two_address_coalescing.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_vsm_cost.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_software_pipeline.cpp
)

add_executable(openvcl_unit_tests
    ${OPENVCL_TEST_SOURCES}
    ${OPENVCL_LIB_SOURCES}
)

# The integration tests fork the binary built by the parent CMake project. The
# generator expression keeps out-of-source builds self-contained instead of
# writing an untracked executable into the vendored source tree.
if(TARGET openvcl)
    set(OPENVCL_TEST_BINARY "$<TARGET_FILE:openvcl>")
    add_dependencies(openvcl_unit_tests openvcl)
else()
    set(OPENVCL_TEST_BINARY "${OPENVCL_ROOT}/openvcl")
endif()

target_compile_definitions(openvcl_unit_tests PRIVATE
    OPENVCL_BIN="${OPENVCL_TEST_BINARY}"
    OPENVCL_SOURCE_ROOT="${OPENVCL_ROOT}"
    OPENVCL_TEST_VSM_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/vsm_cost"
)

# Deliberately NOT adding ${OPENVCL_ROOT}/src to the include path: macOS's
# default case-insensitive filesystem would let libc++'s <cmath> resolve
# <math.h> to openvcl's src/Math.h, which trips a wall of "using_if_exists"
# errors against ::isnan / ::ceil / ::nextafter and friends.  Source files
# themselves don't need -Isrc because their own #include "Foo.h" directives
# resolve via the source file's own directory.  Test files reach openvcl
# headers via explicit relative paths like #include "../src/Expression.h".
# OpenVclVersion.h is generated by configure_file; reproduce it here so both
# standalone (cmake -S test) and integrated (add_subdirectory) builds work.
# CMAKE_PROJECT_VERSION is the top-level project version when built from root,
# and empty when built standalone (acceptable for test purposes).
set(PROJECT_VERSION "${CMAKE_PROJECT_VERSION}")
configure_file(
    "${OPENVCL_ROOT}/src/OpenVclVersion.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/OpenVclVersion.h"
    @ONLY
)

target_include_directories(openvcl_unit_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}
)

# The library code was written for an older C++ dialect; silence the warnings
# that aren't worth chasing in a stable, vendored codebase.  The test files
# themselves are still expected to be warning-clean under modern flags.
target_compile_options(openvcl_unit_tests PRIVATE
    -Wno-unused-but-set-variable
    -Wno-deprecated-declarations
)

add_test(NAME openvcl_unit COMMAND openvcl_unit_tests)

# Convenience target: build and run everything with verbose failure output.
add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} -C $<CONFIG> --output-on-failure
    DEPENDS openvcl_unit_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
