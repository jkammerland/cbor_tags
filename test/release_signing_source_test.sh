#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
test_root="$(mktemp -d)"
trap 'rm -rf -- "${test_root}"' EXIT
fixture="${test_root}/tooling"
mkdir -p -- "${fixture}/scripts" "${fixture}/build/packages"

fixture_git() {
    git -C "${fixture}" -c user.name=Test -c user.email=test@example.invalid \
        -c commit.gpgsign=false -c core.hooksPath=/dev/null "$@"
}

write_metadata() {
    printf 'project(cbor_tags VERSION %s)\n' "$1" >"${fixture}/CMakeLists.txt"
    printf '{"name":"cbor-tags","version":"%s"}\n' "$1" >"${fixture}/vcpkg.json"
    printf 'class CborTagsConan:\n    version = "%s"\n' "$1" >"${fixture}/conanfile.py"
}

# The historical release has an old driver; it must never execute during signing.
git init --quiet "${fixture}"
write_metadata 1.2.3
printf '#!/usr/bin/env bash\necho "historical driver executed" >&2\nexit 99\n' >"${fixture}/scripts/release.sh"
fixture_git add -- CMakeLists.txt vcpkg.json conanfile.py scripts/release.sh
fixture_git commit --quiet -m 'historical release'
release_commit="$(fixture_git rev-parse HEAD)"

export GNUPGHOME="${test_root}/gnupg"
mkdir -m 700 -- "${GNUPGHOME}"
gpg --batch --pinentry-mode loopback --passphrase '' \
    --quick-generate-key 'release source fixture <source@cbor-tags.invalid>' ed25519 sign 1d >/dev/null
export GPG_SIGNING_KEY="$(gpg --with-colons --list-keys | awk -F: '/^fpr:/ {print $10; exit}')"
unset GPG_PASSPHRASE_FILE
gpg --armor --export "${GPG_SIGNING_KEY}" >"${test_root}/public.asc"
fixture_git -c user.signingkey="${GPG_SIGNING_KEY}" tag -s -m 'fixture release' v1.2.3
release_tag_object="$(fixture_git rev-parse refs/tags/v1.2.3)"

# Current tooling has a different project version from the admitted release.
cp -- "${repo_root}/scripts/release.sh" "${repo_root}/scripts/release-verification.sh" "${fixture}/scripts/"
write_metadata 9.8.7
fixture_git add -- CMakeLists.txt vcpkg.json conanfile.py scripts
fixture_git commit --quiet -m 'current tooling'
(
    cd -- "${fixture}"
    bash scripts/release.sh verify-tag --tag v1.2.3 --trusted-ref HEAD \
        --public-key "${test_root}/public.asc" --expected-fingerprint "${GPG_SIGNING_KEY}"
) >"${test_root}/verified-tag"
grep -Fx "release_commit=${release_commit}" "${test_root}/verified-tag"

source "${fixture}/scripts/release.sh" help >/dev/null
for extension in tar.gz zip spdx.json; do
    printf 'historical payload %s\n' "${extension}" >"${fixture}/build/packages/cbor_tags-1.2.3.${extension}"
    write_checksums "${fixture}/build/packages/cbor_tags-1.2.3.${extension}"
done
sign_args=(--package-dir "${fixture}/build/packages" --repository owner/project \
    --version 1.2.3 --tag v1.2.3 --tag-object "${release_tag_object}" --commit "${release_commit}")
bash "${fixture}/scripts/release.sh" sign "${sign_args[@]}"
verify_signed_assets "${fixture}/build/packages" 1.2.3 "${GPG_SIGNING_KEY}" '' \
    owner/project v1.2.3 "${release_tag_object}" "${release_commit}"
echo 'historical release signed by current tooling: OK'

expect_rejected() {
    local description="$1" diagnostic="$2"
    shift 2
    if "$@" >"${test_root}/failure.out" 2>&1; then
        echo "${description}: unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F "${diagnostic}" "${test_root}/failure.out"
    echo "${description}: OK"
}

expect_rejected 'tooling version cannot replace release version' 'signing version does not match release commit metadata' \
    bash "${fixture}/scripts/release.sh" sign "${sign_args[@]}" --version 9.8.7 --tag v9.8.7
expect_rejected 'missing commit' 'release metadata commit does not resolve to a commit' \
    bash "${fixture}/scripts/release.sh" sign "${sign_args[@]}" --commit 3333333333333333333333333333333333333333

# Invalid metadata at the selected commit cannot fall back to current metadata.
: >"${fixture}/vcpkg.json"
fixture_git add -- vcpkg.json
fixture_git commit --quiet -m 'malformed source metadata'
malformed_commit="$(fixture_git rev-parse HEAD)"
write_metadata 9.8.7
fixture_git add -- vcpkg.json
fixture_git commit --quiet -m 'restore tooling metadata'
expect_rejected 'malformed commit metadata' 'could not read signing commit metadata' \
    bash "${fixture}/scripts/release.sh" sign "${sign_args[@]}" --commit "${malformed_commit}"

echo 'release signing source checks passed'
