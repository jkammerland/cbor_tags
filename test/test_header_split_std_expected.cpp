#if __has_include(<version>)
#include <version>
#endif

#if __has_include(<expected>) && defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L

// Keep the extension header first so this test catches direct include dependencies.
// clang-format off
#include <cbor_tags/extensions/std_expected.h>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
// clang-format on
#include <cstddef>
#include <doctest/doctest.h>
#include <expected>
#include <string>
#include <vector>

using namespace cbor::tags;
using namespace cbor::tags::ext::std_expected;

TEST_CASE("std::expected split header is directly usable") {
    std::vector<std::byte>          encoded;
    std::expected<int, std::string> value{42};
    auto                            enc = make_encoder<std_expected_codec>(encoded);
    REQUIRE(enc(value));

    std::expected<int, std::string> decoded{};
    auto                            dec = make_decoder<std_expected_codec>(encoded);
    REQUIRE(dec(decoded));
    REQUIRE(decoded.has_value());
    CHECK_EQ(*decoded, 42);
}

#endif
