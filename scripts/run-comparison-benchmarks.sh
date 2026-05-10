#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/.." && pwd)"
output_dir="${CBOR_TAGS_BENCHMARK_REPORT_DIR:-${repo_root}/build/benchmark-comparison/report}"

if [[ $# -gt 0 ]]; then
  output_dir="$1"
fi

cmake --preset benchmark-comparison
cmake --build --preset benchmark-comparison

"${repo_root}/build/benchmark-comparison/benchmarks/serialization_compare/bench_serialization_compare" --output-dir "${output_dir}"

printf 'report: %s\n' "${output_dir}/report.md"
printf 'json:   %s\n' "${output_dir}/results.json"
printf 'csv:    %s\n' "${output_dir}/results.csv"
