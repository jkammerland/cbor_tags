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

enum class byte_segment_kind : std::uint8_t {
    owned,
    borrowed,
};

using cbor_segment_kind = byte_segment_kind;

template <std::size_t InlineOwnedCapacity = 32> class basic_byte_segment {
  public:
    static constexpr std::size_t inline_owned_capacity = InlineOwnedCapacity;

    [[nodiscard]] static basic_byte_segment owned(std::span<const std::byte> bytes) {
        basic_byte_segment segment;
        segment.kind_ = byte_segment_kind::owned;
        if (bytes.size() <= inline_owned_capacity) {
            std::ranges::copy(bytes, segment.inline_owned_.begin());
            segment.inline_owned_size_ = bytes.size();
        } else {
            segment.owned_.assign(bytes.begin(), bytes.end());
        }
        return segment;
    }

    [[nodiscard]] static basic_byte_segment owned(std::initializer_list<std::byte> bytes) {
        return owned(std::span<const std::byte>{bytes.begin(), bytes.size()});
    }

    [[nodiscard]] static basic_byte_segment owned(detail::cbor_argument_header header) { return owned(header.span()); }

    [[nodiscard]] static basic_byte_segment borrowed(std::span<const std::byte> bytes) {
        basic_byte_segment segment;
        segment.kind_     = byte_segment_kind::borrowed;
        segment.borrowed_ = bytes;
        return segment;
    }

    [[nodiscard]] byte_segment_kind kind() const noexcept { return kind_; }
    [[nodiscard]] bool              is_owned() const noexcept { return kind_ == byte_segment_kind::owned; }
    [[nodiscard]] bool              is_borrowed() const noexcept { return kind_ == byte_segment_kind::borrowed; }

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
    byte_segment_kind                          kind_{byte_segment_kind::owned};
    std::array<std::byte, InlineOwnedCapacity> inline_owned_{};
    std::size_t                                inline_owned_size_{};
    std::vector<std::byte>                     owned_{};
    std::span<const std::byte>                 borrowed_{};
};

using byte_segment = basic_byte_segment<>;
using cbor_segment = byte_segment;

template <std::size_t InlineOwnedCapacity = 32> struct default_byte_segment_storage {
    using segment_type   = basic_byte_segment<InlineOwnedCapacity>;
    using container_type = std::vector<segment_type>;

    static constexpr std::size_t inline_owned_capacity = InlineOwnedCapacity;

    [[nodiscard]] container_type make_container() const { return {}; }
    [[nodiscard]] segment_type   make_owned(std::span<const std::byte> bytes) const { return segment_type::owned(bytes); }
    [[nodiscard]] segment_type   make_borrowed(std::span<const std::byte> bytes) const { return segment_type::borrowed(bytes); }
};

namespace detail {

template <typename T>
concept ByteSegment = requires(const T &segment) {
    { segment.kind() } -> std::same_as<byte_segment_kind>;
    { segment.is_owned() } -> std::convertible_to<bool>;
    { segment.is_borrowed() } -> std::convertible_to<bool>;
    { segment.bytes() } -> std::same_as<std::span<const std::byte>>;
    { segment.data() } -> std::same_as<const std::byte *>;
    { segment.size() } -> std::convertible_to<std::size_t>;
    { segment.empty() } -> std::convertible_to<bool>;
};

template <typename T>
concept ByteSegmentStorage = requires(T storage, std::span<const std::byte> bytes) {
    typename T::segment_type;
    typename T::container_type;
    requires ByteSegment<typename T::segment_type>;
    typename T::container_type::const_iterator;
    typename T::container_type::const_reference;
    { storage.make_container() } -> std::same_as<typename T::container_type>;
    { storage.make_owned(bytes) } -> std::same_as<typename T::segment_type>;
    { storage.make_borrowed(bytes) } -> std::same_as<typename T::segment_type>;
} && requires(typename T::container_type &segments, typename T::segment_type segment) {
    segments.push_back(std::move(segment));
    segments.begin();
    segments.end();
    segments.empty();
    segments.size();
    segments.back();
};

} // namespace detail

template <typename Storage = default_byte_segment_storage<>>
    requires detail::ByteSegmentStorage<Storage>
class basic_byte_segments {
  public:
    using value_type      = std::byte;
    using size_type       = std::size_t;
    using storage_type    = Storage;
    using segment_type    = typename storage_type::segment_type;
    using container_type  = typename storage_type::container_type;
    using const_iterator  = container_type::const_iterator;
    using iterator        = const_iterator;
    using const_reference = container_type::const_reference;

    basic_byte_segments() : storage_{}, segments_(storage_.make_container()) {}
    explicit basic_byte_segments(storage_type storage) : storage_(std::move(storage)), segments_(storage_.make_container()) {}

