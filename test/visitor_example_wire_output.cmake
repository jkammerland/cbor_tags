# Run the compiled example and capture its printed traversal.
execute_process(
  COMMAND "${EXAMPLE_EXECUTABLE}"
  RESULT_VARIABLE example_result
  OUTPUT_VARIABLE example_output
  ERROR_VARIABLE example_error)

# Preserve the example's own failure checks.
if(NOT example_result STREQUAL "0")
  message(FATAL_ERROR "Visitor example failed (${example_result}): ${example_error}")
endif()

# Compare the value bytes, nesting indentation, group boundary, and omitted headers.
set(expected_output "    01\n    02\n\n    03\n")
string(REPLACE "\r\n" "\n" example_output "${example_output}")
if(NOT example_output STREQUAL expected_output)
  message(FATAL_ERROR "Unexpected visitor output:\n${example_output}\nExpected:\n${expected_output}")
endif()
