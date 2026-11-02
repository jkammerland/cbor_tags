execute_process(
  COMMAND "${PROGRAM}" "${CASE}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)

# The child distinguishes an assertion from an uncaught exception or no failure.
if(NOT result STREQUAL "42")
  message(FATAL_ERROR "${CASE}: expected assertion exit 42, got ${result}\n${output}\n${error}")
endif()
if(NOT error MATCHES "${DIAGNOSTIC}")
  message(FATAL_ERROR "${CASE}: expected assertion diagnostic '${DIAGNOSTIC}'\n${error}")
endif()
