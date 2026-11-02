#pragma once

#include "cbor_tags/cbor_concepts.h"

#include <concepts>
#include <cstddef>
#include <ranges>
#include <span>

namespace cbor::tags {

// Positions describe stream progress independently of the current allocation.
// An adapter must preserve positions at and after its committed boundary across
// refresh(), including a working position beyond that boundary.
template <typename Position>
concept ResumablePosition = std::copyable<Position> && requires(const Position &position) {
    { position.offset() } -> std::same_as<std::size_t>;
};

template <typename Source>
concept HasResumableSizeType = requires { typename Source::size_type; };

template <typename Source>
concept ResumableSizeType = (!HasResumableSizeType<Source>) || std::same_as<typename Source::size_type, std::size_t>;

template <typename Source>
concept ResumableRangeSize =
    (!std::ranges::sized_range<const Source>) || std::same_as<std::ranges::range_size_t<const Source>, std::size_t>;

template <typename Source>
concept ResumableSource = ResumableSizeType<Source> && requires(Source &source, const Source &constant) {
    typename Source::position_type;
    requires ResumablePosition<typename Source::position_type>;
    { constant.committed_position() } noexcept -> std::same_as<typename Source::position_type>;
    { constant.eof() } -> std::same_as<bool>;
    { source.refresh() } -> std::same_as<void>;
    { source.close() } -> std::same_as<void>;
    { source.acquire() } -> std::same_as<void>;
    { source.release() } noexcept -> std::same_as<void>;
};

// Sources expose logical byte iterators rather than a contiguous-range identity.
// Sized sources provide remaining_size(position); optional window hooks provide
// bulk access without requiring every source to expose contiguous storage.
template <typename Source>
concept ResumableInputSource =
    ResumableSource<Source> && CborInputBuffer<Source> && ResumableRangeSize<Source> && (!std::ranges::contiguous_range<Source>) &&
    (!std::ranges::contiguous_range<const Source>) && std::same_as<typename Source::position_type, std::ranges::iterator_t<const Source>> &&
    (!std::ranges::sized_range<const Source> || requires(const Source &source, typename Source::position_type position) {
        { source.remaining_size(position) } -> std::same_as<std::size_t>;
    }) && requires(Source &source, const Source &constant, typename Source::position_type position) {
        { source.commit(position) } -> std::same_as<void>;
        { constant.remaining(position) } -> std::ranges::range;
    };

// write() advances the working position by its returned count. A short write
// means temporary exhaustion; ordinary codec adaptation is library-owned.
// Optional borrow_window()/consume_window(n) expose writable constructed bytes.
template <typename Source>
concept ResumableOutputSource =
    ResumableSource<Source> && std::ranges::forward_range<const Source> &&
    requires(Source &source, const Source &constant, typename Source::position_type position, std::span<const std::byte> bytes) {
        typename Source::value_type;
        typename Source::size_type;
        requires std::same_as<typename Source::value_type, std::byte>;
        { constant.position() } -> std::same_as<typename Source::position_type>;
        { source.commit() } -> std::same_as<void>;
        { source.restore(position) } noexcept -> std::same_as<void>;
        { source.write(bytes) } -> std::same_as<std::size_t>;
        { constant.remaining_output() } -> std::ranges::range;
    };

} // namespace cbor::tags

#include "cbor_tags/detail/cbor_resumable_source.h"
