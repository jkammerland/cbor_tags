#include <bit>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <cbor_tags/codec.h>
#include <deque>
#include <doctest/doctest.h>
#include <limits>
#include <list>
#include <map>
#include <ranges>
#include <span>
#include <vector>

using namespace cbor::tags;

namespace {

using input_chunks  = std::list<std::span<const std::byte>>;
using output_chunks = std::list<std::span<std::byte>>;

template <typename Value, typename Wrapped> void check_leaf_splits(const Value &expected, Wrapped &wrapped) {
    std::vector<std::byte> wire;
    REQUIRE(make_encoder(wire)(wrapped));
    for (std::size_t cut = 0; cut <= wire.size(); ++cut) {
        CAPTURE(cut);
        input_chunks storage;
        auto         source = make_input_source(storage);
        Value        actual;
        auto         decoder = make_resumable_decoder(source)(actual);
        decoder(std::span<const std::byte>{wire}.first(cut));
        if (!decoder.done())
            decoder(std::span<const std::byte>{wire}.subspan(cut), end_of_input);
        REQUIRE(decoder.done());
        REQUIRE(decoder.result());
        CHECK(*decoder.result() == wire.size());
        CHECK(actual == expected);

        std::vector<std::byte> encoded(wire.size());
        output_chunks          output;
        auto                   sink    = make_output_source(output);
        auto                   encoder = make_resumable_encoder(sink)(wrapped);
        encoder(std::span{encoded}.first(cut));
        if (!encoder.done())
            encoder(std::span{encoded}.subspan(cut));
        REQUIRE(encoder.done());
        REQUIRE(encoder.result());
        CHECK(*encoder.result() == encoded.size());
        Value independently_decoded;
        REQUIRE(make_decoder(encoded)(independently_decoded));
        CHECK(independently_decoded == expected);
    }
}

template <typename T> void check_leaf_splits(const T &expected) {
    check_leaf_splits(expected, expected);
    auto indefinite = as_indefinite{expected};
    check_leaf_splits(expected, indefinite);
}

struct leaf_calls {
    static inline unsigned encoded{};
    static inline unsigned decoded{};
};

template <typename Self> struct direct_float_codec : codec::base<Self> {
    using codec::base<Self>::decode;
    using codec::base<Self>::encode;

    void encode(float value) {
        static_cast<Self &>(*this).encode(static_cast<std::uint64_t>(value) + 1000);
        ++leaf_calls::encoded;
    }
    status_code decode(float &value) {
        std::uint64_t encoded{};
        const auto    result = static_cast<Self &>(*this)(encoded);
        if (!result)
            return result.error();
        value = static_cast<float>(encoded - 1000);
        ++leaf_calls::decoded;
        return status_code::success;
    }
};

template <typename Self> struct header_float_codec : codec::base<Self> {
    using codec::base<Self>::decode;
    using codec::base<Self>::encode;

    void encode(float value) {
        static_cast<Self &>(*this).encode(static_cast<std::uint64_t>(value) + 1000);
        ++leaf_calls::encoded;
    }
    status_code decode(float &value, major_type major, std::byte info) {
        std::uint64_t encoded{};
        const auto    status = static_cast<Self &>(*this).decode(encoded, major, info);
        if (status != status_code::success)
            return status;
        value = static_cast<float>(encoded - 1000);
        ++leaf_calls::decoded;
        return status_code::success;
    }
};

template <typename Self> struct explicit_float_codec : codec::base<Self> {
    using codec::base<Self>::decode;
    using codec::base<Self>::encode;

    // Probing these deduced ordinary overloads would instantiate invalid code.
    template <typename T>
        requires std::same_as<T, float>
    auto encode(T value) {
        return static_cast<Self &>(*this).ordinary_overload_must_not_be_instantiated(value);
    }
    auto decode(float &value) { return static_cast<Self &>(*this).ordinary_overload_must_not_be_instantiated(value); }

    template <typename T>
        requires std::same_as<std::remove_cv_t<T>, float>
    resumable<status_code> resumable_encode(T &value) {
        ++leaf_calls::encoded;
        co_return co_await static_cast<Self &>(*this)(static_cast<std::uint64_t>(value) + 1000);
    }
    resumable<status_code> resumable_decode(float &value) {
        ++leaf_calls::decoded;
        std::uint64_t encoded{};
        const auto    status = co_await static_cast<Self &>(*this)(encoded);
        if (status == status_code::success)
            value = static_cast<float>(encoded - 1000);
        co_return status;
    }
};

template <template <typename> typename Codec, typename Value, typename WireValue>
void check_float_override(const Value &expected, const WireValue &shifted, std::size_t count) {
    constexpr bool explicit_codec = std::same_as<Codec<int>, explicit_float_codec<int>>;
    constexpr bool header_codec   = std::same_as<Codec<int>, header_float_codec<int>>;
    for (const bool indefinite : {false, true}) {
        std::vector<std::byte> wire;
        std::vector<std::byte> ordinary_wire;
        REQUIRE((indefinite ? make_encoder(ordinary_wire)(as_indefinite{expected}) : make_encoder(ordinary_wire)(expected)));
        if constexpr (header_codec)
            wire = ordinary_wire;
        else
            REQUIRE((indefinite ? make_encoder(wire)(as_indefinite{shifted}) : make_encoder(wire)(shifted)));
        for (const bool bytewise : {false, true}) {
            CAPTURE(indefinite);
            CAPTURE(bytewise);
            leaf_calls::decoded = 0;
            input_chunks storage;
            auto         source = make_input_source(storage);
            Value        actual;
            auto         decoder = make_resumable_decoder<Codec>(source)(actual);
            if (bytewise) {
                for (const auto &byte : wire)
                    decoder(std::span{&byte, 1});
            } else {
                decoder(wire);
            }
            REQUIRE(decoder.done());
            REQUIRE(decoder.result());
            CHECK(actual == expected);
            CHECK(leaf_calls::decoded == (header_codec ? 0 : count));

            leaf_calls::encoded = 0;
            std::vector<std::byte> encoded(explicit_codec ? wire.size() : ordinary_wire.size());
            output_chunks          output;
            auto                   sink        = make_output_source(output);
            auto                   run_encoder = [&](auto &value) {
                auto encoder = make_resumable_encoder<Codec>(sink)(value);
                if (bytewise) {
                    for (auto &byte : encoded)
                        encoder(std::span{&byte, 1});
                } else {
                    encoder(encoded);
                }
                REQUIRE(encoder.done());
                REQUIRE(encoder.result());
            };
            if (indefinite) {
                auto wrapped = as_indefinite{expected};
                run_encoder(wrapped);
            } else {
                run_encoder(expected);
            }
            if constexpr (explicit_codec) {
                WireValue independently_decoded;
                REQUIRE(make_decoder(encoded)(independently_decoded));
                CHECK(independently_decoded == shifted);
                CHECK(leaf_calls::encoded == count);
            } else {
                // Core float overloads hide identical ordinary mixin signatures.
                // Preserve that dispatch instead of inventing an override here.
                Value independently_decoded;
                REQUIRE(make_decoder(encoded)(independently_decoded));
                CHECK(independently_decoded == expected);
                CHECK(leaf_calls::encoded == 0);
            }
        }
    }
}

template <typename Self> struct bool_codec : codec::base<Self> {
    using codec::base<Self>::decode;
    using codec::base<Self>::encode;

    status_code decode(bool &value) {
        std::uint64_t encoded{};
        const auto    result = static_cast<Self &>(*this)(encoded);
        if (!result)
            return result.error();
        if (encoded != 1000 && encoded != 1001)
            return status_code::no_match_for_tag_simple_on_buffer;
        value = encoded == 1001;
        ++leaf_calls::decoded;
        return status_code::success;
    }

    template <typename T>
        requires std::same_as<std::remove_cv_t<T>, bool>
    resumable<status_code> resumable_encode(T &value) {
        ++leaf_calls::encoded;
        co_return co_await static_cast<Self &>(*this)(value ? std::uint64_t{1001} : std::uint64_t{1000});
    }
};

} // namespace

