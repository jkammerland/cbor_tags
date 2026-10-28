#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <cbor_tags/codec.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using namespace cbor::tags;

namespace {

using input_chunks  = std::list<std::span<const std::byte>>;
using output_chunks = std::list<std::span<std::byte>>;

struct resumable_message {
    std::string                name;
    std::vector<std::uint32_t> values;

    bool operator==(const resumable_message &) const = default;
};

struct tagged_message {
    static constexpr std::uint64_t cbor_tag = 45001;
    std::string                    name;
    std::deque<int>                values;

    bool operator==(const tagged_message &) const = default;
};

struct mixin_tagged_value {
    static constexpr std::uint64_t cbor_tag = 45002;
    std::uint32_t                  value{};

    bool operator==(const mixin_tagged_value &) const = default;
};

template <typename Self> struct ordinary_tagged_codec : codec::base<Self> {
    using codec::base<Self>::decode;
    using codec::base<Self>::encode;

    void encode(const mixin_tagged_value &value) {
        auto &encoder = static_cast<Self &>(*this);
        encoder.encode(static_tag<mixin_tagged_value::cbor_tag>{});
        encoder.encode(value.value);
    }

    status_code decode(mixin_tagged_value &value) {
        auto &decoder            = static_cast<Self &>(*this);
        const auto [major, info] = decoder.read_initial_byte();
        return decode(value, major, info);
    }

    status_code decode(mixin_tagged_value &value, major_type major, std::byte info) {
        auto &decoder = static_cast<Self &>(*this);
        if (major != major_type::Tag) {
            return status_code::no_match_for_tag_on_buffer;
        }
        if (decoder.decode_unsigned(info) != mixin_tagged_value::cbor_tag) {
            return status_code::no_match_for_tag;
        }

        std::uint32_t next{};
        const auto    result = decoder(next);
        if (!result) {
            return result.error();
        }
        value.value = next;
        return status_code::success;
    }
};

struct tag_context_value {
    static constexpr std::uint64_t cbor_tag = 100;
    int                            value{};

    bool operator==(const tag_context_value &) const = default;
};

struct header_tag_context_value {
    static constexpr std::uint64_t cbor_tag = 102;
    int                            value{};

    bool operator==(const header_tag_context_value &) const = default;
};

using header_context_value = std::array<int, 2>;

// These overrides expose only the forms that receive an already-decoded header.
template <typename Self> struct contextual_variant_codec : codec::base<Self> {
    using codec::base<Self>::decode;
    using codec::base<Self>::encode;

    status_code decode(tag_context_value &value, std::uint64_t tag) {
        if (tag != tag_context_value::cbor_tag) {
            return status_code::no_match_for_tag;
        }

        int        decoded{};
        const auto result = static_cast<Self &>(*this)(decoded);
        if (!result) {
            return result.error();
        }
        value.value = decoded + 1000;
        return status_code::success;
    }

    status_code decode(header_context_value &value, major_type major, std::byte info) {
        auto &decoder = static_cast<Self &>(*this);
        if (major != major_type::Array) {
            return status_code::no_match_for_array_on_buffer;
        }
        if (decoder.decode_unsigned(info) != 2) {
            return status_code::unexpected_group_size;
        }

        int        first{};
        int        second{};
        const auto result = decoder(first, second);
        if (!result) {
            return result.error();
        }
        value = {first + 1000, second};
        return status_code::success;
    }

    status_code decode(header_tag_context_value &value, major_type major, std::byte info) {
        auto &decoder = static_cast<Self &>(*this);
        if (major != major_type::Tag) {
            return status_code::no_match_for_tag_on_buffer;
        }
        if (decoder.decode_unsigned(info) != header_tag_context_value::cbor_tag) {
            return status_code::no_match_for_tag;
        }

        int        decoded{};
        const auto result = decoder(decoded);
        if (!result) {
            return result.error();
        }
        value.value = decoded + 1000;
        return status_code::success;
    }
};

struct integer_codec_calls {
    static inline std::size_t encoded{};
    static inline std::size_t decoded{};
};

// Shift the application value to distinguish dispatch from the built-in codec.
template <typename Self> struct ordinary_uint32_codec : codec::base<Self> {
    using codec::base<Self>::decode;
    using codec::base<Self>::encode;

    void encode(std::uint32_t value) {
        static_cast<Self &>(*this).encode(static_cast<std::uint64_t>(value) + 1000);
        ++integer_codec_calls::encoded;
    }

    status_code decode(std::uint32_t &value) {
        const auto [major, info] = static_cast<Self &>(*this).read_initial_byte();
        return decode(value, major, info);
    }

    status_code decode(std::uint32_t &value, major_type major, std::byte info) {
        std::uint64_t wire_value{};
        const auto    status = static_cast<Self &>(*this).decode(wire_value, major, info);
        if (status != status_code::success)
            return status;
        if (wire_value < 1000 || wire_value - 1000 > std::numeric_limits<std::uint32_t>::max())
            return status_code::no_match_for_uint_on_buffer;

        value = static_cast<std::uint32_t>(wire_value - 1000);
        ++integer_codec_calls::decoded;
        return status_code::success;
    }
};

// Plain mixins may customize only the overload receiving an already-read header.
template <typename Self> struct plain_uint32_codec {
    status_code decode(std::uint32_t &value, major_type major, std::byte info) {
        std::uint64_t wire_value{};
        const auto    status = static_cast<Self &>(*this).decode(wire_value, major, info);
        if (status != status_code::success)
            return status;
        if (wire_value < 3000 || wire_value - 3000 > std::numeric_limits<std::uint32_t>::max())
            return status_code::no_match_for_uint_on_buffer;

        value = static_cast<std::uint32_t>(wire_value - 3000);
        ++integer_codec_calls::decoded;
        return status_code::success;
    }
};

template <typename Self> struct explicit_uint32_codec : codec::base<Self> {
    using codec::base<Self>::decode;
    using codec::base<Self>::encode;

    template <typename Value>
        requires std::same_as<std::remove_cvref_t<Value>, std::uint32_t>
    resumable<status_code> resumable_encode(Value &value) {
        ++integer_codec_calls::encoded;
        const auto wire_value = static_cast<std::uint64_t>(value) + 2000;
        co_return co_await static_cast<Self &>(*this)(wire_value);
    }

    resumable<status_code> resumable_decode(std::uint32_t &value) {
        ++integer_codec_calls::decoded;
        std::uint64_t wire_value{};
        const auto    status = co_await static_cast<Self &>(*this)(wire_value);
        if (status != status_code::success)
            co_return status;
        if (wire_value < 2000 || wire_value - 2000 > std::numeric_limits<std::uint32_t>::max())
            co_return status_code::no_match_for_uint_on_buffer;

        value = static_cast<std::uint32_t>(wire_value - 2000);
        co_return status_code::success;
    }
};

template <typename Self> struct explicit_only_uint32_codec : codec::base<Self> {
    using codec::base<Self>::decode;
    using codec::base<Self>::encode;

    // Deduced return types expose accidental instantiation during eligibility checks.
    template <typename T>
        requires std::same_as<std::remove_cvref_t<T>, std::uint32_t>
    auto encode(T &) {
        static_assert(sizeof(Self) == 0, "ordinary integer encode must be bypassed");
    }

    template <typename T>
        requires std::same_as<std::remove_cvref_t<T>, std::uint32_t>
    auto decode(T &) {
        static_assert(sizeof(Self) == 0, "ordinary integer decode must be bypassed");
        return status_code::success;
    }

    template <typename T>
        requires std::same_as<std::remove_cvref_t<T>, std::uint32_t>
    resumable<status_code> resumable_encode(T &value) {
        ++integer_codec_calls::encoded;
        const auto wire_value = static_cast<std::uint64_t>(value) + 4000;
        co_return co_await static_cast<Self &>(*this)(wire_value);
    }

    resumable<status_code> resumable_decode(std::uint32_t &value) {
        ++integer_codec_calls::decoded;
        std::uint64_t wire_value{};
        const auto    status = co_await static_cast<Self &>(*this)(wire_value);
        if (status != status_code::success)
            co_return status;
        if (wire_value < 4000 || wire_value - 4000 > std::numeric_limits<std::uint32_t>::max())
            co_return status_code::no_match_for_uint_on_buffer;

        value = static_cast<std::uint32_t>(wire_value - 4000);
        co_return status_code::success;
    }
};

template <typename T> std::vector<std::byte> ordinary_bytes(const T &value) {
    std::vector<std::byte> result;
    REQUIRE(make_encoder(result)(value));
    return result;
}

template <typename T> void check_every_decode_split(const T &expected) {
    const auto                       bytes = ordinary_bytes(expected);
    const std::span<const std::byte> input{bytes};

    for (std::size_t cut = 0; cut <= input.size(); ++cut) {
        CAPTURE(cut);
        input_chunks storage;
        auto         source = make_input_source(storage);
        T            actual{};
        auto         operation = make_resumable_decoder(source)(actual);

        operation(input.first(cut));
        if (!operation.done()) {
            operation(input.subspan(cut), end_of_input);
        }

        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(*operation.result() == bytes.size());
        CHECK(actual == expected);
        CHECK(std::ranges::empty(operation.remaining_input()));
    }
}

template <typename T> void check_every_encode_split(const T &expected) {
    const auto reference = ordinary_bytes(expected);

    for (std::size_t cut = 0; cut <= reference.size(); ++cut) {
        CAPTURE(cut);
        std::vector<std::byte> bytes(reference.size());
        output_chunks          storage;
        auto                   sink      = make_output_source(storage);
        auto                   operation = make_resumable_encoder(sink)(expected);

        operation(std::span{bytes}.first(cut));
        if (!operation.done()) {
            operation(std::span{bytes}.subspan(cut));
        }

        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(*operation.result() == reference.size());

        T actual{};
        REQUIRE(make_decoder(bytes)(actual));
        CHECK(actual == expected);
    }
}

template <typename T> struct is_ordinary_decoder : std::false_type {};

template <typename Buffer, typename Options, template <typename> typename... Codecs>
struct is_ordinary_decoder<decoder<Buffer, Options, Codecs...>> : std::true_type {};

// This customization publishes destination state only after its complete item succeeds.
struct retry_safe_record {
    std::uint32_t              number{};
    std::string                text;
    int                        attempts{};
    std::vector<std::uint32_t> completed;

