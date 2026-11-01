cmake_minimum_required(VERSION 3.23)
project(cbor_tags_source_consumer LANGUAGES CXX)

# Exercise the extracted library, independently of its example names and layout.
add_subdirectory("${CBOR_TAGS_SOURCE_DIR}" cbor_tags EXCLUDE_FROM_ALL)

# Standalone consumers use .cc to stay outside the unit test runner's .cpp glob.
add_executable(cbor_tags_source_consumer main.cc)
target_link_libraries(cbor_tags_source_consumer PRIVATE cbor::tags)

enable_testing()
add_test(NAME source_archive_roundtrip COMMAND cbor_tags_source_consumer)
