#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
test_root="$(mktemp -d)"
trap 'rm -rf -- "${test_root}"' EXIT

fake_bin="${test_root}/bin"
mkdir -p -- "${fake_bin}"

cat >"${fake_bin}/curl" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
output=""
while (($#)); do
    case "$1" in
        --output)
            output="$2"
            shift 2
            ;;
        *) shift ;;
    esac
done
[[ -n "${output}" ]]
case "${output}" in
    *.sha256)
        printf '%s  %s\n' \
            "${FAKE_PUBLISHED_SHA:-e248e05d4b0c1ce8e9649fc83331a548487fa0be86aab2e331f27a2cd6609e31}" \
            'target_install_package-7.1.0-cmake.tar.gz' >"${output}"
        ;;
    *) : >"${output}" ;;
esac
EOF

cat >"${fake_bin}/gpg" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
for argument in "$@"; do
    case "${argument}" in
        --fingerprint)
            printf 'fpr:::::::::%s:\n' \
                "${FAKE_PUBLIC_FINGERPRINT:-7A9DA5E43CC1A9ECB9745CBE3A209DA2768BE08D}"
            exit 0
            ;;
        --verify)
            if [[ "${FAKE_VERIFY_FAIL:-0}" == 1 ]]; then
                printf '[GNUPG:] BADSIG fake\n'
                exit 1
            fi
            fingerprint="${FAKE_SIGNATURE_FINGERPRINT:-7A9DA5E43CC1A9ECB9745CBE3A209DA2768BE08D}"
            printf '[GNUPG:] VALIDSIG %s 2026-01-01 0 4 0 19 10 00 %s\n' \
                "${fingerprint}" "${fingerprint}"
            exit 0
            ;;
    esac
done
exit 0
EOF

cat >"${fake_bin}/sha256sum" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
if [[ "${FAKE_CHECKSUM_FAIL:-0}" == 1 ]]; then
    echo 'target_install_package-7.1.0-cmake.tar.gz: FAILED'
    exit 1
fi
echo 'target_install_package-7.1.0-cmake.tar.gz: OK'
EOF

cat >"${fake_bin}/tar" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
if [[ "${FAKE_MISSING_MEMBER:-0}" != 1 ]]; then
    echo 'target_install_package-7.1.0-cmake/usr/share/cmake/target_install_package/target_install_packageConfig.cmake'
fi
EOF

chmod +x "${fake_bin}/curl" "${fake_bin}/gpg" "${fake_bin}/sha256sum" "${fake_bin}/tar"

verifier=(bash "${repo_root}/scripts/verify-target-install-package-release.sh")

run_verifier() {
    env PATH="${fake_bin}:${PATH}" "$@" "${verifier[@]}"
}

expect_failure() {
    local name="$1"
    local expected="$2"
    shift 2

    local output
    if output="$(run_verifier "$@" 2>&1)"; then
        echo "${name}: verifier unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F -- "${expected}" <<<"${output}" >/dev/null || {
        printf '%s: expected output containing %q, got:\n%s\n' "${name}" "${expected}" "${output}" >&2
        exit 1
    }
    echo "${name}: OK"
}

run_verifier >/dev/null
echo "valid-signed-release: OK"
expect_failure \
    "published-digest-mismatch" \
    "published checksum is" \
    FAKE_PUBLISHED_SHA=1111222233334444555566667777888899990000111122223333444455556666
expect_failure "archive-digest-mismatch" "FAILED" FAKE_CHECKSUM_FAIL=1
expect_failure \
    "wrong-public-key" \
    "release public key fingerprint is" \
    FAKE_PUBLIC_FINGERPRINT=1111222233334444555566667777888899990000
expect_failure "invalid-signature" "does not have a valid signature" FAKE_VERIFY_FAIL=1
expect_failure \
    "unexpected-signing-key" \
    "dependency archive was signed by" \
    FAKE_SIGNATURE_FINGERPRINT=1111222233334444555566667777888899990000
expect_failure "missing-package-config" "dependency archive is missing" FAKE_MISSING_MEMBER=1
