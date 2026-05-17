#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_extensions.h"
#include "cbor_tags/cbor_segments.h"

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace cbor::tags::ext::rfc8746 {

enum class typed_array_byte_order { little, big };

template <typename T, typed_array_byte_order ByteOrder = typed_array_byte_order::little> struct typed_array_traits;

template <> struct typed_array_traits<std::int32_t, typed_array_byte_order::little> {
    using bit_type                     = std::uint32_t;
    static constexpr std::uint64_t tag = 78;
};

template <> struct typed_array_traits<std::int64_t, typed_array_byte_order::little> {
    using bit_type                     = std::uint64_t;
    static constexpr std::uint64_t tag = 79;
};

template <> struct typed_array_traits<float16_t, typed_array_byte_order::little> {
    using bit_type                     = std::uint16_t;
    static constexpr std::uint64_t tag = 84;
};

template <> struct typed_array_traits<float, typed_array_byte_order::little> {
    using bit_type                     = std::uint32_t;
    static constexpr std::uint64_t tag = 85;
};

template <> struct typed_array_traits<double, typed_array_byte_order::little> {
    using bit_type                     = std::uint64_t;
    static constexpr std::uint64_t tag = 86;
};

template <> struct typed_array_traits<float, typed_array_byte_order::big> {
    using bit_type                     = std::uint32_t;
    static constexpr std::uint64_t tag = 81;
};

template <> struct typed_array_traits<double, typed_array_byte_order::big> {
    using bit_type                     = std::uint64_t;
    static constexpr std::uint64_t tag = 82;
};

template <typename T, typed_array_byte_order ByteOrder>
concept IsTypedArrayElementFor = requires {
    typename typed_array_traits<std::remove_cv_t<T>, ByteOrder>::bit_type;
    { typed_array_traits<std::remove_cv_t<T>, ByteOrder>::tag } -> std::convertible_to<std::uint64_t>;
};

template <typename T>
concept IsTypedArrayElement = IsTypedArrayElementFor<T, typed_array_byte_order::little>;

namespace detail {

template <typename R>
concept TypedArrayPayloadRange =
    std::ranges::view<R> && std::copy_constructible<R> && std::ranges::forward_range<const R> && cbor::tags::detail::ByteLikeRange<const R>;

template <typename Decoder, typename R>
concept DecodableTypedArrayPayloadRange =
    TypedArrayPayloadRange<R> && ((std::ranges::contiguous_range<const R> && !IsContiguous<typename Decoder::input_buffer_type>) ||
                                  requires(Decoder &dec, std::uint64_t length) { R{dec.decode_bstring_payload(length)}; });

template <typed_array_byte_order ByteOrder>
inline constexpr bool native_matches_byte_order =
    (std::endian::native == std::endian::little && ByteOrder == typed_array_byte_order::little) ||
    (std::endian::native == std::endian::big && ByteOrder == typed_array_byte_order::big);

template <typename T>
concept DirectResizableByteOutputBuffer = std::ranges::contiguous_range<T> && requires(T &buffer, std::size_t size) {
    buffer.resize(size);
    { buffer.size() } -> std::convertible_to<std::size_t>;
    std::ranges::data(buffer);
} && sizeof(std::ranges::range_value_t<T>) == 1U;

template <typename BitType> [[nodiscard]] constexpr BitType byteswap_bits(BitType value) noexcept {
    static_assert(std::is_unsigned_v<BitType>);
#if defined(__GNUC__) || defined(__clang__)
    if constexpr (sizeof(BitType) == 2U) {
        return static_cast<BitType>(__builtin_bswap16(static_cast<std::uint16_t>(value)));
    } else if constexpr (sizeof(BitType) == 4U) {
        return static_cast<BitType>(__builtin_bswap32(static_cast<std::uint32_t>(value)));
    } else if constexpr (sizeof(BitType) == 8U) {
        return static_cast<BitType>(__builtin_bswap64(static_cast<std::uint64_t>(value)));
    } else {
        return value;
    }
#else
    if constexpr (sizeof(BitType) == 2U) {
        return static_cast<BitType>(((value & BitType{0x00FFU}) << 8U) | ((value & BitType{0xFF00U}) >> 8U));
    } else if constexpr (sizeof(BitType) == 4U) {
        return static_cast<BitType>(((value & BitType{0x000000FFU}) << 24U) | ((value & BitType{0x0000FF00U}) << 8U) |
                                    ((value & BitType{0x00FF0000U}) >> 8U) | ((value & BitType{0xFF000000U}) >> 24U));
    } else if constexpr (sizeof(BitType) == 8U) {
        return static_cast<BitType>(((value & BitType{0x00000000000000FFULL}) << 56U) | ((value & BitType{0x000000000000FF00ULL}) << 40U) |
                                    ((value & BitType{0x0000000000FF0000ULL}) << 24U) | ((value & BitType{0x00000000FF000000ULL}) << 8U) |
                                    ((value & BitType{0x000000FF00000000ULL}) >> 8U) | ((value & BitType{0x0000FF0000000000ULL}) >> 24U) |
                                    ((value & BitType{0x00FF000000000000ULL}) >> 40U) | ((value & BitType{0xFF00000000000000ULL}) >> 56U));
    } else {
        return value;
    }
#endif
}

template <typed_array_byte_order ByteOrder, typename BitType> [[nodiscard]] constexpr BitType native_to_wire_bits(BitType bits) noexcept {
    if constexpr (native_matches_byte_order<ByteOrder>) {
        return bits;
    } else {
        return byteswap_bits(bits);
    }
}

template <typed_array_byte_order ByteOrder, typename BitType> [[nodiscard]] constexpr BitType wire_to_native_bits(BitType bits) noexcept {
    return native_to_wire_bits<ByteOrder>(bits);
}

template <typed_array_byte_order ByteOrder, typename T>
    requires IsTypedArrayElementFor<T, ByteOrder>
[[nodiscard]] constexpr auto to_bits(T value) noexcept {
    using value_type = std::remove_cv_t<T>;
    using bit_type   = typename typed_array_traits<value_type, ByteOrder>::bit_type;
    static_assert(sizeof(value_type) == sizeof(bit_type));
    return std::bit_cast<bit_type>(value);
}

template <typed_array_byte_order ByteOrder, typename T>
    requires IsTypedArrayElementFor<T, ByteOrder>
[[nodiscard]] constexpr T from_endian(std::span<const std::byte> bytes) noexcept {
    using value_type = std::remove_cv_t<T>;
    using bit_type   = typename typed_array_traits<value_type, ByteOrder>::bit_type;
    static_assert(sizeof(value_type) == sizeof(bit_type));

    bit_type bits{};
    for (std::size_t i = 0; i < sizeof(value_type); ++i) {
        const auto shift = ByteOrder == typed_array_byte_order::little ? i * 8U : (sizeof(value_type) - 1U - i) * 8U;
        bits |= static_cast<bit_type>(std::to_integer<std::uint8_t>(bytes[i])) << shift;
    }
    return std::bit_cast<value_type>(bits);
}

template <typed_array_byte_order ByteOrder, typename T>
    requires IsTypedArrayElementFor<T, ByteOrder>
void write_endian_payload_to(std::span<std::byte> output, std::span<const T> values) {
    using value_type = std::remove_cv_t<T>;
    using bit_type   = typename typed_array_traits<value_type, ByteOrder>::bit_type;
    static_assert(sizeof(value_type) == sizeof(bit_type));

    if (values.empty()) {
        return;
    }
    if constexpr (native_matches_byte_order<ByteOrder>) {
        std::memcpy(output.data(), values.data(), values.size_bytes());
    } else {
        auto *cursor = output.data();
        for (const auto value : values) {
            const auto native_bits = to_bits<ByteOrder>(static_cast<value_type>(value));
            const auto wire_bits   = native_to_wire_bits<ByteOrder>(native_bits);
            std::memcpy(cursor, &wire_bits, sizeof(wire_bits));
            cursor += sizeof(wire_bits);
        }
    }
}

template <typed_array_byte_order ByteOrder, typename T>
    requires IsTypedArrayElementFor<T, ByteOrder>
[[nodiscard]] std::vector<std::byte> endian_payload(std::span<const T> values) {
    std::vector<std::byte> bytes(values.size_bytes());
    write_endian_payload_to<ByteOrder>(std::span<std::byte>{bytes.data(), bytes.size()}, values);
    return bytes;
}

template <typed_array_byte_order ByteOrder, DirectResizableByteOutputBuffer OutputBuffer, typename T>
    requires IsTypedArrayElementFor<T, ByteOrder>
void append_endian_payload_to(OutputBuffer &output, std::span<const T> values) {
    const auto start        = static_cast<std::size_t>(output.size());
    const auto payload_size = static_cast<std::size_t>(values.size_bytes());
    output.resize(start + payload_size);
    auto *payload = reinterpret_cast<std::byte *>(std::ranges::data(output) + start);
    write_endian_payload_to<ByteOrder>(std::span<std::byte>{payload, payload_size}, values);
}

template <typename T, typed_array_byte_order ByteOrder, typename AssignPayload>
    requires IsTypedArrayElementFor<T, ByteOrder>
[[nodiscard]] status_code decode_payload_after_tag(auto &dec, std::uint64_t tag, AssignPayload &&assign_payload) {
    using value_type = std::remove_cv_t<T>;

    if (tag != typed_array_traits<value_type, ByteOrder>::tag) {
        return status_code::no_match_for_tag;
    }

    const auto [payload_major, payload_additional_info] = dec.read_initial_byte();
    if (payload_major != major_type::ByteString || payload_additional_info == std::byte{31}) {
        return status_code::no_match_for_bstr_on_buffer;
    }

    return std::forward<AssignPayload>(assign_payload)(payload_major, payload_additional_info);
}

template <typename T, typed_array_byte_order ByteOrder, typename AssignPayload>
    requires IsTypedArrayElementFor<T, ByteOrder>
[[nodiscard]] status_code decode_payload(auto &dec, major_type major, std::byte additional_info, AssignPayload &&assign_payload) {
    if (major != major_type::Tag) {
        return status_code::no_match_for_tag_on_buffer;
    }

    return decode_payload_after_tag<T, ByteOrder>(dec, dec.decode_unsigned(additional_info), std::forward<AssignPayload>(assign_payload));
}

template <typename T, typed_array_byte_order ByteOrder>
    requires IsTypedArrayElementFor<T, ByteOrder>
[[nodiscard]] std::vector<std::remove_cv_t<T>> copy_values(std::span<const std::byte> payload) {
    using value_type = std::remove_cv_t<T>;
    using bit_type   = typename typed_array_traits<value_type, ByteOrder>::bit_type;

    std::vector<value_type> values(payload.size() / sizeof(value_type));
    if (payload.empty()) {
        return values;
    }
    if constexpr (native_matches_byte_order<ByteOrder>) {
        std::memcpy(values.data(), payload.data(), payload.size());
    } else {
        auto *cursor = payload.data();
        for (auto &value : values) {
            bit_type wire_bits{};
            std::memcpy(&wire_bits, cursor, sizeof(wire_bits));
            const auto native_bits = wire_to_native_bits<ByteOrder>(wire_bits);
            value                  = std::bit_cast<value_type>(native_bits);
            cursor += sizeof(wire_bits);
        }
    }
    return values;
}

template <typename T, typed_array_byte_order ByteOrder, typename PayloadRange>
    requires IsTypedArrayElementFor<T, ByteOrder>
[[nodiscard]] std::vector<std::remove_cv_t<T>> materialize_values(PayloadRange &&payload, std::size_t payload_size) {
    using value_type = std::remove_cv_t<T>;

    if constexpr (native_matches_byte_order<ByteOrder> && std::ranges::contiguous_range<PayloadRange>) {
        std::vector<value_type> values(payload_size / sizeof(value_type));
        if (payload_size != 0U) {
            std::memcpy(values.data(), std::ranges::data(payload), payload_size);
        }
        return values;
    } else if constexpr (std::ranges::contiguous_range<PayloadRange>) {
        return copy_values<value_type, ByteOrder>(
            std::span<const std::byte>{reinterpret_cast<const std::byte *>(std::ranges::data(payload)), payload_size});
    } else {
        std::vector<std::byte> payload_bytes;
        payload_bytes.reserve(payload_size);
        for (auto byte_value : payload) {
            payload_bytes.push_back(static_cast<std::byte>(byte_value));
        }
        return copy_values<value_type, ByteOrder>(std::span<const std::byte>{payload_bytes});
    }
}

} // namespace detail

template <typename T, typed_array_byte_order ByteOrder = typed_array_byte_order::little>
    requires IsTypedArrayElementFor<T, ByteOrder>
class typed_array {
  public:
    using value_type                              = std::remove_cv_t<T>;
    using container_type                          = std::vector<value_type>;
    static constexpr auto          byte_order     = ByteOrder;
    static constexpr std::uint64_t cbor_array_tag = typed_array_traits<value_type, ByteOrder>::tag;
    static constexpr std::uint64_t cbor_tag       = cbor_array_tag;

    typed_array() = default;
    explicit typed_array(container_type values) : values_(std::move(values)) {}
    typed_array(std::initializer_list<value_type> values) : values_(values) {}

    [[nodiscard]] container_type             &values() noexcept { return values_; }
    [[nodiscard]] const container_type       &values() const noexcept { return values_; }
    [[nodiscard]] std::span<const value_type> span() const noexcept { return std::span<const value_type>{values_}; }

  private:
    container_type values_{};
};

template <typename T, typed_array_byte_order ByteOrder = typed_array_byte_order::little>
    requires IsTypedArrayElementFor<T, ByteOrder>
class typed_array_ref {
  public:
    using value_type                              = std::remove_cv_t<T>;
    static constexpr auto          byte_order     = ByteOrder;
    static constexpr std::uint64_t cbor_array_tag = typed_array_traits<value_type, ByteOrder>::tag;

    constexpr explicit typed_array_ref(std::span<const value_type> values) noexcept : values_(values) {}

    [[nodiscard]] constexpr std::span<const value_type> values() const noexcept { return values_; }

  private:
    std::span<const value_type> values_{};
};

template <IsTypedArrayElement T> [[nodiscard]] constexpr auto as_typed_array(std::span<const T> values) noexcept {
    return typed_array_ref<std::remove_cv_t<T>>{std::span<const std::remove_cv_t<T>>{values.data(), values.size()}};
}

template <IsTypedArrayElement T>
    requires(!std::is_const_v<T>)
[[nodiscard]] constexpr auto as_typed_array(std::span<T> values) noexcept {
    return as_typed_array(std::span<const T>{values.data(), values.size()});
}

template <IsTypedArrayElement T, typename Allocator>
[[nodiscard]] constexpr auto as_typed_array(const std::vector<T, Allocator> &values) noexcept {
    return as_typed_array(std::span<const T>{values.data(), values.size()});
}

template <IsTypedArrayElement T, typename Allocator> void as_typed_array(std::vector<T, Allocator> &&values) = delete;

template <typename T>
    requires IsTypedArrayElementFor<T, typed_array_byte_order::big>
[[nodiscard]] constexpr auto as_typed_array_be(std::span<const T> values) noexcept {
    return typed_array_ref<std::remove_cv_t<T>, typed_array_byte_order::big>{
        std::span<const std::remove_cv_t<T>>{values.data(), values.size()}};
}

template <typename T>
    requires(!std::is_const_v<T> && IsTypedArrayElementFor<T, typed_array_byte_order::big>)
[[nodiscard]] constexpr auto as_typed_array_be(std::span<T> values) noexcept {
    return as_typed_array_be(std::span<const T>{values.data(), values.size()});
}

template <typename T, typename Allocator>
    requires IsTypedArrayElementFor<T, typed_array_byte_order::big>
[[nodiscard]] constexpr auto as_typed_array_be(const std::vector<T, Allocator> &values) noexcept {
    return as_typed_array_be(std::span<const T>{values.data(), values.size()});
}

template <typename T, typename Allocator>
    requires IsTypedArrayElementFor<T, typed_array_byte_order::big>
void as_typed_array_be(std::vector<T, Allocator> &&values) = delete;

template <typename T, detail::TypedArrayPayloadRange ByteRange, typed_array_byte_order ByteOrder = typed_array_byte_order::little>
    requires IsTypedArrayElementFor<T, ByteOrder>
class typed_array_values_view : public std::ranges::view_interface<typed_array_values_view<T, ByteRange>> {
  public:
    using value_type = std::remove_cv_t<T>;

    class iterator {
      public:
        using value_type        = typed_array_values_view::value_type;
        using difference_type   = std::ptrdiff_t;
        using iterator_category = std::forward_iterator_tag;
        using iterator_concept  = std::forward_iterator_tag;

        iterator() = default;
        constexpr iterator(std::ranges::iterator_t<const ByteRange> current, std::size_t remaining)
            : current_(std::move(current)), remaining_(remaining) {}

        [[nodiscard]] value_type operator*() const {
            std::array<std::byte, sizeof(value_type)> bytes{};
            auto                                      it = current_;
            for (auto &byte : bytes) {
                byte = static_cast<std::byte>(*it);
                ++it;
            }
            return detail::from_endian<ByteOrder, value_type>(std::span<const std::byte>{bytes});
        }

        constexpr iterator &operator++() {
            for (std::size_t i = 0; i < sizeof(value_type); ++i) {
                ++current_;
            }
            --remaining_;
            return *this;
        }

        constexpr iterator operator++(int) {
            auto copy = *this;
            ++(*this);
            return copy;
        }

        friend constexpr bool operator==(const iterator &lhs, const iterator &rhs) noexcept {
            return lhs.current_ == rhs.current_ && lhs.remaining_ == rhs.remaining_;
        }
        friend constexpr bool operator!=(const iterator &lhs, const iterator &rhs) noexcept { return !(lhs == rhs); }
        friend constexpr bool operator==(const iterator &it, std::default_sentinel_t) noexcept { return it.remaining_ == 0U; }
        friend constexpr bool operator==(std::default_sentinel_t, const iterator &it) noexcept { return it == std::default_sentinel; }

      private:
        std::ranges::iterator_t<const ByteRange> current_{};
        std::size_t                              remaining_{};
    };

    constexpr typed_array_values_view()
        requires std::default_initializable<ByteRange>
    = default;
    constexpr typed_array_values_view(ByteRange payload, std::size_t value_count)
        : payload_(std::move(payload)), value_count_(value_count) {}

    [[nodiscard]] constexpr iterator                begin() const { return iterator{std::ranges::begin(payload_), value_count_}; }
    [[nodiscard]] constexpr std::default_sentinel_t end() const noexcept { return {}; }
    [[nodiscard]] constexpr std::size_t             size() const noexcept { return value_count_; }

  private:
    ByteRange   payload_;
    std::size_t value_count_{};
};

template <typename T, detail::TypedArrayPayloadRange ByteRange = std::span<const std::byte>,
          typed_array_byte_order ByteOrder = typed_array_byte_order::little>
    requires IsTypedArrayElementFor<T, ByteOrder>
class typed_array_view {
  public:
    using value_type                              = std::remove_cv_t<T>;
    using payload_range_type                      = ByteRange;
    static constexpr auto          byte_order     = ByteOrder;
    static constexpr std::uint64_t cbor_array_tag = typed_array_traits<value_type, ByteOrder>::tag;
    static constexpr std::uint64_t cbor_tag       = cbor_array_tag;

    constexpr typed_array_view() = default;
    constexpr explicit typed_array_view(ByteRange payload) : payload_(std::move(payload)), payload_size_(payload_size(payload_)) {}
    constexpr typed_array_view(ByteRange payload, std::size_t payload_size) : payload_(std::move(payload)), payload_size_(payload_size) {}

    [[nodiscard]] constexpr const ByteRange &payload_range() const noexcept { return payload_; }

    [[nodiscard]] constexpr std::span<const std::byte> payload_bytes() const noexcept
        requires std::ranges::contiguous_range<const ByteRange> && std::ranges::sized_range<const ByteRange>
    {
        auto values = std::span{std::ranges::data(payload_), static_cast<std::size_t>(std::ranges::size(payload_))};
        return std::as_bytes(values);
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return payload_size_ / sizeof(value_type); }

    [[nodiscard]] constexpr auto values() const { return typed_array_values_view<value_type, ByteRange, ByteOrder>{payload_, size()}; }

    [[nodiscard]] std::vector<value_type> copy_values() const {
        std::vector<value_type> output;
        output.reserve(size());
        for (auto value : values()) {
            output.push_back(value);
        }
        return output;
    }

  private:
    [[nodiscard]] static constexpr std::size_t payload_size(const ByteRange &payload) {
        if constexpr (std::ranges::sized_range<const ByteRange>) {
            return static_cast<std::size_t>(std::ranges::size(payload));
        } else {
            return static_cast<std::size_t>(std::ranges::distance(payload));
        }
    }

    ByteRange   payload_{};
    std::size_t payload_size_{};
};

template <IsTypedArrayElement T, typename Decoder>
using typed_array_view_for =
    typed_array_view<T, std::conditional_t<IsContiguous<typename std::remove_cvref_t<Decoder>::input_buffer_type>,
                                           std::span<const std::byte>, typename std::remove_cvref_t<Decoder>::bstr_view_t>>;

template <typename T, typename Decoder>
    requires IsTypedArrayElementFor<T, typed_array_byte_order::big>
using typed_array_view_be_for =
    typed_array_view<T,
                     std::conditional_t<IsContiguous<typename std::remove_cvref_t<Decoder>::input_buffer_type>, std::span<const std::byte>,
                                        typename std::remove_cvref_t<Decoder>::bstr_view_t>,
                     typed_array_byte_order::big>;

template <typename T>
    requires IsTypedArrayElementFor<T, typed_array_byte_order::big>
using typed_array_be = typed_array<T, typed_array_byte_order::big>;

template <typename T, detail::TypedArrayPayloadRange ByteRange = std::span<const std::byte>>
    requires IsTypedArrayElementFor<T, typed_array_byte_order::big>
using typed_array_view_be = typed_array_view<T, ByteRange, typed_array_byte_order::big>;

template <typename Self> struct typed_array_codec : cbor_codec_mixin_base<Self> {
    using cbor_codec_mixin_base<Self>::decode;
    using cbor_codec_mixin_base<Self>::encode;

    template <typename T, typed_array_byte_order ByteOrder>
        requires IsTypedArrayElementFor<T, ByteOrder>
    void encode(const typed_array<T, ByteOrder> &array) {
        encode_owned_values<T, ByteOrder>(array.span());
    }

    template <typename T, typed_array_byte_order ByteOrder>
        requires IsTypedArrayElementFor<T, ByteOrder>
    void encode(const typed_array_ref<T, ByteOrder> &array) {
        encode_borrowed_values<T, ByteOrder>(array.values());
    }

    template <typename T, typed_array_byte_order ByteOrder>
        requires IsTypedArrayElementFor<T, ByteOrder>
    [[nodiscard]] status_code decode(typed_array<T, ByteOrder> &array, major_type major, std::byte additional_info) {
        using value_type = std::remove_cv_t<T>;
        auto &dec        = static_cast<Self &>(*this);

        return detail::decode_payload<value_type, ByteOrder>(
            dec, major, additional_info, [&](major_type payload_major, std::byte payload_info) {
                if (payload_major != major_type::ByteString || payload_info == std::byte{31}) {
                    return status_code::no_match_for_bstr_on_buffer;
                }
                const auto payload_size_u64 = dec.decode_unsigned(payload_info);
                if constexpr (std::numeric_limits<std::size_t>::max() < std::numeric_limits<std::uint64_t>::max()) {
                    const auto payload_size_limit = static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
                    if (payload_size_u64 > payload_size_limit) {
                        return status_code::error;
                    }
                }

                const auto payload_size = static_cast<std::size_t>(payload_size_u64);
                auto       raw_payload  = dec.decode_bstring_payload(payload_size_u64);
                if ((payload_size % sizeof(value_type)) != 0U) {
                    return status_code::unexpected_group_size;
                }
                array.values() = detail::materialize_values<value_type, ByteOrder>(std::move(raw_payload), payload_size);
                return status_code::success;
            });
    }

    template <typename T, typed_array_byte_order ByteOrder>
        requires IsTypedArrayElementFor<T, ByteOrder>
    [[nodiscard]] status_code decode(typed_array<T, ByteOrder> &array, std::uint64_t tag) {
        using value_type = std::remove_cv_t<T>;
        auto &dec        = static_cast<Self &>(*this);

        return detail::decode_payload_after_tag<value_type, ByteOrder>(dec, tag, [&](major_type payload_major, std::byte payload_info) {
            if (payload_major != major_type::ByteString || payload_info == std::byte{31}) {
                return status_code::no_match_for_bstr_on_buffer;
            }
            const auto payload_size_u64 = dec.decode_unsigned(payload_info);
            if constexpr (std::numeric_limits<std::size_t>::max() < std::numeric_limits<std::uint64_t>::max()) {
                const auto payload_size_limit = static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
                if (payload_size_u64 > payload_size_limit) {
                    return status_code::error;
                }
            }

            const auto payload_size = static_cast<std::size_t>(payload_size_u64);
            auto       raw_payload  = dec.decode_bstring_payload(payload_size_u64);
            if ((payload_size % sizeof(value_type)) != 0U) {
                return status_code::unexpected_group_size;
            }
            array.values() = detail::materialize_values<value_type, ByteOrder>(std::move(raw_payload), payload_size);
            return status_code::success;
        });
    }

    template <typename T, detail::TypedArrayPayloadRange ByteRange, typed_array_byte_order ByteOrder>
        requires(IsTypedArrayElementFor<T, ByteOrder> && detail::DecodableTypedArrayPayloadRange<Self, ByteRange>)
    [[nodiscard]] status_code decode(typed_array_view<T, ByteRange, ByteOrder> &view, major_type major, std::byte additional_info) {
        using value_type = std::remove_cv_t<T>;
        auto &dec        = static_cast<Self &>(*this);

        return detail::decode_payload<value_type, ByteOrder>(dec, major, additional_info, [&](major_type, std::byte payload_info) {
            if constexpr (std::ranges::contiguous_range<const ByteRange> && !IsContiguous<typename Self::input_buffer_type>) {
                return status_code::contiguous_view_on_non_contiguous_data;
            } else {
                const auto payload_size_u64 = dec.decode_unsigned(payload_info);
                if constexpr (std::numeric_limits<std::size_t>::max() < std::numeric_limits<std::uint64_t>::max()) {
                    const auto payload_size_limit = static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
                    if (payload_size_u64 > payload_size_limit) {
                        return status_code::error;
                    }
                }

                auto       raw_payload  = dec.decode_bstring_payload(payload_size_u64);
                const auto payload_size = static_cast<std::size_t>(payload_size_u64);
                if ((payload_size % sizeof(value_type)) != 0U) {
                    return status_code::unexpected_group_size;
                }
                view = typed_array_view<value_type, ByteRange, ByteOrder>{ByteRange{std::move(raw_payload)}, payload_size};
                return status_code::success;
            }
        });
    }

    template <typename T, detail::TypedArrayPayloadRange ByteRange, typed_array_byte_order ByteOrder>
        requires(IsTypedArrayElementFor<T, ByteOrder> && detail::DecodableTypedArrayPayloadRange<Self, ByteRange>)
    [[nodiscard]] status_code decode(typed_array_view<T, ByteRange, ByteOrder> &view, std::uint64_t tag) {
        using value_type = std::remove_cv_t<T>;
        auto &dec        = static_cast<Self &>(*this);

        return detail::decode_payload_after_tag<value_type, ByteOrder>(dec, tag, [&](major_type, std::byte payload_info) {
            if constexpr (std::ranges::contiguous_range<const ByteRange> && !IsContiguous<typename Self::input_buffer_type>) {
                return status_code::contiguous_view_on_non_contiguous_data;
            } else {
                const auto payload_size_u64 = dec.decode_unsigned(payload_info);
                if constexpr (std::numeric_limits<std::size_t>::max() < std::numeric_limits<std::uint64_t>::max()) {
                    const auto payload_size_limit = static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
                    if (payload_size_u64 > payload_size_limit) {
                        return status_code::error;
                    }
                }

                auto       raw_payload  = dec.decode_bstring_payload(payload_size_u64);
                const auto payload_size = static_cast<std::size_t>(payload_size_u64);
                if ((payload_size % sizeof(value_type)) != 0U) {
                    return status_code::unexpected_group_size;
                }
                view = typed_array_view<value_type, ByteRange, ByteOrder>{ByteRange{std::move(raw_payload)}, payload_size};
                return status_code::success;
            }
        });
    }

  private:
    void append_owned_payload_data(std::span<const std::byte> payload) {
        auto &enc = static_cast<Self &>(*this);
        if constexpr (requires { enc.appender_.append_owned(enc.data_, payload); }) {
            enc.appender_.append_owned(enc.data_, payload);
        } else {
            enc.appender_(enc.data_, payload);
        }
    }

    void encode_owned_payload(std::span<const std::byte> payload) {
        auto &enc = static_cast<Self &>(*this);
        enc.encode_major_and_size(static_cast<std::uint64_t>(payload.size()), static_cast<typename Self::byte_type>(0x40));
        append_owned_payload_data(payload);
    }

    template <typename T, typed_array_byte_order ByteOrder>
        requires IsTypedArrayElementFor<T, ByteOrder>
    void encode_converted_payload(std::span<const T> values) {
        auto &enc = static_cast<Self &>(*this);
        enc.encode_major_and_size(static_cast<std::uint64_t>(values.size_bytes()), static_cast<typename Self::byte_type>(0x40));

        using output_buffer_type = std::remove_reference_t<decltype(enc.data_)>;
        if constexpr (detail::DirectResizableByteOutputBuffer<output_buffer_type>) {
            detail::append_endian_payload_to<ByteOrder>(enc.data_, values);
        } else {
            auto payload = detail::endian_payload<ByteOrder>(values);
            append_owned_payload_data(std::span<const std::byte>{payload});
        }
    }

    template <typename T, typed_array_byte_order ByteOrder>
        requires IsTypedArrayElementFor<T, ByteOrder>
    void encode_owned_values(std::span<const T> values) {
        using value_type = std::remove_cv_t<T>;
        auto &enc        = static_cast<Self &>(*this);

        if constexpr (detail::native_matches_byte_order<ByteOrder>) {
            enc.encode(static_tag<typed_array_traits<value_type, ByteOrder>::tag>{});
            encode_owned_payload(std::as_bytes(values));
        } else {
            enc.encode(static_tag<typed_array_traits<value_type, ByteOrder>::tag>{});
            encode_converted_payload<T, ByteOrder>(values);
        }
    }

    template <typename T, typed_array_byte_order ByteOrder>
        requires IsTypedArrayElementFor<T, ByteOrder>
    void encode_borrowed_values(std::span<const T> values) {
        using value_type = std::remove_cv_t<T>;
        auto &enc        = static_cast<Self &>(*this);

        if constexpr (detail::native_matches_byte_order<ByteOrder>) {
            enc.encode(static_tag<typed_array_traits<value_type, ByteOrder>::tag>{});
            enc.encode(as_bstr_range(std::as_bytes(values)));
        } else {
            enc.encode(static_tag<typed_array_traits<value_type, ByteOrder>::tag>{});
            encode_converted_payload<T, ByteOrder>(values);
        }
    }
};

template <typed_array_byte_order ByteOrder = typed_array_byte_order::little, typename T>
    requires IsTypedArrayElementFor<T, ByteOrder>
[[nodiscard]] cbor_segments encode_typed_array_segments(std::span<const T> values) {
    if constexpr (detail::native_matches_byte_order<ByteOrder>) {
        return encode_tagged_bstr_segments(typed_array_traits<std::remove_cv_t<T>, ByteOrder>::tag, std::as_bytes(values));
    } else {
        throw std::logic_error("RFC 8746 typed-array segmented zero-copy encode requires matching native byte order");
    }
}

template <typed_array_byte_order ByteOrder = typed_array_byte_order::little, cbor::tags::detail::ByteSegmentsOutputBuffer Segments,
          typename T>
    requires IsTypedArrayElementFor<T, ByteOrder>
void encode_typed_array_segments_into(Segments &segments, std::span<const T> values) {
    if constexpr (detail::native_matches_byte_order<ByteOrder>) {
        encode_tagged_bstr_segments_into(segments, typed_array_traits<std::remove_cv_t<T>, ByteOrder>::tag, std::as_bytes(values));
    } else {
        throw std::logic_error("RFC 8746 typed-array segmented zero-copy encode requires matching native byte order");
    }
}

template <typed_array_byte_order ByteOrder = typed_array_byte_order::little, typename T>
    requires(!std::is_const_v<T> && IsTypedArrayElementFor<T, ByteOrder>)
[[nodiscard]] cbor_segments encode_typed_array_segments(std::span<T> values) {
    return encode_typed_array_segments<ByteOrder>(std::span<const T>{values.data(), values.size()});
}

template <typed_array_byte_order ByteOrder = typed_array_byte_order::little, cbor::tags::detail::ByteSegmentsOutputBuffer Segments,
          typename T>
    requires(!std::is_const_v<T> && IsTypedArrayElementFor<T, ByteOrder>)
void encode_typed_array_segments_into(Segments &segments, std::span<T> values) {
    encode_typed_array_segments_into<ByteOrder>(segments, std::span<const T>{values.data(), values.size()});
}

template <typed_array_byte_order ByteOrder = typed_array_byte_order::little, typename T>
    requires IsTypedArrayElementFor<T, ByteOrder>
[[nodiscard]] cbor_segments encode_typed_array_segments_copy(std::span<const T> values) {
    const auto tag_header =
        cbor::tags::detail::encode_cbor_major_argument_header(typed_array_traits<std::remove_cv_t<T>, ByteOrder>::tag, std::byte{0xC0});
    auto       payload     = detail::endian_payload<ByteOrder>(values);
    const auto bstr_header = cbor::tags::detail::encode_cbor_major_argument_header(payload.size(), std::byte{0x40});

    cbor_segments segments;
    segments.reserve(3);
    segments.append_owned(tag_header.span());
    segments.append_owned(bstr_header.span());
    segments.append_owned(payload);
    return segments;
}

template <typed_array_byte_order ByteOrder = typed_array_byte_order::little, typename T>
    requires(!std::is_const_v<T> && IsTypedArrayElementFor<T, ByteOrder>)
[[nodiscard]] cbor_segments encode_typed_array_segments_copy(std::span<T> values) {
    return encode_typed_array_segments_copy<ByteOrder>(std::span<const T>{values.data(), values.size()});
}

template <typename T>
    requires IsTypedArrayElementFor<T, typed_array_byte_order::big>
[[nodiscard]] cbor_segments encode_typed_array_segments_be(std::span<const T> values) {
    return encode_typed_array_segments<typed_array_byte_order::big>(values);
}

template <typename T>
    requires(!std::is_const_v<T> && IsTypedArrayElementFor<T, typed_array_byte_order::big>)
[[nodiscard]] cbor_segments encode_typed_array_segments_be(std::span<T> values) {
    return encode_typed_array_segments_be(std::span<const T>{values.data(), values.size()});
}

template <typename T>
    requires IsTypedArrayElementFor<T, typed_array_byte_order::big>
[[nodiscard]] cbor_segments encode_typed_array_segments_copy_be(std::span<const T> values) {
    return encode_typed_array_segments_copy<typed_array_byte_order::big>(values);
}

template <typename T>
    requires(!std::is_const_v<T> && IsTypedArrayElementFor<T, typed_array_byte_order::big>)
[[nodiscard]] cbor_segments encode_typed_array_segments_copy_be(std::span<T> values) {
    return encode_typed_array_segments_copy_be(std::span<const T>{values.data(), values.size()});
}

template <typename T, typed_array_byte_order ByteOrder>
    requires IsTypedArrayElementFor<T, ByteOrder>
[[nodiscard]] cbor_segments encode_segments(const typed_array_ref<T, ByteOrder> &array) {
    return encode_typed_array_segments<ByteOrder>(array.values());
}

template <typename T, typed_array_byte_order ByteOrder>
    requires IsTypedArrayElementFor<T, ByteOrder>
[[nodiscard]] cbor_segments encode_segments(const typed_array<T, ByteOrder> &array) {
    return encode_typed_array_segments_copy<ByteOrder>(array.span());
}

} // namespace cbor::tags::ext::rfc8746
