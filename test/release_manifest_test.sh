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

# Reuse the actual generated verifier definitions for every hostile fixture.
# Pass expected identity from the test, never from downloaded content.
write_release_verification_commands "${version}" "${repository}" "${tag}" "${tag_object}" "${commit}" >"${test_root}/generated.sh"
{ sed '$d' "${test_root}/generated.sh"; printf 'verify_signed_assets "$@"\n'; } >"${test_root}/generated-fixture.sh"
verifier_shell="$(command -v "${RELEASE_VERIFIER_SHELL:-bash}")"

# Exercise shasum even on hosts that also install GNU coreutils. Omitting find
# from this PATH also ensures verification does not depend on its extensions.
portable_bin="${test_root}/portable-bin"
mkdir "${portable_bin}"
for command_name in awk tr sort diff cmp mktemp chmod rm dirname basename gpg shasum; do
    ln -s "$(command -v "${command_name}")" "${portable_bin}/${command_name}"
done

verify_selected() {
    PATH="${verifier_path}" "${fixture_verifier}" "$@"
}

verify_generated() {
    "${verifier_shell}" "${test_root}/generated-fixture.sh" "$@"
}

verify_fixture() {
    verify_selected "${package_dir}" "${version}" "${fingerprint}" '' "${repository}" "${tag}" "${tag_object}" "${commit}"
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

for verifier_path in "${PATH}" "${portable_bin}"; do
for fixture_verifier in verify_signed_assets verify_generated; do
reset_fixture
verify_fixture
assets=("${package_dir}"/*)
[[ ${#assets[@]} -eq 12 ]]
[[ "$(wc -l <"${package_dir}/${manifest_name}")" -eq 17 ]]
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

expect_failure 'repository replay' 'does not match canonical identity and assets' verify_selected \
    "${package_dir}" "${version}" "${fingerprint}" '' attacker/cbor_tags "${tag}" "${tag_object}" "${commit}"
expect_failure 'changed tag' 'release tag must match version' verify_selected \
    "${package_dir}" "${version}" "${fingerprint}" '' "${repository}" v0.29.0 "${tag_object}" "${commit}"
expect_failure 'changed tag object' 'does not match canonical identity and assets' verify_selected \
    "${package_dir}" "${version}" "${fingerprint}" '' "${repository}" "${tag}" 3333333333333333333333333333333333333333 "${commit}"
expect_failure 'changed commit' 'does not match canonical identity and assets' verify_selected \
    "${package_dir}" "${version}" "${fingerprint}" '' "${repository}" "${tag}" "${tag_object}" 3333333333333333333333333333333333333333

# Rename a complete signed old set as a new version. Its valid signature must
# not authenticate the new identity, even when all expected names are present.
for asset in "${package_dir}/"*0.28.0*; do
    mv -- "${asset}" "${asset/0.28.0/0.29.0}"
done
expect_failure 'renamed old release' 'does not match canonical identity and assets' verify_selected \
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
{ head -n 7 "${baseline}/${manifest_name}"; tail -n 10 "${baseline}/${manifest_name}" | awk '{line[NR]=$0} END {for (i=NR; i>0; --i) print line[i]}'; } >"${package_dir}/${manifest_name}"
sign_manifest "${package_dir}/${manifest_name}"
expect_failure 'resigned noncanonical record order' 'does not match canonical identity and assets' verify_fixture

reset_fixture
manifest_content="$(cat "${package_dir}/${manifest_name}")"
printf '%s' "${manifest_content}" >"${package_dir}/${manifest_name}"
sign_manifest "${package_dir}/${manifest_name}"
expect_failure 'resigned missing final newline' 'does not match canonical identity and assets' verify_fixture

# A signature on a canonical manifest still cannot bless an incorrect sidecar.
reset_fixture
printf '%064d  cbor_tags-0.28.0.zip\n' 0 >"${package_dir}/cbor_tags-${version}.zip.sha256"
write_release_manifest "${package_dir}" "${version}" "${repository}" "${tag}" "${tag_object}" "${commit}" >"${package_dir}/${manifest_name}"
sign_manifest "${package_dir}/${manifest_name}"
expect_failure 'signed wrong checksum' 'noncanonical or incorrect sha256 checksum' verify_fixture

# Signed sidecars still require one exact text-mode record for the fixed name.
for algorithm in sha256 sha512; do
    for record_kind in absolute traversal wrong-name multiple binary no-newline; do
        reset_fixture
        payload_name="cbor_tags-${version}.zip"
        sidecar="${package_dir}/${payload_name}.${algorithm}"
        read -r digest _ <"${sidecar}"
        case "${record_kind}" in
            absolute) printf '%s  /tmp/%s\n' "${digest}" "${payload_name}" >"${sidecar}" ;;
            traversal) printf '%s  ../%s\n' "${digest}" "${payload_name}" >"${sidecar}" ;;
            wrong-name) printf '%s  another.zip\n' "${digest}" >"${sidecar}" ;;
            multiple) cat "${baseline}/${payload_name}.${algorithm}" >>"${sidecar}" ;;
            binary) printf '%s *%s\n' "${digest}" "${payload_name}" >"${sidecar}" ;;
            no-newline) printf '%s  %s' "${digest}" "${payload_name}" >"${sidecar}" ;;
        esac
        write_release_manifest "${package_dir}" "${version}" "${repository}" "${tag}" "${tag_object}" "${commit}" >"${package_dir}/${manifest_name}"
        sign_manifest "${package_dir}/${manifest_name}"
        expect_failure "signed ${algorithm} ${record_kind}" "noncanonical or incorrect ${algorithm} checksum" verify_fixture
    done
done

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

reset_fixture
printf 'extra\n' >"${package_dir}/.hidden"
expect_failure 'hidden extra file' 'exact signed release asset set' verify_fixture
reset_fixture
printf 'extra\n' >"${package_dir}/"$'bad\nname'
expect_failure 'newline filename' 'invalid filename' verify_fixture
reset_fixture
rm -- "${package_dir}/cbor_tags-${version}.zip.sha256"
ln -s -- "${baseline}/cbor_tags-${version}.zip.sha256" "${package_dir}/cbor_tags-${version}.zip.sha256"
expect_failure 'checksum symlink' 'regular non-symlink files' verify_fixture

echo "${fixture_verifier}: manifest fixture matrix passed (${verifier_path})"
done
done

echo 'release manifest checks passed'
