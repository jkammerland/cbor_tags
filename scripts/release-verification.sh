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

    release_gpg "${verification_home}" --quiet --import "${verification_key_file}" || die "could not import authenticated release key"
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

validate_release_identity() {
    local version="$1" repository="$2" tag="$3" tag_object="$4" commit="$5"
    [[ "${version}" =~ ^[0-9]+\.[0-9]+\.[0-9]+([-+][A-Za-z0-9.-]+)?$ ]] || die "invalid release version"
    [[ "${repository}" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]] || die "release repository must identify owner/name"
    [[ "${tag}" == "v${version}" ]] || die "release tag must match version"
    [[ "${tag_object}" =~ ^[0-9a-f]{40}$ && "${commit}" =~ ^[0-9a-f]{40}$ ]] || die "release tag object and commit must be full lowercase Git object IDs"
}

expected_unsigned_assets() {
    local version="$1" stem="cbor_tags-$1"
    printf '%s\n' \
        "${stem}.spdx.json" "${stem}.spdx.json.sha256" "${stem}.spdx.json.sha512" \
        "${stem}.tar.gz" "${stem}.tar.gz.sha256" "${stem}.tar.gz.sha512" \
        "${stem}.zip" "${stem}.zip.sha256" "${stem}.zip.sha512"
}

expected_signed_assets() {
    expected_unsigned_assets "$1"
    printf '%s\n' cbor_tags-release-public-key.asc "cbor_tags-$1.release-manifest" "cbor_tags-$1.release-manifest.sig"
}

require_exact_assets() {
    local package_dir="$1" version="$2" asset_kind="$3"
    local expected_assets actual_assets
    case "${asset_kind}" in
        unsigned) expected_assets="$(expected_unsigned_assets "${version}" | LC_ALL=C sort)" ;;
        signed) expected_assets="$(expected_signed_assets "${version}" | LC_ALL=C sort)" ;;
        *) die "unknown asset set: ${asset_kind}" ;;
    esac
    [[ -d "${package_dir}" && ! -L "${package_dir}" ]] || die "release asset directory must be a non-symlink directory"
    [[ -z "$(find "${package_dir}" -mindepth 1 -maxdepth 1 ! -type f -print -quit)" ]] ||
        die "release assets must be regular non-symlink files"
    actual_assets="$(find "${package_dir}" -mindepth 1 -maxdepth 1 -printf '%f\n' | LC_ALL=C sort)"
    if [[ "${expected_assets}" != "${actual_assets}" ]]; then
        diff -u <(printf '%s\n' "${expected_assets}") <(printf '%s\n' "${actual_assets}") || true
        die "${package_dir} does not contain the exact ${asset_kind} release asset set"
    fi
}

verify_checksums() {
    local payload="$1" package_dir payload_name asset
    package_dir="$(dirname -- "${payload}")"
    payload_name="$(basename -- "${payload}")"
    for asset in "${payload}" "${payload}.sha256" "${payload}.sha512"; do
        [[ -f "${asset}" && ! -L "${asset}" ]] || die "checksum assets must be regular non-symlink files: ${asset}"
    done

    # Hash only the fixed expected payload; downloaded records cannot select files.
    (
        cd -- "${package_dir}"
        local algorithm digest
        for algorithm in sha256 sha512; do
            digest="$("${algorithm}sum" -- "${payload_name}")" || die "failed to hash ${payload_name}"
            printf '%s\n' "${digest}" | cmp -s - "${payload_name}.${algorithm}" ||
                die "noncanonical or incorrect ${algorithm} checksum for ${payload_name}"
        done
    )
}

write_release_manifest() {
    local package_dir="$1" version="$2" repository="$3" tag="$4" tag_object="$5" commit="$6"
    validate_release_identity "${version}" "${repository}" "${tag}" "${tag_object}" "${commit}"
    printf 'format=cbor_tags-release-manifest-v1\nrepository=%s\nversion=%s\ntag=%s\ntag_object=%s\ncommit=%s\nasset_count=10\n' \
        "${repository}" "${version}" "${tag}" "${tag_object}" "${commit}"
    (
        cd -- "${package_dir}"
        local asset
        while IFS= read -r asset; do
            [[ -f "${asset}" && ! -L "${asset}" ]] || die "manifest assets must be regular non-symlink files: ${asset}"
            sha256sum -- "${asset}" || die "failed to hash manifest asset ${asset}"
        done < <({ expected_unsigned_assets "${version}"; printf '%s\n' cbor_tags-release-public-key.asc; } | LC_ALL=C sort)
    )
}

verify_signed_assets() {
    local package_dir="$1" version="$2" expected_fingerprint="$3" canonical_public_key="$4"
    local repository="$5" tag="$6" tag_object="$7" commit="$8"
    validate_release_identity "${version}" "${repository}" "${tag}" "${tag_object}" "${commit}"
    require_exact_assets "${package_dir}" "${version}" signed

    (
        local gnupg_home
        gnupg_home="$(mktemp -d)"
        chmod 700 "${gnupg_home}"
        trap 'rm -rf -- "${gnupg_home}"' EXIT
        if [[ -n "${canonical_public_key}" ]]; then
            [[ -f "${canonical_public_key}" && ! -L "${canonical_public_key}" ]] || die "canonical release public key not found: ${canonical_public_key}"
            cmp "${canonical_public_key}" "${package_dir}/cbor_tags-release-public-key.asc" ||
                die "release asset public key differs from the canonical committed key"
        fi
        authenticate_release_key "${gnupg_home}" "${package_dir}/cbor_tags-release-public-key.asc" "${expected_fingerprint}"

        local manifest="cbor_tags-${version}.release-manifest" verification_output
        if ! verification_output="$(release_gpg "${gnupg_home}" --status-fd 1 --verify \
            "${package_dir}/${manifest}.sig" "${package_dir}/${manifest}" 2>&1)"; then
            printf '%s\n' "${verification_output}" >&2
            die "release manifest does not have a valid signature"
        fi
        authenticate_release_signature "${verification_output}" "${expected_fingerprint}" 'release manifest'

        # Regenerate from trusted identity arguments and the fixed assets, never
        # from identity or filenames supplied by the downloaded manifest.
        write_release_manifest "${package_dir}" "${version}" "${repository}" "${tag}" "${tag_object}" "${commit}" |
            cmp -s - "${package_dir}/${manifest}" || die "release manifest does not match canonical identity and assets"

        local payload
        for payload in "cbor_tags-${version}.tar.gz" "cbor_tags-${version}.zip" "cbor_tags-${version}.spdx.json"; do
            verify_checksums "${package_dir}/${payload}" || die "release asset checksum verification failed"
        done
    )
}

write_release_verification_commands() {
    local version="$1" repository="$2" tag="$3" tag_object="$4" commit="$5"
    validate_release_identity "${version}" "${repository}" "${tag}" "${tag_object}" "${commit}"
    printf 'expected_fingerprint=%q\n' '7A9DA5E43CC1A9ECB9745CBE3A209DA2768BE08D'
    printf '%s\n' 'set -euo pipefail'
    # Emit the shared verifier so consumer instructions and local validation use
    # the same canonical format without maintaining a second shell program.
    declare -f die release_gpg authenticate_release_key authenticate_release_signature \
        validate_release_identity expected_unsigned_assets expected_signed_assets \
        require_exact_assets verify_checksums write_release_manifest verify_signed_assets
    printf 'verify_signed_assets . %q "${expected_fingerprint}" %q %q %q %q %q\n' \
        "${version}" '' "${repository}" "${tag}" "${tag_object}" "${commit}"
}
