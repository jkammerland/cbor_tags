if(NOT DEFINED CLI)
  message(FATAL_ERROR "CLI variable must point to cborctl")
endif()

get_filename_component(CLI_NAME "${CLI}" NAME)

if(NOT DEFINED WORK)
  set(WORK "${CMAKE_CURRENT_BINARY_DIR}/cborctl")
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

run_cli(
  help
  0
  "${CLI_NAME} annotate --input hex|base64 [DATA|-] [options]"
  "__skip__"
  --help)
run_cli(
  subcommand_help
  0
  "${CLI_NAME} diagnostic --input hex|base64 [DATA|-] [options]"
  "__skip__"
  diagnostic --help)

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
  annotate_no_annotation_payload_bytes
  0
  "41\n   1c\n61\n   1c\n"
  "__skip__"
  annotate --input hex --mode no_annotation 411c611c)
run_cli(
  annotate_no_annotation_indefinite
  0
  "9f\n   41\n      1c\n   ff\n"
  "__skip__"
  annotate --input hex --mode no_annotation 9f411cff)
run_cli(
  annotate_numeric_options
  0
  "01"
  "__skip__"
  annotate --input hex --mode no_annotation --current-indent 1 --offset 2 --max-bytes-per-line 1 --annotation-column 16 --indent-width 3 --comment-indent-width 4 --max-structure-depth 8 0102)
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
run_cli(
  diagnostic_row_options
  0
  "[
"
  "__skip__"
  diagnostic --input hex --format-by-rows --override-array-by-columns --row-offset 2 --row-current-indent 1 --max-depth 10 --current-depth 0 8101)
run_cli(
  diagnostic_no_override_array_by_columns
  0
  "[1]"
  "__skip__"
  diagnostic --input hex --no-format-by-rows --no-override-array-by-columns 8101)

run_cli(missing_input 2 "__skip__" "error: missing required --input" diagnostic Ymhp)
run_cli(missing_value_input 2 "__skip__" "error: --input requires a value" diagnostic --input)
run_cli(missing_value_mode 2 "__skip__" "error: --mode requires a value" annotate --input hex --mode)
run_cli(missing_value_annotation_column 2 "__skip__" "error: --annotation-column requires a value" annotate --input hex --annotation-column)
run_cli(missing_value_row_offset 2 "__skip__" "error: --row-offset requires a value" diagnostic --input hex --row-offset)
run_cli(invalid_subcommand 2 "__skip__" "error: expected subcommand" bogus --input hex 01)
run_cli(invalid_input_value 2 "__skip__" "error: --input expects 'hex' or 'base64'" diagnostic --input bogus 01)
run_cli(invalid_mode_value 2 "__skip__" "error: --mode expects 'smart' or 'no_annotation'" annotate --input hex --mode bogus 01)
run_cli(invalid_annotation_column_value 2 "__skip__" "error: --annotation-column expects an unsigned integer" annotate --input hex --annotation-column nope 01)
# Annotation byte width and diagnostic nesting depth are separate options.
run_cli(annotate_rejects_old_depth_name 2 "__skip__" "error: unknown option '--max-depth'" annotate --input hex --max-depth 1 01)
run_cli(diagnostic_rejects_annotation_width 2 "__skip__" "error: unknown option '--max-bytes-per-line'" diagnostic --input hex --max-bytes-per-line 1 01)
run_cli(annotate_rejects_diagnostic_flag 2 "__skip__" "error: unknown option '--check-tstr-utf8'" annotate --input hex --check-tstr-utf8 01)
run_cli(odd_hex 1 "__skip__" "error: hex input has odd number of digits" diagnostic --input hex 1)
run_cli(invalid_hex_char 1 "__skip__" "error: hex input contains non-hex character" diagnostic --input hex 0g)
run_cli(malformed_cbor 1 "__skip__" "error: Malformed CBOR diagnostic top-level item" diagnostic --input hex 18)
run_cli(no_annotation_rejects_truncated_uint 1 "__skip__" "error: Malformed CBOR input" annotate --input hex --mode no_annotation 18)
run_cli(no_annotation_rejects_break 1 "__skip__" "error: Malformed CBOR input" annotate --input hex --mode no_annotation ff)
run_cli(no_annotation_rejects_trailing_break 1 "__skip__" "error: Malformed CBOR input" annotate --input hex --mode no_annotation 01ff)
run_cli(no_annotation_rejects_truncated_array 1 "__skip__" "error: Malformed CBOR input" annotate --input hex --mode no_annotation 81)
run_cli(no_annotation_rejects_odd_map 1 "__skip__" "error: Malformed CBOR input" annotate --input hex --mode no_annotation bf01ff)
run_cli(invalid_base64_char 1 "__skip__" "error: base64 input contains invalid character" diagnostic --input base64 "Ym?p")
run_cli(invalid_base64_middle_padding 1 "__skip__" "error: base64 padding must be at the end" diagnostic --input base64 "Ym=hp")
run_cli(invalid_base64_data_after_padding 1 "__skip__" "error: base64 padding must be at the end" diagnostic --input base64 "Yg==AA")
run_cli(invalid_base64_excess_padding 1 "__skip__" "error: base64 padding may contain at most two" diagnostic --input base64 "Yg===")
run_cli(invalid_base64_padded_length 1 "__skip__" "error: padded base64 length must be a multiple of four" diagnostic --input base64 "Yg=")
run_cli(invalid_base64_length_mod_one 1 "__skip__" "error: base64 input length is invalid" diagnostic --input base64 A)
run_cli(invalid_base64_padded_trailing_bits 1 "__skip__" "error: base64 input has non-zero trailing bits" diagnostic --input base64 "AB==")
run_cli(invalid_base64_unpadded_trailing_bits 1 "__skip__" "error: base64 input has non-zero trailing bits" diagnostic --input base64 "AAB")

