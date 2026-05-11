#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_detail.h"
#include "cbor_tags/cbor_raw_views.h"
#include "cbor_tags/detail/cbor_argument.h"
#include "cbor_tags/detail/cbor_item.h"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace cbor::tags {

enum class cbor_segment_kind : std::uint8_t {
    owned,
    borrowed,
};

class cbor_segment {
  public:
    static constexpr std::size_t inline_owned_capacity = 32;

    [[nodiscard]] static cbor_segment owned(std::span<const std::byte> bytes) {
        cbor_segment segment;
        segment.kind_ = cbor_segment_kind::owned;
        if (bytes.size() <= inline_owned_capacity) {
            std::ranges::copy(bytes, segment.inline_owned_.begin());
            segment.inline_owned_size_ = bytes.size();
        } else {
            segment.owned_.assign(bytes.begin(), bytes.end());
        }
        return segment;
    }

    [[nodiscard]] static cbor_segment owned(std::initializer_list<std::byte> bytes) {
        return owned(std::span<const std::byte>{bytes.begin(), bytes.size()});
    }

    [[nodiscard]] static cbor_segment owned(detail::cbor_argument_header header) {
        cbor_segment segment;
        segment.kind_              = cbor_segment_kind::owned;
        segment.inline_owned_size_ = std::min(header.size, header.bytes.size());
        std::ranges::copy_n(header.bytes.begin(), static_cast<std::ptrdiff_t>(segment.inline_owned_size_), segment.inline_owned_.begin());
        return segment;
    }

    [[nodiscard]] static cbor_segment borrowed(std::span<const std::byte> bytes) {
        cbor_segment segment;
        segment.kind_     = cbor_segment_kind::borrowed;
        segment.borrowed_ = bytes;
        return segment;
    }

    [[nodiscard]] cbor_segment_kind kind() const noexcept { return kind_; }
    [[nodiscard]] bool              is_owned() const noexcept { return kind_ == cbor_segment_kind::owned; }
    [[nodiscard]] bool              is_borrowed() const noexcept { return kind_ == cbor_segment_kind::borrowed; }

    bool append_owned(std::span<const std::byte> bytes) {
        if (!is_owned()) {
            return false;
        }
        if (!owned_.empty()) {
            owned_.insert(owned_.end(), bytes.begin(), bytes.end());
            return true;
        }
        if ((inline_owned_size_ + bytes.size()) <= inline_owned_capacity) {
            std::ranges::copy(bytes, inline_owned_.begin() + static_cast<std::ptrdiff_t>(inline_owned_size_));
            inline_owned_size_ += bytes.size();
            return true;
        }

        owned_.reserve(inline_owned_size_ + bytes.size());
        owned_.insert(owned_.end(), inline_owned_.begin(), inline_owned_.begin() + static_cast<std::ptrdiff_t>(inline_owned_size_));
        owned_.insert(owned_.end(), bytes.begin(), bytes.end());
        inline_owned_size_ = 0;
        return true;
    }

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        if (is_borrowed()) {
            return borrowed_;
        }
        if (!owned_.empty()) {
            return std::span<const std::byte>{owned_.data(), owned_.size()};
        }
        return std::span<const std::byte>{inline_owned_.data(), inline_owned_size_};
    }

    [[nodiscard]] const std::byte *data() const noexcept { return bytes().data(); }
    [[nodiscard]] std::size_t      size() const noexcept { return bytes().size(); }
    [[nodiscard]] bool             empty() const noexcept { return bytes().empty(); }

  private:
    cbor_segment_kind          kind_{cbor_segment_kind::owned};
    std::array<std::byte, 32>  inline_owned_{};
    std::size_t                inline_owned_size_{};
    std::vector<std::byte>     owned_{};
    std::span<const std::byte> borrowed_{};
};

class cbor_segments {
  public:
    using value_type      = std::byte;
    using size_type       = std::size_t;
    using container_type  = std::vector<cbor_segment>;
    using const_iterator  = container_type::const_iterator;
    using iterator        = const_iterator;
    using const_reference = container_type::const_reference;

    void reserve(std::size_t count) { segments_.reserve(count); }

    void append_owned(std::span<const std::byte> bytes) {
        if (!segments_.empty() && segments_.back().append_owned(bytes)) {
            return;
        }
        segments_.push_back(cbor_segment::owned(bytes));
    }
    void append_owned(std::initializer_list<std::byte> bytes) { append_owned(std::span<const std::byte>{bytes.begin(), bytes.size()}); }
    void append_owned(detail::cbor_argument_header header) { append_owned(header.span()); }
    void append_borrowed(std::span<const std::byte> bytes) { segments_.push_back(cbor_segment::borrowed(bytes)); }

