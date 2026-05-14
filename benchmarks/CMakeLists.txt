cmake_minimum_required(VERSION 3.22)
project(benchmarks_new CXX)

add_library(benchmark_util INTERFACE)
target_include_directories(benchmark_util INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

function(cbor_tags_configure_benchmark_target target_name)
  # Benchmarks intentionally leave decode destinations with minimal setup so they measure the
  # codec path instead of benchmark scaffolding. Keep warning/tidy policy on production and tests.
  set_target_properties(${target_name} PROPERTIES COMPILE_WARNING_AS_ERROR OFF CXX_CLANG_TIDY "")
  target_compile_options(
    ${target_name}
    PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-maybe-uninitialized>
            $<$<CXX_COMPILER_ID:GNU>:-Wno-uninitialized>
            $<$<CXX_COMPILER_ID:Clang>:-Wno-uninitialized>
            $<$<CXX_COMPILER_ID:AppleClang>:-Wno-uninitialized>
            $<$<CXX_COMPILER_ID:Clang>:-Wno-sometimes-uninitialized>
            $<$<CXX_COMPILER_ID:AppleClang>:-Wno-sometimes-uninitialized>)
endfunction()

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/get_cpm.cmake)
set(CPM_SOURCE_CACHE ${CMAKE_BINARY_DIR}/cpm_cache)

cpmaddpackage("gh:jkammerland/doctest@1.0.1")
if(NOT TARGET fmt::fmt)
  cpmaddpackage("gh:fmtlib/fmt#11.1.2")
endif()
set(CMAKE_WARN_DEPRECATED
    OFF
    CACHE BOOL "" FORCE)
cpmaddpackage("gh:martinus/nanobench@4.3.11")
set(CMAKE_WARN_DEPRECATED
    ON
    CACHE BOOL "" FORCE)
if(NOT TARGET nameof::nameof)
  cpmaddpackage("gh:Neargye/nameof@0.10.4")
endif()

add_subdirectory(encoder)
add_subdirectory(decoder)
add_subdirectory(ranges)

option(CBOR_TAGS_BUILD_COMPARISON_BENCHMARKS "Build serialization comparison benchmarks" OFF)
if(CBOR_TAGS_BUILD_COMPARISON_BENCHMARKS)
  add_subdirectory(serialization_compare)
endif()

option(CBOR_TAGS_OLD_BENCHMARKS "Run old benchmarks" OFF)
if(CBOR_TAGS_OLD_BENCHMARKS)
  add_subdirectory(old_benchmarks)
endif()
