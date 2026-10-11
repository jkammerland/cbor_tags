file(GLOB EXAMPLE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/[0-9][0-9]_*.cpp")
list(SORT EXAMPLE_SOURCES)

set(OPENSSL_EXAMPLES
  24_experimental_cwt_backend
  25_experimental_cwt_token
  26_experimental_cwt_multi_signer
  28_experimental_cwt_borrowed_token
  29_experimental_cwt_borrowed_multi_signer
  32_experimental_cwt_header_claims)

foreach(EXAMPLE_SOURCE IN LISTS EXAMPLE_SOURCES)
  get_filename_component(EXAMPLE_NAME "${EXAMPLE_SOURCE}" NAME_WE)
  if(EXAMPLE_NAME IN_LIST OPENSSL_EXAMPLES AND NOT CBOR_TAGS_ENABLE_CWT_OPENSSL)
    continue()
  endif()

  set(EXAMPLE_TARGET "cbor_tags_example_${EXAMPLE_NAME}")
  add_executable(${EXAMPLE_TARGET} "${EXAMPLE_SOURCE}")
  target_link_libraries(${EXAMPLE_TARGET} PRIVATE cbor::tags)
  if(EXAMPLE_NAME IN_LIST OPENSSL_EXAMPLES)
    target_link_libraries(${EXAMPLE_TARGET} PRIVATE cbor::cwt_openssl)
  endif()
  add_test(NAME "example_${EXAMPLE_NAME}" COMMAND $<TARGET_FILE:${EXAMPLE_TARGET}>)
  set_tests_properties("example_${EXAMPLE_NAME}" PROPERTIES LABELS "examples")
endforeach()

add_test(
  NAME example_visitor_wire_output
  COMMAND "${CMAKE_COMMAND}"
    "-DEXAMPLE_EXECUTABLE=$<TARGET_FILE:cbor_tags_example_14_item_visitor>"
    -P "${PROJECT_SOURCE_DIR}/test/visitor_example_wire_output.cmake")
set_tests_properties(example_visitor_wire_output PROPERTIES LABELS "examples;cbor-wire")
