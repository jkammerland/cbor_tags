cmake_minimum_required(VERSION 3.25)
project(tests VERSION 1.0.0)
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/get_cpm.cmake)
set(CPM_SOURCE_CACHE ${CMAKE_BINARY_DIR}/cpm_cache)

if(NOT TARGET fmt::fmt)
  cpmaddpackage("gh:fmtlib/fmt#11.1.2")
endif()
if(NOT TARGET magic_enum::magic_enum)
  cpmaddpackage("gh:Neargye/magic_enum@0.9.7")
endif()
if(NOT TARGET nameof::nameof)
  cpmaddpackage("gh:Neargye/nameof@0.10.4")
endif()
cpmaddpackage("gh:jkammerland/doctest@1.0.1")

include(CheckCXXCompilerFlag)

# add executables more if needed
add_executable(tests)

# add target_sources, all .cpp files in this directory
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
target_sources(tests PRIVATE ${TEST_SOURCES})

# add test include directories
target_link_libraries(tests PUBLIC test_util)

# add link libraries
target_link_libraries(tests PRIVATE cbor::tags doctest::doctest fmt::fmt nameof::nameof magic_enum::magic_enum)

# Enable ASAN for linux and macos
option(CBOR_TAGS_ENABLE_ASAN "Enable ASAN in tests, DEFAULT=OFF" OFF)
if(CBOR_TAGS_ENABLE_ASAN)
  message(STATUS "ASAN enabled in tests")
  if((UNIX OR APPLE) AND NOT ${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
    target_compile_options(tests PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(tests PRIVATE -fsanitize=address)
  elseif(WIN32)
    # target_compile_options(tests PRIVATE /fsanitize=address) target_link_options(tests PRIVATE /fsanitize=address)
  endif()
endif()

if(CBOR_TAGS_TIDY_TARGET AND CBOR_TAGS_TIDY_TESTS)
  message(STATUS "Tidy enabled in tests")
  target_tidy_sources(tests)
endif()

# add tests for ctest
add_test(NAME tests COMMAND tests)

add_executable(test_smart_ptr_allocation_failure test_smart_ptr_allocation_failure.cc)
target_link_libraries(test_smart_ptr_allocation_failure PRIVATE cbor::tags)
add_test(NAME test_smart_ptr_allocation_failure COMMAND test_smart_ptr_allocation_failure)
add_dependencies(tests test_smart_ptr_allocation_failure)

function(add_incompatible_codec_result_compile_fail_test TEST_NAME COMPILE_DEFINITION PASS_REGEX)
  add_executable(${TEST_NAME} EXCLUDE_FROM_ALL incompatible_codec_result_compile_fail.cc)
  target_compile_definitions(${TEST_NAME} PRIVATE ${COMPILE_DEFINITION})
  target_link_libraries(${TEST_NAME} PRIVATE cbor::tags)
  add_test(
    NAME ${TEST_NAME}
    COMMAND
      ${CMAKE_COMMAND}
      "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
      "-DTEST_TARGET=${TEST_NAME}"
      "-DCONFIG=$<CONFIG>"
      "-DPASS_REGEX=${PASS_REGEX}"
      -P
      "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")
endfunction()

function(add_dynamic_bounded_size_compile_fail_test TEST_NAME SOURCE_FILE PASS_REGEX)
  add_executable(${TEST_NAME} EXCLUDE_FROM_ALL ${SOURCE_FILE})
  target_link_libraries(${TEST_NAME} PRIVATE cbor::tags fmt::fmt nameof::nameof)
  add_test(
    NAME ${TEST_NAME}
    COMMAND
      ${CMAKE_COMMAND}
      "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
      "-DTEST_TARGET=${TEST_NAME}"
      "-DCONFIG=$<CONFIG>"
      "-DPASS_REGEX=${PASS_REGEX}"
      -P
      "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")
endfunction()

function(add_custom_codec_1_compile_fail_test TEST_NAME SOURCE_FILE PASS_REGEX)
  add_executable(${TEST_NAME} EXCLUDE_FROM_ALL ${SOURCE_FILE})
  target_link_libraries(${TEST_NAME} PRIVATE cbor::tags)
  add_test(
    NAME ${TEST_NAME}
    COMMAND
      ${CMAKE_COMMAND}
      "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
      "-DTEST_TARGET=${TEST_NAME}"
      "-DCONFIG=$<CONFIG>"
      "-DPASS_REGEX=${PASS_REGEX}"
      -P
      "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")
endfunction()

add_incompatible_codec_result_compile_fail_test(
  incompatible_member_encode_result_compile_fails
  CBOR_TAGS_INCOMPATIBLE_MEMBER_ENCODE
  "custom encode/transcode must return a result")
add_incompatible_codec_result_compile_fail_test(
  incompatible_member_decode_result_compile_fails
  CBOR_TAGS_INCOMPATIBLE_MEMBER_DECODE
  "custom decode/transcode must return a result")
add_incompatible_codec_result_compile_fail_test(
  incompatible_free_encode_result_compile_fails
  CBOR_TAGS_INCOMPATIBLE_FREE_ENCODE
  "custom encode/transcode must return a result")
add_incompatible_codec_result_compile_fail_test(
  incompatible_free_decode_result_compile_fails
  CBOR_TAGS_INCOMPATIBLE_FREE_DECODE
  "custom decode/transcode must return a result")

add_dynamic_bounded_size_compile_fail_test(
  dynamic_bounded_size_cddl_compile_fails
  dynamic_bounded_size_cddl_compile_fail.cc
  "dynamic_bounded_size cannot be represented by type-based CDDL")
add_dynamic_bounded_size_compile_fail_test(
  dynamic_bounded_size_variant_decode_compile_fails
  dynamic_bounded_size_variant_decode_compile_fail.cc
  "dynamic_bounded_size cannot be decoded as a variant alternative")
add_dynamic_bounded_size_compile_fail_test(
  dynamic_bounded_size_optional_decode_compile_fails
  dynamic_bounded_size_optional_decode_compile_fail.cc
  "generic optional decoding cannot create its bounds")
add_dynamic_bounded_size_compile_fail_test(
  dynamic_bounded_size_container_decode_compile_fails
  dynamic_bounded_size_container_decode_compile_fail.cc
  "generic container decoding cannot create its bounds")
add_dynamic_bounded_size_compile_fail_test(
  dynamic_bounded_size_nested_compile_fails
  dynamic_bounded_size_nested_compile_fail.cc
  "bounded size wrappers cannot directly contain another bounded size wrapper")

add_custom_codec_1_compile_fail_test(
  custom_codec_1_zero_width_dynamic_range_compile_fails
  custom_codec_1_zero_width_dynamic_range_compile_fail.cc
  "custom_codec_1 dynamically sized ranges cannot contain zero-width values")
add_custom_codec_1_compile_fail_test(
  custom_codec_1_zero_width_dynamic_array_compile_fails
  custom_codec_1_zero_width_dynamic_array_compile_fail.cc
  "custom_codec_1 dynamically sized ranges cannot contain zero-width values")
add_custom_codec_1_compile_fail_test(
  custom_codec_1_zero_width_dynamic_range_decode_compile_fails
  custom_codec_1_zero_width_dynamic_range_decode_compile_fail.cc
  "custom_codec_1 dynamically sized ranges cannot contain zero-width values")
add_custom_codec_1_compile_fail_test(
  custom_codec_1_zero_width_dynamic_span_compile_fails
  custom_codec_1_zero_width_dynamic_span_compile_fail.cc
  "custom_codec_1 dynamically sized ranges cannot contain zero-width values")
add_custom_codec_1_compile_fail_test(
  custom_codec_1_zero_width_dynamic_map_compile_fails
  custom_codec_1_zero_width_dynamic_map_compile_fail.cc
  "custom_codec_1 dynamically sized maps cannot contain zero-width entries")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  add_dynamic_bounded_size_compile_fail_test(
    typed_array_static_bounds_compile_fails
    typed_array_static_bounds_compile_fail.cc
    "bounded_size typed-array maximum byte size overflows uint64_t")
endif()

add_executable(cddl_shared_ptr_variant_tag_compile_fails EXCLUDE_FROM_ALL cddl_shared_ptr_variant_tag_compile_fail.cc)
target_link_libraries(cddl_shared_ptr_variant_tag_compile_fails PRIVATE cbor::tags fmt::fmt nameof::nameof)
add_test(
  NAME cddl_shared_ptr_variant_tag_compile_fails
  COMMAND
    ${CMAKE_COMMAND}
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DTEST_TARGET=cddl_shared_ptr_variant_tag_compile_fails"
    "-DCONFIG=$<CONFIG>"
    "-DPASS_REGEX=CDDL pointer variant alternatives overlap on the CBOR wire"
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")

function(add_smart_ptr_cddl_compile_fail_test TEST_NAME SOURCE_FILE PASS_REGEX)
  add_executable(${TEST_NAME} EXCLUDE_FROM_ALL ${SOURCE_FILE})
  target_link_libraries(${TEST_NAME} PRIVATE cbor::tags fmt::fmt nameof::nameof)
  add_test(
    NAME ${TEST_NAME}
    COMMAND
      ${CMAKE_COMMAND}
      "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
      "-DTEST_TARGET=${TEST_NAME}"
      "-DCONFIG=$<CONFIG>"
      "-DPASS_REGEX=${PASS_REGEX}"
      -P
      "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")
endfunction()

add_smart_ptr_cddl_compile_fail_test(
  cddl_shared_ptr_variant_tag29_compile_fails
  cddl_shared_ptr_variant_tag29_compile_fail.cc
  "CDDL pointer variant alternatives overlap on the CBOR wire")
add_smart_ptr_cddl_compile_fail_test(
  cddl_shared_ptr_variant_any_tag_compile_fails
  cddl_shared_ptr_variant_any_tag_compile_fail.cc
  "CDDL pointer variant alternatives overlap on the CBOR wire")
add_smart_ptr_cddl_compile_fail_test(
  cddl_shared_ptr_variant_nested_any_tag_compile_fails
  cddl_shared_ptr_variant_nested_any_tag_compile_fail.cc
  "CDDL pointer variant alternatives overlap on the CBOR wire")
add_smart_ptr_cddl_compile_fail_test(
  cddl_unique_ptr_nested_null_compile_fails
  cddl_unique_ptr_nested_null_compile_fail.cc
  "CDDL unique pointer cannot use T when T also has a CBOR null state")
add_smart_ptr_cddl_compile_fail_test(
  cddl_unique_ptr_variant_overlap_compile_fails
  cddl_unique_ptr_variant_overlap_compile_fail.cc
  "CDDL pointer variant alternatives overlap on the CBOR wire")
add_smart_ptr_cddl_compile_fail_test(
  cddl_optional_smart_ptr_compile_fails
  cddl_optional_smart_ptr_compile_fail.cc
  "CDDL std::optional<T> cannot contain a smart pointer null state because both empty states use CBOR null")
add_executable(cddl_variant_duplicate_tags_compile_fails EXCLUDE_FROM_ALL cddl_variant_duplicate_tags_compile_fail.cc)
target_link_libraries(cddl_variant_duplicate_tags_compile_fails PRIVATE cbor::tags fmt::fmt nameof::nameof)
add_test(
  NAME cddl_variant_duplicate_tags_compile_fails
  COMMAND
    ${CMAKE_COMMAND}
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DTEST_TARGET=cddl_variant_duplicate_tags_compile_fails"
    "-DCONFIG=$<CONFIG>"
    "-DPASS_REGEX=CDDL for variant alternatives with duplicate or catch-all CBOR tag matches is unsupported"
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")

add_executable(cddl_variant_traits_duplicate_tags_compile_fails EXCLUDE_FROM_ALL cddl_variant_traits_duplicate_tags_compile_fail.cc)
target_link_libraries(cddl_variant_traits_duplicate_tags_compile_fails PRIVATE cbor::tags fmt::fmt nameof::nameof)
add_test(
  NAME cddl_variant_traits_duplicate_tags_compile_fails
  COMMAND
    ${CMAKE_COMMAND}
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DTEST_TARGET=cddl_variant_traits_duplicate_tags_compile_fails"
    "-DCONFIG=$<CONFIG>"
    "-DPASS_REGEX=CDDL for variant alternatives with duplicate or catch-all CBOR tag matches is unsupported"
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")

add_executable(variant_traits_throwing_index_compile_fails EXCLUDE_FROM_ALL variant_traits_throwing_index_compile_fail.cc)
target_link_libraries(variant_traits_throwing_index_compile_fails PRIVATE cbor::tags)
add_test(
  NAME variant_traits_throwing_index_compile_fails
  COMMAND
    ${CMAKE_COMMAND}
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DTEST_TARGET=variant_traits_throwing_index_compile_fails"
    "-DCONFIG=$<CONFIG>"
    "-DPASS_REGEX=variant_traits::index must be declared noexcept"
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")

add_executable(cddl_variant_ref_duplicate_tags_compile_fails EXCLUDE_FROM_ALL cddl_variant_ref_duplicate_tags_compile_fail.cc)
target_link_libraries(cddl_variant_ref_duplicate_tags_compile_fails PRIVATE cbor::tags fmt::fmt nameof::nameof)
add_test(
  NAME cddl_variant_ref_duplicate_tags_compile_fails
  COMMAND
    ${CMAKE_COMMAND}
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DTEST_TARGET=cddl_variant_ref_duplicate_tags_compile_fails"
    "-DCONFIG=$<CONFIG>"
    "-DPASS_REGEX=CDDL for variant alternatives with duplicate or catch-all CBOR tag matches is unsupported"
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")

add_executable(cddl_variant_nested_duplicate_tags_compile_fails EXCLUDE_FROM_ALL cddl_variant_nested_duplicate_tags_compile_fail.cc)
target_link_libraries(cddl_variant_nested_duplicate_tags_compile_fails PRIVATE cbor::tags fmt::fmt nameof::nameof)
add_test(
  NAME cddl_variant_nested_duplicate_tags_compile_fails
  COMMAND
    ${CMAKE_COMMAND}
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DTEST_TARGET=cddl_variant_nested_duplicate_tags_compile_fails"
    "-DCONFIG=$<CONFIG>"
    "-DPASS_REGEX=CDDL for variant alternatives with duplicate or catch-all CBOR tag matches is unsupported"
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")

add_executable(cddl_variant_structural_duplicate_tags_compile_fails EXCLUDE_FROM_ALL cddl_variant_structural_duplicate_tags_compile_fail.cc)
target_link_libraries(cddl_variant_structural_duplicate_tags_compile_fails PRIVATE cbor::tags fmt::fmt nameof::nameof)
add_test(
  NAME cddl_variant_structural_duplicate_tags_compile_fails
  COMMAND
    ${CMAKE_COMMAND}
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DTEST_TARGET=cddl_variant_structural_duplicate_tags_compile_fails"
    "-DCONFIG=$<CONFIG>"
    "-DPASS_REGEX=CDDL for variant alternatives with duplicate or catch-all CBOR tag matches is unsupported"
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")

add_executable(cddl_variant_nested_any_tag_compile_fails EXCLUDE_FROM_ALL cddl_variant_nested_any_tag_compile_fail.cc)
target_link_libraries(cddl_variant_nested_any_tag_compile_fails PRIVATE cbor::tags fmt::fmt nameof::nameof)
add_test(
  NAME cddl_variant_nested_any_tag_compile_fails
  COMMAND
    ${CMAKE_COMMAND}
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DTEST_TARGET=cddl_variant_nested_any_tag_compile_fails"
    "-DCONFIG=$<CONFIG>"
    "-DPASS_REGEX=CDDL for variant alternatives with duplicate or catch-all CBOR tag matches is unsupported"
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")

add_executable(cddl_variant_optional_duplicate_tags_compile_fails EXCLUDE_FROM_ALL cddl_variant_optional_duplicate_tags_compile_fail.cc)
target_link_libraries(cddl_variant_optional_duplicate_tags_compile_fails PRIVATE cbor::tags fmt::fmt nameof::nameof)
add_test(
  NAME cddl_variant_optional_duplicate_tags_compile_fails
  COMMAND
    ${CMAKE_COMMAND}
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DTEST_TARGET=cddl_variant_optional_duplicate_tags_compile_fails"
    "-DCONFIG=$<CONFIG>"
    "-DPASS_REGEX=CDDL for variant alternatives with duplicate or catch-all CBOR tag matches is unsupported"
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")

add_executable(cddl_variant_any_tag_compile_fails EXCLUDE_FROM_ALL cddl_variant_any_tag_compile_fail.cc)
target_link_libraries(cddl_variant_any_tag_compile_fails PRIVATE cbor::tags fmt::fmt nameof::nameof)
add_test(
  NAME cddl_variant_any_tag_compile_fails
  COMMAND
    ${CMAKE_COMMAND}
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DTEST_TARGET=cddl_variant_any_tag_compile_fails"
    "-DCONFIG=$<CONFIG>"
    "-DPASS_REGEX=CDDL for variant alternatives with duplicate or catch-all CBOR tag matches is unsupported"
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")

add_executable(cddl_variant_static_tag_compile_fails EXCLUDE_FROM_ALL cddl_variant_static_tag_compile_fail.cc)
target_link_libraries(cddl_variant_static_tag_compile_fails PRIVATE cbor::tags fmt::fmt nameof::nameof)
add_test(
  NAME cddl_variant_static_tag_compile_fails
  COMMAND
    ${CMAKE_COMMAND}
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DTEST_TARGET=cddl_variant_static_tag_compile_fails"
    "-DCONFIG=$<CONFIG>"
    "-DPASS_REGEX=CDDL for variant alternatives with duplicate or catch-all CBOR tag matches is unsupported"
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")

add_executable(cddl_variant_dynamic_tag_compile_fails EXCLUDE_FROM_ALL cddl_variant_dynamic_tag_compile_fail.cc)
target_link_libraries(cddl_variant_dynamic_tag_compile_fails PRIVATE cbor::tags fmt::fmt nameof::nameof)
add_test(
  NAME cddl_variant_dynamic_tag_compile_fails
  COMMAND
    ${CMAKE_COMMAND}
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DTEST_TARGET=cddl_variant_dynamic_tag_compile_fails"
    "-DCONFIG=$<CONFIG>"
    "-DPASS_REGEX=CDDL for variant alternatives with dynamic CBOR tags is unsupported"
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")

add_executable(smart_ptr_variant_unique_value_compile_fails EXCLUDE_FROM_ALL smart_ptr_variant_unique_value_compile_fail.cc)
target_link_libraries(smart_ptr_variant_unique_value_compile_fails PRIVATE cbor::tags)
add_test(
  NAME smart_ptr_variant_unique_value_compile_fails
  COMMAND
    ${CMAKE_COMMAND}
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DTEST_TARGET=smart_ptr_variant_unique_value_compile_fails"
    "-DCONFIG=$<CONFIG>"
    "-DPASS_REGEX=Pointer variant alternatives overlap on the CBOR wire"
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")

add_executable(smart_ptr_variant_array_compile_fails EXCLUDE_FROM_ALL smart_ptr_variant_array_compile_fail.cc)
target_link_libraries(smart_ptr_variant_array_compile_fails PRIVATE cbor::tags)
add_test(
  NAME smart_ptr_variant_array_compile_fails
  COMMAND
    ${CMAKE_COMMAND}
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DTEST_TARGET=smart_ptr_variant_array_compile_fails"
    "-DCONFIG=$<CONFIG>"
    "-DPASS_REGEX=Pointer variant alternatives overlap on the CBOR wire"
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")

add_executable(
  smart_ptr_variant_shared_ptr_vector_array_compile_fails
  EXCLUDE_FROM_ALL
  smart_ptr_variant_shared_ptr_vector_array_compile_fail.cc)
target_link_libraries(smart_ptr_variant_shared_ptr_vector_array_compile_fails PRIVATE cbor::tags)
add_test(
  NAME smart_ptr_variant_shared_ptr_vector_array_compile_fails
  COMMAND
    ${CMAKE_COMMAND}
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DTEST_TARGET=smart_ptr_variant_shared_ptr_vector_array_compile_fails"
    "-DCONFIG=$<CONFIG>"
    "-DPASS_REGEX=variants containing shared pointers require an explicit application codec"
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")

add_executable(smart_ptr_variant_shared_pointer_compile_fails EXCLUDE_FROM_ALL smart_ptr_variant_shared_pointer_compile_fail.cc)
target_link_libraries(smart_ptr_variant_shared_pointer_compile_fails PRIVATE cbor::tags)
add_test(
  NAME smart_ptr_variant_shared_pointer_compile_fails
  COMMAND
    ${CMAKE_COMMAND}
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DTEST_TARGET=smart_ptr_variant_shared_pointer_compile_fails"
    "-DCONFIG=$<CONFIG>"
    "-DPASS_REGEX=variants containing shared pointers require an explicit application codec"
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")

add_executable(smart_ptr_nested_null_compile_fails EXCLUDE_FROM_ALL smart_ptr_nested_null_compile_fail.cc)
target_link_libraries(smart_ptr_nested_null_compile_fails PRIVATE cbor::tags)
add_test(
  NAME smart_ptr_nested_null_compile_fails
  COMMAND
    ${CMAKE_COMMAND}
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DTEST_TARGET=smart_ptr_nested_null_compile_fails"
    "-DCONFIG=$<CONFIG>"
    "-DPASS_REGEX=unique pointer cannot encode a pointee that also has a CBOR null state"
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")

add_executable(smart_ptr_optional_pointer_compile_fails EXCLUDE_FROM_ALL smart_ptr_optional_pointer_compile_fail.cc)
target_link_libraries(smart_ptr_optional_pointer_compile_fails PRIVATE cbor::tags)
add_test(
  NAME smart_ptr_optional_pointer_compile_fails
  COMMAND
    ${CMAKE_COMMAND}
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DTEST_TARGET=smart_ptr_optional_pointer_compile_fails"
    "-DCONFIG=$<CONFIG>"
    "-DPASS_REGEX=std::optional<T> cannot contain a unique pointer null state because both empty states use CBOR null"
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")

add_executable(smart_ptr_optional_shared_pointer_compile_fails EXCLUDE_FROM_ALL smart_ptr_optional_shared_pointer_compile_fail.cc)
target_link_libraries(smart_ptr_optional_shared_pointer_compile_fails PRIVATE cbor::tags)
add_test(
  NAME smart_ptr_optional_shared_pointer_compile_fails
  COMMAND
    ${CMAKE_COMMAND}
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DTEST_TARGET=smart_ptr_optional_shared_pointer_compile_fails"
    "-DCONFIG=$<CONFIG>"
    "-DPASS_REGEX=std::optional<T> cannot contain a smart pointer null state because both empty states use CBOR null"
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")

add_executable(smart_ptr_empty_pointee_compile_fails EXCLUDE_FROM_ALL smart_ptr_empty_pointee_compile_fail.cc)
target_link_libraries(smart_ptr_empty_pointee_compile_fails PRIVATE cbor::tags)
add_test(
  NAME smart_ptr_empty_pointee_compile_fails
  COMMAND
    ${CMAKE_COMMAND}
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DTEST_TARGET=smart_ptr_empty_pointee_compile_fails"
    "-DCONFIG=$<CONFIG>"
    "-DPASS_REGEX=smart pointer pointee must encode exactly one CBOR item"
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")

add_executable(smart_ptr_simple_null_compile_fails EXCLUDE_FROM_ALL smart_ptr_simple_null_compile_fail.cc)
target_link_libraries(smart_ptr_simple_null_compile_fails PRIVATE cbor::tags)
add_test(
  NAME smart_ptr_simple_null_compile_fails
  COMMAND
    ${CMAKE_COMMAND}
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DTEST_TARGET=smart_ptr_simple_null_compile_fails"
    "-DCONFIG=$<CONFIG>"
    "-DPASS_REGEX=unique pointer cannot encode a pointee that also has a CBOR null state"
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")

add_executable(
  cddl_smart_ptr_empty_pointee_compile_fails
  EXCLUDE_FROM_ALL
  cddl_smart_ptr_empty_pointee_compile_fail.cc)
target_link_libraries(cddl_smart_ptr_empty_pointee_compile_fails PRIVATE cbor::tags)
add_test(
  NAME cddl_smart_ptr_empty_pointee_compile_fails
  COMMAND
    ${CMAKE_COMMAND}
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DTEST_TARGET=cddl_smart_ptr_empty_pointee_compile_fails"
    "-DCONFIG=$<CONFIG>"
    "-DPASS_REGEX=CDDL smart pointer pointee must encode exactly one CBOR item"
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")

add_executable(
  cddl_smart_ptr_tag_header_pointee_compile_fails
  EXCLUDE_FROM_ALL
  cddl_smart_ptr_tag_header_pointee_compile_fail.cc)
target_link_libraries(cddl_smart_ptr_tag_header_pointee_compile_fails PRIVATE cbor::tags)
add_test(
  NAME cddl_smart_ptr_tag_header_pointee_compile_fails
  COMMAND
    ${CMAKE_COMMAND}
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DTEST_TARGET=cddl_smart_ptr_tag_header_pointee_compile_fails"
    "-DCONFIG=$<CONFIG>"
    "-DPASS_REGEX=CDDL smart pointer pointee must encode exactly one CBOR item"
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")

add_executable(smart_ptr_tag_header_pointee_compile_fails EXCLUDE_FROM_ALL smart_ptr_tag_header_pointee_compile_fail.cc)
target_link_libraries(smart_ptr_tag_header_pointee_compile_fails PRIVATE cbor::tags)
add_test(
  NAME smart_ptr_tag_header_pointee_compile_fails
  COMMAND
    ${CMAKE_COMMAND}
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DTEST_TARGET=smart_ptr_tag_header_pointee_compile_fails"
    "-DCONFIG=$<CONFIG>"
    "-DPASS_REGEX=smart pointer pointee must encode exactly one CBOR item"
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")

add_executable(
  smart_ptr_unwrapped_multi_item_pointee_compile_fails
  EXCLUDE_FROM_ALL
  smart_ptr_unwrapped_multi_item_pointee_compile_fail.cc)
target_link_libraries(smart_ptr_unwrapped_multi_item_pointee_compile_fails PRIVATE cbor::tags)
add_test(
  NAME smart_ptr_unwrapped_multi_item_pointee_compile_fails
  COMMAND
    ${CMAKE_COMMAND}
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DTEST_TARGET=smart_ptr_unwrapped_multi_item_pointee_compile_fails"
    "-DCONFIG=$<CONFIG>"
    "-DPASS_REGEX=smart pointer pointee must encode exactly one CBOR item"
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")

function(add_typed_array_variant_compile_fail_test TEST_NAME SOURCE_FILE)
  add_executable(${TEST_NAME} EXCLUDE_FROM_ALL ${SOURCE_FILE})
  target_link_libraries(${TEST_NAME} PRIVATE cbor::tags)
  add_test(
    NAME ${TEST_NAME}
    COMMAND
      ${CMAKE_COMMAND}
      "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
      "-DTEST_TARGET=${TEST_NAME}"
      "-DCONFIG=$<CONFIG>"
      "-DPASS_REGEX=Multiple types match against major type 6 \\(tag\\)"
      -P
      "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")
endfunction()

add_typed_array_variant_compile_fail_test(typed_array_variant_static_tag_compile_fails typed_array_variant_static_tag_compile_fail.cc)
add_typed_array_variant_compile_fail_test(typed_array_variant_any_tag_compile_fails typed_array_variant_any_tag_compile_fail.cc)
add_typed_array_variant_compile_fail_test(typed_array_variant_duplicate_tag_compile_fails typed_array_variant_duplicate_tag_compile_fail.cc)

function(add_bounded_fixed_string_compile_fail_test TEST_NAME COMPILE_DEFINITION)
  add_executable(${TEST_NAME} EXCLUDE_FROM_ALL cddl_bounded_fixed_string_compile_fail.cc)
  target_compile_definitions(${TEST_NAME} PRIVATE ${COMPILE_DEFINITION})
  target_link_libraries(${TEST_NAME} PRIVATE cbor::tags fmt::fmt nameof::nameof)
  add_test(
    NAME ${TEST_NAME}
    COMMAND
      ${CMAKE_COMMAND}
      "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
      "-DTEST_TARGET=${TEST_NAME}"
      "-DCONFIG=$<CONFIG>"
      "-DPASS_REGEX=bounded_size fixed string extent must be inside the configured CDDL size bounds"
      -P
      "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")
endfunction()

add_bounded_fixed_string_compile_fail_test(
  cddl_bounded_fixed_byte_array_compile_fails
  CBOR_TAGS_BOUNDED_FIXED_BYTE_ARRAY)
add_bounded_fixed_string_compile_fail_test(
  cddl_bounded_fixed_text_span_compile_fails
  CBOR_TAGS_BOUNDED_FIXED_TEXT_SPAN)

if(CBOR_TAGS_USE_STD_REFLECTION OR CBOR_TAGS_USE_BOOST_PFR_NAMES)
  function(add_named_map_duplicate_keys_compile_fail_test TEST_NAME COMPILE_DEFINITION)
    add_executable(${TEST_NAME} EXCLUDE_FROM_ALL named_map_duplicate_keys_compile_fail.cc)
    target_compile_definitions(${TEST_NAME} PRIVATE ${COMPILE_DEFINITION})
    target_link_libraries(${TEST_NAME} PRIVATE cbor::tags fmt::fmt nameof::nameof)
    add_test(
      NAME ${TEST_NAME}
      COMMAND
        ${CMAKE_COMMAND}
        "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
        "-DTEST_TARGET=${TEST_NAME}"
        "-DCONFIG=$<CONFIG>"
        "-DPASS_REGEX=fixed field names must be unique after flattening as_named_group members"
        -P
        "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")
  endfunction()

  add_named_map_duplicate_keys_compile_fail_test(named_map_duplicate_keys_encode_compile_fails CBOR_TAGS_DUPLICATE_KEYS_ENCODE)
  add_named_map_duplicate_keys_compile_fail_test(named_map_duplicate_keys_decode_compile_fails CBOR_TAGS_DUPLICATE_KEYS_DECODE)
  add_named_map_duplicate_keys_compile_fail_test(named_map_duplicate_keys_cddl_compile_fails CBOR_TAGS_DUPLICATE_KEYS_CDDL)

  function(add_named_map_extension_fields_compile_fail_test TEST_NAME SHAPE_DEFINITION COMPILE_DEFINITION)
    add_executable(${TEST_NAME} EXCLUDE_FROM_ALL named_map_extension_fields_compile_fail.cc)
    target_compile_definitions(${TEST_NAME} PRIVATE ${SHAPE_DEFINITION} ${COMPILE_DEFINITION})
    target_link_libraries(${TEST_NAME} PRIVATE cbor::tags fmt::fmt nameof::nameof)
    add_test(
      NAME ${TEST_NAME}
      COMMAND
        ${CMAKE_COMMAND}
        "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
        "-DTEST_TARGET=${TEST_NAME}"
        "-DCONFIG=$<CONFIG>"
        "-DPASS_REGEX=at most one as_named_extension field after flattening as_named_group members"
        -P
        "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")
  endfunction()

  foreach(SHAPE ROOT_ROOT ROOT_GROUP GROUP_GROUP)
    string(TOLOWER "${SHAPE}" SHAPE_NAME)
    add_named_map_extension_fields_compile_fail_test(
      named_map_extension_fields_${SHAPE_NAME}_encode_compile_fails
      CBOR_TAGS_EXTENSION_FIELDS_${SHAPE}
      CBOR_TAGS_EXTENSION_FIELDS_ENCODE)
    add_named_map_extension_fields_compile_fail_test(
      named_map_extension_fields_${SHAPE_NAME}_decode_compile_fails
      CBOR_TAGS_EXTENSION_FIELDS_${SHAPE}
      CBOR_TAGS_EXTENSION_FIELDS_DECODE)
    add_named_map_extension_fields_compile_fail_test(
      named_map_extension_fields_${SHAPE_NAME}_cddl_compile_fails
      CBOR_TAGS_EXTENSION_FIELDS_${SHAPE}
      CBOR_TAGS_EXTENSION_FIELDS_CDDL)
  endforeach()
endif()

add_executable(test_decode_stack_floor test_decode_stack_floor.cc)
target_link_libraries(test_decode_stack_floor PRIVATE cbor::tags)
if(MSVC)
  target_link_options(test_decode_stack_floor PRIVATE /STACK:8388608)
endif()
add_test(NAME test_decode_stack_floor COMMAND test_decode_stack_floor)
set_tests_properties(test_decode_stack_floor PROPERTIES LABELS resource TIMEOUT 10)

check_cxx_compiler_flag("-funsigned-char" CBOR_TAGS_HAS_FUNSIGNED_CHAR)
if(CBOR_TAGS_HAS_FUNSIGNED_CHAR)
  add_executable(test_unsigned_char test_unsigned_char.cc)
  target_compile_options(test_unsigned_char PRIVATE -funsigned-char)
  target_link_libraries(test_unsigned_char PRIVATE cbor::tags)
  add_test(NAME test_unsigned_char COMMAND test_unsigned_char)
endif()

add_executable(test_smart_ptr_decode_allocations test_smart_ptr_decode_allocations.cc)
target_link_libraries(test_smart_ptr_decode_allocations PRIVATE cbor::tags)
add_test(NAME test_smart_ptr_decode_allocations COMMAND test_smart_ptr_decode_allocations)
add_dependencies(tests test_smart_ptr_decode_allocations)

if(TARGET cbor_tags_cli)
  add_test(
    NAME cbor_tags_cli
    COMMAND
      ${CMAKE_COMMAND}
      "-DCLI=$<TARGET_FILE:cbor_tags_cli>"
      "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/cbor_tags_cli"
      -P
      "${CMAKE_CURRENT_SOURCE_DIR}/test_cli.cmake")
endif()

if(CBOR_TAGS_BUILD_INTEROP_TESTS)
  add_subdirectory(interop)
endif()
