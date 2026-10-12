#!/usr/bin/env bash

# Verification uses an empty keyring and no user options or network retrieval.
release_gpg() {
    local verification_home="$1"
    shift
    GNUPGHOME="${verification_home}" gpg --no-options --batch --no-autostart \
        --no-auto-key-retrieve --auto-key-locate clear "$@"
}

authenticate_release_key() {
    local verification_home="$1"
    local verification_key_file="$2"
    local expected_fingerprint="${3^^}"
    [[ -f "${verification_key_file}" && ! -L "${verification_key_file}" ]] || die "release public key must be a regular non-symlink file"

    local key_listing primary_fingerprints
    key_listing="$(release_gpg "${verification_home}" --with-colons --import-options show-only --dry-run --import "${verification_key_file}")" ||
        die "could not inspect release public key"
    primary_fingerprints="$(awk -F: '
        /^pub:/ { want_fingerprint = 1; next }
        want_fingerprint && /^fpr:/ { print toupper($10); want_fingerprint = 0 }
    ' <<<"${key_listing}")"
    [[ "${primary_fingerprints}" == "${expected_fingerprint}" ]] ||
        die "release public key fingerprint is ${primary_fingerprints:-missing}; expected exactly one primary key ${expected_fingerprint}"

    release_gpg "${verification_home}" --quiet --import "${verification_key_file}"
}

authenticate_release_signature() {
    local verification_output="$1"
    local expected_fingerprint="${2^^}"
    local description="$3"
    local primary_fingerprints
    # VALIDSIG identifies the signing subkey first and its primary key last.
    # Requiring a single record also rejects concatenated valid signatures.
    primary_fingerprints="$(awk '/^\[GNUPG:\] VALIDSIG / {
        print toupper(NF >= 12 ? $12 : $3)
    }' <<<"${verification_output}")"
    [[ "${primary_fingerprints}" == "${expected_fingerprint}" ]] ||
        die "${description} was signed by ${primary_fingerprints:-unknown}; expected exactly one valid signature from ${expected_fingerprint}"
}

write_release_verification_commands() {
    local version="$1"
    printf 'expected_fingerprint=%q\n' '7A9DA5E43CC1A9ECB9745CBE3A209DA2768BE08D'
    cat <<'EOF'
set -euo pipefail
verification_home="$(mktemp -d)"
chmod 700 "${verification_home}"
trap 'rm -rf -- "${verification_home}"' EXIT
release_gpg() {
    GNUPGHOME="${verification_home}" gpg --no-options --batch --no-autostart \
        --no-auto-key-retrieve --auto-key-locate clear "$@"
}
key_listing="$(release_gpg --with-colons --import-options show-only --dry-run --import cbor_tags-release-public-key.asc)"
primary_fingerprints="$(awk -F: '/^pub:/ {want=1; next} want && /^fpr:/ {print toupper($10); want=0}' <<<"${key_listing}")"
[[ "${primary_fingerprints}" == "${expected_fingerprint}" ]]
release_gpg --quiet --import cbor_tags-release-public-key.asc
verify_signature() {
    local payload="$1" status primary
    status="$(release_gpg --status-fd 1 --verify "${payload}.sig" "${payload}")"
    primary="$(awk '/^\[GNUPG:\] VALIDSIG / {print toupper(NF >= 12 ? $12 : $3)}' <<<"${status}")"
    [[ "${primary}" == "${expected_fingerprint}" ]]
}
EOF
    local payload
    for payload in "cbor_tags-${version}.tar.gz" "cbor_tags-${version}.zip" "cbor_tags-${version}.spdx.json"; do
        printf 'verify_signature %q\n' "${payload}"
        printf 'sha256sum -- %q | cmp - %q\n' "${payload}" "${payload}.sha256"
        printf 'sha512sum -- %q | cmp - %q\n' "${payload}" "${payload}.sha512"
    done
}
