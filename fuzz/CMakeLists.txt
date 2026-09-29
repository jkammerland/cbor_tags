cmake_minimum_required(VERSION 3.25)
project(cbor_tags_fuzz LANGUAGES C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "The standalone fuzz suite supports Linux only.")
endif()
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(FATAL_ERROR "Use Clang for FuzzTest coverage instrumentation and ASan.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FUZZTEST_FUZZING_MODE "Build coverage-guided fuzzing with ASan" ON)
option(CBOR_FUZZ_COVERAGE "Collect LLVM source coverage as well as fuzzing feedback" OFF)
set(CBOR_FUZZTEST_SOURCE_DIR "" CACHE PATH "Existing external FuzzTest checkout (optional)")

# Dependencies stay in the build tree; nothing is vendored or installed by this suite.
include(FetchContent)
if(CBOR_FUZZTEST_SOURCE_DIR)
  add_subdirectory("${CBOR_FUZZTEST_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/fuzztest" EXCLUDE_FROM_ALL)
else()
  FetchContent_Declare(fuzztest
    GIT_REPOSITORY https://github.com/google/fuzztest.git
    GIT_TAG 7a8e9056de2a4f745082a4b3861a67ada47ced36)
  FetchContent_MakeAvailable(fuzztest)
endif()

# Keep the ordinary doctest build and consumers independent of FuzzTest.
set(CBOR_TAGS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CBOR_TAGS_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(CBOR_TAGS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CBOR_TAGS_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(CBOR_TAGS_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/.." "${CMAKE_BINARY_DIR}/cbor_tags" EXCLUDE_FROM_ALL)

find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/wire_vocabulary.h"
  COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/generate_vocabulary.py"
          "${CMAKE_CURRENT_SOURCE_DIR}/cbor.dict" "${CMAKE_CURRENT_BINARY_DIR}/wire_vocabulary.h"
  DEPENDS generate_vocabulary.py cbor.dict VERBATIM)

# Apply upstream's instrumentation only after adding framework dependencies.
fuzztest_setup_fuzzing_flags()
add_executable(cbor_fuzz
  roundtrip_fuzz.cpp wire_fuzz.cpp extensions_fuzz.cpp visualization_fuzz.cpp
  "${CMAKE_CURRENT_BINARY_DIR}/wire_vocabulary.h")
target_include_directories(cbor_fuzz PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(cbor_fuzz PRIVATE cbor::tags)
link_fuzztest(cbor_fuzz)
target_compile_options(cbor_fuzz PRIVATE -fno-omit-frame-pointer)
if(CBOR_FUZZ_COVERAGE)
  target_compile_options(cbor_fuzz PRIVATE -fprofile-instr-generate -fcoverage-mapping)
  target_link_options(cbor_fuzz PRIVATE -fprofile-instr-generate)
endif()

enable_testing()
add_test(NAME fuzz_properties COMMAND cbor_fuzz --corpus_database=)
set_tests_properties(fuzz_properties PROPERTIES TIMEOUT 180)
add_test(NAME fuzz_tools COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/test_tools.py")

# Upstream sets C++17 in its directory. Abseil's source_location ABI depends on
# the language standard, so framework objects and our C++20 consumers must agree.
function(cbor_fuzz_set_cxx20 directory)
  get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(target IN LISTS targets)
    get_target_property(type "${target}" TYPE)
    if(NOT type STREQUAL "INTERFACE_LIBRARY" AND NOT type STREQUAL "UTILITY")
      set_target_properties("${target}" PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    endif()
  endforeach()
  get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
  foreach(child IN LISTS children)
    cbor_fuzz_set_cxx20("${child}")
  endforeach()
endfunction()
cbor_fuzz_set_cxx20("${CMAKE_CURRENT_SOURCE_DIR}")
