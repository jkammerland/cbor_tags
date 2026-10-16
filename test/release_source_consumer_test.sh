#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
test_root="$(mktemp -d)"
trap 'rm -rf -- "${test_root}"' EXIT

# Use the real example target naming without rebuilding the library or fetching dependencies.
fixture="${test_root}/source"
mkdir -p -- "${fixture}/examples"
cp -- "${repo_root}/examples/CMakeLists.txt" "${fixture}/examples/"
[[ -f "${repo_root}/examples/01_basic_values.cpp" ]]

cat >"${fixture}/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.23)
project(release_source_consumer_fixture LANGUAGES CXX)
enable_testing()
add_library(cbor::tags INTERFACE IMPORTED)
add_subdirectory(examples)
CMAKE

cat >"${fixture}/examples/01_basic_values.cpp" <<'CPP'
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

# The example CMake file also registers a visitor test; only its target is needed here.
printf 'int main() { return 0; }\n' >"${fixture}/examples/14_item_visitor.cpp"
touch "${test_root}/toolchain.cmake"
export CBOR_TAGS_CONSUMER_MARKER="${test_root}/consumer-ran"
unset CBOR_TAGS_FAIL_CONSUMER

run_consumer() {
    bash -c '
        source "$1" --help >/dev/null
        run_source_consumer "$2" "$3" "$4"
    ' _ "${repo_root}/scripts/release.sh" "${fixture}" \
        "${test_root}/toolchain.cmake" "${test_root}/build"
}

# A successful build is insufficient: the release driver must execute the consumer.
if ! run_consumer >"${test_root}/success.log" 2>&1; then
    cat "${test_root}/success.log" >&2
    exit 1
fi
grep -Fx 'source consumer ran' "${CBOR_TAGS_CONSUMER_MARKER}"

# A failed round trip must stop release validation rather than being ignored.
consumer_status=0
CBOR_TAGS_FAIL_CONSUMER=1 run_consumer >"${test_root}/failure.log" 2>&1 || consumer_status=$?
if [[ "${consumer_status}" -ne 23 ]]; then
    cat "${test_root}/failure.log" >&2
    echo "expected consumer exit 23, got ${consumer_status}" >&2
    exit 1
fi

echo 'release source consumer checks passed'
