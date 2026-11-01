#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
test_root="$(mktemp -d)"
generated_root="${repo_root}/build/release-asset-verifier-test-$$"
trap 'rm -rf -- "${test_root}" "${generated_root}"' EXIT

fixture_repo="${generated_root}/tooling"
package_dir="${fixture_repo}/build/packages"
if ! mkdir -p -- "${package_dir}" 2>/dev/null; then
    echo "release asset verifier integration requires a writable repository build directory"
    exit 77
fi

version="$(
    awk '
        /^[[:space:]]*project[[:space:]]*\(/ { in_project = 1 }
        in_project && /^[[:space:]]*VERSION[[:space:]]+/ { print $2; exit }
        in_project && /\)/ { exit }
    ' "${repo_root}/CMakeLists.txt"
)"
if [[ -z "${version}" ]]; then
    echo 'could not read the cbor_tags CMake project version' >&2
    exit 1
fi
# Give signing a fixed source commit independent of the caller's worktree.
mkdir -p -- "${fixture_repo}/scripts"
cp -- "${repo_root}/scripts/release.sh" "${repo_root}/scripts/release-verification.sh" "${fixture_repo}/scripts/"
cp -- "${repo_root}/CMakeLists.txt" "${repo_root}/vcpkg.json" "${repo_root}/conanfile.py" "${fixture_repo}/"
git init --quiet "${fixture_repo}"
git -C "${fixture_repo}" add -- CMakeLists.txt vcpkg.json conanfile.py scripts
git -C "${fixture_repo}" -c user.name=Test -c user.email=test@example.invalid \
    -c commit.gpgsign=false -c core.hooksPath=/dev/null commit --quiet -m fixture
fixture_commit="$(git -C "${fixture_repo}" rev-parse HEAD)"

readonly version
readonly stem="cbor_tags-${version}"
identity_args=(--repository jkammerland/cbor_tags --tag "v${version}" --tag-object 1111111111111111111111111111111111111111 --commit "${fixture_commit}" --version "${version}")
payloads=("${stem}.tar.gz" "${stem}.zip" "${stem}.spdx.json")

for payload in "${payloads[@]}"; do
    printf 'fixture:%s\n' "${payload}" >"${package_dir}/${payload}"
    (
        cd -- "${package_dir}"
        sha256sum "${payload}" >"${payload}.sha256"
        sha512sum "${payload}" >"${payload}.sha512"
    )
done

fixture_home="${test_root}/fixture-gnupg"
mkdir -p -- "${fixture_home}"
chmod 700 "${fixture_home}"
GNUPGHOME="${fixture_home}" gpg --batch --pinentry-mode loopback --passphrase '' \
    --quick-generate-key 'cbor_tags release fixture <release@cbor-tags.invalid>' ed25519 sign 1d >/dev/null
fixture_fingerprint="$(
    GNUPGHOME="${fixture_home}" gpg --batch --with-colons --fingerprint |
        awk -F: '/^fpr:/ { print toupper($10); exit }'
)"
[[ -n "${fixture_fingerprint}" ]]
export GNUPGHOME="${fixture_home}"
export GPG_SIGNING_KEY="${fixture_fingerprint}"
unset GPG_PASSPHRASE_FILE
bash "${fixture_repo}/scripts/release.sh" sign --package-dir "${package_dir}" "${identity_args[@]}" >/dev/null
if bash "${repo_root}/scripts/release.sh" verify-assets \
    --package-dir "${package_dir}" \
    "${identity_args[@]}" >"${test_root}/test-key.out" 2>&1; then
    echo 'test-key release unexpectedly passed production-key verification' >&2
    exit 1
fi
echo 'single-test-key-signature: OK'
unset GPG_SIGNING_KEY GNUPGHOME

attacker_home="${test_root}/attacker-gnupg"
mkdir -p -- "${attacker_home}"
chmod 700 "${attacker_home}"
GNUPGHOME="${attacker_home}" gpg --batch --pinentry-mode loopback --passphrase '' \
    --quick-generate-key 'cbor_tags attacker fixture <attacker@cbor-tags.invalid>' ed25519 sign 1d >/dev/null
attacker_fingerprint="$(
    GNUPGHOME="${attacker_home}" gpg --batch --with-colons --fingerprint |
        awk -F: '/^fpr:/ { print toupper($10); exit }'
)"
[[ -n "${attacker_fingerprint}" ]]

for payload in "${stem}.release-manifest"; do
    GNUPGHOME="${attacker_home}" gpg --batch --yes --pinentry-mode loopback --passphrase '' \
        --local-user "${attacker_fingerprint}" \
        --output "${package_dir}/${payload}.sig" \
        --detach-sign "${package_dir}/${payload}"
done
{
    cat "${repo_root}/.github/release-signing-key.asc"
    GNUPGHOME="${attacker_home}" gpg --armor --export "${attacker_fingerprint}"
} >"${package_dir}/cbor_tags-release-public-key.asc"

if bash "${repo_root}/scripts/release.sh" verify-assets \
    --package-dir "${package_dir}" \
    "${identity_args[@]}" >"${test_root}/mixed-key.out" 2>&1; then
    echo 'mixed trusted/attacker key asset unexpectedly verified attacker signatures' >&2
    exit 1
fi
grep -F 'release asset public key differs from the canonical committed key' "${test_root}/mixed-key.out" >/dev/null
echo 'mixed-key-attacker-signatures: OK'
