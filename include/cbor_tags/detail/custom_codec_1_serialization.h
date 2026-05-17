#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_detail.h"
#include "cbor_tags/cbor_integer.h"
#include "cbor_tags/cbor_reflection.h"
#include "cbor_tags/cbor_segments.h"
#include "cbor_tags/cbor_simple.h"
#include "cbor_tags/float16_ieee754.h"

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace cbor::tags::detail::custom_codec_1 {

template <typename T> struct is_std_array : std::false_type {};
template <typename T, std::size_t N> struct is_std_array<std::array<T, N>> : std::true_type {};
template <typename T> inline constexpr bool is_std_array_v = is_std_array<std::remove_cvref_t<T>>::value;

template <typename T> struct is_mutable_byte_span : std::false_type {};
template <std::size_t Extent> struct is_mutable_byte_span<std::span<std::byte, Extent>> : std::true_type {};
template <typename T> inline constexpr bool is_mutable_byte_span_v = is_mutable_byte_span<std::remove_cvref_t<T>>::value;

template <typename T> struct is_basic_string_view : std::false_type {};
template <typename Char, typename Traits>
struct is_basic_string_view<std::basic_string_view<Char, Traits>> : std::bool_constant<IsTextChar<Char>> {};
template <typename T> inline constexpr bool is_basic_string_view_v = is_basic_string_view<std::remove_cvref_t<T>>::value;

template <typename T> struct is_basic_string : std::false_type {};
template <typename Char, typename Traits, typename Allocator>
struct is_basic_string<std::basic_string<Char, Traits, Allocator>> : std::bool_constant<IsTextChar<Char>> {};
template <typename T> inline constexpr bool is_basic_string_v = is_basic_string<std::remove_cvref_t<T>>::value;

template <typename T> inline constexpr bool is_text_string_v = is_basic_string_v<T> || is_basic_string_view_v<T>;

template <typename T> struct dependent_false : std::false_type {};

template <typename T>
inline constexpr bool is_bulk_little_endian_scalar_v =
    (!std::is_same_v<std::remove_cvref_t<T>, bool> && std::is_unsigned_v<std::remove_cvref_t<T>> &&
     std::is_integral_v<std::remove_cvref_t<T>>) ||
    std::is_floating_point_v<std::remove_cvref_t<T>>;

template <typename T>
concept ResizableRange = requires(T value, typename std::remove_cvref_t<T>::size_type size) { value.resize(size); };

template <typename T>
inline constexpr bool is_bulk_little_endian_contiguous_range_v =
    std::ranges::contiguous_range<T> && std::ranges::sized_range<T> && is_bulk_little_endian_scalar_v<std::ranges::range_value_t<T>>;

template <typename Writer, typename T> constexpr void encode_value(Writer &writer, const T &value);

class span_reader {
  public:
    explicit constexpr span_reader(std::span<const std::byte> input) noexcept : input_(input) {}

    [[nodiscard]] constexpr bool        empty() const noexcept { return position_ == input_.size(); }
    [[nodiscard]] constexpr std::size_t remaining() const noexcept { return input_.size() - position_; }

    constexpr status_code read_byte(std::byte &out) noexcept {
        if (empty()) {
            return status_code::incomplete;
        }
        out = input_[position_++];
        return status_code::success;
    }

    constexpr status_code read_bytes(std::size_t count, std::span<const std::byte> &out) noexcept {
        if (count > remaining()) {
            return status_code::incomplete;
        }
        out = input_.subspan(position_, count);
        position_ += count;
        return status_code::success;
    }

  private:
    std::span<const std::byte> input_;
    std::size_t                position_{};
};

class size_writer {
  public:
    constexpr void write_byte(std::byte) noexcept { ++size_; }
    constexpr void write_bytes(std::span<const std::byte> bytes) noexcept { size_ += bytes.size(); }
    constexpr void write_bytes(std::string_view bytes) noexcept { size_ += bytes.size(); }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }

  private:
    std::size_t size_{};
};

template <typename Appender, typename Output> class appender_writer {
  public:
    constexpr appender_writer(Appender &appender, Output &output) noexcept : appender_(&appender), output_(&output) {}

    constexpr void write_byte(std::byte value) {
        using output_byte = typename std::remove_cvref_t<Output>::value_type;
        (*appender_)(*output_, static_cast<output_byte>(value));
    }

    constexpr void write_bytes(std::span<const std::byte> bytes) { (*appender_)(*output_, bytes); }

    constexpr void write_bytes(std::string_view bytes) { (*appender_)(*output_, bytes); }

  private:
    Appender *appender_{};
    Output   *output_{};
};

template <typename Segments> class borrowed_segment_writer {
  public:
    explicit constexpr borrowed_segment_writer(Segments &output) noexcept : output_(&output) {}

    constexpr void write_byte(std::byte value) { output_->append_owned(std::span<const std::byte>{&value, 1}); }

    constexpr void write_bytes(std::span<const std::byte> bytes) { output_->append_owned(bytes); }

    constexpr void write_bytes(std::string_view bytes) { output_->append_owned(std::as_bytes(std::span{bytes.data(), bytes.size()})); }

    constexpr void write_borrowed_bytes(std::span<const std::byte> bytes) { output_->append_borrowed(bytes); }

  private:
    Segments *output_{};
};

template <typename Writer> constexpr void write_varuint(Writer &writer, std::uint64_t value) {
    do {
        auto byte = static_cast<std::uint8_t>(value & 0x7FU);
        value >>= 7U;
        if (value != 0U) {
            byte |= 0x80U;
        }
        writer.write_byte(static_cast<std::byte>(byte));
    } while (value != 0U);
}

