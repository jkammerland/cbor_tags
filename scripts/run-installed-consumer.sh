#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source_dir="${CBOR_TAGS_SOURCE_DIR:-$(cd -- "${script_dir}/.." && pwd)}"
name="${CBOR_TAGS_CONSUMER_NAME:-local}"
build_dir="${CBOR_TAGS_BUILD_DIR:-${source_dir}/build/install-consumer-${name}}"
install_prefix="${CBOR_TAGS_INSTALL_PREFIX:-${build_dir}/prefix}"
consumer_dir="${CBOR_TAGS_CONSUMER_DIR:-${build_dir}/consumer}"
cxx_standard="${CBOR_TAGS_CXX_STANDARD:-20}"
configure_flags="${CBOR_TAGS_CONFIGURE_FLAGS:-}"
vcpkg_features="${CBOR_TAGS_VCPKG_FEATURES:-}"
vcpkg_no_default_features="${CBOR_TAGS_VCPKG_NO_DEFAULT_FEATURES:-OFF}"
consumer_mode="${CBOR_TAGS_CONSUMER_MODE:-default}"
generator="${CBOR_TAGS_CMAKE_GENERATOR:-${CMAKE_GENERATOR:-Ninja}}"

configure_args=()
if [[ -n "${configure_flags}" ]]; then
  read -r -a configure_args <<< "${configure_flags}"
fi

feature_args=()
if [[ "${vcpkg_no_default_features}" == "ON" ]]; then
  feature_args+=(-DVCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON)
fi
if [[ -n "${vcpkg_features}" ]]; then
  feature_args+=(-DVCPKG_MANIFEST_FEATURES="${vcpkg_features}")
fi

toolchain_args=()
if [[ -n "${CMAKE_TOOLCHAIN_FILE:-}" ]]; then
  toolchain_args+=(-DCMAKE_TOOLCHAIN_FILE="${CMAKE_TOOLCHAIN_FILE}")
elif [[ -n "${VCPKG_ROOT:-}" ]]; then
  toolchain_args+=(-DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
fi

cmake -S "${source_dir}" -B "${build_dir}" -G "${generator}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCBOR_TAGS_BUILD_TESTS=OFF \
  -DCBOR_TAGS_INSTALL=ON \
  -DCMAKE_CXX_STANDARD="${cxx_standard}" \
  -DCMAKE_CXX_STANDARD_REQUIRED=ON \
  -DCMAKE_CXX_EXTENSIONS=OFF \
  -DCMAKE_INSTALL_PREFIX="${install_prefix}" \
  "${toolchain_args[@]}" \
  "${configure_args[@]}" \
  "${feature_args[@]}"

cmake --build "${build_dir}" --parallel
cmake --install "${build_dir}"

rm -rf "${consumer_dir}"
mkdir -p "${consumer_dir}"

cat > "${consumer_dir}/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.25)
project(cbor_tags_installed_consumer LANGUAGES CXX)
find_package(cbor_tags REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE cbor::tags)
set_property(TARGET consumer PROPERTY CXX_STANDARD ${cxx_standard})
set_property(TARGET consumer PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET consumer PROPERTY CXX_EXTENSIONS OFF)
EOF

case "${consumer_mode}" in
  magic_enum)
    cat > "${consumer_dir}/main.cpp" <<'EOF'
#include <cbor_tags/extensions/cbor_visualization.h>
#include <fmt/format.h>

#include <cstdint>
#include <string>

enum class Color : std::uint8_t { red = 1, green = 2 };

int main() {
    fmt::memory_buffer schema;
    cbor::tags::cddl_schema_to<Color>(
        schema,
        {.row_options = {.format_by_rows = false}, .enum_mode = cbor::tags::CDDLEnumMode::named_values});
    const auto text = fmt::to_string(schema);
    return text.find("red") == std::string::npos ? 1 : 0;
}
EOF
    ;;
  boost_pfr)
    cat > "${consumer_dir}/main.cpp" <<'EOF'
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_reflection_config.h>
#include <cbor_tags/extensions/cbor_visualization.h>
#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct Person {
    std::uint8_t age;
    std::string name;
};

int main() {
    static_assert(CBOR_TAGS_HAS_BOOST_PFR_NAMES == 1);

    fmt::memory_buffer schema;
    cbor::tags::cddl_schema_to<cbor::tags::as_named_map<Person>>(
        schema, {.row_options = {.format_by_rows = false}, .root_name = "person"});
    const auto text = fmt::to_string(schema);
    if (text.find("age: uint") == std::string::npos || text.find("name: tstr") == std::string::npos) {
        return 1;
    }

    Person input{.age = 42, .name = "Ada"};
    std::vector<std::byte> bytes;
    auto enc = cbor::tags::make_encoder(bytes);
    if (!enc(cbor::tags::as_named_map{input})) {
        return 2;
    }

    Person decoded{};
    auto dec = cbor::tags::make_decoder(bytes);
    if (!dec(cbor::tags::as_named_map{decoded})) {
        return 3;
    }
    return decoded.age == input.age && decoded.name == input.name ? 0 : 4;
}
EOF
    ;;
  default)
    cat > "${consumer_dir}/main.cpp" <<'EOF'
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>

#include <cstddef>
#include <cstdint>
#include <vector>

int main() {
    std::vector<std::byte> bytes;
    auto enc = cbor::tags::make_encoder(bytes);
    if (!enc(std::uint64_t{42})) {
        return 1;
    }

    std::uint64_t decoded{};
    auto dec = cbor::tags::make_decoder(bytes);
    if (!dec(decoded)) {
        return 2;
    }
    return decoded == 42U ? 0 : 3;
}
EOF
    ;;
  *)
    echo "Unknown CBOR_TAGS_CONSUMER_MODE: ${consumer_mode}" >&2
    exit 2
    ;;
esac

deps_prefix="${CBOR_TAGS_DEPS_PREFIX:-}"
if [[ -z "${deps_prefix}" && -n "${VCPKG_ROOT:-}" ]]; then
  deps_prefix="${build_dir}/vcpkg_installed/${VCPKG_DEFAULT_TRIPLET:-x64-linux}"
fi

prefix_path="${install_prefix}"
if [[ -n "${deps_prefix}" ]]; then
  prefix_path="${prefix_path};${deps_prefix}"
fi

cmake -S "${consumer_dir}" -B "${consumer_dir}/build" -G "${generator}" \
  -DCMAKE_PREFIX_PATH="${prefix_path}"
cmake --build "${consumer_dir}/build" --parallel
"${consumer_dir}/build/consumer"
