#pragma once

#include "someip/status.h"
#include "someip/wire/cursor.h"
#include "someip/wire/endian.h"
#include "someip/wire/someip.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace someip::sd {

constexpr std::uint16_t kServiceId = 0xFFFF;
constexpr std::uint16_t kMethodId  = 0x8100;

namespace entry_type {
static constexpr std::uint8_t find_service  = 0x00;
static constexpr std::uint8_t offer_service = 0x01;

static constexpr std::uint8_t subscribe_eventgroup     = 0x06;
static constexpr std::uint8_t subscribe_eventgroup_ack = 0x07;
} // namespace entry_type

namespace option_type {
static constexpr std::uint8_t configuration      = 0x01;
static constexpr std::uint8_t load_balancing     = 0x02;
static constexpr std::uint8_t ipv4_endpoint      = 0x04;
static constexpr std::uint8_t ipv6_endpoint      = 0x06;
static constexpr std::uint8_t ipv4_multicast     = 0x14;
static constexpr std::uint8_t ipv6_multicast     = 0x16;
static constexpr std::uint8_t ipv4_sd_endpoint   = 0x24;
static constexpr std::uint8_t ipv6_sd_endpoint   = 0x26;
} // namespace option_type

struct payload_header {
    std::uint8_t  flags{0};
    std::uint32_t reserved24{0}; // low 24 bits used
};

struct entry_common {
    std::uint8_t  type{0};
    std::uint8_t  index1{0};
    std::uint8_t  index2{0};
    std::uint8_t  numopt1_numopt2{0}; // high nibble: run1 count, low nibble: run2 count
    std::uint16_t service_id{0};
    std::uint16_t instance_id{0};
    std::uint8_t  major_version{0};
    std::uint32_t ttl{0}; // low 24 bits used
};

struct service_entry {
    entry_common  c{};
    std::uint32_t minor_version{0};
};

struct eventgroup_entry {
    entry_common  c{};
    std::uint16_t reserved12_counter4{0};
    std::uint16_t eventgroup_id{0};
};

using entry = std::variant<service_entry, eventgroup_entry>;

struct option_prefix {
    std::uint16_t length{0}; // excludes length field + type field
    std::uint8_t  type{0};
    std::uint8_t  discardable_and_reserved{0}; // bit7 discardable, lower 7 reserved
};

[[nodiscard]] constexpr bool discardable(option_prefix p) noexcept { return (p.discardable_and_reserved & 0x80u) != 0u; }

struct configuration_option {
    bool                   discardable{false};
    std::vector<std::byte> bytes{};
};

struct load_balancing_option {
    bool          discardable{false};
    std::uint16_t priority{0};
    std::uint16_t weight{0};
};

struct ipv4_endpoint_option {
    bool                   discardable{false};
    std::array<std::byte, 4> address{};
    std::uint8_t           l4_proto{0};
    std::uint16_t          port{0};
    std::uint8_t           reserved{0};
};

struct ipv6_endpoint_option {
    bool                    discardable{false};
    std::array<std::byte, 16> address{};
    std::uint8_t            l4_proto{0};
    std::uint16_t           port{0};
    std::uint8_t            reserved{0};
};

struct ipv4_multicast_option : ipv4_endpoint_option {};
struct ipv6_multicast_option : ipv6_endpoint_option {};
struct ipv4_sd_endpoint_option : ipv4_endpoint_option {};
struct ipv6_sd_endpoint_option : ipv6_endpoint_option {};

struct unknown_option {
    std::uint8_t           type{0};
    bool                   discardable{false};
    std::vector<std::byte> data{};
};

using option = std::variant<configuration_option, load_balancing_option, ipv4_endpoint_option, ipv6_endpoint_option, ipv4_multicast_option,
                            ipv6_multicast_option, ipv4_sd_endpoint_option, ipv6_sd_endpoint_option, unknown_option>;

struct payload {
    payload_header       hdr{};
    std::vector<entry>   entries{};
    std::vector<option> options{};
};