constexpr status_code read_varuint(span_reader &reader, std::uint64_t &value) noexcept {
    value = 0;
    for (std::uint32_t shift = 0; shift <= 63; shift += 7) {
        std::byte byte{};
        auto      status = reader.read_byte(byte);
        if (status != status_code::success) {
            return status;
        }
        const auto raw = std::to_integer<std::uint8_t>(byte);
        if (shift == 63 && (raw & 0xFEU) != 0U) {
            return status_code::error;
        }
        value |= static_cast<std::uint64_t>(raw & 0x7FU) << shift;
        if ((raw & 0x80U) == 0U) {
            return status_code::success;
        }
    }
    return status_code::error;
}

template <typename Writer, typename U>
    requires(std::is_unsigned_v<U> && std::is_integral_v<U>)
constexpr void write_little_endian(Writer &writer, U value) {
    for (std::size_t byte_index = 0; byte_index < sizeof(U); ++byte_index) {
        writer.write_byte(static_cast<std::byte>((value >> (byte_index * 8U)) & U{0xFFU}));
    }
}

template <typename U>
    requires(std::is_unsigned_v<U> && std::is_integral_v<U>)
constexpr status_code read_little_endian(span_reader &reader, U &value) noexcept {
    value = 0;
    for (std::size_t byte_index = 0; byte_index < sizeof(U); ++byte_index) {
        std::byte byte{};
        auto      status = reader.read_byte(byte);
        if (status != status_code::success) {
            return status;
        }
        value |= static_cast<U>(std::to_integer<std::uint8_t>(byte)) << (byte_index * 8U);
    }
    return status_code::success;
}

template <typename T> [[nodiscard]] constexpr std::span<const std::byte> as_const_bytes(const T &value) noexcept {
    return std::as_bytes(std::span{std::ranges::data(value), std::ranges::size(value)});
}

template <typename T> [[nodiscard]] constexpr std::span<std::byte> as_writable_bytes(T &value) noexcept {
    return std::as_writable_bytes(std::span{std::ranges::data(value), std::ranges::size(value)});
}

template <typename T> [[nodiscard]] constexpr bool byte_size_overflows(std::uint64_t length) noexcept {
    return length > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max() / sizeof(T));
}

template <typename Writer, typename T> constexpr void encode_bulk_little_endian_range(Writer &writer, const T &value) {
    if constexpr (std::endian::native == std::endian::little) {
        const auto bytes = as_const_bytes(value);
        if constexpr (requires { writer.write_borrowed_bytes(bytes); }) {
            writer.write_borrowed_bytes(bytes);
        } else {
            writer.write_bytes(bytes);
        }
    } else {
        for (const auto &element : value) {
            encode_value(writer, element);
        }
    }
}

template <typename T> constexpr std::uint64_t tag_to_uint64(const T &tag) {
    using type = std::remove_cvref_t<T>;
    if constexpr (is_static_tag_t<type>::value || is_dynamic_tag_t<type>) {
        return static_cast<std::uint64_t>(tag);
    } else if constexpr (std::is_integral_v<type> && std::is_unsigned_v<type>) {
        return static_cast<std::uint64_t>(tag);
    } else {
        static_assert(dependent_false<T>::value, "custom_codec_1 requires an unsigned CBOR tag");
    }
}

template <typename T> constexpr std::uint64_t tag_for(const T &value) {
    using type = std::remove_cvref_t<T>;
    if constexpr (IsTaggedTuple<type>) {
        return tag_to_uint64(std::get<0>(value));
    } else if constexpr (HasInlineTag<type>) {
        return static_cast<std::uint64_t>(type::cbor_tag);
    } else if constexpr (HasTagMember<type>) {
        return tag_to_uint64(Access::cbor_tag(value));
    } else if constexpr (HasTagNonConstructible<type>) {
        return tag_to_uint64(cbor::tags::cbor_tag<type>());
    } else if constexpr (HasTagFreeFunction<type>) {
        return tag_to_uint64(cbor_tag(value));
    } else {
        static_assert(dependent_false<T>::value,
                      "as_custom_codec_1(value) requires a CBOR tag; use as_custom_codec_1(tag, value) for explicit tags");
    }
}

template <typename T> constexpr std::remove_cvref_t<T> make_decode_value_for_existing(T &value) {
    using type = std::remove_cvref_t<T>;
    if constexpr (is_static_tag_t<type>::value || is_dynamic_tag_t<type>) {
        return value;
    } else if constexpr (IsOptional<type>) {
        if (value.has_value()) {
            return type{std::in_place, make_decode_value_for_existing(*value)};
        }
        return type{};
    } else if constexpr (is_mutable_byte_span_v<type>) {
        return value;
    } else if constexpr (requires { value.get_allocator(); }) {
        return std::make_from_tuple<type>(std::uses_allocator_construction_args<type>(value.get_allocator()));
    } else if constexpr (is_std_array_v<type>) {
        return [&]<std::size_t... Indices>(std::index_sequence<Indices...>) {
            return type{make_decode_value_for_existing(std::get<Indices>(value))...};
        }(std::make_index_sequence<std::tuple_size_v<type>>{});
    } else if constexpr (IsUntaggedTuple<type> || IsTaggedTuple<type>) {
        return [&]<std::size_t... Indices>(std::index_sequence<Indices...>) {
            return type{make_decode_value_for_existing(std::get<Indices>(value))...};
        }(std::make_index_sequence<std::tuple_size_v<type>>{});
    } else if constexpr (IsVariant<type> && std::copy_constructible<type>) {
        return value;
    } else if constexpr (std::is_aggregate_v<type> && !is_std_array_v<type> && !IsMap<type> && !IsRangeOfCborValues<type> &&
                         !IsVariant<type> && !is_text_string_v<type> && !IsBinaryString<type>) {
        auto tuple       = cbor::tags::to_tuple(value);
        using tuple_type = std::remove_cvref_t<decltype(tuple)>;
        return [&]<std::size_t... Indices>(std::index_sequence<Indices...>) {
            return type{make_decode_value_for_existing(std::get<Indices>(tuple))...};
        }(std::make_index_sequence<std::tuple_size_v<tuple_type>>{});
    } else {
        return type{};
    }
}

