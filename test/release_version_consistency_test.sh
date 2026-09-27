#!/usr/bin/env bash
set -euo pipefail

# Verifies that scripts/release.sh check-versions accepts agreeing project
# versions and rejects any single-file drift with a specific diagnostic.

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
test_root="$(mktemp -d)"
trap 'rm -rf -- "${test_root}"' EXIT

# Build a minimal fixture repository containing only what check-versions reads,
# so the test never mutates the real metadata files.
fixture="${test_root}/repo"
mkdir -p -- "${fixture}/scripts"
cp -- "${repo_root}/scripts/release.sh" "${fixture}/scripts/release.sh"

write_metadata() {
    local cmake_version="$1"
    local vcpkg_version="$2"
    local conan_version="$3"

    cat >"${fixture}/CMakeLists.txt" <<CMAKE
cmake_minimum_required(VERSION 3.23)
project(
  cbor_tags
  VERSION ${cmake_version}
  LANGUAGES CXX)
CMAKE

    cat >"${fixture}/vcpkg.json" <<VCPKG
{
  "name": "cbor-tags",
  "version": "${vcpkg_version}"
}
VCPKG

    cat >"${fixture}/conanfile.py" <<CONAN
from conan import ConanFile


class CborTagsConan(ConanFile):
    name = "cbor-tags"
    version = "${conan_version}"
CONAN
}

expect_success() {
    local description="$1"
    if ! output="$(bash "${fixture}/scripts/release.sh" check-versions 2>&1)"; then
        echo "FAIL: ${description} should have succeeded, got:" >&2
        echo "${output}" >&2
        exit 1
    fi
    grep -qF "Project versions agree" <<<"${output}" || {
        echo "FAIL: ${description} succeeded without the expected message, got:" >&2
        echo "${output}" >&2
        exit 1
    }
    echo "OK: ${description}"
}

expect_drift_rejected() {
    local description="$1"
    if output="$(bash "${fixture}/scripts/release.sh" check-versions 2>&1)"; then
        echo "FAIL: ${description} should have failed, got:" >&2
        echo "${output}" >&2
        exit 1
    fi
    grep -qF "project versions disagree" <<<"${output}" || {
        echo "FAIL: ${description} failed for an unexpected reason:" >&2
        echo "${output}" >&2
        exit 1
    }
    echo "OK: ${description}"
}

write_metadata 0.24.0 0.24.0 0.24.0
expect_success "agreeing versions"

# Each file drifting on its own must be caught independently, so a bump that
# forgets any one manifest cannot pass.
write_metadata 0.25.0 0.24.0 0.24.0
expect_drift_rejected "CMakeLists.txt ahead of the manifests"

write_metadata 0.24.0 0.25.0 0.24.0
expect_drift_rejected "vcpkg.json out of step"

write_metadata 0.24.0 0.24.0 0.25.0
expect_drift_rejected "conanfile.py out of step"

# The real repository must agree as-is; this is the check CI relies on.
if ! output="$(bash "${repo_root}/scripts/release.sh" check-versions 2>&1)"; then
    echo "FAIL: the checked-in project metadata disagrees:" >&2
    echo "${output}" >&2
    exit 1
fi
echo "OK: checked-in metadata agrees"

echo "release version consistency checks passed"