TEST_SUITE("roundtrip/resumable") {

    TEST_CASE("bounded float and bool leaves retain values at every array and map split") {
        check_leaf_splits(std::vector<float>{0.0F, -0.0F, 1.25F, -8192.5F, 0.125F});
        check_leaf_splits(std::vector<double>{0.0, -0.0, 1.25, -8192.5, 0.125});
        check_leaf_splits(std::deque<bool>{true, false, false, true, true});
        check_leaf_splits(std::map<float, bool>{{-1.25F, true}, {0.0F, false}, {32.5F, true}});
        check_leaf_splits(std::map<bool, double>{{false, -8192.5}, {true, 0.125}});
    }

    TEST_CASE_TEMPLATE("bounded float leaves preserve special values bit for bit across bytewise admission", T, float, double) {
        using bits_type = std::conditional_t<std::same_as<T, float>, std::uint32_t, std::uint64_t>;
        const std::vector<T>   expected{T{0},
                                        -T{0},
                                        std::numeric_limits<T>::lowest(),
                                        std::numeric_limits<T>::max(),
                                        std::numeric_limits<T>::min(),
                                        std::numeric_limits<T>::denorm_min(),
                                        std::numeric_limits<T>::infinity(),
                                        -std::numeric_limits<T>::infinity(),
                                        std::numeric_limits<T>::quiet_NaN()};
        std::vector<std::byte> wire;
        REQUIRE(make_encoder(wire)(as_indefinite{expected}));
        input_chunks   storage;
        auto           source = make_input_source(storage);
        std::vector<T> actual;
        auto           decoder = make_resumable_decoder(source)(actual);
        for (const auto &byte : wire)
            decoder(std::span{&byte, 1});
        REQUIRE(decoder.result());
        REQUIRE(actual.size() == expected.size());
        for (std::size_t i = 0; i != expected.size(); ++i)
            CHECK(std::bit_cast<bits_type>(actual[i]) == std::bit_cast<bits_type>(expected[i]));

        std::vector<std::byte> encoded(wire.size());
        output_chunks          output;
        auto                   sink    = make_output_source(output);
        auto                   wrapped = as_indefinite{expected};
        auto                   encoder = make_resumable_encoder(sink)(wrapped);
        for (auto &byte : encoded)
            encoder(std::span{&byte, 1});
        REQUIRE(encoder.result());
        std::vector<T> independently_decoded;
        REQUIRE(make_decoder(encoded)(independently_decoded));
        REQUIRE(independently_decoded.size() == expected.size());
        for (std::size_t i = 0; i != expected.size(); ++i)
            CHECK(std::bit_cast<bits_type>(independently_decoded[i]) == std::bit_cast<bits_type>(expected[i]));
    }

    TEST_CASE("bounded bool leaves retain ordinary direct decoding and resumable encoding hooks") {
        const std::deque<bool>           expected{false, true, false};
        const std::vector<std::uint64_t> shifted{1000, 1001, 1000};
        std::vector<std::byte>           wire;
        REQUIRE(make_encoder(wire)(as_indefinite{shifted}));
        for (const bool bytewise : {false, true}) {
            CAPTURE(bytewise);
            leaf_calls::decoded = 0;
            input_chunks     storage;
            auto             source = make_input_source(storage);
            std::deque<bool> actual;
            auto             decoder = make_resumable_decoder<bool_codec>(source)(actual);
            if (bytewise) {
                for (const auto &byte : wire)
                    decoder(std::span{&byte, 1});
            } else {
                decoder(wire);
            }
            REQUIRE(decoder.result());
            CHECK(actual == expected);
            CHECK(leaf_calls::decoded == expected.size());

            leaf_calls::encoded = 0;
            std::vector<std::byte> encoded(wire.size());
            output_chunks          output;
            auto                   sink    = make_output_source(output);
            auto                   wrapped = as_indefinite{expected};
            auto                   encoder = make_resumable_encoder<bool_codec>(sink)(wrapped);
            if (bytewise) {
                for (auto &byte : encoded)
                    encoder(std::span{&byte, 1});
            } else {
                encoder(encoded);
            }
            REQUIRE(encoder.result());
            CHECK(leaf_calls::encoded == expected.size());
            std::vector<std::uint64_t> independently_decoded;
            REQUIRE(make_decoder(encoded)(independently_decoded));
            CHECK(independently_decoded == shifted);
        }
    }

    TEST_CASE("bounded leaf drains preserve direct and explicit float customization dispatch") {
        const std::vector<float>                     array{0, 24, 256};
        const std::vector<std::uint64_t>             shifted_array{1000, 1024, 1256};
        const std::map<float, float>                 map{{0, 24}, {256, 65536}};
        const std::map<std::uint64_t, std::uint64_t> shifted_map{{1000, 1024}, {1256, 66536}};
        check_float_override<direct_float_codec>(array, shifted_array, array.size());
        check_float_override<header_float_codec>(array, shifted_array, array.size());
        check_float_override<explicit_float_codec>(array, shifted_array, array.size());
        check_float_override<direct_float_codec>(map, shifted_map, 2 * map.size());
        check_float_override<header_float_codec>(map, shifted_map, 2 * map.size());
        check_float_override<explicit_float_codec>(map, shifted_map, 2 * map.size());
    }

    TEST_CASE("packed bool proxies and value yielding float ranges retain item fallback") {
        // Both ranges yield values or proxies rather than stable element references.
        std::vector<bool>      packed{true, false, true, false};
        const std::deque<bool> expected{true, false, true, false};
        check_leaf_splits(expected, packed);
        auto values  = std::views::iota(0, 5) | std::views::transform([](int value) { return static_cast<float>(value) / 4.0F; });
        auto wrapped = as_array_range(values);
        check_leaf_splits(std::vector<float>{0.0F, 0.25F, 0.5F, 0.75F, 1.0F}, wrapped);
    }

    TEST_CASE("bounded leaves traverse noncontiguous input and output chunks") {
        const std::vector<double> expected{0.125, 32.5, -8192.5};
        std::vector<std::byte>    wire;
        REQUIRE(make_encoder(wire)(as_indefinite{expected}));
        std::list<std::byte> input(wire.begin(), wire.end());
        using input_chunk = std::ranges::subrange<std::list<std::byte>::const_iterator>;
        std::list<input_chunk> storage;
        auto                   source = make_input_source(storage);
        std::vector<double>    actual;
        auto                   decoder = make_resumable_decoder(source)(actual);
        decoder(input_chunk{input.cbegin(), input.cend()}, end_of_input);
        REQUIRE(decoder.result());
        CHECK(actual == expected);

        std::list<std::byte> output(wire.size());
        using output_chunk = std::ranges::subrange<std::list<std::byte>::iterator>;
        std::list<output_chunk> output_storage;
        auto                    sink    = make_output_source(output_storage);
        auto                    wrapped = as_indefinite{expected};
        auto                    encoder = make_resumable_encoder(sink)(wrapped);
        encoder(output_chunk{output.begin(), output.end()});
        REQUIRE(encoder.result());
        std::vector<double> independently_decoded;
        REQUIRE(make_decoder(output)(independently_decoded));
        CHECK(independently_decoded == expected);
    }
}