template <typename T> constexpr T make_decode_value_for_existing_optional(std::optional<T> &value) {
    if (value.has_value()) {
        return make_decode_value_for_existing(*value);
    }
    return cbor::tags::detail::make_decode_value_for_optional<T>(value);
}

template <typename T, typename Allocator> constexpr std::remove_cvref_t<T> make_decode_value_with_allocator(const Allocator &allocator) {
    using type = std::remove_cvref_t<T>;
    if constexpr (IsOptional<type>) {
        return type{std::in_place, make_decode_value_with_allocator<typename type::value_type>(allocator)};
    } else if constexpr (std::uses_allocator_v<type, Allocator>) {
        return std::make_from_tuple<type>(std::uses_allocator_construction_args<type>(allocator));
    } else if constexpr (is_std_array_v<type>) {
        return [&]<std::size_t... Indices>(std::index_sequence<Indices...>) {
            return type{((void)Indices, make_decode_value_with_allocator<typename type::value_type>(allocator))...};
        }(std::make_index_sequence<std::tuple_size_v<type>>{});
    } else if constexpr (IsUntaggedTuple<type> || IsTaggedTuple<type>) {
        return [&]<std::size_t... Indices>(std::index_sequence<Indices...>) {
            return type{make_decode_value_with_allocator<std::remove_cvref_t<std::tuple_element_t<Indices, type>>>(allocator)...};
        }(std::make_index_sequence<std::tuple_size_v<type>>{});
    } else if constexpr (std::is_aggregate_v<type> && !is_std_array_v<type> && !IsMap<type> && !IsRangeOfCborValues<type> &&
                         !IsVariant<type> && !is_text_string_v<type> && !IsBinaryString<type>) {
        using tuple_type = std::remove_cvref_t<decltype(cbor::tags::to_tuple(std::declval<type &>()))>;
        return [&]<std::size_t... Indices>(std::index_sequence<Indices...>) {
            return type{make_decode_value_with_allocator<std::remove_cvref_t<std::tuple_element_t<Indices, tuple_type>>>(allocator)...};
        }(std::make_index_sequence<std::tuple_size_v<tuple_type>>{});
    } else {
        return type{};
    }
}

template <typename Value, typename Container> constexpr Value make_decode_value_for_container(Container &container) {
    if constexpr (requires { container.get_allocator(); }) {
        return make_decode_value_with_allocator<Value>(container.get_allocator());
    } else {
        return Value{};
    }
}

template <typename T> constexpr status_code reserve_decoded_container(T &value, std::uint64_t length) {
    using type = std::remove_cvref_t<T>;
    if constexpr (requires { typename type::size_type; }) {
        if (length > static_cast<std::uint64_t>(std::numeric_limits<typename type::size_type>::max())) {
            return status_code::error;
        }
    }
    if constexpr (requires { value.max_size(); }) {
        if (length > static_cast<std::uint64_t>(value.max_size())) {
            return status_code::error;
        }
    }
    if constexpr (HasReserve<type>) {
        value.reserve(static_cast<typename type::size_type>(length));
    }
    return status_code::success;
}

template <typename T> constexpr bool first_tuple_field_is_tag_v = false;

template <typename T> constexpr bool tuple_first_field_is_tag() {
    using tuple_type = std::remove_cvref_t<T>;
    if constexpr (std::tuple_size_v<tuple_type> == 0) {
        return false;
    } else {
        using first_type = std::remove_cvref_t<std::tuple_element_t<0, tuple_type>>;
        return is_static_tag_t<first_type>::value || is_dynamic_tag_t<first_type>;
    }
}

template <typename Tuple, typename Fn> constexpr decltype(auto) visit_tuple_payload(Tuple &&tuple, Fn &&fn) {
    if constexpr (tuple_first_field_is_tag<Tuple>()) {
        return std::apply(
            [&fn](auto &&, auto &&...tail) -> decltype(auto) { return std::forward<Fn>(fn)(std::forward<decltype(tail)>(tail)...); },
            std::forward<Tuple>(tuple));
    } else {
        return std::apply(std::forward<Fn>(fn), std::forward<Tuple>(tuple));
    }
}

template <typename Writer, typename T> constexpr void encode_value(Writer &writer, const T &value);
template <typename T> constexpr status_code           decode_value(span_reader &reader, T &value);

template <typename Writer, typename Tuple> constexpr void encode_tuple_fields(Writer &writer, const Tuple &tuple) {
    visit_tuple_payload(tuple, [&writer](const auto &...fields) { (encode_value(writer, fields), ...); });
}

