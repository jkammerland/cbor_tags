cmake_minimum_required(VERSION 3.20)
project(tools VERSION 0.1.0)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/get_cpm.cmake)
set(CPM_SOURCE_CACHE ${CMAKE_BINARY_DIR}/cpm_cache)

# As soon as std::format is more widely available, we can remove this
cpmaddpackage("gh:fmtlib/fmt#11.1.2")

add_executable(reflection_module_generator reflection_module_generator.cpp)
target_link_libraries(reflection_module_generator PRIVATE fmt::fmt)

# Set specific compile options for reflection_module_generator
if(MSVC)
  # target_compile_options(reflection_module_generator PRIVATE /O2 /Ob2 /Oi /Ot /GL)
else()
  target_compile_options(reflection_module_generator PRIVATE -O2 -g0)
endif()

# Define ranges as a space-separated string
set(REFLECTION_RANGES
    "1:100" # Default ranges, space-separated
    CACHE STRING "Ranges for reflection (format: start1:end1 start2:end2 ...)")
foreach(RANGE ${REFLECTION_RANGES})
  if(NOT RANGE MATCHES "^\"?([0-9]+):([0-9]+)\"?$")
    message(FATAL_ERROR "Invalid range format: ${RANGE}. Expected format: start:end")
  endif()
endforeach()

set(RANGES_CACHE_FILE "${CMAKE_BINARY_DIR}/reflection_ranges.txt")
set(OUTPUT_HEADER_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../include/cbor_tags/cbor_reflection_impl.h")

# Ensure the output directory exists
file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../include/cbor_tags")

# Handle REFLECTION_RANGES changes
if(EXISTS "${RANGES_CACHE_FILE}")
  file(READ "${RANGES_CACHE_FILE}" CACHED_RANGES)
  if(NOT "${CACHED_RANGES}" STREQUAL "${REFLECTION_RANGES}")
    file(REMOVE ${OUTPUT_HEADER_FILE})
    file(WRITE "${RANGES_CACHE_FILE}" "${REFLECTION_RANGES}")
    message(STATUS "Reflection ranges changed [${CACHED_RANGES}] -> [${REFLECTION_RANGES}]")
  else()
    message(STATUS "Reflection ranges unchanged [${REFLECTION_RANGES}]")
  endif()
else()
  file(WRITE "${RANGES_CACHE_FILE}" "${REFLECTION_RANGES}")
  message(STATUS "Reflection ranges set [${REFLECTION_RANGES}]")
endif()

# Generate the header
add_custom_command(
  OUTPUT ${OUTPUT_HEADER_FILE}
  COMMAND reflection_module_generator ${REFLECTION_RANGES}
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
  COMMAND reflection_module_generator ${REFLECTION_RANGES}
  DEPENDS reflection_module_generator
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../include/cbor_tags)
