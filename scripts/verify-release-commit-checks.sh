#!/usr/bin/env bash
set -euo pipefail

die() {
    echo "error: $*" >&2
    exit 2
}

commit=""
repository="${GITHUB_REPOSITORY:-}"
branch="master"
while (($#)); do
    case "$1" in
        --commit)
            [[ $# -ge 2 ]] || die "--commit requires a value"
            commit="$2"
            shift 2
            ;;
        --repository)
            [[ $# -ge 2 ]] || die "--repository requires a value"
            repository="$2"
            shift 2
            ;;
        --branch)
            [[ $# -ge 2 ]] || die "--branch requires a value"
            branch="$2"
            shift 2
            ;;
        *) die "unknown option: $1" ;;
    esac
done

[[ "${commit}" =~ ^[0-9a-fA-F]{40}$ ]] || die "--commit must be a full 40-character Git object ID"
[[ "${repository}" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]] || die "--repository must be an owner/name pair"
[[ -n "${branch}" && "${branch}" != *$'\t'* && "${branch}" != *$'\n'* ]] || die "--branch must be a branch name"
command -v gh >/dev/null 2>&1 || die "required command not found: gh"

readonly expected_workflow_names=(
    "Quality (Lint, Coverage)"
    "Ubuntu Tests (GCC 12-16, LLVM 17-22)"
    "Windows Tests (MSVC/Clang-CL C++20/26)"
    "macOS Tests (AppleClang C++20/26)"
)
readonly expected_workflow_paths=(
    ".github/workflows/quality.yml"
    ".github/workflows/ubuntu.yml"
    ".github/workflows/windows.yml"
    ".github/workflows/macos.yml"
)

workflow_runs="$(
    gh api --paginate \
        "repos/${repository}/actions/runs?head_sha=${commit}&event=push&per_page=100" \
        --jq '.workflow_runs[] | [.name, .path, .head_branch, .head_sha, .status, (.conclusion // "pending"), .run_attempt, .id] | @tsv'
)" || die "could not query workflow runs for ${repository}@${commit}"

for index in "${!expected_workflow_paths[@]}"; do
    expected_name="${expected_workflow_names[${index}]}"
    expected_path="${expected_workflow_paths[${index}]}"
    latest_name=""
    latest_status=""
    latest_conclusion=""
    latest_attempt=""
    latest_id=0

    while IFS=$'\t' read -r name path head_branch head_sha status conclusion run_attempt run_id; do
        [[ "${path}" == "${expected_path}" ]] || continue
        [[ "${head_branch}" == "${branch}" && "${head_sha}" == "${commit}" ]] || continue
        if ((run_id > latest_id)); then
            latest_name="${name}"
            latest_status="${status}"
            latest_conclusion="${conclusion}"
            latest_attempt="${run_attempt}"
            latest_id="${run_id}"
        fi
    done <<<"${workflow_runs}"

    ((latest_id > 0)) ||
        die "required workflow has no ${branch} push run for ${commit}: ${expected_path}"
    [[ "${latest_name}" == "${expected_name}" ]] ||
        die "required workflow has unexpected name for ${expected_path}: ${latest_name}"
    [[ "${latest_status}" == "completed" ]] ||
        die "required workflow is not complete for ${commit}: ${expected_name} (${latest_status})"
    [[ "${latest_conclusion}" == "success" ]] ||
        die "required workflow did not succeed for ${commit}: ${expected_name} (${latest_conclusion})"
    printf 'verified workflow: %s (run %s, attempt %s)\n' \
        "${expected_name}" "${latest_id}" "${latest_attempt}"
done
