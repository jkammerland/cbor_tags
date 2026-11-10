#pragma once

#include "cbor_tags/detail/cbor_extension_decode.h"
#include "cbor_tags/detail/cbor_extension_encode.h"
#include "cbor_tags/detail/cbor_resumable_control.h"
#include "cbor_tags/detail/cbor_resumable_retry.h"
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
#include <type_traits>
#include <utility>

namespace cbor::tags::detail {

// Retry only the small tagged header through the one-shot codec machinery.
// Lengths get checked here too, so a rejected array commits nothing.
template <std::uint64_t Tag, std::size_t ElementSize, typename Destination = void> struct resumable_typed_array_header {
    std::uint64_t byte_count{};
    std::uint64_t min_count{};
    std::uint64_t max_count{std::numeric_limits<std::uint64_t>::max()};
    Destination  *destination{};

    template <typename Decoder> expected<void, status_code> decode(Decoder &decoder) {
        const auto [major, info] = decoder.read_initial_byte();
        const auto status        = decode_tagged_bstr_payload_header(decoder, Tag, major, info, [&](std::byte payload_info) {
            const auto argument_status = decode_unsigned_argument(decoder, payload_info, byte_count);
            if (argument_status != status_code::success) {
                return argument_status;
            }
            if (byte_count > std::numeric_limits<std::size_t>::max() || !within_bounds()) {
                return status_code::size_limit_exceeded;
            }
            if (byte_count % ElementSize != 0U) {
                return status_code::unexpected_group_size;
            }
            if constexpr (std::is_void_v<Destination>) {
                return status_code::success;
            } else {
                return typed_array_destination_size_status(*destination, byte_count / ElementSize);
            }
        });
        if (status != status_code::success) {
            return unexpected<status_code>{status};
        }
        return {};
    }

    // Compares bytes like one-shot decoding, so an odd length past a bound reports the bound
    [[nodiscard]] constexpr bool within_bounds() const noexcept {
        constexpr auto max_elements = std::numeric_limits<std::uint64_t>::max() / ElementSize;
        if (min_count > max_elements || byte_count < min_count * ElementSize) {
            return false;
        }
        return max_count > max_elements || byte_count <= max_count * ElementSize;
    }

    template <typename Encoder> expected<void, status_code> encode(Encoder &encoder) const {
        encoder.encode(static_tag<Tag>{});
        encode_extension_bstr_header(encoder, byte_count);
        return {};
    }
};

// Clears growable destinations. Returns where insert_after or a fixed
// non-contiguous range writes next, the others don't need a position.
template <typename R> auto typed_array_writer_start(R &range, std::size_t count) {
    if constexpr (TypedArrayAppendableRange<R>) {
        range.clear();
        if constexpr (requires { range.reserve(count); }) {
            range.reserve(count);
        }
    }

    if constexpr (TypedArrayBackAppendableRange<R> || (!TypedArrayAppendableRange<R> && std::ranges::contiguous_range<R>)) {
        return nullptr;
    } else if constexpr (TypedArrayAfterAppendableRange<R>) {
        return range.before_begin();
    } else {
        return std::ranges::begin(range);
    }
}

// Puts finished elements into the destination. Growable ones are cleared and
// appended to, fixed ones are overwritten from the front.
template <typename R> class typed_array_writer {
  public:
    using value_type = std::ranges::range_value_t<R>;

    typed_array_writer(R &range, std::size_t count) : range_(range), cursor_(typed_array_writer_start(range, count)) {}

    [[nodiscard]] std::size_t size() const noexcept { return written_; }

    // bytes holds count whole elements
    template <bool NativeOrder, typename Convert> void append(std::span<const std::byte> bytes, std::size_t count, Convert &convert) {
        constexpr bool copy = NativeOrder && std::is_trivially_copyable_v<value_type>;
        constexpr bool grow =
            resizes && (copy ? std::is_default_constructible_v<value_type>
                             : std::is_nothrow_default_constructible_v<value_type> && std::is_nothrow_move_assignable_v<value_type>);
        if constexpr (grow) {
            // Grow once per window instead of once per element
            range_.resize(written_ + count);
            fill<copy>(std::ranges::data(range_) + written_, bytes, count, convert);
        } else if constexpr (fixed_contiguous) {
            fill<copy>(std::ranges::data(range_) + written_, bytes, count, convert);
        } else {
            for (std::size_t index = 0; index != count; ++index) {
                push(convert(bytes.subspan(index * sizeof(value_type), sizeof(value_type))));
            }
            return;
        }
        written_ += count;
    }

    void push(value_type value) {
        if constexpr (TypedArrayBackAppendableRange<R>) {
            range_.push_back(std::move(value));
        } else if constexpr (TypedArrayAfterAppendableRange<R>) {
            cursor_ = range_.insert_after(cursor_, std::move(value));
        } else if constexpr (fixed_contiguous) {
            std::ranges::data(range_)[written_] = std::move(value);
        } else {
            *cursor_ = std::move(value);
            ++cursor_;
        }
        ++written_;
    }

  private:
    static constexpr bool appendable       = TypedArrayAppendableRange<R>;
    static constexpr bool fixed_contiguous = !appendable && std::ranges::contiguous_range<R>;
    static constexpr bool resizes = appendable && std::ranges::contiguous_range<R> && requires(R &range) { range.resize(std::size_t{}); };

    template <bool Copy, typename Convert>
    static void fill(value_type *destination, std::span<const std::byte> bytes, std::size_t count, Convert &convert) {
        if constexpr (Copy) {
            std::memcpy(destination, bytes.data(), count * sizeof(value_type));
        } else {
            for (std::size_t index = 0; index != count; ++index) {
                destination[index] = convert(bytes.subspan(index * sizeof(value_type), sizeof(value_type)));
            }
        }
    }

    R                                                                     &range_;
    decltype(typed_array_writer_start(std::declval<R &>(), std::size_t{})) cursor_;
    std::size_t                                                            written_{};
};

// Destination ranges are checked against the count first, like one-shot
// decoding. Owning arrays aren't.
template <std::uint64_t Tag, bool NativeOrder, bool CheckDestination, typename Context, typename R, typename Convert>
resumable_task<status_code> resumable_decode_typed_array(Context &context, R &range, std::uint64_t min_count, std::uint64_t max_count,
                                                         Convert convert) {
    using value_type            = std::ranges::range_value_t<R>;
    constexpr auto element_size = sizeof(value_type);
    static_assert(element_size <= 16U);

    resumable_typed_array_header<Tag, element_size, std::conditional_t<CheckDestination, R, void>> header{0, min_count, max_count};
    if constexpr (CheckDestination) {
        header.destination = &range;
    }
    const auto header_status = co_await context.one_shot(header);
    if (header_status != status_code::success) {
        co_return header_status;
    }

    // Admission and allocation bounds belong to the caller of the resumable API.
    const auto            element_count = static_cast<std::size_t>(header.byte_count / element_size);
    typed_array_writer<R> values{range, element_count};

    auto                               &source = resumable_context_access::source(context);
    auto                               &reader = resumable_context_access::one_shot(context).reader_;
    std::array<std::byte, element_size> boundary{};
    std::size_t                         boundary_size{};
    while (values.size() != element_count) {
        const auto available = resumable_source_access::borrow_window(source, reader.position_);
        if (available ? available->empty() : resumable_context_access::empty(context)) {
            co_await resumable_context_access::wait(context);
            continue;
        }

        if (available) {
            // Complete elements go straight into their final destination storage.
            if (boundary_size == 0U && available->size() >= element_size) {
                const auto count = std::min(element_count - values.size(), available->size() / element_size);
                const auto bytes = count * element_size;
                values.template append<NativeOrder>(available->first(bytes), count, convert);
                resumable_source_access::consume_window(source, reader.position_, bytes);
                reader.current_offset_ += bytes;
                resumable_context_access::commit(context);
                continue;
            }

            const auto count = std::min(element_size - boundary_size, available->size());
            std::memcpy(boundary.data() + boundary_size, available->data(), count);
            resumable_source_access::consume_window(source, reader.position_, count);
            reader.current_offset_ += count;
            boundary_size += count;
        } else {
            boundary[boundary_size++] = reader.read(source);
        }

        // A split element stays private until all of its representation is present.
        if (boundary_size == element_size) {
            values.push(convert(std::span<const std::byte>{boundary}));
            boundary_size = 0;
            resumable_context_access::commit(context);
        }
    }
    co_return status_code::success;
}

// Output bytes may span several windows, but an element is published atomically.
template <typename Context>
resumable_task<status_code> resumable_encode_typed_array_boundary(Context &context, std::span<const std::byte> bytes) {
    auto &source = resumable_context_access::source(context);
    while (!bytes.empty()) {
        const auto available = resumable_source_access::borrow_window(source);
        if (available && !available->empty()) {
            const auto count = std::min(bytes.size(), available->size());
            std::memcpy(available->data(), bytes.data(), count);
            resumable_source_access::consume_window(source, count);
            bytes = bytes.subspan(count);
            continue;
        }
        if (!available) {
            const auto count = source.write(bytes);
            if (count != 0) {
                bytes = bytes.subspan(count);
                continue;
            }
        }
        co_await resumable_context_access::wait(context);
    }
    resumable_context_access::commit(context);
    co_return status_code::success;
}

template <std::uint64_t Tag, typename Context, typename Range, typename Fill>
resumable_task<status_code> resumable_encode_typed_array(Context &context, Range &values, Fill fill) {
    using value_type            = std::ranges::range_value_t<Range>;
    constexpr auto element_size = sizeof(value_type);
    static_assert(element_size <= 16U);

    const auto count = typed_array_range_size(values);
    if (count > std::numeric_limits<std::uint64_t>::max() / element_size) {
        co_return status_code::size_limit_exceeded;
    }
    resumable_typed_array_header<Tag, element_size> header{count * element_size};
    const auto                                      header_status = co_await context.one_shot(header);
    if (header_status != status_code::success) {
        co_return header_status;
    }

    if constexpr (std::ranges::contiguous_range<Range> && std::ranges::sized_range<Range>) {
        auto &source    = resumable_context_access::source(context);
        auto  remaining = std::span<const value_type>{std::ranges::data(values), std::ranges::size(values)};
        while (!remaining.empty()) {
            const auto available = resumable_source_access::borrow_window(source);
            if (available && available->size() >= element_size) {
                const auto elements = std::min(remaining.size(), available->size() / element_size);
                const auto bytes    = elements * element_size;
                fill(available->first(bytes), remaining.first(elements));
                resumable_source_access::consume_window(source, bytes);
                resumable_context_access::commit(context);
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

namespace cbor::tags {

template <std::uint64_t Tag, std::size_t ElementSize, typename Destination>
struct resumable_retry_traits<detail::resumable_typed_array_header<Tag, ElementSize, Destination>> {
    static constexpr bool encode = true;
    static constexpr bool decode = true;
};

} // namespace cbor::tags
