cmake_minimum_required(VERSION 3.20)
project(cbor_tags VERSION 0.7.3)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add option for debug mode
option(CBOR_TAGS_DEBUG "Enable debug mode" OFF)

# Configure a header file to pass the CMake setting to the source code
configure_file(
    "${PROJECT_SOURCE_DIR}/cbor_tags_config.h.in"
    "${PROJECT_BINARY_DIR}/include/cbor_tags/cbor_tags_config.h"
)

# Check minimum compiler version
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/minimum_compiler.cmake)

option(CBOR_TAGS_USE_DEV_SETTINGS "Use dev settings" OFF)
if(CBOR_TAGS_USE_DEV_SETTINGS)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ccache.cmake)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/use_mold_linker.cmake)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/common_settings.cmake)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/print_compiler_and_flags.cmake)
endif()

# add tools - reflection module generator - makes cbor_reflection_impl.h
option(CBOR_TAGS_BUILD_TOOLS "Build tools (for to_tuple(...) generation, it is required for struct reflection)" ON)
if(CBOR_TAGS_BUILD_TOOLS)
  add_subdirectory(tools)
else()
  message(STATUS "Skipping tools - reflections headers must exist.")
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include/cbor_tags/cbor_reflection_impl.h")
    message(FATAL_ERROR "cbor_reflection_impl.h not found. Enable CBOR_TAGS_BUILD_TOOLS to generate it.")
  endif()
endif()

# THE LIBRARY
add_library(${PROJECT_NAME} INTERFACE)
target_include_directories(${PROJECT_NAME} INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}> )

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
    1.1.0
    GIT_TAG
    v1.1.0
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

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/installation_target.cmake)
create_installation_target(cbor_tags)

# Make ${PROJECT_NAME} depend on the generated header
add_dependencies(${PROJECT_NAME} generate_reflection_module)

# Extra options
option(CBOR_TAGS_BUILD_TESTS "Build tests" OFF)
option(CBOR_TAGS_BUILD_BENCHMARKS "Build benchmarks" OFF)
if(CBOR_TAGS_BUILD_TESTS OR CBOR_TAGS_BUILD_BENCHMARKS)
  include(CTest)
  enable_testing()
endif()

# add tests
if(CBOR_TAGS_BUILD_TESTS)
  add_subdirectory(test)
  # add_subdirectory(doc/test_readme) # TODO:
endif()

# add benchmarks
if(CBOR_TAGS_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

option(CBOR_TAGS_TIDY_TARGET "Enable clang-tidy target" OFF)
if(CBOR_TAGS_TIDY_TARGET)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/create_tidy_target.cmake)
  create_tidy_target(${CMAKE_CURRENT_SOURCE_DIR}/test)
endif()

include (${CMAKE_CURRENT_SOURCE_DIR}/cmake/print_options.cmake)
print_options(DEBUG "^CBOR_TAGS")
