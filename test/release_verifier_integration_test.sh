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

verify_fixture_tag() (
    cd -- "${fixture_repo}"
    bash "${repo_root}/scripts/release.sh" verify-tag \
        --tag v0.22.0 \
        --trusted-ref refs/heads/master "$@"
)

verification_output="$(verify_fixture_tag --public-key "${test_root}/public-key.asc" \
    --expected-fingerprint "$(printf '%s' "${fingerprint}" | LC_ALL=C tr '[:upper:]' '[:lower:]')")"

tag_object="$(git -C "${fixture_repo}" rev-parse refs/tags/v0.22.0)"
tag_commit="$(git -C "${fixture_repo}" rev-list -n 1 refs/tags/v0.22.0)"
grep -Fx 'release_tag=v0.22.0' <<<"${verification_output}" >/dev/null
grep -Fx "release_tag_object=${tag_object}" <<<"${verification_output}" >/dev/null
grep -Fx "release_commit=${tag_commit}" <<<"${verification_output}" >/dev/null
grep -Fx 'release_version=0.22.0' <<<"${verification_output}" >/dev/null
echo 'real-signed-tag: OK'

# Sign while the fixture key is valid, then verify at the current time.
gpg --batch --pinentry-mode loopback --passphrase '' --faked-system-time 20240101T000000 \
    --quick-generate-key 'expired tag fixture <expired-tag@cbor-tags.invalid>' ed25519 sign 1d >/dev/null 2>&1
expired_fingerprint="$(gpg --batch --with-colons --fingerprint expired-tag@cbor-tags.invalid |
    awk -F: '/^fpr:/ { print toupper($10); exit }')"
gpg --armor --export "${expired_fingerprint}" >"${test_root}/expired-key.asc"
printf '%s\n' '#!/usr/bin/env bash' \
    "exec $(printf '%q' "$(command -v gpg)") --faked-system-time 20240101T010000 \"\$@\"" >"${test_root}/sign-in-2024"
chmod +x "${test_root}/sign-in-2024"
GIT_COMMITTER_DATE=2024-01-01T01:00:00Z git -C "${fixture_repo}" -c "gpg.program=${test_root}/sign-in-2024" \
    tag -fs -u "${expired_fingerprint}" v0.22.0 -m 'Fixture release signed before expiry'

expired_key=(--public-key "${test_root}/expired-key.asc" --expected-fingerprint "${expired_fingerprint}")
if verify_fixture_tag "${expired_key[@]}" >"${test_root}/expired.out" 2>&1; then
    echo 'candidate accepted a tag with an expired key' >&2
    exit 1
fi
grep -F 'was signed by an expired key' "${test_root}/expired.out" >/dev/null
verified_output="$(verify_fixture_tag "${expired_key[@]}" --published)"
tag_object="$(git -C "${fixture_repo}" rev-parse refs/tags/v0.22.0)"
grep -Fx "release_tag_object=${tag_object}" <<<"${verified_output}" >/dev/null
grep -Fx "release_commit=${tag_commit}" <<<"${verified_output}" >/dev/null
echo 'archived-tag-with-expired-key: OK'
