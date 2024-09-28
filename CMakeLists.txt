cmake_minimum_required(VERSION 3.20)
project(cbor_tags VERSION 1.0.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(cbor_tags INTERFACE)
target_include_directories(cbor_tags INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# add tests
enable_testing()
add_subdirectory(test)