# Limits cover collected source bytes, decoded bytes, and the final stdout newline.
function(run_bounded_cli NAME EXPECTED_EXIT EXPECTED_STDOUT STDERR_CONTAINS SOURCE)
  set(STDIN_FILE "${WORK}/${NAME}.stdin")
  file(WRITE "${STDIN_FILE}" "${SOURCE}")
  execute_process(
    COMMAND "${CLI}" ${ARGN}
    INPUT_FILE "${STDIN_FILE}"
    RESULT_VARIABLE RESULT
    OUTPUT_VARIABLE STDOUT
    ERROR_VARIABLE STDERR)
  if(NOT RESULT EQUAL EXPECTED_EXIT OR NOT STDOUT STREQUAL EXPECTED_STDOUT)
    message(FATAL_ERROR "${NAME}: expected exit ${EXPECTED_EXIT}, stdout=[${EXPECTED_STDOUT}]; got ${RESULT}, stdout=[${STDOUT}], stderr=[${STDERR}]")
  endif()
  check_contains("${NAME}" "${STDERR}" "${STDERR_CONTAINS}" "stderr")
endfunction()

run_bounded_cli(source_exact 0 "[1]\n" "__skip__" "01 #x" diagnostic --input hex --no-format-by-rows --max-source-size 5)
run_bounded_cli(source_one_over 1 "" "source size limit exceeded" "01 #x" diagnostic --input hex --no-format-by-rows --max-source-size 4)
run_bounded_cli(annotate_source_over 1 "" "source size limit exceeded" "01" annotate --input hex --max-source-size 1)
run_bounded_cli(positional_exact 0 "[1, 2]\n" "__skip__" "" diagnostic --input hex --no-format-by-rows --max-source-size 5 01 02)
run_bounded_cli(positional_one_over 1 "" "source size limit exceeded" "" diagnostic --input hex --no-format-by-rows --max-source-size 4 01 02)
run_bounded_cli(source_zero 0 "[]\n" "__skip__" "" diagnostic --input hex --no-format-by-rows --max-source-size 0)
run_bounded_cli(source_zero_nonempty 1 "" "source size limit exceeded" " " diagnostic --input hex --max-source-size 0)

string(REPEAT "x" 4093 CHUNK_COMMENT)
set(CHUNK_SOURCE "#${CHUNK_COMMENT}\n01")
run_bounded_cli(source_chunk_exact 0 "[1]\n" "__skip__" "${CHUNK_SOURCE}" diagnostic --input hex --no-format-by-rows --max-source-size 4097)
run_bounded_cli(source_chunk_over 1 "" "source size limit exceeded" "${CHUNK_SOURCE}" diagnostic --input hex --no-format-by-rows --max-source-size 4096)

run_bounded_cli(hex_input_exact 0 "[1, 2]\n" "__skip__" "01 02 #ignored" diagnostic --input hex --no-format-by-rows --max-input-size 2)
run_bounded_cli(hex_input_over 1 "" "input size limit exceeded" "01 02" diagnostic --input hex --max-input-size 1)
run_bounded_cli(base64_input_exact 0 "[1, 2]\n" "__skip__" "A Q I =" diagnostic --input base64 --no-format-by-rows --max-input-size 2)
run_bounded_cli(base64_input_over 1 "" "input size limit exceeded" "AQI=" diagnostic --input base64 --max-input-size 1)
run_bounded_cli(input_zero 0 "[]\n" "__skip__" "#ignored" diagnostic --input hex --no-format-by-rows --max-input-size 0)
run_bounded_cli(input_zero_nonempty 1 "" "input size limit exceeded" "AA==" diagnostic --input base64 --max-input-size 0)
run_bounded_cli(plain_input_over 1 "" "input size limit exceeded" "0102" annotate --input hex --mode no_annotation --max-input-size 1)
run_bounded_cli(smart_input_over 1 "" "input size limit exceeded" "0102" annotate --input hex --max-input-size 1)
run_bounded_cli(smart_zero_empty 0 "" "__skip__" "" annotate --input hex --max-input-size 0 --max-output-size 0)