struct service_entry_data {
    std::uint8_t           type{entry_type::offer_service};
    std::uint16_t          service_id{0};
    std::uint16_t          instance_id{0};
    std::uint8_t           major_version{0};
    std::uint32_t          ttl{0};
    std::uint32_t          minor_version{0};
    std::vector<option>    run1{};
    std::vector<option>    run2{};
};

struct eventgroup_entry_data {
    std::uint8_t           type{entry_type::subscribe_eventgroup};
    std::uint16_t          service_id{0};
    std::uint16_t          instance_id{0};
    std::uint8_t           major_version{0};
    std::uint32_t          ttl{0};
    std::uint16_t          reserved12_counter4{0};
    std::uint16_t          eventgroup_id{0};
    std::vector<option>    run1{};
    std::vector<option>    run2{};
};

using entry_data = std::variant<service_entry_data, eventgroup_entry_data>;

struct packet_data {
    payload_header          hdr{};
    std::uint16_t           client_id{0};
    std::uint16_t           session_id{0};
    std::vector<entry_data> entries{};
};

[[nodiscard]] constexpr std::uint8_t run1_count(std::uint8_t numopt1_numopt2) noexcept { return static_cast<std::uint8_t>(numopt1_numopt2 >> 4); }
[[nodiscard]] constexpr std::uint8_t run2_count(std::uint8_t numopt1_numopt2) noexcept { return static_cast<std::uint8_t>(numopt1_numopt2 & 0x0F); }

[[nodiscard]] inline expected<std::uint32_t> add_u32(std::uint32_t left, std::uint32_t right) noexcept {
    if (right > (std::numeric_limits<std::uint32_t>::max() - left)) {
        return unexpected<status_code>(status_code::invalid_length);
    }
    return static_cast<std::uint32_t>(left + right);
}

[[nodiscard]] inline expected<std::uint32_t> entries_wire_len(std::size_t entries_count) noexcept {
    if (entries_count > (std::numeric_limits<std::uint32_t>::max() / 16u)) {
        return unexpected<status_code>(status_code::invalid_length);
    }
    return static_cast<std::uint32_t>(entries_count * 16u);
}

[[nodiscard]] inline expected<std::uint16_t> option_len_field(const option &o) noexcept;

[[nodiscard]] inline expected<std::uint32_t> options_wire_len(const std::vector<option> &options) noexcept {
    std::uint32_t options_len = 0;
    for (const auto &o : options) {
        auto len_field = option_len_field(o);
        if (!len_field) return unexpected<status_code>(len_field.error());
        auto option_total = add_u32(3u, static_cast<std::uint32_t>(*len_field));
        if (!option_total) return unexpected<status_code>(option_total.error());
        auto updated_len = add_u32(options_len, *option_total);
        if (!updated_len) return unexpected<status_code>(updated_len.error());
        options_len = *updated_len;
    }
    return options_len;
}

