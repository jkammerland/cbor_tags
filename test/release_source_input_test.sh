#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
test_root="$(mktemp -d)"
trap 'rm -rf -- "${test_root}"' EXIT

# Keep tooling and source distinct, with no examples directory in the source.
tools_root="${test_root}/tools"
source_root="${tools_root}/build/source"
consumer_root="${tools_root}/test/consumer/source_archive"
mkdir -p -- "${tools_root}/scripts" "${source_root}" "${consumer_root}"
cp -- "${repo_root}/scripts/release.sh" "${tools_root}/scripts/"
cp -- "${repo_root}/CMakeLists.txt" "${repo_root}/vcpkg.json" "${repo_root}/conanfile.py" "${tools_root}/"
cp -- "${repo_root}/test/consumer/source_archive/CMakeLists.txt" "${consumer_root}/"

cat >"${source_root}/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.23)
project(cbor_tags VERSION 1.2.3 LANGUAGES CXX)
if(NOT VCPKG_MANIFEST_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  message(FATAL_ERROR "consumer must use the source manifest, not the tooling manifest")
endif()
add_library(cbor_fixture INTERFACE)
add_library(cbor::tags ALIAS cbor_fixture)
CMAKE
printf '{"name":"cbor-tags","version":"1.2.3"}\n' >"${source_root}/vcpkg.json"
printf 'class CborTagsConan:\n    version = "1.2.3"\n' >"${source_root}/conanfile.py"

git init --quiet "${source_root}"
git -C "${source_root}" add -- CMakeLists.txt vcpkg.json conanfile.py
git -C "${source_root}" -c user.name=Test -c user.email=test@example.invalid \
    -c commit.gpgsign=false -c core.hooksPath=/dev/null commit --quiet -m fixture
source_commit="$(git -C "${source_root}" rev-parse HEAD)"

cat >"${consumer_root}/main.cc" <<'CPP'
#include <cstdlib>
#include <fstream>

int main() {
    std::ofstream marker{std::getenv("CBOR_TAGS_CONSUMER_MARKER")};
    marker << "source consumer ran\n";
    if (!marker) {
        return 2;
    }

    return std::getenv("CBOR_TAGS_FAIL_CONSUMER") ? 23 : 0;
}
CPP
touch "${test_root}/toolchain.cmake"
export CBOR_TAGS_CONSUMER_MARKER="${test_root}/consumer-ran"
unset CBOR_TAGS_FAIL_CONSUMER

invoke() {
    bash -c '
        source "$1" --help >/dev/null
        shift
        "$@"
    ' _ "${tools_root}/scripts/release.sh" "$@"
}

# Version selection must follow the source argument rather than the tooling checkout.
[[ "$(invoke metadata_version "${source_root}")" == 1.2.3 ]]
if ! invoke run_source_consumer "${source_root}" "${test_root}/toolchain.cmake" \
        "${test_root}/consumer-build" >"${test_root}/success.log" 2>&1; then
    cat "${test_root}/success.log" >&2
    exit 1
fi
grep -Fx 'source consumer ran' "${CBOR_TAGS_CONSUMER_MARKER}"

# CTest must propagate a failed consumer, not merely confirm successful compilation.
consumer_status=0
CBOR_TAGS_FAIL_CONSUMER=1 invoke run_source_consumer \
    "${source_root}" "${test_root}/toolchain.cmake" "${test_root}/consumer-build" \
    >"${test_root}/failure.log" 2>&1 || consumer_status=$?
if [[ "${consumer_status}" -ne 8 ]]; then
    cat "${test_root}/failure.log" >&2
    echo "expected CTest failure exit 8, got ${consumer_status}" >&2
    exit 1
fi

expect_rejected() {
    local diagnostic="$1"
    shift
    if output="$(bash "${tools_root}/scripts/release.sh" build "$@" 2>&1)"; then
        echo "unexpectedly accepted source: $*" >&2
        exit 1
    fi
    grep -F -- "${diagnostic}" <<<"${output}"
}

# Reject invalid or modified input before downloads, builds, or output cleanup.
expect_rejected '--source-dir requires a value' --source-dir
expect_rejected 'release source directory not found' --source-dir "${test_root}/missing"
expect_rejected 'release source must be a Git checkout root' --source-dir "${tools_root}"
printf '\n# modified\n' >>"${source_root}/CMakeLists.txt"
expect_rejected 'release source checkout must be clean' --source-dir "${source_root}"
git -C "${source_root}" restore --worktree -- CMakeLists.txt
touch "${source_root}/untracked"
expect_rejected 'release source checkout must be clean' --source-dir "${source_root}"
rm -- "${source_root}/untracked"

# Generated-output cleanup must never remove the admitted source checkout.
expect_rejected 'release source overlaps generated output' \
    --source-dir "${source_root}" --package-dir "${source_root}"
cp -R -- "${source_root}" "${tools_root}/build/release"
expect_rejected 'release source overlaps generated output' \
    --source-dir "${tools_root}/build/release"
[[ "$(git -C "${source_root}" rev-parse HEAD)" == "${source_commit}" ]]
[[ -z "$(git -C "${source_root}" status --porcelain)" ]]

echo 'release source input checks passed'
