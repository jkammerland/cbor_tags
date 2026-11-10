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

// A value-only declaration cannot authorize a mixin, and an encode declaration
// cannot authorize that codec's decoding direction.
namespace cbor::tags {
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
    (void)factory(std::uint32_t{1});
#endif
}
