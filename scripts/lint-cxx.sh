#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/lint-cxx.sh

Runs the local C++ quality gate:
  1. clang-format check over all tracked C++ files
  2. clang-tidy CMake target over the library anchor translation unit

Environment:
  BUILD_DIR       CMake build directory (default: build/tidy)
  CC              C compiler (default: clang)
  CXX             C++ compiler (default: clang++)
  CLANG_FORMAT    clang-format executable (default: clang-format)
  CLANG_TIDY_EXE  clang-tidy executable (default: clang-tidy)
  JOBS            build parallelism (default: CMake default)
EOF
}

case "${1:-}" in
    "" )
        ;;
    "-h" | "--help")
        usage
        exit 0
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac

root="$(git rev-parse --show-toplevel)"
cd "$root"

"$root/scripts/format-cxx.sh" --check

cc="${CC:-clang}"
cxx="${CXX:-clang++}"
clang_tidy="${CLANG_TIDY_EXE:-clang-tidy}"
build_dir="${BUILD_DIR:-build/tidy}"

for tool in cmake "$cc" "$cxx" "$clang_tidy"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "error: required tool not found: $tool" >&2
        exit 127
    fi
done

configure_args=(
    --preset tidy
    -B "$build_dir"
    -DCMAKE_C_COMPILER="$cc"
    -DCMAKE_CXX_COMPILER="$cxx"
    -DCLANG_TIDY_EXE="$clang_tidy"
    -DCBOR_TAGS_TIDY_TARGET=ON
    -DCBOR_TAGS_TIDY_TESTS=OFF
    -DCBOR_TAGS_TIDY_BENCHMARKS=OFF
)

if ! command -v ccache >/dev/null 2>&1; then
    configure_args+=(
        -DCMAKE_C_COMPILER_LAUNCHER=
        -DCMAKE_CXX_COMPILER_LAUNCHER=
    )
fi

cmake "${configure_args[@]}"

build_args=(--build "$build_dir" --target tidy)
if [[ -n "${JOBS:-}" ]]; then
    build_args+=(--parallel "$JOBS")
else
    build_args+=(--parallel)
fi

cmake "${build_args[@]}"
