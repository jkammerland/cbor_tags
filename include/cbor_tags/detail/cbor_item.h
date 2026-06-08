#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/detail/cbor_argument.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <ranges>
#include <type_traits>

namespace cbor::tags::detail {

struct cbor_item_header {
    major_type    major{};
    std::uint8_t  additional_info{};
    std::uint64_t argument{};
};

struct cbor_item_frame {
    major_type    major{};
    bool          map_expects_value{};
    std::uint64_t parent_pending{};
};

struct cbor_item_reader {
    template <typename Iterator> static bool read_byte(Iterator &cursor, Iterator end, std::uint8_t &value, status_code &status) {
        if (cursor == end) {
            status = status_code::incomplete;
            return false;
        }
        value = cbor_byte_to_u8(*cursor);
        ++cursor;
        return true;
    }

    template <typename Iterator> static bool advance_bytes(Iterator &cursor, Iterator end, std::uint64_t length, status_code &status) {
        if constexpr (std::random_access_iterator<Iterator>) {
            const auto remaining = end - cursor;
            if (remaining < 0) {
                status = status_code::error;
                return false;
            }
            if (length > static_cast<std::uint64_t>(remaining)) {
                status = status_code::incomplete;
                return false;
            }
            cursor += static_cast<std::iter_difference_t<Iterator>>(length);
            return true;
        }

        for (std::uint64_t index = 0; index < length; ++index) {
            if (cursor == end) {
                status = status_code::incomplete;
                return false;
            }
            ++cursor;
        }
        return true;
    }

    template <typename Iterator>
    static bool read_argument(Iterator &cursor, Iterator end, std::uint8_t additional_info, std::uint64_t &value, status_code &status) {
        return read_cbor_argument(additional_info, value, status,
                                  [&cursor, end, &status](std::uint8_t &byte) { return read_byte(cursor, end, byte, status); });
    }

    template <typename Iterator> static bool read_header(Iterator &cursor, Iterator end, cbor_item_header &header, status_code &status) {
        std::uint8_t initial{};
        if (!read_byte(cursor, end, initial, status)) {
            return false;
        }
        header.major           = static_cast<major_type>(initial >> 5U);
        header.additional_info = static_cast<std::uint8_t>(initial & 0x1FU);
        header.argument        = 0;
        return true;
    }

