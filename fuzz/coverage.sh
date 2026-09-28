#!/usr/bin/env bash
set -euo pipefail

if [[ $# != 2 ]]; then
  echo "Usage: fuzz/coverage.sh <instrumented-binary> <campaign-output>" >&2
  exit 2
fi
binary="$(realpath "$1")"
report="$(realpath "$2")"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
shopt -s nullglob
profiles=("$report"/*.profraw)
if ((${#profiles[@]} == 0)); then
  echo "No profiles: configure with -DCBOR_FUZZ_COVERAGE=ON and run a campaign." >&2
  exit 1
fi
"${LLVM_PROFDATA:-llvm-profdata}" merge -sparse "${profiles[@]}" -o "$report/coverage.profdata"
# Restrict the denominator to library source, excluding framework, STL and harness.
mapfile -t sources < <(rg --files "$root/include/cbor_tags" -g '*.h')
"${LLVM_COV:-llvm-cov}" report "$binary" -instr-profile="$report/coverage.profdata" "${sources[@]}" \
  > "$report/coverage.txt"
"${LLVM_COV:-llvm-cov}" show "$binary" -instr-profile="$report/coverage.profdata" "${sources[@]}" \
  -format=html -output-dir="$report/html" -show-branches=count
cat "$report/coverage.txt"