    [[nodiscard]] const_iterator begin() const noexcept { return segments_.begin(); }
    [[nodiscard]] const_iterator end() const noexcept { return segments_.end(); }

    [[nodiscard]] const_reference operator[](std::size_t index) const noexcept { return segments_[index]; }
    [[nodiscard]] const_reference front() const noexcept { return segments_.front(); }
    [[nodiscard]] const_reference back() const noexcept { return segments_.back(); }

    [[nodiscard]] bool        empty() const noexcept { return segments_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return segments_.size(); }

    [[nodiscard]] std::size_t total_size() const noexcept {
        std::size_t result{};
        for (const auto &segment : segments_) {
            result += segment.size();
        }
        return result;
    }

    [[nodiscard]] std::vector<std::byte> flatten() const {
        std::vector<std::byte> output;
        output.reserve(total_size());
        for (const auto &segment : segments_) {
            const auto bytes = segment.bytes();
            output.insert(output.end(), bytes.begin(), bytes.end());
        }
        return output;
    }

  private:
    container_type segments_{};
};

namespace detail {

template <> struct is_segment_output_buffer<cbor_segments> : std::true_type {};

class cbor_segment_byte_iterator {
  public:
    using iterator_category = std::input_iterator_tag;
    using iterator_concept  = std::input_iterator_tag;
    using value_type        = std::byte;
    using difference_type   = std::ptrdiff_t;

    cbor_segment_byte_iterator() = default;
    cbor_segment_byte_iterator(cbor_segments::const_iterator current, cbor_segments::const_iterator end) : current_(current), end_(end) {
        skip_empty_segments();
    }

    [[nodiscard]] std::byte operator*() const noexcept { return current_->bytes()[offset_]; }

    cbor_segment_byte_iterator &operator++() {
        ++offset_;
        if (offset_ == current_->size()) {
            ++current_;
            offset_ = 0;
            skip_empty_segments();
        }
        return *this;
    }

    void operator++(int) { ++(*this); }

    friend bool operator==(const cbor_segment_byte_iterator &lhs, const cbor_segment_byte_iterator &rhs) noexcept {
        return lhs.current_ == rhs.current_ && (lhs.current_ == lhs.end_ || lhs.offset_ == rhs.offset_);
    }

    friend bool operator!=(const cbor_segment_byte_iterator &lhs, const cbor_segment_byte_iterator &rhs) noexcept { return !(lhs == rhs); }

  private:
    void skip_empty_segments() noexcept {
        while (current_ != end_ && current_->empty()) {
            ++current_;
        }
    }

    cbor_segments::const_iterator current_{};
    cbor_segments::const_iterator end_{};
    std::size_t                   offset_{};
};

} // namespace detail

class encoded_item_segments {
  public:
    encoded_item_segments() = delete;

    [[nodiscard]] const cbor_segments   &segments() const noexcept { return segments_; }
    [[nodiscard]] std::size_t            total_size() const noexcept { return segments_.total_size(); }
    [[nodiscard]] std::vector<std::byte> flatten() const { return segments_.flatten(); }

  private:
    struct validated_t {};

    explicit encoded_item_segments(cbor_segments segments, validated_t) : segments_(std::move(segments)) {}

    cbor_segments segments_{};

    friend expected<encoded_item_segments, status_code> validate_item_segments(cbor_segments segments);
};

[[nodiscard]] inline expected<encoded_item_segments, status_code> validate_item_segments(cbor_segments segments) {
    auto cursor = detail::cbor_segment_byte_iterator{segments.begin(), segments.end()};
    auto end    = detail::cbor_segment_byte_iterator{segments.end(), segments.end()};

    auto status = status_code::success;
    if (!detail::cbor_item_skipper<>::skip_item(cursor, end, status)) {
        return unexpected<status_code>{status};
    }
    if (cursor != end) {
        return unexpected<status_code>{status_code::error};
    }

    return encoded_item_segments{std::move(segments), encoded_item_segments::validated_t{}};
}

class encoded_item_bstr {
  public:
    [[nodiscard]] const encoded_item_segments &item() const noexcept { return *item_; }

  private:
    explicit encoded_item_bstr(const encoded_item_segments &item) noexcept : item_(&item) {}

    const encoded_item_segments *item_{};

    friend encoded_item_bstr as_bstr(const encoded_item_segments &item) noexcept;
};

