#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "usage: $0 <source-dir> <package-stem> <output.zip>" >&2
    exit 2
fi

source_dir="$1"
package_stem="$2"
output_zip="$3"

[[ -d "${source_dir}/${package_stem}" ]] || {
    echo "error: package root not found: ${source_dir}/${package_stem}" >&2
    exit 2
}
[[ "${package_stem}" == "$(basename -- "${package_stem}")" && "${package_stem}" != -* ]] || {
    echo "error: package stem must be a plain relative name: ${package_stem}" >&2
    exit 2
}
command -v zip >/dev/null 2>&1 || {
    echo "error: required command not found: zip" >&2
    exit 2
}

output_parent="$(cd -- "$(dirname -- "${output_zip}")" && pwd -P)"
output_zip="${output_parent}/$(basename -- "${output_zip}")"
(
    cd -- "${source_dir}"
    find "${package_stem}" -print | LC_ALL=C sort | TZ=UTC zip -X -q "${output_zip}" -@
)
