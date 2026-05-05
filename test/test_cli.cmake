if(NOT DEFINED CLI)
  message(FATAL_ERROR "CLI variable must point to cbor_tags_cli")
endif()

if(NOT DEFINED WORK)
  set(WORK "${CMAKE_CURRENT_BINARY_DIR}/cbor_tags_cli")
endif()

file(MAKE_DIRECTORY "${WORK}")

function(check_contains NAME TEXT NEEDLE KIND)
  if(NEEDLE STREQUAL "__skip__")
    return()
  endif()
  string(FIND "${TEXT}" "${NEEDLE}" POSITION)
  if(POSITION EQUAL -1)
    message(FATAL_ERROR "${NAME}: expected ${KIND} to contain [${NEEDLE}], got [${TEXT}]")
  endif()
endfunction()

function(run_cli NAME EXPECTED_EXIT STDOUT_CONTAINS STDERR_CONTAINS)
  execute_process(
    COMMAND "${CLI}" ${ARGN}
    RESULT_VARIABLE RESULT
    OUTPUT_VARIABLE STDOUT
    ERROR_VARIABLE STDERR)
  if(NOT RESULT EQUAL EXPECTED_EXIT)
    message(FATAL_ERROR "${NAME}: expected exit ${EXPECTED_EXIT}, got ${RESULT}; stdout=[${STDOUT}], stderr=[${STDERR}]")
  endif()
  check_contains("${NAME}" "${STDOUT}" "${STDOUT_CONTAINS}" "stdout")
  check_contains("${NAME}" "${STDERR}" "${STDERR_CONTAINS}" "stderr")
endfunction()

function(run_cli_stdin NAME EXPECTED_EXIT STDOUT_CONTAINS STDERR_CONTAINS STDIN_TEXT)
  set(STDIN_FILE "${WORK}/${NAME}.stdin")
  file(WRITE "${STDIN_FILE}" "${STDIN_TEXT}")
  execute_process(
    COMMAND "${CLI}" ${ARGN}
    INPUT_FILE "${STDIN_FILE}"
    RESULT_VARIABLE RESULT
    OUTPUT_VARIABLE STDOUT
    ERROR_VARIABLE STDERR)
  if(NOT RESULT EQUAL EXPECTED_EXIT)
    message(FATAL_ERROR "${NAME}: expected exit ${EXPECTED_EXIT}, got ${RESULT}; stdout=[${STDOUT}], stderr=[${STDERR}]")
  endif()
  check_contains("${NAME}" "${STDOUT}" "${STDOUT_CONTAINS}" "stdout")
  check_contains("${NAME}" "${STDERR}" "${STDERR_CONTAINS}" "stderr")
endfunction()

set(COMMENTED_HEX [=[
bf
  63 46756e # "Fun"
  f5
  63 416d74 # "Amt"
  21
ff
]=])

run_cli(
  annotate_hex
  0
  "bf           # map(*)"
  "__skip__"
  annotate --input hex --annotation-column 13 bf6346756ef563416d7421ff)
run_cli(
  annotate_hex_comments
  0
  "# map(*)"
  "__skip__"
  annotate --input hex "${COMMENTED_HEX}")
run_cli(
  annotate_no_annotation
  0
  "6869"
  "__skip__"
  annotate --input hex --mode no_annotation 626869)
run_cli(
  diagnostic_base64_standard
  0
  "\"hi\""
  "__skip__"
  diagnostic --input base64 Ymhp)
run_cli(
  diagnostic_base64_standard_padded_one_byte
  0
  "[1]"
  "__skip__"
  diagnostic --input base64 --no-format-by-rows AQ==)
run_cli(
  diagnostic_base64_standard_padded_two_bytes
  0
  "[1, 2]"
  "__skip__"
  diagnostic --input base64 --no-format-by-rows AQI=)
run_cli(
  diagnostic_base64_standard_unpadded_one_byte
  0
  "[1]"
  "__skip__"
  diagnostic --input base64 --no-format-by-rows AQ)
run_cli(
  diagnostic_base64_standard_unpadded_two_bytes
  0
  "[1, 2]"
  "__skip__"
  diagnostic --input base64 --no-format-by-rows AQI)
run_cli(
  diagnostic_base64_whitespace
  0
  "\"hi\""
  "__skip__"
  diagnostic --input base64 "Y m
h	p")
run_cli(
  annotate_base64url_unpadded_dash_argument
  0
  "fa 7f800000"
  "__skip__"
  annotate --input base64 -- -n-AAAA)
run_cli(
  annotate_base64url_padded_dash_argument
  0
  "fa 7f800000"
  "__skip__"
  annotate --input base64 -- -n-AAAA=)
run_cli(
  annotate_base64url_underscore_argument
  0
  "# array(*)"
  "__skip__"
  annotate --input base64 nwH_)
run_cli_stdin(
  annotate_base64url_stdin
  0
  "# map(*)"
  "__skip__"
  "v2NGdW71Y0FtdCH_
"
  annotate --input base64 -)
run_cli_stdin(
  diagnostic_base64_stdin_implicit
  0
  "\"hi\""
  "__skip__"
  "Ymhp
"
  diagnostic --input base64)
run_cli(
  diagnostic_utf8_check
  0
  "[non-utf8(2)]"
  "__skip__"
  diagnostic --input hex --no-format-by-rows --check-tstr-utf8 62c328)

run_cli(missing_input 2 "__skip__" "error: missing required --input" diagnostic Ymhp)
run_cli(invalid_subcommand 2 "__skip__" "error: expected subcommand" bogus --input hex 01)
run_cli(annotate_rejects_diagnostic_flag 2 "__skip__" "error: unknown option '--check-tstr-utf8'" annotate --input hex --check-tstr-utf8 01)
run_cli(odd_hex 1 "__skip__" "error: hex input has odd number of digits" diagnostic --input hex 1)
run_cli(invalid_hex_char 1 "__skip__" "error: hex input contains non-hex character" diagnostic --input hex 0g)
run_cli(malformed_cbor 1 "__skip__" "error: Malformed CBOR diagnostic top-level item" diagnostic --input hex 18)
run_cli(invalid_base64_char 1 "__skip__" "error: base64 input contains invalid character" diagnostic --input base64 "Ym?p")
run_cli(invalid_base64_middle_padding 1 "__skip__" "error: base64 padding must be at the end" diagnostic --input base64 "Ym=hp")
run_cli(invalid_base64_data_after_padding 1 "__skip__" "error: base64 padding must be at the end" diagnostic --input base64 "Yg==AA")
run_cli(invalid_base64_excess_padding 1 "__skip__" "error: base64 padding may contain at most two" diagnostic --input base64 "Yg===")
run_cli(invalid_base64_padded_length 1 "__skip__" "error: padded base64 length must be a multiple of four" diagnostic --input base64 "Yg=")
run_cli(invalid_base64_length_mod_one 1 "__skip__" "error: base64 input length is invalid" diagnostic --input base64 A)
run_cli(invalid_base64_padded_trailing_bits 1 "__skip__" "error: base64 input has non-zero trailing bits" diagnostic --input base64 "AB==")
run_cli(invalid_base64_unpadded_trailing_bits 1 "__skip__" "error: base64 input has non-zero trailing bits" diagnostic --input base64 "AAB")
