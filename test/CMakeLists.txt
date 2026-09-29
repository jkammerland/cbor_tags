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
cpmaddpackage("gh:jkammerland/doctest@1.0.4")

include(CheckCXXCompilerFlag)
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("#include <boost/circular_buffer.hpp>
#include <boost/container/static_vector.hpp>
int main() {}" CBOR_TAGS_TEST_HAS_BOOST_CAPACITY)
check_cxx_source_compiles("#include <version>
#if !defined(__cpp_lib_inplace_vector) || __cpp_lib_inplace_vector < 202406L
#error inplace_vector unavailable
#endif
#include <inplace_vector>
int main() { std::inplace_vector<int, 4> v; v.push_back(1); }" CBOR_TAGS_TEST_HAS_INPLACE_VECTOR)
message(STATUS "Capacity tests: Boost containers=${CBOR_TAGS_TEST_HAS_BOOST_CAPACITY}, std::inplace_vector=${CBOR_TAGS_TEST_HAS_INPLACE_VECTOR} (empty means unavailable)")

# add executables more if needed
add_executable(tests)

# add target_sources, all .cpp files in this directory
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
target_sources(tests PRIVATE ${TEST_SOURCES})

# add test include directories
target_link_libraries(tests PUBLIC test_util)

# add link libraries
target_link_libraries(tests PRIVATE cbor::tags doctest::doctest fmt::fmt nameof::nameof magic_enum::magic_enum)
target_compile_definitions(tests PRIVATE $<$<PLATFORM_ID:QNX>:DOCTEST_CONFIG_USE_STD_HEADERS>)

# Enable ASAN for linux and macos
option(CBOR_TAGS_ENABLE_ASAN "Enable ASAN in tests, DEFAULT=OFF" OFF)
if(CBOR_TAGS_ENABLE_ASAN)
  if(UNIX OR APPLE)
    message(STATUS "ASAN enabled in tests")
    target_compile_options(tests PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(tests PRIVATE -fsanitize=address)
  else()
    # Decline audibly rather than reporting a sanitizer that was never applied.
    message(WARNING "CBOR_TAGS_ENABLE_ASAN=ON is not supported for ${CMAKE_CXX_COMPILER_ID} on this platform; "
                    "tests will be built WITHOUT AddressSanitizer")
  endif()
endif()

if(CBOR_TAGS_TIDY_TARGET AND CBOR_TAGS_TIDY_TESTS)
  message(STATUS "Tidy enabled in tests")
  target_tidy_sources(tests)
endif()

# add tests for ctest
add_test(NAME tests COMMAND tests "--test-suite-exclude=cbor_wire/*,roundtrip/aggregate_decode,roundtrip/std_expected,roundtrip/container_interoperability,roundtrip/std_value_wrappers,roundtrip/fixed_text_spans,roundtrip/any_headers,roundtrip/headers_visualization,roundtrip/traversal")
add_test(NAME aggregate_decode_wire COMMAND tests "--test-suite=cbor_wire/aggregate_decode")
set_tests_properties(aggregate_decode_wire PROPERTIES LABELS "cbor-wire")
add_test(NAME aggregate_decode_roundtrip COMMAND tests "--test-suite=roundtrip/aggregate_decode")
set_tests_properties(aggregate_decode_roundtrip PROPERTIES LABELS "roundtrip")
add_test(NAME container_interoperability_wire COMMAND tests "--test-suite=cbor_wire/container_interoperability")
set_tests_properties(container_interoperability_wire PROPERTIES LABELS "cbor-wire")
add_test(NAME container_interoperability_roundtrip COMMAND tests "--test-suite=roundtrip/container_interoperability")
set_tests_properties(container_interoperability_roundtrip PROPERTIES LABELS "roundtrip")
add_test(NAME fixed_text_spans_wire COMMAND tests "--test-suite=cbor_wire/fixed_text_spans")
set_tests_properties(fixed_text_spans_wire PROPERTIES LABELS "cbor-wire")
add_test(NAME fixed_text_spans_roundtrip COMMAND tests "--test-suite=roundtrip/fixed_text_spans")
set_tests_properties(fixed_text_spans_roundtrip PROPERTIES LABELS "roundtrip")

include(CheckCXXSourceCompiles)
check_cxx_source_compiles("#include <memory>
#include <version>
#if !defined(__cpp_lib_indirect) || __cpp_lib_indirect < 202502L || !defined(__cpp_lib_polymorphic) || __cpp_lib_polymorphic < 202502L
#error C++26 value wrappers unavailable
#endif
struct value { int number{}; };
int main() { std::indirect<int> a(1); std::polymorphic<value> b; }" CBOR_TAGS_TEST_HAS_STD_VALUE_WRAPPERS)
option(CBOR_TAGS_TEST_REQUIRE_STD_VALUE_WRAPPERS "Require C++26 value-wrapper integration coverage" OFF)
if(CBOR_TAGS_TEST_REQUIRE_STD_VALUE_WRAPPERS AND NOT CBOR_TAGS_TEST_HAS_STD_VALUE_WRAPPERS)
  message(FATAL_ERROR "This integration job requires C++26 std::indirect and std::polymorphic")
endif()
message(STATUS "C++26 value-wrapper tests=${CBOR_TAGS_TEST_HAS_STD_VALUE_WRAPPERS} (empty means unavailable)")

if(CBOR_TAGS_TEST_HAS_STD_VALUE_WRAPPERS)
  add_test(NAME std_value_wrappers_wire COMMAND tests "--test-suite=cbor_wire/std_value_wrappers")
  set_tests_properties(std_value_wrappers_wire PROPERTIES LABELS "cbor-wire")
  add_test(NAME std_value_wrappers_roundtrip COMMAND tests "--test-suite=roundtrip/std_value_wrappers")
  set_tests_properties(std_value_wrappers_roundtrip PROPERTIES LABELS "roundtrip")
  function(add_value_wrapper_compile_fail TEST_NAME SHAPE OPERATION DIAGNOSTIC)
    add_executable(${TEST_NAME} EXCLUDE_FROM_ALL std_value_wrapper_compile_fail.cc)
    target_compile_definitions(${TEST_NAME} PRIVATE CBOR_VALUE_${SHAPE} CBOR_VALUE_${OPERATION})
    target_link_libraries(${TEST_NAME} PRIVATE cbor::tags fmt::fmt nameof::nameof)
    add_test(NAME ${TEST_NAME} COMMAND ${CMAKE_COMMAND}
      "-DBUILD_DIR=${CMAKE_BINARY_DIR}" "-DTEST_TARGET=${TEST_NAME}" "-DCONFIG=$<CONFIG>"
      "-DPASS_REGEX=${DIAGNOSTIC}" -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")
  endfunction()
  add_value_wrapper_compile_fail(std_indirect_direct_mixin_decode_compile_fails DIRECT_MIXIN DECODE "std::indirect payload must encode exactly one CBOR item")
  foreach(SHAPE EMPTY TAG_HEADER UNWRAPPED_GROUP)
    foreach(OPERATION ENCODE DECODE)
      string(TOLOWER "std_indirect_${SHAPE}_${OPERATION}_compile_fails" TEST_NAME)
      add_value_wrapper_compile_fail(${TEST_NAME} ${SHAPE} ${OPERATION} "std::indirect payload must encode exactly one CBOR item")
    endforeach()
  endforeach()
  foreach(SHAPE VARIANT OPTIONAL_VARIANT NESTED_VARIANT)
    foreach(OPERATION ENCODE DECODE)
      string(TOLOWER "std_indirect_${SHAPE}_${OPERATION}_compile_fails" TEST_NAME)
      add_value_wrapper_compile_fail(${TEST_NAME} ${SHAPE} ${OPERATION} "std::indirect variant alternatives require an explicit application wire variant")
    endforeach()
    string(TOLOWER "cddl_std_indirect_${SHAPE}_compile_fails" TEST_NAME)
    add_value_wrapper_compile_fail(${TEST_NAME} ${SHAPE} CDDL "CDDL variants containing wire-type customizations require an explicit wire variant")
  endforeach()
  add_value_wrapper_compile_fail(cddl_std_indirect_header_compile_fails TAG_HEADER CDDL "CDDL wire type must describe a complete CBOR item")
  add_value_wrapper_compile_fail(polymorphic_duplicate_wire_tags_compile_fails DUPLICATE_TAGS DECODE "Multiple types match against major type 6")
  foreach(OPERATION ENCODE DECODE)
    string(TOLOWER "polymorphic_variant_${OPERATION}_compile_fails" TEST_NAME)
    add_value_wrapper_compile_fail(${TEST_NAME} POLYMORPHIC_VARIANT ${OPERATION} "polymorphic variant alternatives require the explicit animal_wire variant")
  endforeach()
  add_value_wrapper_compile_fail(cddl_polymorphic_variant_compile_fails POLYMORPHIC_VARIANT CDDL "CDDL variants containing wire-type customizations require an explicit wire variant")
endif()

if(CMAKE_CXX_STANDARD GREATER_EQUAL 23)
  include(CheckCXXSourceCompiles)
  check_cxx_source_compiles(
    "#include <expected>\nint main() { std::expected<int, int> value; return *value; }"
    CBOR_TAGS_TEST_HAS_STD_EXPECTED)
  if(CBOR_TAGS_TEST_HAS_STD_EXPECTED)
    add_test(NAME std_expected_wire COMMAND tests "--test-suite=cbor_wire/std_expected")
    set_tests_properties(std_expected_wire PROPERTIES LABELS "cbor-wire")
    add_test(NAME std_expected_roundtrip COMMAND tests "--test-suite=roundtrip/std_expected")
    set_tests_properties(std_expected_roundtrip PROPERTIES LABELS "roundtrip")
    foreach(ROUTE HEADER_ROOT HEADER_GROUP HEADER_ARRAY DIRECT_ARRAY DIRECT_OPTIONAL TAG_VARIANT BOUNDED_CONTAINER INDEFINITE_CONTAINER)
      string(TOLOWER "std_expected_${ROUTE}_compile_fails" TEST_NAME)
      add_executable(${TEST_NAME} EXCLUDE_FROM_ALL std_expected_dispatch_compile_fail.cc)
      target_compile_definitions(${TEST_NAME} PRIVATE CBOR_EXPECTED_${ROUTE})
      target_link_libraries(${TEST_NAME} PRIVATE cbor::tags)
      add_test(NAME ${TEST_NAME} COMMAND ${CMAKE_COMMAND}
        "-DBUILD_DIR=${CMAKE_BINARY_DIR}" "-DTEST_TARGET=${TEST_NAME}" "-DCONFIG=$<CONFIG>"
        "-DPASS_REGEX=std::expected value payload must encode exactly one CBOR item"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")
    endforeach()
    foreach(SHAPE EMPTY_VALUE EMPTY_ERROR OPTIONAL_EMPTY CONTAINER_EMPTY BOUNDED_EMPTY UNWRAPPED_GROUP TAG_HEADER CONVERTING_MIXIN GENERIC_MIXIN GENERIC_ARRAY_MIXIN MUTABLE_MIXIN CONST_MIXIN)
      foreach(OPERATION ENCODE DECODE)
        if((SHAPE STREQUAL "MUTABLE_MIXIN" AND OPERATION STREQUAL "DECODE") OR
           (SHAPE STREQUAL "CONST_MIXIN" AND OPERATION STREQUAL "ENCODE"))
          continue()
        endif()
        string(TOLOWER "std_expected_${SHAPE}_${OPERATION}_compile_fails" TEST_NAME)
        add_executable(${TEST_NAME} EXCLUDE_FROM_ALL std_expected_payload_compile_fail.cc)
        target_compile_definitions(${TEST_NAME} PRIVATE CBOR_TAGS_EXPECTED_${SHAPE} CBOR_TAGS_EXPECTED_${OPERATION})
        target_link_libraries(${TEST_NAME} PRIVATE cbor::tags)
        add_test(
          NAME ${TEST_NAME}
          COMMAND
            ${CMAKE_COMMAND}
            "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
            "-DTEST_TARGET=${TEST_NAME}"
            "-DCONFIG=$<CONFIG>"
            "-DPASS_REGEX=std::expected (value|error) payload must encode exactly one CBOR item"
            -P
            "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")
      endforeach()
    endforeach()
  endif()
endif()

# Keep this suite reachable when a branch excludes wire tests from the unit run.
add_test(NAME byte_views_wire COMMAND tests --test-suite=cbor_wire/byte_views)
set_tests_properties(byte_views_wire PROPERTIES LABELS "cbor-wire")

add_test(NAME container_capacity_wire COMMAND tests "--test-suite=cbor_wire/container_capacity")
set_tests_properties(container_capacity_wire PROPERTIES LABELS "cbor-wire")

add_test(NAME any_headers_wire COMMAND tests "--test-suite=cbor_wire/any_headers,cbor_wire/headers_visualization")
set_tests_properties(any_headers_wire PROPERTIES LABELS "cbor-wire")
add_test(NAME any_headers_roundtrip COMMAND tests "--test-suite=roundtrip/any_headers,roundtrip/headers_visualization")
set_tests_properties(any_headers_roundtrip PROPERTIES LABELS "roundtrip")
add_test(NAME status_errors_wire COMMAND tests "--test-suite=cbor_wire/status_errors")
set_tests_properties(status_errors_wire PROPERTIES LABELS "cbor-wire")
add_test(NAME traversal_wire COMMAND tests "--test-suite=cbor_wire/traversal")
set_tests_properties(traversal_wire PROPERTIES LABELS "cbor-wire")
add_test(NAME traversal_roundtrip COMMAND tests "--test-suite=roundtrip/traversal")
set_tests_properties(traversal_roundtrip PROPERTIES LABELS "roundtrip")

add_executable(traversal_callback_result_compile_fails EXCLUDE_FROM_ALL traversal_callback_result_compile_fail.cc)
target_link_libraries(traversal_callback_result_compile_fails PRIVATE cbor::tags)
add_test(
  NAME traversal_callback_result_compile_fails
  COMMAND
    ${CMAKE_COMMAND}
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DTEST_TARGET=traversal_callback_result_compile_fails"
    "-DCONFIG=$<CONFIG>"
    "-DPASS_REGEX=walk_item visitor must return exactly void or status_code"
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")

add_executable(test_smart_ptr_allocation_failure test_smart_ptr_allocation_failure.cc)
target_link_libraries(test_smart_ptr_allocation_failure PRIVATE cbor::tags)
add_test(NAME test_smart_ptr_allocation_failure COMMAND test_smart_ptr_allocation_failure)
add_dependencies(tests test_smart_ptr_allocation_failure)

if(NOT CBOR_TAGS_USE_STD_REFLECTION AND NOT CBOR_TAGS_USE_BOOST_PFR_NAMES)
  function(add_reflection_compile_fail_test TEST_NAME SOURCE_FILE)
    add_executable(${TEST_NAME} EXCLUDE_FROM_ALL ${SOURCE_FILE})
    target_link_libraries(${TEST_NAME} PRIVATE cbor::tags)
    add_test(
      NAME ${TEST_NAME}
      COMMAND
        ${CMAKE_COMMAND}
        "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
        "-DTEST_TARGET=${TEST_NAME}"
        "-DCONFIG=$<CONFIG>"
        "-DPASS_REGEX=Could not reflect this non-empty aggregate"
        -P
        "${CMAKE_CURRENT_SOURCE_DIR}/expect_compile_failure.cmake")
  endfunction()

  add_reflection_compile_fail_test(
    reflection_unsafe_lvalue_probe_compile_fails
    reflection_unsafe_lvalue_probe_compile_fail.cc)
  add_reflection_compile_fail_test(
    reflection_generated_range_overflow_compile_fails
    reflection_generated_range_overflow_compile_fail.cc)
endif()

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
  smart_ptr_variant_aggregate_array_compile_fails
  EXCLUDE_FROM_ALL
  smart_ptr_variant_aggregate_array_compile_fail.cc)
target_link_libraries(smart_ptr_variant_aggregate_array_compile_fails PRIVATE cbor::tags)
add_test(
  NAME smart_ptr_variant_aggregate_array_compile_fails
  COMMAND
    ${CMAKE_COMMAND}
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DTEST_TARGET=smart_ptr_variant_aggregate_array_compile_fails"
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

add_executable(
  smart_ptr_variant_composed_codec_compile_fails
  EXCLUDE_FROM_ALL
  smart_ptr_variant_composed_codec_compile_fail.cc)
target_link_libraries(smart_ptr_variant_composed_codec_compile_fails PRIVATE cbor::tags)
add_test(
  NAME smart_ptr_variant_composed_codec_compile_fails
  COMMAND
    ${CMAKE_COMMAND}
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DTEST_TARGET=smart_ptr_variant_composed_codec_compile_fails"
    "-DCONFIG=$<CONFIG>"
    "-DPASS_REGEX=unique pointer variant alternatives must have codec-independent CBOR wire shapes"
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
elseif(CMAKE_SYSTEM_NAME STREQUAL "QNX")
  target_link_options(test_decode_stack_floor PRIVATE "LINKER:-z,stack-size=8388608")
endif()
add_test(NAME test_decode_stack_floor COMMAND test_decode_stack_floor)
set_tests_properties(test_decode_stack_floor PROPERTIES LABELS resource TIMEOUT 10)

check_cxx_compiler_flag("-funsigned-char" CBOR_TAGS_HAS_FUNSIGNED_CHAR)
if(CBOR_TAGS_HAS_FUNSIGNED_CHAR)
  add_executable(test_unsigned_char test_unsigned_char.cc)
  target_compile_options(test_unsigned_char PRIVATE -funsigned-char)
  target_link_libraries(test_unsigned_char PRIVATE cbor::tags test_util)
  add_test(NAME test_unsigned_char COMMAND test_unsigned_char)
endif()

add_executable(test_smart_ptr_expected_classification test_smart_ptr_expected_classification.cc)
target_link_libraries(test_smart_ptr_expected_classification PRIVATE cbor::tags)
set_target_properties(
  test_smart_ptr_expected_classification
  PROPERTIES CXX_STANDARD 23
             CXX_STANDARD_REQUIRED ON
             CXX_EXTENSIONS OFF)
add_test(NAME test_smart_ptr_expected_classification COMMAND test_smart_ptr_expected_classification)
add_dependencies(tests test_smart_ptr_expected_classification)

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

include(CheckCXXSourceCompiles)
set(CBOR_TAGS_TEST_BOOST_ROOT "" CACHE PATH "Optional Boost header root for container integration tests")
option(CBOR_TAGS_TEST_REQUIRE_RECENT_BOOST "Require Boost 1.92 container integration coverage" OFF)
set(CMAKE_REQUIRED_INCLUDES "${CBOR_TAGS_TEST_BOOST_ROOT}")
check_cxx_source_compiles("#include <boost/container/slist.hpp>
int main() {}" CBOR_TAGS_TEST_HAS_BOOST_CONTAINERS)
check_cxx_source_compiles("#include <boost/version.hpp>
#include <boost/container/hub.hpp>
#include <boost/container/segtor.hpp>
static_assert(BOOST_VERSION >= 109200);
int main() {}" CBOR_TAGS_TEST_HAS_RECENT_BOOST)
unset(CMAKE_REQUIRED_INCLUDES)
if(CBOR_TAGS_TEST_REQUIRE_RECENT_BOOST AND NOT CBOR_TAGS_TEST_HAS_RECENT_BOOST)
  message(FATAL_ERROR "This integration job requires Boost 1.92 with hub and segtor")
endif()
message(STATUS "Container tests: Boost=${CBOR_TAGS_TEST_HAS_BOOST_CONTAINERS}, Boost 1.92=${CBOR_TAGS_TEST_HAS_RECENT_BOOST} (empty means unavailable)")
foreach(FEATURE flat_set flat_map inplace_vector hive)
  check_cxx_source_compiles("#include <version>
#ifndef __cpp_lib_${FEATURE}
#error ${FEATURE} unavailable
#endif
#include <${FEATURE}>
int main() {}" CBOR_TAGS_TEST_HAS_STD_${FEATURE})
  message(STATUS "Container tests: std::${FEATURE}=${CBOR_TAGS_TEST_HAS_STD_${FEATURE}} (empty means unavailable)")
endforeach()
if(CBOR_TAGS_TEST_BOOST_ROOT)
  target_include_directories(tests SYSTEM PRIVATE "${CBOR_TAGS_TEST_BOOST_ROOT}")
endif()
