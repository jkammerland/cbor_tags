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
#include <optional>
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
        return copy_in(bytes, working_);
    }

    // Producer admission is bounded by retained bytes, including tentative work.
    std::size_t append(std::span<const std::byte> bytes)
        requires(!Output)
    {
        return copy_in(bytes, available_);
    }

    // The caller must release every external borrow of this prefix first.
    void reclaim(iterator through) {
        assert(through.source == this && through.offset() >= retained_ &&
               through.offset() <= committed_);
        check_capacity_end(through.offset());
        retained_ = through.offset();
    }

    // Optional windows let the codec copy whole runs of the storage instead of
    // single bytes, and let sinks see input in place. A run ends where the
    // storage wraps.
    std::optional<std::span<const std::byte>> borrow_window(iterator &from) const
        requires(!Output)
    {
        const auto physical = from.offset() % Capacity;
        return std::span<const std::byte>{bytes_.data() + physical,
            std::min(limit() - from.offset(), Capacity - physical)};
    }
    void consume_window(iterator &from, std::size_t count) const
        requires(!Output)
    {
        assert(count <= limit() - from.offset());
        from.absolute += count;
    }
    std::optional<std::span<std::byte>> borrow_window()
        requires(Output)
    {
        const auto physical = working_ % Capacity;
        return std::span<std::byte>{bytes_.data() + physical,
            std::min(retained_ + Capacity - working_, Capacity - physical)};
    }
    void consume_window(std::size_t count)
        requires(Output)
    {
        assert(count <= retained_ + Capacity - working_);
        working_ += count;
    }

    // Also optional. Everything after the committed position, bytes to read or
    // room to write, already spans the ring, so reclaiming can't give a waiting
    // codec more. Its operation fails with capacity_exceeded instead of waiting
    // forever.
    bool at_capacity() const noexcept {
        if constexpr (Output) {
            return retained_ + Capacity - committed_ == Capacity;
        } else {
            return available_ - committed_ == Capacity;
        }
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
    bool active_{};
};

static_assert(ct::ResumableInputSource<ring_source<false, 11>>);
static_assert(ct::ResumableOutputBuffer<ring_source<true, 11>>);
static_assert(std::ranges::sized_range<const ring_source<false, 11>>);
static_assert(!std::ranges::contiguous_range<const ring_source<false, 11>>);
static_assert(!std::ranges::contiguous_range<const ring_source<true, 11>>);

// FNV-1a, so the image can be checked without storing it
constexpr std::uint32_t fnv1a_basis = 2'166'136'261U;
std::uint32_t fnv1a(std::uint32_t hash, std::span<const std::byte> bytes) {
    for (const auto byte : bytes) {
        hash = (hash ^ std::to_integer<std::uint32_t>(byte)) * 16'777'619U;
    }
    return hash;
}

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
image = bstr
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

    // The image is many times the ring. Its payload passes through in pieces.
    std::vector<std::byte> image(1'000);
    for (std::size_t index = 0; index < image.size(); ++index) {
        image[index] = static_cast<std::byte>(index * 7);
    }
    auto encoding = encoder(sent, image);
    std::vector<std::byte> transport;
    std::size_t output_resumptions{};
    while (!encoding.done()) {
        const auto published_before = transport.size();
        publish(output, transport);
        assert(transport.size() > published_before);

        const auto state = encoding.resume();
        assert(state == ct::resume_state::need_output || state == ct::resume_state::finished);
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
    assert(transport.size() > image.size());

    // Admit short chunks, preserving any uncommitted integer across each refill.
    ring_source<false, capacity> input(initial_offset);
    auto decoder = ct::make_resumable_decoder(input);
    std::vector<std::uint32_t> received;

    // The sink hashes the image as it arrives instead of storing it. Each piece
    // points into the ring and is only valid during the call.
    std::uint32_t image_hash = fnv1a_basis;
    std::size_t image_pieces{};
    auto decoding = decoder(received, ct::as_bstr_sink([&](std::span<const std::byte> piece) {
        assert(piece.size() <= capacity);
        image_hash = fnv1a(image_hash, piece);
        ++image_pieces;
    }));
    std::size_t supplied{};
    std::size_t input_resumptions{};
    while (!decoding.done()) {
        input.reclaim(input.committed_position());
        const auto pending = std::span<const std::byte>{transport}.subspan(supplied);
        const auto admitted = input.append(pending.first(std::min(pending.size(), std::size_t{5})));
        assert(admitted > 0);
        supplied += admitted;

        const auto state = decoding.resume();
        assert(state == ct::resume_state::need_input || state == ct::resume_state::finished);
        ++input_resumptions;
    }

    // Owning decoded values retain no borrows into the reclaimed ring bytes.
    const auto decoded = decoding.result();
    assert(decoded);
    assert(*decoded == transport.size());
    assert(decoding.committed_bytes() == transport.size());
    assert(received == sent);
    assert(image_hash == fnv1a(fnv1a_basis, image));
    assert(image_pieces > 1);
    assert(supplied == transport.size());
    assert(input.committed_position().offset() == initial_offset + transport.size());
    assert(input_resumptions > 1);
    input.reclaim(input.committed_position());
    assert(input.size() == 0);

    // A view of the image would need all of it in the ring at once. Thanks to
    // at_capacity() that's an error instead of a wait that never ends.
    ring_source<false, capacity> view_input(initial_offset);
    auto view_decoder = ct::make_resumable_decoder(view_input);
    std::vector<std::uint32_t> readings;
    decltype(view_decoder)::bstr_view_t image_view;
    auto viewing = view_decoder(readings, image_view);
    for (std::size_t offered{}; !viewing.done();) {
        view_input.reclaim(view_input.committed_position());
        offered += view_input.append(std::span<const std::byte>{transport}.subspan(offered));
        viewing.resume();
    }
    assert(!viewing.result());
    assert(viewing.result().error() == ct::status_code::capacity_exceeded);

    std::cout << "encoded and decoded " << transport.size() << " bytes through " << capacity
              << "-byte rings\n";
    return 0;
}
