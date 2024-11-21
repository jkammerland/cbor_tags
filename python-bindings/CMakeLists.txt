cmake_minimum_required(VERSION 3.22)

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

# Other shared lib

# add_library(python_bindings_example SHARED python_bindings_example.cpp)

# target_link_libraries(python_bindings_example PRIVATE fmt nameof::nameof magic_enum::magic_enum)

# target_compile_options(python_bindings_example PRIVATE -fPIC)