[[nodiscard]] inline expected<payload> build_payload(const packet_data &pd) noexcept {
    payload out{};
    out.hdr = pd.hdr;
    out.entries.reserve(pd.entries.size());

    std::size_t total_options = 0;
    for (const auto &e : pd.entries) {
        const auto add = std::visit(
            [](const auto &d) -> std::size_t {
                return d.run1.size() + d.run2.size();
            },
            e);
        if (add > 0xFFu || total_options > (0xFFu - add)) {
            return unexpected<status_code>(status_code::invalid_length);
        }
        total_options += add;
    }
    out.options.reserve(total_options);

    for (const auto &e : pd.entries) {
        if (std::holds_alternative<service_entry_data>(e)) {
            const auto &d = std::get<service_entry_data>(e);

            if (d.run1.size() > 0x0Fu || d.run2.size() > 0x0Fu) {
                return unexpected<status_code>(status_code::invalid_length);
            }
            const auto total_opts = out.options.size() + d.run1.size() + d.run2.size();
            if (total_opts > 0xFFu) {
                return unexpected<status_code>(status_code::invalid_length);
            }

            service_entry se{};
            se.c.type           = d.type;
            se.c.service_id     = d.service_id;
            se.c.instance_id    = d.instance_id;
            se.c.major_version  = d.major_version;
            se.c.ttl            = d.ttl & 0xFFFFFFu;
            se.minor_version    = d.minor_version;

            const auto start1 = static_cast<std::uint8_t>(out.options.size());
            se.c.index1 = d.run1.empty() ? 0u : start1;
            se.c.index2 = 0u;

            for (const auto &opt : d.run1) {
                out.options.push_back(opt);
            }

            const auto start2 = static_cast<std::uint8_t>(out.options.size());
            se.c.index2 = d.run2.empty() ? 0u : start2;
            for (const auto &opt : d.run2) {
                out.options.push_back(opt);
            }

            se.c.numopt1_numopt2 = static_cast<std::uint8_t>((std::uint8_t(d.run1.size()) << 4) | std::uint8_t(d.run2.size()));
            out.entries.emplace_back(se);
        } else {
            const auto &d = std::get<eventgroup_entry_data>(e);

            if (d.run1.size() > 0x0Fu || d.run2.size() > 0x0Fu) {
                return unexpected<status_code>(status_code::invalid_length);
            }
            const auto total_opts = out.options.size() + d.run1.size() + d.run2.size();
            if (total_opts > 0xFFu) {
                return unexpected<status_code>(status_code::invalid_length);
            }

            eventgroup_entry eg{};
            eg.c.type              = d.type;
            eg.c.service_id        = d.service_id;
            eg.c.instance_id       = d.instance_id;
            eg.c.major_version     = d.major_version;
            eg.c.ttl               = d.ttl & 0xFFFFFFu;
            eg.reserved12_counter4 = d.reserved12_counter4;
            eg.eventgroup_id       = d.eventgroup_id;

            const auto start1 = static_cast<std::uint8_t>(out.options.size());
            eg.c.index1 = d.run1.empty() ? 0u : start1;
            eg.c.index2 = 0u;
            for (const auto &opt : d.run1) {
                out.options.push_back(opt);
            }

            const auto start2 = static_cast<std::uint8_t>(out.options.size());
            eg.c.index2 = d.run2.empty() ? 0u : start2;
            for (const auto &opt : d.run2) {
                out.options.push_back(opt);
            }

            eg.c.numopt1_numopt2 = static_cast<std::uint8_t>((std::uint8_t(d.run1.size()) << 4) | std::uint8_t(d.run2.size()));
            out.entries.emplace_back(eg);
        }
    }

    return out;
}

template <class Out>
expected<void> encode_entry(wire::writer<Out> &out, const service_entry &e) noexcept {
    auto st = wire::write_uint<wire::endian::big>(out, e.c.type);
    if (!st) return st;
    st = wire::write_uint<wire::endian::big>(out, e.c.index1);
    if (!st) return st;
    st = wire::write_uint<wire::endian::big>(out, e.c.index2);
    if (!st) return st;
    st = wire::write_uint<wire::endian::big>(out, e.c.numopt1_numopt2);
    if (!st) return st;
    st = wire::write_uint<wire::endian::big>(out, e.c.service_id);
    if (!st) return st;
    st = wire::write_uint<wire::endian::big>(out, e.c.instance_id);
    if (!st) return st;
    st = wire::write_uint<wire::endian::big>(out, e.c.major_version);
    if (!st) return st;
    st = wire::write_u24_be(out, e.c.ttl & 0xFFFFFFu);
    if (!st) return st;
    st = wire::write_uint<wire::endian::big>(out, e.minor_version);
    if (!st) return st;
    return {};
}

template <class Out>
expected<void> encode_entry(wire::writer<Out> &out, const eventgroup_entry &e) noexcept {
    auto st = wire::write_uint<wire::endian::big>(out, e.c.type);
    if (!st) return st;
    st = wire::write_uint<wire::endian::big>(out, e.c.index1);
    if (!st) return st;
    st = wire::write_uint<wire::endian::big>(out, e.c.index2);
    if (!st) return st;
    st = wire::write_uint<wire::endian::big>(out, e.c.numopt1_numopt2);
    if (!st) return st;
    st = wire::write_uint<wire::endian::big>(out, e.c.service_id);
    if (!st) return st;
    st = wire::write_uint<wire::endian::big>(out, e.c.instance_id);
    if (!st) return st;
    st = wire::write_uint<wire::endian::big>(out, e.c.major_version);
    if (!st) return st;
    st = wire::write_u24_be(out, e.c.ttl & 0xFFFFFFu);
    if (!st) return st;
    st = wire::write_uint<wire::endian::big>(out, e.reserved12_counter4);
    if (!st) return st;
    st = wire::write_uint<wire::endian::big>(out, e.eventgroup_id);
    if (!st) return st;
    return {};
}