    void reserve_segments(std::size_t count) {
        if constexpr (requires { segments_.reserve(count); }) {
            segments_.reserve(count);
        }
    }

    void reserve_owned_bytes(std::size_t count) {
        if constexpr (requires(storage_type &storage) { storage.reserve_owned_bytes(count); }) {
            storage_.reserve_owned_bytes(count);
        }
    }

    void reserve(std::size_t count) { reserve_segments(count); }

    void append_owned(std::span<const std::byte> bytes) {
        if constexpr (requires(storage_type &storage, container_type &segments) {
                          { storage.try_append_owned(segments, bytes) } -> std::convertible_to<bool>;
                      }) {
            if (storage_.try_append_owned(segments_, bytes)) {
                return;
            }
        } else if constexpr (requires(storage_type &storage, container_type &segments) {
                                 { storage.append_owned(segments, bytes) } -> std::same_as<void>;
                             }) {
            storage_.append_owned(segments_, bytes);
            return;
        } else {
            if (try_append_to_last_owned_segment(bytes)) {
                return;
            }
        }
        append_owned_segment(bytes);
    }
    void append_owned(detail::cbor_argument_header header) { append_owned(header.span()); }
    void append_owned(std::initializer_list<std::byte> bytes) { append_owned(std::span<const std::byte>{bytes.begin(), bytes.size()}); }
    void append_owned_segment(std::span<const std::byte> bytes) { segments_.push_back(storage_.make_owned(bytes)); }
    void append_borrowed(std::span<const std::byte> bytes) { segments_.push_back(storage_.make_borrowed(bytes)); }

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
        flatten_to(output);
        return output;
    }

    template <typename OutputBuffer>
        requires CborAppendOutputBuffer<OutputBuffer> && IsCborBufferByte<typename std::remove_cvref_t<OutputBuffer>::value_type>
    void flatten_to(OutputBuffer &output) const {
        using output_byte = typename std::remove_cvref_t<OutputBuffer>::value_type;
        for (const auto &segment : segments_) {
            const auto bytes = segment.bytes();
            if (bytes.empty()) {
                continue;
            }
            output.insert(output.end(), reinterpret_cast<const output_byte *>(bytes.data()),
                          reinterpret_cast<const output_byte *>(bytes.data() + bytes.size()));
        }
    }

  private:
    bool try_append_to_last_owned_segment(std::span<const std::byte> bytes) {
        if constexpr (requires(segment_type &segment) {
                          { segment.append_owned(bytes) } -> std::convertible_to<bool>;
                      }) {
            if (!segments_.empty() && segments_.back().append_owned(bytes)) {
                return true;
            }
        }
        return false;
    }

    storage_type   storage_{};
    container_type segments_{};
};

using byte_segments = basic_byte_segments<>;
using cbor_segments = byte_segments;

