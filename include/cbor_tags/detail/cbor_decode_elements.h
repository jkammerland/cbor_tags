#pragma once

#include "cbor_tags/cbor_detail.h"

#include <cstdint>
#include <optional>
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
// Mutable count/break references cannot be swapped; bounds follow min/max order.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
template <bool CheckBounds = false, typename Decoder, typename Container, typename Appender, typename Completed = ignore_decoded_element,
          typename Available = decode_elements_available>
constexpr status_code decode_indefinite_array_elements(Decoder &decoder, Container &value, Appender &append, std::uint64_t &count,
                                                       bool &ended, std::uint64_t minimum, std::uint64_t maximum, Completed completed = {},
                                                       Available available = {}) {
    // NOLINTEND(bugprone-easily-swappable-parameters)
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

template <typename Container> using decoded_map_pair = std::pair<typename Container::key_type, typename Container::mapped_type>;

// Keep the key when only the mapped value is incomplete. The same loop serves
// ordinary decoding with no-op callbacks and resumable decoding with checkpoints.
// Progress references are grouped by purpose; bounds follow min/max order.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
template <bool Indefinite, bool CheckBounds = false, typename Decoder, typename Container, typename Appender,
          typename Completed = ignore_decoded_element, typename Available = decode_elements_available>
constexpr status_code decode_map_elements(Decoder &decoder, Container &value, Appender &append,
                                          std::optional<decoded_map_pair<Container>> &entry, bool &key_complete, std::uint64_t &count,
                                          bool &ended, std::uint64_t minimum, std::uint64_t maximum, Completed completed = {},
                                          Available available = {}) {
    // NOLINTEND(bugprone-easily-swappable-parameters)
    while ((Indefinite || count != maximum) && available()) {
        if (!key_complete) {
            major_type major{};
            std::byte  info{};
            if constexpr (Indefinite) {
                std::tie(major, info) = decoder.read_initial_byte();
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
            }

            if (!entry) {
                if constexpr (can_propagate_container_allocator_for<typename Container::key_type, Container>() ||
                              can_propagate_container_allocator_for<typename Container::mapped_type, Container>()) {
                    auto key    = make_decode_value_for<typename Container::key_type>(value);
                    auto mapped = make_decode_value_for<typename Container::mapped_type>(value);
                    entry.emplace(std::move(key), std::move(mapped));
                } else {
                    // Ordinary maps construct directly in place without temporary
                    // key/value moves; only allocator propagation needs temporaries.
                    entry.emplace();
                }
            }
            const auto status = [&] {
                if constexpr (Indefinite)
                    return decoder.decode(entry->first, major, info);
                else
                    return decoder.decode(entry->first);
            }();
            if (status != status_code::success)
                return status;
            key_complete = true;
            completed();
        }

        const auto status = [&] {
            if constexpr (Indefinite) {
                const auto [major, info] = decoder.read_initial_byte();
                if (major == major_type::Simple && info == std::byte{31})
                    return status_code::malformed_structure;
                return decoder.decode(entry->second, major, info);
            } else
                return decoder.decode(entry->second);
        }();
        if (status != status_code::success)
            return status;

        append(value, std::move(*entry));
        entry.reset();
        key_complete = false;
        if constexpr (!Indefinite || CheckBounds)
            ++count;
        completed();
    }
    return status_code::success;
}

} // namespace cbor::tags::detail