[[nodiscard]] inline encoded_item_bstr as_bstr(const encoded_item_segments &item) noexcept { return encoded_item_bstr{item}; }
[[nodiscard]] inline encoded_item_bstr as_bstr(encoded_item_segments &&)       = delete;
[[nodiscard]] inline encoded_item_bstr as_bstr(const encoded_item_segments &&) = delete;

[[nodiscard]] inline std::vector<std::byte> flatten_segments(const cbor_segments &segments) { return segments.flatten(); }

namespace detail {

template <> struct appender<cbor_segments, false> {
    using value_type = std::byte;

    void operator()(cbor_segments &segments, value_type value) { segments.append_owned(std::span<const std::byte>{&value, 1}); }

    template <typename... Ts> void multi_append(cbor_segments &segments, Ts &&...values) {
        static_assert(sizeof...(Ts) > 1, "multi_append requires at least 2 arguments, use operator() for single values");
        constexpr bool all_1_byte = ((sizeof(Ts) == 1) && ...);
        static_assert(all_1_byte, "multi_append requires all arguments to be 1 byte types");
        const std::array bytes{static_cast<value_type>(values)...};
        segments.append_owned(std::span<const std::byte>{bytes});
    }

    void operator()(cbor_segments &segments, std::span<const std::byte> values) { append_owned(segments, values); }

    void operator()(cbor_segments &segments, std::string_view value) {
        append_owned(segments, std::as_bytes(std::span{value.data(), value.size()}));
    }

    void append_owned(cbor_segments &segments, std::span<const std::byte> values) { segments.append_owned(values); }
    void append_borrowed(cbor_segments &segments, std::span<const std::byte> values) { segments.append_borrowed(values); }
};

template <typename Encoder> void append_segment_to_encoder(Encoder &enc, const cbor_segment &segment) {
    const auto bytes = segment.bytes();
    if constexpr (requires {
                      enc.appender_.append_borrowed(enc.data_, bytes);
                      enc.appender_.append_owned(enc.data_, bytes);
                  }) {
        if (segment.is_borrowed()) {
            enc.appender_.append_borrowed(enc.data_, bytes);
        } else {
            enc.appender_.append_owned(enc.data_, bytes);
        }
    } else {
        append_byte_range(enc.appender_, enc.data_, bytes);
    }
}

template <typename Encoder> void append_item_segments_to_encoder(Encoder &enc, const encoded_item_segments &item) {
    for (const auto &segment : item.segments()) {
        append_segment_to_encoder(enc, segment);
    }
}

template <typename T>
concept SpanBackedEncodedItemView =
    IsEncodedItemView<T> && requires { typename std::remove_cvref_t<T>::range_type; } &&
    std::ranges::contiguous_range<const typename std::remove_cvref_t<T>::range_type> &&
    std::ranges::sized_range<const typename std::remove_cvref_t<T>::range_type> && requires(const std::remove_cvref_t<T> &value) {
        { value.span() } -> std::same_as<std::span<const std::byte>>;
    };

template <CborAppendOutputBuffer OutputBuffer>
constexpr void append_segment_bytes(OutputBuffer &output, std::span<const std::byte> segment) {
    if (segment.empty()) {
        return;
    }
    using output_byte = typename std::remove_cvref_t<OutputBuffer>::value_type;
    output.insert(output.end(), reinterpret_cast<const output_byte *>(segment.data()),
                  reinterpret_cast<const output_byte *>(segment.data() + segment.size()));
}

} // namespace detail

template <typename Encoder> auto encode(Encoder &enc, const encoded_item_segments &item) {
    detail::append_item_segments_to_encoder(enc, item);
    return typename Encoder::expected_type{};
}

template <typename Encoder> auto encode(Encoder &enc, encoded_item_bstr value) {
    enc.encode_major_and_size(value.item().total_size(), static_cast<typename Encoder::byte_type>(0x40));
    detail::append_item_segments_to_encoder(enc, value.item());
    return typename Encoder::expected_type{};
}

template <typename VisitSegment> constexpr void visit_bstr_segments(std::span<const std::byte> payload, VisitSegment &&visit_segment) {
    const auto header = detail::encode_cbor_major_argument_header(payload.size(), std::byte{0x40});
    visit_segment(header.span());
    visit_segment(payload);
}

