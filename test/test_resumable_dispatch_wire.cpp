#include <array>
#include <cbor_tags/cbor_resumable.h>
#include <cstdint>
#include <doctest/doctest.h>
#include <list>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace ct = cbor::tags;

namespace {

struct packet {
    int value{};
    packet() = default;
    explicit packet(int n) : value(n) {}
};

// Each native hook has exactly the behavior of its matching one-shot overload.
template <typename Self> struct broad_codec : ct::codec::base<Self> {
    using ct::codec::base<Self>::encode;
    using ct::codec::base<Self>::decode;

    template <typename T>
        requires std::same_as<T, packet>
    void encode(const T &value) {
        static_cast<Self &>(*this).encode(value.value + 100);
    }
    template <typename T>
        requires std::same_as<T, packet>
    ct::status_code decode(T &value) {
        int        next{};
        const auto status = static_cast<Self &>(*this).decode(next);
        if (status == ct::status_code::success)
            value.value = next + 100;
        return status;
    }
    template <typename T>
        requires std::same_as<T, packet>
    ct::resumable<ct::status_code> resumable_encode(const T &value) {
        const int next = value.value + 100;
        co_return co_await static_cast<Self &>(*this)(next);
    }
    template <typename T>
        requires std::same_as<T, packet>
    ct::resumable<ct::status_code> resumable_decode(T &value) {
        int        next{};
        const auto status = co_await static_cast<Self &>(*this)(next);
        if (status == ct::status_code::success)
            value.value = next + 100;
        co_return status;
    }
};

template <typename Self> struct exact_codec : ct::codec::base<Self> {
    using ct::codec::base<Self>::encode;
    using ct::codec::base<Self>::decode;

    void            encode(const packet &value) { static_cast<Self &>(*this).encode(value.value + 200); }
    ct::status_code decode(packet &value) {
        int        next{};
        const auto status = static_cast<Self &>(*this).decode(next);
        if (status == ct::status_code::success)
            value.value = next + 200;
        return status;
    }
    ct::resumable<ct::status_code> resumable_encode(const packet &value) {
        const int next = value.value + 200;
        co_return co_await static_cast<Self &>(*this)(next);
    }
    ct::resumable<ct::status_code> resumable_decode(packet &value) {
        int        next{};
        const auto status = co_await static_cast<Self &>(*this)(next);
        if (status == ct::status_code::success)
            value.value = next + 200;
        co_return status;
    }
};

template <template <typename> typename... Codecs> void check_overloads() {
    const std::array input{std::byte{0x18}, std::byte{0x2a}};
    packet           ordinary;
    REQUIRE(ct::make_decoder<Codecs...>(input)(ordinary));
    REQUIRE(ordinary.value == 242);

    for (std::size_t split = 0; split <= input.size(); ++split) {
        CAPTURE(split);
        std::list<std::span<const std::byte>> chunks{std::span{input}.first(split)};
        auto                                  decoder = ct::make_resumable_decoder<Codecs...>(chunks);
        std::optional<packet>                 decoded;
        auto                                  operation = decoder(decoded);
        if (split != input.size()) {
            REQUIRE_FALSE(operation.done());
            chunks.push_back(std::span{input}.subspan(split));
            operation.resume();
        }
        REQUIRE(operation.done());
        REQUIRE(operation.result());
        REQUIRE(decoded.has_value());
        CHECK(decoded->value == 242);
    }

    const packet           value{7};
    const std::array       expected{std::byte{0x18}, std::byte{0xcf}};
    std::vector<std::byte> ordinary_output;
    REQUIRE(ct::make_encoder<Codecs...>(ordinary_output)(value));
    CHECK(std::ranges::equal(ordinary_output, expected));
    for (std::size_t split = 0; split <= expected.size(); ++split) {
        CAPTURE(split);
        std::array<std::byte, 2>        bytes{};
        std::list<std::span<std::byte>> chunks{std::span{bytes}.first(split)};
        auto                            encoder   = ct::make_resumable_encoder<Codecs...>(chunks);
        auto                            operation = encoder(value);
        if (split != bytes.size()) {
            REQUIRE_FALSE(operation.done());
            chunks.push_back(std::span{bytes}.subspan(split));
            operation.resume();
        }
        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(bytes == expected);
    }
}

using inner = std::variant<std::uint32_t, std::string>;
using outer = std::variant<inner, bool>;

template <typename Self> struct inner_codec : ct::codec::decoder_base<Self> {
    using ct::codec::decoder_base<Self>::decode;
    ct::status_code decode(inner &value, ct::major_type major, std::byte info) {
        if (major == ct::major_type::TextString) {
            std::string text;
            const auto  status = static_cast<Self &>(*this).decode(text, major, info);
            if (status == ct::status_code::success)
                value = std::move(text);
            return status;
        }
        std::uint32_t next{};
        const auto    status = static_cast<Self &>(*this).decode(next, major, info);
        if (status == ct::status_code::success)
            value = next + 1000;
        return status;
    }
};

} // namespace

namespace cbor::tags {
template <typename Self> struct resumable_retry_traits<inner, inner_codec<Self>> {
    static constexpr bool decode = true;
};
} // namespace cbor::tags

TEST_SUITE("cbor_wire/resumable") {
    TEST_CASE("mixin overload resolution is independent of installation order") {
        check_overloads<broad_codec, exact_codec>();
        check_overloads<exact_codec, broad_codec>();
    }

    TEST_CASE("nested variant selection preserves the one-shot codec route") {
        const std::array input{std::byte{0x19}, std::byte{0x01}, std::byte{0x00}, std::byte{0x07}};
        inner            direct;
        REQUIRE(ct::make_decoder<inner_codec>(input)(direct));
        REQUIRE(std::get<0>(direct) == 1256);

        // Direct and optional values still use the codec in resumable mode.
        auto direct_decoder   = ct::make_resumable_decoder<inner_codec>(input);
        auto direct_operation = direct_decoder(direct);
        REQUIRE(direct_operation.done());
        REQUIRE(direct_operation.result());
        CHECK(std::get<0>(direct) == 1256);
        std::optional<inner> optional;
        auto                 optional_decoder   = ct::make_resumable_decoder<inner_codec>(input);
        auto                 optional_operation = optional_decoder(optional);
        REQUIRE(optional_operation.done());
        REQUIRE(optional_operation.result());
        REQUIRE(optional.has_value());
        CHECK(std::get<0>(*optional) == 1256);

        outer ordinary;
        int   trailer{};
        REQUIRE(ct::make_decoder<inner_codec>(input)(ordinary, trailer));
        REQUIRE(std::get<0>(std::get<0>(ordinary)) == 256);
        REQUIRE(trailer == 7);

        for (std::size_t split = 0; split <= input.size(); ++split) {
            CAPTURE(split);
            std::list<std::span<const std::byte>> chunks{std::span{input}.first(split)};
            auto                                  decoder = ct::make_resumable_decoder<inner_codec>(chunks);
            outer                                 decoded;
            trailer        = 0;
            auto operation = decoder(decoded, trailer);
            if (split != input.size()) {
                REQUIRE_FALSE(operation.done());
                chunks.push_back(std::span{input}.subspan(split));
                operation.resume();
            }
            REQUIRE(operation.done());
            REQUIRE(operation.result());
            CHECK(*operation.result() == input.size());
            CHECK(std::get<0>(std::get<0>(decoded)) == 256);
            CHECK(trailer == 7);
        }
    }
}
