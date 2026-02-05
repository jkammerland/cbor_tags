cmake_minimum_required(VERSION 3.20)
project(cbor_tags VERSION 0.10.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add option for debug mode
option(CBOR_TAGS_DEBUG "Enable debug mode" OFF)

# Configure a header file to pass the CMake setting to the source code
configure_file("${PROJECT_SOURCE_DIR}/cbor_tags_config.h.in" "${PROJECT_BINARY_DIR}/include/cbor_tags/cbor_tags_config.h")

# Check minimum compiler version
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/minimum_compiler.cmake)

set(CPM_SOURCE_CACHE ${CMAKE_BINARY_DIR}/cpm_cache)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/get_cpm.cmake)

option(CBOR_TAGS_USE_DEV_SETTINGS "Use dev settings" OFF)
if(CBOR_TAGS_USE_DEV_SETTINGS)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ccache.cmake)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/use_mold_linker.cmake)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/common_settings.cmake)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/print_compiler_and_flags.cmake)
endif()

# THE LIBRARY
file(GLOB_RECURSE CBOR_HEADERS include/*.h include/*.hpp)
add_library(${PROJECT_NAME} INTERFACE)
target_sources(
  ${PROJECT_NAME}
  INTERFACE FILE_SET
            HEADERS
            BASE_DIRS
            "include"
            "${PROJECT_BINARY_DIR}/include/"
            FILES
            "${CBOR_HEADERS}"
            "${PROJECT_BINARY_DIR}/include/cbor_tags/cbor_tags_config.h")
add_library(cbor::tags ALIAS ${PROJECT_NAME})

# Add tl::expected - the only dependency if not using compiler supporting c++23 std::expected
option(CBOR_TAGS_USE_SYSTEM_EXPECTED "Use system installed tl::expected" OFF)
if(NOT CBOR_TAGS_USE_SYSTEM_EXPECTED)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/get_cpm.cmake)
  set(CPM_SOURCE_CACHE ${CMAKE_BINARY_DIR}/cpm_cache)
  cpmaddpackage(
    NAME
    expected
    GITHUB_REPOSITORY
    TartanLlama/expected
    VERSION
    1.2.0
    GIT_TAG
    v1.2.0
    OPTIONS
    "EXPECTED_BUILD_TESTS OFF"
    "EXPECTED_BUILD_PACKAGE OFF")
else()
  find_package(tl-expected REQUIRED)
endif()

# Define public dependencies (if any)
set(CBOR_TAGS_PUBLIC_DEPENDENCIES "")
# If you had dependencies, you would add them like this: list(APPEND CBOR_TAGS_PUBLIC_DEPENDENCIES "find_dependency(SomePackage)")
if(CBOR_TAGS_USE_SYSTEM_EXPECTED)
  list(APPEND CBOR_TAGS_PUBLIC_DEPENDENCIES "find_dependency(tl-expected)")
endif()

# Link dependencies
target_link_libraries(${PROJECT_NAME} INTERFACE $<IF:$<BOOL:${CBOR_TAGS_USE_SYSTEM_EXPECTED}>,tl::expected,expected>)

# Make ${PROJECT_NAME} depend on the generated header add tools - reflection module generator - makes cbor_reflection_impl.h
option(CBOR_TAGS_BUILD_TOOLS "Build tools (for to_tuple(...) generation, it is required for struct reflection)" OFF)
if(CBOR_TAGS_BUILD_TOOLS)
  add_subdirectory(tools)
  message(DEBUG "Setting ${PROJECT_NAME} to depend on generate_reflection_module, which generates cbor_reflection_impl.h")
  add_dependencies(${PROJECT_NAME} generate_reflection_module)
else()
  message(STATUS "Skipping generator tools - cbor_reflection_impl.h must exist.")
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include/cbor_tags/cbor_reflection_impl.h")
    message(FATAL_ERROR "cbor_reflection_impl.h not found. Enable CBOR_TAGS_BUILD_TOOLS to generate it.")
  endif()
endif()

# Extra options
option(CBOR_TAGS_BUILD_TESTS "Build tests" OFF)
option(CBOR_TAGS_BUILD_BENCHMARKS "Build benchmarks" OFF)
if(CBOR_TAGS_BUILD_TESTS OR CBOR_TAGS_BUILD_BENCHMARKS)
  include(CTest)
  enable_testing()
  add_library(test_util INTERFACE)
  target_include_directories(test_util INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/test_util)
endif()

option(CBOR_TAGS_TIDY_TARGET "Enable clang-tidy target" OFF)
if(CBOR_TAGS_TIDY_TARGET)
  cpmaddpackage("gh:jkammerland/clang-tidy.cmake@1.2.0")
endif()

# add tests
if(CBOR_TAGS_BUILD_TESTS)
  add_subdirectory(test)
endif()

# add benchmarks
if(CBOR_TAGS_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

option(CBOR_TAGS_INSTALL "Install the library" OFF)
if(CBOR_TAGS_INSTALL)
  cpmaddpackage(gh:jkammerland/target_install_package.cmake@6.0.1)
  target_install_package(${PROJECT_NAME} NAMESPACE cbor:: ALIAS_NAME tags)
endif()

# CMake Presets are available for this project
# Run the following commands to see available presets:
message(STATUS "CMake Presets available:")
message(STATUS "  List all presets: cmake --list-presets=all")
message(STATUS "  Configure: cmake --preset=<preset-name>")
message(STATUS "  Build: cmake --build --preset=<preset-name>")
message(STATUS "  Test: ctest --preset=<preset-name>")
message(STATUS "  Workflow: cmake --workflow --preset=<workflow-name>")
message(STATUS "")
message(STATUS "Standard build types: debug, release, relwithdebinfo, minsizerel")
message(STATUS "Analysis presets: coverage, coverage-release, tidy, tidy-fix")
message(STATUS "Profiling presets: profile, profile-release, profile-gprof, pgo-generate, pgo-use")
message(STATUS "Diagnostic presets (platform-dependent): alsan, alusan, msan, cfihwasan, fullsan")
message(STATUS "Memory-check presets (Linux): valgrind")

# Valgrind MemCheck integration (Linux)
#
# The `valgrind` target runs: `ctest -T MemCheck --output-on-failure`.
# Configure with the `valgrind` preset to set `MEMORYCHECK_COMMAND_OPTIONS`.
find_program(CGEN_VALGRIND_COMMAND valgrind)
if(CGEN_VALGRIND_COMMAND)
  set(CGEN_VALGRIND_LABEL_EXCLUDE_REGEX
      "cmake|package|codegen"
      CACHE STRING "CTest label exclude regex for `valgrind` target (ctest -LE <regex>). Set empty to include everything.")

  set(_cgen_ctest_command "${CMAKE_CTEST_COMMAND}")
  if(NOT _cgen_ctest_command)
    find_program(_cgen_ctest_command ctest)
  endif()

  if(_cgen_ctest_command)
    set(_cgen_valgrind_cmd ${_cgen_ctest_command} -T MemCheck --output-on-failure)
    if(NOT CGEN_VALGRIND_LABEL_EXCLUDE_REGEX STREQUAL "")
      string(REPLACE "|" "\\|" _cgen_valgrind_label_exclude_shell "${CGEN_VALGRIND_LABEL_EXCLUDE_REGEX}")
      list(APPEND _cgen_valgrind_cmd -LE "${_cgen_valgrind_label_exclude_shell}")
    endif()

    add_custom_target(valgrind
      COMMAND ${_cgen_valgrind_cmd}
      WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
      USES_TERMINAL
    )
    unset(_cgen_valgrind_cmd)
    unset(_cgen_valgrind_label_exclude_shell)
  endif()
  unset(_cgen_ctest_command)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/print_options.cmake)
print_options(DEBUG "^CBOR_TAGS")

# TODO: this is for testing docs
# ~~~
# set(BaseName
#     "binary"
#     CACHE STRING "BaseName chosen by the user at CMake configure time")
# set_property(CACHE BaseName PROPERTY STRINGS binary octal decimal hexadecimal)
# add_subdirectory(doc/test_readme) # TODO:
# ~~~
