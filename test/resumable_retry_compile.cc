#include <cbor_tags/cbor_resumable.h>
#include <cbor_tags/codec/shared_ptr.h>
#include <cbor_tags/codec/typed_array.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

using namespace cbor::tags;

struct record {
    std::string                      first;
    std::string                      second;
    template <typename Decoder> auto decode(Decoder &decoder) { return decoder(wrap_as_array{first, second}); }
    template <typename Encoder> auto encode(Encoder &encoder) const { return encoder(wrap_as_array{first, second}); }
};

struct adl_record {
    std::string value;
};
template <typename Decoder, typename T>
    requires std::same_as<std::remove_cvref_t<T>, adl_record>
auto decode(Decoder &decoder, T &&value) {
    return decoder(value.value);
}

struct tagged_record {
    static constexpr std::uint64_t cbor_tag = 123;
    int                            value{};
};

enum class retry_level : std::uint32_t { low = 7 };

template <typename Self> struct unsafe_integer_codec : codec::decoder_base<Self> {
    using codec::decoder_base<Self>::decode;
    std::size_t calls{};

    status_code decode(std::uint32_t &value) {
        ++calls;
        std::uint64_t next{};
        const auto    result = static_cast<Self &>(*this)(next);
        if (!result)
            return result.error();
        value = static_cast<std::uint32_t>(next);
        return status_code::success;
    }
};

// A whole-enum codec does not call the installed uint32 codec.
template <typename Self> struct enum_codec : codec::decoder_base<Self> {
    using codec::decoder_base<Self>::decode;

    status_code decode(retry_level &value) {
        std::uint64_t next{};
        const auto    result = static_cast<Self &>(*this)(next);
        if (!result)
            return result.error();
        value = static_cast<retry_level>(next);
        return status_code::success;
    }
};

// The core IsEnum overload hides this same-signature template.
template <typename Self> struct hidden_enum_codec : codec::decoder_base<Self> {
    using codec::decoder_base<Self>::decode;

    template <IsEnum T> status_code decode(T &value) {
        std::uint64_t next{};
        const auto    result = static_cast<Self &>(*this)(next);
        if (!result)
            return result.error();
        value = static_cast<T>(next);
        return status_code::success;
    }
};

template <typename Self> struct unsafe_codec : codec::base<Self> {
    using codec::base<Self>::decode;
    using codec::base<Self>::encode;
    status_code decode(std::uint32_t &value, major_type major, std::byte info) {
        std::uint64_t next{};
        const auto    status = static_cast<Self &>(*this).decode(next, major, info);
        value += static_cast<std::uint32_t>(next);
        return status;
    }
    status_code decode(tagged_record &value, std::uint64_t) {
        const auto result = static_cast<Self &>(*this)(value.value);
        return result ? status_code::success : result.error();
    }
    void encode(std::uint32_t value) { static_cast<Self &>(*this).encode(static_cast<std::uint64_t>(value)); }
};

template <typename Self> struct plain_unsafe_codec {
    status_code decode(std::uint32_t &value, major_type major, std::byte info) {
        return static_cast<Self &>(*this).decode(value, major, info);
    }
};

template <typename Self> struct stateful_observer : codec::base<Self> {
    using codec::base<Self>::decode;
    using codec::base<Self>::encode;
    std::size_t calls{};
    status_code observe_decoded_cbor_tag(std::uint64_t) {
        ++calls;
        return status_code::success;
    }
    void observe_encoded_cbor_tag(std::uint64_t) { ++calls; }
};

// Takes over one view type from codec::typed_array. The library's declaration
// for codec::typed_array doesn't cover it.
template <typename Self> struct view_override : codec::base<Self> {
    using codec::base<Self>::decode;
    using codec::base<Self>::encode;
    std::size_t calls{};

    template <typename ByteRange> status_code decode(rfc8746::typed_array_view<std::uint16_t, ByteRange> &, major_type, std::byte) {
        ++calls;
        return status_code::success;
    }
};

// A value-only declaration cannot authorize a mixin, and an encode declaration
// cannot authorize that codec's decoding direction.
namespace cbor::tags {
template <typename Self> struct resumable_retry_traits<retry_level, enum_codec<Self>> {
    static constexpr bool decode = true;
};
template <typename Self> struct resumable_retry_traits<retry_level, hidden_enum_codec<Self>> {
    static constexpr bool decode = true;
};
template <> struct resumable_retry_traits<std::uint32_t> {
    static constexpr bool encode = true;
    static constexpr bool decode = true;
};
template <typename Self> struct resumable_retry_traits<std::uint32_t, unsafe_codec<Self>> {
    static constexpr bool encode = true;
    static constexpr bool decode = false;
};
} // namespace cbor::tags