    template <typename Encoder> auto encode(Encoder &enc) const { return enc(wrap_as_array{number, text}); }

    template <typename Decoder> expected<void, status_code> decode(Decoder &dec) {
        static_assert(is_ordinary_decoder<Decoder>::value);
        ++attempts;

        as_array_any header;
        if (const auto result = dec(header); !result) {
            return unexpected<status_code>{result.error()};
        }
        if (header.indefinite || header.size != 2) {
            return unexpected<status_code>{status_code::unexpected_group_size};
        }

        std::uint32_t next_number{};
        if (const auto result = dec(next_number); !result) {
            return unexpected<status_code>{result.error()};
        }

        std::string next_text;
        if (const auto result = dec(next_text); !result) {
            return unexpected<status_code>{result.error()};
        }

        number = next_number;
        text   = std::move(next_text);
        completed.push_back(number);
        return {};
    }
};

struct retry_safe_encoder_record {
    std::uint32_t number{};
    std::string   text;
    mutable int   attempts{};

    template <typename Encoder> expected<void, status_code> encode(Encoder &enc) const {
        ++attempts;
        if (const auto result = enc(as_array{2}); !result) {
            return unexpected<status_code>{result.error()};
        }
        if (const auto result = enc(number); !result) {
            return unexpected<status_code>{result.error()};
        }
        if (const auto result = enc(text); !result) {
            return unexpected<status_code>{result.error()};
        }
        return {};
    }
};

struct explicitly_resumable_record {
    std::uint32_t number{};
    std::string   text;
    int           ordinary_calls{};
    int           resumable_calls{};
    mutable int   ordinary_encode_calls{};
    mutable int   resumable_encode_calls{};

    template <typename Encoder> auto encode(Encoder &enc) const {
        ++ordinary_encode_calls;
        return enc(wrap_as_array{number, text});
    }
    template <typename Decoder> auto decode(Decoder &dec) {
        ++ordinary_calls;
        return dec(wrap_as_array{number, text});
    }
};

template <typename Context> resumable<status_code> resumable_decode(Context &context, explicitly_resumable_record &value) {
    ++value.resumable_calls;
    co_return co_await context(wrap_as_array{value.number, value.text});
}

template <typename Context> resumable<status_code> resumable_encode(Context &context, const explicitly_resumable_record &value) {
    ++value.resumable_encode_calls;
    co_return co_await context(wrap_as_array{value.number, value.text});
}

struct adl_tagged_resumable_record : explicitly_resumable_record {
    static constexpr std::uint64_t cbor_tag = 45003;

