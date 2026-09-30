execute_process(COMMAND "${PROGRAM}" invalid-value RESULT_VARIABLE result)
# CMake on Windows can append a line ending to the textual process status.
string(REGEX REPLACE "\r?\n$" "" result "${result}")
if(MSVC_NO_EXCEPTIONS)
  # Microsoft STL's _HAS_EXCEPTIONS=0 makes set_terminate a no-op; terminate
  # calls abort, which Windows reports as this fast-fail process status.
  set(expected_result "Exit code 0xc0000409")
else()
  set(expected_result "42")
endif()
if(NOT result STREQUAL expected_result)
  string(LENGTH "${expected_result}" expected_length)
  string(LENGTH "${result}" result_length)
  string(HEX "${expected_result}" expected_hex)
  string(HEX "${result}" result_hex)
  message(FATAL_ERROR
    "invalid value access must terminate with ${expected_result} "
    "(length ${expected_length}, hex ${expected_hex}); got ${result} "
    "(length ${result_length}, hex ${result_hex}); CMake ${CMAKE_VERSION}")
endif()
