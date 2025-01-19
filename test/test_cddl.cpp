#include "cbor_tags/extensions/cbor_cddl.h"

#include <doctest/doctest.h>
#include <fmt/format.h>
#include <test_util.h>
#include <vector>

using namespace cbor::tags;

TEST_CASE("CDDL extension") {
    fmt::memory_buffer     buffer;
    std::vector<std::byte> cbor_buffer = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    CDDL(cbor_buffer, buffer);
    fmt::print("CDDL: {}\n", fmt::to_string(buffer));
}