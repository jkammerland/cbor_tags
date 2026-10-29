#pragma once

#include "cbor_tags/detail/cbor_extension_decode.h"
#include "cbor_tags/detail/cbor_extension_encode.h"
#include "cbor_tags/detail/cbor_resumable_control.h"
#include "cbor_tags/detail/cbor_resumable_source.h"
#include "cbor_tags/detail/cbor_typed_array_range.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <ranges>
#include <span>

namespace cbor::tags::detail {

// Retry only the small tagged header through the ordinary codec machinery.
template <std::uint64_t Tag, std::size_t ElementSize> struct resumable_typed_array_header {
    std::uint64_t byte_count{};

    template <typename Decoder> expected<void, status_code> decode(Decoder &decoder) {
        const auto [major, info] = decoder.read_initial_byte();
        const auto status        = decode_tagged_bstr_payload_header(decoder, Tag, major, info, [&](std::byte payload_info) {
            const auto argument_status = decode_unsigned_argument(decoder, payload_info, byte_count);
            if (argument_status != status_code::success) {
                return argument_status;
            }
            if (byte_count > std::numeric_limits<std::size_t>::max()) {
                return status_code::size_limit_exceeded;
            }
            if (byte_count % ElementSize != 0U) {
                return status_code::unexpected_group_size;
            }
            return status_code::success;
        });
        if (status != status_code::success) {
            return unexpected<status_code>{status};
        }
        return {};
    }

    template <typename Encoder> expected<void, status_code> encode(Encoder &encoder) const {
        encoder.encode(static_tag<Tag>{});
        encode_extension_bstr_header(encoder, byte_count);
        return {};
    }
};

template <std::uint64_t Tag, bool NativeOrder, typename Context, typename Array, typename Convert>
resumable<status_code> resumable_decode_typed_array(Context &context, Array &array, Convert convert) {
    using value_type            = typename Array::value_type;
    constexpr auto element_size = sizeof(value_type);
    static_assert(element_size <= 16U);

    resumable_typed_array_header<Tag, element_size> header;
    const auto                                      header_status = co_await context.ordinary(header);
    if (header_status != status_code::success) {
        co_return header_status;
    }

    // Admission and allocation bounds belong to the caller of the resumable API.
    const auto element_count = static_cast<std::size_t>(header.byte_count / element_size);
    auto      &values        = array.values();
    values.clear();
    values.reserve(element_count);

    auto                               &reader = context.ordinary().reader_;
    std::array<std::byte, element_size> boundary{};
    std::size_t                         boundary_size{};
    while (values.size() != element_count) {
        const auto available = context.source().borrow_window(reader.position_);
        if (available ? available->empty() : context.empty()) {
            if (context.eof()) {
                co_return status_code::incomplete;
            }
            co_await context.wait();
            continue;
        }

        if (available) {
            // Complete elements go straight into their final destination storage.
            if (boundary_size == 0U && available->size() >= element_size) {
                const auto count = std::min(element_count - values.size(), available->size() / element_size);
                const auto bytes = count * element_size;
                if constexpr (NativeOrder) {
                    const auto start = values.size();
                    values.resize(start + count);
                    std::memcpy(values.data() + start, available->data(), bytes);
                } else {
                    for (std::size_t offset = 0; offset != bytes; offset += element_size) {
                        values.push_back(convert(available->subspan(offset, element_size)));
                    }
                }
                context.source().consume_window(reader.position_, bytes);
                reader.current_offset_ += bytes;
                context.commit();
                continue;
            }

            const auto count = std::min(element_size - boundary_size, available->size());
            std::memcpy(boundary.data() + boundary_size, available->data(), count);
            context.source().consume_window(reader.position_, count);
            reader.current_offset_ += count;
            boundary_size += count;
        } else {
            boundary[boundary_size++] = reader.read(context.source());
        }

        // A split element stays private until all of its representation is present.
        if (boundary_size == element_size) {
            values.push_back(convert(std::span<const std::byte>{boundary}));
            boundary_size = 0;
            context.commit();
        }
    }
    co_return status_code::success;
}

// Output bytes may span several windows, but an element is published atomically.
template <typename Context>
resumable<status_code> resumable_encode_typed_array_boundary(Context &context, std::span<const std::byte> bytes) {
    while (!bytes.empty()) {
        const auto available = resumable_source_access::borrow_window(context.source());
        if (available && !available->empty()) {
            const auto count = std::min(bytes.size(), available->size());
            std::memcpy(available->data(), bytes.data(), count);
            resumable_source_access::consume_window(context.source(), count);
            bytes = bytes.subspan(count);
            continue;
        }
        if (!available) {
            bool written{};
            try {
                context.source().push_back(bytes.front());
                written = true;
            } catch (const encode_status_exception &error) {
                if (error.status != status_code::incomplete) {
                    co_return error.status;
                }
            }
            if (written) {
                bytes = bytes.subspan(1);
                continue;
            }
        }
        if (context.eof()) {
            context.rollback();
            co_return status_code::incomplete;
        }
        co_await context.wait();
    }
    context.commit();
    co_return status_code::success;
}

template <std::uint64_t Tag, typename Context, typename Range, typename Fill>
resumable<status_code> resumable_encode_typed_array(Context &context, Range &values, Fill fill) {
    using value_type            = std::ranges::range_value_t<Range>;
    constexpr auto element_size = sizeof(value_type);
    static_assert(element_size <= 16U);

    const auto count = typed_array_range_size(values);
    if (count > std::numeric_limits<std::uint64_t>::max() / element_size) {
        co_return status_code::size_limit_exceeded;
    }
    resumable_typed_array_header<Tag, element_size> header{count * element_size};
    const auto                                      header_status = co_await context.ordinary(header);
    if (header_status != status_code::success) {
        co_return header_status;
    }

    if constexpr (std::ranges::contiguous_range<Range> && std::ranges::sized_range<Range>) {
        auto remaining = std::span<const value_type>{std::ranges::data(values), std::ranges::size(values)};
        while (!remaining.empty()) {
            const auto available = resumable_source_access::borrow_window(context.source());
            if (available && available->size() >= element_size) {
                const auto elements = std::min(remaining.size(), available->size() / element_size);
                const auto bytes    = elements * element_size;
                fill(available->first(bytes), remaining.first(elements));
                resumable_source_access::consume_window(context.source(), bytes);
                context.commit();
                remaining = remaining.subspan(elements);
                continue;
            }

            // Only the element crossing an output boundary needs scratch storage.
            std::array<std::byte, element_size> boundary{};
            fill(std::span<std::byte>{boundary}, remaining.first(1));
            const auto status = co_await resumable_encode_typed_array_boundary(context, std::span<const std::byte>{boundary});
            if (status != status_code::success) {
                co_return status;
            }
            remaining = remaining.subspan(1);
        }
    } else {
        for (const auto &value : values) {
            std::array<std::byte, element_size> boundary{};
            fill(std::span<std::byte>{boundary}, std::span<const value_type>{&value, 1});
            const auto status = co_await resumable_encode_typed_array_boundary(context, std::span<const std::byte>{boundary});
            if (status != status_code::success) {
                co_return status;
            }
        }
    }
    co_return status_code::success;
}

} // namespace cbor::tags::detail