[[nodiscard]] inline expected<std::uint16_t> option_len_field(const option &o) noexcept {
    auto result = std::visit(
        [&](const auto &opt) -> expected<std::uint16_t> {
            using O = std::remove_cvref_t<decltype(opt)>;
            if constexpr (std::is_same_v<O, configuration_option>) {
                const auto len = 1u + opt.bytes.size();
                if (len > 0xFFFFu) return unexpected<status_code>(status_code::invalid_length);
                return static_cast<std::uint16_t>(len);
            } else if constexpr (std::is_same_v<O, load_balancing_option>) {
                return static_cast<std::uint16_t>(1u + 4u);
            } else if constexpr (std::is_same_v<O, ipv4_endpoint_option> || std::is_same_v<O, ipv4_multicast_option> ||
                                 std::is_same_v<O, ipv4_sd_endpoint_option>) {
                return static_cast<std::uint16_t>(0x0009u);
            } else if constexpr (std::is_same_v<O, ipv6_endpoint_option> || std::is_same_v<O, ipv6_multicast_option> ||
                                 std::is_same_v<O, ipv6_sd_endpoint_option>) {
                return static_cast<std::uint16_t>(0x0015u);
            } else if constexpr (std::is_same_v<O, unknown_option>) {
                const auto len = 1u + opt.data.size();
                if (len > 0xFFFFu) return unexpected<status_code>(status_code::invalid_length);
                return static_cast<std::uint16_t>(len);
            } else {
                return unexpected<status_code>(status_code::error);
            }
        },
        o);
    return result;
}

[[nodiscard]] inline expected<std::uint8_t> option_type_id(const option &o) noexcept {
    return std::visit(
        [&](const auto &opt) -> expected<std::uint8_t> {
            using O = std::remove_cvref_t<decltype(opt)>;
            if constexpr (std::is_same_v<O, configuration_option>) return option_type::configuration;
            if constexpr (std::is_same_v<O, load_balancing_option>) return option_type::load_balancing;
            if constexpr (std::is_same_v<O, ipv4_endpoint_option>) return option_type::ipv4_endpoint;
            if constexpr (std::is_same_v<O, ipv6_endpoint_option>) return option_type::ipv6_endpoint;
            if constexpr (std::is_same_v<O, ipv4_multicast_option>) return option_type::ipv4_multicast;
            if constexpr (std::is_same_v<O, ipv6_multicast_option>) return option_type::ipv6_multicast;
            if constexpr (std::is_same_v<O, ipv4_sd_endpoint_option>) return option_type::ipv4_sd_endpoint;
            if constexpr (std::is_same_v<O, ipv6_sd_endpoint_option>) return option_type::ipv6_sd_endpoint;
            if constexpr (std::is_same_v<O, unknown_option>) return opt.type;
            return unexpected<status_code>(status_code::error);
        },
        o);
}

