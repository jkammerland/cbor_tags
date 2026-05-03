#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/format-cxx.sh [--check|--fix]

Checks or rewrites all tracked C++ source/header files with clang-format.

Environment:
  CLANG_FORMAT  clang-format executable to use (default: clang-format)
EOF
}

mode="check"
case "${1:-}" in
    "" | "--check")
        mode="check"
        ;;
    "--fix")
        mode="fix"
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

clang_format="${CLANG_FORMAT:-clang-format}"
if ! command -v "$clang_format" >/dev/null 2>&1; then
    echo "error: clang-format not found: $clang_format" >&2
    exit 127
fi

mapfile -d '' files < <(git ls-files -z -- '*.h' '*.hpp' '*.cpp' '*.cc' '*.cxx')
if ((${#files[@]} == 0)); then
    echo "No tracked C++ files found."
    exit 0
fi

if [[ "$mode" == "fix" ]]; then
    "$clang_format" -i "${files[@]}"
else
    "$clang_format" --dry-run --Werror "${files[@]}"
fi
