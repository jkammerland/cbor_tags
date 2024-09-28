cmake_minimum_required(VERSION 3.20)
project(tests VERSION 1.0.0)
include(cmake/get_cpm.cmake)

cpmaddpackage("gh:fmtlib/fmt#11.0.2")
# cpmaddpackage("gh:catchorg/Catch2@3.7.1")
cpmaddpackage("gh:Neargye/nameof@0.10.4")
cpmaddpackage("gh:onqtam/doctest@2.4.11")

# add executables more if needed
add_executable(tests)

# add target_sources, all .cpp files in this directory
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
target_sources(tests PRIVATE ${TEST_SOURCES})

# add include directories (zpp_bits)
target_include_directories(tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# add link libraries
target_link_libraries(tests PRIVATE cbor_tags doctest::doctest fmt::fmt nameof::nameof)

# add tests for ctest
add_test(NAME tests COMMAND tests)