template <class Out>
expected<void> encode_option(wire::writer<Out> &out, const option &o) noexcept {
    auto len = option_len_field(o);
    if (!len) return unexpected<status_code>(len.error());
    auto type = option_type_id(o);
    if (!type) return unexpected<status_code>(type.error());

    auto st = wire::write_uint<wire::endian::big>(out, *len);
    if (!st) return st;
    st = wire::write_uint<wire::endian::big>(out, *type);
    if (!st) return st;

    return std::visit(
        [&](const auto &opt) -> expected<void> {
            using O = std::remove_cvref_t<decltype(opt)>;
            const auto discard_byte = std::uint8_t(opt.discardable ? 0x80u : 0x00u);
            auto       st2          = wire::write_uint<wire::endian::big>(out, discard_byte);
            if (!st2) return st2;

            if constexpr (std::is_same_v<O, configuration_option>) {
                return out.write_bytes(std::span<const std::byte>(opt.bytes.data(), opt.bytes.size()));
            } else if constexpr (std::is_same_v<O, load_balancing_option>) {
                st2 = wire::write_uint<wire::endian::big>(out, opt.priority);
                if (!st2) return st2;
                st2 = wire::write_uint<wire::endian::big>(out, opt.weight);
                if (!st2) return st2;
                return {};
            } else if constexpr (std::is_same_v<O, ipv4_endpoint_option> || std::is_same_v<O, ipv4_multicast_option> ||
                                 std::is_same_v<O, ipv4_sd_endpoint_option>) {
                st2 = out.write_bytes(std::span<const std::byte>(opt.address.data(), opt.address.size()));
                if (!st2) return st2;
                st2 = wire::write_uint<wire::endian::big>(out, opt.reserved);
                if (!st2) return st2;
                st2 = wire::write_uint<wire::endian::big>(out, opt.l4_proto);
                if (!st2) return st2;
                st2 = wire::write_uint<wire::endian::big>(out, opt.port);
                if (!st2) return st2;
                return {};
            } else if constexpr (std::is_same_v<O, ipv6_endpoint_option> || std::is_same_v<O, ipv6_multicast_option> ||
                                 std::is_same_v<O, ipv6_sd_endpoint_option>) {
                st2 = out.write_bytes(std::span<const std::byte>(opt.address.data(), opt.address.size()));
                if (!st2) return st2;
                st2 = wire::write_uint<wire::endian::big>(out, opt.reserved);
                if (!st2) return st2;
                st2 = wire::write_uint<wire::endian::big>(out, opt.l4_proto);
                if (!st2) return st2;
                st2 = wire::write_uint<wire::endian::big>(out, opt.port);
                if (!st2) return st2;
                return {};
            } else if constexpr (std::is_same_v<O, unknown_option>) {
                return out.write_bytes(std::span<const std::byte>(opt.data.data(), opt.data.size()));
            } else {
                return unexpected<status_code>(status_code::error);
            }
        },
        o);
}

template <class Out>
expected<void> encode_payload(wire::writer<Out> &out, const payload &p, std::uint32_t options_len) noexcept {
    auto st = wire::write_uint<wire::endian::big>(out, p.hdr.flags);
    if (!st) return st;
    st = wire::write_u24_be(out, p.hdr.reserved24 & 0xFFFFFFu);
    if (!st) return st;

    auto entries_len = entries_wire_len(p.entries.size());
    if (!entries_len) return unexpected<status_code>(entries_len.error());
    st = wire::write_uint<wire::endian::big>(out, *entries_len);
    if (!st) return st;

    for (const auto &e : p.entries) {
        st = std::visit([&](const auto &ent) { return encode_entry(out, ent); }, e);
        if (!st) return st;
    }

    st = wire::write_uint<wire::endian::big>(out, options_len);
    if (!st) return st;
    for (const auto &o : p.options) {
        st = encode_option(out, o);
        if (!st) return st;
    }

    return {};
}

template <class Out>
expected<void> encode_payload(wire::writer<Out> &out, const payload &p) noexcept {
    auto options_len = options_wire_len(p.options);
    if (!options_len) return unexpected<status_code>(options_len.error());
    return encode_payload(out, p, *options_len);
}

