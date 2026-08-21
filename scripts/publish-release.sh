#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
release_driver="${RELEASE_DRIVER:-${repo_root}/scripts/release.sh}"

die() {
    echo "error: $*" >&2
    exit 2
}

usage() {
    cat <<'EOF'
Usage: scripts/publish-release.sh <command> [options]

Commands:
  state --tag <tag> --commit <commit>
      Print missing, draft, or published for a release targeting the commit.

  publish --tag <tag> --tag-object <object> --commit <commit> --version <version>
          --package-dir <dir> --notes-file <path>
      Replace any matching draft, verify uploaded digests, and publish it.

  notes --tag <tag> --commit <commit> --version <version> --output <path>
      Generate release notes below build/ with the authenticated GitHub CLI.

  audit --tag <tag> --tag-object <object> --commit <commit> --version <version>
        --download-dir <dir> [--expected-unsigned-dir <dir>] [--wait]
      Download and verify an existing immutable release without mutating it.
EOF
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

initialize_repository_context() {
    if [[ -n "${GITHUB_REPOSITORY:-}" && -n "${GH_REPO:-}" && "${GITHUB_REPOSITORY}" != "${GH_REPO}" ]]; then
        die "GITHUB_REPOSITORY and GH_REPO identify different repositories"
    fi
    local repository="${GITHUB_REPOSITORY:-${GH_REPO:-}}"
    [[ "${repository}" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]] ||
        die "GITHUB_REPOSITORY or GH_REPO must identify one owner/name repository"
    export GITHUB_REPOSITORY="${repository}"
    export GH_REPO="${repository}"
}

canonical_generated_dir() {
    local requested="$1"
    local parent name candidate
    name="$(basename -- "${requested}")"
    parent="$(dirname -- "${requested}")"
    mkdir -p -- "${parent}"
    parent="$(cd -- "${parent}" && pwd -P)"
    candidate="${parent}/${name}"
    case "${candidate}" in
        "${repo_root}/build/"*) printf '%s\n' "${candidate}" ;;
        *) die "generated release paths must be below ${repo_root}/build: ${candidate}" ;;
    esac
}

canonical_generated_file() {
    local requested="$1"
    local parent name candidate
    name="$(basename -- "${requested}")"
    parent="$(dirname -- "${requested}")"
    mkdir -p -- "${parent}"
    parent="$(cd -- "${parent}" && pwd -P)"
    candidate="${parent}/${name}"
    case "${candidate}" in
        "${repo_root}/build/"*) printf '%s\n' "${candidate}" ;;
        *) die "generated release paths must be below ${repo_root}/build: ${candidate}" ;;
    esac
}

release_metadata() {
    local tag="$1"
    gh release view "${tag}" --json isDraft,targetCommitish
}

validate_release_target() {
    local metadata="$1"
    local tag="$2"
    local expected_commit="$3"
    local target_commitish
    target_commitish="$(jq -r .targetCommitish <<<"${metadata}")"
    [[ "${target_commitish}" == "${expected_commit}" ]] ||
        die "release ${tag} targets ${target_commitish}, expected ${expected_commit}"
}

release_state() {
    local tag=""
    local commit=""
    while (($#)); do
        case "$1" in
            --tag) tag="${2:-}"; shift 2 ;;
            --commit) commit="${2:-}"; shift 2 ;;
            *) die "unknown state option: $1" ;;
        esac
    done
    [[ -n "${tag}" && -n "${commit}" ]] || die "state requires --tag and --commit"

    local metadata
    if ! metadata="$(release_metadata "${tag}" 2>/dev/null)"; then
        printf 'missing\n'
        return
    fi
    validate_release_target "${metadata}" "${tag}" "${commit}"
    if [[ "$(jq -r .isDraft <<<"${metadata}")" == "true" ]]; then
        printf 'draft\n'
    else
        printf 'published\n'
    fi
}