template <typename Tuple> constexpr status_code decode_tuple_fields(span_reader &reader, Tuple &&tuple) {
    return visit_tuple_payload(std::forward<Tuple>(tuple), [&reader](auto &...fields) -> status_code {
        auto status = status_code::success;
        auto one    = [&reader, &status](auto &field) {
            if (status == status_code::success) {
                status = decode_value(reader, field);
            }
        };
        (one(fields), ...);
        return status;
    });
}

template <typename Writer, typename T> constexpr void encode_scalar(Writer &writer, const T &value) {
    using type = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<type, std::byte>) {
        writer.write_byte(value);
    } else if constexpr (std::is_same_v<type, bool>) {
        writer.write_byte(value ? std::byte{0x01} : std::byte{0x00});
    } else if constexpr (std::is_unsigned_v<type> && std::is_integral_v<type>) {
        write_little_endian(writer, value);
    } else if constexpr (std::is_signed_v<type> && std::is_integral_v<type>) {
        using unsigned_type = std::make_unsigned_t<type>;
        write_little_endian(writer, static_cast<unsigned_type>(value));
    } else if constexpr (std::is_enum_v<type>) {
        encode_scalar(writer, static_cast<std::underlying_type_t<type>>(value));
    } else if constexpr (std::is_same_v<type, float16_t>) {
        write_little_endian(writer, value.value);
    } else if constexpr (std::is_same_v<type, float>) {
        write_little_endian(writer, std::bit_cast<std::uint32_t>(value));
    } else if constexpr (std::is_same_v<type, double>) {
        write_little_endian(writer, std::bit_cast<std::uint64_t>(value));
    } else if constexpr (std::is_same_v<type, negative>) {
        write_little_endian(writer, value.value);
    } else if constexpr (std::is_same_v<type, integer>) {
        writer.write_byte(value.is_negative ? std::byte{0x01} : std::byte{0x00});
        write_little_endian(writer, value.value);
    } else if constexpr (std::is_same_v<type, simple>) {
        writer.write_byte(static_cast<std::byte>(value.value));
    } else if constexpr (std::is_same_v<type, std::nullptr_t>) {
    } else {
        static_assert(dependent_false<T>::value, "unsupported custom_codec_1 scalar type");
    }
}

template <typename T> constexpr status_code decode_scalar(span_reader &reader, T &value) {
    using type = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<type, std::byte>) {
        return reader.read_byte(value);
    } else if constexpr (std::is_same_v<type, bool>) {
        std::byte byte{};
        auto      status = reader.read_byte(byte);
        if (status != status_code::success) {
            return status;
        }
        const auto raw = std::to_integer<std::uint8_t>(byte);
        if (raw > 1U) {
            return status_code::error;
        }
        value = raw == 1U;
        return status_code::success;
    } else if constexpr (std::is_unsigned_v<type> && std::is_integral_v<type>) {
        return read_little_endian(reader, value);
    } else if constexpr (std::is_signed_v<type> && std::is_integral_v<type>) {
        using unsigned_type = std::make_unsigned_t<type>;
        unsigned_type raw{};
        auto          status = read_little_endian(reader, raw);
        if (status != status_code::success) {
            return status;
        }
        value = std::bit_cast<type>(raw);
        return status_code::success;
    } else if constexpr (std::is_enum_v<type>) {
        std::underlying_type_t<type> raw{};
        auto                         status = decode_scalar(reader, raw);
        if (status != status_code::success) {
            return status;
        }
        value = static_cast<type>(raw);
        return status_code::success;
    } else if constexpr (std::is_same_v<type, float16_t>) {
        return read_little_endian(reader, value.value);
    } else if constexpr (std::is_same_v<type, float>) {
        std::uint32_t raw{};
        auto          status = read_little_endian(reader, raw);
        if (status != status_code::success) {
            return status;
        }
        value = std::bit_cast<float>(raw);
        return status_code::success;
    } else if constexpr (std::is_same_v<type, double>) {
        std::uint64_t raw{};
        auto          status = read_little_endian(reader, raw);
        if (status != status_code::success) {
            return status;
        }
        value = std::bit_cast<double>(raw);
        return status_code::success;
    } else if constexpr (std::is_same_v<type, negative>) {
        return read_little_endian(reader, value.value);
    } else if constexpr (std::is_same_v<type, integer>) {
        std::byte sign{};
        auto      status = reader.read_byte(sign);
        if (status != status_code::success) {
            return status;
        }
        const auto raw_sign = std::to_integer<std::uint8_t>(sign);
        if (raw_sign > 1U) {
            return status_code::error;
        }
        value.is_negative = raw_sign == 1U;
        return read_little_endian(reader, value.value);
    } else if constexpr (std::is_same_v<type, simple>) {
        std::byte byte{};
        auto      status = reader.read_byte(byte);
        if (status != status_code::success) {
            return status;
        }
        value.value = std::to_integer<std::uint8_t>(byte);
        return status_code::success;
    } else if constexpr (std::is_same_v<type, std::nullptr_t>) {
        value = nullptr;
        return status_code::success;
    } else {
        static_assert(dependent_false<T>::value, "unsupported custom_codec_1 scalar type");
    }
}

template <typename Writer, typename T> constexpr void encode_byte_string(Writer &writer, const T &value) {
    write_varuint(writer, static_cast<std::uint64_t>(std::ranges::size(value)));
    if constexpr (std::ranges::contiguous_range<const T>) {
        const auto bytes =
            std::span<const std::byte>(reinterpret_cast<const std::byte *>(std::ranges::data(value)), std::ranges::size(value));
        if constexpr (requires { writer.write_borrowed_bytes(bytes); }) {
            writer.write_borrowed_bytes(bytes);
        } else {
            writer.write_bytes(bytes);
        }
    } else {
        for (auto byte : value) {
            writer.write_byte(static_cast<std::byte>(byte));
        }
    }
}

