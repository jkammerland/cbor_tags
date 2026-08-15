#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
test_root="$(mktemp -d)"
trap 'rm -rf -- "${test_root}"' EXIT

export GNUPGHOME="${test_root}/gnupg"
mkdir -p -- "${GNUPGHOME}"
chmod 700 "${GNUPGHOME}"
gpg --batch --pinentry-mode loopback --passphrase '' \
    --quick-generate-key 'cbor_tags verifier test <verifier@cbor-tags.invalid>' ed25519 sign 1d >/dev/null
fingerprint="$(gpg --batch --with-colons --fingerprint | awk -F: '/^fpr:/ { print toupper($10); exit }')"
[[ -n "${fingerprint}" ]]
gpg --armor --export "${fingerprint}" >"${test_root}/public-key.asc"

fixture_repo="${test_root}/repository"
git init --quiet --initial-branch=master "${fixture_repo}"
git -C "${fixture_repo}" config user.name 'cbor_tags verifier test'
git -C "${fixture_repo}" config user.email verifier@cbor-tags.invalid
git -C "${fixture_repo}" config user.signingkey "${fingerprint}"

cat >"${fixture_repo}/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.23)
project(cbor_tags VERSION 0.22.0)
EOF
cat >"${fixture_repo}/vcpkg.json" <<'EOF'
{"name":"cbor-tags","version":"0.22.0"}
EOF
cat >"${fixture_repo}/conanfile.py" <<'EOF'
class CborTagsConan:
    version = "0.22.0"
EOF

git -C "${fixture_repo}" add CMakeLists.txt vcpkg.json conanfile.py
git -C "${fixture_repo}" commit --quiet -m 'fixture release'
git -C "${fixture_repo}" tag -s -u "${fingerprint}" v0.22.0 -m 'Release v0.22.0'

verification_output="$(
    cd -- "${fixture_repo}"
    bash "${repo_root}/scripts/release.sh" verify-tag \
        --tag v0.22.0 \
        --trusted-ref refs/heads/master \
        --public-key "${test_root}/public-key.asc" \
        --expected-fingerprint "${fingerprint}"
)"

tag_object="$(git -C "${fixture_repo}" rev-parse refs/tags/v0.22.0)"
tag_commit="$(git -C "${fixture_repo}" rev-list -n 1 refs/tags/v0.22.0)"
grep -Fx 'release_tag=v0.22.0' <<<"${verification_output}" >/dev/null
grep -Fx "release_tag_object=${tag_object}" <<<"${verification_output}" >/dev/null
grep -Fx "release_commit=${tag_commit}" <<<"${verification_output}" >/dev/null
grep -Fx 'release_version=0.22.0' <<<"${verification_output}" >/dev/null
echo 'real-signed-tag: OK'
