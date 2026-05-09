#pragma once

#include "cbor_tags/cbor.h"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <ranges>
#include <type_traits>

namespace cbor::tags::detail {

template <typename Byte> constexpr std::uint8_t cbor_byte_to_u8(Byte value) {
    if constexpr (std::same_as<std::remove_cvref_t<Byte>, std::byte>) {
        return std::to_integer<std::uint8_t>(value);
    } else {
        return static_cast<std::uint8_t>(value);
    }
}

[[nodiscard]] constexpr bool is_cbor_break_byte(std::uint8_t value) noexcept { return value == 0xFFU; }

[[nodiscard]] constexpr bool is_reserved_simple_argument(std::uint8_t additional_info) noexcept {
    return additional_info == 28U || additional_info == 29U || additional_info == 30U || additional_info == 31U;
}

struct cbor_item_header {
    major_type    major{};
    std::uint8_t  additional_info{};
    std::uint64_t argument{};
};

struct cbor_item_frame {
    bool          indefinite{};
    major_type    major{};
    std::uint64_t remaining{};
    bool          map_expects_value{};
};

template <typename ReadByte>
bool read_cbor_argument(std::uint8_t additional_info, std::uint64_t &value, status_code &status, ReadByte &&read_byte) {
    if (additional_info < 24U) {
        value = additional_info;
        return true;
    }
    if (additional_info > 27U) {
        status = status_code::error;
        return false;
    }

    const auto byte_count = static_cast<std::uint8_t>(1U << (additional_info - 24U));
    value                 = 0;
    for (std::uint8_t index = 0; index < byte_count; ++index) {
        std::uint8_t byte{};
        if (!read_byte(byte)) {
            return false;
        }
        value = (value << 8U) | byte;
    }
    return true;
}

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
    Iterator      payload_begin{};
};

