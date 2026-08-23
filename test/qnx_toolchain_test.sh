#!/usr/bin/env bash
set -euo pipefail

readonly cmake_command="${1:?usage: qnx_toolchain_test.sh <cmake> <toolchain> <generator>}"
readonly toolchain="${2:?usage: qnx_toolchain_test.sh <cmake> <toolchain> <generator>}"
readonly cmake_generator="${3:?usage: qnx_toolchain_test.sh <cmake> <toolchain> <generator>}"
readonly test_root="$(mktemp -d)"
trap 'rm -rf -- "${test_root}"' EXIT

readonly qnx_host="${test_root}/host"
readonly qnx_target="${test_root}/target/qnx"
mkdir -p -- "${qnx_host}/usr/bin" "${qnx_target}/x86_64" "${qnx_target}/aarch64le"
for qnx_compiler in qcc q++; do
    cat >"${qnx_host}/usr/bin/${qnx_compiler}" <<'EOF'
#!/usr/bin/env bash
exit 0
EOF
    chmod +x "${qnx_host}/usr/bin/${qnx_compiler}"
done

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

readonly reconfigure_source="${test_root}/reconfigure-source"
readonly reconfigure_build="${test_root}/reconfigure-build"
mkdir -p -- "${reconfigure_source}"
cat >"${reconfigure_source}/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.23)
project(qnx_toolchain_reconfigure LANGUAGES C CXX)

function(assert_equal actual expected)
  if(NOT "${${actual}}" STREQUAL "${expected}")
    message(FATAL_ERROR "${actual} is '${${actual}}', expected '${expected}'")
  endif()
endfunction()

assert_equal(CMAKE_C_COMPILER_TARGET "gcc_nto${EXPECTED_ARCH}")
assert_equal(CMAKE_CXX_COMPILER_TARGET "gcc_nto${EXPECTED_ARCH}")
assert_equal(CMAKE_FIND_ROOT_PATH "${EXPECTED_ROOT}")
EOF

configure_reused_build() {
    local architecture="$1"
    env QNX_HOST="${qnx_host}" QNX_TARGET="${qnx_target}" \
        "${cmake_command}" \
        -S "${reconfigure_source}" \
        -B "${reconfigure_build}" \
        -G "${cmake_generator}" \
        -DCMAKE_TOOLCHAIN_FILE="${toolchain}" \
        -DCMAKE_C_COMPILER_WORKS=TRUE \
        -DCMAKE_CXX_COMPILER_WORKS=TRUE \
        -DCBOR_TAGS_QNX_ARCH="${architecture}" \
        -DEXPECTED_ARCH="${architecture}" \
        -DEXPECTED_ROOT="${qnx_target}/${architecture}"
}

configure_reused_build x86_64
configure_reused_build aarch64le

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
