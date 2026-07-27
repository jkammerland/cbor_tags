#include "test_util.h"

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/extensions/custom_codec_1.h>
#include <cbor_tags/extensions/rfc8746_typed_arrays.h>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <list>
#include <memory>
#include <vector>

using namespace cbor::tags;
using namespace cbor::tags::ext::rfc8746;

namespace {

struct CountingUnsizedByteRange {
    using value_type = std::byte;

    struct iterator {
        using value_type        = std::byte;
        using difference_type   = std::ptrdiff_t;
        using iterator_concept  = std::bidirectional_iterator_tag;
        using iterator_category = std::bidirectional_iterator_tag;

        std::list<std::byte>::const_iterator current{};
        std::size_t                         *increments{};

        [[nodiscard]] const std::byte &operator*() const noexcept { return *current; }
        [[nodiscard]] const std::byte *operator->() const noexcept { return std::addressof(*current); }

        iterator &operator++() noexcept {
            ++current;
            ++*increments;
            return *this;
        }
        iterator operator++(int) noexcept {
            auto copy = *this;
            ++*this;
            return copy;
        }
        iterator &operator--() noexcept {
            --current;
            return *this;
        }
        iterator operator--(int) noexcept {
            auto copy = *this;
            --*this;
            return copy;
        }

        friend bool operator==(const iterator &, const iterator &) = default;
    };

    std::list<std::byte> bytes;
    mutable std::size_t  increments{};

    [[nodiscard]] iterator begin() const noexcept { return {bytes.cbegin(), &increments}; }
    [[nodiscard]] iterator end() const noexcept { return {bytes.cend(), &increments}; }
};

} // namespace

TEST_CASE("extension payloads consume unsized non-contiguous input once") {
    SUBCASE("owned typed arrays") {
        CountingUnsizedByteRange  input{{std::byte{0xD8}, std::byte{0x40}, std::byte{0x45}, std::byte{0x01}, std::byte{0x02},
                                         std::byte{0x03}, std::byte{0x04}, std::byte{0x05}}};
        typed_array<std::uint8_t> decoded;
        auto                      dec = make_decoder<typed_array_codec>(input);

        REQUIRE(dec(decoded));
        CHECK_EQ(decoded.values(), (std::vector<std::uint8_t>{1, 2, 3, 4, 5}));
        CHECK_EQ(input.increments, input.bytes.size());
        CHECK(dec.tell() == input.end());
    }

    SUBCASE("typed array views") {
        CountingUnsizedByteRange input{{std::byte{0xD8}, std::byte{0x40}, std::byte{0x45}, std::byte{0x01}, std::byte{0x02},
                                        std::byte{0x03}, std::byte{0x04}, std::byte{0x05}}};
        auto                     dec = make_decoder<typed_array_codec>(input);
        using view_type              = typed_array_view_for<std::uint8_t, decltype(dec)>;
        view_type decoded;

        REQUIRE(dec(decoded));
        CHECK_EQ(input.increments, input.bytes.size());
        CHECK(dec.tell() == input.end());
        CHECK_EQ(decoded.copy_values(), (std::vector<std::uint8_t>{1, 2, 3, 4, 5}));
    }

    SUBCASE("bounded owned typed arrays") {
        CountingUnsizedByteRange input{{std::byte{0xD8}, std::byte{0x40}, std::byte{0x45}, std::byte{0x01}, std::byte{0x02},
                                        std::byte{0x03}, std::byte{0x04}, std::byte{0x05}}};
        bounded_size<typed_array<std::uint8_t>, 1, 5> decoded;
        auto                                          dec = make_decoder<typed_array_codec>(input);

        REQUIRE(dec(decoded));
        CHECK_EQ(decoded.value().values(), (std::vector<std::uint8_t>{1, 2, 3, 4, 5}));
        CHECK_EQ(input.increments, input.bytes.size());
        CHECK(dec.tell() == input.end());
    }

    SUBCASE("bounded typed array views") {
        CountingUnsizedByteRange input{{std::byte{0xD8}, std::byte{0x40}, std::byte{0x45}, std::byte{0x01}, std::byte{0x02},
                                        std::byte{0x03}, std::byte{0x04}, std::byte{0x05}}};
        auto                     dec = make_decoder<typed_array_codec>(input);
        using view_type              = typed_array_view_for<std::uint8_t, decltype(dec)>;
        bounded_size<view_type, 1, 5> decoded;

        REQUIRE(dec(decoded));
        CHECK_EQ(input.increments, input.bytes.size());
        CHECK(dec.tell() == input.end());
        CHECK_EQ(decoded.value().copy_values(), (std::vector<std::uint8_t>{1, 2, 3, 4, 5}));
    }

    SUBCASE("owning custom_codec_1 values") {
        CountingUnsizedByteRange input{
            {std::byte{0xC1}, std::byte{0x44}, std::byte{0x03}, std::byte{0x11}, std::byte{0x22}, std::byte{0x33}}};
        std::vector<std::uint8_t> decoded;
        auto                      dec = make_decoder<cbor::tags::ext::custom_codec_1::custom_codec_1>(input);

        REQUIRE(dec(cbor::tags::ext::custom_codec_1::as_custom_codec_1(static_tag<1>{}, decoded)));
        CHECK_EQ(decoded, (std::vector<std::uint8_t>{0x11, 0x22, 0x33}));
        CHECK_EQ(input.increments, input.bytes.size());
        CHECK(dec.tell() == input.end());
    }
}

TEST_CASE("extension payloads retain terminal incomplete behavior for unsized input") {
    SUBCASE("owned typed arrays") {
        CountingUnsizedByteRange  input{{std::byte{0xD8}, std::byte{0x40}, std::byte{0x45}, std::byte{0x01}, std::byte{0x02}}};
        typed_array<std::uint8_t> decoded{{0xAA}};
        auto                      dec = make_decoder<typed_array_codec>(input);

        const auto result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
        CHECK_EQ(decoded.values(), (std::vector<std::uint8_t>{0xAA}));
        CHECK_EQ(input.increments, input.bytes.size());
        CHECK(dec.tell() == input.end());
    }

    SUBCASE("typed array views report incomplete from direct extension dispatch") {
        CountingUnsizedByteRange input{{std::byte{0xD8}, std::byte{0x40}, std::byte{0x45}, std::byte{0x01}, std::byte{0x02}}};
        auto                     dec = make_decoder<typed_array_codec>(input);
        using view_type              = typed_array_view_for<std::uint8_t, decltype(dec)>;
        view_type decoded;
        const auto [major, additional_info] = dec.read_initial_byte();
        auto status                         = status_code::success;

        CHECK_NOTHROW(status = dec.decode(decoded, major, additional_info));
        CHECK_EQ(status, status_code::incomplete);
        CHECK_EQ(input.increments, input.bytes.size());
        CHECK(dec.tell() == input.end());
    }

    SUBCASE("owning custom_codec_1 values") {
        CountingUnsizedByteRange  input{{std::byte{0xC1}, std::byte{0x44}, std::byte{0x03}, std::byte{0x11}}};
        std::vector<std::uint8_t> decoded{0xAA};
        auto                      dec = make_decoder<cbor::tags::ext::custom_codec_1::custom_codec_1>(input);

        const auto result = dec(cbor::tags::ext::custom_codec_1::as_custom_codec_1(static_tag<1>{}, decoded));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
        CHECK_EQ(decoded, (std::vector<std::uint8_t>{0xAA}));
        CHECK_EQ(input.increments, input.bytes.size());
        CHECK(dec.tell() == input.end());
    }
}
