#if __has_include(<expected>)
#include <expected>
#endif

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/extensions/std_expected.h>
#include <cstddef>
#include <doctest/doctest.h>
#include <expected>
#include <string>
#include <vector>

TEST_CASE("std::expected split header is directly usable") {
    std::vector<std::byte>          encoded;
    std::expected<int, std::string> value{42};
    auto                            enc = cbor::tags::make_encoder<cbor::tags::ext::std_expected::std_expected_codec>(encoded);
    REQUIRE(enc(value));

    std::expected<int, std::string> decoded{};
    auto                            dec = cbor::tags::make_decoder<cbor::tags::ext::std_expected::std_expected_codec>(encoded);
    REQUIRE(dec(decoded));
    REQUIRE(decoded.has_value());
    CHECK_EQ(*decoded, 42);
}

#endif
