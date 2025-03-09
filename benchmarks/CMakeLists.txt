cmake_minimum_required(VERSION 3.22)
project(benchmarks_new CXX)

add_library(benchmark_util INTERFACE)
target_include_directories(benchmark_util INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/get_cpm.cmake)
set(CPM_SOURCE_CACHE ${CMAKE_BINARY_DIR}/cpm_cache)

cpmaddpackage("gh:fmtlib/fmt#11.1.2")
set(CMAKE_WARN_DEPRECATED
    OFF
    CACHE BOOL "" FORCE)
cpmaddpackage("gh:onqtam/doctest@2.4.11")
cpmaddpackage("gh:martinus/nanobench@4.3.11")
set(CMAKE_WARN_DEPRECATED
    ON
    CACHE BOOL "" FORCE)
cpmaddpackage("gh:Neargye/nameof@0.10.4")

add_subdirectory(encoder)
add_subdirectory(decoder)

option(CBOR_TAGS_OLD_BENCHMARKS "Run old benchmarks" OFF)
if (CBOR_TAGS_OLD_BENCHMARKS)
    add_subdirectory(old_benchmarks)
endif()
