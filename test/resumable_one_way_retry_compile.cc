#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace ct = cbor::tags;

// File-local on purpose. The declaration below gets internal linkage from it,
// and Clang warns about a flag in there that nobody uses. This file is built
// with that warning as an error, and label only ever gets encoded
namespace {
struct label {
    std::string text;

    template <typename Encoder> auto encode(Encoder &encoder) const { return encoder(text); }
};
} // namespace

// A declaration has both flags, leaving one out is the compile-fail case
template <> struct cbor::tags::resumable_retry_traits<label> {
    static constexpr bool encode = true;
#if !defined(CBOR_TAGS_RETRY_ONE_FLAG)
    static constexpr bool decode = false;
#endif
};

// Only ever installed in an encoder
template <typename Self> struct doubled : ct::codec::encoder_base<Self> {
    using ct::codec::encoder_base<Self>::encode;

    void encode(std::uint32_t value) { static_cast<Self &>(*this).encode(std::uint64_t{value} * 2); }
};

// The decode flag asks the one-shot decoder for a type the encoder doesn't
// have. Nothing decodes here, so nothing may read that flag. MSVC works out
// every flag as soon as the declaration is used at all, whoever asks, so it
// gets a flag that can always be worked out
template <typename Self> struct cbor::tags::resumable_retry_traits<std::uint32_t, doubled<Self>> {
    static constexpr bool encode = true;
#if defined(_MSC_VER) && !defined(__clang__)
    static constexpr bool decode = false;
#else
    static constexpr bool decode = std::is_class_v<typename Self::input_buffer_type>;
#endif
};

int main() {
    std::vector<std::byte> bytes;
    const label            value{"only ever encoded"};

    auto encoder   = ct::make_resumable_encoder(bytes);
    auto operation = encoder(value);

    std::vector<std::byte> doubled_bytes;
    auto                   doubling          = ct::make_resumable_encoder<doubled>(doubled_bytes);
    auto                   doubled_operation = doubling(std::uint32_t{21});

    // 21 goes out as 42
    const bool doubled_ok =
        doubled_operation.done() && doubled_operation.result() && doubled_bytes == std::vector<std::byte>{std::byte{0x18}, std::byte{0x2a}};
    return operation.done() && operation.result() && doubled_ok ? 0 : 1;
}
