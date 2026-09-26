#pragma once

#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/detail/cbor_utf8.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <new>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace cbor::tags {

enum class walk_phase { begin, value, payload, end };

struct walk_options {
    std::size_t max_depth{64};
    bool        strict_validation{false};
};

struct validation_options {
    std::size_t max_depth{64};
};

template <typename Iterator> struct walk_context {
    walk_phase                      phase;
    std::size_t                     depth;
    std::ranges::subrange<Iterator> source;
};

namespace detail {

using catch_all_variant = std::variant<positive, negative, as_text_any, as_bstr_any, as_array_any, as_map_any, as_tag_any, float16_t, float,
                                       double, bool, std::nullptr_t, simple, indefinite_break>;

// Consumers can translate structural errors without intercepting exceptions
// thrown by their presentation callbacks. This is deliberately not public API.
enum class walk_failure_reason { none, malformed, unexpected_break, invalid_chunk, depth, invalid_utf8, payload };

struct walk_failure {
    walk_failure_reason reason{walk_failure_reason::none};
    std::size_t         depth{};
};

template <typename Decoder, typename Visitor> class item_walker {
    using iterator     = decltype(std::declval<Decoder &>().tell());
    using source_range = std::ranges::subrange<iterator>;
    using context      = walk_context<iterator>;

    Decoder      &decoder_;
    Visitor      &visitor_;
    walk_options  options_;
    walk_failure *failure_;

    void mark_failure(walk_failure_reason reason, std::size_t depth) {
        if (failure_) {
            *failure_ = {.reason = reason, .depth = depth};
        }
    }

    status_code fail(walk_failure_reason reason, std::size_t depth, status_code status = status_code::error) {
        mark_failure(reason, depth);
        return status;
    }

    template <typename T> status_code emit(const T &value, walk_phase phase, std::size_t depth, source_range source) {
        mark_failure(walk_failure_reason::none, depth);
        static_assert(std::is_invocable_v<Visitor &, const T &, const context &>,
                      "walk_item visitor must accept each value and const walk_context&");
        if constexpr (std::is_invocable_v<Visitor &, const T &, const context &>) {
            using result_type = std::invoke_result_t<Visitor &, const T &, const context &>;
            static_assert(std::same_as<result_type, void> || std::same_as<result_type, status_code>,
                          "walk_item visitor must return exactly void or status_code");
            const context event{.phase = phase, .depth = depth, .source = std::move(source)};
            if constexpr (std::same_as<result_type, status_code>) {
                return std::invoke(visitor_, value, event);
            } else if constexpr (std::same_as<result_type, void>) {
                std::invoke(visitor_, value, event);
            }
        }
        return status_code::success;
    }

    status_code read_token(catch_all_variant &value, std::size_t depth) {
        mark_failure(walk_failure_reason::malformed, depth);
        const auto [major, additional_info] = decoder_.read_initial_byte();
        const auto status                   = decoder_.decode(value, major, additional_info);
        if (status != status_code::success) {
            switch (status) {
            case status_code::incomplete:
            case status_code::out_of_memory:
            case status_code::size_limit_exceeded:
            case status_code::invalid_utf8_sequence: return status;
            default: return status_code::error;
            }
        }
        // Keep the initial additional-info value: simple{n} alone cannot tell
        // whether a small simple value used the forbidden two-byte form.
        if (options_.strict_validation && major == major_type::Simple && additional_info == std::byte{24}) {
            if (const auto *value_ptr = std::get_if<simple>(&value); value_ptr && value_ptr->value < 32U) {
                return status_code::error;
            }
        }
        return status_code::success;
    }

    template <typename Header> status_code string_payload(const Header &header, std::size_t depth) {
        const auto begin = decoder_.tell();
        mark_failure(walk_failure_reason::payload, depth);
        if constexpr (IsTextHeader<Header>) {
            const auto payload = decoder_.decode_text_payload(header.size);
            if (options_.strict_validation && !is_valid_utf8(payload)) {
                return fail(walk_failure_reason::invalid_utf8, depth, status_code::invalid_utf8_sequence);
            }
            return emit(payload, walk_phase::payload, depth, {begin, decoder_.tell()});
        } else {
            const auto payload = decoder_.decode_bstring_payload(header.size);
            return emit(payload, walk_phase::payload, depth, {begin, decoder_.tell()});
        }
    }

    template <typename Header> status_code end_item(const Header &header, std::size_t depth) {
        const auto cursor = decoder_.tell();
        return emit(header, walk_phase::end, depth, {cursor, cursor});
    }

    status_code read_item(std::size_t depth) {
        const auto        begin = decoder_.tell();
        catch_all_variant value;
        const auto        status = read_token(value, depth);
        return status == status_code::success ? start_item(value, depth, {begin, decoder_.tell()}) : status;
    }

    template <typename Header> status_code sequence_items(const Header &header, std::size_t depth) {
        auto remaining = header.size;
        while (header.indefinite || remaining != 0U) {
            const auto        begin = decoder_.tell();
            catch_all_variant value;
            auto              status = read_token(value, depth + 1U);
            if (status != status_code::success) {
                return status;
            }
            const source_range source{begin, decoder_.tell()};
            if (std::holds_alternative<indefinite_break>(value)) {
                if (!header.indefinite) {
                    return fail(walk_failure_reason::unexpected_break, depth + 1U);
                }
                return emit(header, walk_phase::end, depth, source);
            }
            if constexpr (IsTextHeader<Header> || IsBinaryHeader<Header>) {
                const auto *chunk = std::get_if<Header>(&value);
                if (!chunk || chunk->indefinite) {
                    return fail(walk_failure_reason::invalid_chunk, depth + 1U);
                }
            }
            status = start_item(value, depth + 1U, source);
            if (status != status_code::success) {
                return status;
            }
            if constexpr (IsMapHeader<Header>) {
                // A break is permitted only before the key. Count complete
                // pairs, avoiding overflow from doubling a declared size.
                status = read_item(depth + 1U);
                if (status != status_code::success) {
                    return status;
                }
            }
            if (!header.indefinite) {
                --remaining;
            }
        }
        return end_item(header, depth);
    }

    status_code start_item(const catch_all_variant &value, std::size_t depth, source_range source) {
        return std::visit(
            [&](const auto &item) -> status_code {
                using T = std::remove_cvref_t<decltype(item)>;
                if constexpr (std::same_as<T, indefinite_break>) {
                    return fail(walk_failure_reason::unexpected_break, depth);
                } else if constexpr (IsTextHeader<T> || IsBinaryHeader<T> || IsArrayHeader<T> || IsMapHeader<T> || IsTagHeader<T>) {
                    if (depth >= options_.max_depth) {
                        return fail(walk_failure_reason::depth, depth, status_code::size_limit_exceeded);
                    }
                    auto status = emit(item, walk_phase::begin, depth, source);
                    if (status != status_code::success) {
                        return status;
                    }
                    if constexpr (IsTextHeader<T> || IsBinaryHeader<T>) {
                        if (!item.indefinite) {
                            status = string_payload(item, depth);
                            return status == status_code::success ? end_item(item, depth) : status;
                        }
                    }
                    if constexpr (IsTagHeader<T>) {
                        status = read_item(depth + 1U);
                        return status == status_code::success ? end_item(item, depth) : status;
                    } else {
                        return sequence_items(item, depth);
                    }
                } else {
                    return emit(item, walk_phase::value, depth, source);
                }
            },
            value);
    }

  public:
    item_walker(Decoder &decoder, Visitor &visitor, walk_options options, walk_failure *failure)
        : decoder_(decoder), visitor_(visitor), options_(options), failure_(failure) {}

    status_code walk() { return read_item(0U); }

    status_code walk_decoded(const catch_all_variant &value, source_range source) { return start_item(value, 0U, source); }
};

template <typename Decoder, typename Visitor>
status_code walk_item_status(Decoder &decoder, Visitor &&visitor, walk_options options = {}, walk_failure *failure = nullptr) {
    item_walker<Decoder, std::remove_reference_t<Visitor>> walker{decoder, visitor, options, failure};
    return walker.walk();
}

// Header-only diagnostic visitors have already consumed the root token. Their
// child events retain exact source ranges; the unavailable root range is empty.
template <typename Decoder, typename T, typename Visitor>
status_code walk_decoded_item(Decoder &decoder, const T &value, Visitor &&visitor, walk_options options = {},
                              walk_failure *failure = nullptr) {
    item_walker<Decoder, std::remove_reference_t<Visitor>> walker{decoder, visitor, options, failure};
    const auto                                             cursor = decoder.tell();
    return walker.walk_decoded(catch_all_variant{value}, {cursor, cursor});
}

} // namespace detail

// Walk exactly one root item. Input and payloads remain borrowed; failure is
// terminal and does not restore the decoder cursor or undo callback effects.
template <typename Decoder, typename Visitor>
expected<void, status_code> walk_item(Decoder &decoder, Visitor &&visitor, walk_options options = {}) noexcept {
    try {
        const auto status = detail::walk_item_status(decoder, std::forward<Visitor>(visitor), options);
        if (status != status_code::success) {
            return unexpected<status_code>(status);
        }
        return {};
    } catch (const std::bad_alloc &) { return unexpected<status_code>(status_code::out_of_memory); } catch (const std::length_error &) {
        return unexpected<status_code>(status_code::out_of_memory);
    } catch (const parse_incomplete_exception &) { return unexpected<status_code>(status_code::incomplete); } catch (...) {
        return unexpected<status_code>(status_code::error);
    }
}

template <typename Decoder> expected<void, status_code> validate_item(Decoder &decoder, validation_options options = {}) noexcept {
    return walk_item(decoder, [](const auto &, const auto &) {}, {.max_depth = options.max_depth, .strict_validation = true});
}

} // namespace cbor::tags
