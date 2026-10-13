#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
source "${repo_root}/scripts/release.sh" help >/dev/null
test_root="$(mktemp -d)"
trap 'rm -rf -- "${test_root}"' EXIT

fixture_home="${test_root}/fixture"
attacker_home="${test_root}/attacker"
mkdir -m 700 -- "${fixture_home}" "${attacker_home}"
GNUPGHOME="${fixture_home}" gpg --batch --pinentry-mode loopback --passphrase '' \
    --quick-generate-key 'release primary <release@cbor-tags.invalid>' ed25519 cert 1d >/dev/null
fixture_fingerprint="$(GNUPGHOME="${fixture_home}" gpg --with-colons --list-keys | awk -F: '/^fpr:/ {print $10; exit}')"
GNUPGHOME="${fixture_home}" gpg --batch --pinentry-mode loopback --passphrase '' \
    --quick-add-key "${fixture_fingerprint}" ed25519 sign 1d >/dev/null
GNUPGHOME="${fixture_home}" gpg --armor --export "${fixture_fingerprint}" >"${test_root}/trusted.asc"

GNUPGHOME="${attacker_home}" gpg --batch --pinentry-mode loopback --passphrase '' \
    --quick-generate-key 'attacker <attacker@cbor-tags.invalid>' ed25519 sign 1d >/dev/null
attacker_fingerprint="$(GNUPGHOME="${attacker_home}" gpg --with-colons --list-keys | awk -F: '/^fpr:/ {print $10; exit}')"
GNUPGHOME="${attacker_home}" gpg --armor --export "${attacker_fingerprint}" >"${test_root}/attacker.asc"
cat "${test_root}/trusted.asc" "${test_root}/attacker.asc" >"${test_root}/extra-primary.asc"

expect_failure() {
    local name="$1"
    shift
    if ("$@") >"${test_root}/failure.out" 2>&1; then
        echo "${name}: unexpectedly succeeded" >&2
        exit 1
    fi
    echo "${name}: OK"
}

for bad_key in attacker extra-primary; do
    verification_home="${test_root}/verify-${bad_key}"
    mkdir -m 700 -- "${verification_home}"
    expect_failure "${bad_key} before import" authenticate_release_key \
        "${verification_home}" "${test_root}/${bad_key}.asc" "${fixture_fingerprint}"
    # Inspection may initialize an empty keybox, but rejected keys are never imported.
    [[ -z "$(release_gpg "${verification_home}" --with-colons --list-keys 2>/dev/null | awk -F: '$1 == "pub" {print}')" ]]
done

package_dir="${test_root}/packages"
mkdir -- "${package_dir}"
cp -- "${test_root}/trusted.asc" "${package_dir}/cbor_tags-release-public-key.asc"
for payload in cbor_tags-0.28.0.tar.gz cbor_tags-0.28.0.zip cbor_tags-0.28.0.spdx.json; do
    printf 'fixture %s\n' "${payload}" >"${package_dir}/${payload}"
    write_checksums "${package_dir}/${payload}"
    GNUPGHOME="${fixture_home}" gpg --batch --yes --pinentry-mode loopback --passphrase '' \
        --local-user "${fixture_fingerprint}" --output "${package_dir}/${payload}.sig" --detach-sign "${package_dir}/${payload}"
done

# A global attacker keyring and options cannot participate in verification.
export GNUPGHOME="${attacker_home}"
printf 'auto-key-retrieve\nkeyserver hkp://127.0.0.1:1\n' >"${attacker_home}/gpg.conf"
verify_signed_assets "${package_dir}" 0.28.0 "${fixture_fingerprint}"
echo 'trusted signing subkey with hostile global keyring: OK'

write_release_verification_commands 0.28.0 >"${test_root}/generated.sh"
grep -Fx 'expected_fingerprint=7A9DA5E43CC1A9ECB9745CBE3A209DA2768BE08D' "${test_root}/generated.sh"
bash -n "${test_root}/generated.sh"
# Substitute only the independently trusted fixture fingerprint; do not derive
# the expected identity from the downloaded public key.
{
    printf 'expected_fingerprint=%q\n' "${fixture_fingerprint}"
    tail -n +2 "${test_root}/generated.sh"
} >"${test_root}/fixture-commands.sh"
(cd -- "${package_dir}" && bash "${test_root}/fixture-commands.sh")
echo 'generated consumer commands: OK'

cp -- "${test_root}/attacker.asc" "${package_dir}/cbor_tags-release-public-key.asc"
expect_failure 'consumer wrong key' bash -c 'cd -- "$1" && bash "$2"' _ "${package_dir}" "${test_root}/fixture-commands.sh"
cp -- "${test_root}/extra-primary.asc" "${package_dir}/cbor_tags-release-public-key.asc"
expect_failure 'consumer extra primary' bash -c 'cd -- "$1" && bash "$2"' _ "${package_dir}" "${test_root}/fixture-commands.sh"
cp -- "${test_root}/trusted.asc" "${package_dir}/cbor_tags-release-public-key.asc"

payload="${package_dir}/cbor_tags-0.28.0.tar.gz"
cp -- "${payload}.sig" "${test_root}/trusted.sig"
cat "${test_root}/trusted.sig" >>"${payload}.sig"
expect_failure 'multiple valid signatures' verify_signed_assets "${package_dir}" 0.28.0 "${fixture_fingerprint}"

GNUPGHOME="${attacker_home}" gpg --no-options --batch --yes --pinentry-mode loopback --passphrase '' \
    --local-user "${attacker_fingerprint}" --output "${payload}.sig" --detach-sign "${payload}"
expect_failure 'attacker signature with global attacker key' verify_signed_assets "${package_dir}" 0.28.0 "${fixture_fingerprint}"
expect_failure 'consumer attacker signature' bash -c 'cd -- "$1" && bash "$2"' _ "${package_dir}" "${test_root}/fixture-commands.sh"

echo 'release trust anchor checks passed'
