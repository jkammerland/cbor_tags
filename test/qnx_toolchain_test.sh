#!/usr/bin/env bash
set -euo pipefail

readonly cmake_command="${1:?usage: qnx_toolchain_test.sh <cmake> <toolchain>}"
readonly toolchain="${2:?usage: qnx_toolchain_test.sh <cmake> <toolchain>}"
readonly test_root="$(mktemp -d)"
trap 'rm -rf -- "${test_root}"' EXIT

readonly qnx_host="${test_root}/host"
readonly qnx_target="${test_root}/target/qnx"
mkdir -p -- "${qnx_host}/usr/bin" "${qnx_target}/x86_64" "${qnx_target}/aarch64le"
: >"${qnx_host}/usr/bin/qcc"
: >"${qnx_host}/usr/bin/q++"

verify_toolchain() {
    local architecture="$1"
    local verify_script="${test_root}/verify-${architecture}.cmake"
    sed \
        -e "s|@TOOLCHAIN@|${toolchain}|g" \
        -e "s|@QNX_HOST@|${qnx_host}|g" \
        -e "s|@QNX_TARGET@|${qnx_target}|g" \
        -e "s|@ARCH@|${architecture}|g" \
        "${test_root}/verify.cmake.in" >"${verify_script}"
    env QNX_HOST="${qnx_host}" QNX_TARGET="${qnx_target}" \
        "${cmake_command}" -DCBOR_TAGS_QNX_ARCH="${architecture}" -P "${verify_script}"
}

cat >"${test_root}/verify.cmake.in" <<'EOF'
include("@TOOLCHAIN@")

macro(assert_equal actual expected)
  if(NOT "${${actual}}" STREQUAL "${expected}")
    message(FATAL_ERROR "${actual} is '${${actual}}', expected '${expected}'")
  endif()
endmacro()

macro(assert_list_contains actual expected)
  list(FIND ${actual} "${expected}" item_index)
  if(item_index EQUAL -1)
    message(FATAL_ERROR "${actual} does not contain '${expected}'")
  endif()
endmacro()

assert_equal(CMAKE_SYSTEM_NAME QNX)
assert_equal(CMAKE_SYSTEM_PROCESSOR @ARCH@)
assert_equal(CMAKE_C_COMPILER @QNX_HOST@/usr/bin/qcc)
assert_equal(CMAKE_C_COMPILER_TARGET gcc_nto@ARCH@)
assert_equal(CMAKE_CXX_COMPILER @QNX_HOST@/usr/bin/q++)
assert_equal(CMAKE_CXX_COMPILER_TARGET gcc_nto@ARCH@)
assert_equal(CMAKE_FIND_ROOT_PATH @QNX_TARGET@/@ARCH@)
assert_equal(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
assert_equal(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
assert_equal(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
assert_equal(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
assert_list_contains(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CBOR_TAGS_QNX_ARCH)
EOF

verify_toolchain x86_64
verify_toolchain aarch64le

if env -u QNX_HOST QNX_TARGET="${qnx_target}" \
    "${cmake_command}" -P "${toolchain}" >"${test_root}/missing-host.log" 2>&1; then
    echo "QNX toolchain unexpectedly accepted a missing QNX_HOST" >&2
    exit 1
fi
grep -F "QNX_HOST is not set" "${test_root}/missing-host.log" >/dev/null

if env QNX_HOST="${qnx_host}" QNX_TARGET="${qnx_target}" \
    "${cmake_command}" -DCBOR_TAGS_QNX_ARCH=armv7 -P "${toolchain}" >"${test_root}/missing-sysroot.log" 2>&1; then
    echo "QNX toolchain unexpectedly accepted a missing architecture sysroot" >&2
    exit 1
fi
grep -F "QNX target sysroot does not exist" "${test_root}/missing-sysroot.log" >/dev/null

echo "QNX toolchain isolation: OK"
