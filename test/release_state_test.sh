#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
test_root="$(mktemp -d)"
generated_root="${repo_root}/build/release-state-test-$$"
trap 'rm -rf -- "${test_root}" "${generated_root}"' EXIT

fake_bin="${test_root}/bin"
state_dir="${test_root}/state"
package_dir="${generated_root}/packages"
download_dir="${generated_root}/download"
mkdir -p -- "${fake_bin}" "${state_dir}/remote-assets" "${package_dir}"

readonly tag=v0.22.0
readonly tag_object=1111111111111111111111111111111111111111
readonly commit=2222222222222222222222222222222222222222
readonly version=0.22.0
readonly stem=cbor_tags-0.22.0-cmake

asset_names=(
    "${stem}.spdx.json"
    "${stem}.spdx.json.sha256"
    "${stem}.spdx.json.sha512"
    "${stem}.spdx.json.sig"
    "${stem}.tar.gz"
    "${stem}.tar.gz.sha256"
    "${stem}.tar.gz.sha512"
    "${stem}.tar.gz.sig"
    "${stem}.zip"
    "${stem}.zip.sha256"
    "${stem}.zip.sha512"
    "${stem}.zip.sig"
    cbor_tags-release-public-key.asc
)
for asset_name in "${asset_names[@]}"; do
    printf 'fixture:%s\n' "${asset_name}" >"${package_dir}/${asset_name}"
done
printf 'fixture release notes\n' >"${generated_root}/notes.md"

cat >"${fake_bin}/release-driver" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf 'release-driver %s\n' "$*" >>"${FAKE_STATE_DIR}/operations"
if [[ "$1" == verify-tag ]]; then
    printf 'release_tag=%s\n' "${FAKE_TAG}"
    printf 'release_tag_object=%s\n' "${FAKE_TAG_OBJECT}"
    printf 'release_commit=%s\n' "${FAKE_TAG_COMMIT}"
    printf 'release_version=%s\n' "${FAKE_VERSION}"
fi
EOF

cat >"${fake_bin}/git" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
if [[ "$1" == -C ]]; then
    shift 2
    case "$1" in
        rev-parse)
            printf '%s\n' "${FAKE_HEAD:-${FAKE_TAG_COMMIT}}"
            ;;
        status)
            [[ "${FAKE_DIRTY:-0}" != 1 ]] || printf ' M scripts/publish-release.sh\n'
            ;;
        *)
            exit 2
            ;;
    esac
elif [[ "$1" == ls-remote ]]; then
    [[ "$2" == "https://github.com/${GITHUB_REPOSITORY}.git" ]]
    printf '%s\trefs/tags/%s\n' "${FAKE_TAG_OBJECT}" "${FAKE_TAG}"
    printf '%s\trefs/tags/%s^{}\n' "${FAKE_TAG_COMMIT}" "${FAKE_TAG}"
else
    exit 2
fi
EOF

cat >"${fake_bin}/gh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
[[ "${GH_REPO:-}" == "${GITHUB_REPOSITORY}" ]]

state_file="${FAKE_STATE_DIR}/release-state"
target_file="${FAKE_STATE_DIR}/target"
remote_dir="${FAKE_STATE_DIR}/remote-assets"
operations="${FAKE_STATE_DIR}/operations"
command_name="$1"
shift

if [[ "${command_name}" == api ]]; then
    api_path=""
    for argument in "$@"; do
        [[ "${argument}" == repos/* ]] && api_path="${argument}"
    done
    case "${api_path}" in
        */releases/generate-notes)
            printf 'Generated fixture notes\n'
            ;;
        */immutable-releases)
            immutable_count_file="${FAKE_STATE_DIR}/immutable-check-count"
            immutable_count=0
            [[ ! -f "${immutable_count_file}" ]] || immutable_count="$(<"${immutable_count_file}")"
            immutable_count=$((immutable_count + 1))
            printf '%s\n' "${immutable_count}" >"${immutable_count_file}"
            if [[ "${FAKE_IMMUTABLE_DISABLE_AFTER_FIRST:-0}" == 1 && ${immutable_count} -gt 1 ]]; then
                printf 'false\n'
            else
                printf '%s\n' "${FAKE_IMMUTABLE_RELEASES_ENABLED:-true}"
            fi
            ;;
        */releases/tags/*)
            printf '42\n'
            ;;
        */releases/42/assets)
            first=true
            while IFS= read -r asset; do
                digest="$(sha256sum "${asset}" | awk '{ print $1 }')"
                if [[ "${FAKE_DIGEST_MISMATCH:-0}" == 1 && "${first}" == true ]]; then
                    digest=0000000000000000000000000000000000000000000000000000000000000000
                fi
                first=false
                printf '%s\tsha256:%s\n' "$(basename -- "${asset}")" "${digest}"
            done < <(find "${remote_dir}" -maxdepth 1 -type f | sort)
            ;;
        *)
            echo "unexpected fake gh api path: ${api_path}" >&2
            exit 2
            ;;
    esac
    exit 0
