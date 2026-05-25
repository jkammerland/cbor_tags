set(CBOR_TAGS_INTEROP_NLOHMANN
    "AUTO"
    CACHE STRING "Build nlohmann/json CBOR interop tests: AUTO, ON, or OFF")
set(CBOR_TAGS_INTEROP_CBOR_DIAG
    "AUTO"
    CACHE STRING "Build Rust cbor-diag interop tests: AUTO, ON, or OFF")
set_property(CACHE CBOR_TAGS_INTEROP_NLOHMANN PROPERTY STRINGS AUTO ON OFF)
set_property(CACHE CBOR_TAGS_INTEROP_CBOR_DIAG PROPERTY STRINGS AUTO ON OFF)

function(cbor_tags_validate_interop_mode MODE_VARIABLE)
  if(NOT "${${MODE_VARIABLE}}" MATCHES "^(AUTO|ON|OFF)$")
    message(FATAL_ERROR "${MODE_VARIABLE} must be AUTO, ON, or OFF.")
  endif()
endfunction()

cbor_tags_validate_interop_mode(CBOR_TAGS_INTEROP_NLOHMANN)
cbor_tags_validate_interop_mode(CBOR_TAGS_INTEROP_CBOR_DIAG)

if(NOT CBOR_TAGS_INTEROP_NLOHMANN STREQUAL "OFF")
  find_package(nlohmann_json 3.11 CONFIG QUIET)
  if((NOT TARGET nlohmann_json::nlohmann_json) AND CBOR_TAGS_INTEROP_NLOHMANN STREQUAL "ON")
    cpmaddpackage("gh:nlohmann/json@3.12.0")
  endif()

  if(TARGET nlohmann_json::nlohmann_json)
    add_executable(cbor_tags_interop_nlohmann_cbor nlohmann_cbor_roundtrip.cc)
    target_link_libraries(cbor_tags_interop_nlohmann_cbor PRIVATE cbor::tags doctest::doctest nlohmann_json::nlohmann_json)
    add_test(NAME interop_nlohmann_cbor COMMAND cbor_tags_interop_nlohmann_cbor)
  elseif(CBOR_TAGS_INTEROP_NLOHMANN STREQUAL "ON")
    message(FATAL_ERROR "CBOR_TAGS_INTEROP_NLOHMANN=ON requires nlohmann_json::nlohmann_json.")
  else()
    message(STATUS "Skipping nlohmann/json CBOR interop tests; nlohmann_json was not found.")
  endif()
endif()

if(NOT CBOR_TAGS_INTEROP_CBOR_DIAG STREQUAL "OFF")
  find_program(CBOR_TAGS_CARGO_EXECUTABLE cargo)
  if(CBOR_TAGS_CARGO_EXECUTABLE)
    set(CBOR_TAGS_CBOR_DIAG_VECTOR_FILE "${CMAKE_CURRENT_BINARY_DIR}/cbor_diag_vectors.tsv")

    add_executable(cbor_tags_interop_cbor_diag_vectors cbor_diag_vectors.cc)
    target_link_libraries(cbor_tags_interop_cbor_diag_vectors PRIVATE cbor::tags)

    add_custom_command(
      OUTPUT "${CBOR_TAGS_CBOR_DIAG_VECTOR_FILE}"
      COMMAND cbor_tags_interop_cbor_diag_vectors "${CBOR_TAGS_CBOR_DIAG_VECTOR_FILE}"
      DEPENDS cbor_tags_interop_cbor_diag_vectors
      VERBATIM)
    add_custom_target(cbor_tags_interop_cbor_diag_vectors_file ALL DEPENDS "${CBOR_TAGS_CBOR_DIAG_VECTOR_FILE}")

    add_test(
      NAME interop_cbor_diag
      COMMAND
        ${CMAKE_COMMAND}
        -E
        env
        "CARGO_TARGET_DIR=${CMAKE_CURRENT_BINARY_DIR}/cargo-target"
        "${CBOR_TAGS_CARGO_EXECUTABLE}"
        run
        --locked
        --quiet
        --manifest-path
        "${CMAKE_CURRENT_SOURCE_DIR}/rust_cbor_diag/Cargo.toml"
        --
        validate-vector-file
        "${CBOR_TAGS_CBOR_DIAG_VECTOR_FILE}")
  elseif(CBOR_TAGS_INTEROP_CBOR_DIAG STREQUAL "ON")
    message(FATAL_ERROR "CBOR_TAGS_INTEROP_CBOR_DIAG=ON requires cargo.")
  else()
    message(STATUS "Skipping Rust cbor-diag interop tests; cargo was not found.")
  endif()
endif()
