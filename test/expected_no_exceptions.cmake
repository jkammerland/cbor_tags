execute_process(COMMAND "${PROGRAM}" invalid-value RESULT_VARIABLE result)
if(NOT result STREQUAL "42")
  message(FATAL_ERROR "invalid value access must call the installed terminate handler; got ${result}")
endif()
