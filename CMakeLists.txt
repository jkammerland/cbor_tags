cmake_minimum_required(VERSION 3.20)
project(cbor_tags VERSION 0.1.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(cmake/common_settings.cmake)

add_library(cbor_tags INTERFACE)
target_include_directories(cbor_tags INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
include(cmake/installation_target.cmake)

# add python bindings TODO: FIX FPIC FOR ONLY THIS SUBDIR
option(BUILD_PYTHON_BINDINGS "Build Python bindings" ON)
if(BUILD_PYTHON_BINDINGS)
  add_subdirectory(python-bindings)
endif()

# add tools
option(BUILD_TOOLS "Build tools" ON)
if(BUILD_TOOLS)
  add_subdirectory(tools)
endif()

# add tests
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()

include(cmake/create_tidy_target.cmake)
create_tidy_target(${CMAKE_CURRENT_SOURCE_DIR}/test)
