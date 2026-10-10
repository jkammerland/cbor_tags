#include <algorithm>
#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <doctest/doctest.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace cbor::tags;

TEST_SUITE("roundtrip/container_interoperability") {
    TEST_CASE("containers decode text views as elements") {
        const std::array<std::string, 2> source{"first", "second"};
        std::vector<std::byte>           bytes;
        REQUIRE(make_encoder(bytes)(source));
        const auto check = [&](auto &destination) {
            REQUIRE(make_decoder(bytes)(destination));
            REQUIRE(destination.size() == source.size());
            CHECK(std::ranges::equal(source, destination));
        };
        std::vector<std::string_view> dynamic;
        check(dynamic);
        std::array<std::string_view, 2> fixed;
        check(fixed);
    }

    TEST_CASE("containers decode byte spans as elements") {
        const std::array<std::vector<std::byte>, 2> source{{{std::byte{1}}, {std::byte{2}, std::byte{3}}}};
        std::vector<std::byte>                      bytes;
        REQUIRE(make_encoder(bytes)(source));
        const auto check = [&](auto &destination) {
            REQUIRE(make_decoder(bytes)(destination));
            REQUIRE(destination.size() == source.size());
            for (std::size_t index = 0; index < source.size(); ++index) {
                CHECK(std::ranges::equal(source[index], destination[index]));
            }
        };
        std::vector<std::span<const std::byte>> dynamic;
        check(dynamic);
        std::array<std::span<const std::byte>, 2> fixed;
        check(fixed);
    }
}