    adl_tagged_resumable_record() = default;
    adl_tagged_resumable_record(std::uint32_t number, std::string text) : explicitly_resumable_record{number, std::move(text)} {}
};

struct member_tagged_resumable_record : explicitly_resumable_record {
    static constexpr std::uint64_t cbor_tag = 45004;

    member_tagged_resumable_record() = default;
    member_tagged_resumable_record(std::uint32_t number, std::string text) : explicitly_resumable_record{number, std::move(text)} {}

    template <typename Context> resumable<status_code> resumable_encode(Context &context) const {
        ++resumable_encode_calls;
        co_return co_await context(wrap_as_array{number, text});
    }

    template <typename Context> resumable<status_code> resumable_decode(Context &context) {
        ++resumable_calls;
        co_return co_await context(wrap_as_array{number, text});
    }
};

struct static_tag_metadata_record : explicitly_resumable_record {
    static_tag<45005> cbor_tag;

    static_tag_metadata_record() = default;
    static_tag_metadata_record(std::uint32_t number, std::string text) : explicitly_resumable_record{number, std::move(text)} {}
};

struct rvalue_numeric_tag {
    constexpr operator std::uint64_t() && { return 45006; }
};

struct rvalue_numeric_metadata_record : explicitly_resumable_record {
    rvalue_numeric_tag cbor_tag;

    rvalue_numeric_metadata_record() = default;
    rvalue_numeric_metadata_record(std::uint32_t number, std::string text) : explicitly_resumable_record{number, std::move(text)} {}
};

struct metadata_tag_calls {
    static inline int encoded{};
    static inline int decoded{};
};

template <typename Self> struct rvalue_metadata_tag_codec : codec::base<Self> {
    using codec::base<Self>::decode;
    using codec::base<Self>::encode;

    void encode(static_tag<45005> &&) {
        static_cast<Self &>(*this).encode_tag_argument(45005);
        ++metadata_tag_calls::encoded;
    }

    status_code decode(static_tag<45005> &&tag) {
        auto &decoder            = static_cast<Self &>(*this);
        const auto [major, info] = decoder.read_initial_byte();
        const auto status        = decoder.decode(tag, major, info);
        if (status == status_code::success) {
            ++metadata_tag_calls::decoded;
        }
        return status;
    }
};

struct explicitly_transcoded_record {
    std::uint32_t number{};
    std::string   text;
    mutable int   ordinary_calls{};
    mutable int   resumable_calls{};

    template <typename Context> auto transcode(Context &context) {
        ++ordinary_calls;
        return context(wrap_as_array{number, text});
    }

    template <typename Context> auto transcode(Context &context) const {
        ++ordinary_calls;
        return context(wrap_as_array{number, text});
    }
};

template <typename Context, typename Value>
    requires std::same_as<std::remove_cvref_t<Value>, explicitly_transcoded_record>
resumable<status_code> resumable_transcode(Context &context, Value &value) {
    ++value.resumable_calls;
    co_return co_await context(wrap_as_array{value.number, value.text});
}

} // namespace