fi

[[ "${command_name}" == release ]]
subcommand="$1"
shift
case "${subcommand}" in
    view)
        [[ -f "${state_file}" ]] || exit 1
        state="$(<"${state_file}")"
        target="$(<"${target_file}")"
        if [[ "${state}" == draft ]]; then
            printf '{"isDraft":true,"targetCommitish":"%s"}\n' "${target}"
        else
            printf '{"isDraft":false,"targetCommitish":"%s"}\n' "${target}"
        fi
        ;;
    delete)
        printf 'delete\n' >>"${operations}"
        rm -f -- "${state_file}" "${target_file}"
        rm -rf -- "${remote_dir}"
        mkdir -p -- "${remote_dir}"
        ;;
    create)
        printf 'create\n' >>"${operations}"
        target=""
        while (($#)); do
            if [[ "$1" == --target ]]; then
                target="$2"
                shift 2
            else
                shift
            fi
        done
        printf 'draft\n' >"${state_file}"
        printf '%s\n' "${target}" >"${target_file}"
        ;;
    upload)
        printf 'upload\n' >>"${operations}"
        shift
        for argument in "$@"; do
            [[ "${argument}" == --clobber ]] && continue
            cp -- "${argument}" "${remote_dir}/"
        done
        ;;
    edit)
        printf 'edit\n' >>"${operations}"
        printf 'published\n' >"${state_file}"
        ;;
    download)
        destination=""
        while (($#)); do
            if [[ "$1" == --dir ]]; then
                destination="$2"
                shift 2
            else
                shift
            fi
        done
        cp -- "${remote_dir}"/* "${destination}/"
        ;;
    verify)
        printf 'release attestation verified\n'
        ;;
    verify-asset)
        ;;
    *)
        echo "unexpected fake gh release command: ${subcommand}" >&2
        exit 2
        ;;
esac
EOF

chmod +x "${fake_bin}/gh" "${fake_bin}/git" "${fake_bin}/release-driver"
export PATH="${fake_bin}:${PATH}"
export FAKE_STATE_DIR="${state_dir}"
export FAKE_TAG="${tag}"
export FAKE_TAG_OBJECT="${tag_object}"
export FAKE_TAG_COMMIT="${commit}"
export FAKE_VERSION="${version}"
export FAKE_IMMUTABLE_RELEASES_ENABLED=true
export GITHUB_REPOSITORY=jkammerland/cbor_tags
export RELEASE_DRIVER="${fake_bin}/release-driver"

publish_args=(
    publish
    --tag "${tag}"
    --tag-object "${tag_object}"
    --commit "${commit}"
    --version "${version}"
    --package-dir "${package_dir}"
    --notes-file "${generated_root}/notes.md"
)
audit_args=(
    audit
    --tag "${tag}"
    --tag-object "${tag_object}"
    --commit "${commit}"
    --version "${version}"
    --download-dir "${download_dir}"
    --expected-unsigned-dir "${package_dir}"
)

generated_notes="$(
    bash "${repo_root}/scripts/publish-release.sh" notes \
        --tag "${tag}" \
        --commit "${commit}" \
        --version "${version}" \
        --output "${generated_root}/notes.md"
)"
[[ "${generated_notes}" == "${generated_root}/notes.md" ]]
grep -F 'Signed cbor_tags v0.22.0 install packages.' "${generated_root}/notes.md" >/dev/null
grep -F 'Generated fixture notes' "${generated_root}/notes.md" >/dev/null
echo 'release-notes: OK'

reset_state() {
    rm -f -- \
        "${state_dir}/release-state" \
        "${state_dir}/target" \
        "${state_dir}/operations" \
        "${state_dir}/immutable-check-count"
    rm -rf -- "${state_dir}/remote-assets" "${download_dir}"
    mkdir -p -- "${state_dir}/remote-assets"
    unset FAKE_DIGEST_MISMATCH
    unset FAKE_DIRTY FAKE_HEAD
    unset FAKE_IMMUTABLE_DISABLE_AFTER_FIRST
    export FAKE_IMMUTABLE_RELEASES_ENABLED=true
    export FAKE_TAG_OBJECT="${tag_object}"
}

expect_failure() {
    local name="$1"
    shift
    if "$@" >"${test_root}/${name}.out" 2>&1; then
        echo "${name}: unexpectedly succeeded" >&2
        exit 1
    fi
    echo "${name}: OK"
}

if GITHUB_REPOSITORY=jkammerland/cbor_tags GH_REPO=someone/else \
    bash "${repo_root}/scripts/publish-release.sh" state --tag "${tag}" --commit "${commit}" \
    >"${test_root}/repository-mismatch.out" 2>&1; then
    echo 'repository mismatch unexpectedly succeeded' >&2
    exit 1
fi
grep -F 'GITHUB_REPOSITORY and GH_REPO identify different repositories' "${test_root}/repository-mismatch.out" >/dev/null
echo 'repository-mismatch: OK'

reset_state
printf 'draft\n' >"${state_dir}/release-state"
printf '%s\n' "${commit}" >"${state_dir}/target"
printf 'injected\n' >"${state_dir}/remote-assets/unexpected.bin"
bash "${repo_root}/scripts/publish-release.sh" "${publish_args[@]}" >/dev/null
grep -Fx 'published' "${state_dir}/release-state" >/dev/null
[[ ! -e "${state_dir}/remote-assets/unexpected.bin" ]]
first_mutation="$(grep -E '^(delete|create|upload|edit)$' "${state_dir}/operations" | head -1)"
if [[ "${first_mutation}" != delete ]]; then
    echo "draft recovery did not delete the existing draft first" >&2
    exit 1
fi
echo 'draft-recovery: OK'

: >"${state_dir}/operations"
state="$(bash "${repo_root}/scripts/publish-release.sh" state --tag "${tag}" --commit "${commit}")"
[[ "${state}" == published ]]
bash "${repo_root}/scripts/publish-release.sh" "${audit_args[@]}" >/dev/null
if grep -E '^(delete|create|upload|edit)$' "${state_dir}/operations" >/dev/null; then
    echo 'published audit performed a mutation' >&2
    exit 1
fi
echo 'published-rerun-audit: OK'

reset_state
export FAKE_TAG_OBJECT=3333333333333333333333333333333333333333
expect_failure moved-tag bash "${repo_root}/scripts/publish-release.sh" "${publish_args[@]}"
[[ ! -f "${state_dir}/release-state" ]]

reset_state
export FAKE_HEAD=3333333333333333333333333333333333333333
expect_failure stale-publisher-checkout bash "${repo_root}/scripts/publish-release.sh" "${publish_args[@]}"
[[ ! -f "${state_dir}/release-state" ]]

reset_state
export FAKE_DIRTY=1
expect_failure dirty-publisher-checkout bash "${repo_root}/scripts/publish-release.sh" "${publish_args[@]}"
[[ ! -f "${state_dir}/release-state" ]]

reset_state
export FAKE_DIGEST_MISMATCH=1
expect_failure digest-mismatch bash "${repo_root}/scripts/publish-release.sh" "${publish_args[@]}"
grep -Fx 'draft' "${state_dir}/release-state" >/dev/null
if grep -Fx edit "${state_dir}/operations" >/dev/null; then
    echo 'digest mismatch was published' >&2
    exit 1
fi

reset_state
export FAKE_IMMUTABLE_RELEASES_ENABLED=false
expect_failure immutable-releases-disabled bash "${repo_root}/scripts/publish-release.sh" "${publish_args[@]}"
if [[ -s "${state_dir}/operations" ]] && grep -E '^(delete|create|upload|edit)$' "${state_dir}/operations" >/dev/null; then
    echo 'disabled immutable releases allowed a release mutation' >&2
    exit 1
fi
[[ ! -f "${state_dir}/release-state" ]]

reset_state
export FAKE_IMMUTABLE_DISABLE_AFTER_FIRST=1
expect_failure immutable-releases-disabled-before-publication bash "${repo_root}/scripts/publish-release.sh" "${publish_args[@]}"
grep -Fx 'draft' "${state_dir}/release-state" >/dev/null
if grep -Fx edit "${state_dir}/operations" >/dev/null; then
    echo 'immutable releases disabled before publication still made the draft public' >&2
    exit 1
fi

reset_state
printf 'draft\n' >"${state_dir}/release-state"
printf '4444444444444444444444444444444444444444\n' >"${state_dir}/target"
expect_failure wrong-target bash "${repo_root}/scripts/publish-release.sh" state --tag "${tag}" --commit "${commit}"
