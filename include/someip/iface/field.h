#pragma once

#include "someip/wire/someip.h"

#include <cstdint>

namespace someip::iface {

struct field_descriptor {
    std::uint16_t service_id{0};
    std::uint16_t getter_method_id{0};
    std::uint16_t setter_method_id{0};
    std::uint16_t notifier_event_id{0};
    std::uint16_t eventgroup_id{0};
    bool          readable{true};
    bool          writable{false};
    bool          notifies{false};
};

inline wire::header make_get_request_header(const field_descriptor &f, wire::request_id req,
                                            std::uint8_t interface_version, std::uint8_t protocol_version = 1) noexcept {
    wire::header h{};
    h.msg.service_id    = f.service_id;
    h.msg.method_id     = f.getter_method_id;
    h.req              = req;
    h.protocol_version = protocol_version;
    h.interface_version = interface_version;
    h.msg_type          = wire::message_type::request;
    h.return_code       = 0;
    return h;
}

inline wire::header make_set_request_header(const field_descriptor &f, wire::request_id req,
                                            std::uint8_t interface_version, std::uint8_t protocol_version = 1) noexcept {
    wire::header h{};
    h.msg.service_id    = f.service_id;
    h.msg.method_id     = f.setter_method_id;
    h.req              = req;
    h.protocol_version = protocol_version;
    h.interface_version = interface_version;
    h.msg_type          = wire::message_type::request;
    h.return_code       = 0;
    return h;
}

inline wire::header make_notify_header(const field_descriptor &f, std::uint8_t interface_version,
                                       std::uint8_t protocol_version = 1) noexcept {
    wire::header h{};
    h.msg.service_id    = f.service_id;
    h.msg.method_id     = f.notifier_event_id;
    h.req.client_id     = 0;
    h.req.session_id    = 0;
    h.protocol_version  = protocol_version;
    h.interface_version = interface_version;
    h.msg_type          = wire::message_type::notification;
    h.return_code       = 0;
    return h;
}

[[nodiscard]] constexpr bool is_get_request(const wire::header &h, const field_descriptor &f) noexcept {
    return h.msg.service_id == f.service_id && h.msg.method_id == f.getter_method_id &&
           h.msg_type == wire::message_type::request;
}

[[nodiscard]] constexpr bool is_set_request(const wire::header &h, const field_descriptor &f) noexcept {
    return h.msg.service_id == f.service_id && h.msg.method_id == f.setter_method_id &&
           h.msg_type == wire::message_type::request;
}

} // namespace someip::iface
