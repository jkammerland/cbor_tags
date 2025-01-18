cmake_minimum_required(VERSION 3.22)
project(benchmarks CXX)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/get_cpm.cmake)
set(CPM_SOURCE_CACHE ${CMAKE_BINARY_DIR}/cpm_cache)

cpmaddpackage("gh:fmtlib/fmt#11.1.2")
cpmaddpackage("gh:catchorg/Catch2@3.8.0")

# add executables more if needed
add_executable(${PROJECT_NAME})

# add target_sources, all .cpp files in this directory
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
target_sources(${PROJECT_NAME} PRIVATE ${TEST_SOURCES})

find_package(fmt CONFIG REQUIRED)
find_package(Catch2 CONFIG REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE cbor_tags fmt::fmt Catch2::Catch2WithMain)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# add tests for ctest
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
