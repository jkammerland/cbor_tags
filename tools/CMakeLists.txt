cmake_minimum_required(VERSION 3.20)
project(tools VERSION 0.1.0)

include(${CMAKE_SOURCE_DIR}/cmake/get_cpm.cmake)
set(CPM_SOURCE_CACHE ${CMAKE_BINARY_DIR}/cpm_cache)

cpmaddpackage("gh:fmtlib/fmt#11.0.2")

add_executable(reflection_module_generator reflection_module_generator.cpp)
target_link_libraries(reflection_module_generator PRIVATE fmt::fmt)

set(REFLECTION_MAX_MEMBERS
    100
    CACHE STRING "Maximum number of members for reflection")

message(STATUS "REFLECTION_MAX_MEMBERS: ${REFLECTION_MAX_MEMBERS}")

add_custom_target(
  run_reflection_module_generator
  COMMAND reflection_module_generator ${REFLECTION_MAX_MEMBERS}
  DEPENDS reflection_module_generator
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/include/cbor_tags)
