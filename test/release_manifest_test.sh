#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
source "${repo_root}/scripts/release.sh" help >/dev/null
test_root="$(mktemp -d)"
trap 'rm -rf -- "${test_root}"' EXIT

fixture_home="${test_root}/gnupg"
mkdir -m 700 -- "${fixture_home}"
GNUPGHOME="${fixture_home}" gpg --batch --pinentry-mode loopback --passphrase '' \
    --quick-generate-key 'manifest fixture <manifest@cbor-tags.invalid>' ed25519 sign 1d >/dev/null
fingerprint="$(GNUPGHOME="${fixture_home}" gpg --with-colons --list-keys | awk -F: '/^fpr:/ {print $10; exit}')"

version=0.28.0
repository=jkammerland/cbor_tags
tag=v0.28.0
tag_object=1111111111111111111111111111111111111111
commit=2222222222222222222222222222222222222222
baseline="${test_root}/baseline"
package_dir="${test_root}/packages"
mkdir -- "${baseline}" "${package_dir}"
for extension in tar.gz zip spdx.json; do
    printf 'original %s\n' "${extension}" >"${baseline}/cbor_tags-${version}.${extension}"
    write_checksums "${baseline}/cbor_tags-${version}.${extension}"
done
GNUPGHOME="${fixture_home}" gpg --armor --export "${fingerprint}" >"${baseline}/cbor_tags-release-public-key.asc"
manifest_name="cbor_tags-${version}.release-manifest"
write_release_manifest "${baseline}" "${version}" "${repository}" "${tag}" "${tag_object}" "${commit}" >"${baseline}/${manifest_name}"

sign_manifest() {
    GNUPGHOME="${fixture_home}" gpg --batch --yes --pinentry-mode loopback --passphrase '' \
        --local-user "${fingerprint}" --output "$1.sig" --detach-sign "$1"
}
sign_manifest "${baseline}/${manifest_name}"

verify_fixture() {
    verify_signed_assets "${package_dir}" "${version}" "${fingerprint}" '' "${repository}" "${tag}" "${tag_object}" "${commit}"
}