run_bounded_cli(diagnostic_output_exact 0 "[1]\n" "__skip__" "01" diagnostic --input hex --no-format-by-rows --max-output-size 4)
run_bounded_cli(diagnostic_newline_over 1 "" "output size limit exceeded" "01" diagnostic --input hex --no-format-by-rows --max-output-size 3)

# Closing-row padding and the final stdout newline both count toward the limit.
run_bounded_cli(diagnostic_row_padding_exact 0 "[\n[\n    1\n  ]\n]\n" "__skip__" "8101"
  diagnostic --input hex --format-by-rows --no-override-array-by-columns --row-offset 2 --row-current-indent 1 --max-output-size 16)
run_bounded_cli(diagnostic_row_padding_over 1 "" "output size limit exceeded" "8101"
  diagnostic --input hex --format-by-rows --no-override-array-by-columns --row-offset 2 --row-current-indent 1 --max-output-size 15)

run_bounded_cli(plain_output_exact 0 "01\n" "__skip__" "01" annotate --input hex --mode no_annotation --max-output-size 3)
run_bounded_cli(plain_output_over 1 "" "output size limit exceeded" "01" annotate --input hex --mode no_annotation --max-output-size 2)
run_bounded_cli(plain_output_zero 0 "" "__skip__" "" annotate --input hex --mode no_annotation --max-output-size 0)
run_bounded_cli(diagnostic_output_zero 1 "" "output size limit exceeded" "" diagnostic --input hex --max-output-size 0)
run_bounded_cli(smart_output_exact 0 "01 # unsigned(1)\n" "__skip__" "01" annotate --input hex --annotation-column 3 --max-output-size 17)
run_bounded_cli(smart_output_over 1 "" "output size limit exceeded" "01" annotate --input hex --annotation-column 3 --max-output-size 16)

# Huge requested padding must fail before allocating it, including overflow cases.
run_bounded_cli(plain_huge_indent 1 "" "output size limit exceeded" "01" annotate --input hex --mode no_annotation --current-indent 1000000000 --max-output-size 32)
run_bounded_cli(diagnostic_huge_indent 1 "" "output size limit exceeded" "8101" diagnostic --input hex --row-offset 1000000000 --max-output-size 32)
run_bounded_cli(smart_huge_indent 1 "" "output size limit exceeded" "01" annotate --input hex --current-indent 1000000000 --annotation-column 4000000000 --max-output-size 32)
if(CLI_SIZEOF_SIZE_T EQUAL 4)
  set(CLI_SIZE_MAX 4294967295)
else()
  set(CLI_SIZE_MAX 18446744073709551615)
endif()
run_bounded_cli(plain_indent_overflow 1 "" "size overflow" "01" annotate --input hex --mode no_annotation --current-indent ${CLI_SIZE_MAX} --max-output-size 32)
run_bounded_cli(diagnostic_indent_overflow 1 "" "size overflow" "8101" diagnostic --input hex --row-current-indent ${CLI_SIZE_MAX} --max-output-size 32)
run_bounded_cli(diagnostic_no_extra_depth_cap 0 "[1]\n" "__skip__" "01" diagnostic --input hex --no-format-by-rows --max-depth 2048 --current-depth 1025)
run_bounded_cli(invalid_base64_padding_no_stdout 1 "" "padding must be at the end" "Yg==AA" diagnostic --input base64 --max-output-size 32)
run_bounded_cli(invalid_base64_tail_no_stdout 1 "" "non-zero trailing bits" "AB==" diagnostic --input base64 --max-output-size 32)
run_bounded_cli(late_malformed_no_stdout 1 "" "Malformed CBOR" "0118" diagnostic --input hex --max-output-size 32)

# No rendering mode imposes a default byte cap.
function(check_unlimited_output NAME)
  set(OUTPUT_FILE "${WORK}/${NAME}.stdout")
  execute_process(COMMAND "${CLI}" ${ARGN} RESULT_VARIABLE RESULT OUTPUT_FILE "${OUTPUT_FILE}" ERROR_VARIABLE STDERR)
  file(SIZE "${OUTPUT_FILE}" OUTPUT_SIZE)
  if(NOT RESULT EQUAL 0 OR NOT OUTPUT_SIZE GREATER 16777216)
    message(FATAL_ERROR "${NAME}: expected successful output above 16 MiB; exit=${RESULT}, bytes=${OUTPUT_SIZE}, stderr=[${STDERR}]")
  endif()
endfunction()

check_unlimited_output(plain_unlimited annotate --input hex --mode no_annotation --current-indent 8388608 01)
check_unlimited_output(diagnostic_unlimited diagnostic --input hex --row-offset 16777216 8101)
check_unlimited_output(smart_unlimited annotate --input hex --annotation-column 16777217 01)
