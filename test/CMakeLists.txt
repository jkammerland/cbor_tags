cmake_minimum_required(VERSION 3.20)
project(tests VERSION 1.0.0)
include(${CMAKE_SOURCE_DIR}/cmake/get_cpm.cmake)
set(CPM_SOURCE_CACHE ${CMAKE_BINARY_DIR}/cpm_cache)

cpmaddpackage("gh:fmtlib/fmt#11.0.2")
# cpmaddpackage("gh:catchorg/Catch2@3.7.1")
cpmaddpackage("gh:Neargye/nameof@0.10.4")

set(CMAKE_WARN_DEPRECATED
    OFF
    CACHE BOOL "" FORCE)
cpmaddpackage("gh:onqtam/doctest@2.4.11")
set(CMAKE_WARN_DEPRECATED
    ON
    CACHE BOOL "" FORCE)

# add executables more if needed
add_executable(tests)

# add target_sources, all .cpp files in this directory
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
target_sources(tests PRIVATE ${TEST_SOURCES})

# add test include directories
target_include_directories(tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# add link libraries
target_link_libraries(tests PRIVATE cbor_tags doctest::doctest fmt::fmt nameof::nameof)

# Enable ASAN for linux and macos
if(UNIX OR APPLE)
  target_compile_options(tests PRIVATE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(tests PRIVATE -fsanitize=address)
elseif(WIN32)
  target_compile_options(tests PRIVATE /fsanitize=address)
  target_link_options(tests PRIVATE /fsanitize=address)
endif()

# add tests for ctest
add_test(NAME tests COMMAND tests)
