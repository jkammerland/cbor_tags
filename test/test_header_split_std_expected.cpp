#if __has_include(<version>)
#include <version>
#endif

#if __has_include(<expected>) && defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L

// A public extension header should be enough for its codec wrappers and factories.
#include <cbor_tags/codec/std_expected.h>
#include <cstddef>
#include <doctest/doctest.h>
#include <expected>
#include <string>
#include <vector>

using namespace cbor::tags;

TEST_CASE("std::expected split header is directly usable") {
    std::vector<std::byte>          encoded;
    std::expected<int, std::string> value{42};
    auto                            enc = make_encoder<codec::std_expected>(encoded);
    REQUIRE(enc(value));

    std::expected<int, std::string> decoded{};
    auto                            dec = make_decoder<codec::std_expected>(encoded);
    REQUIRE(dec(decoded));
    REQUIRE(decoded.has_value());
    CHECK_EQ(*decoded, 42);
}

#endif
