cmake_minimum_required(VERSION 3.20)
project(cbor_tags VERSION 0.16.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add option for debug mode
option(CBOR_TAGS_DEBUG "Enable debug mode" OFF)
option(CBOR_TAGS_USE_STD_REFLECTION "Build cbor_tags tests with GCC C++26 std::meta reflection" OFF)
option(CBOR_TAGS_USE_BOOST_PFR_NAMES "Enable C++20 named-map reflection via Boost.PFR field names" OFF)
option(CBOR_TAGS_USE_MAGIC_ENUM_NAMES "Enable CDDL enum-name definitions via magic_enum" OFF)
option(CBOR_TAGS_USE_STD_EXPECTED "Use C++23 std::expected for public return values instead of tl::expected" OFF)

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
file(GLOB_RECURSE CBOR_HEADERS CONFIGURE_DEPENDS include/*.h include/*.hpp)
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

if(CBOR_TAGS_USE_STD_REFLECTION)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    include(CheckCXXSourceCompiles)
    set(_CBOR_TAGS_CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS}")
    set(_CBOR_TAGS_CMAKE_CXX_STANDARD "${CMAKE_CXX_STANDARD}")
    unset(CMAKE_CXX_STANDARD)
    set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -std=gnu++26 -freflection")
    check_cxx_source_compiles(
      [=[
#include <meta>
#include <string_view>
#ifndef __cpp_impl_reflection
#error "__cpp_impl_reflection is not defined"
#endif
struct S { int a; };
enum class Color : unsigned char { red = 1, green = 2 };
static_assert(__cpp_impl_reflection >= 202506L);
static_assert(std::meta::nonstatic_data_members_of(^^S, std::meta::access_context::current()).size() == 1);
static_assert(std::meta::enumerators_of(^^Color).size() == 2);
static_assert(std::meta::identifier_of(std::meta::enumerators_of(^^Color)[0]) == std::string_view{"red"});
static_assert(std::meta::extract<Color>(std::meta::enumerators_of(^^Color)[1]) == Color::green);
int main() { return 0; }
]=]
      CBOR_TAGS_COMPILER_HAS_STD_REFLECTION)
    set(CMAKE_REQUIRED_FLAGS "${_CBOR_TAGS_CMAKE_REQUIRED_FLAGS}")
    set(CMAKE_CXX_STANDARD "${_CBOR_TAGS_CMAKE_CXX_STANDARD}")
    unset(_CBOR_TAGS_CMAKE_REQUIRED_FLAGS)
    unset(_CBOR_TAGS_CMAKE_CXX_STANDARD)

    if(NOT CBOR_TAGS_COMPILER_HAS_STD_REFLECTION)
      message(FATAL_ERROR "CBOR_TAGS_USE_STD_REFLECTION requires GCC with C++26 reflection support (-std=gnu++26 -freflection).")
    endif()
    target_compile_options(${PROJECT_NAME} INTERFACE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++26> $<$<COMPILE_LANGUAGE:CXX>:-freflection>)
  else()
    message(FATAL_ERROR "CBOR_TAGS_USE_STD_REFLECTION currently supports GCC's C++26 reflection implementation only.")
  endif()
endif()

if(CBOR_TAGS_USE_BOOST_PFR_NAMES)
  find_package(Boost 1.84 REQUIRED CONFIG)
  if(NOT TARGET Boost::headers)
    message(FATAL_ERROR "CBOR_TAGS_USE_BOOST_PFR_NAMES requires a Boost CMake config package that exports Boost::headers.")
  endif()

  include(CheckCXXSourceCompiles)
  set(_CBOR_TAGS_CMAKE_REQUIRED_INCLUDES "${CMAKE_REQUIRED_INCLUDES}")
  set(_CBOR_TAGS_BOOST_INCLUDE_DIRS ${Boost_INCLUDE_DIRS})
  get_target_property(_CBOR_TAGS_BOOST_HEADERS_INCLUDE_DIRS Boost::headers INTERFACE_INCLUDE_DIRECTORIES)
  if(_CBOR_TAGS_BOOST_HEADERS_INCLUDE_DIRS)
    list(APPEND _CBOR_TAGS_BOOST_INCLUDE_DIRS ${_CBOR_TAGS_BOOST_HEADERS_INCLUDE_DIRS})
  endif()
  list(REMOVE_DUPLICATES _CBOR_TAGS_BOOST_INCLUDE_DIRS)
  set(CMAKE_REQUIRED_INCLUDES ${CMAKE_REQUIRED_INCLUDES} ${_CBOR_TAGS_BOOST_INCLUDE_DIRS})
  check_cxx_source_compiles(
    [=[
#include <boost/pfr/core.hpp>
#include <boost/pfr/core_name.hpp>
#include <string_view>
#ifndef BOOST_PFR_CORE_NAME_ENABLED
#error "BOOST_PFR_CORE_NAME_ENABLED is not defined"
#endif
#if !BOOST_PFR_CORE_NAME_ENABLED
#error "BOOST_PFR_CORE_NAME_ENABLED is disabled"
#endif
struct S { int field_name; };
static_assert(boost::pfr::tuple_size_v<S> == 1);
static_assert(boost::pfr::get_name<0, S>() == std::string_view{"field_name"});
int main() { return 0; }
]=]
    CBOR_TAGS_COMPILER_HAS_BOOST_PFR_NAMES)
  set(CMAKE_REQUIRED_INCLUDES "${_CBOR_TAGS_CMAKE_REQUIRED_INCLUDES}")
  unset(_CBOR_TAGS_CMAKE_REQUIRED_INCLUDES)
  unset(_CBOR_TAGS_BOOST_INCLUDE_DIRS)
  unset(_CBOR_TAGS_BOOST_HEADERS_INCLUDE_DIRS)

  if(NOT CBOR_TAGS_COMPILER_HAS_BOOST_PFR_NAMES)
    message(FATAL_ERROR "CBOR_TAGS_USE_BOOST_PFR_NAMES requires Boost.PFR field names "
                        "(<boost/pfr/core_name.hpp>, Boost 1.84 or newer, BOOST_PFR_CORE_NAME_ENABLED).")
  endif()

  target_compile_definitions(${PROJECT_NAME} INTERFACE CBOR_TAGS_USE_BOOST_PFR_NAMES=1)
  target_link_libraries(${PROJECT_NAME} INTERFACE Boost::headers)
endif()

if(CBOR_TAGS_USE_MAGIC_ENUM_NAMES)
  find_package(magic_enum 0.9.8 REQUIRED CONFIG)
  if(NOT TARGET magic_enum::magic_enum)
    message(FATAL_ERROR "CBOR_TAGS_USE_MAGIC_ENUM_NAMES requires a magic_enum CMake config that exports magic_enum::magic_enum.")
  endif()

  target_compile_definitions(${PROJECT_NAME} INTERFACE CBOR_TAGS_USE_MAGIC_ENUM_NAMES=1)
  target_link_libraries(${PROJECT_NAME} INTERFACE magic_enum::magic_enum)
endif()

# Build options
option(CBOR_TAGS_BUILD_TESTS "Build tests" OFF)
option(CBOR_TAGS_BUILD_CLI "Build cbor_tags_cli" ${CBOR_TAGS_BUILD_TESTS})
option(CBOR_TAGS_BUILD_TOOLS "Build tools (for to_tuple(...) generation, it is required for struct reflection)" OFF)
option(CBOR_TAGS_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(CBOR_TAGS_INSTALL "Install the library" OFF)
if(CBOR_TAGS_INSTALL AND CMAKE_VERSION VERSION_LESS 3.25)
  message(FATAL_ERROR "CBOR_TAGS_INSTALL requires CMake 3.25 or newer because target_install_package.cmake v7 requires it.")
endif()

# Expected return backend
option(CBOR_TAGS_USE_SYSTEM_EXPECTED "Use system installed tl::expected" OFF)
if(CBOR_TAGS_USE_STD_EXPECTED)
  if(CMAKE_CXX_STANDARD LESS 23)
    message(FATAL_ERROR "CBOR_TAGS_USE_STD_EXPECTED requires CMAKE_CXX_STANDARD=23 or newer.")
  endif()

  include(CheckCXXSourceCompiles)
  check_cxx_source_compiles(
    [=[
#include <expected>
#include <version>
#ifndef __cpp_lib_expected
#error "__cpp_lib_expected is not defined"
#endif
#if __cpp_lib_expected < 202202L
#error "__cpp_lib_expected is too old"
#endif
int main() {
  std::expected<void, int> value{};
  return value.has_value() ? 0 : 1;
}
]=]
    CBOR_TAGS_COMPILER_HAS_STD_EXPECTED)
  if(NOT CBOR_TAGS_COMPILER_HAS_STD_EXPECTED)
    message(FATAL_ERROR "CBOR_TAGS_USE_STD_EXPECTED requires C++23 <expected> with __cpp_lib_expected >= 202202L.")
  endif()
  target_compile_definitions(${PROJECT_NAME} INTERFACE CBOR_TAGS_USE_STD_EXPECTED=1)
  target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_23)
elseif(CBOR_TAGS_INSTALL AND NOT CBOR_TAGS_USE_SYSTEM_EXPECTED)
  message(FATAL_ERROR "CBOR_TAGS_INSTALL requires -DCBOR_TAGS_USE_SYSTEM_EXPECTED=ON or -DCBOR_TAGS_USE_STD_EXPECTED=ON")
endif()

if(NOT CBOR_TAGS_USE_STD_EXPECTED AND NOT CBOR_TAGS_USE_SYSTEM_EXPECTED)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/get_cpm.cmake)
  set(CPM_SOURCE_CACHE ${CMAKE_BINARY_DIR}/cpm_cache)
  cpmaddpackage(
    NAME
    expected
    GITHUB_REPOSITORY
    TartanLlama/expected
    VERSION
    1.3.1
    GIT_TAG
    v1.3.1
    OPTIONS
    "EXPECTED_BUILD_TESTS OFF"
    "EXPECTED_BUILD_PACKAGE OFF")
elseif(NOT CBOR_TAGS_USE_STD_EXPECTED)
  find_package(tl-expected REQUIRED)
endif()

if(CBOR_TAGS_INSTALL)
  find_package(fmt REQUIRED CONFIG)
else()
  find_package(fmt CONFIG QUIET)
  if(NOT TARGET fmt::fmt)
    cpmaddpackage("gh:fmtlib/fmt#11.1.2")
  endif()
endif()
if(NOT TARGET fmt::fmt)
  message(FATAL_ERROR "cbor_tags requires a fmt CMake config that exports fmt::fmt.")
endif()
if(DEFINED fmt_VERSION AND fmt_VERSION VERSION_LESS 11.0.2)
  message(FATAL_ERROR "cbor_tags requires fmt 11.0.2 or newer.")
endif()

if(CBOR_TAGS_INSTALL)
  find_package(nameof REQUIRED CONFIG)
else()
  find_package(nameof CONFIG QUIET)
  if(NOT TARGET nameof::nameof)
    cpmaddpackage("gh:Neargye/nameof@0.10.4")
  endif()
endif()
if(NOT TARGET nameof::nameof)
  message(FATAL_ERROR "cbor_tags requires a nameof CMake config that exports nameof::nameof.")
endif()
if(DEFINED nameof_VERSION AND nameof_VERSION VERSION_LESS 0.10.4)
  message(FATAL_ERROR "cbor_tags requires nameof 0.10.4 or newer.")
endif()

# Define public dependencies (if any)
set(CBOR_TAGS_PUBLIC_DEPENDENCIES "")
# If you had dependencies, you would add them like this: list(APPEND CBOR_TAGS_PUBLIC_DEPENDENCIES "find_dependency(SomePackage)")
if(CBOR_TAGS_USE_SYSTEM_EXPECTED AND NOT CBOR_TAGS_USE_STD_EXPECTED)
  list(APPEND CBOR_TAGS_PUBLIC_DEPENDENCIES "tl-expected REQUIRED")
endif()
list(APPEND CBOR_TAGS_PUBLIC_DEPENDENCIES "fmt REQUIRED CONFIG")
list(APPEND CBOR_TAGS_PUBLIC_DEPENDENCIES "nameof REQUIRED CONFIG")
if(CBOR_TAGS_USE_BOOST_PFR_NAMES)
  list(APPEND CBOR_TAGS_PUBLIC_DEPENDENCIES "Boost 1.84 REQUIRED CONFIG")
endif()
if(CBOR_TAGS_USE_MAGIC_ENUM_NAMES)
  list(APPEND CBOR_TAGS_PUBLIC_DEPENDENCIES "magic_enum 0.9.8 REQUIRED CONFIG")
endif()

# Link dependencies
if(CBOR_TAGS_USE_STD_EXPECTED)
  target_link_libraries(${PROJECT_NAME} INTERFACE fmt::fmt nameof::nameof)
elseif(CBOR_TAGS_USE_SYSTEM_EXPECTED)
  target_link_libraries(${PROJECT_NAME} INTERFACE tl::expected fmt::fmt nameof::nameof)
else()
  target_link_libraries(${PROJECT_NAME} INTERFACE expected fmt::fmt nameof::nameof)
endif()

# Make ${PROJECT_NAME} depend on the generated header add tools - reflection module generator - makes cbor_reflection_impl.h
if(CBOR_TAGS_BUILD_CLI OR CBOR_TAGS_BUILD_TOOLS)
  add_subdirectory(tools)
endif()

if(CBOR_TAGS_BUILD_TOOLS)
  message(DEBUG "Setting ${PROJECT_NAME} to depend on generate_reflection_module, which generates cbor_reflection_impl.h")
  add_dependencies(${PROJECT_NAME} generate_reflection_module)
else()
  message(STATUS "Skipping generator tools - cbor_reflection_impl.h must exist.")
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include/cbor_tags/cbor_reflection_impl.h")
    message(FATAL_ERROR "cbor_reflection_impl.h not found. Enable CBOR_TAGS_BUILD_TOOLS to generate it.")
  endif()
endif()

# Extra options
if(CBOR_TAGS_BUILD_TESTS OR CBOR_TAGS_BUILD_BENCHMARKS)
  include(CTest)
  enable_testing()
  add_library(test_util INTERFACE)
  target_include_directories(test_util INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/test_util)
endif()

option(CBOR_TAGS_TIDY_TARGET "Enable clang-tidy target" OFF)
option(CBOR_TAGS_TIDY_TESTS "Run clang-tidy on test sources" OFF)
option(CBOR_TAGS_TIDY_BENCHMARKS "Deprecated/no-op: benchmark sources are excluded from clang-tidy" OFF)
if(CBOR_TAGS_TIDY_TARGET)
  cpmaddpackage("gh:jkammerland/clang-tidy.cmake@1.2.0")
  add_library(cbor_tags_tidy OBJECT EXCLUDE_FROM_ALL tools/cbor_tags_tidy.cpp)
  target_link_libraries(cbor_tags_tidy PRIVATE cbor::tags fmt::fmt nameof::nameof)
  target_tidy_sources(cbor_tags_tidy)
endif()

# add tests
if(CBOR_TAGS_BUILD_TESTS)
  add_subdirectory(test)
endif()

# add benchmarks
if(CBOR_TAGS_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

if(CBOR_TAGS_INSTALL)
  cpmaddpackage(gh:jkammerland/target_install_package.cmake@7.0.6)
  set(CBOR_TAGS_INSTALL_PACKAGE_ARGS ${PROJECT_NAME} NAMESPACE cbor:: ALIAS_NAME tags)
  if(CBOR_TAGS_PUBLIC_DEPENDENCIES)
    list(APPEND CBOR_TAGS_INSTALL_PACKAGE_ARGS PUBLIC_DEPENDENCIES ${CBOR_TAGS_PUBLIC_DEPENDENCIES})
  endif()
  target_install_package(${CBOR_TAGS_INSTALL_PACKAGE_ARGS})
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
message(STATUS "Benchmark presets: benchmark-comparison")
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
