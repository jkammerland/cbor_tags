#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
test_root="$(mktemp -d)"
trap 'rm -rf -- "${test_root}"' EXIT

readonly epoch=1700000000
for timezone in UTC Europe/Stockholm; do
    source_dir="${test_root}/source-${timezone//\//-}"
    package_root="${source_dir}/cbor_tags-fixture/usr/share/cbor_tags"
    mkdir -p -- "${package_root}"
    printf 'timezone-independent payload\n' >"${package_root}/payload.txt"
    TZ="${timezone}" find "${source_dir}/cbor_tags-fixture" -exec touch -h -d "@${epoch}" {} +
    TZ="${timezone}" bash "${repo_root}/scripts/create-reproducible-zip.sh" \
        "${source_dir}" \
        cbor_tags-fixture \
        "${test_root}/${timezone//\//-}.zip"
done

cmp "${test_root}/UTC.zip" "${test_root}/Europe-Stockholm.zip"
echo 'zip-timezone-independent: OK'
