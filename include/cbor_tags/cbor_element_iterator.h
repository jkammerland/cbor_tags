#pragma once

#include <cstdint>
#include <ranges>
#include <variant>
#include <optional>
#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_decoder.h"

namespace cbor::tags {

// A variant that can hold any CBOR element for skipping/iteration
using cbor_any = std::variant<
    std::uint64_t,           // Unsigned
    negative,                // Negative  
    as_bstr_any,            // Byte string (skip)
    as_text_any,            // Text string (skip)
    as_array_any,           // Array header
    as_map_any,             // Map header
    as_tag_any,             // Tag
    bool,                   // Boolean
    std::nullptr_t,         // Null
    float16_t,              // Float16
    float,                  // Float32
    double,                 // Float64
    simple                  // Simple value
>;

// Iterator using the decoder to traverse elements
template <typename Buffer>
class cbor_element_iterator {
public:
    using difference_type = std::ptrdiff_t;
    using value_type = typename Buffer::size_type;  // Position in buffer
    using reference = value_type;
    using pointer = void;
    using iterator_category = std::forward_iterator_tag;
    
    cbor_element_iterator() = default;
    
    cbor_element_iterator(const Buffer& buffer, typename Buffer::size_type pos = 0)
        : buffer_(&buffer), decoder_(buffer), start_pos_(pos) {
        decoder_.reader_.position_ = pos;
        if (!at_end()) {
            advance();
        }
    }
    
    value_type operator*() const {
        return start_pos_;
    }
    
    cbor_element_iterator& operator++() {
        if (!at_end()) {
            advance();
        }
        return *this;
    }
    
    cbor_element_iterator operator++(int) {
        auto tmp = *this;
        ++(*this);
        return tmp;
    }
    
    bool operator==(const cbor_element_iterator& other) const {
        if (!buffer_ && !other.buffer_) return true;
        if (!buffer_ || !other.buffer_) return false;
        return start_pos_ == other.start_pos_;
    }
    
private:
    const Buffer* buffer_ = nullptr;
    mutable decoder<Buffer, Options<default_expected, default_wrapping>, cbor_header_decoder> decoder_{*buffer_};
    typename Buffer::size_type start_pos_ = 0;
    
    bool at_end() const {
        return !buffer_ || decoder_.reader_.empty(*buffer_);
    }
    
    void advance() {
        start_pos_ = decoder_.reader_.position_;
        
        // Use variant to decode/skip any CBOR element
        cbor_any element;
        auto result = decoder_.decode(element);
        
        // For compound types, skip their contents
        if (auto* arr = std::get_if<as_array_any>(&element)) {
            for (std::uint64_t i = 0; i < arr->size; ++i) {
                cbor_any item;
                decoder_.decode(item);
            }
        } else if (auto* map = std::get_if<as_map_any>(&element)) {
            for (std::uint64_t i = 0; i < map->size * 2; ++i) {
                cbor_any item;
                decoder_.decode(item);
            }
        } else if (auto* tag = std::get_if<as_tag_any>(&element)) {
            cbor_any tagged_content;
            decoder_.decode(tagged_content);
        }
    }
};

// Simple view that uses the decoder for iteration
template <typename Buffer>
    requires ValidCborBuffer<Buffer>
class cbor_view : public std::ranges::view_interface<cbor_view<Buffer>> {
public:
    using iterator = cbor_element_iterator<Buffer>;
    using sentinel = cbor_element_iterator<Buffer>;
    
    explicit cbor_view(const Buffer& buffer) : buffer_(buffer) {}
    
    iterator begin() const { return iterator(buffer_, 0); }
    sentinel end() const { return iterator(); }
    
    // Get a decoder positioned at a specific element
    auto decoder_at(typename Buffer::size_type pos) const {
        decoder<Buffer, Options<default_expected, default_wrapping>, cbor_header_decoder> dec(buffer_);
        dec.reader_.position_ = pos;
        return dec;
    }
    
private:
    const Buffer& buffer_;
};

// Factory function
template <typename Buffer>
auto make_cbor_view(const Buffer& buffer) {
    return cbor_view<Buffer>(buffer);
}

} // namespace cbor::tags