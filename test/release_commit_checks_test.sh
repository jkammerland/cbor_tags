#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
test_root="$(mktemp -d)"
trap 'rm -rf -- "${test_root}"' EXIT

fake_bin="${test_root}/bin"
mkdir -p -- "${fake_bin}"
cat >"${fake_bin}/gh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
[[ "$1" == api ]]

readonly commit=1111111111111111111111111111111111111111
readonly wrong_commit=2222222222222222222222222222222222222222
readonly branch=master

case "${FAKE_SCENARIO:-success}" in
    success)
        printf 'Quality (Lint, Coverage)\t.github/workflows/quality.yml\t%s\t%s\tcompleted\tfailure\t9\t10\n' "${branch}" "${commit}"
        printf 'Quality (Lint, Coverage)\t.github/workflows/quality.yml\t%s\t%s\tcompleted\tsuccess\t1\t11\n' "${branch}" "${commit}"
        ;;
    pending)
        printf 'Quality (Lint, Coverage)\t.github/workflows/quality.yml\t%s\t%s\tcompleted\tsuccess\t9\t10\n' "${branch}" "${commit}"
        printf 'Quality (Lint, Coverage)\t.github/workflows/quality.yml\t%s\t%s\tin_progress\tpending\t1\t11\n' "${branch}" "${commit}"
        ;;
    failure)
        printf 'Quality (Lint, Coverage)\t.github/workflows/quality.yml\t%s\t%s\tcompleted\tfailure\t1\t11\n' "${branch}" "${commit}"
        ;;
    wrong-head)
        printf 'Quality (Lint, Coverage)\t.github/workflows/quality.yml\t%s\t%s\tcompleted\tsuccess\t1\t11\n' "${branch}" "${wrong_commit}"
        ;;
    wrong-branch)
        printf 'Quality (Lint, Coverage)\t.github/workflows/quality.yml\tintegration\t%s\tcompleted\tsuccess\t1\t11\n' "${commit}"
        ;;
    wrong-path)
        printf 'Quality (Lint, Coverage)\t.github/workflows/fake.yml\t%s\t%s\tcompleted\tsuccess\t1\t11\n' "${branch}" "${commit}"
        ;;
    wrong-name)
        printf 'Impostor\t.github/workflows/quality.yml\t%s\t%s\tcompleted\tsuccess\t1\t11\n' "${branch}" "${commit}"
        ;;
    missing)
        ;;
    api-failure)
        exit 1
        ;;
    *)
        exit 2
        ;;
esac
printf 'Ubuntu Tests (GCC 12-16, LLVM 17-22)\t.github/workflows/ubuntu.yml\t%s\t%s\tcompleted\tsuccess\t1\t20\n' "${branch}" "${commit}"
printf 'Windows Tests (MSVC/Clang-CL C++20/26)\t.github/workflows/windows.yml\t%s\t%s\tcompleted\tsuccess\t1\t30\n' "${branch}" "${commit}"
printf 'macOS Tests (AppleClang C++20/26)\t.github/workflows/macos.yml\t%s\t%s\tcompleted\tsuccess\t1\t40\n' "${branch}" "${commit}"
printf 'Quality (Lint, Coverage)\t.github/workflows/fake.yml\t%s\t%s\tcompleted\tsuccess\t99\t50\n' "${branch}" "${commit}"
EOF
chmod +x "${fake_bin}/gh"

export PATH="${fake_bin}:${PATH}"
readonly commit=1111111111111111111111111111111111111111
readonly repository=jkammerland/cbor_tags

verify=(bash "${repo_root}/scripts/verify-release-commit-checks.sh" --commit "${commit}" --repository "${repository}" --branch master)

FAKE_SCENARIO=success "${verify[@]}" >"${test_root}/success.out"
grep -F 'verified workflow: Quality (Lint, Coverage) (run 11, attempt 1)' "${test_root}/success.out" >/dev/null
echo 'all-required-workflows: OK'

expect_failure() {
    local scenario="$1"
    local expected_message="$2"
    if FAKE_SCENARIO="${scenario}" "${verify[@]}" >"${test_root}/${scenario}.out" 2>&1; then
        echo "${scenario}: unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F "${expected_message}" "${test_root}/${scenario}.out" >/dev/null
    echo "${scenario}: OK"
}

expect_failure pending 'required workflow is not complete'
expect_failure failure 'required workflow did not succeed'
expect_failure missing 'required workflow has no master push run'
expect_failure wrong-head 'required workflow has no master push run'
expect_failure wrong-branch 'required workflow has no master push run'
expect_failure wrong-path 'required workflow has no master push run'
expect_failure wrong-name 'required workflow has unexpected name'
expect_failure api-failure 'could not query workflow runs'