verify_remote_tag() {
    local tag="$1"
    local expected_object="$2"
    local expected_commit="$3"
    local refs
    refs="$(git ls-remote "https://github.com/${GITHUB_REPOSITORY}.git" "refs/tags/${tag}" "refs/tags/${tag}^{}")"

    local actual_object actual_commit
    actual_object="$(awk -v ref="refs/tags/${tag}" '$2 == ref { print $1; exit }' <<<"${refs}")"
    actual_commit="$(awk -v ref="refs/tags/${tag}^{}" '$2 == ref { print $1; exit }' <<<"${refs}")"
    [[ "${actual_object}" == "${expected_object}" ]] ||
        die "remote tag ${tag} object is ${actual_object:-missing}, expected ${expected_object}"
    [[ "${actual_commit}" == "${expected_commit}" ]] ||
        die "remote tag ${tag} commit is ${actual_commit:-missing}, expected ${expected_commit}"
}

verify_local_release_context() {
    local tag="$1"
    local expected_object="$2"
    local expected_commit="$3"
    local expected_version="$4"
    local actual_head status verification_output

    actual_head="$(git -C "${repo_root}" rev-parse HEAD)"
    [[ "${actual_head}" == "${expected_commit}" ]] ||
        die "publisher checkout is at ${actual_head}, expected release commit ${expected_commit}"
    status="$(git -C "${repo_root}" status --porcelain --untracked-files=normal)"
    [[ -z "${status}" ]] || die "publisher checkout must be clean"

    verification_output="$(
        cd -- "${repo_root}"
        bash "${release_driver}" verify-tag --tag "${tag}" --trusted-ref HEAD
    )"
    grep -Fx "release_tag=${tag}" <<<"${verification_output}" >/dev/null ||
        die "local tag verification returned an unexpected tag"
    grep -Fx "release_tag_object=${expected_object}" <<<"${verification_output}" >/dev/null ||
        die "local tag verification returned an unexpected tag object"
    grep -Fx "release_commit=${expected_commit}" <<<"${verification_output}" >/dev/null ||
        die "local tag verification returned an unexpected commit"
    grep -Fx "release_version=${expected_version}" <<<"${verification_output}" >/dev/null ||
        die "local tag verification returned an unexpected version"
}

local_digest_manifest() {
    local package_dir="$1"
    local file
    while IFS= read -r file; do
        printf '%s\tsha256:%s\n' "$(basename -- "${file}")" "$(sha256sum "${file}" | awk '{ print $1 }')"
    done < <(find "${package_dir}" -maxdepth 1 -type f | sort)
}

remote_digest_manifest() {
    local tag="$1"
    local release_id
    release_id="$(gh api "repos/${GITHUB_REPOSITORY}/releases/tags/${tag}" --jq .id)"
    gh api --paginate "repos/${GITHUB_REPOSITORY}/releases/${release_id}/assets" \
        --jq '.[] | [.name, .digest] | @tsv' | sort
}

compare_remote_digests() {
    local tag="$1"
    local package_dir="$2"
    local local_manifest remote_manifest
    local_manifest="$(mktemp)"
    remote_manifest="$(mktemp)"
    local_digest_manifest "${package_dir}" >"${local_manifest}"
    remote_digest_manifest "${tag}" >"${remote_manifest}"
    if ! diff -u "${local_manifest}" "${remote_manifest}"; then
        rm -f -- "${local_manifest}" "${remote_manifest}"
        die "release ${tag} does not contain the exact expected asset names and SHA-256 digests"
    fi
    rm -f -- "${local_manifest}" "${remote_manifest}"
}

compare_unsigned_payloads() {
    local version="$1"
    local expected_dir="$2"
    local actual_dir="$3"
    local stem="cbor_tags-${version}-cmake"
    local name
    for name in \
        "${stem}.tar.gz" "${stem}.tar.gz.sha256" "${stem}.tar.gz.sha512" \
        "${stem}.zip" "${stem}.zip.sha256" "${stem}.zip.sha512" \
        "${stem}.spdx.json" "${stem}.spdx.json.sha256" "${stem}.spdx.json.sha512"; do
        cmp "${expected_dir}/${name}" "${actual_dir}/${name}" ||
            die "published release asset ${name} differs from the reproducible build"
    done
}

verify_immutable_releases_enabled() {
    local enabled
    enabled="$(gh api "repos/${GITHUB_REPOSITORY}/immutable-releases" --jq .enabled)" ||
        die "could not verify the repository immutable-releases setting"
    [[ "${enabled}" == "true" ]] || die "repository immutable releases are not enabled"
}

