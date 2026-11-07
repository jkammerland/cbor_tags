#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cbor_tags/cbor_resumable_source.h>
#include <cstddef>
#include <functional>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>

namespace resumable_test {

// A bounded public-API consumer with absolute positions. Windows are a template option,
// capacity reports a runtime one, so tests can still see a source without them.
// Tests retire the committed prefix, forcing live codec positions across wraparound.
template <bool Output, std::size_t Capacity, bool Windows = false> class ring_source {
  public:
    using value_type = std::byte;
    using size_type  = std::size_t;

    struct iterator {
        using value_type        = std::byte;
        using difference_type   = std::ptrdiff_t;
        using iterator_category = std::bidirectional_iterator_tag;
        using iterator_concept  = std::bidirectional_iterator_tag;

        const ring_source *source{};
        std::size_t        absolute{};

        const std::byte &operator*() const {
            if (absolute < source->retained_ || absolute >= source->limit()) {
                throw std::logic_error("dereferencing unavailable storage");
            }
            return source->bytes_[absolute % Capacity];
        }
        iterator &operator++() {
            ++absolute;
            return *this;
        }
        iterator operator++(int) {
            auto before = *this;
            ++*this;
            return before;
        }
        iterator &operator--() {
            --absolute;
            return *this;
        }
        iterator operator--(int) {
            auto before = *this;
            --*this;
            return before;
        }
        std::size_t offset() const { return absolute; }
        friend bool operator==(iterator, iterator) = default;
    };
    using position_type = iterator;

    explicit ring_source(std::size_t initial_offset = 100)
        : retained_(initial_offset), available_(initial_offset), working_(initial_offset), committed_(initial_offset) {}

    iterator    begin() const { return {this, retained_}; }
    iterator    end() const { return {this, limit()}; }
    std::size_t size() const { return limit() - retained_; }
    iterator    position() const { return {this, working_}; }
    iterator    committed_position() const noexcept { return {this, committed_}; }
    std::size_t remaining_size(iterator from) const { return limit() - from.offset(); }
    auto        remaining(iterator from) const { return std::ranges::subrange(from, end()); }
    auto        remaining_output() const { return std::ranges::subrange(position(), iterator{this, retained_ + Capacity}); }
    bool        closed() const { return closed_; }
    void        refresh() {}
    void        close() { closed_ = true; }

    void acquire() {
        if (active_) {
            throw std::logic_error("source already active");
        }
        active_ = true;
    }
    void release() noexcept { active_ = false; }

    void commit(iterator position) {
        if (position.source != this || position.offset() < committed_ || position.offset() > limit()) {
            throw std::logic_error("invalid input commitment");
        }
        committed_ = position.offset();
    }
    void commit() { committed_ = working_; }
    void restore(iterator position) noexcept {
        assert(position.source == this && position.offset() >= committed_ && position.offset() <= working_);
        working_ = position.offset();
    }

    // Application-side admission updates availability without a codec call.
    std::size_t append(std::span<const std::byte> bytes) {
        const auto count = std::min(bytes.size(), retained_ + Capacity - available_);
        for (std::size_t index = 0; index != count; ++index) {
            bytes_[(available_ + index) % Capacity] = bytes[index];
        }
        available_ += count;
        return count;
    }
    std::size_t write(std::span<const std::byte> bytes) {
        const auto count = std::min(bytes.size(), retained_ + Capacity - working_);
        for (std::size_t index = 0; index != count; ++index) {
            bytes_[(working_ + index) % Capacity] = bytes[index];
        }
        working_ += count;
        return count;
    }

    void reclaim(std::size_t through) {
        if (through < retained_ || through > committed_) {
            throw std::logic_error("reclamation exceeds committed progress");
        }
        retained_ = through;
    }

    // Windows stop where the storage wraps
    std::optional<std::span<const std::byte>> borrow_window(iterator &from) const
        requires(Windows && !Output)
    {
        const auto physical = from.absolute % Capacity;
        return std::span<const std::byte>{bytes_.data() + physical, std::min(limit() - from.absolute, Capacity - physical)};
    }
    void consume_window(iterator &from, std::size_t count) const
        requires(Windows && !Output)
    {
        if (count > limit() - from.absolute) {
            throw std::logic_error("consuming past the available input");
        }
        from.absolute += count;
    }
    std::optional<std::span<std::byte>> borrow_window()
        requires(Windows && Output)
    {
        const auto physical = working_ % Capacity;
        return std::span<std::byte>{bytes_.data() + physical, std::min(retained_ + Capacity - working_, Capacity - physical)};
    }
    void consume_window(std::size_t count)
        requires(Windows && Output)
    {
        if (count > retained_ + Capacity - working_) {
            throw std::logic_error("consuming past the output capacity");
        }
        working_ += count;
    }

    // Everything after the committed position, bytes to read or room to write,
    // already spans the ring, so reclaiming can't give the codec more
    bool at_capacity() const noexcept {
        if constexpr (Output) {
            return reports_capacity && retained_ + Capacity - committed_ == Capacity;
        } else {
            return reports_capacity && available_ - committed_ == Capacity;
        }
    }
    bool reports_capacity{};

    // Lets tests tell borrowed pieces from copies
    bool stores(const std::byte *pointer) const {
        return !std::less<const std::byte *>{}(pointer, bytes_.data()) && std::less<const std::byte *>{}(pointer, bytes_.data() + Capacity);
    }

  private:
    std::size_t limit() const {
        if constexpr (Output) {
            return working_;
        } else {
            return available_;
        }
    }

    std::array<std::byte, Capacity> bytes_;
    std::size_t                     retained_;
    std::size_t                     available_;
    std::size_t                     working_;
    std::size_t                     committed_;
    bool                            closed_{};
    bool                            active_{};
};

static_assert(cbor::tags::ResumableInputSource<ring_source<false, 11>>);
static_assert(cbor::tags::ResumableOutputSource<ring_source<true, 11>>);
static_assert(std::ranges::sized_range<const ring_source<false, 11>>);
static_assert(!std::ranges::contiguous_range<ring_source<false, 11>>);
static_assert(cbor::tags::ResumableInputSource<ring_source<false, 11, true>>);
static_assert(cbor::tags::ResumableOutputSource<ring_source<true, 11, true>>);
static_assert(cbor::tags::detail::HasInputWindow<ring_source<false, 11, true>, ring_source<false, 11, true>::iterator>);
static_assert(!cbor::tags::detail::HasInputWindow<ring_source<false, 11>, ring_source<false, 11>::iterator>);
static_assert(cbor::tags::detail::HasOutputWindow<ring_source<true, 11, true>>);
static_assert(!cbor::tags::detail::HasOutputWindow<ring_source<true, 11>>);

} // namespace resumable_test
