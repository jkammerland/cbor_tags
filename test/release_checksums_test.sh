#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
source "${repo_root}/scripts/release.sh" help >/dev/null
test_root="$(mktemp -d)"
trap 'rm -rf -- "${test_root}"' EXIT

payload="${test_root}/cbor_tags-0.28.0.tar.gz"
printf 'release payload\n' >"${payload}"
printf 'outside payload\n' >"${test_root}/outside"
write_checksums "${payload}"
verify_checksums "${payload}"
echo 'canonical SHA-256 and SHA-512: OK'

expect_rejected() {
    local name="$1"
    if (verify_checksums "${payload}") >"${test_root}/failure.out" 2>&1; then
        echo "${name}: checksum verifier unexpectedly succeeded" >&2
        exit 1
    fi
    echo "${name}: OK"
}

for algorithm in sha256 sha512; do
    for filename in "${test_root}/outside" ../outside other.tar.gz; do
        write_checksums "${payload}"
        # Give the hostile filename the correct payload digest. Only canonical
        # record comparison, rather than digest-only admission, rejects it.
        digest="$("${algorithm}sum" -- "${payload}" | awk '{print $1}')"
        printf '%s  %s\n' "${digest}" "${filename}" >"${payload}.${algorithm}"
        expect_rejected "${algorithm} filename ${filename}"
    done

    write_checksums "${payload}"
    cp -- "${payload}.${algorithm}" "${test_root}/record"
    cat "${test_root}/record" >>"${payload}.${algorithm}"
    expect_rejected "${algorithm} multiple records"

    write_checksums "${payload}"
    digest="$("${algorithm}sum" -- "${payload}" | awk '{print $1}')"
    printf '%s *%s\n' "${digest}" "$(basename -- "${payload}")" >"${payload}.${algorithm}"
    expect_rejected "${algorithm} GNU binary record"

    write_checksums "${payload}"
    printf '%s  %s' "${digest}" "$(basename -- "${payload}")" >"${payload}.${algorithm}"
    expect_rejected "${algorithm} missing final newline"

    write_checksums "${payload}"
    mv -- "${payload}.${algorithm}" "${test_root}/linked-record"
    ln -s -- "${test_root}/linked-record" "${payload}.${algorithm}"
    expect_rejected "${algorithm} symlink"
    rm -- "${payload}.${algorithm}" "${test_root}/linked-record"
done

write_checksums "${payload}"
mv -- "${payload}" "${test_root}/linked-payload"
ln -s -- "${test_root}/linked-payload" "${payload}"
expect_rejected 'payload symlink'

echo 'release checksum checks passed'
