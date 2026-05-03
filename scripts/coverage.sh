#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/coverage.sh

Builds tests with coverage instrumentation, runs ctest, and generates gcovr reports.
Coverage percentage is reported but not threshold-gated.

Environment:
  BUILD_DIR    CMake build directory (default: build/coverage)
  REPORT_DIR   Coverage report directory (default: coverage-report)
  CC           C compiler (default: gcc)
  CXX          C++ compiler (default: g++)
  GCOVR        gcovr executable (default: gcovr)
  JOBS         build parallelism (default: CMake default)
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

cc="${CC:-gcc}"
cxx="${CXX:-g++}"
gcovr="${GCOVR:-gcovr}"
build_dir="${BUILD_DIR:-build/coverage}"
report_dir="${REPORT_DIR:-coverage-report}"

for tool in cmake ctest "$cc" "$cxx" "$gcovr"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "error: required tool not found: $tool" >&2
        exit 127
    fi
done

configure_args=(
    --preset coverage
    -B "$build_dir"
    -DCBOR_TAGS_BUILD_TESTS=ON
    -DCBOR_TAGS_USE_DEV_SETTINGS=ON
    -DCMAKE_C_COMPILER="$cc"
    -DCMAKE_CXX_COMPILER="$cxx"
)

if ! command -v ccache >/dev/null 2>&1; then
    configure_args+=(
        -DCMAKE_C_COMPILER_LAUNCHER=
        -DCMAKE_CXX_COMPILER_LAUNCHER=
    )
fi

cmake "${configure_args[@]}"

if [[ -d "$build_dir" ]]; then
    find "$build_dir" -name '*.gcda' -delete
fi

build_args=(--build "$build_dir")
if [[ -n "${JOBS:-}" ]]; then
    build_args+=(--parallel "$JOBS")
else
    build_args+=(--parallel)
fi

cmake "${build_args[@]}"
ctest --test-dir "$build_dir" --output-on-failure

rm -rf "$report_dir"
mkdir -p "$report_dir"

"$gcovr" \
    --root "$root" \
    --object-directory "$build_dir" \
    --filter 'include/cbor_tags/' \
    --exclude 'include/cbor_tags/detail/.*' \
    --txt "$report_dir/coverage.txt" \
    --txt-summary \
    --html-details "$report_dir/index.html" \
    --xml "$report_dir/coverage.xml" \
    --xml-pretty \
    "$build_dir"

echo "Coverage reports written to $report_dir"
