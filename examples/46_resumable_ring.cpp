#include "cbor_tags/cbor_resumable.h"
#include "cbor_tags/cbor_resumable_source.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <vector>

namespace ct = cbor::tags;

// The application owns this storage and serializes admission, publication and
// codec driving. Logical positions survive physical wraparound and reclamation.
template <bool Output, std::size_t Capacity> class ring_source {
    static_assert(Capacity > 0);

  public:
    using value_type = std::byte;
    using size_type = std::size_t;

    struct iterator {
        using value_type = std::byte;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::bidirectional_iterator_tag;
        using iterator_concept = std::bidirectional_iterator_tag;

        const ring_source *source{};
        std::size_t absolute{};

        const std::byte &operator*() const {
            assert(absolute >= source->retained_ && absolute < source->limit());
            return source->bytes_[absolute % Capacity];
        }
        iterator &operator++() {
            ++absolute;
            return *this;
        }
        iterator operator++(int) {
            auto previous = *this;
            ++*this;
            return previous;
        }
        iterator &operator--() {
            --absolute;
            return *this;
        }
        iterator operator--(int) {
            auto previous = *this;
            --*this;
            return previous;
        }
        std::size_t offset() const {
            return absolute;
        }
        friend bool operator==(iterator, iterator) = default;
    };
    using position_type = iterator;

    explicit ring_source(std::size_t initial_offset)
        : retained_(initial_offset), available_(initial_offset), working_(initial_offset),
          committed_(initial_offset) {
        check_capacity_end(initial_offset);
    }

    iterator begin() const {
        return {this, retained_};
    }
    iterator end() const {
        return {this, limit()};
    }
    std::size_t size() const {
        return limit() - retained_;
    }
    iterator committed_position() const noexcept {
        return {this, committed_};
    }
    bool eof() const {
        return closed_;
    }
    void close() {
        closed_ = true;
    }

    // Availability is read directly; no cached pointers need refreshing.
    void refresh() {
    }

    void acquire() {
        if (active_) {
            throw std::logic_error("ring source already has an active operation");
        }
        active_ = true;
    }
    void release() noexcept {
        active_ = false;
    }

    // Input reports retained size separately from its absolute stream offset.
    std::size_t remaining_size(iterator from) const {
        assert(from.source == this && from.offset() >= retained_ && from.offset() <= limit());
        return limit() - from.offset();
    }
    auto remaining(iterator from) const {
        return std::ranges::subrange(from, end());
    }
    void commit(iterator position) {
        assert(position.source == this && position.offset() >= committed_ &&
               position.offset() <= limit());
        committed_ = position.offset();
    }

    // Output separates tentative writes from the prefix safe to publish.
    iterator position() const {
        return {this, working_};
    }
    auto remaining_output() const {
        return std::ranges::subrange(position(), iterator{this, retained_ + Capacity});
    }
    void commit() {
        committed_ = working_;
    }
    void restore(iterator position) noexcept {
        assert(position.source == this && position.offset() >= committed_ &&
               position.offset() <= working_);
        working_ = position.offset();
    }
    std::size_t write(std::span<const std::byte> bytes)
        requires(Output)
    {
        assert(!closed_);
        return copy_in(bytes, working_);
    }

    // Producer admission is bounded by retained bytes, including tentative work.
    std::size_t append(std::span<const std::byte> bytes)
        requires(!Output)
    {
        assert(!closed_);
        return copy_in(bytes, available_);
    }

    // The caller must release every external borrow of this prefix first.
    void reclaim(iterator through) {
        assert(through.source == this && through.offset() >= retained_ &&
               through.offset() <= committed_);
        check_capacity_end(through.offset());
        retained_ = through.offset();
    }

  private:
    static void check_capacity_end(std::size_t retained) {
        if (retained > std::numeric_limits<std::size_t>::max() - Capacity) {
            throw std::length_error("ring source absolute offset overflow");
        }
    }

    std::size_t limit() const {
        if constexpr (Output) {
            return working_;
        } else {
            return available_;
        }
    }

    std::size_t copy_in(std::span<const std::byte> bytes, std::size_t &offset) {
        const auto count = std::min(bytes.size(), Capacity - (offset - retained_));
        for (std::size_t index = 0; index < count; ++index) {
            bytes_[(offset + index) % Capacity] = bytes[index];
        }
        offset += count;
        return count;
    }

    std::array<std::byte, Capacity> bytes_;
    std::size_t retained_;
    std::size_t available_;
    std::size_t working_;
    std::size_t committed_;
    bool closed_{};
    bool active_{};
};

