#pragma once

#include "someip/status.h"
#include "someip/wire/cursor.h"
#include "someip/wire/endian.h"
#include "someip/wire/tp.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

namespace someip::wire {

struct message_id {
    std::uint16_t service_id{0};
    std::uint16_t method_id{0};
};

struct request_id {
    std::uint16_t client_id{0};
    std::uint16_t session_id{0};
};

namespace message_type {
static constexpr std::uint8_t request           = 0x00;
static constexpr std::uint8_t request_no_return = 0x01;
static constexpr std::uint8_t notification      = 0x02;
static constexpr std::uint8_t response          = 0x80;
static constexpr std::uint8_t error             = 0x81;
static constexpr std::uint8_t tp_flag           = 0x20;
} // namespace message_type

struct header {
    message_id    msg{};
    std::uint32_t length{0}; // bytes after this field: 8(header tail) + TP hdr (optional) + payload bytes
    request_id    req{};
    std::uint8_t  protocol_version{1};
    std::uint8_t  interface_version{0};
    std::uint8_t  msg_type{0};
    std::uint8_t  return_code{0};
};

template <class Out>
expected<void> encode_header(writer<Out> &out, const header &h) noexcept {
    // Header is always big endian.
    auto st = write_uint<endian::big>(out, h.msg.service_id);
    if (!st) return st;
    st = write_uint<endian::big>(out, h.msg.method_id);
    if (!st) return st;
    st = write_uint<endian::big>(out, h.length);
    if (!st) return st;
    st = write_uint<endian::big>(out, h.req.client_id);
    if (!st) return st;
    st = write_uint<endian::big>(out, h.req.session_id);
    if (!st) return st;
    st = write_uint<endian::big>(out, h.protocol_version);
    if (!st) return st;
    st = write_uint<endian::big>(out, h.interface_version);
    if (!st) return st;
    st = write_uint<endian::big>(out, h.msg_type);
    if (!st) return st;
    st = write_uint<endian::big>(out, h.return_code);
    if (!st) return st;
    return {};
}

inline expected<header> decode_header(std::span<const std::byte> frame) noexcept {
    if (frame.size() < 16) {
        return unexpected<status_code>(status_code::buffer_overrun);
    }
    reader in{frame.subspan(0, 16)};

    header h{};
    auto   service_id = read_uint<endian::big, std::uint16_t>(in);
    if (!service_id) return unexpected<status_code>(service_id.error());
    auto method_id = read_uint<endian::big, std::uint16_t>(in);
    if (!method_id) return unexpected<status_code>(method_id.error());
    auto length = read_uint<endian::big, std::uint32_t>(in);
    if (!length) return unexpected<status_code>(length.error());
    auto client_id = read_uint<endian::big, std::uint16_t>(in);
    if (!client_id) return unexpected<status_code>(client_id.error());
    auto session_id = read_uint<endian::big, std::uint16_t>(in);
    if (!session_id) return unexpected<status_code>(session_id.error());
    auto pv = read_uint<endian::big, std::uint8_t>(in);
    if (!pv) return unexpected<status_code>(pv.error());
    auto iv = read_uint<endian::big, std::uint8_t>(in);
    if (!iv) return unexpected<status_code>(iv.error());
    auto mt = read_uint<endian::big, std::uint8_t>(in);
    if (!mt) return unexpected<status_code>(mt.error());
    auto rc = read_uint<endian::big, std::uint8_t>(in);
    if (!rc) return unexpected<status_code>(rc.error());

    h.msg.service_id       = *service_id;
    h.msg.method_id        = *method_id;
    h.length               = *length;
    h.req.client_id        = *client_id;
    h.req.session_id       = *session_id;
    h.protocol_version     = *pv;
    h.interface_version    = *iv;
    h.msg_type             = *mt;
    h.return_code          = *rc;

    if (h.protocol_version != 1) {
        return unexpected<status_code>(status_code::invalid_protocol_version);
    }

    if (h.length < 8) {
        return unexpected<status_code>(status_code::invalid_length);
    }

    return h;
}

[[nodiscard]] constexpr bool has_tp_flag(const header &h) noexcept { return (h.msg_type & message_type::tp_flag) != 0; }

inline expected<std::size_t> frame_size_from_prefix(std::span<const std::byte> prefix8) noexcept {
    if (prefix8.size() < 8) {
        return unexpected<status_code>(status_code::buffer_overrun);
    }
    reader in{prefix8.subspan(0, 8)};
    // Skip message id
    auto service_id = read_uint<endian::big, std::uint16_t>(in);
    if (!service_id) return unexpected<status_code>(service_id.error());
    auto method_id = read_uint<endian::big, std::uint16_t>(in);
    if (!method_id) return unexpected<status_code>(method_id.error());
    (void)service_id;
    (void)method_id;
    auto length = read_uint<endian::big, std::uint32_t>(in);
    if (!length) {
        return unexpected<status_code>(length.error());
    }
    if (*length < 8) {
        return unexpected<status_code>(status_code::invalid_length);
    }
    return static_cast<std::size_t>(8u + *length);
}

struct parsed_frame {
    header                         hdr{};
    std::optional<tp_header>       tp{};
    std::span<const std::byte>     payload{};
    std::size_t                    consumed{0};
};

inline expected<parsed_frame> try_parse_frame(std::span<const std::byte> bytes) noexcept {
    if (bytes.size() < 8) {
        return unexpected<status_code>(status_code::incomplete_frame);
    }
    const auto total = frame_size_from_prefix(bytes.subspan(0, 8));
    if (!total) {
        return unexpected<status_code>(total.error());
    }
    if (bytes.size() < *total) {
        return unexpected<status_code>(status_code::incomplete_frame);
    }

    auto hdr = decode_header(bytes.subspan(0, 16));
    if (!hdr) {
        return unexpected<status_code>(hdr.error());
    }

    const bool tp = has_tp_flag(*hdr);
    const auto tp_bytes = tp ? 4u : 0u;

    if (hdr->length < (8u + tp_bytes)) {
        return unexpected<status_code>(status_code::invalid_length);
    }

    const std::size_t payload_size  = static_cast<std::size_t>(hdr->length - 8u - tp_bytes);
    const std::size_t payload_start = tp ? 20u : 16u;
    if (payload_start + payload_size > *total) {
        return unexpected<status_code>(status_code::invalid_length);
    }

    parsed_frame pf{};
    pf.hdr      = *hdr;
    pf.consumed = *total;

    if (tp) {
        reader tp_r{bytes.subspan(16, 4)};
        auto tp_h = decode_tp_header(tp_r);
        if (!tp_h) {
            return unexpected<status_code>(tp_h.error());
        }
        pf.tp = *tp_h;
    }

    pf.payload = bytes.subspan(payload_start, payload_size);
    return pf;
}

} // namespace someip::wire

