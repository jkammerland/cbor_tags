#!/usr/bin/env bash
set -euo pipefail

repository_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source_file="${repository_root}/benchmarks/compile/reflection_compile_stress.cpp"
include_dir=${CBOR_TAGS_REFLECTION_BENCH_INCLUDE_DIR:-"${repository_root}/include"}
instances=${CBOR_TAGS_REFLECTION_BENCH_INSTANCES:-512}
runs=${CBOR_TAGS_REFLECTION_BENCH_RUNS:-5}
value_expected_arity=${CBOR_TAGS_REFLECTION_BENCH_VALUE_EXPECTED_ARITY:-8}
reference_expected_arity=${CBOR_TAGS_REFLECTION_BENCH_REFERENCE_EXPECTED_ARITY:-8}

if [[ ! ${instances} =~ ^[1-9][0-9]*$ ]] || [[ ! ${runs} =~ ^[1-9][0-9]*$ ]] ||
  [[ ! ${value_expected_arity} =~ ^[0-9]+$ ]] || [[ ! ${reference_expected_arity} =~ ^[0-9]+$ ]]; then
  echo "Reflection benchmark counts must be non-negative integers, with instances and runs greater than zero" >&2
  exit 2
fi

if [[ ! -x /usr/bin/time ]]; then
  echo "/usr/bin/time is required" >&2
  exit 2
fi

compilers=("$@")
if (( ${#compilers[@]} == 0 )); then
  for candidate in g++ clang++; do
    if command -v "${candidate}" >/dev/null 2>&1; then
      compilers+=("${candidate}")
    fi
  done
fi

if (( ${#compilers[@]} == 0 )); then
  echo "Pass at least one C++ compiler or install g++/clang++" >&2
  exit 2
fi

for compiler in "${compilers[@]}"; do
  if ! command -v "${compiler}" >/dev/null 2>&1; then
    echo "Compiler not found: ${compiler}" >&2
    exit 2
  fi

  "${compiler}" --version | sed -n '1p'
  for scenario in value mutable-reference; do
    expected_arity=${value_expected_arity}
    if [[ ${scenario} == mutable-reference ]]; then
      expected_arity=${reference_expected_arity}
    fi
    definitions=(
      "-DCBOR_TAGS_REFLECTION_BENCH_INSTANCES=${instances}"
      "-DCBOR_TAGS_REFLECTION_BENCH_EXPECTED_ARITY=${expected_arity}"
    )
    if [[ ${scenario} == mutable-reference ]]; then
      definitions+=("-DCBOR_TAGS_REFLECTION_BENCH_MUTABLE_REFERENCES=1")
    fi

    command_line=(
      "${compiler}"
      -std=c++20
      -O0
      -fsyntax-only
      "-I${include_dir}"
      "${definitions[@]}"
      "${source_file}"
    )

    CCACHE_DISABLE=1 "${command_line[@]}"
    for ((run = 1; run <= runs; ++run)); do
      /usr/bin/time \
        -f "scenario=${scenario} run=${run} elapsed_seconds=%e max_rss_kb=%M" \
        env CCACHE_DISABLE=1 "${command_line[@]}"
    done
  done
done
