cmake_minimum_required(VERSION 3.20)
project(tests VERSION 1.0.0)
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/get_cpm.cmake)
set(CPM_SOURCE_CACHE ${CMAKE_BINARY_DIR}/cpm_cache)

cpmaddpackage("gh:fmtlib/fmt#11.1.2")
cpmaddpackage("gh:Neargye/nameof@0.10.4")
cpmaddpackage("gh:Neargye/magic_enum@0.9.7")
cpmaddpackage("gh:ericniebler/range-v3#ca1388fb9da8e69314dda222dc7b139ca84e092f")
cpmaddpackage("gh:jkammerland/doctest@1.0.1")

# add executables more if needed
add_executable(tests)

# add target_sources, all .cpp files in this directory
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
target_sources(tests PRIVATE ${TEST_SOURCES})

# add link libraries
target_link_libraries(
  tests
  PRIVATE cbor::tags
          test_util
          doctest::doctest
          fmt::fmt
          nameof::nameof
          magic_enum::magic_enum
          range-v3::range-v3)

# Enable ASAN for linux and macos
option(CBOR_TAGS_ENABLE_ASAN "Enable ASAN in tests, DEFAULT=OFF" OFF)
if(CBOR_TAGS_ENABLE_ASAN)
  message(STATUS "ASAN enabled in tests")
  if((UNIX OR APPLE) AND NOT ${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
    target_compile_options(tests PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(tests PRIVATE -fsanitize=address)
  endif()
endif()

if(CBOR_TAGS_TIDY_TARGET)
  message(STATUS "Tidy enabled in tests")
  target_tidy_sources(tests)
endif()

# add tests for ctest
add_test(NAME tests COMMAND tests)

option(CBOR_TAGS_BUILD_CPP23_TESTS "Build cpp23 tests, DEFAULT=OFF" OFF)
if(CBOR_TAGS_BUILD_CPP23_TESTS)
  add_subdirectory(cpp23)
endif()