template <std::size_t MaxDepth, typename Iterator> class cbor_item_walker {
  public:
    cbor_item_walker() = default;
    constexpr cbor_item_walker(Iterator begin, Iterator end) : cursor_(begin), end_(end) {}

    [[nodiscard]] constexpr Iterator    end() const { return end_; }
    [[nodiscard]] constexpr status_code status() const noexcept { return status_; }
    [[nodiscard]] constexpr bool        failed() const noexcept { return status_ != status_code::success; }
    [[nodiscard]] constexpr bool        done() const noexcept { return done_; }
    [[nodiscard]] constexpr std::size_t stack_depth() const noexcept { return stack_size_; }

    constexpr bool next_tag(cbor_tag_event<Iterator> &event) {
        if (done_) {
            return false;
        }

        while (true) {
            pop_completed_frames();

            if (cursor_ == end_) {
                if (stack_empty()) {
                    done_   = true;
                    status_ = status_code::success;
                } else {
                    fail(status_code::incomplete);
                }
                return false;
            }

            if (consume_indefinite_break_if_present()) {
                if (done_) {
                    return false;
                }
                continue;
            }

            if (is_cbor_break_byte(cbor_byte_to_u8(*cursor_))) {
                fail(status_code::error);
                return false;
            }

            if (!consume_parent_item()) {
                fail(status_code::error);
                return false;
            }

            cbor_item_header header{};
            auto             read_status = status_code::success;
            if (!cbor_item_reader::read_header(cursor_, end_, header, read_status)) {
                fail(read_status);
                return false;
            }

            switch (header.major) {
            case major_type::UnsignedInteger:
            case major_type::NegativeInteger: {
                std::uint64_t ignored{};
                if (header.additional_info == 31U || !read_argument(header.additional_info, ignored)) {
                    if (!done_) {
                        fail(status_code::error);
                    }
                    return false;
                }
                break;
            }
            case major_type::ByteString:
            case major_type::TextString: {
                if (header.additional_info == 31U) {
                    if (!skip_indefinite_string(header.major)) {
                        return false;
                    }
                } else {
                    std::uint64_t length{};
                    if (!read_argument(header.additional_info, length) || !skip_bytes(length)) {
                        return false;
                    }
                }
                break;
            }
            case major_type::Array: {
                if (header.additional_info == 31U) {
                    if (!push_frame(cbor_item_frame{.indefinite = true, .major = major_type::Array})) {
                        return false;
                    }
                } else {
                    std::uint64_t length{};
                    if (!read_argument(header.additional_info, length)) {
                        return false;
                    }
                    if (length > 0U && !push_frame(cbor_item_frame{.major = major_type::Array, .remaining = length})) {
                        return false;
                    }
                }
                break;
            }
            case major_type::Map: {
                if (header.additional_info == 31U) {
                    if (!push_frame(cbor_item_frame{.indefinite = true, .major = major_type::Map})) {
                        return false;
                    }
                } else {
                    std::uint64_t length{};
                    if (!read_argument(header.additional_info, length)) {
                        return false;
                    }
                    if (length > (std::numeric_limits<std::uint64_t>::max() / 2U)) {
                        fail(status_code::error);
                        return false;
                    }
                    const auto item_count = length * 2U;
                    if (item_count > 0U && !push_frame(cbor_item_frame{.major = major_type::Map, .remaining = item_count})) {
                        return false;
                    }
                }
                break;
            }
            case major_type::Tag: {
                std::uint64_t tag{};
                if (header.additional_info == 31U || !read_argument(header.additional_info, tag)) {
                    if (!done_) {
                        fail(status_code::error);
                    }
                    return false;
                }
                auto payload_begin = cursor_;
                if (!push_frame(cbor_item_frame{.major = major_type::Tag, .remaining = 1})) {
                    return false;
                }
                event = cbor_tag_event<Iterator>{.tag = tag, .payload_begin = payload_begin};
                return true;
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
        }
    }

  private:
    constexpr void fail(status_code status) noexcept {
        status_ = status;
        done_   = true;
    }

    constexpr void pop_completed_frames() noexcept {
        while (!stack_empty() && !stack_back().indefinite && stack_back().remaining == 0) {
            stack_pop_back();
        }
    }

    constexpr bool consume_parent_item() noexcept {
        if (stack_empty()) {
            return true;
        }
        auto &frame = stack_back();
        if (frame.indefinite) {
            if (frame.major == major_type::Map) {
                frame.map_expects_value = !frame.map_expects_value;
            }
            return true;
        }
        if (frame.remaining == 0) {
            return false;
        }
        --frame.remaining;
        return true;
    }

    constexpr bool consume_indefinite_break_if_present() {
        if (stack_empty() || !stack_back().indefinite || cursor_ == end_ || !is_cbor_break_byte(cbor_byte_to_u8(*cursor_))) {
            return false;
        }
        if (stack_back().major == major_type::Map && stack_back().map_expects_value) {
            fail(status_code::error);
            return true;
        }
        ++cursor_;
        stack_pop_back();
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

    [[nodiscard]] constexpr bool  stack_empty() const noexcept { return stack_size_ == 0; }
    [[nodiscard]] constexpr auto &stack_back() noexcept { return stack_[stack_size_ - 1]; }
    constexpr void                stack_pop_back() noexcept { --stack_size_; }
    constexpr bool                push_frame(cbor_item_frame frame) {
        if (stack_size_ == stack_.size()) {
            fail(status_code::error);
            return false;
        }
        stack_[stack_size_++] = frame;
        return true;
    }

    Iterator                              cursor_{};
    Iterator                              end_{};
    std::array<cbor_item_frame, MaxDepth> stack_{};
    std::size_t                           stack_size_{};
    status_code                           status_{status_code::success};
    bool                                  done_{};
};

template <std::size_t MaxDepth = 256> struct cbor_item_skipper {
    template <typename Iterator> static bool skip_item(Iterator &cursor, Iterator end, status_code &status, std::size_t initial_depth = 0) {
        if (initial_depth > MaxDepth) {
            status = status_code::error;
            return false;
        }
        std::array<cbor_item_frame, MaxDepth> stack{};
        std::size_t                           stack_size{};
        bool                                  root_started{};

        auto stack_empty = [&] { return stack_size == 0; };
        auto stack_back  = [&]() -> cbor_item_frame  &{ return stack[stack_size - 1]; };
        auto pop_frame   = [&] { --stack_size; };
        auto push_frame  = [&](cbor_item_frame frame) {
            if (initial_depth + stack_size == stack.size()) {
                status = status_code::error;
                return false;
            }
            stack[stack_size++] = frame;
            return true;
        };

        while (true) {
            while (!stack_empty() && !stack_back().indefinite && stack_back().remaining == 0) {
                pop_frame();
            }
            if (root_started && stack_empty()) {
                return true;
            }

            if (!stack_empty() && stack_back().indefinite) {
                if (cursor == end) {
                    status = status_code::incomplete;
                    return false;
                }
                if (is_cbor_break_byte(cbor_byte_to_u8(*cursor))) {
                    if (stack_back().major == major_type::Map && stack_back().map_expects_value) {
                        status = status_code::error;
                        return false;
                    }
                    ++cursor;
                    pop_frame();
                    continue;
                }
            }

            if (cursor == end) {
                status = status_code::incomplete;
                return false;
            }
            if (is_cbor_break_byte(cbor_byte_to_u8(*cursor))) {
                status = status_code::error;
                return false;
            }

            if (stack_empty()) {
                root_started = true;
            } else if (auto &frame = stack_back(); frame.indefinite) {
                if (frame.major == major_type::Map) {
                    frame.map_expects_value = !frame.map_expects_value;
                }
            } else {
                if (frame.remaining == 0) {
                    status = status_code::error;
                    return false;
                }
                --frame.remaining;
            }

            cbor_item_header header{};
            if (!cbor_item_reader::read_header(cursor, end, header, status)) {
                return false;
            }

            switch (header.major) {
            case major_type::UnsignedInteger:
            case major_type::NegativeInteger:
                if (header.additional_info == 31U) {
                    status = status_code::error;
                    return false;
                }
                if (!cbor_item_reader::read_argument(cursor, end, header.additional_info, header.argument, status)) {
                    return false;
                }
                break;
            case major_type::ByteString:
            case major_type::TextString:
                if (header.additional_info == 31U) {
                    if (!cbor_item_reader::skip_indefinite_string(cursor, end, header.major, status)) {
                        return false;
                    }
                } else {
                    if (!cbor_item_reader::read_argument(cursor, end, header.additional_info, header.argument, status)) {
                        return false;
                    }
                    if (!cbor_item_reader::advance_bytes(cursor, end, header.argument, status)) {
                        return false;
                    }
                }
                break;
            case major_type::Array:
                if (header.additional_info == 31U) {
                    if (!push_frame(cbor_item_frame{.indefinite = true, .major = major_type::Array})) {
                        return false;
                    }
                } else {
                    if (!cbor_item_reader::read_argument(cursor, end, header.additional_info, header.argument, status)) {
                        return false;
                    }
                    if (header.argument > 0U && !push_frame(cbor_item_frame{.major = major_type::Array, .remaining = header.argument})) {
                        return false;
                    }
                }
                break;
            case major_type::Map:
                if (header.additional_info == 31U) {
                    if (!push_frame(cbor_item_frame{.indefinite = true, .major = major_type::Map})) {
                        return false;
                    }
                } else {
                    if (!cbor_item_reader::read_argument(cursor, end, header.additional_info, header.argument, status)) {
                        return false;
                    }
                    if (header.argument > (std::numeric_limits<std::uint64_t>::max() / 2U)) {
                        status = status_code::error;
                        return false;
                    }
                    const auto item_count = header.argument * 2U;
                    if (item_count > 0U && !push_frame(cbor_item_frame{.major = major_type::Map, .remaining = item_count})) {
                        return false;
                    }
                }
                break;
            case major_type::Tag:
                if (header.additional_info == 31U) {
                    status = status_code::error;
                    return false;
                }
                if (!cbor_item_reader::read_argument(cursor, end, header.additional_info, header.argument, status)) {
                    return false;
                }
                if (!push_frame(cbor_item_frame{.major = major_type::Tag, .remaining = 1})) {
                    return false;
                }
                break;
            case major_type::Simple:
                if (is_reserved_simple_argument(header.additional_info)) {
                    status = status_code::error;
                    return false;
                }
                if (header.additional_info >= 24U &&
                    !cbor_item_reader::advance_bytes(cursor, end, std::uint64_t{1U << (header.additional_info - 24U)}, status)) {
                    return false;
                }
                break;
            default: status = status_code::error; return false;
            }
        }
    }
};

} // namespace cbor::tags::detail