template <typename T> constexpr status_code decode_byte_string(span_reader &reader, T &value) {
    using type = std::remove_cvref_t<T>;
    std::uint64_t length{};
    auto          status = read_varuint(reader, length);
    if (status != status_code::success) {
        return status;
    }
    if (length > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return status_code::error;
    }
    if constexpr (IsConstBinaryView<type> && cbor::tags::detail::is_static_extent_span_v<type>) {
        if (length != static_cast<std::uint64_t>(type::extent)) {
            return status_code::unexpected_group_size;
        }
    }
    if constexpr (is_mutable_byte_span_v<type>) {
        if (length != static_cast<std::uint64_t>(value.size())) {
            return status_code::unexpected_group_size;
        }
    }
    std::span<const std::byte> bytes{};
    status = reader.read_bytes(static_cast<std::size_t>(length), bytes);
    if (status != status_code::success) {
        return status;
    }
    if constexpr (IsConstBinaryView<type>) {
        value = T(bytes.data(), bytes.size());
    } else if constexpr (is_mutable_byte_span_v<type>) {
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            value[i] = bytes[i];
        }
    } else {
        auto decoded = make_decode_value_for_existing(value);
        status       = reserve_decoded_container(decoded, length);
        if (status != status_code::success) {
            return status;
        }
        detail::appender<type> appender;
        appender(decoded, bytes);
        value = std::move(decoded);
    }
    return status_code::success;
}

template <typename Writer, typename T> constexpr void encode_text_string(Writer &writer, const T &value) {
    const auto text = std::string_view(reinterpret_cast<const char *>(value.data()), value.size());
    write_varuint(writer, static_cast<std::uint64_t>(text.size()));
    if constexpr (requires { writer.write_borrowed_bytes(std::as_bytes(std::span{text.data(), text.size()})); }) {
        writer.write_borrowed_bytes(std::as_bytes(std::span{text.data(), text.size()}));
    } else {
        writer.write_bytes(text);
    }
}

template <typename T> constexpr status_code decode_text_string(span_reader &reader, T &value) {
    using type      = std::remove_cvref_t<T>;
    using char_type = typename type::value_type;

    std::uint64_t length{};
    auto          status = read_varuint(reader, length);
    if (status != status_code::success) {
        return status;
    }
    if (length > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return status_code::error;
    }
    std::span<const std::byte> bytes{};
    status = reader.read_bytes(static_cast<std::size_t>(length), bytes);
    if (status != status_code::success) {
        return status;
    }
    const auto text = reinterpret_cast<const char_type *>(bytes.data());
    if constexpr (is_basic_string_view_v<type>) {
        value = type{text, bytes.size()};
    } else {
        value.assign(text, bytes.size());
    }
    return status_code::success;
}

template <typename Writer, typename T, std::size_t N> constexpr void encode_std_array(Writer &writer, const std::array<T, N> &value) {
    if constexpr (std::is_same_v<T, std::byte>) {
        writer.write_bytes(std::span<const std::byte>(value.data(), value.size()));
    } else if constexpr (is_bulk_little_endian_scalar_v<T>) {
        encode_bulk_little_endian_range(writer, value);
    } else {
        for (const auto &element : value) {
            encode_value(writer, element);
        }
    }
}

template <typename T, std::size_t N> constexpr status_code decode_std_array(span_reader &reader, std::array<T, N> &value) {
    if constexpr (std::is_same_v<T, std::byte>) {
        std::span<const std::byte> bytes{};
        auto                       status = reader.read_bytes(N, bytes);
        if (status != status_code::success) {
            return status;
        }
        if constexpr (N != 0U) {
            std::memcpy(value.data(), bytes.data(), N);
        }
        return status_code::success;
    } else if constexpr (is_bulk_little_endian_scalar_v<T> && std::endian::native == std::endian::little) {
        constexpr auto             byte_count = N * sizeof(T);
        std::span<const std::byte> bytes{};
        auto                       status = reader.read_bytes(byte_count, bytes);
        if (status != status_code::success) {
            return status;
        }
        if constexpr (byte_count != 0U) {
            std::memcpy(value.data(), bytes.data(), byte_count);
        }
        return status_code::success;
    } else {
        for (auto &element : value) {
            auto status = decode_value(reader, element);
            if (status != status_code::success) {
                return status;
            }
        }
        return status_code::success;
    }
}

template <typename Writer, typename T> constexpr void encode_optional(Writer &writer, const std::optional<T> &value) {
    writer.write_byte(value.has_value() ? std::byte{0x01} : std::byte{0x00});
    if (value.has_value()) {
        encode_value(writer, *value);
    }
}

template <typename T> constexpr status_code decode_optional(span_reader &reader, std::optional<T> &value) {
    std::byte present{};
    auto      status = reader.read_byte(present);
    if (status != status_code::success) {
        return status;
    }
    const auto raw = std::to_integer<std::uint8_t>(present);
    if (raw == 0U) {
        value = std::nullopt;
        return status_code::success;
    }
    if (raw != 1U) {
        return status_code::error;
    }
    auto decoded = make_decode_value_for_existing_optional(value);
    status       = decode_value(reader, decoded);
    if (status != status_code::success) {
        return status;
    }
    value = std::move(decoded);
    return status_code::success;
}

