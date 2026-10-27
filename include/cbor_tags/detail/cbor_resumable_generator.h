#pragma once

#include "cbor_tags/detail/cbor_resumable_decode.h"
#include "cbor_tags/detail/cbor_resumable_validate.h"

#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

namespace cbor::tags::detail {

template <typename Options, typename T, template <typename> typename... Codecs> consteval bool resumable_complete_item() {
    using context = resumable_decoder_context<Options, Codecs...>;
    return codec_payload_encodes_one_item<context, T>();
}

template <typename T, typename Options, template <typename> typename... Codecs>
resumable_task<status_code> resumable_items_root(resumable_io &io, std::shared_ptr<std::optional<T>> slot) {
    resumable_decoder_context<Options, Codecs...> decoder{io};
    while (co_await resumable_input_or_eof{io}) {
        T          item{};
        const auto status = co_await decoder.decode(item);
        if (status != status_code::success)
            co_return status;
        slot->emplace(std::move(item));
        co_await resumable_yield{io};
    }
    co_return status_code::success;
}

template <typename T, typename Options, template <typename> typename... Codecs>
resumable_task<status_code> resumable_elements_root(resumable_io &io, std::shared_ptr<std::optional<T>> slot) {
    resumable_decoder_context<Options, Codecs...> decoder{io};
    const auto                                    header = co_await resumable_read_header(io);
    if (header.major != major_type::Array)
        co_return status_code::no_match_for_array_on_buffer;
    const bool indefinite = header.info == std::byte{31};
    auto       remaining  = indefinite ? 0U : co_await resumable_read_argument(io, header.info);
    while (indefinite || remaining != 0U) {
        T           item{};
        status_code status{};
        if (indefinite) {
            const auto child = co_await resumable_read_header(io);
            if (resumable_break(child))
                co_return status_code::success;
            status = co_await decoder.decode_header(item, child);
        } else {
            status = co_await decoder.decode(item);
            --remaining;
        }
        if (status != status_code::success)
            co_return status;
        slot->emplace(std::move(item));
        co_await resumable_yield{io};
    }
    co_return status_code::success;
}

inline resumable_task<status_code> resumable_payload_part(resumable_io &io, std::uint64_t remaining,
                                                          std::shared_ptr<std::optional<readable_chunk>> slot) {
    while (remaining != 0U) {
        if (!(co_await resumable_input_or_eof{io}))
            co_return status_code::incomplete;
        const auto limit           = remaining > std::numeric_limits<std::size_t>::max() ? std::numeric_limits<std::size_t>::max()
                                                                                         : static_cast<std::size_t>(remaining);
        const auto available_count = std::numeric_limits<std::size_t>::max() - io.total;
        if (available_count == 0U)
            co_return status_code::size_limit_exceeded;
        std::size_t count{};
        auto        prefix = io.input[io.input_index].take(std::min(limit, available_count), count);
        io.total += count;
        remaining -= count;
        slot->emplace(std::move(prefix));
        co_await resumable_yield{io};
    }
    co_return status_code::success;
}

inline resumable_task<status_code> resumable_payload_root(resumable_io &io, major_type major,
                                                          std::shared_ptr<std::optional<readable_chunk>> slot) {
    const auto header = co_await resumable_read_header(io);
    if (header.major != major)
        co_return major_type_mismatch_status(major);
    if (header.info != std::byte{31}) {
        const auto length = co_await resumable_read_argument(io, header.info);
        co_return co_await resumable_payload_part(io, length, slot);
    }
    while (true) {
        const auto chunk = co_await resumable_read_header(io);
        if (resumable_break(chunk))
            co_return status_code::success;
        if (chunk.major != major || chunk.info == std::byte{31})
            co_return status_code::malformed_structure;
        const auto length = co_await resumable_read_argument(io, chunk.info);
        const auto status = co_await resumable_payload_part(io, length, slot);
        if (status != status_code::success)
            co_return status;
    }
}

} // namespace cbor::tags::detail
