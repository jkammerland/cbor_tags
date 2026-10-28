#pragma once

#include <algorithm>
#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory_resource>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace resumable_bench {

namespace ct = cbor::tags;
using bytes  = std::vector<std::byte>;

inline void require(bool condition, std::string_view description) {
    if (!condition) {
        throw std::runtime_error(std::string{description});
    }
}

inline std::size_t parse_size(std::string_view argument) {
    std::size_t value{};
    const auto [end, error] = std::from_chars(argument.data(), argument.data() + argument.size(), value);
    require(error == std::errc{} && end == argument.data() + argument.size() && value != 0, "expected a positive integer");
    return value;
}

inline std::size_t header_size(std::size_t value) {
    return value < 24 ? 1 : value <= 0xff ? 2 : value <= 0xffff ? 3 : value <= 0xffffffffULL ? 5 : 9;
}

// CBOR string headers leave a few impossible total sizes at width transitions.
inline std::optional<std::size_t> payload_size(std::size_t encoded_size) {
    for (const auto header : {1U, 2U, 3U, 5U, 9U}) {
        if (encoded_size >= header && header_size(encoded_size - header) == header) {
            return encoded_size - header;
        }
    }
    return std::nullopt;
}

inline std::uint32_t integer_at(std::size_t position) {
    constexpr std::array<std::uint32_t, 8> values{0, 23, 24, 255, 256, 65535, 65536, 0xffffffffU};
    return values[position % values.size()];
}

inline bytes make_binary(std::size_t encoded_size) {
    const auto size = payload_size(encoded_size);
    require(size.has_value(), "requested size cannot be represented by one definite bstr");
    bytes result(*size);
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i] = static_cast<std::byte>((i * 37U + i / 251U) % 256U);
    }
    return result;
}

inline std::string make_text(std::size_t encoded_size) {
    const auto size = payload_size(encoded_size);
    require(size.has_value(), "requested size cannot be represented by one definite tstr");
    std::string result(*size, ' ');
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i] = static_cast<char>('a' + (i * 7U + i / 29U) % 26U);
    }
    return result;
}

struct record {
    std::string                name;
    std::vector<std::uint32_t> values;
    bool                       operator==(const record &) const = default;
};

struct records {
    std::vector<record> items;
    bytes               padding;
    bool                operator==(const records &) const = default;
};

inline records make_records(std::size_t encoded_size) {
    require(encoded_size >= 8192, "structured fixture requires at least 8192 encoded bytes");

    record      prototype{make_text(4099), std::vector<std::uint32_t>(64)};
    std::size_t record_size = 1 + 4099 + header_size(prototype.values.size());
    for (std::size_t i = 0; i < prototype.values.size(); ++i) {
        prototype.values[i] = integer_at(i);
        record_size += header_size(prototype.values[i]);
    }

    // Leave a small bstr to make the whole aggregate exactly the requested wire size.
    std::size_t                count = (encoded_size - 16) / record_size;
    std::optional<std::size_t> padding;
    for (;;) {
        padding = payload_size(encoded_size - 1 - header_size(count) - count * record_size);
        if (padding) {
            break;
        }
        require(count != 0, "requested size cannot be represented by the structured fixture");
        --count;
    }

    records result;
    result.items.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto item = prototype;
        // Vary records without changing either their integer widths or encoded size.
        const auto shift = static_cast<std::ptrdiff_t>(i % 8U);
        std::rotate(item.values.begin(), item.values.begin() + shift, item.values.end());
        item.name.front() = static_cast<char>('A' + i % 26U);
        result.items.push_back(std::move(item));
    }
    result.padding.resize(*padding, std::byte{0x5a});
    return result;
}

inline std::vector<std::uint32_t> make_scalars(std::size_t approximate_encoded_size) {
    std::vector<std::uint32_t> result(std::max<std::size_t>(8, approximate_encoded_size / 3));
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i] = integer_at(i);
    }
    return result;
}

// Distinct keys preserve every pair and make ordered/hash maps comparable.
template <typename Map> Map make_integer_map(std::size_t approximate_encoded_size) {
    Map        result;
    const auto count = std::max<std::size_t>(8, approximate_encoded_size / 12);
    require(count <= std::numeric_limits<std::uint32_t>::max(), "map fixture exceeds uint32 keys");
    if constexpr (requires { result.reserve(count); }) {
        result.reserve(count);
    }
    for (std::size_t i = 0; i < count; ++i) {
        result.emplace(static_cast<std::uint32_t>(i), integer_at(i));
    }
    return result;
}

// Build wire chunks through the ordinary encoder; transport splitting is independent.
template <typename String> bytes encode_string_chunks(const String &value, std::size_t chunk_size) {
    bytes encoded;
    auto  encoder = ct::make_encoder(encoded);
    if constexpr (ct::IsTextString<String>) {
        encoded.push_back(std::byte{0x7f});
    } else {
        encoded.push_back(std::byte{0x5f});
    }
    std::size_t expected = 2;
    for (std::size_t offset = 0; offset < value.size(); offset += chunk_size) {
        const auto count = std::min(chunk_size, value.size() - offset);
        expected += header_size(count) + count;
        if constexpr (ct::IsTextString<String>) {
            require(static_cast<bool>(encoder(std::string_view{value}.substr(offset, count))), "text chunk failed");
        } else {
            require(static_cast<bool>(encoder(std::span<const std::byte>{value}.subspan(offset, count))), "binary chunk failed");
        }
    }
    encoded.push_back(std::byte{0xff});
    require(encoded.size() == expected, "indefinite string byte count mismatch");
    return encoded;
}

class allocation_counter : public std::pmr::memory_resource {
  public:
    std::size_t allocations{};
    std::size_t allocated_bytes{};

  private:
    void *do_allocate(std::size_t size, std::size_t alignment) override {
        ++allocations;
        allocated_bytes += size;
        return std::pmr::new_delete_resource()->allocate(size, alignment);
    }
    void do_deallocate(void *pointer, std::size_t size, std::size_t alignment) override {
        std::pmr::new_delete_resource()->deallocate(pointer, size, alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override { return this == &other; }
};

template <template <typename> typename... Codecs, typename T>
bytes encode_fixture(const T &value, std::optional<std::size_t> exact_size = {}) {
    bytes encoded;
    if (exact_size) {
        encoded.reserve(*exact_size);
    }
    require(static_cast<bool>(ct::make_encoder<Codecs...>(encoded)(value)), "fixture encoding failed");
    require(!exact_size || encoded.size() == *exact_size, "fixture encoded size does not match independent size calculation");
    return encoded;
}

template <typename T> void check_fixture(const T &value, const bytes &encoded) {
    T    decoded;
    auto decoder = ct::make_decoder(encoded);
    require(static_cast<bool>(decoder(decoded)), "fixture decoding failed");
    require(decoded == value && decoder.tell() == encoded.end(), "fixture roundtrip mismatch or trailing bytes");
}

} // namespace resumable_bench
