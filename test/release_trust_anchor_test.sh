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

# Fails for the reason it names, not for any reason
expect_rejection() {
    local name="$1"
    local expected="$2"
    shift 2
    if ("$@") >"${test_root}/failure.out" 2>&1; then
        echo "${name}: unexpectedly succeeded" >&2
        exit 1
    fi
    if ! grep -F -- "${expected}" "${test_root}/failure.out" >/dev/null; then
        printf '%s: expected output containing %q, got:\n' "${name}" "${expected}" >&2
        cat "${test_root}/failure.out" >&2
        exit 1
    fi
    echo "${name}: OK"
}

identity=(jkammerland/cbor_tags v0.28.0 1111111111111111111111111111111111111111 2222222222222222222222222222222222222222)

# Three assets with checksums and a manifest signed by the key in signer_home.
# Further arguments go to the signing gpg
write_signed_packages() {
    local packages="$1" public_key="$2" signer_home="$3" signer="$4"
    shift 4
    mkdir -- "${packages}"
    cp -- "${public_key}" "${packages}/cbor_tags-release-public-key.asc"

    local asset
    for asset in cbor_tags-0.28.0.tar.gz cbor_tags-0.28.0.zip cbor_tags-0.28.0.spdx.json; do
        printf 'fixture %s\n' "${asset}" >"${packages}/${asset}"
        write_checksums "${packages}/${asset}"
    done

    local signed_manifest="${packages}/cbor_tags-0.28.0.release-manifest"
    write_release_manifest "${packages}" 0.28.0 "${identity[@]}" >"${signed_manifest}"
    GNUPGHOME="${signer_home}" gpg --no-options --batch --yes --pinentry-mode loopback --passphrase '' "$@" \
        --local-user "${signer}" --output "${signed_manifest}.sig" --detach-sign "${signed_manifest}"
}

# The commands a consumer pastes, with the fingerprint they got elsewhere
write_consumer_commands() {
    local fingerprint="$1"
    printf 'expected_fingerprint=%q\n' "${fingerprint}"
    write_release_verification_commands 0.28.0 "${identity[@]}" | tail -n +2
}

for bad_key in attacker extra-primary; do
    verification_home="${test_root}/verify-${bad_key}"
    mkdir -m 700 -- "${verification_home}"
    expect_failure "${bad_key} before import" authenticate_release_key \
        "${verification_home}" "${test_root}/${bad_key}.asc" "${fixture_fingerprint}"
    # Inspection may initialize an empty keybox, but rejected keys are never imported.
    [[ -z "$(release_gpg "${verification_home}" --with-colons --list-keys 2>/dev/null | awk -F: '$1 == "pub" {print}')" ]]
done

# The right public key with somebody's secret key behind it. It has one primary
# public key with the pinned fingerprint, and an import would take the rest along
GNUPGHOME="${attacker_home}" gpg --no-options --batch --pinentry-mode loopback --passphrase '' \
    --armor --export-secret-keys "${attacker_fingerprint}" >"${test_root}/attacker-secret.asc"
cat "${test_root}/trusted.asc" "${test_root}/attacker-secret.asc" >"${test_root}/with-secret.asc"

verification_home="${test_root}/verify-with-secret"
mkdir -m 700 -- "${verification_home}"
expect_rejection 'secret key in the key file' 'release public key file holds secret key material' authenticate_release_key \
    "${verification_home}" "${test_root}/with-secret.asc" "${fixture_fingerprint}"
[[ -z "$(release_gpg "${verification_home}" --with-colons --list-secret-keys 2>/dev/null)" ]]
[[ -z "$(release_gpg "${verification_home}" --with-colons --list-keys 2>/dev/null | awk -F: '$1 == "pub" {print}')" ]]

package_dir="${test_root}/packages"
mkdir -- "${package_dir}"
cp -- "${test_root}/trusted.asc" "${package_dir}/cbor_tags-release-public-key.asc"
for payload in cbor_tags-0.28.0.tar.gz cbor_tags-0.28.0.zip cbor_tags-0.28.0.spdx.json; do
    printf 'fixture %s\n' "${payload}" >"${package_dir}/${payload}"
    write_checksums "${package_dir}/${payload}"
done
manifest="${package_dir}/cbor_tags-0.28.0.release-manifest"
write_release_manifest "${package_dir}" 0.28.0 "${identity[@]}" >"${manifest}"
GNUPGHOME="${fixture_home}" gpg --batch --yes --pinentry-mode loopback --passphrase '' \
    --local-user "${fixture_fingerprint}" --output "${manifest}.sig" --detach-sign "${manifest}"
verify_fixture_assets() {
    verify_signed_assets "${package_dir}" 0.28.0 "${fixture_fingerprint}" '' "${identity[@]}"
}