[[nodiscard]] inline expected<std::vector<std::byte>> encode_message(const packet_data &pd) noexcept {
    auto built = build_payload(pd);
    if (!built) {
        return unexpected<status_code>(built.error());
    }

    auto entries_len = entries_wire_len(built->entries.size());
    if (!entries_len) return unexpected<status_code>(entries_len.error());
    auto options_len = options_wire_len(built->options);
    if (!options_len) return unexpected<status_code>(options_len.error());

    auto payload_len = add_u32(8u, *entries_len);
    if (!payload_len) return unexpected<status_code>(payload_len.error());
    payload_len = add_u32(*payload_len, 4u);
    if (!payload_len) return unexpected<status_code>(payload_len.error());
    payload_len = add_u32(*payload_len, *options_len);
    if (!payload_len) return unexpected<status_code>(payload_len.error());

    wire::header h{};
    h.msg.service_id       = kServiceId;
    h.msg.method_id        = kMethodId;
    h.req.client_id        = pd.client_id;
    h.req.session_id       = pd.session_id;
    h.protocol_version     = 1;
    h.interface_version    = 1;
    h.msg_type             = wire::message_type::notification;
    h.return_code          = 0;
    auto total_length = add_u32(8u, *payload_len);
    if (!total_length) return unexpected<status_code>(total_length.error());
    h.length               = *total_length;

    std::vector<std::byte> out{};
    wire::writer<std::vector<std::byte>> w{out};
    auto st = wire::encode_header(w, h);
    if (!st) return unexpected<status_code>(st.error());

    st = encode_payload(w, *built, *options_len);
    if (!st) return unexpected<status_code>(st.error());

    return out;
}

[[nodiscard]] inline expected<entry> decode_entry(std::span<const std::byte> bytes) noexcept {
    if (bytes.size() != 16) {
        return unexpected<status_code>(status_code::invalid_length);
    }
    wire::reader in{bytes};

    entry_common c{};
    auto         type = wire::read_uint<wire::endian::big, std::uint8_t>(in);
    if (!type) return unexpected<status_code>(type.error());
    auto idx1 = wire::read_uint<wire::endian::big, std::uint8_t>(in);
    if (!idx1) return unexpected<status_code>(idx1.error());
    auto idx2 = wire::read_uint<wire::endian::big, std::uint8_t>(in);
    if (!idx2) return unexpected<status_code>(idx2.error());
    auto num = wire::read_uint<wire::endian::big, std::uint8_t>(in);
    if (!num) return unexpected<status_code>(num.error());
    auto sid = wire::read_uint<wire::endian::big, std::uint16_t>(in);
    if (!sid) return unexpected<status_code>(sid.error());
    auto iid = wire::read_uint<wire::endian::big, std::uint16_t>(in);
    if (!iid) return unexpected<status_code>(iid.error());
    auto maj = wire::read_uint<wire::endian::big, std::uint8_t>(in);
    if (!maj) return unexpected<status_code>(maj.error());
    auto ttl = wire::read_u24_be(in);
    if (!ttl) return unexpected<status_code>(ttl.error());

    c.type           = *type;
    c.index1         = *idx1;
    c.index2         = *idx2;
    c.numopt1_numopt2 = *num;
    c.service_id     = *sid;
    c.instance_id    = *iid;
    c.major_version  = *maj;
    c.ttl            = *ttl;

    const bool is_service = (c.type == entry_type::find_service) || (c.type == entry_type::offer_service);
    const bool is_eventgroup = (c.type == entry_type::subscribe_eventgroup) || (c.type == entry_type::subscribe_eventgroup_ack);
    if (!is_service && !is_eventgroup) {
        return unexpected<status_code>(status_code::sd_invalid_header);
    }

    if (is_service) {
        auto minor = wire::read_uint<wire::endian::big, std::uint32_t>(in);
        if (!minor) return unexpected<status_code>(minor.error());
        service_entry se{};
        se.c           = c;
        se.minor_version = *minor;
        return entry{se};
    }

    auto rsv_counter = wire::read_uint<wire::endian::big, std::uint16_t>(in);
    if (!rsv_counter) return unexpected<status_code>(rsv_counter.error());
    auto egid = wire::read_uint<wire::endian::big, std::uint16_t>(in);
    if (!egid) return unexpected<status_code>(egid.error());
    eventgroup_entry eg{};
    eg.c                 = c;
    eg.reserved12_counter4 = *rsv_counter;
    eg.eventgroup_id     = *egid;
    return entry{eg};
}

