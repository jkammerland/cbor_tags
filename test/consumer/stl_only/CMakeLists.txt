cmake_minimum_required(VERSION 3.25)

project(cbor_tags_stl_only_consumer LANGUAGES CXX)

find_package(cbor_tags CONFIG REQUIRED)

add_executable(consumer main.cc)
target_link_libraries(consumer PRIVATE cbor::tags)