    template <typename Iterator>
    static bool skip_indefinite_string(Iterator &cursor, Iterator end, major_type expected_major, status_code &status) {
        while (true) {
            if (cursor == end) {
                status = status_code::incomplete;
                return false;
            }
            if (is_cbor_break_byte(cbor_byte_to_u8(*cursor))) {
                ++cursor;
                return true;
            }

            cbor_item_header chunk{};
            if (!read_header(cursor, end, chunk, status)) {
                return false;
            }
            if (chunk.major != expected_major || chunk.additional_info == 31U) {
                status = status_code::error;
                return false;
            }
            if (!read_argument(cursor, end, chunk.additional_info, chunk.argument, status)) {
                return false;
            }
            if (!advance_bytes(cursor, end, chunk.argument, status)) {
                return false;
            }
        }
    }
};

template <typename Iterator> struct cbor_tag_event {
    std::uint64_t tag{};
    Iterator      tag_begin{};
    Iterator      payload_begin{};
    Iterator      payload_end{};
    Iterator      item_end{};
};

template <std::size_t MaxDepth, typename Iterator> class cbor_structural_cursor {
  public:
    cbor_structural_cursor() = default;
    constexpr cbor_structural_cursor(Iterator begin, Iterator end) : cursor_(begin), end_(end) {}

    [[nodiscard]] constexpr Iterator    end() const { return end_; }
    [[nodiscard]] constexpr status_code status() const noexcept { return status_; }
    [[nodiscard]] constexpr bool        failed() const noexcept { return status_ != status_code::success; }
    [[nodiscard]] constexpr bool        done() const noexcept { return done_; }
    [[nodiscard]] constexpr Iterator    position() const { return cursor_; }

    constexpr bool skip_one(Iterator &item_end) {
        pending_    = 1;
        stack_size_ = 0;
        status_     = status_code::success;
        done_       = false;

        while (true) {
            auto has_item = false;
            if (!prepare_item(false, has_item)) {
                return false;
            }
            if (!has_item) {
                item_end = cursor_;
                return true;
            }
            event_sink sink{};
            auto       ignored = false;
            if (!consume_item(static_cast<no_predicate *>(nullptr), sink, ignored)) {
                return false;
            }
        }
    }

    template <typename Predicate> constexpr bool next_matching_tag(Predicate &predicate, cbor_tag_event<Iterator> &event) {
        if (done_) {
            return false;
        }

        while (true) {
            auto has_item = false;
            if (!prepare_item(true, has_item)) {
                return false;
            }
            if (!has_item) {
                return false;
            }
            auto emitted = false;
            if (!consume_item(&predicate, event, emitted)) {
                return false;
            }
            if (emitted) {
                return true;
            }
        }
    }

  private:
    struct event_sink {};
    struct no_predicate {};

    constexpr void fail(status_code status) noexcept {
        status_ = status;
        done_   = true;
    }

    constexpr bool prepare_item(bool sequence, bool &has_item) {
        while (pending_ == 0U) {
            if (stack_empty()) {
                if (sequence && cursor_ != end_) {
                    pending_ = 1;
                    has_item = true;
                    return true;
                }
                done_    = true;
                has_item = false;
                return true;
            }

            if (cursor_ == end_) {
                fail(status_code::incomplete);
                return false;
            }

            auto &frame = stack_back();
            if (is_cbor_break_byte(cbor_byte_to_u8(*cursor_))) {
                if (frame.major == major_type::Map && frame.map_expects_value) {
                    fail(status_code::error);
                    return false;
                }
                ++cursor_;
                pending_ = frame.parent_pending;
                stack_pop_back();
                continue;
            }

            if (frame.major == major_type::Map) {
                frame.map_expects_value = !frame.map_expects_value;
            }
            pending_ = 1;
            has_item = true;
            return true;
        }

        has_item = true;
        return true;
    }

    constexpr bool add_pending(std::uint64_t count) {
        if (count > std::numeric_limits<std::uint64_t>::max() - pending_) {
            fail(status_code::error);
            return false;
        }
        pending_ += count;
        return true;
    }

    constexpr bool enter_indefinite(major_type major) {
        if (stack_size_ == stack_.size()) {
            fail(status_code::error);
            return false;
        }
        stack_[stack_size_++] = cbor_item_frame{.major = major, .parent_pending = pending_};
        pending_              = 0;
        return true;
    }

    constexpr bool read_argument(std::uint8_t additional_info, std::uint64_t &value) {
        auto read_status = status_code::success;
        if (!cbor_item_reader::read_argument(cursor_, end_, additional_info, value, read_status)) {
            fail(read_status);
            return false;
        }
        return true;
    }

    constexpr bool skip_indefinite_string(major_type major) {
        auto read_status = status_code::success;
        if (!cbor_item_reader::skip_indefinite_string(cursor_, end_, major, read_status)) {
            fail(read_status);
            return false;
        }
        return true;
    }

    constexpr bool skip_bytes(std::uint64_t length) {
        auto read_status = status_code::success;
        if (!cbor_item_reader::advance_bytes(cursor_, end_, length, read_status)) {
            fail(read_status);
            return false;
        }
        return true;
    }

    template <typename Predicate, typename Event> constexpr bool consume_item(Predicate *predicate, Event &event, bool &emitted_tag) {
        emitted_tag = false;
        if (pending_ == 0U) {
            fail(status_code::error);
            return false;
        }
        --pending_;

        if (cursor_ == end_) {
            fail(status_code::incomplete);
            return false;
        }
        if (is_cbor_break_byte(cbor_byte_to_u8(*cursor_))) {
            fail(status_code::error);
            return false;
        }

        const auto       item_begin = cursor_;
        cbor_item_header header{};
        auto             read_status = status_code::success;
        if (!cbor_item_reader::read_header(cursor_, end_, header, read_status)) {
            fail(read_status);
            return false;
        }

        switch (header.major) {
        case major_type::UnsignedInteger:
        case major_type::NegativeInteger:
            if (header.additional_info == 31U || !read_argument(header.additional_info, header.argument)) {
                if (!done_) {
                    fail(status_code::error);
                }
                return false;
            }
            break;
        case major_type::ByteString:
        case major_type::TextString:
            if (header.additional_info == 31U) {
                if (!skip_indefinite_string(header.major)) {
                    return false;
                }
            } else {
                if (!read_argument(header.additional_info, header.argument) || !skip_bytes(header.argument)) {
                    return false;
                }
            }
            break;
        case major_type::Array:
            if (header.additional_info == 31U) {
                if (!enter_indefinite(major_type::Array)) {
                    return false;
                }
            } else if (!read_argument(header.additional_info, header.argument) || !add_pending(header.argument)) {
                return false;
            }
            break;
        case major_type::Map:
            if (header.additional_info == 31U) {
                if (!enter_indefinite(major_type::Map)) {
                    return false;
                }
            } else {
                if (!read_argument(header.additional_info, header.argument)) {
                    return false;
                }
                if (header.argument > (std::numeric_limits<std::uint64_t>::max() / 2U)) {
                    fail(status_code::error);
                    return false;
                }
                if (!add_pending(header.argument * 2U)) {
                    return false;
                }
            }
            break;
        case major_type::Tag: {
            if (header.additional_info == 31U || !read_argument(header.additional_info, header.argument) || !add_pending(1)) {
                if (!done_) {
                    fail(status_code::error);
                }
                return false;
            }
            const auto payload_begin = cursor_;
            if constexpr (!std::is_same_v<Event, event_sink>) {
                if (predicate != nullptr && std::invoke(*predicate, header.argument)) {
                    cbor_structural_cursor<MaxDepth, Iterator> payload_cursor{payload_begin, end_};
                    auto                                       payload_end = payload_begin;
                    if (!payload_cursor.skip_one(payload_end)) {
                        fail(payload_cursor.status());
                        return false;
                    }
                    event       = cbor_tag_event<Iterator>{.tag           = header.argument,
                                                           .tag_begin     = item_begin,
                                                           .payload_begin = payload_begin,
                                                           .payload_end   = payload_end,
                                                           .item_end      = payload_end};
                    emitted_tag = true;
                }
            }
            break;
        }
        case major_type::Simple:
            if (is_reserved_simple_argument(header.additional_info)) {
                fail(status_code::error);
                return false;
            }
            if (header.additional_info >= 24U && !skip_bytes(std::uint64_t{1U << (header.additional_info - 24U)})) {
                return false;
            }
            break;
        default: fail(status_code::error); return false;
        }
        return true;
    }

    [[nodiscard]] constexpr bool  stack_empty() const noexcept { return stack_size_ == 0; }
    [[nodiscard]] constexpr auto &stack_back() noexcept { return stack_[stack_size_ - 1]; }
    constexpr void                stack_pop_back() noexcept { --stack_size_; }

    Iterator                              cursor_{};
    Iterator                              end_{};
    std::array<cbor_item_frame, MaxDepth> stack_{};
    std::size_t                           stack_size_{};
    std::uint64_t                         pending_{};
    status_code                           status_{status_code::success};
    bool                                  done_{};
};

template <std::size_t MaxDepth = 256> struct cbor_item_skipper {
    template <typename Iterator> static bool skip_item(Iterator &cursor, Iterator end, status_code &status) {
        cbor_structural_cursor<MaxDepth, Iterator> structural_cursor{cursor, end};
        auto                                       item_end = cursor;
        if (!structural_cursor.skip_one(item_end)) {
            status = structural_cursor.status();
            return false;
        }
        cursor = item_end;
        status = status_code::success;
        return true;
    }
};

} // namespace cbor::tags::detail
