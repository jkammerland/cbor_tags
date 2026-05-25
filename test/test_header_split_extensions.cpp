// A custom codec user should not need an order-sensitive encoder/decoder include recipe.
#include <cbor_tags/cbor_extensions.h>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <vector>

using namespace cbor::tags;

namespace {

struct split_codec_value {
    std::uint64_t value{};

    friend constexpr bool operator==(split_codec_value, split_codec_value) = default;
};

template <typename Self> struct split_codec : cbor_codec_mixin_base<Self> {
    using cbor_codec_mixin_base<Self>::decode;
    using cbor_codec_mixin_base<Self>::encode;

    constexpr void encode(split_codec_value value) { static_cast<Self &>(*this).encode(value.value); }

    constexpr status_code decode(split_codec_value &value, major_type major, std::byte additional_info) {
        std::uint64_t decoded{};
        const auto    status = static_cast<Self &>(*this).decode(decoded, major, additional_info);
        if (status != status_code::success) {
            return status;
        }
        value = split_codec_value{decoded};
        return status_code::success;
    }
};

template <typename Self> struct split_encoder_only : cbor_encoder_mixin_base<Self> {
    using cbor_encoder_mixin_base<Self>::encode;

    constexpr void encode(split_codec_value value) { static_cast<Self &>(*this).encode(value.value); }
};

template <typename Self> struct split_decoder_only : cbor_decoder_mixin_base<Self> {
    using cbor_decoder_mixin_base<Self>::decode;

    constexpr status_code decode(split_codec_value &value, major_type major, std::byte additional_info) {
        std::uint64_t decoded{};
        const auto    status = static_cast<Self &>(*this).decode(decoded, major, additional_info);
        if (status != status_code::success) {
            return status;
        }
        value = split_codec_value{decoded};
        return status_code::success;
    }
};

} // namespace

TEST_CASE("extension mixin base split header is directly usable") {
    std::vector<std::byte> encoded;
    auto                   enc = make_encoder<split_codec>(encoded);
    REQUIRE(enc(split_codec_value{42}));

    split_codec_value decoded{};
    auto              dec = make_decoder<split_codec>(encoded);
    REQUIRE(dec(decoded));
    CHECK(decoded == split_codec_value{42});
}

TEST_CASE("directional extension mixin bases support encoder-only and decoder-only codecs") {
    std::vector<std::byte> encoded;
    auto                   enc = make_encoder<split_encoder_only>(encoded);
    REQUIRE(enc(split_codec_value{7}));

    split_codec_value decoded{};
    auto              dec = make_decoder<split_decoder_only>(encoded);
    REQUIRE(dec(decoded));
    CHECK(decoded == split_codec_value{7});
}
