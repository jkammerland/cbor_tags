cmake_minimum_required(VERSION 3.20)
project(cbor_tags VERSION 0.1.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable Address Sanitizer if supported
if(MSVC)
    # For Visual Studio 2019 and later
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        # add_compile_options(/fsanitize=address)
        # Disable incremental linking which is incompatible with asan
        # string(REPLACE "/INCREMENTAL" "/INCREMENTAL:NO" CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG}")
    endif()
endif()

option(USE_DEV_SETTINGS "Use dev settings" OFF)
if(USE_DEV_SETTINGS)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ccache.cmake)
  # include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/use_mold_linker.cmake)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/common_settings.cmake)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/print_compiler_and_flags.cmake)
endif()

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

# Define public dependencies (if any)
set(CBOR_TAGS_PUBLIC_DEPENDENCIES "")
# If you had dependencies, you would add them like this: list(APPEND CBOR_TAGS_PUBLIC_DEPENDENCIES "find_dependency(SomePackage)")

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
set(BUILD_TESTS ON)
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
  # add_subdirectory(doc/test_readme)
endif()

option(TIDY_TARGET "Enable clang-tidy target" OFF)
if(TIDY_TARGET)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/create_tidy_target.cmake)
  create_tidy_target(${CMAKE_CURRENT_SOURCE_DIR}/test)
endif()
