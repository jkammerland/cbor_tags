#pragma once

#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_segments.h"
#include "cbor_tags/cwt/keys.h"

#include <array>
#include <ranges>

namespace cbor::tags::cwt {

// The deterministic projection owns framing and borrows public key bytes.
template <detail::PublicKey Key> [[nodiscard]] expected<byte_segments, status_code> make_key_thumbprint_input(const Key &key) {
    if (!detail::valid_public_key(key))
        return unexpected<status_code>{status_code::error};
    if constexpr (Key::key_type != 3) {
        if (!std::holds_alternative<std::int64_t>(key.curve))
            return unexpected<status_code>{status_code::error};
    }
    if constexpr (Key::key_type == 2) {
        if (std::holds_alternative<bool>(key.y))
            return unexpected<status_code>{status_code::error};
    }

    byte_segments parts;
    auto          enc    = make_encoder(parts);
    auto          result = enc(as_map{Key::key_type == 2 ? 4U : 3U}, 1, Key::key_type, -1);
    if (!result)
        return unexpected<status_code>{result.error()};

    if constexpr (Key::key_type == 3) {
        encode_bstr_segments_into(parts, std::span<const std::byte>{key.modulus});
        result = enc(-2);
        if (!result)
            return unexpected<status_code>{result.error()};
        encode_bstr_segments_into(parts, std::span<const std::byte>{key.exponent});
    } else {
        result = enc(std::get<std::int64_t>(key.curve), -2);
        if (!result)
            return unexpected<status_code>{result.error()};
        encode_bstr_segments_into(parts, std::span<const std::byte>{key.x});
        if constexpr (Key::key_type == 2) {
            result = enc(-3);
            if (!result)
                return unexpected<status_code>{result.error()};
            encode_bstr_segments_into(parts, std::span<const std::byte>{std::get<detail::byte_storage<Key::borrowed>>(key.y)});
        }
    }
    return parts;
}

template <detail::PublicKey Key> auto make_key_thumbprint_input(const Key &&) = delete;

// SHA256::digest consumes the range once and fills exactly 32 output bytes.
template <typename SHA256, detail::PublicKey Key>
[[nodiscard]] expected<void, status_code> key_thumbprint(const Key &key, std::span<std::byte, 32> output) {
    auto parts = make_key_thumbprint_input(key);
    if (!parts)
        return unexpected<status_code>{parts.error()};
    auto spans = *parts | std::views::transform([](const auto &part) { return part.bytes(); });
    return SHA256::digest(spans, output);
}

template <typename SHA256, detail::PublicKey Key>
[[nodiscard]] expected<std::array<std::byte, 32>, status_code> key_thumbprint(const Key &key) {
    std::array<std::byte, 32> output{};
    auto                      result = key_thumbprint<SHA256>(key, std::span<std::byte, 32>{output});
    if (!result)
        return unexpected<status_code>{result.error()};
    return output;
}
} // namespace cbor::tags::cwt
