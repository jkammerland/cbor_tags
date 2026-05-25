#pragma once

#include "cbor_tags/cbor.h"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace cbor::tags::detail {

template <typename Byte> constexpr std::uint8_t cbor_byte_to_u8(Byte value) {
    if constexpr (std::same_as<std::remove_cvref_t<Byte>, std::byte>) {
        return std::to_integer<std::uint8_t>(value);
    } else {
        return static_cast<std::uint8_t>(value);
    }
}

[[nodiscard]] constexpr bool is_cbor_break_byte(std::uint8_t value) noexcept { return value == 0xFFU; }

[[nodiscard]] constexpr bool is_reserved_simple_argument(std::uint8_t additional_info) noexcept {
    return additional_info == 28U || additional_info == 29U || additional_info == 30U || additional_info == 31U;
}

[[nodiscard]] constexpr bool is_valid_cbor_argument_info(std::uint8_t additional_info) noexcept { return additional_info <= 27U; }

[[nodiscard]] constexpr std::uint8_t cbor_argument_payload_size(std::uint8_t additional_info) noexcept {
    if (additional_info < 24U || !is_valid_cbor_argument_info(additional_info)) {
        return 0;
    }
    return static_cast<std::uint8_t>(1U << (additional_info - 24U));
}

struct cbor_argument_header {
    std::array<std::byte, 9> bytes{};
    std::size_t              size{};

    [[nodiscard]] std::span<const std::byte> span() const noexcept {
        const auto bounded_size = size <= bytes.size() ? size : bytes.size();
        return {bytes.data(), bounded_size};
    }
};

inline constexpr std::byte cbor_bstr_major_byte = get_major_3_bit_tag<as_bstr_any>();
inline constexpr std::byte cbor_tag_major_byte  = get_major_3_bit_tag<as_tag_any>();

template <typename EmitByte> constexpr void emit_cbor_major_argument(std::uint64_t value, std::uint8_t major_type, EmitByte &&emit_byte) {
    auto emit = [&emit_byte](std::uint64_t byte) { emit_byte(static_cast<std::uint8_t>(byte)); };

    if (value < 24U) {
        emit(major_type | value);
    } else if (value <= 0xFFU) {
        emit(major_type | 24U);
        emit(value);
    } else if (value <= 0xFFFFU) {
        emit(major_type | 25U);
        emit(value >> 8U);
        emit(value);
    } else if (value <= 0xFFFFFFFFULL) {
        emit(major_type | 26U);
        emit(value >> 24U);
        emit(value >> 16U);
        emit(value >> 8U);
        emit(value);
    } else {
        emit(major_type | 27U);
        emit(value >> 56U);
        emit(value >> 48U);
        emit(value >> 40U);
        emit(value >> 32U);
        emit(value >> 24U);
        emit(value >> 16U);
        emit(value >> 8U);
        emit(value);
    }
}

[[nodiscard]] constexpr cbor_argument_header encode_cbor_major_argument_header(std::uint64_t value, std::byte major_type) noexcept {
    cbor_argument_header header;
    emit_cbor_major_argument(value, cbor_byte_to_u8(major_type),
                             [&header](std::uint8_t byte) { header.bytes[header.size++] = static_cast<std::byte>(byte); });
    return header;
}

[[nodiscard]] constexpr cbor_argument_header encode_cbor_bstr_header(std::uint64_t size) noexcept {
    return encode_cbor_major_argument_header(size, cbor_bstr_major_byte);
}

[[nodiscard]] constexpr cbor_argument_header encode_cbor_tag_header(std::uint64_t tag) noexcept {
    return encode_cbor_major_argument_header(tag, cbor_tag_major_byte);
}

template <typename Appender, typename OutputBuffer, typename Byte>
constexpr void append_cbor_major_argument(Appender &appender, OutputBuffer &output, std::uint64_t value, Byte major_type) {
    using output_byte = typename std::remove_cvref_t<OutputBuffer>::value_type;
    const auto major  = cbor_byte_to_u8(major_type);
    auto       byte   = [](std::uint64_t value) { return static_cast<output_byte>(static_cast<std::uint8_t>(value)); };

    if (value < 24U) {
        appender(output, byte(major | value));
    } else if (value <= 0xFFU) {
        appender.multi_append(output, byte(major | 24U), byte(value));
    } else if (value <= 0xFFFFU) {
        appender.multi_append(output, byte(major | 25U), byte(value >> 8U), byte(value));
    } else if (value <= 0xFFFFFFFFULL) {
        appender.multi_append(output, byte(major | 26U), byte(value >> 24U), byte(value >> 16U), byte(value >> 8U), byte(value));
    } else {
        appender.multi_append(output, byte(major | 27U), byte(value >> 56U), byte(value >> 48U), byte(value >> 40U), byte(value >> 32U),
                              byte(value >> 24U), byte(value >> 16U), byte(value >> 8U), byte(value));
    }
}

template <typename ReadByte>
constexpr bool read_cbor_argument(std::uint8_t additional_info, std::uint64_t &value, status_code &status, ReadByte &&read_byte) {
    if (additional_info < 24U) {
        value = additional_info;
        return true;
    }
    if (!is_valid_cbor_argument_info(additional_info)) {
        status = status_code::error;
        return false;
    }

    const auto byte_count = cbor_argument_payload_size(additional_info);
    value                 = 0;
    for (std::uint8_t index = 0; index < byte_count; ++index) {
        std::uint8_t byte{};
        if (!read_byte(byte)) {
            return false;
        }
        value = (value << 8U) | byte;
    }
    return true;
}

[[nodiscard]] inline status_code read_cbor_argument_from_span(std::span<const std::byte> input, std::size_t &offset,
                                                              std::uint8_t additional_info, std::uint64_t &value) noexcept {
    if (!is_valid_cbor_argument_info(additional_info)) {
        return status_code::error;
    }

    const auto byte_count = cbor_argument_payload_size(additional_info);
    if (offset > input.size() || byte_count > (input.size() - offset)) {
        return status_code::incomplete;
    }

    auto       status = status_code::success;
    const auto ok     = read_cbor_argument(additional_info, value, status, [&input, &offset, &status](std::uint8_t &byte) noexcept -> bool {
        if (offset >= input.size()) {
            status = status_code::incomplete;
            return false;
        }
        byte = std::to_integer<std::uint8_t>(input[offset]);
        ++offset;
        return true;
    });

    return ok ? status_code::success : status;
}

} // namespace cbor::tags::detail
