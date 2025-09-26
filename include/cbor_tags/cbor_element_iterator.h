#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_decoder.h"

#include <cstdint>
#include <optional>
#include <ranges>
#include <variant>

namespace cbor::tags {

// Iterator using the decoder to traverse elements
template <typename Buffer> class cbor_element_iterator {
  public:
    using difference_type   = std::ptrdiff_t;
    using value_type        = typename Buffer::size_type;
    using reference         = value_type;
    using pointer           = void;
    using iterator_category = std::forward_iterator_tag;
    using iterator_concept  = std::forward_iterator_tag;

    cbor_element_iterator() = default;

    cbor_element_iterator(const Buffer *buffer, typename Buffer::size_type pos = 0) : buffer_(buffer), start_pos_(pos), current_pos_(pos) {
        if (buffer_) {
            at_end_ = check_at_end();
            if (!at_end_) {
                advance();
            }
        }
    }

    value_type operator*() const { return start_pos_; }

    cbor_element_iterator &operator++() {
        if (!at_end_) {
            advance();
        }
        return *this;
    }

    cbor_element_iterator operator++(int) {
        auto tmp = *this;
        ++(*this);
        return tmp;
    }

    bool operator==(const cbor_element_iterator &other) const {
        if (!buffer_ && !other.buffer_)
            return true;
        if (!buffer_ || !other.buffer_)
            return false;
        return start_pos_ == other.start_pos_;
    }

    bool is_at_end() const { return at_end_; }

  private:
    const Buffer              *buffer_      = nullptr;
    typename Buffer::size_type start_pos_   = 0;
    typename Buffer::size_type current_pos_ = 0;
    bool                       at_end_      = false;

    bool check_at_end() const {
        if (!buffer_)
            return true;
        detail::reader<Buffer> reader(*buffer_);
        reader.position_ = current_pos_;
        return reader.empty(*buffer_);
    }

    void advance() {
        if (at_end_)
            return;

        start_pos_ = current_pos_;

        // Create a decoder for this operation
        auto dec              = make_decoder(*buffer_);
        dec.reader_.position_ = current_pos_;

        // Use variant to decode/skip any CBOR element
        cbor_any              element;
        [[maybe_unused]] auto result = dec.decode(element);

        // For compound types, skip their contents
        if (auto *arr = std::get_if<as_array_any>(&element)) {
            for (std::uint64_t i = 0; i < arr->size; ++i) {
                cbor_any item;
                dec.decode(item);
            }
        } else if (auto *map = std::get_if<as_map_any>(&element)) {
            for (std::uint64_t i = 0; i < map->size * 2; ++i) {
                cbor_any item;
                dec.decode(item);
            }
        } else if ([[maybe_unused]] auto *tag = std::get_if<as_tag_any>(&element)) {
            cbor_any tagged_content;
            dec.decode(tagged_content);
        }

        // Update position for next iteration
        current_pos_ = dec.reader_.position_;
        at_end_      = check_at_end();
    }
};

// Simple view that uses the decoder for iteration
template <typename Buffer>
    requires ValidCborBuffer<Buffer>
class cbor_view : public std::ranges::view_interface<cbor_view<Buffer>> {
  public:
    using iterator       = cbor_element_iterator<Buffer>;
    using const_iterator = iterator;
    using sentinel       = std::default_sentinel_t;

    explicit cbor_view(const Buffer &buffer) : buffer_(&buffer) {}

    iterator begin() const { return iterator(buffer_, 0); }

    sentinel end() const { return std::default_sentinel; }

    // Get a decoder positioned at a specific element
    auto decoder_at(typename Buffer::size_type pos) const {
        auto dec              = make_decoder(*buffer_);
        dec.reader_.position_ = pos;
        return dec;
    }

  private:
    const Buffer *buffer_;
};

// Specialize sentinel comparison
template <typename Buffer> bool operator==(const cbor_element_iterator<Buffer> &it, std::default_sentinel_t) { return it.is_at_end(); }

template <typename Buffer> bool operator==(std::default_sentinel_t, const cbor_element_iterator<Buffer> &it) { return it.is_at_end(); }

// Factory function
template <typename Buffer> auto make_cbor_view(const Buffer &buffer) { return cbor_view<Buffer>(buffer); }

// Range adaptor for pipeline syntax
namespace views {
inline constexpr auto cbor_elements = []<typename Buffer>(const Buffer &buffer) { return make_cbor_view(buffer); };
} // namespace views

} // namespace cbor::tags