#pragma once

#include "cbor_tags/cbor_traversal.h"
#include "cbor_tags/detail/cbor_resumable_decode.h"
#include "cbor_tags/detail/cbor_utf8.h"

#include <cstddef>
#include <cstdint>

namespace cbor::tags {

struct until_eof_t {};
inline constexpr until_eof_t until_eof{};

namespace detail {

struct resumable_utf8_state {
    std::uint8_t first{};
    std::uint8_t remaining{};
    std::uint8_t position{};
    bool         invalid{};

    void consume(std::uint8_t byte) noexcept {
        if (remaining == 0U) {
            const auto length = utf8_sequence_length(byte);
            if (length == 0U) {
                invalid = true;
                return;
            }
            first     = byte;
            remaining = static_cast<std::uint8_t>(length - 1U);
            position  = 0;
        } else {
            ++position;
            if (!is_utf8_continuation_byte(byte) || (position == 1U && !is_valid_utf8_second_byte({.first = first, .second = byte})))
                invalid = true;
            --remaining;
        }
    }
    [[nodiscard]] bool valid() const noexcept { return !invalid && remaining == 0U; }
};

struct resumable_input_or_eof {
    resumable_io      &io;
    [[nodiscard]] bool await_ready() const { return io.terminal || io.has_input() || io.eof; }
    void               await_suspend(std::coroutine_handle<> handle) const noexcept {
        io.leaf    = handle;
        io.blocked = resumable_block::input;
    }
    [[nodiscard]] bool await_resume() const {
        io.check_cancelled();
        return io.has_input();
    }
};

inline resumable_task<status_code> resumable_validate_header(resumable_io &io, resumable_header header, std::size_t depth,
                                                             validation_options options) {
    const auto info = std::to_integer<std::uint8_t>(header.info);
    if (info >= 28U && info <= 30U)
        co_return status_code::invalid_additional_info;
    if (header.major == major_type::UnsignedInteger || header.major == major_type::NegativeInteger) {
        (void)co_await resumable_read_argument(io, header.info);
        co_return status_code::success;
    }
    if (header.major == major_type::Simple) {
        if (info == 31U)
            co_return status_code::malformed_structure;
        const auto simple_value = co_await resumable_read_argument(io, header.info);
        if (info == 24U && simple_value < 32U)
            co_return status_code::malformed_structure;
        co_return status_code::success;
    }
    if (header.major == major_type::Tag) {
        (void)co_await resumable_read_argument(io, header.info);
        if (depth >= options.max_depth)
            co_return status_code::size_limit_exceeded;
        const auto child = co_await resumable_read_header(io);
        co_return co_await resumable_validate_header(io, child, depth + 1U, options);
    }

    const bool indefinite = info == 31U;
    const auto size       = indefinite ? 0U : co_await resumable_read_argument(io, header.info);
    if (depth >= options.max_depth)
        co_return status_code::size_limit_exceeded;
    const bool text   = header.major == major_type::TextString;
    const bool string = text || header.major == major_type::ByteString;
    const bool map    = header.major == major_type::Map;
    if (string && !indefinite) {
        resumable_utf8_state utf8;
        for (std::uint64_t index = 0; index < size; ++index) {
            const auto byte = std::to_integer<std::uint8_t>(co_await resumable_read_byte{io});
            if (text)
                utf8.consume(byte);
        }
        co_return text && !utf8.valid() ? status_code::invalid_utf8_sequence : status_code::success;
    }

    auto remaining = size;
    while (indefinite || remaining != 0U) {
        const auto child      = co_await resumable_read_header(io);
        const auto child_info = std::to_integer<std::uint8_t>(child.info);
        if (child_info >= 28U && child_info <= 30U)
            co_return status_code::invalid_additional_info;
        if (resumable_break(child)) {
            co_return indefinite ? status_code::success : status_code::malformed_structure;
        }
        if (string && (child.major != header.major || child.info == std::byte{31})) {
            // The synchronous walker reads the complete child token before it
            // rejects an invalid chunk type, including its argument bytes.
            if (child.info != std::byte{31}) {
                (void)co_await resumable_read_argument(io, child.info);
            } else if (child.major == major_type::UnsignedInteger || child.major == major_type::NegativeInteger ||
                       child.major == major_type::Tag) {
                co_return status_code::invalid_additional_info;
            }
            co_return status_code::malformed_structure;
        }
        auto status = co_await resumable_validate_header(io, child, depth + 1U, options);
        if (status != status_code::success)
            co_return status;
        if (map) {
            const auto mapped = co_await resumable_read_header(io);
            status            = co_await resumable_validate_header(io, mapped, depth + 1U, options);
            if (status != status_code::success)
                co_return status;
        }
        if (!indefinite)
            --remaining;
    }
    co_return status_code::success;
}

inline resumable_task<status_code> resumable_validate_root(resumable_io &io, std::size_t count, bool stream, validation_options options) {
    std::size_t completed{};
    while (stream || completed < count) {
        if (stream && !(co_await resumable_input_or_eof{io}))
            co_return status_code::success;
        const auto header = co_await resumable_read_header(io);
        const auto status = co_await resumable_validate_header(io, header, 0U, options);
        if (status != status_code::success)
            co_return status;
        ++completed;
    }
    co_return status_code::success;
}

} // namespace detail
} // namespace cbor::tags
