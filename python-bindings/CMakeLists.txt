cmake_minimum_required(VERSION 3.22)

project(python-bindings VERSION 0.1.0)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
message(STATUS "CMAKE_POSITION_INDEPENDENT_CODE: ${CMAKE_POSITION_INDEPENDENT_CODE}")

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/get_cpm.cmake)
set(CPM_SOURCE_CACHE ${CMAKE_BINARY_DIR}/python_cache/cpm_cache)
cpmaddpackage("gh:fmtlib/fmt#11.0.2")

# Find LLVM and Clang packages
find_package(LLVM REQUIRED CONFIG PATHS)
find_package(Clang REQUIRED CONFIG PATHS)
# find_package(LibArchive REQUIRED)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "Found Clang ${CLANG_PACKAGE_VERSION}")
message(STATUS "Using ClangConfig.cmake in: ${Clang_DIR}")

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(HandleLLVMOptions)
add_definitions(${LLVM_DEFINITIONS})

# Find the libraries that correspond to the LLVM components that we wish to use
# llvm_map_components_to_libnames(llvm_libs SUPPORT frontenddriver core irreader analysis)

# Add executable
add_executable(binding_generator)
target_sources(binding_generator PRIVATE binding_generator.cpp)
target_include_directories(binding_generator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Glob all libs in /usr/local/lib *.a and *.so
# file(GLOB LIBS /usr/local/lib/*.a /usr/local/lib/*.so)

set(CLANG_LIBS
    clangTooling
    clangFrontend
    clangASTMatchers
    clang-cpp
    clangBasic
    clangAST
    clangSerialization)

# Link LLVM and Clang libraries
target_link_libraries(binding_generator PRIVATE fmt::fmt tinfo ${CLANG_LIBS})

# add_subdirectory(tests)