write_release_notes() {
    local tag=""
    local commit=""
    local version=""
    local output=""
    while (($#)); do
        case "$1" in
            --tag) tag="${2:-}"; shift 2 ;;
            --commit) commit="${2:-}"; shift 2 ;;
            --version) version="${2:-}"; shift 2 ;;
            --output) output="${2:-}"; shift 2 ;;
            *) die "unknown notes option: $1" ;;
        esac
    done
    [[ -n "${tag}" && -n "${commit}" && -n "${version}" && -n "${output}" ]] ||
        die "notes requires --tag, --commit, --version, and --output"
    [[ "${tag}" == "v${version}" ]] || die "release tag ${tag} does not match version ${version}"
    [[ "${commit}" =~ ^[0-9a-fA-F]{40}$ ]] || die "release commit must be a full Git object ID"
    output="$(canonical_generated_file "${output}")"

    local generated_notes
    generated_notes="$(
        gh api --method POST "repos/${GITHUB_REPOSITORY}/releases/generate-notes" \
            -f tag_name="${tag}" \
            -f target_commitish="${commit}" \
            --jq .body
    )"
    {
        echo "Signed cbor_tags ${tag} install packages."
        echo
        echo "Assets include TGZ and ZIP CMake install trees, an SPDX SBOM, detached GPG signatures, SHA-256/SHA-512 checksums, and the public verification key. The archives contain the default C++20 package configuration; fmt, nameof, and tl::expected remain consumer dependencies."
        echo
        echo "Verify an archive and the SBOM with:"
        echo
        echo '```sh'
        echo "gpg --import cbor_tags-release-public-key.asc"
        echo "gpg --verify cbor_tags-${version}-cmake.tar.gz.sig cbor_tags-${version}-cmake.tar.gz"
        echo "gpg --verify cbor_tags-${version}-cmake.spdx.json.sig cbor_tags-${version}-cmake.spdx.json"
        echo "sha256sum -c cbor_tags-${version}-cmake.tar.gz.sha256"
        echo '```'
        echo
        printf '%s\n' "${generated_notes}"
    } >"${output}"
    printf '%s\n' "${output}"
}

verify_attestation() {
    local tag="$1"
    local wait_for_attestation="$2"
    local attempts=1
    [[ "${wait_for_attestation}" == true ]] && attempts=30

    local output=""
    local attempt
    for ((attempt = 1; attempt <= attempts; ++attempt)); do
        if output="$(gh release verify "${tag}" 2>&1)"; then
            printf '%s\n' "${output}"
            return
        fi
        printf '%s\n' "${output}" >&2
        if [[ "${output}" != *"no attestations for tag"* || ${attempt} -eq ${attempts} ]]; then
            die "immutable release attestation verification failed for ${tag}"
        fi
        sleep 10
    done
}

audit_release() {
    local tag=""
    local tag_object=""
    local commit=""
    local version=""
    local download_dir=""
    local expected_unsigned_dir=""
    local wait_for_attestation=false
    while (($#)); do
        case "$1" in
            --tag) tag="${2:-}"; shift 2 ;;
            --tag-object) tag_object="${2:-}"; shift 2 ;;
            --commit) commit="${2:-}"; shift 2 ;;
            --version) version="${2:-}"; shift 2 ;;
            --download-dir) download_dir="${2:-}"; shift 2 ;;
            --expected-unsigned-dir) expected_unsigned_dir="${2:-}"; shift 2 ;;
            --wait) wait_for_attestation=true; shift ;;
            *) die "unknown audit option: $1" ;;
        esac
    done
    [[ -n "${tag}" && -n "${tag_object}" && -n "${commit}" && -n "${version}" && -n "${download_dir}" ]] ||
        die "audit requires --tag, --tag-object, --commit, --version, and --download-dir"
    download_dir="$(canonical_generated_dir "${download_dir}")"

    verify_remote_tag "${tag}" "${tag_object}" "${commit}"
    local metadata
    metadata="$(release_metadata "${tag}")"
    validate_release_target "${metadata}" "${tag}" "${commit}"
    [[ "$(jq -r .isDraft <<<"${metadata}")" == "false" ]] || die "release ${tag} is still a draft"

    rm -rf -- "${download_dir}"
    mkdir -p -- "${download_dir}"
    gh release download "${tag}" --dir "${download_dir}"
    bash "${release_driver}" verify-assets --package-dir "${download_dir}" --version "${version}"
    compare_remote_digests "${tag}" "${download_dir}"
    if [[ -n "${expected_unsigned_dir}" ]]; then
        compare_unsigned_payloads "${version}" "${expected_unsigned_dir}" "${download_dir}"
    fi
    verify_attestation "${tag}" "${wait_for_attestation}"

    local release_asset
    while IFS= read -r release_asset; do
        gh release verify-asset "${tag}" "${release_asset}"
    done < <(find "${download_dir}" -maxdepth 1 -type f | sort)
}

