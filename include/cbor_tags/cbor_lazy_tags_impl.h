#pragma once

#include "cbor_tags/cbor_decoder.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>

namespace cbor::tags {

namespace detail {

template <typename Byte> constexpr std::uint8_t scanner_byte_to_u8(Byte value) {
    if constexpr (std::same_as<std::remove_cvref_t<Byte>, std::byte>) {
        return std::to_integer<std::uint8_t>(value);
    } else {
        return static_cast<std::uint8_t>(value);
    }
}

struct tag_scan_frame {
    bool          indefinite{};
    major_type    major{};
    std::uint64_t remaining{};
    bool          map_expects_value{};
};

struct cbor_item_skipper {
    template <typename Iterator> static bool read_byte(Iterator &cursor, Iterator end, std::uint8_t &value, status_code &status) {
        if (cursor == end) {
            status = status_code::incomplete;
            return false;
        }
        value = scanner_byte_to_u8(*cursor);
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
        if (additional_info < 24) {
            value = additional_info;
            return true;
        }
        if (additional_info > 27) {
            status = status_code::error;
            return false;
        }

        const auto byte_count = static_cast<std::uint8_t>(1U << (additional_info - 24U));
        value                 = 0;
        for (std::uint8_t index = 0; index < byte_count; ++index) {
            std::uint8_t byte{};
            if (!read_byte(cursor, end, byte, status)) {
                return false;
            }
            value = (value << 8U) | byte;
        }
        return true;
    }

    template <typename Iterator>
    static bool skip_indefinite_string(Iterator &cursor, Iterator end, major_type expected_major, status_code &status) {
        while (true) {
            if (cursor == end) {
                status = status_code::incomplete;
                return false;
            }
            if (scanner_byte_to_u8(*cursor) == 0xFFU) {
                ++cursor;
                return true;
            }

            std::uint8_t initial{};
            if (!read_byte(cursor, end, initial, status)) {
                return false;
            }
            const auto    chunk_major     = static_cast<major_type>(initial >> 5U);
            const auto    additional_info = static_cast<std::uint8_t>(initial & 0x1FU);
            std::uint64_t chunk_length{};
            if (chunk_major != expected_major || additional_info == 31U) {
                status = status_code::error;
                return false;
            }
            if (!read_argument(cursor, end, additional_info, chunk_length, status)) {
                return false;
            }
            if (!advance_bytes(cursor, end, chunk_length, status)) {
                return false;
            }
        }
    }

