#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
proof_dir="$root/results/cbor-range-finding-proofs"
build_dir="${TMPDIR:-/tmp}/cbor-range-finding-proofs"
mkdir -p "$build_dir"

cxx="${CXX:-c++}"
common_flags=(
  -std=gnu++20
  -I"$root/include"
  -I"$root/build/default/include"
  -I"$root/build/default/cpm_cache/expected/4981/include"
)

pass() {
  printf 'PASS: %s\n' "$1"
}

fail() {
  printf 'FAIL: %s\n' "$1" >&2
  exit 1
}

"$cxx" "${common_flags[@]}" "$proof_dir/proof_map_range_triple.cpp" -o "$build_dir/proof_map_range_triple"
"$build_dir/proof_map_range_triple"
pass "tuple-like map entries with arity > 2 are accepted and truncated"

if "$cxx" "${common_flags[@]}" "$proof_dir/compile_fail_rvalue_range.cpp" -o "$build_dir/compile_fail_rvalue_range" \
  >"$build_dir/compile_fail_rvalue_range.log" 2>&1; then
  fail "rvalue owning range wrapper unexpectedly compiled"
else
  pass "rvalue owning range wrapper compile failure reproduced"
fi

if "$cxx" "${common_flags[@]}" "$proof_dir/compile_fail_subrange_encoder.cpp" -o "$build_dir/compile_fail_subrange_encoder" \
  >"$build_dir/compile_fail_subrange_encoder.log" 2>&1; then
  fail "subrange encoder unexpectedly compiled"
else
  pass "ValidCborBuffer/subrange encoder mismatch compile failure reproduced"
fi

"$cxx" "${common_flags[@]}" "$proof_dir/proof_scanner_bad_alloc.cpp" -o "$build_dir/proof_scanner_bad_alloc"
"$build_dir/proof_scanner_bad_alloc"
pass "scanner allocation and escaping bad_alloc reproduced"

"$cxx" "${common_flags[@]}" "$proof_dir/proof_payload_decoder_decode_type.cpp" -o "$build_dir/proof_payload_decoder_decode_type"
"$build_dir/proof_payload_decoder_decode_type"
pass "payload decoder raw status_code decode path reproduced"

"$cxx" "${common_flags[@]}" -O0 -g -fsanitize=address -fsanitize-address-use-after-scope \
  "$proof_dir/proof_find_tags_temporary_dangles.cpp" -o "$build_dir/proof_find_tags_temporary_dangles"
if "$build_dir/proof_find_tags_temporary_dangles" >"$build_dir/proof_find_tags_temporary_dangles.log" 2>&1; then
  fail "temporary find_tags dangling proof unexpectedly ran cleanly under ASan"
else
  pass "temporary find_tags dangling proof triggered ASan"
fi

printf '\nProof logs are in %s\n' "$build_dir"
