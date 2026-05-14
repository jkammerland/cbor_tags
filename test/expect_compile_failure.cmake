if(NOT DEFINED BUILD_DIR)
  message(FATAL_ERROR "BUILD_DIR is required")
endif()

if(NOT DEFINED TEST_TARGET)
  message(FATAL_ERROR "TEST_TARGET is required")
endif()

if(NOT DEFINED PASS_REGEX)
  message(FATAL_ERROR "PASS_REGEX is required")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${BUILD_DIR}" --target "${TEST_TARGET}"
  WORKING_DIRECTORY "${BUILD_DIR}"
  RESULT_VARIABLE build_result
  OUTPUT_VARIABLE build_stdout
  ERROR_VARIABLE build_stderr)

set(build_output "${build_stdout}\n${build_stderr}")

if(build_result EQUAL 0)
  message(FATAL_ERROR "Expected ${TEST_TARGET} to fail compilation, but it built successfully.")
endif()

if(NOT build_output MATCHES "${PASS_REGEX}")
  message(FATAL_ERROR "Expected ${TEST_TARGET} failure output to match:\n${PASS_REGEX}\n\nActual output:\n${build_output}")
endif()

message(STATUS "${TEST_TARGET} failed with expected diagnostic")
