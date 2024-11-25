cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED ENV{VCPKG_ROOT})
  message(FATAL_ERROR "VCPKG_ROOT is not defined, please set it to the vcpkg root directory")
else()
  message(STATUS "VCPKG_ROOT: $ENV{VCPKG_ROOT}")
endif()
include("$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")

project(python-bindings VERSION 0.1.0)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
message(STATUS "CMAKE_POSITION_INDEPENDENT_CODE: ${CMAKE_POSITION_INDEPENDENT_CODE}")

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/get_cpm.cmake)
set(CPM_SOURCE_CACHE ${CMAKE_BINARY_DIR}/python_cache/cpm_cache)

cpmaddpackage("gh:fmtlib/fmt#11.0.2")
cpmaddpackage("gh:Neargye/nameof@0.10.4")
cpmaddpackage("gh:Neargye/magic_enum@0.9.7")

set(PYBIND11_FINDPYTHON ON)
cpmaddpackage("gh:pybind/pybind11@2.13.6")

# Link pybind11
pybind11_add_module(pybinding1 python_bindings_example.cpp)
target_link_libraries(pybinding1 PRIVATE fmt::fmt nameof::nameof magic_enum::magic_enum cbor_tags)
target_compile_definitions(pybinding1 PRIVATE VERSION_INFO=${PROJECT_VERSION})
install(TARGETS pybinding1 DESTINATION ${CMAKE_BINARY_DIR}/python_bindings)

# Install main.py in binary directory
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/main.py DESTINATION ${CMAKE_BINARY_DIR}/python_bindings)

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
llvm_map_components_to_libnames(llvm_libs SUPPORT frontenddriver core irreader analysis)

# Add executable
add_executable(binding_generator binding_generator.cpp)

target_include_directories(binding_generator PRIVATE ${LLVM_INCLUDE_DIRS})

target_link_directories(binding_generator PRIVATE "$ENV{VCPKG_ROOT}/installed/x64-linux/lib")

message(STATUS "LLVM LIBS: ${llvm_libs}")

file(GLOB CLANG_LIBS "$ENV{VCPKG_ROOT}/installed/x64-linux/lib/lib*.a" "$ENV{VCPKG_ROOT}/installed/x64-linux/lib/lib*.so")

# Link LLVM and Clang libraries
target_link_libraries(binding_generator PRIVATE fmt::fmt nameof::nameof magic_enum::magic_enum ${CLANG_LIBS})

# Other shared lib

# add_library(python_bindings_example SHARED python_bindings_example.cpp)

# target_link_libraries(python_bindings_example PRIVATE fmt nameof::nameof magic_enum::magic_enum)

# target_compile_options(python_bindings_example PRIVATE -fPIC)
