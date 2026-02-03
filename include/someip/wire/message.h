#pragma once

#include "someip/ser/encode.h"
#include "someip/status.h"
#include "someip/wire/someip.h"
#include "someip/wire/tp.h"

#include <cstddef>
#include <optional>

namespace someip::wire {

template <class Out, class Payload>
expected<void> encode_message(Out &out, const header &h_in, const ser::config &cfg, const Payload &payload,
                              std::optional<tp_header> tp = std::nullopt) noexcept {
    const auto tp_bytes         = tp ? 4u : 0u;
    const auto payload_base_off = static_cast<std::size_t>(16u + tp_bytes);

    auto payload_size = ser::measure(cfg, payload, payload_base_off);
    if (!payload_size) {
        return unexpected<status_code>(payload_size.error());
    }

    header h = h_in;
    h.length = static_cast<std::uint32_t>(8u + tp_bytes + *payload_size);

    wire::writer<Out> w{out};
    auto              st = encode_header(w, h);
    if (!st) return st;

    if (tp) {
        st = encode_tp_header(w, *tp);
        if (!st) return st;
    }

    return ser::encode(out, cfg, payload, 0);
}

} // namespace someip::wire

