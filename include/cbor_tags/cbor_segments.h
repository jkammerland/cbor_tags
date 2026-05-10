#pragma once

#include "cbor_tags/cbor_raw_views.h"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
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

    [[nodiscard]] static cbor_segment borrowed(std::span<const std::byte> bytes) {
        cbor_segment segment;
        segment.kind_     = cbor_segment_kind::borrowed;
        segment.borrowed_ = bytes;
        return segment;
    }

    [[nodiscard]] cbor_segment_kind kind() const noexcept { return kind_; }
    [[nodiscard]] bool              is_owned() const noexcept { return kind_ == cbor_segment_kind::owned; }
    [[nodiscard]] bool              is_borrowed() const noexcept { return kind_ == cbor_segment_kind::borrowed; }

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
    using container_type  = std::vector<cbor_segment>;
    using const_iterator  = container_type::const_iterator;
    using const_reference = container_type::const_reference;

    void reserve(std::size_t count) { segments_.reserve(count); }

    void append_owned(std::span<const std::byte> bytes) { segments_.push_back(cbor_segment::owned(bytes)); }
    void append_owned(std::initializer_list<std::byte> bytes) { segments_.push_back(cbor_segment::owned(bytes)); }
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

[[nodiscard]] inline std::vector<std::byte> flatten_segments(const cbor_segments &segments) { return segments.flatten(); }

namespace detail {

struct cbor_segment_header {
    std::array<std::byte, 9> bytes{};
    std::size_t              size{};

    [[nodiscard]] std::span<const std::byte> span() const noexcept { return {bytes.data(), size}; }
};

[[nodiscard]] constexpr std::byte segment_byte(std::uint64_t value) noexcept {
    return static_cast<std::byte>(static_cast<std::uint8_t>(value));
}

[[nodiscard]] constexpr cbor_segment_header encode_segment_major_and_size(std::uint64_t value, std::byte major_type) noexcept {
    cbor_segment_header header;
    const auto          major = static_cast<std::uint8_t>(major_type);
    auto                push  = [&](std::byte byte) { header.bytes[header.size++] = byte; };

    if (value < 24) {
        push(static_cast<std::byte>(static_cast<std::uint8_t>(major | static_cast<std::uint8_t>(value))));
    } else if (value <= 0xFFU) {
        push(static_cast<std::byte>(static_cast<std::uint8_t>(major | 24U)));
        push(segment_byte(value));
    } else if (value <= 0xFFFFU) {
        push(static_cast<std::byte>(static_cast<std::uint8_t>(major | 25U)));
        push(segment_byte(value >> 8U));
        push(segment_byte(value));
    } else if (value <= 0xFFFFFFFFULL) {
        push(static_cast<std::byte>(static_cast<std::uint8_t>(major | 26U)));
        push(segment_byte(value >> 24U));
        push(segment_byte(value >> 16U));
        push(segment_byte(value >> 8U));
        push(segment_byte(value));
    } else {
        push(static_cast<std::byte>(static_cast<std::uint8_t>(major | 27U)));
        push(segment_byte(value >> 56U));
        push(segment_byte(value >> 48U));
        push(segment_byte(value >> 40U));
        push(segment_byte(value >> 32U));
        push(segment_byte(value >> 24U));
        push(segment_byte(value >> 16U));
        push(segment_byte(value >> 8U));
        push(segment_byte(value));
    }
    return header;
}

template <typename T>
concept SpanBackedEncodedItemView =
    IsEncodedItemView<T> && requires { typename std::remove_cvref_t<T>::range_type; } &&
    std::ranges::contiguous_range<const typename std::remove_cvref_t<T>::range_type> &&
    std::ranges::sized_range<const typename std::remove_cvref_t<T>::range_type> && requires(const std::remove_cvref_t<T> &value) {
        { value.span() } -> std::same_as<std::optional<std::span<const std::byte>>>;
    };

} // namespace detail

[[nodiscard]] inline cbor_segments encode_bstr_segments(std::span<const std::byte> payload) {
    cbor_segments segments;
    const auto    header = detail::encode_segment_major_and_size(payload.size(), std::byte{0x40});
    segments.reserve(2);
    segments.append_owned(header.span());
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
    segments.append_owned({std::byte{0x5F}});

    for (std::size_t offset = 0; offset < payload.size(); offset += chunk_size) {
        const auto chunk_length = std::min(chunk_size, payload.size() - offset);
        const auto header       = detail::encode_segment_major_and_size(chunk_length, std::byte{0x40});
        segments.append_owned(header.span());
        segments.append_borrowed(payload.subspan(offset, chunk_length));
    }

    segments.append_owned({std::byte{0xFF}});
    return segments;
}

[[nodiscard]] inline cbor_segments encode_tagged_bstr_segments(std::uint64_t tag, std::span<const std::byte> payload) {
    cbor_segments segments;
    const auto    tag_header  = detail::encode_segment_major_and_size(tag, std::byte{0xC0});
    const auto    bstr_header = detail::encode_segment_major_and_size(payload.size(), std::byte{0x40});
    segments.reserve(3);
    segments.append_owned(tag_header.span());
    segments.append_owned(bstr_header.span());
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
    segments.append_borrowed(*bytes);
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