[[nodiscard]] inline expected<option> decode_option(wire::reader &in) noexcept {
    auto len = wire::read_uint<wire::endian::big, std::uint16_t>(in);
    if (!len) return unexpected<status_code>(len.error());
    auto type = wire::read_uint<wire::endian::big, std::uint8_t>(in);
    if (!type) return unexpected<status_code>(type.error());

    if (*len == 0) {
        return unexpected<status_code>(status_code::invalid_length);
    }

    auto rest = in.read_bytes(*len);
    if (!rest) return unexpected<status_code>(rest.error());
    const auto rest_bytes = *rest;

    if (rest_bytes.empty()) {
        return unexpected<status_code>(status_code::invalid_length);
    }

    const auto discard = (std::to_integer<std::uint8_t>(rest_bytes[0]) & 0x80u) != 0u;
    const auto payload_bytes = rest_bytes.subspan(1);

    switch (*type) {
    case option_type::configuration: {
        configuration_option o{};
        o.discardable = discard;
        o.bytes.assign(payload_bytes.begin(), payload_bytes.end());
        return option{o};
    }
    case option_type::load_balancing: {
        if (payload_bytes.size() != 4) {
            return unexpected<status_code>(status_code::invalid_length);
        }
        wire::reader tmp{payload_bytes};
        auto prio = wire::read_uint<wire::endian::big, std::uint16_t>(tmp);
        if (!prio) return unexpected<status_code>(prio.error());
        auto weight = wire::read_uint<wire::endian::big, std::uint16_t>(tmp);
        if (!weight) return unexpected<status_code>(weight.error());
        load_balancing_option o{};
        o.discardable = discard;
        o.priority    = *prio;
        o.weight      = *weight;
        return option{o};
    }
    case option_type::ipv4_endpoint:
    case option_type::ipv4_multicast:
    case option_type::ipv4_sd_endpoint: {
        if (*len != 0x0009u || payload_bytes.size() != 8) {
            return unexpected<status_code>(status_code::invalid_length);
        }
        ipv4_endpoint_option o{};
        o.discardable = discard;
        std::memcpy(o.address.data(), payload_bytes.data(), 4);
        o.reserved = std::to_integer<std::uint8_t>(payload_bytes[4]);
        o.l4_proto = std::to_integer<std::uint8_t>(payload_bytes[5]);
        o.port     = (std::uint16_t(std::to_integer<std::uint8_t>(payload_bytes[6])) << 8) | std::uint16_t(std::to_integer<std::uint8_t>(payload_bytes[7]));

        if (*type == option_type::ipv4_endpoint) return option{o};
        if (*type == option_type::ipv4_multicast) return option{ipv4_multicast_option{o}};
        return option{ipv4_sd_endpoint_option{o}};
    }
    case option_type::ipv6_endpoint:
    case option_type::ipv6_multicast:
    case option_type::ipv6_sd_endpoint: {
        if (*len != 0x0015u || payload_bytes.size() != 20) {
            return unexpected<status_code>(status_code::invalid_length);
        }
        ipv6_endpoint_option o{};
        o.discardable = discard;
        std::memcpy(o.address.data(), payload_bytes.data(), 16);
        o.reserved = std::to_integer<std::uint8_t>(payload_bytes[16]);
        o.l4_proto = std::to_integer<std::uint8_t>(payload_bytes[17]);
        o.port     = (std::uint16_t(std::to_integer<std::uint8_t>(payload_bytes[18])) << 8) | std::uint16_t(std::to_integer<std::uint8_t>(payload_bytes[19]));

        if (*type == option_type::ipv6_endpoint) return option{o};
        if (*type == option_type::ipv6_multicast) return option{ipv6_multicast_option{o}};
        return option{ipv6_sd_endpoint_option{o}};
    }
    default: {
        unknown_option o{};
        o.type        = *type;
        o.discardable = discard;
        o.data.assign(payload_bytes.begin(), payload_bytes.end());
        return option{o};
    }
    }
}