    template <typename Iterator> static bool skip_item(Iterator &cursor, Iterator end, status_code &status) {
        std::uint8_t initial{};
        if (!read_byte(cursor, end, initial, status)) {
            return false;
        }

        const auto major           = static_cast<major_type>(initial >> 5U);
        const auto additional_info = static_cast<std::uint8_t>(initial & 0x1FU);

        switch (major) {
        case major_type::UnsignedInteger:
        case major_type::NegativeInteger: {
            std::uint64_t ignored{};
            if (additional_info == 31U) {
                status = status_code::error;
                return false;
            }
            return read_argument(cursor, end, additional_info, ignored, status);
        }
        case major_type::ByteString:
        case major_type::TextString: {
            if (additional_info == 31U) {
                return skip_indefinite_string(cursor, end, major, status);
            }
            std::uint64_t length{};
            return read_argument(cursor, end, additional_info, length, status) && advance_bytes(cursor, end, length, status);
        }
        case major_type::Array: {
            if (additional_info == 31U) {
                while (true) {
                    if (cursor == end) {
                        status = status_code::incomplete;
                        return false;
                    }
                    if (scanner_byte_to_u8(*cursor) == 0xFFU) {
                        ++cursor;
                        return true;
                    }
                    if (!skip_item(cursor, end, status)) {
                        return false;
                    }
                }
            }
            std::uint64_t length{};
            if (!read_argument(cursor, end, additional_info, length, status)) {
                return false;
            }
            for (std::uint64_t index = 0; index < length; ++index) {
                if (!skip_item(cursor, end, status)) {
                    return false;
                }
            }
            return true;
        }
        case major_type::Map: {
            if (additional_info == 31U) {
                while (true) {
                    if (cursor == end) {
                        status = status_code::incomplete;
                        return false;
                    }
                    if (scanner_byte_to_u8(*cursor) == 0xFFU) {
                        ++cursor;
                        return true;
                    }
                    if (!skip_item(cursor, end, status)) {
                        return false;
                    }
                    if (cursor == end) {
                        status = status_code::incomplete;
                        return false;
                    }
                    if (scanner_byte_to_u8(*cursor) == 0xFFU) {
                        status = status_code::error;
                        return false;
                    }
                    if (!skip_item(cursor, end, status)) {
                        return false;
                    }
                }
            }
            std::uint64_t length{};
            if (!read_argument(cursor, end, additional_info, length, status)) {
                return false;
            }
            if (length > (std::numeric_limits<std::uint64_t>::max() / 2U)) {
                status = status_code::error;
                return false;
            }
            for (std::uint64_t index = 0; index < length * 2U; ++index) {
                if (!skip_item(cursor, end, status)) {
                    return false;
                }
            }
            return true;
        }
        case major_type::Tag: {
            std::uint64_t ignored{};
            if (additional_info == 31U) {
                status = status_code::error;
                return false;
            }
            return read_argument(cursor, end, additional_info, ignored, status) && skip_item(cursor, end, status);
        }
        case major_type::Simple:
            if (additional_info == 31U || additional_info == 28U || additional_info == 29U || additional_info == 30U) {
                status = status_code::error;
                return false;
            }
            if (additional_info < 24U) {
                return true;
            }
            return advance_bytes(cursor, end, std::uint64_t{1U << (additional_info - 24U)}, status);
        default: status = status_code::error; return false;
        }
    }
};

template <std::uint64_t... Tags> struct static_tag_filter {
    constexpr bool operator()(std::uint64_t tag) const { return ((tag == Tags) || ...); }
};

} // namespace detail

template <CborInputBuffer Buffer> struct tag_payload_decoder {
    using buffer_type = std::remove_cvref_t<Buffer>;
    using iterator    = std::ranges::iterator_t<const buffer_type>;
    using subrange    = std::ranges::subrange<iterator>;

    subrange range_;

    template <typename... T> [[nodiscard]] auto operator()(T &&...values) {
        auto dec = cbor::tags::make_decoder(range_);
        return dec(std::forward<T>(values)...);
    }

    template <typename T> [[nodiscard]] auto decode(T &value) { return (*this)(value); }
};

template <CborInputBuffer Buffer> struct tag_match {
    using buffer_type = std::remove_cvref_t<Buffer>;
    using iterator    = std::ranges::iterator_t<const buffer_type>;
    using subrange    = std::ranges::subrange<iterator>;

    const buffer_type *buffer_{};
    std::uint64_t      tag_{};
    iterator           payload_begin_{};
    iterator           payload_end_{};

    [[nodiscard]] constexpr std::uint64_t tag() const noexcept { return tag_; }
    [[nodiscard]] constexpr subrange      payload_range() const { return subrange{payload_begin_, payload_end_}; }

    [[nodiscard]] auto payload_span() const
        requires std::ranges::contiguous_range<const buffer_type>
    {
        const auto size = static_cast<std::size_t>(std::ranges::distance(payload_begin_, payload_end_));
        return std::span<const std::byte>{reinterpret_cast<const std::byte *>(std::to_address(payload_begin_)), size};
    }

    [[nodiscard]] auto make_decoder() const { return tag_payload_decoder<buffer_type>{payload_range()}; }

    template <typename T> [[nodiscard]] auto decode(T &value) const {
        auto range = payload_range();
        auto dec   = cbor::tags::make_decoder(range);
        return dec(value);
    }
};

template <CborInputBuffer Buffer, typename Predicate> class tag_view : public std::ranges::view_interface<tag_view<Buffer, Predicate>> {
  public:
    using buffer_type = std::remove_cvref_t<Buffer>;
    using iterator_t  = std::ranges::iterator_t<const buffer_type>;
    using match_type  = tag_match<buffer_type>;

    class iterator {
      public:
        using value_type        = match_type;
        using difference_type   = std::ptrdiff_t;
        using iterator_category = std::input_iterator_tag;
        using iterator_concept  = std::input_iterator_tag;

        iterator() = default;
        explicit iterator(tag_view *view)
            : view_(view), cursor_(std::ranges::begin(*view->buffer_)), end_(std::ranges::end(*view->buffer_)) {
            find_next();
        }

        const match_type &operator*() const { return current_; }
        const match_type *operator->() const { return &current_; }

        iterator &operator++() {
            find_next();
            return *this;
        }

        void operator++(int) { ++(*this); }

        friend bool operator==(const iterator &lhs, std::default_sentinel_t) { return lhs.done_; }
        friend bool operator==(std::default_sentinel_t, const iterator &rhs) { return rhs.done_; }

      private:
        void fail(status_code status) {
            view_->status_ = status;
            done_          = true;
        }

        void pop_completed_frames() {
            while (!stack_empty() && !stack_back().indefinite && stack_back().remaining == 0) {
                stack_pop_back();
            }
        }

        bool consume_parent_item() {
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

        bool consume_indefinite_break_if_present() {
            if (stack_empty() || !stack_back().indefinite || cursor_ == end_ || detail::scanner_byte_to_u8(*cursor_) != 0xFFU) {
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

        bool read_argument(std::uint8_t additional_info, std::uint64_t &value) {
            status_code status = status_code::success;
            if (!detail::cbor_item_skipper::read_argument(cursor_, end_, additional_info, value, status)) {
                fail(status);
                return false;
            }
            return true;
        }

        bool skip_indefinite_string(major_type major) {
            status_code status = status_code::success;
            if (!detail::cbor_item_skipper::skip_indefinite_string(cursor_, end_, major, status)) {
                fail(status);
                return false;
            }
            return true;
        }

        bool skip_bytes(std::uint64_t length) {
            status_code status = status_code::success;
            if (!detail::cbor_item_skipper::advance_bytes(cursor_, end_, length, status)) {
                fail(status);
                return false;
            }
            return true;
        }

        void find_next() {
            if (done_) {
                return;
            }

            while (true) {
                pop_completed_frames();

                if (cursor_ == end_) {
                    if (stack_empty()) {
                        view_->status_ = status_code::success;
                        done_          = true;
                    } else {
                        fail(status_code::incomplete);
                    }
                    return;
                }

                if (consume_indefinite_break_if_present()) {
                    if (done_) {
                        return;
                    }
                    continue;
                }

                if (detail::scanner_byte_to_u8(*cursor_) == 0xFFU) {
                    fail(status_code::error);
                    return;
                }

                if (!consume_parent_item()) {
                    fail(status_code::error);
                    return;
                }

                std::uint8_t initial{};
                status_code  status = status_code::success;
                if (!detail::cbor_item_skipper::read_byte(cursor_, end_, initial, status)) {
                    fail(status);
                    return;
                }

                const auto major           = static_cast<major_type>(initial >> 5U);
                const auto additional_info = static_cast<std::uint8_t>(initial & 0x1FU);

                switch (major) {
                case major_type::UnsignedInteger:
                case major_type::NegativeInteger: {
                    std::uint64_t ignored{};
                    if (additional_info == 31U || !read_argument(additional_info, ignored)) {
                        if (!done_) {
                            fail(status_code::error);
                        }
                        return;
                    }
                    break;
                }
                case major_type::ByteString:
                case major_type::TextString: {
                    if (additional_info == 31U) {
                        if (!skip_indefinite_string(major)) {
                            return;
                        }
                    } else {
                        std::uint64_t length{};
                        if (!read_argument(additional_info, length) || !skip_bytes(length)) {
                            return;
                        }
                    }
                    break;
                }
                case major_type::Array: {
                    if (additional_info == 31U) {
                        if (!push_frame(detail::tag_scan_frame{.indefinite = true, .major = major_type::Array})) {
                            return;
                        }
                    } else {
                        std::uint64_t length{};
                        if (!read_argument(additional_info, length)) {
                            return;
                        }
                        if (length > 0) {
                            if (!push_frame(detail::tag_scan_frame{.major = major_type::Array, .remaining = length})) {
                                return;
                            }
                        }
                    }
                    break;
                }
                case major_type::Map: {
                    if (additional_info == 31U) {
                        if (!push_frame(detail::tag_scan_frame{.indefinite = true, .major = major_type::Map})) {
                            return;
                        }
                    } else {
                        std::uint64_t length{};
                        if (!read_argument(additional_info, length)) {
                            return;
                        }
                        if (length > (std::numeric_limits<std::uint64_t>::max() / 2U)) {
                            fail(status_code::error);
                            return;
                        }
                        if (length > 0) {
                            if (!push_frame(detail::tag_scan_frame{.major = major_type::Map, .remaining = length * 2U})) {
                                return;
                            }
                        }
                    }
                    break;
                }
                case major_type::Tag: {
                    std::uint64_t tag{};
                    if (additional_info == 31U || !read_argument(additional_info, tag)) {
                        if (!done_) {
                            fail(status_code::error);
                        }
                        return;
                    }
                    auto payload_begin = cursor_;
                    auto payload_end   = payload_begin;
                    status             = status_code::success;
                    if (!detail::cbor_item_skipper::skip_item(payload_end, end_, status)) {
                        fail(status);
                        return;
                    }

                    if (!push_frame(detail::tag_scan_frame{.major = major_type::Tag, .remaining = 1})) {
                        return;
                    }
                    if (std::invoke(view_->predicate_, tag)) {
                        current_ = match_type{.buffer_        = view_->buffer_,
                                              .tag_           = tag,
                                              .payload_begin_ = payload_begin,
                                              .payload_end_   = payload_end};
                        return;
                    }
                    break;
                }
                case major_type::Simple:
                    if (additional_info == 31U || additional_info == 28U || additional_info == 29U || additional_info == 30U) {
                        fail(status_code::error);
                        return;
                    }
                    if (additional_info >= 24U && !skip_bytes(std::uint64_t{1U << (additional_info - 24U)})) {
                        return;
                    }
                    break;
                default: fail(status_code::error); return;
                }
            }
        }

        [[nodiscard]] bool  stack_empty() const noexcept { return stack_size_ == 0; }
        [[nodiscard]] auto &stack_back() noexcept { return stack_[stack_size_ - 1]; }
        void                stack_pop_back() noexcept { --stack_size_; }
        bool                push_frame(detail::tag_scan_frame frame) {
            if (stack_size_ == stack_.size()) {
                fail(status_code::error);
                return false;
            }
            stack_[stack_size_++] = frame;
            return true;
        }

        static constexpr std::size_t                        max_stack_depth = 256;
        tag_view                                           *view_{};
        iterator_t                                          cursor_{};
        iterator_t                                          end_{};
        std::array<detail::tag_scan_frame, max_stack_depth> stack_{};
        std::size_t                                         stack_size_{};
        match_type                                          current_{};
        bool                                                done_{};
    };

    constexpr tag_view(const buffer_type &buffer, Predicate predicate) : buffer_(&buffer), predicate_(std::move(predicate)) {}

    iterator begin() {
        status_ = status_code::success;
        return iterator{this};
    }
    std::default_sentinel_t end() const noexcept { return {}; }

    [[nodiscard]] status_code status() const noexcept { return status_; }
    [[nodiscard]] bool        failed() const noexcept { return status_ != status_code::success; }

  private:
    const buffer_type *buffer_;
    Predicate          predicate_;
    status_code        status_{status_code::success};

    friend class iterator;
};

template <std::uint64_t... Tags, CborInputBuffer Buffer> [[nodiscard]] auto find_tags(Buffer &buffer) {
    return tag_view<std::remove_cvref_t<Buffer>, detail::static_tag_filter<Tags...>>{buffer, {}};
}

template <std::uint64_t... Tags, CborInputBuffer Buffer>
auto find_tags(Buffer &&buffer)
    requires(!std::is_lvalue_reference_v<Buffer>)
= delete;

template <CborInputBuffer Buffer, typename Predicate> [[nodiscard]] auto find_tags(Buffer &buffer, Predicate predicate) {
    return tag_view<std::remove_cvref_t<Buffer>, std::remove_cvref_t<Predicate>>{buffer, std::forward<Predicate>(predicate)};
}

template <CborInputBuffer Buffer, typename Predicate>
auto find_tags(Buffer &&buffer, Predicate &&predicate)
    requires(!std::is_lvalue_reference_v<Buffer>)
= delete;

} // namespace cbor::tags
