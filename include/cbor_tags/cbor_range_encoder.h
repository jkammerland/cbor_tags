#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_detail.h"
#include "cbor_tags/cbor_raw_view_encoder.h"

#include <cstddef>
#include <cstdint>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cbor::tags {

template <typename T> struct cbor_range_encoder : cbor_raw_view_encoder<T> {
    using cbor_raw_view_encoder<T>::encode;
    using cbor_raw_view_encoder<T>::encode_encoded_view;

    template <typename Byte> static constexpr auto output_byte(Byte value) { return static_cast<typename T::byte_type>(value); }

  private:
    struct container_range_markers {
        std::byte definite_major;
        std::byte indefinite_start;
    };

    template <typename R, typename WriteItem>
    constexpr void encode_container_range(R &&range, container_range_markers markers, WriteItem &&write_item) {
        auto &enc = detail::underlying<T>(this);
        if constexpr (std::ranges::sized_range<R>) {
            enc.encode_major_and_size(static_cast<std::uint64_t>(std::ranges::size(range)),
                                      static_cast<typename T::byte_type>(markers.definite_major));
        } else {
            enc.appender_(enc.data_, static_cast<typename T::byte_type>(markers.indefinite_start));
        }

        for (auto &&item : range) {
            write_item(enc, std::forward<decltype(item)>(item));
        }

        if constexpr (!std::ranges::sized_range<R>) {
            enc.appender_(enc.data_, static_cast<typename T::byte_type>(0xFF));
        }
    }

    template <typename R, typename Item> constexpr void encode_array_range_item(Item &&item) {
        auto &enc = detail::underlying<T>(this);
        if constexpr (detail::CborRangeComponent<Item>) {
            enc.encode(std::forward<Item>(item));
        } else {
            using value_type = std::ranges::range_value_t<std::remove_reference_t<R>>;
            static_assert(detail::MaterializableCborRangeComponent<Item, value_type>);
            enc.encode(static_cast<value_type>(std::forward<Item>(item)));
        }
    }

  public:
    template <typename R> constexpr void encode_array_range(R &&range) {
        auto *self = this;
        encode_container_range(
            std::forward<R>(range),
            container_range_markers{.definite_major = std::byte{0x80}, .indefinite_start = get_indefinite_start<as_indefinite_array>()},
            [self]<typename Enc, typename Item>(Enc &, Item &&item) {
                self->template encode_array_range_item<R>(std::forward<Item>(item));
            });
    }

    template <typename R> constexpr void encode(array_range<R> &value) { encode_array_range(value.range_); }
    template <typename R> constexpr void encode(array_range<R> &&value) { encode_array_range(value.range_); }
    template <typename R>
        requires std::ranges::range<const R>
    constexpr void encode(const array_range<R> &value) {
        encode_array_range(value.range_);
    }

    template <typename R> constexpr void encode_map_range(R &&range) {
        encode_container_range(
            std::forward<R>(range),
            container_range_markers{.definite_major = std::byte{0xA0}, .indefinite_start = get_indefinite_start<as_indefinite_map>()},
            [](auto &enc, auto &&entry) {
                enc.encode(detail::pair_first(entry));
                enc.encode(detail::pair_second(entry));
            });
    }

    template <typename R> constexpr void encode(map_range<R> &value) { encode_map_range(value.range_); }
    template <typename R> constexpr void encode(map_range<R> &&value) { encode_map_range(value.range_); }
    template <typename R>
        requires std::ranges::range<const R>
    constexpr void encode(const map_range<R> &value) {
        encode_map_range(value.range_);
    }

    template <typename R> constexpr void encode_bstr_range(R &&range, std::size_t chunk_size) {
        auto &enc = detail::underlying<T>(this);
        if (chunk_size == 0) {
            throw std::runtime_error("CBOR byte string chunk size must be greater than zero");
        }
        if constexpr (std::ranges::sized_range<R>) {
            enc.encode_major_and_size(static_cast<std::uint64_t>(std::ranges::size(range)), static_cast<typename T::byte_type>(0x40));
            detail::append_byte_range(enc.appender_, enc.data_, range);
        } else {
            enc.appender_(enc.data_, static_cast<typename T::byte_type>(get_indefinite_start<as_indefinite_byte_string>()));
            std::vector<typename T::byte_type> chunk;
            chunk.reserve(chunk_size);
            auto flush_chunk = [&] {
                if (chunk.empty()) {
                    return;
                }
                enc.encode_major_and_size(static_cast<std::uint64_t>(chunk.size()), static_cast<typename T::byte_type>(0x40));
                detail::append_byte_range(enc.appender_, enc.data_, chunk);
                chunk.clear();
            };

            for (auto byte : range) {
                chunk.push_back(output_byte(byte));
                if (chunk.size() == chunk_size) {
                    flush_chunk();
                }
            }
            flush_chunk();
            enc.appender_(enc.data_, static_cast<typename T::byte_type>(0xFF));
        }
    }

    template <typename R> constexpr void encode(bstr_range<R> &value) { encode_bstr_range(value.range_, value.chunk_size_); }
    template <typename R> constexpr void encode(bstr_range<R> &&value) { encode_bstr_range(value.range_, value.chunk_size_); }
    template <typename R>
        requires std::ranges::range<const R>
    constexpr void encode(const bstr_range<R> &value) {
        encode_bstr_range(value.range_, value.chunk_size_);
    }
};

} // namespace cbor::tags