publish_release() {
    local tag=""
    local tag_object=""
    local commit=""
    local version=""
    local package_dir=""
    local notes_file=""
    while (($#)); do
        case "$1" in
            --tag) tag="${2:-}"; shift 2 ;;
            --tag-object) tag_object="${2:-}"; shift 2 ;;
            --commit) commit="${2:-}"; shift 2 ;;
            --version) version="${2:-}"; shift 2 ;;
            --package-dir) package_dir="${2:-}"; shift 2 ;;
            --notes-file) notes_file="${2:-}"; shift 2 ;;
            *) die "unknown publish option: $1" ;;
        esac
    done
    [[ -n "${tag}" && -n "${tag_object}" && -n "${commit}" && -n "${version}" && -n "${package_dir}" && -n "${notes_file}" ]] ||
        die "publish requires --tag, --tag-object, --commit, --version, --package-dir, and --notes-file"
    [[ -f "${notes_file}" ]] || die "release notes not found: ${notes_file}"
    verify_local_release_context "${tag}" "${tag_object}" "${commit}" "${version}"
    bash "${release_driver}" verify-assets --package-dir "${package_dir}" --version "${version}"
    verify_immutable_releases_enabled

    local metadata
    if metadata="$(release_metadata "${tag}" 2>/dev/null)"; then
        validate_release_target "${metadata}" "${tag}" "${commit}"
        if [[ "$(jq -r .isDraft <<<"${metadata}")" == "false" ]]; then
            die "published release ${tag} already exists; use the audit command"
        fi
        gh release delete "${tag}" --yes
    fi

    verify_remote_tag "${tag}" "${tag_object}" "${commit}"
    gh release create "${tag}" \
        --draft \
        --verify-tag \
        --target "${commit}" \
        --title "${tag}" \
        --notes-file "${notes_file}"

    local release_assets=()
    mapfile -t release_assets < <(find "${package_dir}" -maxdepth 1 -type f | sort)
    gh release upload "${tag}" "${release_assets[@]}" --clobber
    compare_remote_digests "${tag}" "${package_dir}"
    verify_remote_tag "${tag}" "${tag_object}" "${commit}"

    metadata="$(release_metadata "${tag}")"
    validate_release_target "${metadata}" "${tag}" "${commit}"
    [[ "$(jq -r .isDraft <<<"${metadata}")" == "true" ]] || die "release ${tag} was published concurrently"
    verify_immutable_releases_enabled
    gh release edit "${tag}" \
        --title "${tag}" \
        --notes-file "${notes_file}" \
        --draft=false

    audit_release \
        --tag "${tag}" \
        --tag-object "${tag_object}" \
        --commit "${commit}" \
        --version "${version}" \
        --download-dir "${repo_root}/build/published-release-audit" \
        --expected-unsigned-dir "${package_dir}" \
        --wait
}

require_command cmp
require_command diff
require_command gh
require_command git
require_command jq
require_command sha256sum

initialize_repository_context
command_name="${1:-}"
case "${command_name}" in
    state) shift; release_state "$@" ;;
    publish) shift; publish_release "$@" ;;
    notes) shift; write_release_notes "$@" ;;
    audit) shift; audit_release "$@" ;;
    "" | -h | --help | help) usage ;;
    *) usage >&2; die "unknown command: ${command_name}" ;;
esac
