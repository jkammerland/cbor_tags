#pragma once

#include "cbor_tags/cbor_detail.h"

#include <cstdint>
#include <utility>

namespace cbor::tags::detail {

struct ignore_decoded_element {
    constexpr void operator()() const noexcept {}
};

struct decode_elements_available {
    constexpr bool operator()() const noexcept { return true; }
};

// The caller owns count and append progress. Ordinary decoding uses the empty
// completion callback; resumable decoding retains only completed child items.
template <typename Decoder, typename Container, typename Appender, typename Completed = ignore_decoded_element,
          typename Available = decode_elements_available>
constexpr status_code decode_array_elements(Decoder &decoder, Container &value, Appender &append, std::uint64_t &remaining,
                                            Completed completed = {}, Available available = {}) {
    while (remaining != 0 && available()) {
        auto       decoded = make_decode_value_for<typename Container::value_type>(value);
        const auto status  = decoder.decode(decoded);
        if (status != status_code::success)
            return status;

        append(value, std::move(decoded));
        --remaining;
        completed();
    }
    return status_code::success;
}

// A consumed break is distinct from a temporarily exhausted input window.
// Header-context dispatch matches the ordinary indefinite container decoder.
template <bool CheckBounds = false, typename Decoder, typename Container, typename Appender, typename Completed = ignore_decoded_element,
          typename Available = decode_elements_available>
constexpr status_code decode_indefinite_array_elements(Decoder &decoder, Container &value, Appender &append, std::uint64_t &count,
                                                       bool &ended, std::uint64_t minimum, std::uint64_t maximum, Completed completed = {},
                                                       Available available = {}) {
    while (available()) {
        const auto [major, info] = decoder.read_initial_byte();
        if (major == major_type::Simple && info == std::byte{31}) {
            ended = true;
            completed();
            if constexpr (CheckBounds) {
                if (count < minimum)
                    return status_code::size_limit_exceeded;
            }
            return status_code::success;
        }
        if constexpr (CheckBounds) {
            if (count == maximum)
                return status_code::size_limit_exceeded;
        }

        auto       decoded = make_decode_value_for<typename Container::value_type>(value);
        const auto status  = decoder.decode(decoded, major, info);
        if (status != status_code::success)
            return status;

        append(value, std::move(decoded));
        if constexpr (CheckBounds)
            ++count;
        completed();
    }
    return status_code::success;
}

} // namespace cbor::tags::detail
