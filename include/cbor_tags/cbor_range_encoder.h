#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_detail.h"

#include <cstddef>
#include <cstdint>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cbor::tags {

template <typename T> struct cbor_range_encoder {
    template <typename Byte> static constexpr auto output_byte(Byte value) { return static_cast<typename T::byte_type>(value); }

    template <typename R> constexpr void encode_array_range(R &&range) {
        auto &enc = detail::underlying<T>(this);
        if constexpr (std::ranges::sized_range<R>) {
            enc.encode_major_and_size(static_cast<std::uint64_t>(std::ranges::size(range)), static_cast<typename T::byte_type>(0x80));
        } else {
            enc.appender_(enc.data_, static_cast<typename T::byte_type>(get_indefinite_start<as_indefinite_array>()));
        }

        for (auto &&item : range) {
            enc.encode(std::forward<decltype(item)>(item));
        }

        if constexpr (!std::ranges::sized_range<R>) {
            enc.appender_(enc.data_, static_cast<typename T::byte_type>(0xFF));
        }
    }

    template <typename R> constexpr void encode(array_range<R> &value) { encode_array_range(value.range_); }
    template <typename R> constexpr void encode(array_range<R> &&value) { encode_array_range(value.range_); }
    template <typename R>
        requires std::ranges::range<const R>
    constexpr void encode(const array_range<R> &value) {
        encode_array_range(value.range_);
    }

    template <typename R> constexpr void encode_map_range(R &&range) {
        auto &enc = detail::underlying<T>(this);
        if constexpr (std::ranges::sized_range<R>) {
            enc.encode_major_and_size(static_cast<std::uint64_t>(std::ranges::size(range)), static_cast<typename T::byte_type>(0xA0));
        } else {
            enc.appender_(enc.data_, static_cast<typename T::byte_type>(get_indefinite_start<as_indefinite_map>()));
        }

        for (auto &&entry : range) {
            enc.encode(detail::pair_first(entry));
            enc.encode(detail::pair_second(entry));
        }

        if constexpr (!std::ranges::sized_range<R>) {
            enc.appender_(enc.data_, static_cast<typename T::byte_type>(0xFF));
        }
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
        if constexpr (std::ranges::sized_range<R>) {
            enc.encode_major_and_size(static_cast<std::uint64_t>(std::ranges::size(range)), static_cast<typename T::byte_type>(0x40));
            for (auto byte : range) {
                enc.appender_(enc.data_, output_byte(byte));
            }
        } else {
            if (chunk_size == 0) {
                throw std::runtime_error("CBOR byte string chunk size must be greater than zero");
            }

            enc.appender_(enc.data_, static_cast<typename T::byte_type>(get_indefinite_start<as_indefinite_byte_string>()));
            std::vector<typename T::byte_type> chunk;
            chunk.reserve(chunk_size);
            auto flush_chunk = [&] {
                if (chunk.empty()) {
                    return;
                }
                enc.encode_major_and_size(static_cast<std::uint64_t>(chunk.size()), static_cast<typename T::byte_type>(0x40));
                for (auto byte : chunk) {
                    enc.appender_(enc.data_, byte);
                }
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