int main() {
    std::vector<std::byte> bytes;
#if defined(CBOR_RETRY_SHARED)
    auto source = make_input_source(bytes);
    auto sink   = make_output_buffer(bytes);
#if defined(CBOR_RETRY_SOURCE)
#define CBOR_RETRY_INPUT  source
#define CBOR_RETRY_OUTPUT sink
#else
#define CBOR_RETRY_INPUT  bytes
#define CBOR_RETRY_OUTPUT bytes
#endif
#if defined(CBOR_RETRY_ENCODE) && defined(CBOR_RETRY_OPTIONS)
    auto factory = make_resumable_encoder_with_options<default_options, codec::shared_ptr>(CBOR_RETRY_OUTPUT);
#elif defined(CBOR_RETRY_ENCODE)
    auto factory = make_resumable_encoder<codec::shared_ptr>(CBOR_RETRY_OUTPUT);
#elif defined(CBOR_RETRY_OPTIONS)
    auto factory = make_resumable_decoder_with_options<default_options, codec::shared_ptr>(CBOR_RETRY_INPUT);
#else
    auto factory = make_resumable_decoder<codec::shared_ptr>(CBOR_RETRY_INPUT);
#endif
    (void)factory;
#elif defined(CBOR_RETRY_OBSERVER_DECODE)
    rfc8746::typed_array<std::uint16_t> value;
    auto                                factory   = make_resumable_decoder<stateful_observer, codec::typed_array>(bytes);
    auto                                operation = factory(value);
#elif defined(CBOR_RETRY_VIEW_OVERRIDE)
    auto factory = make_resumable_decoder<view_override, codec::typed_array>(bytes);
    rfc8746::typed_array_view_for<std::uint16_t, decltype(factory)> value;
    auto                                                            operation = factory(value);
#elif defined(CBOR_RETRY_OBSERVER_ENCODE)
    rfc8746::typed_array<std::uint16_t> value{1};
    auto                                factory   = make_resumable_encoder<stateful_observer, codec::typed_array>(bytes);
    auto                                operation = factory(value);
#elif defined(CBOR_RETRY_MEMBER_ENCODE)
    record value;
    auto   factory   = make_resumable_encoder(bytes);
    auto   operation = factory(value);
#elif defined(CBOR_RETRY_MEMBER_DECODE)
    record value;
    auto   factory   = make_resumable_decoder(bytes);
    auto   operation = factory(value);
#elif defined(CBOR_RETRY_ADL)
    static_assert(IsClassWithDecodingOverload<decltype(make_decoder(bytes)), adl_record>);
    adl_record value;
    auto       factory   = make_resumable_decoder(bytes);
    auto       operation = factory(value);
#elif defined(CBOR_RETRY_MIXIN_DIRECT)
    std::uint32_t value{};
    auto          factory   = make_resumable_decoder<unsafe_codec>(bytes);
    auto          operation = factory(value);
#elif defined(CBOR_RETRY_ENUM_DIRECT)
    retry_level value{};
    auto        factory   = make_resumable_decoder<unsafe_integer_codec>(bytes);
    auto        operation = factory(value);
#elif defined(CBOR_RETRY_ENUM_HEADER)
    std::optional<retry_level> value;
    auto                       factory   = make_resumable_decoder<unsafe_codec>(bytes);
    auto                       operation = factory(value);
#elif defined(CBOR_RETRY_ENUM_HIDDEN)
    retry_level value{};
    auto        factory   = make_resumable_decoder<hidden_enum_codec, unsafe_integer_codec>(bytes);
    auto        operation = factory(value);
#elif defined(CBOR_RETRY_MIXIN_HEADER)
    std::optional<std::uint32_t> value;
    auto                         factory   = make_resumable_decoder<unsafe_codec>(bytes);
    auto                         operation = factory(value);
#elif defined(CBOR_RETRY_MIXIN_TAG)
    std::variant<tagged_record, std::string> value;
    auto                                     factory   = make_resumable_decoder<unsafe_codec>(bytes);
    auto                                     operation = factory(value);
#elif defined(CBOR_RETRY_PLAIN)
    std::uint32_t value{};
    auto          factory   = make_resumable_decoder<plain_unsafe_codec>(bytes);
    auto          operation = factory(value);
#elif defined(CBOR_RETRY_NODISCARD)
    int  value{};
    auto factory = make_resumable_decoder(bytes);
    factory(value);
#else
    // Positive control: opted-in encoding and explicit discarding compile.
    auto factory = make_resumable_encoder<unsafe_codec>(bytes);
    {
        auto operation = factory(std::uint32_t{42});
        if (!operation.done() || !operation.result())
            return 1;
    }
    auto          ordinary = make_decoder(bytes);
    std::uint64_t value{};
    if (!ordinary(value) || value != 42)
        return 2;

    // Inactive integer codecs need no declaration when the enum has its own codec.
    auto        enum_factory = make_resumable_decoder<enum_codec, unsafe_integer_codec, unsafe_codec>(bytes);
    retry_level enum_value{};
    auto        enum_operation = enum_factory(enum_value);
    if (!enum_operation.done() || !enum_operation.result() || static_cast<std::uint32_t>(enum_value) != 42)
        return 3;
    (void)factory(std::uint32_t{1});
#endif
}