# A global attacker keyring and options cannot participate in verification.
export GNUPGHOME="${attacker_home}"
printf 'auto-key-retrieve\nkeyserver hkp://127.0.0.1:1\n' >"${attacker_home}/gpg.conf"
ambient_keys() {
    gpg --no-options --batch --with-colons --list-keys | awk -F: '$1 == "fpr" {print $10}'
}
[[ "$(ambient_keys)" == "${attacker_fingerprint}" ]]

lowercase_fingerprint="$(printf '%s' "${fixture_fingerprint}" | tr '[:upper:]' '[:lower:]')"
verify_signed_assets "${package_dir}" 0.28.0 "${lowercase_fingerprint}" '' "${identity[@]}"
echo 'trusted signing subkey with hostile global keyring: OK'

# The check above also passes when the ambient keyring does the verifying: the
# trusted key is imported there and the fingerprint still matches. So look at
# the keyrings. The ambient one got nothing
[[ "$(ambient_keys)" == "${attacker_fingerprint}" ]]
echo 'ambient keyring unchanged by verification: OK'

# The verification keyring holds the imported key and nothing ambient
isolated_home="${test_root}/isolated"
mkdir -m 700 -- "${isolated_home}"
authenticate_release_key "${isolated_home}" "${test_root}/trusted.asc" "${fixture_fingerprint}"
[[ "$(release_gpg "${isolated_home}" --with-colons --list-keys | awk -F: '/^pub:/ {want = 1; next} want && /^fpr:/ {print $10; want = 0}')" == "${fixture_fingerprint}" ]]
echo 'ambient key invisible to verification: OK'

# So a signature by the ambient key has no key to be checked with
printf 'signed by the ambient key\n' >"${test_root}/ambient-payload"
gpg --no-options --batch --yes --pinentry-mode loopback --passphrase '' --local-user "${attacker_fingerprint}" \
    --output "${test_root}/ambient-payload.sig" --detach-sign "${test_root}/ambient-payload"
ambient_status="$(release_gpg "${isolated_home}" --status-fd 1 --verify "${test_root}/ambient-payload.sig" "${test_root}/ambient-payload" 2>/dev/null || true)"
grep -Fx "[GNUPG:] NO_PUBKEY ${attacker_fingerprint:24}" <<<"${ambient_status}" >/dev/null
echo 'signature by the ambient key finds no key: OK'

write_release_verification_commands 0.28.0 "${identity[@]}" >"${test_root}/generated.sh"
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
[[ "$(ambient_keys)" == "${attacker_fingerprint}" ]]
echo 'ambient keyring unchanged by the consumer commands: OK'

cp -- "${test_root}/attacker.asc" "${package_dir}/cbor_tags-release-public-key.asc"
expect_failure 'consumer wrong key' bash -c 'cd -- "$1" && bash "$2"' _ "${package_dir}" "${test_root}/fixture-commands.sh"
cp -- "${test_root}/extra-primary.asc" "${package_dir}/cbor_tags-release-public-key.asc"
expect_failure 'consumer extra primary' bash -c 'cd -- "$1" && bash "$2"' _ "${package_dir}" "${test_root}/fixture-commands.sh"

# The pasted commands print no reason. A refusal at inspection and an import
# that fails later look the same from outside, so record what gpg is asked to do
recording_bin="${test_root}/recording-bin"
mkdir -- "${recording_bin}"
printf '%s\n' '#!/usr/bin/env bash' 'printf "%s\n" "$*" >>"${GPG_CALLS}"' "exec $(printf '%q' "$(command -v gpg)") \"\$@\"" \
    >"${recording_bin}/gpg"
chmod +x "${recording_bin}/gpg"

cp -- "${test_root}/with-secret.asc" "${package_dir}/cbor_tags-release-public-key.asc"
expect_failure 'consumer secret key in the key file' env "PATH=${recording_bin}:${PATH}" "GPG_CALLS=${test_root}/gpg-calls" \
    bash -c 'cd -- "$1" && bash "$2"' _ "${package_dir}" "${test_root}/fixture-commands.sh"
[[ "$(grep -c -- 'show-only' "${test_root}/gpg-calls")" == 1 ]]
[[ "$(grep -c -- '--quiet --import' "${test_root}/gpg-calls")" == 0 ]]
cp -- "${test_root}/trusted.asc" "${package_dir}/cbor_tags-release-public-key.asc"

payload="${manifest}"
cp -- "${payload}.sig" "${test_root}/trusted.sig"
cat "${test_root}/trusted.sig" >>"${payload}.sig"
expect_failure 'multiple valid signatures' verify_fixture_assets
expect_failure 'consumer multiple valid signatures' bash -c 'cd -- "$1" && bash "$2"' _ "${package_dir}" "${test_root}/fixture-commands.sh"

