cmake_minimum_required(VERSION 3.31)
project(markdown_extractor CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Create the markdown code extractor executable
add_executable(md_extractor md_extractor.cpp)

# Define the output directory for extracted code
set(EXTRACTED_CODE_DIR ${CMAKE_BINARY_DIR}/extracted_code)

# Find all markdown files in the docs directory recursively, excluding build directory
file(GLOB_RECURSE MD_FILES_TMP "${CMAKE_SOURCE_DIR}/*.md")
list(FILTER MD_FILES_TMP EXCLUDE REGEX "${CMAKE_SOURCE_DIR}/build/.*")
set(MD_FILES ${MD_FILES_TMP})

# Print the markdown files found
message(STATUS "Markdown files found: ${MD_FILES}")

# Add custom command to extract code from markdown files
add_custom_command(
  OUTPUT ${EXTRACTED_CODE_DIR}/extraction_complete
  COMMAND ${CMAKE_COMMAND} -E make_directory ${EXTRACTED_CODE_DIR}
  COMMAND md_extractor -o ${EXTRACTED_CODE_DIR} -v ${MD_FILES_TMP}
  COMMAND ${CMAKE_COMMAND} -E touch ${EXTRACTED_CODE_DIR}/extraction_complete
  DEPENDS md_extractor ${MD_FILES_TMP}
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Extracting code examples from markdown files"
  VERBATIM CODEGEN)

# Add custom target for code generation
add_custom_target(generate_code_examples DEPENDS ${EXTRACTED_CODE_DIR}/extraction_complete)

# Function to add extracted code as tests
function(add_extracted_code_tests)
  file(GLOB_RECURSE EXTRACTED_CPP_FILES "${EXTRACTED_CODE_DIR}/*.cpp")
  foreach(SOURCE_FILE ${EXTRACTED_CPP_FILES})
    get_filename_component(TEST_NAME ${SOURCE_FILE} NAME_WE)
    add_executable(${TEST_NAME} ${SOURCE_FILE})
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    add_dependencies(${TEST_NAME} generate_code_examples)
  endforeach()
endfunction()

# Enable testing
enable_testing()

# Add the extracted code tests after code generation
add_extracted_code_tests()
