#pragma once

#include "cbor_tags/cbor_segments.h"
#include "cbor_tags/codec/cwt.h"

namespace cbor::tags::cwt {

template <typename T> [[nodiscard]] inline expected<byte_string, status_code> encode_to_bytes(T &&value) {
    byte_string output;
    auto        enc    = make_encoder<codec::cwt>(output);
    auto        result = enc(std::forward<T>(value));
    if (!result) {
        return unexpected<status_code>{result.error()};
    }
    return output;
}

[[nodiscard]] inline expected<byte_string, status_code> encode_protected_header(const header_map &header) {
    if (header.empty()) {
        return byte_string{};
    }
    return encode_to_bytes(header);
}

template <typename Bytes> [[nodiscard]] inline expected<header_map, status_code> decode_protected_header(const Bytes &bytes) {
    header_map header{};
    if (bytes.empty()) {
        return header;
    }

    auto dec    = make_decoder<codec::cwt>(bytes);
    auto result = dec(header);
    if (!result) {
        return unexpected<status_code>{result.error()};
    }
    if (dec.tell() != bytes.end()) {
        return unexpected<status_code>{status_code::error};
    }
    return header;
}

[[nodiscard]] inline byte_string copy_bytes(std::span<const std::byte> bytes) { return byte_string{bytes.begin(), bytes.end()}; }

template <bool Borrowed> [[nodiscard]] inline auto as_cose_sign1(basic_cose_sign1<Borrowed> value) {
    return make_tag_pair(cose_sign1_tag{}, std::move(value));
}
template <bool Borrowed> [[nodiscard]] inline auto as_cose_sign(basic_cose_sign<Borrowed> value) {
    return make_tag_pair(cose_sign_tag{}, std::move(value));
}

template <typename T> [[nodiscard]] inline auto as_cwt(T value) { return make_tag_pair(cwt_tag{}, std::move(value)); }

} // namespace cbor::tags::cwt