static_assert(ct::ResumableInputSource<ring_source<false, 11>>);
static_assert(ct::ResumableOutputSource<ring_source<true, 11>>);
static_assert(std::ranges::sized_range<const ring_source<false, 11>>);
static_assert(!std::ranges::contiguous_range<const ring_source<false, 11>>);
static_assert(!std::ranges::contiguous_range<const ring_source<true, 11>>);

template <std::size_t Capacity>
void publish(ring_source<true, Capacity> &output, std::vector<std::byte> &transport) {
    // Copy only committed bytes. The local borrow ends before reclamation.
    {
        const auto committed = std::ranges::subrange(output.begin(), output.committed_position());
        transport.insert(transport.end(), committed.begin(), committed.end());
    }
    output.reclaim(output.committed_position());
}

/**
readings = [* uint]
*/
int main() {
    constexpr std::size_t initial_offset = 100;
    constexpr std::size_t capacity = 11;

    // Start with a nonzero absolute position and reuse the same fixed storage.
    ring_source<true, capacity> output(initial_offset);
    auto encoder = ct::make_resumable_encoder(output);

    // Every integer retry fits the ring; the complete array is much larger.
    const std::vector<std::uint32_t> sent{0, 1, 23, 24, 255, 256, 65'535, 65'536, 1'000'000, 23, 24,
        255, 256, 65'535, 65'536, 1'000'000, 255, 256, 65'535, 65'536, 1'000'000};
    auto encoding = encoder(sent);
    std::vector<std::byte> transport;
    std::size_t output_resumptions{};
    while (!encoding.done()) {
        const auto published_before = transport.size();
        publish(output, transport);
        assert(transport.size() > published_before);

        const auto state = encoding.resume();
        assert(state == ct::resume_state::need_output || state == ct::resume_state::done);
        ++output_resumptions;
    }
    publish(output, transport);

    const auto encoded = encoding.result();
    assert(encoded);
    assert(*encoded == transport.size());
    assert(encoding.committed_bytes() == transport.size());
    assert(output.committed_position().offset() == initial_offset + transport.size());
    assert(output.size() == 0);
    assert(output_resumptions > 1);
    assert(transport.size() > 3 * capacity);

    // Admit short chunks, preserving any uncommitted integer across each refill.
    ring_source<false, capacity> input(initial_offset);
    auto decoder = ct::make_resumable_decoder(input);
    std::vector<std::uint32_t> received;
    auto decoding = decoder(received);
    std::size_t supplied{};
    std::size_t input_resumptions{};
    while (!decoding.done()) {
        input.reclaim(input.committed_position());
        const auto pending = std::span<const std::byte>{transport}.subspan(supplied);
        const auto admitted = input.append(pending.first(std::min(pending.size(), std::size_t{5})));
        assert(admitted > 0);
        supplied += admitted;

        const auto state = decoding.resume();
        assert(state == ct::resume_state::need_input || state == ct::resume_state::done);
        ++input_resumptions;
    }

    // Owning decoded values retain no borrows into the reclaimed ring bytes.
    const auto decoded = decoding.result();
    assert(decoded);
    assert(*decoded == transport.size());
    assert(decoding.committed_bytes() == transport.size());
    assert(received == sent);
    assert(supplied == transport.size());
    assert(input.committed_position().offset() == initial_offset + transport.size());
    assert(input_resumptions > 1);
    input.reclaim(input.committed_position());
    assert(input.size() == 0);

    std::cout << "encoded and decoded " << transport.size() << " bytes through " << capacity
              << "-byte rings\n";
    return 0;
}
