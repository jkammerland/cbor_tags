#include "test_util.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cbor_tags/cbor_encoder.h>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <memory>
#include <new>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace cbor::tags;

namespace {

struct construction_state {
    std::size_t calls{};
    std::size_t successful_limit{};
};

template <typename T> struct throwing_construct_allocator {
    using value_type = T;
    construction_state *state{};

    throwing_construct_allocator() = default;
    explicit throwing_construct_allocator(construction_state &value) : state(&value) {}
    template <typename U> throwing_construct_allocator(const throwing_construct_allocator<U> &other) : state(other.state) {}

    T   *allocate(std::size_t count) { return std::allocator<T>{}.allocate(count); }
    void deallocate(T *pointer, std::size_t count) { std::allocator<T>{}.deallocate(pointer, count); }

    template <typename U, typename... Args> void construct(U *pointer, Args &&...args) {
        ++state->calls;
        if (state->calls > state->successful_limit) {
            throw std::bad_alloc{};
        }
        std::construct_at(pointer, std::forward<Args>(args)...);
    }

    friend bool operator==(const throwing_construct_allocator &, const throwing_construct_allocator &) = default;
};

template <typename Self> struct float_array_codec : codec::encoder_base<Self> {
    using codec::encoder_base<Self>::encode;

    void encode(const std::vector<float> &) { static_cast<Self &>(*this).encode(42U); }
};

template <typename Self> struct passthrough_codec : codec::encoder_base<Self> {
    using codec::encoder_base<Self>::encode;
};

template <typename Float> constexpr bool float_array_is_constexpr() {
    const std::array<Float, 2>                         source{Float{1}, Float{-2.5}};
    std::array<std::byte, 1 + 2 * (sizeof(Float) + 1)> output{};
    encoder<decltype(output), default_options, cbor_header_encoder, cbor_indefinite_encoder, cbor_optional_encoder, cbor_variant_encoder>
        enc{output};
    enc.encode(source);
    if constexpr (std::same_as<Float, float>) {
        return output == std::array<std::byte, 11>{std::byte{0x82}, std::byte{0xfa}, std::byte{0x3f}, std::byte{0x80},
                                                   std::byte{0x00}, std::byte{0x00}, std::byte{0xfa}, std::byte{0xc0},
                                                   std::byte{0x20}, std::byte{0x00}, std::byte{0x00}};
    } else {
        return output == std::array<std::byte, 19>{std::byte{0x82}, std::byte{0xfb}, std::byte{0x3f}, std::byte{0xf0}, std::byte{0x00},
                                                   std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
                                                   std::byte{0xfb}, std::byte{0xc0}, std::byte{0x04}, std::byte{0x00}, std::byte{0x00},
                                                   std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
    }
}

static_assert(float_array_is_constexpr<float>());
static_assert(float_array_is_constexpr<double>());

} // namespace

TEST_SUITE("cbor_wire/float_arrays") {

    TEST_CASE("ordinary float arrays preserve exact float bit patterns") {
        const std::array<float, 6> source{1.0F,
                                          -2.5F,
                                          -0.0F,
                                          std::bit_cast<float>(std::uint32_t{0x7f800000}),
                                          std::bit_cast<float>(std::uint32_t{0xff800000}),
                                          std::bit_cast<float>(std::uint32_t{0x7fc12345})};
        for (const bool reserved : {false, true}) {
            CAPTURE(reserved);
            std::vector<std::byte> output;
            if (reserved) {
                output.reserve(64);
            }
            REQUIRE(make_encoder(output)(source));
            CHECK(to_hex(output) == "86fa3f800000fac0200000fa80000000fa7f800000faff800000fa7fc12345");
        }
    }

    TEST_CASE("ordinary double arrays preserve exact double bit patterns") {
        const std::vector<double> source{1.0,
                                         -2.5,
                                         -0.0,
                                         std::bit_cast<double>(std::uint64_t{0x7ff0000000000000}),
                                         std::bit_cast<double>(std::uint64_t{0xfff0000000000000}),
                                         std::bit_cast<double>(std::uint64_t{0x7ff8123456789abc})};
        std::vector<std::byte>    output;
        output.reserve(64);
        REQUIRE(make_encoder(output)(source));
        CHECK(to_hex(output) ==
              "86fb3ff0000000000000fbc004000000000000fb8000000000000000fb7ff0000000000000fbfff0000000000000fb7ff8123456789abc");
    }

    TEST_CASE("ordinary float arrays compose at the existing output cursor") {
        const std::array<float, 2> source{1.0F, -2.5F};
        std::vector<std::byte>     output;
        output.reserve(32);
        auto enc = make_encoder(output);
        REQUIRE(enc(7, source, 8));
        CHECK(to_hex(output) == "0782fa3f800000fac020000008");

        std::array<std::byte, 14> fixed;
        fixed.fill(std::byte{0xee});
        auto fixed_enc = make_encoder(fixed);
        REQUIRE(fixed_enc(7, source, 8));
        CHECK(fixed_enc.appender_.head_ == 13U);
        CHECK(to_hex(std::span<const std::byte>{fixed}.first(13)) == "0782fa3f800000fac020000008");
        CHECK(fixed.back() == std::byte{0xee});
    }

    TEST_CASE_TEMPLATE("ordinary float arrays support each output byte type", Byte, std::byte, char, unsigned char) {
        const std::array<float, 2> source{1.0F, -2.5F};
        std::vector<Byte>          output;
        output.reserve(32);
        REQUIRE(make_encoder(output)(source));
        CHECK(to_hex(output) == "82fa3f800000fac0200000");
    }

    TEST_CASE("ordinary float arrays use exact remaining vector capacity") {
        const std::array<float, 2> source{1.0F, -2.5F};
        std::vector<std::byte>     output;
        output.reserve(32);
        const auto capacity    = output.capacity();
        const auto prefix_size = capacity - 11;
        REQUIRE(prefix_size > 0);
        output.resize(prefix_size, std::byte{0xee});
        const auto *data = output.data();
        REQUIRE(make_encoder(output)(source));
        CHECK(output.size() == capacity);
        CHECK(output.data() == data);
        CHECK(
            std::ranges::all_of(std::span<const std::byte>{output}.first(prefix_size), [](auto byte) { return byte == std::byte{0xee}; }));
        CHECK(to_hex(std::span<const std::byte>{output}.subspan(prefix_size)) == "82fa3f800000fac0200000");
    }

    TEST_CASE_TEMPLATE("fixed float output keeps scalar cadence when storage overlaps", Float, float, double) {
        struct backing {
            std::array<Float, 2>      source{Float{1}, Float{-2.5}};
            std::array<std::byte, 16> tail{};
        };
        backing optimized;
        backing scalar;
        auto    optimized_bytes = std::as_writable_bytes(std::span{&optimized, 1});
        auto    scalar_bytes    = std::as_writable_bytes(std::span{&scalar, 1});
        auto    optimized_enc   = make_encoder(optimized_bytes);
        auto    scalar_enc      = make_encoder<passthrough_codec>(scalar_bytes);
        REQUIRE(optimized_enc(optimized.source));
        REQUIRE(scalar_enc(scalar.source));
        const auto encoded_size = 1 + 2 * (sizeof(Float) + 1);
        CHECK(optimized_enc.appender_.head_ == encoded_size);
        CHECK(scalar_enc.appender_.head_ == encoded_size);
        CHECK(std::ranges::equal(optimized_bytes.first(encoded_size), scalar_bytes.first(encoded_size)));
    }

    TEST_CASE("short fixed float output retains complete scalar prefix") {
        const std::array<float, 2> source{1.0F, -2.5F};
        for (const std::size_t capacity : {1U, 5U, 6U, 10U}) {
            CAPTURE(capacity);
            std::array<std::byte, 10> storage;
            storage.fill(std::byte{0xee});
            auto       output = std::span<std::byte>{storage}.first(capacity);
            auto       enc    = make_encoder(output);
            const auto result = enc(source);
            REQUIRE_FALSE(result);
            CHECK(result.error() == status_code::error);
            const auto prefix_size = capacity < 6 ? 1U : 6U;
            CHECK(enc.appender_.head_ == prefix_size);
            CHECK(to_hex(std::span<const std::byte>{storage}.first(prefix_size)) == (capacity < 6 ? "82" : "82fa3f800000"));
            CHECK(storage[prefix_size] == std::byte{0xee});
        }
    }

    TEST_CASE("custom output allocator keeps scalar construction and failure prefix") {
        const std::array<float, 2>                                      source{1.0F, -2.5F};
        construction_state                                              state{0, 6};
        std::vector<std::byte, throwing_construct_allocator<std::byte>> output{throwing_construct_allocator<std::byte>{state}};
        output.reserve(32);
        const auto result = make_encoder(output)(source);
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::out_of_memory);
        CHECK(state.calls == 7U);
        CHECK(to_hex(output) == "82fa3f800000");
    }

    TEST_CASE("throwing float source retains one traversal and the encoded prefix") {
        const std::array<float, 2> values{1.0F, -2.5F};
        std::size_t                calls{};
        const auto                 source = values | std::views::transform([&](float item) {
                                if (++calls == 2) {
                                    throw std::runtime_error("source callback failed");
                                }
                                return item;
                                            });
        std::vector<std::byte>     output;
        output.reserve(32);
        const auto result = make_encoder(output)(source);
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::error);
        CHECK(calls == 2U);
        CHECK(to_hex(output) == "82fa3f800000");
    }

    TEST_CASE("custom array codec keeps overload dispatch") {
        const std::vector<float> source{1.0F, -2.5F};
        std::vector<std::byte>   output;
        output.reserve(32);
        REQUIRE(make_encoder<float_array_codec>(output)(source));
        CHECK(to_hex(output) == "182a");
    }
}
