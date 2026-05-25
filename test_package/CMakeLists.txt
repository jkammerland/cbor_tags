cmake_minimum_required(VERSION 3.25)

project(cbor_tags_test_package LANGUAGES CXX)

find_package(cbor_tags REQUIRED)

add_executable(test_package test_package.cpp)
target_link_libraries(test_package PRIVATE cbor::tags)
if(CMAKE_CXX_STANDARD)
  set_property(TARGET test_package PROPERTY CXX_STANDARD ${CMAKE_CXX_STANDARD})
endif()
set_property(TARGET test_package PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET test_package PROPERTY CXX_EXTENSIONS OFF)