[[nodiscard]] inline expected<payload> decode_payload(std::span<const std::byte> bytes) noexcept {
    wire::reader in{bytes};

    payload p{};
    auto flags = wire::read_uint<wire::endian::big, std::uint8_t>(in);
    if (!flags) return unexpected<status_code>(flags.error());
    auto rsv = wire::read_u24_be(in);
    if (!rsv) return unexpected<status_code>(rsv.error());
    auto entries_len = wire::read_uint<wire::endian::big, std::uint32_t>(in);
    if (!entries_len) return unexpected<status_code>(entries_len.error());

    if ((*entries_len % 16u) != 0u) {
        return unexpected<status_code>(status_code::sd_invalid_lengths);
    }
    auto entries_bytes = in.read_bytes(*entries_len);
    if (!entries_bytes) return unexpected<status_code>(entries_bytes.error());
    p.entries.reserve(static_cast<std::size_t>(*entries_len / 16u));

    for (std::size_t off = 0; off < entries_bytes->size(); off += 16u) {
        auto e = decode_entry(entries_bytes->subspan(off, 16u));
        if (!e) return unexpected<status_code>(e.error());
        p.entries.push_back(*e);
    }

    auto options_len = wire::read_uint<wire::endian::big, std::uint32_t>(in);
    if (!options_len) return unexpected<status_code>(options_len.error());

    auto options_bytes = in.read_bytes(*options_len);
    if (!options_bytes) return unexpected<status_code>(options_bytes.error());
    if (*options_len >= 3u) {
        p.options.reserve(static_cast<std::size_t>(*options_len / 3u));
    }

    wire::reader opt_reader{*options_bytes};
    while (!opt_reader.empty()) {
        auto opt = decode_option(opt_reader);
        if (!opt) return unexpected<status_code>(opt.error());
        p.options.push_back(*opt);
    }

    p.hdr.flags     = *flags;
    p.hdr.reserved24 = *rsv;

    if (!in.empty()) {
        return unexpected<status_code>(status_code::sd_invalid_lengths);
    }
    return p;
}

struct option_runs_view {
    std::span<const option> run1{};
    std::span<const option> run2{};
};

[[nodiscard]] inline expected<option_runs_view> resolve_option_runs(const payload &p, const entry_common &e) noexcept {
    const auto c1 = run1_count(e.numopt1_numopt2);
    const auto c2 = run2_count(e.numopt1_numopt2);

    if (c1 == 0 && e.index1 != 0) {
        return unexpected<status_code>(status_code::sd_invalid_lengths);
    }
    if (c2 == 0 && e.index2 != 0) {
        return unexpected<status_code>(status_code::sd_invalid_lengths);
    }

    if (c1 > 0 && (std::size_t(e.index1) + c1) > p.options.size()) {
        return unexpected<status_code>(status_code::sd_invalid_lengths);
    }
    if (c2 > 0 && (std::size_t(e.index2) + c2) > p.options.size()) {
        return unexpected<status_code>(status_code::sd_invalid_lengths);
    }

    if (c1 > 0 && c2 > 0) {
        const auto run1_start = std::size_t(e.index1);
        const auto run1_end   = run1_start + c1;
        const auto run2_start = std::size_t(e.index2);
        const auto run2_end   = run2_start + c2;
        const bool overlaps   = (run1_start < run2_end) && (run2_start < run1_end);
        if (overlaps) {
            return unexpected<status_code>(status_code::sd_invalid_lengths);
        }
    }

    option_runs_view v{};
    if (c1 > 0) {
        v.run1 = std::span<const option>(p.options.data() + e.index1, c1);
    }
    if (c2 > 0) {
        v.run2 = std::span<const option>(p.options.data() + e.index2, c2);
    }
    return v;
}

struct decoded_message {
    wire::header header{};
    payload      sd_payload{};
};

[[nodiscard]] inline expected<decoded_message> decode_message(std::span<const std::byte> frame) noexcept {
    auto parsed = wire::try_parse_frame(frame);
    if (!parsed) {
        return unexpected<status_code>(parsed.error());
    }
    if (parsed->hdr.msg.service_id != kServiceId || parsed->hdr.msg.method_id != kMethodId) {
        return unexpected<status_code>(status_code::sd_invalid_header);
    }
    if (parsed->hdr.interface_version != 1 || parsed->hdr.msg_type != wire::message_type::notification) {
        return unexpected<status_code>(status_code::sd_invalid_header);
    }
    auto p = decode_payload(parsed->payload);
    if (!p) {
        return unexpected<status_code>(p.error());
    }
    decoded_message m{};
    m.header  = parsed->hdr;
    m.sd_payload = *p;
    return m;
}

} // namespace someip::sd