template <typename Writer, typename... Ts> constexpr void encode_variant(Writer &writer, const std::variant<Ts...> &value) {
    write_varuint(writer, static_cast<std::uint64_t>(value.index()));
    std::visit([&writer](const auto &alternative) { encode_value(writer, alternative); }, value);
}

template <std::size_t I = 0, typename... Ts>
constexpr status_code decode_variant_alternative(std::uint64_t index, span_reader &reader, std::variant<Ts...> &value) {
    if constexpr (I >= sizeof...(Ts)) {
        (void)index;
        (void)reader;
        (void)value;
        return status_code::no_match_in_variant_on_buffer;
    } else {
        if (index == I) {
            using alternative_type = std::variant_alternative_t<I, std::variant<Ts...>>;
            auto make_alternative  = [&value]() -> alternative_type {
                if constexpr (std::default_initializable<alternative_type>) {
                    return alternative_type{};
                } else if constexpr (std::copy_constructible<alternative_type>) {
                    return alternative_type{std::get<I>(value)};
                } else {
                    static_assert(dependent_false<alternative_type>::value,
                                  "custom_codec_1 variant alternatives must be default-initializable or copy-constructible");
                }
            };
            if constexpr (!std::default_initializable<alternative_type>) {
                if (value.index() != I) {
                    return status_code::error;
                }
            }
            auto alternative = make_alternative();
            auto status      = decode_value(reader, alternative);
            if (status != status_code::success) {
                return status;
            }
            value.template emplace<I>(std::move(alternative));
            return status_code::success;
        }
        return decode_variant_alternative<I + 1>(index, reader, value);
    }
}

template <typename... Ts> constexpr status_code decode_variant(span_reader &reader, std::variant<Ts...> &value) {
    std::uint64_t index{};
    auto          status = read_varuint(reader, index);
    if (status != status_code::success) {
        return status;
    }
    return decode_variant_alternative(index, reader, value);
}

template <typename Writer, typename T> constexpr void encode_map(Writer &writer, const T &value) {
    write_varuint(writer, static_cast<std::uint64_t>(std::ranges::size(value)));
    for (const auto &entry : value) {
        encode_value(writer, pair_first(entry));
        encode_value(writer, pair_second(entry));
    }
}

template <typename T> constexpr status_code decode_map(span_reader &reader, T &value) {
    std::uint64_t length{};
    auto          status = read_varuint(reader, length);
    if (status != status_code::success) {
        return status;
    }
    if constexpr (requires { value.clear(); }) {
        value.clear();
    }
    status = reserve_decoded_container(value, length);
    if (status != status_code::success) {
        return status;
    }
    detail::appender<T> appender;
    for (std::uint64_t i = 0; i < length; ++i) {
        std::pair<typename T::key_type, typename T::mapped_type> entry{make_decode_value_for_container<typename T::key_type>(value),
                                                                       make_decode_value_for_container<typename T::mapped_type>(value)};
        status = decode_value(reader, entry.first);
        if (status != status_code::success) {
            return status;
        }
        status = decode_value(reader, entry.second);
        if (status != status_code::success) {
            return status;
        }
        appender(value, std::move(entry));
    }
    return status_code::success;
}

template <typename Writer, typename T> constexpr void encode_range(Writer &writer, const T &value) {
    if constexpr (std::ranges::sized_range<const T>) {
        write_varuint(writer, static_cast<std::uint64_t>(std::ranges::size(value)));
        if constexpr (is_bulk_little_endian_contiguous_range_v<const T>) {
            encode_bulk_little_endian_range(writer, value);
        } else {
            for (const auto &element : value) {
                encode_value(writer, element);
            }
        }
    } else {
        using value_type = std::ranges::range_value_t<T>;
        std::vector<value_type> materialized;
        for (auto &&element : value) {
            materialized.emplace_back(std::forward<decltype(element)>(element));
        }
        write_varuint(writer, static_cast<std::uint64_t>(materialized.size()));
        for (const auto &element : materialized) {
            encode_value(writer, element);
        }
    }
}

template <typename T> constexpr status_code resize_decoded_range(T &value, std::uint64_t length) {
    using type = std::remove_cvref_t<T>;
    if constexpr (requires { typename type::size_type; }) {
        if (length > static_cast<std::uint64_t>(std::numeric_limits<typename type::size_type>::max())) {
            return status_code::error;
        }
    }
    if constexpr (requires { value.max_size(); }) {
        if (length > static_cast<std::uint64_t>(value.max_size())) {
            return status_code::error;
        }
    }
    value.resize(static_cast<typename type::size_type>(length));
    return status_code::success;
}

template <typename T> constexpr status_code decode_contiguous_bulk_range(span_reader &reader, T &value, std::uint64_t length) {
    using element_type = std::ranges::range_value_t<T>;
    if (byte_size_overflows<element_type>(length)) {
        return status_code::error;
    }
    const auto byte_count = static_cast<std::size_t>(length) * sizeof(element_type);
    if (byte_count > reader.remaining()) {
        return status_code::incomplete;
    }
    auto status = resize_decoded_range(value, length);
    if (status != status_code::success) {
        return status;
    }
    if constexpr (std::endian::native == std::endian::little) {
        std::span<const std::byte> bytes{};
        status = reader.read_bytes(byte_count, bytes);
        if (status != status_code::success) {
            return status;
        }
        if (byte_count != 0U) {
            std::memcpy(std::ranges::data(value), bytes.data(), byte_count);
        }
        return status_code::success;
    } else {
        for (auto &element : value) {
            status = decode_value(reader, element);
            if (status != status_code::success) {
                return status;
            }
        }
        return status_code::success;
    }
}

