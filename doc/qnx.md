# QNX 8

`cbor_tags` supports cross-compiling its C++20 library and tests with QNX SDP
8.0. Source the SDP environment, then use the repository toolchain file so
host libraries and CMake packages cannot be selected for QNX targets:

```bash
source /path/to/qnx800/qnxsdp-env.sh

cmake -S . -B build-qnx -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/qnx.cmake \
  -DCBOR_TAGS_BUILD_TESTS=ON \
  -DCBOR_TAGS_BUILD_CLI=OFF \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-qnx --parallel
```

The default target is `x86_64`. Select the QNX 8 ARM64 target with
`-DCBOR_TAGS_QNX_ARCH=aarch64le`. The toolchain passes the matching
`gcc_nto<architecture>` target to both QCC compiler drivers, so the ARM64
setting selects `gcc_ntoaarch64le` as well as the ARM64 sysroot.

The toolchain reads `QNX_HOST` and `QNX_TARGET` from `qnxsdp-env.sh`. Host
program lookup remains enabled, while library, include, and package lookup is
restricted to `${QNX_TARGET}/${CBOR_TAGS_QNX_ARCH}`. Dependencies unavailable
there are therefore cross-built through the project's normal CPM path instead
of accidentally linking host binaries. Do not pass a host package directory
such as `fmt_DIR` into a QNX build.

QCC does not expose its libc++ selection in the form expected by doctest. The
project consequently enables `DOCTEST_CONFIG_USE_STD_HEADERS` on its QNX
doctest targets; no global `CMAKE_CXX_FLAGS` workaround is needed.

Cross-compiled tests must run on QNX rather than on the build host. Copy the
executables from `build-qnx/test/` to the target and run at least:

```sh
./tests
./test_decode_stack_floor
./test_smart_ptr_allocation_failure
./test_smart_ptr_decode_allocations
./test_smart_ptr_expected_classification
./test_unsigned_char
```

The main executable reports doctest totals. Each additional executable must
return status zero.
