execute_process(COMMAND "${PROGRAM}" invalid-value RESULT_VARIABLE result)
if(MSVC_NO_EXCEPTIONS)
  # Microsoft STL's _HAS_EXCEPTIONS=0 makes set_terminate a no-op; terminate
  # calls abort, which Windows reports as this fast-fail process status.
  set(expected_result "Exit code 0xc0000409")
else()
  set(expected_result "42")
endif()
if(NOT result STREQUAL expected_result)
  message(FATAL_ERROR "invalid value access must terminate with ${expected_result}; got ${result}")
endif()
