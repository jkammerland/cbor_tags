cmake_minimum_required(VERSION 3.20)
project(cbor_tags VERSION 0.1.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(cmake/common_settings.cmake)

add_library(cbor_tags INTERFACE)
target_include_directories(cbor_tags INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
include(cmake/installation_target.cmake)

# add python bindings: WIP
option(BUILD_PYTHON_BINDINGS "Build Python bindings" OFF)
if(BUILD_PYTHON_BINDINGS)
  add_subdirectory(python-bindings)
endif()

# add tools
option(BUILD_TOOLS "Build tools" OFF)
if(BUILD_TOOLS)
  add_subdirectory(tools)
endif()

# add tests
option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()

option(TIDY_TARGET "Enable clang-tidy target" OFF)

if(TIDY_TARGET)
  include(cmake/create_tidy_target.cmake)
  create_tidy_target(${CMAKE_CURRENT_SOURCE_DIR}/test)
endif()
