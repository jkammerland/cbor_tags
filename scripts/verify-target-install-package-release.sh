#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
readonly dependency_version="7.1.0"
readonly dependency_archive="target_install_package-${dependency_version}-cmake.tar.gz"
readonly dependency_sha256="e248e05d4b0c1ce8e9649fc83331a548487fa0be86aab2e331f27a2cd6609e31"
readonly dependency_fingerprint="7A9DA5E43CC1A9ECB9745CBE3A209DA2768BE08D"
readonly dependency_url="https://github.com/jkammerland/target_install_package.cmake/releases/download/v${dependency_version}"
readonly public_key="${repo_root}/.github/release-signing-key.asc"

die() {
    echo "error: $*" >&2
    exit 2
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

for command_name in curl gpg sha256sum tar; do
    require_command "${command_name}"
done
[[ -f "${public_key}" ]] || die "release public key not found: ${public_key}"

verify_dir="$(mktemp -d)"
gnupg_home="${verify_dir}/gnupg"
cleanup() {
    rm -rf -- "${verify_dir}"
}
trap cleanup EXIT
mkdir -m 700 "${gnupg_home}"

for suffix in "" .sha256 .sig; do
    curl \
        --fail \
        --location \
        --retry 3 \
        --show-error \
        --silent \
        --output "${verify_dir}/${dependency_archive}${suffix}" \
        "${dependency_url}/${dependency_archive}${suffix}"
done

published_sha256="$(awk 'NR == 1 { print tolower($1) }' "${verify_dir}/${dependency_archive}.sha256")"
[[ "${published_sha256}" == "${dependency_sha256}" ]] ||
    die "published checksum is ${published_sha256:-missing}, expected ${dependency_sha256}"
(
    cd -- "${verify_dir}"
    sha256sum --check "${dependency_archive}.sha256"
)

GNUPGHOME="${gnupg_home}" gpg --batch --quiet --no-autostart --import "${public_key}"
imported_fingerprint="$({
    GNUPGHOME="${gnupg_home}" gpg --batch --no-autostart --with-colons --fingerprint
} | awk -F: '/^fpr:/ { print toupper($10); exit }')"
[[ "${imported_fingerprint}" == "${dependency_fingerprint}" ]] ||
    die "release public key fingerprint is ${imported_fingerprint:-missing}, expected ${dependency_fingerprint}"

verification_output=""
if ! verification_output="$(
    GNUPGHOME="${gnupg_home}" gpg \
        --batch \
        --no-autostart \
        --status-fd 1 \
        --verify \
        "${verify_dir}/${dependency_archive}.sig" \
        "${verify_dir}/${dependency_archive}" 2>&1
)"; then
    printf '%s\n' "${verification_output}" >&2
    die "dependency archive does not have a valid signature"
fi
signature_fingerprint="$(awk '/^\[GNUPG:\] VALIDSIG / { print toupper($3); exit }' <<<"${verification_output}")"
[[ "${signature_fingerprint}" == "${dependency_fingerprint}" ]] ||
    die "dependency archive was signed by ${signature_fingerprint:-unknown}, expected ${dependency_fingerprint}"

required_member="target_install_package-${dependency_version}-cmake/usr/share/cmake/target_install_package/target_install_packageConfig.cmake"
tar -tzf "${verify_dir}/${dependency_archive}" | grep -Fx -- "${required_member}" >/dev/null ||
    die "dependency archive is missing ${required_member}"

echo "Verified signed immutable target_install_package.cmake v${dependency_version} archive (${dependency_sha256})"
