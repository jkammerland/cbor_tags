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
set(CMAKE_WARN_DEPRECATED
    ON
    CACHE BOOL "" FORCE)
cpmaddpackage("gh:martinus/nanobench@4.3.11")

# add executables more if needed
add_executable(${PROJECT_NAME})

# add target_sources, all .cpp files in this directory
target_sources(${PROJECT_NAME} PRIVATE main.cpp)

find_package(fmt CONFIG REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE cbor_tags fmt::fmt doctest nanobench benchmark_util)

# add tests for ctest
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})

add_subdirectory(old_benchmarks)