GNUPGHOME="${attacker_home}" gpg --no-options --batch --yes --pinentry-mode loopback --passphrase '' \
    --local-user "${attacker_fingerprint}" --output "${payload}.sig" --detach-sign "${payload}"
expect_failure 'attacker signature with global attacker key' verify_fixture_assets
expect_failure 'consumer attacker signature' bash -c 'cd -- "$1" && bash "$2"' _ "${package_dir}" "${test_root}/fixture-commands.sh"

# The signing subkey is copied while it is good, then revoked. The key file a
# verifier gets carries the revocation. GnuPG still exits 0 and prints VALIDSIG
# for a signature the copy makes: only REVKEYSIG in place of GOODSIG tells
maintainer_home="${test_root}/maintainer"
thief_home="${test_root}/thief"
mkdir -m 700 -- "${maintainer_home}"
GNUPGHOME="${maintainer_home}" gpg --no-options --batch --pinentry-mode loopback --passphrase '' \
    --quick-generate-key 'revoking primary <revoking@cbor-tags.invalid>' ed25519 cert 1d >/dev/null
maintainer_fingerprint="$(GNUPGHOME="${maintainer_home}" gpg --no-options --with-colons --list-keys | awk -F: '/^fpr:/ {print $10; exit}')"
GNUPGHOME="${maintainer_home}" gpg --no-options --batch --pinentry-mode loopback --passphrase '' \
    --quick-add-key "${maintainer_fingerprint}" ed25519 sign 1d >/dev/null

cp -R -- "${maintainer_home}" "${thief_home}"
find "${thief_home}" -type s -delete
printf 'key 1\nrevkey\ny\n1\n\ny\nsave\n' |
    GNUPGHOME="${maintainer_home}" gpg --no-options --batch --pinentry-mode loopback --passphrase '' \
        --command-fd 0 --edit-key "${maintainer_fingerprint}" >/dev/null 2>&1
GNUPGHOME="${maintainer_home}" gpg --no-options --armor --export "${maintainer_fingerprint}" >"${test_root}/revoked-subkey.asc"
[[ "$(GNUPGHOME="${maintainer_home}" gpg --no-options --with-colons --list-keys "${maintainer_fingerprint}" | awk -F: '$1 == "sub" {print $2}')" == r ]]

revoked_packages="${test_root}/revoked-packages"
write_signed_packages "${revoked_packages}" "${test_root}/revoked-subkey.asc" "${thief_home}" "${maintainer_fingerprint}"
expect_rejection 'signature by a revoked subkey' 'was signed by a revoked key' \
    verify_signed_assets "${revoked_packages}" 0.28.0 "${maintainer_fingerprint}" '' "${identity[@]}"
write_consumer_commands "${maintainer_fingerprint}" >"${test_root}/revoked-commands.sh"
expect_failure 'consumer signature by a revoked subkey' bash -c 'cd -- "$1" && bash "$2"' _ "${revoked_packages}" "${test_root}/revoked-commands.sh"

# A key that has expired since it signed. Nothing new is published with it, but
# the key file that comes with an old release cannot change, so the commands a
# consumer pastes keep accepting what it signed
expired_home="${test_root}/expired"
mkdir -m 700 -- "${expired_home}"
GNUPGHOME="${expired_home}" gpg --no-options --batch --pinentry-mode loopback --passphrase '' --faked-system-time 20240101T000000 \
    --quick-generate-key 'expired <expired@cbor-tags.invalid>' ed25519 sign 1d >/dev/null 2>&1
expired_fingerprint="$(GNUPGHOME="${expired_home}" gpg --no-options --with-colons --list-keys 2>/dev/null | awk -F: '/^fpr:/ {print $10; exit}')"
GNUPGHOME="${expired_home}" gpg --no-options --armor --export "${expired_fingerprint}" >"${test_root}/expired.asc"

expired_packages="${test_root}/expired-packages"
write_signed_packages "${expired_packages}" "${test_root}/expired.asc" "${expired_home}" "${expired_fingerprint}" \
    --faked-system-time 20240101T010000 2>/dev/null
expect_rejection 'signature by an expired key' 'was signed by an expired key' \
    verify_signed_assets "${expired_packages}" 0.28.0 "${expired_fingerprint}" '' "${identity[@]}"
write_consumer_commands "${expired_fingerprint}" >"${test_root}/expired-commands.sh"
(cd -- "${expired_packages}" && bash "${test_root}/expired-commands.sh")
echo 'consumer signature by a key that expired later: OK'

echo 'release trust anchor checks passed'
