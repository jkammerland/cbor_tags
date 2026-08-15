#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
test_root="$(mktemp -d)"
trap 'rm -rf -- "${test_root}"' EXIT

fake_bin="${test_root}/bin"
mkdir -p -- "${fake_bin}"

cat >"${fake_bin}/gpg" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

for argument in "$@"; do
    if [[ "${argument}" == "--fingerprint" ]]; then
        printf 'fpr:::::::::%s:\n' "${FAKE_PUBLIC_FINGERPRINT:-7A9DA5E43CC1A9ECB9745CBE3A209DA2768BE08D}"
        exit 0
    fi
done
exit 0
EOF

cat >"${fake_bin}/git" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

command_name="$1"
shift
case "${command_name}" in
    check-ref-format)
        exit 0
        ;;
    cat-file)
        printf '%s\n' "${FAKE_TAG_TYPE:-tag}"
        ;;
    rev-parse)
        printf '%s\n' "89abcdef0123456789abcdef0123456789abcdef"
        ;;
    verify-tag)
        if [[ "${FAKE_VERIFY_FAIL:-0}" == 1 ]]; then
            printf '[GNUPG:] BADSIG fake\n' >&2
            exit 1
        fi
        fingerprint="${FAKE_TAG_FINGERPRINT:-7A9DA5E43CC1A9ECB9745CBE3A209DA2768BE08D}"
        printf '[GNUPG:] VALIDSIG %s 2026-01-01 0 4 0 19 10 00 %s\n' "${fingerprint}" "${fingerprint}" >&2
        ;;
    rev-list)
        printf '%s\n' "0123456789abcdef0123456789abcdef01234567"
        ;;
    merge-base)
        [[ "${FAKE_OFF_MASTER:-0}" != 1 ]]
        ;;
    show)
        case "$*" in
            *:CMakeLists.txt)
                printf 'cmake_minimum_required(VERSION 3.23)\nproject(cbor_tags VERSION %s)\n' "${FAKE_CMAKE_VERSION:-0.22.0}"
                ;;
            *:vcpkg.json)
                printf '{"name":"cbor-tags","version":"%s"}\n' "${FAKE_VCPKG_VERSION:-0.22.0}"
                ;;
            *:conanfile.py)
                printf 'class CborTagsConan:\n    version = "%s"\n' "${FAKE_CONAN_VERSION:-0.22.0}"
                ;;
            *)
                echo "unexpected git show path: $*" >&2
                exit 2
                ;;
        esac
        ;;
    *)
        echo "unexpected fake git command: ${command_name} $*" >&2
        exit 2
        ;;
esac
EOF

chmod +x "${fake_bin}/gpg" "${fake_bin}/git"

verifier=(
    bash "${repo_root}/scripts/release.sh" verify-tag
    --tag v0.22.0
    --trusted-ref refs/remotes/origin/master
)

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
    if ! grep -F -- "${expected}" <<<"${output}" >/dev/null; then
        printf '%s: expected output containing %q, got:\n%s\n' "${name}" "${expected}" "${output}" >&2
        exit 1
    fi
    echo "${name}: OK"
}

valid_output="$(run_verifier)"
grep -Fx 'release_tag=v0.22.0' <<<"${valid_output}" >/dev/null
grep -Fx 'release_commit=0123456789abcdef0123456789abcdef01234567' <<<"${valid_output}" >/dev/null
grep -Fx 'release_version=0.22.0' <<<"${valid_output}" >/dev/null
echo "valid-release-tag: OK"

expect_failure "lightweight-tag" "must be an annotated tag" FAKE_TAG_TYPE=commit
expect_failure "invalid-signature" "does not have a valid signature" FAKE_VERIFY_FAIL=1
expect_failure "unexpected-signing-key" "was signed by" FAKE_TAG_FINGERPRINT=1111222233334444555566667777888899990000
expect_failure "wrong-public-key" "release public key fingerprint is" FAKE_PUBLIC_FINGERPRINT=1111222233334444555566667777888899990000
expect_failure "tag-outside-master" "is not contained in" FAKE_OFF_MASTER=1
expect_failure "cmake-version-mismatch" "does not match CMake project version" FAKE_CMAKE_VERSION=0.21.0
expect_failure "vcpkg-version-mismatch" "does not match vcpkg.json version" FAKE_VCPKG_VERSION=0.21.0
expect_failure "conan-version-mismatch" "does not match conanfile.py version" FAKE_CONAN_VERSION=0.21.0

invalid_name_output="${test_root}/invalid-name-output"
if env PATH="${fake_bin}:${PATH}" bash "${repo_root}/scripts/release.sh" verify-tag --tag release-0.22.0 >"${invalid_name_output}" 2>&1; then
    echo "invalid-tag-name: verifier unexpectedly succeeded" >&2
    exit 1
fi
grep -F "must start with 'v'" "${invalid_name_output}" >/dev/null
echo "invalid-tag-name: OK"