reset_fixture() {
    # Only this test's concrete temporary fixture directory is replaced.
    rm -rf -- "${package_dir}"
    mkdir -- "${package_dir}"
    cp -- "${baseline}"/* "${package_dir}/"
}

expect_failure() {
    local name="$1" diagnostic="$2"
    shift 2
    if ("$@") >"${test_root}/failure.out" 2>&1; then
        echo "${name}: unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F -- "${diagnostic}" "${test_root}/failure.out" >/dev/null || {
        cat "${test_root}/failure.out" >&2
        echo "${name}: unexpected failure reason" >&2
        exit 1
    }
    echo "${name}: OK"
}

reset_fixture
verify_fixture
[[ "$(find "${package_dir}" -mindepth 1 -maxdepth 1 | wc -l)" == 12 ]]
[[ "$(wc -l <"${package_dir}/${manifest_name}")" == 17 ]]
cat >"${test_root}/expected-header" <<'EOF'
format=cbor_tags-release-manifest-v1
repository=jkammerland/cbor_tags
version=0.28.0
tag=v0.28.0
tag_object=1111111111111111111111111111111111111111
commit=2222222222222222222222222222222222222222
asset_count=10
EOF
head -n 7 "${package_dir}/${manifest_name}" | cmp - "${test_root}/expected-header"
echo 'good manifest format and exactly 12 assets: OK'

expect_failure 'repository replay' 'does not match canonical identity and assets' verify_signed_assets \
    "${package_dir}" "${version}" "${fingerprint}" '' attacker/cbor_tags "${tag}" "${tag_object}" "${commit}"
expect_failure 'changed tag' 'release tag must match version' verify_signed_assets \
    "${package_dir}" "${version}" "${fingerprint}" '' "${repository}" v0.29.0 "${tag_object}" "${commit}"
expect_failure 'changed tag object' 'does not match canonical identity and assets' verify_signed_assets \
    "${package_dir}" "${version}" "${fingerprint}" '' "${repository}" "${tag}" 3333333333333333333333333333333333333333 "${commit}"
expect_failure 'changed commit' 'does not match canonical identity and assets' verify_signed_assets \
    "${package_dir}" "${version}" "${fingerprint}" '' "${repository}" "${tag}" "${tag_object}" 3333333333333333333333333333333333333333

# Rename a complete signed old set as a new version. Its valid signature must
# not authenticate the new identity, even when all expected names are present.
for asset in "${package_dir}/"*0.28.0*; do
    mv -- "${asset}" "${asset/0.28.0/0.29.0}"
done
expect_failure 'renamed old release' 'does not match canonical identity and assets' verify_signed_assets \
    "${package_dir}" 0.29.0 "${fingerprint}" '' "${repository}" v0.29.0 "${tag_object}" "${commit}"

reset_fixture
printf 'different signed release zip\n' >"${package_dir}/cbor_tags-${version}.zip"
write_checksums "${package_dir}/cbor_tags-${version}.zip"
write_release_manifest "${package_dir}" "${version}" "${repository}" "${tag}" "${tag_object}" "${commit}" >"${package_dir}/${manifest_name}"
sign_manifest "${package_dir}/${manifest_name}"
verify_fixture
cp -- "${baseline}/cbor_tags-${version}.zip"* "${package_dir}/"
expect_failure 'mix signed asset sets' 'does not match canonical identity and assets' verify_fixture

reset_fixture
printf 'asset_count=10\n' >>"${package_dir}/${manifest_name}"
sign_manifest "${package_dir}/${manifest_name}"
expect_failure 'resigned duplicate field' 'does not match canonical identity and assets' verify_fixture

reset_fixture
{ head -n 7 "${baseline}/${manifest_name}"; tail -n 10 "${baseline}/${manifest_name}" | tac; } >"${package_dir}/${manifest_name}"
sign_manifest "${package_dir}/${manifest_name}"
expect_failure 'resigned noncanonical record order' 'does not match canonical identity and assets' verify_fixture

reset_fixture
truncate -s -1 "${package_dir}/${manifest_name}"
sign_manifest "${package_dir}/${manifest_name}"
expect_failure 'resigned missing final newline' 'does not match canonical identity and assets' verify_fixture

# A signature on a canonical manifest still cannot bless an incorrect sidecar.
reset_fixture
printf '%064d  cbor_tags-0.28.0.zip\n' 0 >"${package_dir}/cbor_tags-${version}.zip.sha256"
write_release_manifest "${package_dir}" "${version}" "${repository}" "${tag}" "${tag_object}" "${commit}" >"${package_dir}/${manifest_name}"
sign_manifest "${package_dir}/${manifest_name}"
expect_failure 'signed wrong checksum' 'noncanonical or incorrect sha256 checksum' verify_fixture

reset_fixture
printf 'extra\n' >"${package_dir}/unexpected.sig"
expect_failure 'extra file' 'exact signed release asset set' verify_fixture
reset_fixture
mkdir "${package_dir}/extra-directory"
expect_failure 'extra directory' 'regular non-symlink files' verify_fixture
reset_fixture
rm -- "${package_dir}/cbor_tags-${version}.zip"
expect_failure 'missing payload' 'exact signed release asset set' verify_fixture
ln -s -- "${baseline}/cbor_tags-${version}.zip" "${package_dir}/cbor_tags-${version}.zip"
expect_failure 'payload symlink' 'regular non-symlink files' verify_fixture
reset_fixture
rm -- "${package_dir}/${manifest_name}"
ln -s -- "${baseline}/${manifest_name}" "${package_dir}/${manifest_name}"
expect_failure 'manifest symlink' 'regular non-symlink files' verify_fixture

echo 'release manifest checks passed'
