cmake_minimum_required(VERSION 3.20)
project(cbor_tags VERSION 0.1.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/common_settings.cmake)

# add tools - reflection module generator primarily
option(BUILD_TOOLS "Build tools" ON)
if(BUILD_TOOLS)
  add_subdirectory(tools)
else()
  message(STATUS "Skipping tools - reflections headers must exist.")
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include/cbor_tags/reflection_module.hpp")
    message(FATAL_ERROR "reflection_module.hpp not found. Enable BUILD_TOOLS to generate it.")
  endif()
endif()

# THE LIBRARY
add_library(${PROJECT_NAME} INTERFACE)
target_include_directories(${PROJECT_NAME} INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/installation_target.cmake)

# Make ${PROJECT_NAME} depend on the generated header
add_dependencies(${PROJECT_NAME} generate_reflection_module)

# add python bindings: WIP
option(BUILD_PYTHON_BINDINGS "Build Python bindings" OFF)
if(BUILD_PYTHON_BINDINGS)
  add_subdirectory(python-bindings)
endif()

# add tests
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()

option(TIDY_TARGET "Enable clang-tidy target" OFF)
if(TIDY_TARGET)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/create_tidy_target.cmake)
  create_tidy_target(${CMAKE_CURRENT_SOURCE_DIR}/test)
endif()