TEST_SUITE("roundtrip/resumable") {

    TEST_CASE_TEMPLATE("integer arrays preserve signed and unsigned boundaries across every split", T, std::uint8_t, std::uint16_t,
                       std::uint32_t, std::uint64_t, std::int8_t, std::int16_t, std::int32_t, std::int64_t) {
        std::vector<T> expected{0, 1, 23, 24, std::numeric_limits<T>::max()};
        for (const std::uint64_t boundary : {255ULL, 256ULL, 65535ULL, 65536ULL, 4294967295ULL, 4294967296ULL}) {
            if (boundary <= static_cast<std::uint64_t>(std::numeric_limits<T>::max()))
                expected.push_back(static_cast<T>(boundary));
        }
        if constexpr (std::is_signed_v<T>) {
            expected.insert(expected.end(), {-1, -24, -25, std::numeric_limits<T>::min()});
            for (const std::int64_t boundary : {-256LL, -257LL, -65536LL, -65537LL, -4294967296LL, -4294967297LL}) {
                if (boundary >= static_cast<std::int64_t>(std::numeric_limits<T>::min()))
                    expected.push_back(static_cast<T>(boundary));
            }
        }

        check_every_decode_split(expected);
        check_every_encode_split(expected);
        check_every_decode_split(std::deque<T>{expected.begin(), expected.end()});
    }

    TEST_CASE("integer arrays retain ordinary codec overrides through bytewise admission") {
        const std::vector<std::uint32_t> expected{0, 24, 65536};
        const std::vector<std::uint64_t> shifted{1000, 1024, 66536};
        const auto                       reference = ordinary_bytes(shifted);
        std::vector<std::byte>           bytes(reference.size());
        output_chunks                    output_storage;
        auto                             sink = make_output_source(output_storage);
        integer_codec_calls::encoded          = 0;
        auto encoder                          = make_resumable_encoder<ordinary_uint32_codec>(sink)(expected);
        for (auto &byte : bytes)
            encoder(std::span{&byte, 1});

        REQUIRE(encoder.done());
        REQUIRE(encoder.result());
        CHECK(*encoder.result() == bytes.size());
        CHECK(integer_codec_calls::encoded == expected.size());
        std::vector<std::uint64_t> encoded_values;
        REQUIRE(make_decoder(bytes)(encoded_values));
        CHECK(encoded_values == shifted);

        input_chunks               input_storage;
        auto                       source = make_input_source(input_storage);
        std::vector<std::uint32_t> actual;
        integer_codec_calls::decoded = 0;
        auto decoder                 = make_resumable_decoder<ordinary_uint32_codec>(source)(actual);
        for (const auto &byte : reference)
            decoder(std::span{&byte, 1});

        REQUIRE(decoder.done());
        REQUIRE(decoder.result());
        CHECK(*decoder.result() == reference.size());
        CHECK(integer_codec_calls::decoded == expected.size());
        CHECK(actual == expected);
    }

    TEST_CASE("integer arrays retain ordinary codec overrides with full contiguous buffers") {
        const std::vector<std::uint32_t> expected{0, 24, 65536};
        const std::vector<std::uint64_t> shifted{1000, 1024, 66536};
        const auto                       reference = ordinary_bytes(shifted);
        REQUIRE(reference.size() > 9U);
        std::vector<std::byte> bytes(reference.size());
        output_chunks          output_storage;
        auto                   sink  = make_output_source(output_storage);
        integer_codec_calls::encoded = 0;
        auto encoder                 = make_resumable_encoder<ordinary_uint32_codec>(sink)(expected);
        encoder(bytes);

        REQUIRE(encoder.done());
        REQUIRE(encoder.result());
        CHECK(*encoder.result() == bytes.size());
        CHECK(integer_codec_calls::encoded == expected.size());
        std::vector<std::uint64_t> encoded_values;
        REQUIRE(make_decoder(bytes)(encoded_values));
        CHECK(encoded_values == shifted);

        input_chunks               input_storage;
        auto                       source = make_input_source(input_storage);
        std::vector<std::uint32_t> actual;
        integer_codec_calls::decoded = 0;
        auto decoder                 = make_resumable_decoder<ordinary_uint32_codec>(source)(actual);
        decoder(reference);
        REQUIRE(decoder.done());
        REQUIRE(decoder.result());
        CHECK(*decoder.result() == reference.size());
        CHECK(integer_codec_calls::decoded == expected.size());
        CHECK(actual == expected);
    }

    TEST_CASE("integer arrays retain plain header codec overrides with full contiguous input") {
        const std::vector<std::uint32_t> expected{0, 24, 65536, std::numeric_limits<std::uint32_t>::max()};
        const std::vector<std::uint64_t> shifted{3000, 3024, 68536, 4294970295ULL};
        const auto                       reference = ordinary_bytes(shifted);
        REQUIRE(reference.size() > 9U);
        input_chunks               storage;
        auto                       source = make_input_source(storage);
        std::vector<std::uint32_t> actual;
        integer_codec_calls::decoded = 0;
        auto operation               = make_resumable_decoder<plain_uint32_codec>(source)(actual);
        operation(reference);

        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(*operation.result() == reference.size());
        CHECK(integer_codec_calls::decoded == expected.size());
        CHECK(actual == expected);
    }

    TEST_CASE("integer arrays invoke explicit hooks once per element across bytewise admission") {
        const std::vector<std::uint32_t> expected{0, 24, 65536};
        const std::vector<std::uint64_t> shifted{2000, 2024, 67536};
        const auto                       reference = ordinary_bytes(shifted);
        std::vector<std::byte>           bytes(reference.size());
        output_chunks                    output_storage;
        auto                             sink = make_output_source(output_storage);
        integer_codec_calls::encoded          = 0;
        auto encoder                          = make_resumable_encoder<explicit_uint32_codec>(sink)(expected);
        for (auto &byte : bytes)
            encoder(std::span{&byte, 1});

        REQUIRE(encoder.done());
        REQUIRE(encoder.result());
        CHECK(integer_codec_calls::encoded == expected.size());
        std::vector<std::uint64_t> encoded_values;
        REQUIRE(make_decoder(bytes)(encoded_values));
        CHECK(encoded_values == shifted);

        input_chunks               input_storage;
        auto                       source = make_input_source(input_storage);
        std::vector<std::uint32_t> actual;
        integer_codec_calls::decoded = 0;
        auto decoder                 = make_resumable_decoder<explicit_uint32_codec>(source)(actual);
        for (const auto &byte : reference)
            decoder(std::span{&byte, 1});

        REQUIRE(decoder.done());
        REQUIRE(decoder.result());
        CHECK(integer_codec_calls::decoded == expected.size());
        CHECK(actual == expected);
    }

    TEST_CASE("explicit integer hooks bypass ordinary auto return codec instantiation") {
        const std::vector<std::uint32_t> expected{0, 24, 65536};
        const std::vector<std::uint64_t> shifted{4000, 4024, 69536};
        const auto                       reference = ordinary_bytes(shifted);

        for (const bool bytewise : {false, true}) {
            CAPTURE(bytewise);
            std::vector<std::byte> bytes(reference.size());
            output_chunks          output_storage;
            auto                   sink  = make_output_source(output_storage);
            integer_codec_calls::encoded = 0;
            auto encoder                 = make_resumable_encoder<explicit_only_uint32_codec>(sink)(expected);
            if (bytewise) {
                for (auto &byte : bytes)
                    encoder(std::span{&byte, 1});
            } else {
                encoder(bytes);
            }

            REQUIRE(encoder.done());
            REQUIRE(encoder.result());
            CHECK(*encoder.result() == bytes.size());
            CHECK(integer_codec_calls::encoded == expected.size());
            std::vector<std::uint64_t> encoded_values;
            REQUIRE(make_decoder(bytes)(encoded_values));
            CHECK(encoded_values == shifted);

            input_chunks               input_storage;
            auto                       source = make_input_source(input_storage);
            std::vector<std::uint32_t> actual;
            integer_codec_calls::decoded = 0;
            auto decoder                 = make_resumable_decoder<explicit_only_uint32_codec>(source)(actual);
            if (bytewise) {
                for (const auto &byte : reference)
                    decoder(std::span{&byte, 1});
            } else {
                decoder(reference);
            }

            REQUIRE(decoder.done());
            REQUIRE(decoder.result());
            CHECK(*decoder.result() == reference.size());
            CHECK(integer_codec_calls::decoded == expected.size());
            CHECK(actual == expected);
        }
    }

    TEST_CASE("integer arrays encode and decode through noncontiguous chunk storage") {
        const std::array<std::int64_t, 5> expected{1, -24, 256, -65537, std::numeric_limits<std::int64_t>::min()};
        const auto                        reference = ordinary_bytes(expected);
        std::list<std::byte>              bytes(reference.size());
        const auto                        middle = std::next(bytes.begin(), reference.size() / 2);
        using chunk                              = std::ranges::subrange<std::list<std::byte>::iterator>;
        std::array chunks{chunk{bytes.begin(), middle}, chunk{middle, bytes.end()}};
        static_assert(!std::ranges::contiguous_range<chunk>);
        std::list<chunk> output_storage;
        auto             sink    = make_output_source(output_storage);
        auto             encoder = make_resumable_encoder(sink)(expected);
        encoder(chunks);

        REQUIRE(encoder.done());
        REQUIRE(encoder.result());
        CHECK(*encoder.result() == reference.size());
        std::array<std::int64_t, 5> ordinary_actual{};
        REQUIRE(make_decoder(bytes)(ordinary_actual));
        CHECK(ordinary_actual == expected);

        std::list<chunk>            input_storage;
        auto                        source = make_input_source(input_storage);
        std::array<std::int64_t, 5> actual{};
        auto                        decoder = make_resumable_decoder(source)(actual);
        decoder(chunks);
        REQUIRE(decoder.done());
        REQUIRE(decoder.result());
        CHECK(*decoder.result() == reference.size());
        CHECK(actual == expected);
    }

    TEST_CASE("reflected message survives every three-chunk partition") {
        const resumable_message          expected{"abc", {1, 24}};
        const auto                       bytes = ordinary_bytes(expected);
        const std::span<const std::byte> input{bytes};

        for (std::size_t first = 0; first <= input.size(); ++first) {
            for (std::size_t second = first; second <= input.size(); ++second) {
                CAPTURE(first);
                CAPTURE(second);
                input_chunks      storage;
                auto              source = make_input_source(storage);
                resumable_message actual;
                auto              operation = make_resumable_decoder(source)(actual);

                operation(input.first(first));
                if (!operation.done()) {
                    operation(input.subspan(first, second - first));
                }
                if (!operation.done()) {
                    operation(input.subspan(second), end_of_input);
                }

                REQUIRE(operation.done());
                REQUIRE(operation.result());
                CHECK(*operation.result() == input.size());
                CHECK(actual == expected);
            }
        }
    }

    TEST_CASE("owning containers maps variants and tagged records survive every split") {
        check_every_decode_split(std::deque<int>{1, -24, 65536});
        check_every_decode_split(std::map<std::string, std::vector<int>>{{"first", {1, 24}}, {"second", {-1, 65536}}});
        check_every_decode_split(std::variant<int, std::string>{std::string{"variant text"}});
        check_every_decode_split(std::optional<std::string>{"optional text"});
        check_every_decode_split(std::array<int, 3>{1, 24, 65536});
        check_every_decode_split(tagged_message{"tagged", {1, -2, 65536}});
        check_every_decode_split(std::vector<std::byte>{std::byte{1}, std::byte{2}, std::byte{3}});
        check_every_decode_split(std::array<std::byte, 3>{std::byte{1}, std::byte{2}, std::byte{3}});
        check_every_decode_split(std::tuple<int, std::string>{65536, "tuple"});
        check_every_decode_split(std::tuple<static_tag<101>, int, std::string>{{}, 65536, "tagged tuple"});
    }

    TEST_CASE("encoding resumes inside strings arrays maps and fixed containers") {
        check_every_encode_split(resumable_message{"a split string", {1, 24, 65536}});
        check_every_encode_split(std::deque<int>{1, -24, 65536});
        check_every_encode_split(std::map<std::string, std::vector<int>>{{"first", {1, 24}}, {"second", {-1, 65536}}});
        check_every_encode_split(std::array<int, 3>{1, 24, 65536});
        check_every_encode_split(tagged_message{"tagged", {1, -2, 65536}});
        check_every_encode_split(std::vector<std::byte>{std::byte{1}, std::byte{2}, std::byte{3}});
    }

    TEST_CASE("ordinary codec mixins retain tagged variant dispatch across every split") {
        using value_type = std::variant<tagged_message, mixin_tagged_value>;
        const value_type       expected{mixin_tagged_value{65536}};
        std::vector<std::byte> bytes;
        REQUIRE(make_encoder<ordinary_tagged_codec>(bytes)(expected));
        const std::span<const std::byte> input{bytes};

        for (std::size_t cut = 0; cut <= input.size(); ++cut) {
            CAPTURE(cut);
            input_chunks storage;
            auto         source = make_input_source(storage);
            value_type   actual;
            auto         operation = make_resumable_decoder<ordinary_tagged_codec>(source)(actual);
            operation(input.first(cut));
            if (!operation.done()) {
                operation(input.subspan(cut), end_of_input);
            }

            REQUIRE(operation.result());
            CHECK(*operation.result() == bytes.size());
            CHECK(actual == expected);
        }
    }

    TEST_CASE("variant dispatch preserves tag-only and header-only ordinary codec overrides") {
        auto check = []<typename T>(const T &input, const T &expected) {
            const auto bytes = ordinary_bytes(input);
            T          direct;
            REQUIRE(make_decoder<contextual_variant_codec>(bytes)(direct));
            if constexpr (std::same_as<T, tag_context_value>) {
                CHECK(direct == input);
            } else {
                CHECK(direct == expected);
            }

            using variant_type = std::variant<T, int>;
            variant_type ordinary;
            REQUIRE(make_decoder<contextual_variant_codec>(bytes)(ordinary));
            REQUIRE(std::holds_alternative<T>(ordinary));
            CHECK(std::get<T>(ordinary) == expected);

            for (std::size_t cut = 0; cut <= bytes.size(); ++cut) {
                CAPTURE(cut);
                input_chunks storage;
                auto         source = make_input_source(storage);
                variant_type actual;
                auto         operation = make_resumable_decoder<contextual_variant_codec>(source)(actual);
                operation(std::span<const std::byte>{bytes}.first(cut));
                if (!operation.done()) {
                    operation(std::span<const std::byte>{bytes}.subspan(cut), end_of_input);
                }

                REQUIRE(operation.result());
                CHECK(*operation.result() == bytes.size());
                REQUIRE(std::holds_alternative<T>(actual));
                CHECK(std::get<T>(actual) == expected);
            }
        };

        check(tag_context_value{65536}, tag_context_value{66536});
        check(header_context_value{24, 65536}, header_context_value{1024, 65536});
    }

    TEST_CASE("optionals and indefinite containers retain header-context codec overrides") {
        const header_tag_context_value original{65536};
        const auto                     scalar_bytes = ordinary_bytes(original);
        header_tag_context_value       direct;
        REQUIRE(make_decoder<contextual_variant_codec>(scalar_bytes)(direct));
        CHECK(direct == original);

        auto check = []<typename T>(const std::vector<std::byte> &bytes, const T &expected) {
            T ordinary;
            REQUIRE(make_decoder<contextual_variant_codec>(bytes)(ordinary));
            CHECK(ordinary == expected);

            for (std::size_t cut = 0; cut <= bytes.size(); ++cut) {
                CAPTURE(cut);
                input_chunks storage;
                auto         source = make_input_source(storage);
                T            actual;
                auto         operation = make_resumable_decoder<contextual_variant_codec>(source)(actual);
                operation(std::span<const std::byte>{bytes}.first(cut));
                if (!operation.done()) {
                    operation(std::span<const std::byte>{bytes}.subspan(cut), end_of_input);
                }

                REQUIRE(operation.result());
                CHECK(*operation.result() == bytes.size());
                CHECK(actual == expected);
            }
        };

        check(scalar_bytes, std::optional<header_tag_context_value>{{66536}});

        const std::vector<header_tag_context_value> values{{24}, {65536}};
        std::vector<std::byte>                      indefinite_bytes;
        REQUIRE(make_encoder(indefinite_bytes)(as_indefinite{values}));
        check(indefinite_bytes, std::vector<header_tag_context_value>{{1024}, {66536}});
    }

    TEST_CASE("variadic items share one byte count across one-byte chunks") {
        const int              expected_number = 65536;
        const std::string      expected_text   = "separate item";
        const std::vector<int> expected_values{1, 24, -400};
        std::vector<std::byte> bytes;
        REQUIRE(make_encoder(bytes)(expected_number, expected_text, expected_values));

        input_chunks     storage;
        auto             source = make_input_source(storage);
        int              number{};
        std::string      text;
        std::vector<int> values;
        auto             operation = make_resumable_decoder(source)(number, text, values);
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            operation(std::span<const std::byte>{bytes}.subspan(index, 1));
            CHECK(operation.done() == (index + 1 == bytes.size()));
        }

        REQUIRE(operation.result());
        CHECK(*operation.result() == bytes.size());
        CHECK(number == expected_number);
        CHECK(text == expected_text);
        CHECK(values == expected_values);
    }

    TEST_CASE("runtime chunk ranges preserve order and can contain empty chunks") {
        const resumable_message          expected{"runtime chunks", {1, 24, 65536}};
        const auto                       bytes = ordinary_bytes(expected);
        const std::span<const std::byte> input{bytes};
        const std::array                 chunks{input.first(0), input.first(2), input.subspan(2, 0), input.subspan(2)};

        input_chunks      storage;
        auto              source = make_input_source(storage);
        resumable_message actual;
        auto              operation = make_resumable_decoder(source)(actual);
        operation(std::span{chunks}, end_of_input);

        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(*operation.result() == bytes.size());
        CHECK(actual == expected);
    }

    TEST_CASE("ordinary customization retries its complete item without duplicate destination effects") {
        const retry_safe_record expected{65536, "retry-safe payload", 0, {}};
        const auto              bytes = ordinary_bytes(expected);
        input_chunks            storage;
        auto                    source = make_input_source(storage);
        retry_safe_record       actual;
        auto                    operation = make_resumable_decoder(source)(actual);

        // Nested normal decoder calls succeed before the final string is complete.
        operation(std::span<const std::byte>{bytes}.first(bytes.size() - 1));
        REQUIRE_FALSE(operation.done());
        CHECK(actual.completed.empty());
        CHECK(actual.text.empty());
        CHECK(std::ranges::distance(operation.remaining_input()) == static_cast<std::ptrdiff_t>(bytes.size() - 1));
        const auto attempts = actual.attempts;

        operation(std::span<const std::byte>{});
        CHECK(actual.attempts == attempts);
        operation(std::span<const std::byte>{bytes}.last(1), end_of_input);

        REQUIRE(operation.result());
        CHECK(*operation.result() == bytes.size());
        CHECK(actual.number == expected.number);
        CHECK(actual.text == expected.text);
        CHECK(actual.completed == std::vector<std::uint32_t>{expected.number});
        CHECK(actual.attempts == attempts + 1);
    }

    TEST_CASE("a supplied batch invokes an ordinary codec only after all chunks are visible") {
        const retry_safe_record                 expected{65536, "one attempt for a whole batch", 0, {}};
        const auto                              bytes = ordinary_bytes(expected);
        std::vector<std::span<const std::byte>> chunks;
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            chunks.emplace_back(bytes.data() + index, 1);
        }

        input_chunks      storage;
        auto              source = make_input_source(storage);
        retry_safe_record actual;
        auto              operation = make_resumable_decoder(source)(actual);
        const auto        attempts  = actual.attempts;
        operation(chunks, end_of_input);

        REQUIRE(operation.result());
        CHECK(actual.attempts == attempts + 1);
        CHECK(actual.text == expected.text);
        CHECK(actual.completed == std::vector<std::uint32_t>{expected.number});
    }

    TEST_CASE("ordinary encoder customization keeps nested writes tentative until its complete item succeeds") {
        const retry_safe_encoder_record expected{65536, "tentative output"};
        const auto                      reference = ordinary_bytes(std::tuple{expected.number, expected.text});
        std::vector<std::byte>          bytes(reference.size());
        output_chunks                   storage;
        auto                            sink      = make_output_source(storage);
        auto                            operation = make_resumable_encoder(sink)(expected);

        operation(std::span{bytes}.first(bytes.size() - 1));
        REQUIRE_FALSE(operation.done());
        CHECK(std::ranges::empty(operation.produced()));
        const auto attempts = expected.attempts;
        operation(std::span<std::byte>{});
        CHECK(expected.attempts == attempts);
        operation(std::span{bytes}.last(1));

        REQUIRE(operation.result());
        CHECK(*operation.result() == bytes.size());
        CHECK(expected.attempts == attempts + 1);
        CHECK(std::ranges::distance(operation.produced()) == static_cast<std::ptrdiff_t>(bytes.size()));
        std::tuple<std::uint32_t, std::string> actual;
        REQUIRE(make_decoder(bytes)(actual));
        CHECK(std::get<0>(actual) == expected.number);
        CHECK(std::get<1>(actual) == expected.text);
    }

    TEST_CASE("explicit resumable hook takes precedence over ordinary forwarding customization") {
        const explicitly_resumable_record expected{65536, "opt-in continuation", 0, 0};
        const auto                        bytes = ordinary_bytes(expected);
        input_chunks                      storage;
        auto                              source = make_input_source(storage);
        explicitly_resumable_record       actual;
        auto                              operation = make_resumable_decoder(source)(actual);

        for (std::size_t index = 0; index < bytes.size(); ++index) {
            operation(std::span<const std::byte>{bytes}.subspan(index, 1));
        }

        REQUIRE(operation.result());
        CHECK(actual.number == expected.number);
        CHECK(actual.text == expected.text);
        CHECK(actual.ordinary_calls == 0);
        CHECK(actual.resumable_calls == 1);
    }

    TEST_CASE("nested explicit encode and decode hooks bypass ordinary forwarding customizations") {
        const std::vector<explicitly_resumable_record> expected{{65536, "nested continuation", 0, 0}};
        const auto                                     reference        = ordinary_bytes(expected);
        const auto                                     ordinary_encodes = expected.front().ordinary_encode_calls;
        std::vector<std::byte>                         bytes(reference.size());
        output_chunks                                  outputs;
        auto                                           sink    = make_output_source(outputs);
        auto                                           encoder = make_resumable_encoder(sink)(expected);

        for (std::size_t index = 0; index < bytes.size(); ++index) {
            encoder(std::span{bytes}.subspan(index, 1));
        }

        REQUIRE(encoder.result());
        CHECK(expected.front().ordinary_encode_calls == ordinary_encodes);
        CHECK(expected.front().resumable_encode_calls == 1);

        input_chunks                             inputs;
        auto                                     source = make_input_source(inputs);
        std::vector<explicitly_resumable_record> actual;
        auto                                     decoder = make_resumable_decoder(source)(actual);
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            decoder(std::span<const std::byte>{bytes}.subspan(index, 1));
        }

        REQUIRE(decoder.result());
        REQUIRE(actual.size() == 1U);
        CHECK(actual.front().number == expected.front().number);
        CHECK(actual.front().text == expected.front().text);
        CHECK(actual.front().ordinary_calls == 0);
        CHECK(actual.front().resumable_calls == 1);
    }

    TEST_CASE("tagged explicit member and adl hooks preserve automatic metadata tags") {
        auto check = []<typename T>(const T &expected) {
            const auto reference        = ordinary_bytes(expected);
            const auto ordinary_encodes = expected.ordinary_encode_calls;

            for (std::size_t cut = 0; cut <= reference.size(); ++cut) {
                CAPTURE(cut);
                input_chunks inputs;
                auto         source = make_input_source(inputs);
                T            actual;
                auto         decoder = make_resumable_decoder(source)(actual);
                decoder(std::span<const std::byte>{reference}.first(cut));
                if (!decoder.done()) {
                    decoder(std::span<const std::byte>{reference}.subspan(cut), end_of_input);
                }

                REQUIRE(decoder.result());
                CHECK(*decoder.result() == reference.size());
                CHECK(actual.number == expected.number);
                CHECK(actual.text == expected.text);
                CHECK(actual.ordinary_calls == 0);
                CHECK(actual.resumable_calls == 1);

                std::vector<std::byte> bytes(reference.size());
                output_chunks          outputs;
                auto                   sink              = make_output_source(outputs);
                const auto             resumable_encodes = expected.resumable_encode_calls;
                auto                   encoder           = make_resumable_encoder(sink)(expected);
                encoder(std::span{bytes}.first(cut));
                if (!encoder.done()) {
                    encoder(std::span{bytes}.subspan(cut));
                }

                REQUIRE(encoder.result());
                CHECK(*encoder.result() == reference.size());
                CHECK(expected.ordinary_encode_calls == ordinary_encodes);
                CHECK(expected.resumable_encode_calls == resumable_encodes + 1);
                T decoded;
                REQUIRE(make_decoder(bytes)(decoded));
                CHECK(decoded.number == expected.number);
                CHECK(decoded.text == expected.text);
            }
        };

        check(adl_tagged_resumable_record{65536, "tagged adl continuation"});
        check(member_tagged_resumable_record{65536, "tagged member continuation"});
        check(rvalue_numeric_metadata_record{65536, "rvalue numeric metadata"});
    }

    TEST_CASE("explicit hooks preserve rvalue codec dispatch for static tag metadata") {
        const static_tag_metadata_record expected{65536, "rvalue metadata"};
        std::vector<std::byte>           reference;
        REQUIRE(make_encoder<rvalue_metadata_tag_codec>(reference)(expected));

        for (std::size_t cut = 0; cut <= reference.size(); ++cut) {
            CAPTURE(cut);
            input_chunks               inputs;
            auto                       source = make_input_source(inputs);
            static_tag_metadata_record actual;
            const auto                 initial_decodes = metadata_tag_calls::decoded;
            auto                       decoder         = make_resumable_decoder<rvalue_metadata_tag_codec>(source)(actual);
            decoder(std::span<const std::byte>{reference}.first(cut));
            if (!decoder.done()) {
                decoder(std::span<const std::byte>{reference}.subspan(cut), end_of_input);
            }

            REQUIRE(decoder.result());
            CHECK(*decoder.result() == reference.size());
            CHECK(actual.number == expected.number);
            CHECK(actual.text == expected.text);
            CHECK(actual.resumable_calls == 1);
            CHECK(metadata_tag_calls::decoded == initial_decodes + 1);

            std::vector<std::byte> bytes(reference.size());
            output_chunks          outputs;
            auto                   sink            = make_output_source(outputs);
            const auto             initial_encodes = metadata_tag_calls::encoded;
            const auto             initial_hooks   = expected.resumable_encode_calls;
            auto                   encoder         = make_resumable_encoder<rvalue_metadata_tag_codec>(sink)(expected);
            encoder(std::span{bytes}.first(cut));
            if (!encoder.done()) {
                encoder(std::span{bytes}.subspan(cut));
            }

            REQUIRE(encoder.result());
            CHECK(*encoder.result() == reference.size());
            CHECK(metadata_tag_calls::encoded == initial_encodes + 1);
            CHECK(expected.resumable_encode_calls == initial_hooks + 1);
            static_tag_metadata_record decoded;
            REQUIRE(make_decoder(bytes)(decoded));
            CHECK(decoded.number == expected.number);
            CHECK(decoded.text == expected.text);
        }
    }

    TEST_CASE("explicit transcode hook works symmetrically with const encode input") {
        const explicitly_transcoded_record expected{65536, "shared continuation"};
        const auto                         reference        = ordinary_bytes(expected);
        const auto                         ordinary_encodes = expected.ordinary_calls;
        std::vector<std::byte>             bytes(reference.size());
        output_chunks                      outputs;
        auto                               sink    = make_output_source(outputs);
        auto                               encoder = make_resumable_encoder(sink)(expected);
        encoder(std::span{bytes}.first(2));
        encoder(std::span{bytes}.subspan(2));

        REQUIRE(encoder.result());
        CHECK(expected.ordinary_calls == ordinary_encodes);
        CHECK(expected.resumable_calls == 1);

        input_chunks                 inputs;
        auto                         source = make_input_source(inputs);
        explicitly_transcoded_record actual;
        auto                         decoder = make_resumable_decoder(source)(actual);
        decoder(std::span<const std::byte>{bytes}.first(3));
        decoder(std::span<const std::byte>{bytes}.subspan(3));

        REQUIRE(decoder.result());
        CHECK(actual.number == expected.number);
        CHECK(actual.text == expected.text);
        CHECK(actual.ordinary_calls == 0);
        CHECK(actual.resumable_calls == 1);
    }

    TEST_CASE("bounded wrappers retain their limits while payload arrives incrementally") {
        const std::vector<int> expected{1, 24, 65536};
        const auto             bytes = ordinary_bytes(expected);
        input_chunks           storage;
        auto                   source = make_input_source(storage);
        std::vector<int>       actual;
        auto                   bounded   = as_bounded_size<3, 3>(actual);
        auto                   operation = make_resumable_decoder(source)(bounded);

        for (std::size_t index = 0; index < bytes.size(); ++index) {
            operation(std::span<const std::byte>{bytes}.subspan(index, 1));
        }

        REQUIRE(operation.result());
        CHECK(*operation.result() == bytes.size());
        CHECK(actual == expected);
    }

    TEST_CASE("bounded append counts exclude preexisting destination elements") {
        const std::vector<int> expected{1, 24};
        const auto             bytes = ordinary_bytes(expected);
        input_chunks           storage;
        auto                   source = make_input_source(storage);
        std::vector<int>       actual{99};
        auto                   bounded   = as_bounded_size<2, 2>(actual);
        auto                   operation = make_resumable_decoder(source)(bounded);

        for (std::size_t index = 0; index < bytes.size(); ++index) {
            operation(std::span<const std::byte>{bytes}.subspan(index, 1));
        }

        REQUIRE(operation.result());
        CHECK(*operation.result() == bytes.size());
        CHECK(actual == std::vector<int>{99, 1, 24});
    }

    TEST_CASE("unwrapped group options remain effective through suspended variadic operations") {
        using unwrapped_options = Options<default_expected>;
        const resumable_message expected{"unwrapped", {1, 24, 65536}};
        std::vector<std::byte>  bytes;
        REQUIRE(make_encoder_with_options<unwrapped_options>(bytes)(expected));
        input_chunks      storage;
        auto              source = make_input_source(storage);
        resumable_message actual;
        auto              operation = make_resumable_decoder_with_options<unwrapped_options>(source)(actual);

        for (std::size_t index = 0; index < bytes.size(); ++index) {
            operation(std::span<const std::byte>{bytes}.subspan(index, 1));
        }

        REQUIRE(operation.result());
        CHECK(*operation.result() == bytes.size());
        CHECK(actual == expected);
    }

    TEST_CASE("nested container values retain the destination allocator across suspension") {
        const std::map<std::string, std::vector<int>>          expected{{std::string(48, 'k'), {1, 24, 65536}}};
        const auto                                             bytes = ordinary_bytes(expected);
        std::array<std::byte, 4096>                            allocation_storage{};
        std::pmr::monotonic_buffer_resource                    resource{allocation_storage.data(), allocation_storage.size(),
                                                                        std::pmr::null_memory_resource()};
        std::pmr::map<std::pmr::string, std::pmr::vector<int>> actual{&resource};
        input_chunks                                           storage;
        auto                                                   source    = make_input_source(storage);
        auto                                                   operation = make_resumable_decoder(source)(actual);

        for (std::size_t index = 0; index < bytes.size(); ++index) {
            operation(std::span<const std::byte>{bytes}.subspan(index, 1));
        }

        REQUIRE(operation.result());
        REQUIRE(actual.size() == 1U);
        CHECK(std::string(actual.begin()->first) == expected.begin()->first);
        CHECK(std::ranges::equal(actual.begin()->second, expected.begin()->second));
        CHECK(actual.get_allocator().resource() == &resource);
        CHECK(actual.begin()->first.get_allocator().resource() == &resource);
        CHECK(actual.begin()->second.get_allocator().resource() == &resource);
    }

    TEST_CASE("borrowed text and binary ranges span caller-owned input chunks") {
        const std::string            expected_text = "borrowed across chunks";
        const std::vector<std::byte> expected_bytes{std::byte{1}, std::byte{2}, std::byte{3}};
        std::vector<std::byte>       bytes;
        REQUIRE(make_encoder(bytes)(expected_text, expected_bytes));
        input_chunks storage;
        auto         source    = make_input_source(storage);
        using ordinary_decoder = decltype(make_decoder(source));
        typename ordinary_decoder::tstr_view_t actual_text;
        typename ordinary_decoder::bstr_view_t actual_bytes;
        auto                                   bounded_text = as_bounded_size<1, 64>(actual_text);
        auto                                   operation    = make_resumable_decoder(source)(bounded_text, actual_bytes);

        for (std::size_t index = 0; index < bytes.size(); ++index) {
            operation(std::span<const std::byte>{bytes}.subspan(index, 1));
        }

        REQUIRE(operation.result());
        CHECK(*operation.result() == bytes.size());
        CHECK(std::string(actual_text.begin(), actual_text.end()) == expected_text);
        CHECK(std::ranges::equal(actual_bytes, expected_bytes));
    }

    TEST_CASE("generator keeps each yielded item until the consumer asks for the next") {
        const std::array<int, 3> expected{7, 300, -65536};
        std::vector<std::byte>   bytes;
        REQUIRE(make_encoder(bytes)(expected[0], expected[1], expected[2]));
        input_chunks storage;
        auto         source    = make_input_source(storage);
        auto         generator = make_resumable_generator<int>(source);
        auto        &first     = generator.next();
        generator(bytes, end_of_input);

        REQUIRE(first.done());
        REQUIRE(first.result());
        CHECK_FALSE(generator.done());
        CHECK(generator.value() == expected[0]);
        CHECK(generator.value() == expected[0]);
        std::size_t consumed = *first.result();

        for (std::size_t index = 1; index < expected.size(); ++index) {
            auto &next = generator.next();
            REQUIRE(next.done());
            REQUIRE(next.result());
            CHECK_FALSE(generator.done());
            CHECK(generator.value() == expected[index]);
            consumed += *next.result();
        }

        auto &eof = generator.next();
        REQUIRE(eof.done());
        REQUIRE(eof.result());
        CHECK(*eof.result() == 0U);
        CHECK(generator.done());
        CHECK(consumed == bytes.size());
    }
}