namespace detail {

template <ByteSegmentStorage Storage> struct is_segment_output_buffer<basic_byte_segments<Storage>> : std::true_type {};

template <typename Segments> class byte_segment_byte_iterator {
  public:
    using iterator_category = std::input_iterator_tag;
    using iterator_concept  = std::input_iterator_tag;
    using value_type        = std::byte;
    using difference_type   = std::ptrdiff_t;

    byte_segment_byte_iterator() = default;
    byte_segment_byte_iterator(typename Segments::const_iterator current, typename Segments::const_iterator end)
        : current_(current), end_(end) {
        skip_empty_segments();
    }

    [[nodiscard]] std::byte operator*() const noexcept { return current_->bytes()[offset_]; }

    byte_segment_byte_iterator &operator++() {
        ++offset_;
        if (offset_ == current_->size()) {
            ++current_;
            offset_ = 0;
            skip_empty_segments();
        }
        return *this;
    }

    void operator++(int) { ++(*this); }

    friend bool operator==(const byte_segment_byte_iterator &lhs, const byte_segment_byte_iterator &rhs) noexcept {
        return lhs.current_ == rhs.current_ && (lhs.current_ == lhs.end_ || lhs.offset_ == rhs.offset_);
    }

    friend bool operator!=(const byte_segment_byte_iterator &lhs, const byte_segment_byte_iterator &rhs) noexcept { return !(lhs == rhs); }

  private:
    void skip_empty_segments() noexcept {
        while (current_ != end_ && current_->empty()) {
            ++current_;
        }
    }

    typename Segments::const_iterator current_{};
    typename Segments::const_iterator end_{};
    std::size_t                       offset_{};
};

using cbor_segment_byte_iterator = byte_segment_byte_iterator<cbor_segments>;

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
    auto cursor = detail::byte_segment_byte_iterator<cbor_segments>{segments.begin(), segments.end()};
    auto end    = detail::byte_segment_byte_iterator<cbor_segments>{segments.end(), segments.end()};

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

template <typename Storage> [[nodiscard]] inline std::vector<std::byte> flatten_segments(const basic_byte_segments<Storage> &segments) {
    return segments.flatten();
}

namespace detail {

template <typename Storage> struct appender<basic_byte_segments<Storage>, false> {
    using value_type    = std::byte;
    using segments_type = basic_byte_segments<Storage>;

    void operator()(segments_type &segments, value_type value) { segments.append_owned(std::span<const std::byte>{&value, 1}); }

    template <typename... Ts> void multi_append(segments_type &segments, Ts &&...values) {
        static_assert(sizeof...(Ts) > 1, "multi_append requires at least 2 arguments, use operator() for single values");
        constexpr bool all_1_byte = ((sizeof(Ts) == 1) && ...);
        static_assert(all_1_byte, "multi_append requires all arguments to be 1 byte types");
        const std::array bytes{static_cast<value_type>(values)...};
        segments.append_owned(std::span<const std::byte>{bytes});
    }

    void operator()(segments_type &segments, std::span<const std::byte> values) { append_owned(segments, values); }

    void operator()(segments_type &segments, std::string_view value) {
        append_owned(segments, std::as_bytes(std::span{value.data(), value.size()}));
    }

    void append_owned(segments_type &segments, std::span<const std::byte> values) { segments.append_owned(values); }
    void append_borrowed(segments_type &segments, std::span<const std::byte> values) { segments.append_borrowed(values); }
};

template <typename Encoder, typename Segment> void append_segment_to_encoder(Encoder &enc, const Segment &segment) {
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

template <typename T>
concept BorrowedSpanBackedEncodedItemView =
    SpanBackedEncodedItemView<T> && std::ranges::borrowed_range<typename std::remove_cvref_t<T>::range_type>;

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
    requires(!std::is_reference_v<RawView>) && detail::BorrowedSpanBackedEncodedItemView<RawView>
constexpr void visit_encoded_segments(const RawView &view, VisitSegment &&visit_segment) {
    visit_segment(view.span());
}

template <typename RawView, typename VisitSegment>
    requires(!std::is_reference_v<RawView>) && detail::BorrowedSpanBackedEncodedItemView<RawView>
constexpr void visit_encoded_segments(RawView &&, VisitSegment &&) = delete;

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

namespace detail {

template <typename T>
concept ByteSegmentsOutputBuffer = CborSegmentOutputBuffer<T> && requires(T &segments, std::span<const std::byte> bytes) {
    { segments.size() } -> std::convertible_to<std::size_t>;
    segments.reserve_segments(std::size_t{});
    segments.append_owned(bytes);
    segments.append_borrowed(bytes);
};

template <ByteSegmentsOutputBuffer Segments> void append_owned_segment(Segments &segments, std::span<const std::byte> bytes) {
    if constexpr (requires { segments.append_owned_segment(bytes); }) {
        segments.append_owned_segment(bytes);
    } else {
        segments.append_owned(bytes);
    }
}

} // namespace detail

template <detail::ByteSegmentsOutputBuffer Segments>
inline void encode_bstr_segments_into(Segments &segments, std::span<const std::byte> payload) {
    const auto initial_size = segments.size();
    segments.reserve_segments(segments.size() + 2U);
    const auto header = detail::encode_cbor_major_argument_header(payload.size(), std::byte{0x40});
    if (initial_size == 0U) {
        segments.append_owned(header.span());
    } else {
        detail::append_owned_segment(segments, header.span());
    }
    segments.append_borrowed(payload);
}

[[nodiscard]] inline cbor_segments encode_bstr_segments(std::span<const std::byte> payload) {
    cbor_segments segments;
    encode_bstr_segments_into(segments, payload);
    return segments;
}

template <detail::ByteSegmentsOutputBuffer Segments>
inline void encode_indefinite_bstr_segments_into(Segments &segments, std::span<const std::byte> payload, std::size_t chunk_size = 4096) {
    if (chunk_size == 0) {
        throw std::invalid_argument("CBOR indefinite byte string chunk size must be greater than zero");
    }

    const auto chunk_count         = payload.empty() ? std::size_t{0} : ((payload.size() + chunk_size - 1U) / chunk_size);
    const auto initial_size        = segments.size();
    auto       first_owned_segment = true;
    segments.reserve_segments(segments.size() + 2U + (chunk_count * 2U));
    const auto payload_begin = reinterpret_cast<std::uintptr_t>(payload.data());
    const auto payload_end   = payload_begin + payload.size();
    visit_indefinite_bstr_segments(
        payload,
        [&](std::span<const std::byte> segment) {
            const auto segment_begin = reinterpret_cast<std::uintptr_t>(segment.data());
            if (!payload.empty() && segment_begin >= payload_begin && segment_begin < payload_end) {
                segments.append_borrowed(segment);
            } else {
                if (first_owned_segment && initial_size != 0U) {
                    detail::append_owned_segment(segments, segment);
                } else {
                    segments.append_owned(segment);
                }
                first_owned_segment = false;
            }
        },
        chunk_size);
}

[[nodiscard]] inline cbor_segments encode_indefinite_bstr_segments(std::span<const std::byte> payload, std::size_t chunk_size = 4096) {
    cbor_segments segments;
    encode_indefinite_bstr_segments_into(segments, payload, chunk_size);
    return segments;
}

template <detail::ByteSegmentsOutputBuffer Segments>
inline void encode_tagged_bstr_segments_into(Segments &segments, std::uint64_t tag, std::span<const std::byte> payload) {
    const auto initial_size = segments.size();
    segments.reserve_segments(segments.size() + 3U);
    const auto tag_header = detail::encode_cbor_major_argument_header(tag, std::byte{0xC0});
    if (initial_size == 0U) {
        segments.append_owned(tag_header.span());
    } else {
        detail::append_owned_segment(segments, tag_header.span());
    }
    segments.append_owned(detail::encode_cbor_major_argument_header(payload.size(), std::byte{0x40}).span());
    segments.append_borrowed(payload);
}

[[nodiscard]] inline cbor_segments encode_tagged_bstr_segments(std::uint64_t tag, std::span<const std::byte> payload) {
    cbor_segments segments;
    encode_tagged_bstr_segments_into(segments, tag, payload);
    return segments;
}

template <std::uint64_t Tag>
[[nodiscard]] inline cbor_segments encode_tagged_bstr_segments(static_tag<Tag>, std::span<const std::byte> payload) {
    return encode_tagged_bstr_segments(Tag, payload);
}

template <detail::ByteSegmentsOutputBuffer Segments, std::uint64_t Tag>
inline void encode_tagged_bstr_segments_into(Segments &segments, static_tag<Tag>, std::span<const std::byte> payload) {
    encode_tagged_bstr_segments_into(segments, Tag, payload);
}

template <IsUnsigned Tag>
[[nodiscard]] inline cbor_segments encode_tagged_bstr_segments(dynamic_tag<Tag> tag, std::span<const std::byte> payload) {
    return encode_tagged_bstr_segments(static_cast<std::uint64_t>(tag.cbor_tag), payload);
}

template <detail::ByteSegmentsOutputBuffer Segments, IsUnsigned Tag>
inline void encode_tagged_bstr_segments_into(Segments &segments, dynamic_tag<Tag> tag, std::span<const std::byte> payload) {
    encode_tagged_bstr_segments_into(segments, static_cast<std::uint64_t>(tag.cbor_tag), payload);
}

template <typename RawView>
    requires(!std::is_reference_v<RawView>) && detail::BorrowedSpanBackedEncodedItemView<RawView>
[[nodiscard]] inline cbor_segments borrow_segments(const RawView &view) {
    cbor_segments segments;
    segments.reserve_segments(1);
    segments.append_borrowed(view.span());
    return segments;
}

template <typename RawView>
    requires(!std::is_reference_v<RawView>) && detail::BorrowedSpanBackedEncodedItemView<RawView>
[[nodiscard]] inline cbor_segments borrow_segments(RawView &&) = delete;

template <typename RawView>
    requires IsEncodedItemView<RawView>
[[nodiscard]] inline cbor_segments copy_segments(const RawView &view) {
    std::vector<std::byte> bytes;
    bytes.reserve(view.size());
    for (auto byte : view.bytes()) {
        bytes.push_back(static_cast<std::byte>(byte));
    }

    cbor_segments segments;
    segments.reserve_segments(1);
    segments.append_owned(bytes);
    return segments;
}

template <typename RawView>
    requires IsEncodedItemView<RawView>
[[nodiscard]] inline cbor_segments to_segments(const RawView &view) {
    return copy_segments(view);
}

template <typename RawView>
    requires IsEncodedItemView<RawView>
[[nodiscard]] inline cbor_segments as_segments(const RawView &view) {
    return to_segments(view);
}

template <typename RawView>
    requires(!std::is_reference_v<RawView>) && detail::BorrowedSpanBackedEncodedItemView<RawView>
[[nodiscard]] inline cbor_segments encode_encoded_segments(const RawView &view) {
    return borrow_segments(view);
}

template <typename RawView>
    requires(!std::is_reference_v<RawView>) && detail::BorrowedSpanBackedEncodedItemView<RawView>
[[nodiscard]] inline cbor_segments encode_encoded_segments(RawView &&) = delete;

template <typename RawView>
    requires IsEncodedItemView<RawView>
[[nodiscard]] inline cbor_segments encode_encoded_segments_copy(const RawView &view) {
    return copy_segments(view);
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
