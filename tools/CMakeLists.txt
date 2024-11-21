cmake_minimum_required(VERSION 3.20)
project(tools VERSION 0.1.0)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/get_cpm.cmake)
set(CPM_SOURCE_CACHE ${CMAKE_BINARY_DIR}/cpm_cache)

cpmaddpackage("gh:fmtlib/fmt#11.0.2")

add_executable(reflection_module_generator reflection_module_generator.cpp)
target_link_libraries(reflection_module_generator PRIVATE fmt::fmt)

set(REFLECTION_MAX_MEMBERS
    100
    CACHE STRING "Maximum number of members for reflection")

message(STATUS "REFLECTION_MAX_MEMBERS: ${REFLECTION_MAX_MEMBERS}")

set(MEMBERS_CACHE_FILE "${CMAKE_BINARY_DIR}/reflection_members.txt")
set(OUTPUT_HEADER_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../include/cbor_tags/cbor_reflection_impl.h")

# Ensure the output directory exists
file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../include/cbor_tags")

# Handle REFLECTION_MAX_MEMBERS changes
if(EXISTS "${MEMBERS_CACHE_FILE}")
  file(READ "${MEMBERS_CACHE_FILE}" CACHED_MEMBER_COUNT)
  if(NOT "${CACHED_MEMBER_COUNT}" STREQUAL "${REFLECTION_MAX_MEMBERS}")
    file(REMOVE ${OUTPUT_HEADER_FILE})
    file(WRITE "${MEMBERS_CACHE_FILE}" "${REFLECTION_MAX_MEMBERS}")
  endif()
else()
  file(WRITE "${MEMBERS_CACHE_FILE}" "${REFLECTION_MAX_MEMBERS}")
endif()

# Generate the header
add_custom_command(
  OUTPUT ${OUTPUT_HEADER_FILE}
  COMMAND reflection_module_generator ${REFLECTION_MAX_MEMBERS}
  DEPENDS reflection_module_generator
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../include/cbor_tags
  COMMENT "Generating reflection module header")

# Create target for generation
add_custom_target(generate_reflection_module ALL DEPENDS ${OUTPUT_HEADER_FILE})
add_dependencies(generate_reflection_module reflection_module_generator)

# Install the generated header
install(
  FILES ${OUTPUT_HEADER_FILE}
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cbor_tags
  COMPONENT headers)

# Optional: Keep the manual regeneration target
add_custom_target(
  run_reflection_module_generator
  COMMAND reflection_module_generator ${REFLECTION_MAX_MEMBERS}
  DEPENDS reflection_module_generator
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../include/cbor_tags)