template <typename T> constexpr status_code decode_resizable_bulk_range(span_reader &reader, T &value, std::uint64_t length) {
    auto status = resize_decoded_range(value, length);
    if (status != status_code::success) {
        return status;
    }
    for (auto &element : value) {
        status = decode_value(reader, element);
        if (status != status_code::success) {
            return status;
        }
    }
    return status_code::success;
}

template <typename T> constexpr status_code decode_range(span_reader &reader, T &value) {
    std::uint64_t length{};
    auto          status = read_varuint(reader, length);
    if (status != status_code::success) {
        return status;
    }
    if constexpr (IsFixedArray<std::remove_cvref_t<T>>) {
        if (length != static_cast<std::uint64_t>(std::ranges::size(value))) {
            return status_code::unexpected_group_size;
        }
        for (auto &element : value) {
            status = decode_value(reader, element);
            if (status != status_code::success) {
                return status;
            }
        }
        return status_code::success;
    } else {
        if constexpr (requires { value.clear(); }) {
            value.clear();
        }
        if constexpr (ResizableRange<T> && is_bulk_little_endian_contiguous_range_v<T>) {
            return decode_contiguous_bulk_range(reader, value, length);
        } else if constexpr (ResizableRange<T> && is_bulk_little_endian_scalar_v<std::ranges::range_value_t<T>>) {
            return decode_resizable_bulk_range(reader, value, length);
        }
        status = reserve_decoded_container(value, length);
        if (status != status_code::success) {
            return status;
        }
        detail::appender<T> appender;
        for (std::uint64_t i = 0; i < length; ++i) {
            auto element = make_decode_value_for_container<typename T::value_type>(value);
            status       = decode_value(reader, element);
            if (status != status_code::success) {
                return status;
            }
            if constexpr (requires { value.push_back(std::move(element)); }) {
                value.push_back(std::move(element));
            } else {
                appender(value, std::move(element));
            }
        }
        return status_code::success;
    }
}

template <typename Writer, typename T> constexpr void encode_aggregate(Writer &writer, const T &value) {
    if constexpr (IsUntaggedTuple<std::remove_cvref_t<T>> || IsTaggedTuple<std::remove_cvref_t<T>>) {
        encode_tuple_fields(writer, value);
    } else {
        const auto tuple = cbor::tags::to_tuple(value);
        encode_tuple_fields(writer, tuple);
    }
}

template <typename T> constexpr status_code decode_aggregate(span_reader &reader, T &value) {
    if constexpr (IsUntaggedTuple<std::remove_cvref_t<T>> || IsTaggedTuple<std::remove_cvref_t<T>>) {
        return decode_tuple_fields(reader, value);
    } else {
        auto tuple = cbor::tags::to_tuple(value);
        return decode_tuple_fields(reader, tuple);
    }
}

template <typename Writer, typename T> constexpr void encode_value(Writer &writer, const T &value) {
    using type = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<type, std::byte> || std::is_same_v<type, bool> ||
                  (std::is_integral_v<type> && !std::is_same_v<type, bool>) || std::is_enum_v<type> || std::is_same_v<type, float16_t> ||
                  std::is_same_v<type, float> || std::is_same_v<type, double> || std::is_same_v<type, negative> ||
                  std::is_same_v<type, integer> || std::is_same_v<type, simple> || std::is_same_v<type, std::nullptr_t>) {
        encode_scalar(writer, value);
    } else if constexpr (is_std_array_v<type>) {
        encode_std_array(writer, value);
    } else if constexpr (IsOptional<type>) {
        encode_optional(writer, value);
    } else if constexpr (IsVariant<type>) {
        encode_variant(writer, value);
    } else if constexpr (is_text_string_v<type>) {
        encode_text_string(writer, value);
    } else if constexpr (IsBinaryString<type>) {
        encode_byte_string(writer, value);
    } else if constexpr (IsMap<type>) {
        encode_map(writer, value);
    } else if constexpr (IsRangeOfCborValues<type>) {
        encode_range(writer, value);
    } else if constexpr (IsUntaggedTuple<type> || IsTaggedTuple<type>) {
        encode_aggregate(writer, value);
    } else if constexpr (std::is_aggregate_v<type>) {
        encode_aggregate(writer, value);
    } else {
        static_assert(dependent_false<T>::value, "unsupported custom_codec_1 type");
    }
}

template <typename T> constexpr status_code decode_value(span_reader &reader, T &value) {
    using type = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<type, std::byte> || std::is_same_v<type, bool> ||
                  (std::is_integral_v<type> && !std::is_same_v<type, bool>) || std::is_enum_v<type> || std::is_same_v<type, float16_t> ||
                  std::is_same_v<type, float> || std::is_same_v<type, double> || std::is_same_v<type, negative> ||
                  std::is_same_v<type, integer> || std::is_same_v<type, simple> || std::is_same_v<type, std::nullptr_t>) {
        return decode_scalar(reader, value);
    } else if constexpr (is_std_array_v<type>) {
        return decode_std_array(reader, value);
    } else if constexpr (IsOptional<type>) {
        return decode_optional(reader, value);
    } else if constexpr (IsVariant<type>) {
        return decode_variant(reader, value);
    } else if constexpr (is_text_string_v<type>) {
        return decode_text_string(reader, value);
    } else if constexpr (IsBinaryString<type>) {
        return decode_byte_string(reader, value);
    } else if constexpr (IsMap<type>) {
        return decode_map(reader, value);
    } else if constexpr (IsRangeOfCborValues<type>) {
        return decode_range(reader, value);
    } else if constexpr (IsUntaggedTuple<type> || IsTaggedTuple<type>) {
        return decode_aggregate(reader, value);
    } else if constexpr (std::is_aggregate_v<type>) {
        return decode_aggregate(reader, value);
    } else {
        static_assert(dependent_false<T>::value, "unsupported custom_codec_1 type");
    }
}