template <typename VisitSegment>
constexpr void visit_indefinite_bstr_segments(std::span<const std::byte> payload, VisitSegment &&visit_segment,
                                              std::size_t chunk_size = 4096) {
    if (chunk_size == 0) {
        throw std::invalid_argument("CBOR indefinite byte string chunk size must be greater than zero");
    }

    constexpr std::array start{std::byte{0x5F}};
    constexpr std::array stop{std::byte{0xFF}};
    visit_segment(std::span<const std::byte>{start});

    for (std::size_t offset = 0; offset < payload.size(); offset += chunk_size) {
        const auto chunk_length = std::min(chunk_size, payload.size() - offset);
        const auto header       = detail::encode_cbor_major_argument_header(chunk_length, std::byte{0x40});
        visit_segment(header.span());
        visit_segment(payload.subspan(offset, chunk_length));
    }

    visit_segment(std::span<const std::byte>{stop});
}

template <typename VisitSegment>
constexpr void visit_tagged_bstr_segments(std::uint64_t tag, std::span<const std::byte> payload, VisitSegment &&visit_segment) {
    const auto tag_header  = detail::encode_cbor_major_argument_header(tag, std::byte{0xC0});
    const auto bstr_header = detail::encode_cbor_major_argument_header(payload.size(), std::byte{0x40});
    visit_segment(tag_header.span());
    visit_segment(bstr_header.span());
    visit_segment(payload);
}

template <std::uint64_t Tag, typename VisitSegment>
constexpr void visit_tagged_bstr_segments(static_tag<Tag>, std::span<const std::byte> payload, VisitSegment &&visit_segment) {
    visit_tagged_bstr_segments(Tag, payload, std::forward<VisitSegment>(visit_segment));
}

template <IsUnsigned Tag, typename VisitSegment>
constexpr void visit_tagged_bstr_segments(dynamic_tag<Tag> tag, std::span<const std::byte> payload, VisitSegment &&visit_segment) {
    visit_tagged_bstr_segments(static_cast<std::uint64_t>(tag.cbor_tag), payload, std::forward<VisitSegment>(visit_segment));
}

template <typename RawView, typename VisitSegment>
    requires detail::SpanBackedEncodedItemView<RawView>
constexpr void visit_encoded_segments(const RawView &view, VisitSegment &&visit_segment) {
    visit_segment(view.span());
}

template <CborAppendOutputBuffer OutputBuffer, typename VisitSegments>
constexpr void append_visited_segments(OutputBuffer &output, VisitSegments &&visit_segments) {
    visit_segments([&](std::span<const std::byte> segment) { detail::append_segment_bytes(output, segment); });
}

template <CborAppendOutputBuffer OutputBuffer>
constexpr void append_bstr_segments(OutputBuffer &output, std::span<const std::byte> payload) {
    append_visited_segments(
        output, [&](auto &&visit_segment) { visit_bstr_segments(payload, std::forward<decltype(visit_segment)>(visit_segment)); });
}

template <CborAppendOutputBuffer OutputBuffer>
constexpr void append_indefinite_bstr_segments(OutputBuffer &output, std::span<const std::byte> payload, std::size_t chunk_size = 4096) {
    append_visited_segments(output, [&](auto &&visit_segment) {
        visit_indefinite_bstr_segments(payload, std::forward<decltype(visit_segment)>(visit_segment), chunk_size);
    });
}

template <CborAppendOutputBuffer OutputBuffer>
constexpr void append_tagged_bstr_segments(OutputBuffer &output, std::uint64_t tag, std::span<const std::byte> payload) {
    append_visited_segments(output, [&](auto &&visit_segment) {
        visit_tagged_bstr_segments(tag, payload, std::forward<decltype(visit_segment)>(visit_segment));
    });
}

template <CborAppendOutputBuffer OutputBuffer, std::uint64_t Tag>
constexpr void append_tagged_bstr_segments(OutputBuffer &output, static_tag<Tag> tag, std::span<const std::byte> payload) {
    append_visited_segments(output, [&](auto &&visit_segment) {
        visit_tagged_bstr_segments(tag, payload, std::forward<decltype(visit_segment)>(visit_segment));
    });
}

template <CborAppendOutputBuffer OutputBuffer, IsUnsigned Tag>
constexpr void append_tagged_bstr_segments(OutputBuffer &output, dynamic_tag<Tag> tag, std::span<const std::byte> payload) {
    append_visited_segments(output, [&](auto &&visit_segment) {
        visit_tagged_bstr_segments(tag, payload, std::forward<decltype(visit_segment)>(visit_segment));
    });
}

template <CborAppendOutputBuffer OutputBuffer, typename RawView>
    requires IsEncodedItemView<RawView>
