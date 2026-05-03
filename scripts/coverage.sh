#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/coverage.sh

Builds tests with coverage instrumentation, runs ctest, and generates gcovr reports.
Coverage percentages are reported but not threshold-gated.

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

for tool in cmake ctest python3 "$cc" "$cxx" "$gcovr"; do
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

python3 - "$report_dir/coverage.xml" "$report_dir/coverage.txt" <<'PY'
import sys
import xml.etree.ElementTree as ET

xml_path, text_report_path = sys.argv[1], sys.argv[2]
root = ET.parse(xml_path).getroot()

total = 0
covered = 0
for cls in root.findall(".//class"):
    filename = cls.attrib.get("filename", "")
    if not filename.startswith("include/cbor_tags/"):
        continue
    if filename.startswith("include/cbor_tags/detail/"):
        continue

    physical_lines = {}
    for line in cls.findall("./lines/line"):
        number = int(line.attrib["number"])
        hits = int(line.attrib.get("hits", "0"))
        physical_lines[number] = physical_lines.get(number, 0) + hits

    total += len(physical_lines)
    covered += sum(1 for hits in physical_lines.values() if hits > 0)

percentage = (covered / total * 100.0) if total else 0.0
summary = (
    "\n"
    "------------------------------------------------------------------------------\n"
    "Unique Physical Header Line Coverage\n"
    "Directory: include/cbor_tags (excluding include/cbor_tags/detail)\n"
    "------------------------------------------------------------------------------\n"
    f"Lines: {covered} / {total} = {percentage:.1f}%\n"
    "Note: Counts each source line once across template instantiations.\n"
    "------------------------------------------------------------------------------\n"
)

with open(text_report_path, "a", encoding="utf-8") as report:
    report.write(summary)

print(summary, end="")
PY

echo "Coverage reports written to $report_dir"