template <typename Appender, typename Output, typename T>
constexpr void encode_payload_to(Appender &appender, Output &output, const T &value) {
    appender_writer writer{appender, output};
    encode_value(writer, value);
}

template <typename T> [[nodiscard]] constexpr std::size_t encoded_size(const T &value) {
    size_writer writer;
    encode_value(writer, value);
    return writer.size();
}

template <typename Segments, typename T> [[nodiscard]] inline Segments encode_payload_segments_as(const T &value) {
    Segments                               payload;
    cbor::tags::detail::appender<Segments> appender;
    encode_payload_to(appender, payload, value);
    return payload;
}

template <typename T> [[nodiscard]] inline cbor_segments encode_payload_segments(const T &value) {
    return encode_payload_segments_as<cbor_segments>(value);
}

template <typename Segments, typename T> [[nodiscard]] inline Segments encode_payload_borrowed_segments_as(const T &value) {
    Segments                          payload;
    borrowed_segment_writer<Segments> writer{payload};
    encode_value(writer, value);
    return payload;
}

template <typename T> [[nodiscard]] inline cbor_segments encode_payload_borrowed_segments(const T &value) {
    return encode_payload_borrowed_segments_as<cbor_segments>(value);
}

template <typename T> [[nodiscard]] inline std::vector<std::byte> encode_payload(const T &value) {
    auto payload = encode_payload_segments(value);
    return payload.flatten();
}

namespace borrowed {

template <typename T> struct has_borrowed_decode_refs;

template <typename Tuple, std::size_t... Indices> consteval bool tuple_has_borrowed_decode_refs_impl(std::index_sequence<Indices...>) {
    return (has_borrowed_decode_refs<std::remove_cvref_t<std::tuple_element_t<Indices, Tuple>>>::value || ...);
}

template <typename Tuple> struct tuple_has_borrowed_decode_refs {
    using tuple_type = std::remove_cvref_t<Tuple>;
    static constexpr bool value =
        tuple_has_borrowed_decode_refs_impl<tuple_type>(std::make_index_sequence<std::tuple_size_v<tuple_type>>{});
};

template <typename T> struct has_borrowed_decode_refs {
  private:
    using type = std::remove_cvref_t<T>;

    static consteval bool value_impl() {
        if constexpr (IsConstView<type> || is_basic_string_view_v<type>) {
            return true;
        } else if constexpr (is_std_array_v<type>) {
            return has_borrowed_decode_refs<typename type::value_type>::value;
        } else if constexpr (IsOptional<type>) {
            return has_borrowed_decode_refs<typename type::value_type>::value;
        } else if constexpr (IsVariant<type>) {
            return []<typename... Ts>(std::variant<Ts...> *) consteval {
                return (has_borrowed_decode_refs<std::remove_cvref_t<Ts>>::value || ...);
            }(static_cast<type *>(nullptr));
        } else if constexpr (IsMap<type>) {
            return has_borrowed_decode_refs<typename type::key_type>::value || has_borrowed_decode_refs<typename type::mapped_type>::value;
        } else if constexpr (IsRangeOfCborValues<type>) {
            return has_borrowed_decode_refs<typename type::value_type>::value;
        } else if constexpr (IsUntaggedTuple<type> || IsTaggedTuple<type>) {
            return tuple_has_borrowed_decode_refs<type>::value;
        } else if constexpr (std::is_aggregate_v<type>) {
            using tuple_type = std::remove_cvref_t<decltype(cbor::tags::to_tuple(std::declval<type &>()))>;
            return tuple_has_borrowed_decode_refs<tuple_type>::value;
        } else {
            return false;
        }
    }

  public:
    static constexpr bool value = value_impl();
};

} // namespace borrowed

template <typename T> inline constexpr bool has_borrowed_decode_refs_v = borrowed::has_borrowed_decode_refs<T>::value;

template <typename T> [[nodiscard]] constexpr status_code decode_payload(std::span<const std::byte> payload, T &value) {
    span_reader reader{payload};
    if constexpr (std::default_initializable<T> && std::assignable_from<T &, T>) {
        auto decoded = make_decode_value_for_existing(value);
        auto status  = decode_value(reader, decoded);
        if (status != status_code::success) {
            return status;
        }
        if (!reader.empty()) {
            return status_code::error;
        }
        value = std::move(decoded);
        return status_code::success;
    } else {
        if constexpr (std::copy_constructible<T> && std::assignable_from<T &, T>) {
            T    decoded{value};
            auto status = decode_value(reader, decoded);
            if (status != status_code::success) {
                return status;
            }
            if (!reader.empty()) {
                return status_code::error;
            }
            value = std::move(decoded);
            return status_code::success;
        }
        auto status = decode_value(reader, value);
        if (status != status_code::success) {
            return status;
        }
        return reader.empty() ? status_code::success : status_code::error;
    }
}

} // namespace cbor::tags::detail::custom_codec_1
