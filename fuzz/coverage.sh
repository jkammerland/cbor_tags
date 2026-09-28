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
  echo "No profiles: build with --config=coverage and run a campaign." >&2
  exit 1
fi
"${LLVM_PROFDATA:-llvm-profdata}" merge -sparse "${profiles[@]}" -o "$report/coverage.profdata"

# Bazel records virtual include paths. Map those back to the checked-out headers.
"${LLVM_COV:-llvm-cov}" export "$binary" -instr-profile="$report/coverage.profdata" -summary-only \
  > "$report/coverage-summary.json"
mapfile -t prefixes < <(python3 - "$report/coverage-summary.json" <<'PY'
import json
import sys

marker = "/_virtual_includes/cbor_tags/"
with open(sys.argv[1]) as source:
    files = [f["filename"] for data in json.load(source)["data"] for f in data["files"]]
for prefix in sorted({path.split(marker)[0] + marker.rstrip("/") for path in files if marker in path}):
    print(prefix)
PY
)
if ((${#prefixes[@]} != 1)); then
  echo "Expected library coverage mapping; rebuild with --config=coverage." >&2
  exit 1
fi
mapping="-path-equivalence=${prefixes[0]},$root/include"

# Restrict the denominator to library source, excluding framework, STL and harness.
mapfile -t sources < <(rg --files --no-ignore "$root/include/cbor_tags" -g '*.h')
"${LLVM_COV:-llvm-cov}" report "$binary" -instr-profile="$report/coverage.profdata" "$mapping" "${sources[@]}" \
  > "$report/coverage.txt"
"${LLVM_COV:-llvm-cov}" show "$binary" -instr-profile="$report/coverage.profdata" "$mapping" "${sources[@]}" \
  -format=html -output-dir="$report/html" -show-branches=count
cat "$report/coverage.txt"
