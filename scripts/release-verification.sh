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
    local expected_fingerprint
    expected_fingerprint="$(printf '%s' "$3" | LC_ALL=C tr '[:lower:]' '[:upper:]')"
    [[ -f "${verification_key_file}" && ! -L "${verification_key_file}" ]] || die "release public key must be a regular non-symlink file"

    local key_listing primary_fingerprints
    key_listing="$(release_gpg "${verification_home}" --with-colons --import-options show-only --dry-run --import "${verification_key_file}")" ||
        die "could not inspect release public key"

    # A secret key is no public key: it is not counted below, and the import
    # would take it along.
    [[ -z "$(awk -F: '$1 == "sec" || $1 == "ssb"' <<<"${key_listing}")" ]] ||
        die "release public key file holds secret key material"

    primary_fingerprints="$(awk -F: '
        /^pub:/ { want_fingerprint = 1; next }
        want_fingerprint && /^fpr:/ { print toupper($10); want_fingerprint = 0 }
    ' <<<"${key_listing}")"
    [[ "${primary_fingerprints}" == "${expected_fingerprint}" ]] ||
        die "release public key fingerprint is ${primary_fingerprints:-missing}; expected exactly one primary key ${expected_fingerprint}"

    release_gpg "${verification_home}" --quiet --import "${verification_key_file}" || die "could not import authenticated release key"
}

release_has_status() {
    awk -v status="$2" '/^\[GNUPG:\] / && $2 == status { found = 1 }
        END { exit !found }' <<<"$1"
}

authenticate_release_asset_keys() {
    local verification_home="$1" bundled_key="$2" expected_fingerprint="$3"
    local canonical_key="$4" expired_key="$5"

    if [[ -n "${canonical_key}" ]]; then
        [[ -f "${canonical_key}" && ! -L "${canonical_key}" ]] ||
            die "canonical release public key not found: ${canonical_key}"
        if [[ "${expired_key}" == accept-expired-key ]]; then
            # Include revocations that the archived export may predate.
            authenticate_release_key "${verification_home}" "${canonical_key}" "${expected_fingerprint}"
        else
            cmp "${canonical_key}" "${bundled_key}" ||
                die "release asset public key differs from the canonical committed key"
        fi
    fi
    authenticate_release_key "${verification_home}" "${bundled_key}" "${expected_fingerprint}"
}

# A signature by a key that has expired since is refused, unless the fourth
# argument is accept-expired-key: for a release that is already published, whose
# key file cannot change any more.
authenticate_release_signature() {
    local verification_output="$1"
    local expected_fingerprint
    expected_fingerprint="$(printf '%s' "$2" | LC_ALL=C tr '[:lower:]' '[:upper:]')"
    local description="$3"
    local expired_key="${4:-reject-expired-key}"

    # Expiry can take precedence in the verdict while revocation is still reported.
    if release_has_status "${verification_output}" KEYREVOKED; then
        die "${description} was signed by a revoked key"
    fi

    # GnuPG exits 0 and prints VALIDSIG for a signature by a revoked or an
    # expired key as well. What it makes of the key is on this line.
    local verdicts
    verdicts="$(awk '/^\[GNUPG:\] (GOODSIG|EXPKEYSIG|REVKEYSIG|EXPSIG|BADSIG|ERRSIG) / {
        print $2
    }' <<<"${verification_output}")"
    case "${verdicts}" in
        GOODSIG) ;;
        EXPKEYSIG)
            [[ "${expired_key}" == accept-expired-key ]] || die "${description} was signed by an expired key"
            ;;
        REVKEYSIG) die "${description} was signed by a revoked key" ;;
        *) die "${description} needs exactly one good signature; GnuPG reported ${verdicts:-none}" ;;
    esac

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
    printf '%s\n' "BASH_ENV= bash --noprofile --norc <<'CBOR_TAGS_VERIFY'" 'set -euo pipefail'
    printf 'expected_fingerprint=%q\n' '7A9DA5E43CC1A9ECB9745CBE3A209DA2768BE08D'
    declare -f die release_gpg authenticate_release_key release_has_status authenticate_release_signature
    cat <<'EOF'
verification_home="$(mktemp -d)"
trap 'rm -rf -- "${verification_home}"' EXIT
chmod 700 "${verification_home}"
authenticate_release_key "${verification_home}" cbor_tags-release-public-key.asc "${expected_fingerprint}"
verify_signature() {
    local payload="$1" status
    if ! status="$(release_gpg "${verification_home}" --status-fd 1 --verify "${payload}.sig" "${payload}" 2>&1)"; then
        printf '%s\n' "${status}" >&2
        die "release asset ${payload} does not have a valid signature"
    fi
    authenticate_release_signature "${status}" "${expected_fingerprint}" "release asset ${payload}" accept-expired-key
}
EOF
    local payload
    for payload in "cbor_tags-${version}.tar.gz" "cbor_tags-${version}.zip" "cbor_tags-${version}.spdx.json"; do
        printf 'verify_signature %q\n' "${payload}"
        printf 'sha256sum -- %q | cmp - %q\n' "${payload}" "${payload}.sha256"
        printf 'sha512sum -- %q | cmp - %q\n' "${payload}" "${payload}.sha512"
    done
    printf '%s\n' 'CBOR_TAGS_VERIFY'
}
