# Link pybind11
pybind11_add_module(pybinding2 my_module_bindings.cpp)
target_include_directories(pybinding2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(pybinding2 PRIVATE VERSION_INFO=${PROJECT_VERSION})
install(TARGETS pybinding2 DESTINATION ${CMAKE_BINARY_DIR}/python_bindings)

# Install main.py in binary directory
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/main2.py DESTINATION ${CMAKE_BINARY_DIR}/python_bindings)

message(STATUS "Done with python bindings tests")
