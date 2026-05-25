cmake_minimum_required(VERSION 3.25)

project(cbor_tags_stl_only_consumer LANGUAGES CXX)

find_package(cbor_tags CONFIG REQUIRED)

add_executable(consumer main.cc)
target_link_libraries(consumer PRIVATE cbor::tags)

get_target_property(cbor_tags_include_dirs cbor::tags INTERFACE_INCLUDE_DIRECTORIES)
add_executable(direct_type_name_header direct_type_name_header.cc)
target_include_directories(direct_type_name_header PRIVATE ${cbor_tags_include_dirs})
