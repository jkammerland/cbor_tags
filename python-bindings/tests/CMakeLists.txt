include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/get_cpm.cmake)
set(CPM_SOURCE_CACHE ${CMAKE_BINARY_DIR}/python_cache/cpm_cache)

set(PYBIND11_FINDPYTHON ON)
cpmaddpackage("gh:pybind/pybind11@2.13.6")

# Link pybind11
pybind11_add_module(pybinding2)
target_sources(pybinding2 PRIVATE pybinding2.cpp)
target_include_directories(pybinding2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(pybinding2 PRIVATE VERSION_INFO=${PROJECT_VERSION})
install(TARGETS pybinding2 DESTINATION ${CMAKE_BINARY_DIR}/python_bindings)

# Install main.py in binary directory

message(STATUS "Done with python bindings tests")