constexpr void append_encoded_segments(OutputBuffer &output, const RawView &view) {
    using output_byte = typename std::remove_cvref_t<OutputBuffer>::value_type;
    if constexpr (requires {
                      { view.span() } -> std::same_as<std::span<const std::byte>>;
                  }) {
        detail::append_segment_bytes(output, view.span());
    } else {
        for (auto byte : view.bytes()) {
            output.push_back(static_cast<output_byte>(byte));
        }
    }
}

[[nodiscard]] inline cbor_segments encode_bstr_segments(std::span<const std::byte> payload) {
    cbor_segments segments;
    segments.reserve(2);
    segments.append_owned(detail::encode_cbor_major_argument_header(payload.size(), std::byte{0x40}));
    segments.append_borrowed(payload);
    return segments;
}

[[nodiscard]] inline cbor_segments encode_indefinite_bstr_segments(std::span<const std::byte> payload, std::size_t chunk_size = 4096) {
    if (chunk_size == 0) {
        throw std::invalid_argument("CBOR indefinite byte string chunk size must be greater than zero");
    }

    cbor_segments segments;
    const auto    chunk_count = payload.empty() ? std::size_t{0} : ((payload.size() + chunk_size - 1U) / chunk_size);
    segments.reserve(2U + (chunk_count * 2U));
    const auto payload_begin = reinterpret_cast<std::uintptr_t>(payload.data());
    const auto payload_end   = payload_begin + payload.size();
    visit_indefinite_bstr_segments(
        payload,
        [&](std::span<const std::byte> segment) {
            const auto segment_begin = reinterpret_cast<std::uintptr_t>(segment.data());
            if (!payload.empty() && segment_begin >= payload_begin && segment_begin < payload_end) {
                segments.append_borrowed(segment);
            } else {
                segments.append_owned(segment);
            }
        },
        chunk_size);
    return segments;
}

[[nodiscard]] inline cbor_segments encode_tagged_bstr_segments(std::uint64_t tag, std::span<const std::byte> payload) {
    cbor_segments segments;
    segments.reserve(3);
    segments.append_owned(detail::encode_cbor_major_argument_header(tag, std::byte{0xC0}));
    segments.append_owned(detail::encode_cbor_major_argument_header(payload.size(), std::byte{0x40}));
    segments.append_borrowed(payload);
    return segments;
}

template <std::uint64_t Tag>
[[nodiscard]] inline cbor_segments encode_tagged_bstr_segments(static_tag<Tag>, std::span<const std::byte> payload) {
    return encode_tagged_bstr_segments(Tag, payload);
}

template <IsUnsigned Tag>
[[nodiscard]] inline cbor_segments encode_tagged_bstr_segments(dynamic_tag<Tag> tag, std::span<const std::byte> payload) {
    return encode_tagged_bstr_segments(static_cast<std::uint64_t>(tag.cbor_tag), payload);
}

template <typename RawView>
    requires detail::SpanBackedEncodedItemView<RawView>
[[nodiscard]] inline cbor_segments encode_encoded_segments(const RawView &view) {
    cbor_segments segments;
    const auto    bytes = view.span();
    segments.reserve(1);
    segments.append_borrowed(bytes);
    return segments;
}

template <typename RawView>
    requires IsEncodedItemView<RawView>
[[nodiscard]] inline cbor_segments encode_encoded_segments_copy(const RawView &view) {
    std::vector<std::byte> bytes;
    bytes.reserve(view.size());
    for (auto byte : view.bytes()) {
        bytes.push_back(static_cast<std::byte>(byte));
    }

    cbor_segments segments;
    segments.reserve(1);
    segments.append_owned(bytes);
    return segments;
}

} // namespace cbor::tags

#include "cbor_tags/cbor_encoder.h"

namespace cbor::tags {

template <typename T> [[nodiscard]] inline expected<encoded_item_segments, status_code> encode_item_segments(T &&value) {
    cbor_segments segments;
    auto          enc    = make_encoder(segments);
    const auto    result = enc(std::forward<T>(value));
    if (!result) {
        return unexpected<status_code>{result.error()};
    }
    return validate_item_segments(std::move(segments));
}

template <template <typename> typename... Extensions, typename T>
    requires(sizeof...(Extensions) > 0)
[[nodiscard]] inline expected<encoded_item_segments, status_code> encode_item_segments(T &&value) {
    cbor_segments segments;
    auto          enc    = make_encoder<Extensions...>(segments);
    const auto    result = enc(std::forward<T>(value));
    if (!result) {
        return unexpected<status_code>{result.error()};
    }
    return validate_item_segments(std::move(segments));
}

} // namespace cbor::tags
