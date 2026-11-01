#pragma once

#include "cbor_tags/cbor_detail.h"
#include "cbor_tags/cbor_resumable_source.h"
#include "cbor_tags/detail/cbor_encode_error.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <optional>
#include <stdexcept>

namespace cbor::tags {

namespace detail {

template <typename Source>
concept HasOutputWindow = requires(Source &source, std::size_t count) {
    { source.borrow_window() } -> std::convertible_to<std::optional<std::span<std::byte>>>;
    { source.consume_window(count) } -> std::same_as<void>;
};

template <typename Source, typename Position>
concept HasInputWindow = requires(const Source &source, Position &position) {
    { source.borrow_window(position) } -> std::convertible_to<std::optional<std::span<const std::byte>>>;
};

template <typename Source, typename Position>
concept HasInputWindowConsumption = requires(const Source &source, Position &position, std::size_t count) {
    { source.consume_window(position, count) } -> std::same_as<void>;
};

template <typename Chunks, typename Position>
concept HasBulkChunkWrite = requires(Chunks &chunks, Position &position, std::span<const std::byte> bytes) {
    { chunks.write_bytes(position, bytes) } -> std::same_as<std::size_t>;
};

// Adapters can omit bulk windows. Byte traversal remains the baseline contract.
struct resumable_source_access {
    template <typename Source> static std::optional<std::span<std::byte>> borrow_window(Source &source) {
        if constexpr (HasOutputWindow<Source>)
            return source.borrow_window();
        else
            return std::nullopt;
    }
    template <typename Source> static void consume_window(Source &source, std::size_t count) {
        if constexpr (HasOutputWindow<Source>)
            source.consume_window(count);
        else if (count != 0)
            throw std::logic_error("CBOR output source has no writable window");
    }
    template <typename Source, typename Position>
    static std::optional<std::span<const std::byte>> borrow_window(const Source &source, Position &position) {
        if constexpr (HasInputWindow<Source, Position>)
            return source.borrow_window(position);
        else
            return std::nullopt;
    }
    template <typename Source, typename Position> static void consume_window(const Source &source, Position &position, std::size_t count) {
        if constexpr (HasInputWindowConsumption<Source, Position>)
            source.consume_window(position, count);
        else
            std::ranges::advance(position, static_cast<std::iter_difference_t<Position>>(count));
    }
};

template <typename Chunk>
concept ResumableByteChunk =
    std::ranges::bidirectional_range<Chunk> && std::ranges::common_range<Chunk> && IsCborBufferByte<std::ranges::range_value_t<Chunk>> &&
    std::is_lvalue_reference_v<std::ranges::range_reference_t<Chunk>> &&
    (!std::is_volatile_v<std::remove_reference_t<std::ranges::range_reference_t<Chunk>>>);

template <typename Storage>
concept ResumableChunkStorage =
    std::ranges::bidirectional_range<Storage> && std::ranges::common_range<Storage> &&
    std::is_lvalue_reference_v<std::ranges::range_reference_t<Storage>> && ResumableByteChunk<std::ranges::range_value_t<Storage>>;

template <typename Storage>
concept ResumableOutputChunkStorage =
    ResumableChunkStorage<Storage> && std::indirectly_writable<std::ranges::iterator_t<std::ranges::range_value_t<Storage>>,
                                                               std::ranges::range_value_t<std::ranges::range_value_t<Storage>>>;

template <typename Storage>
concept HasResumablePositionType = requires { typename Storage::position_type; };

// A declared source must satisfy its public protocol; do not silently wrap an
// invalid adapter as ordinary storage and discard its commitment semantics.
template <typename Storage>
concept ResumableStorage = (!HasResumablePositionType<Storage>) && (ResumableByteChunk<Storage> || ResumableChunkStorage<Storage>);

template <typename Storage>
concept ResumableOutputStorage =
    (!HasResumablePositionType<Storage>) &&
    (ResumableOutputChunkStorage<Storage> ||
     (ResumableByteChunk<Storage> && std::indirectly_writable<std::ranges::iterator_t<Storage>, std::ranges::range_value_t<Storage>>));

// The caller owns descriptors and bytes. Only append and final-chunk growth are
// permitted. Node-based ranges preserve their existing element iterators.
template <ResumableChunkStorage Storage> class chunk_source {
    using chunk_type     = std::ranges::range_value_t<Storage>;
    using chunk_pointer  = std::add_pointer_t<std::remove_reference_t<std::ranges::range_reference_t<Storage>>>;
    using outer_iterator = std::ranges::iterator_t<Storage>;
    using inner_iterator = std::ranges::iterator_t<chunk_type>;

    static constexpr bool indexed      = std::ranges::random_access_range<Storage>;
    static constexpr bool indexed_byte = std::ranges::random_access_range<chunk_type>;
    static constexpr bool sized        = std::ranges::sized_range<chunk_type>;
    using outer_position               = std::conditional_t<indexed, std::size_t, outer_iterator>;
    using byte_offset                  = std::conditional_t<sized, std::size_t, std::optional<std::size_t>>;

  public:
    class iterator {
        struct indexed_bookmark {
            std::size_t offset{};
        };
        struct no_boundary {};
        struct node_bookmark {
            std::optional<inner_iterator> current;
            std::optional<inner_iterator> previous;
            std::optional<std::size_t>    offset{0};
            bool                          exhausted{};

            // Sized chunks detect growth from their offset. Only unsized
            // chunks need the boundary observed when they became exhausted.
            [[no_unique_address]] std::conditional_t<sized, no_boundary, std::optional<inner_iterator>> boundary;
        };
        using inner_bookmark = std::conditional_t<indexed_byte, indexed_bookmark, node_bookmark>;
        using outer_bookmark = std::conditional_t<indexed, std::size_t, std::optional<outer_iterator>>;

      public:
        using value_type        = std::ranges::range_value_t<chunk_type>;
        using difference_type   = std::ptrdiff_t;
        using reference         = std::ranges::range_reference_t<chunk_type>;
        using pointer           = void;
        using iterator_category = std::bidirectional_iterator_tag;
        using iterator_concept  = std::bidirectional_iterator_tag;
        using bulk_input_tag    = bulk_input_position_tag;

        iterator() = default;

        reference operator*() const {
            normalize();
            assert(!end_marker_ && has_chunk());
            if constexpr (indexed_byte) {
                assert(inner_.offset < std::ranges::size(chunk()));
                return *(std::ranges::begin(chunk()) + static_cast<std::iter_difference_t<inner_iterator>>(inner_.offset));
            } else {
                assert(inner_.current && !inner_.exhausted);
                return **inner_.current;
            }
        }

        // The caller has already checked the complete scalar payload. Native
        // iterators remain local to this call; suspension retains only offsets.
        std::uint64_t read_unsigned(std::size_t count)
            requires(indexed_byte && sized)
        {
            assert(count <= sizeof(std::uint64_t));
            std::uint64_t value{};
            while (count != 0) {
                normalize();
                assert(!end_marker_ && has_chunk() && !chunk_exhausted());
                const auto amount  = std::min(count, std::ranges::size(chunk()) - inner_.offset);
                auto       current = std::ranges::begin(chunk()) + static_cast<std::iter_difference_t<inner_iterator>>(inner_.offset);
                for (std::size_t index = 0; index != amount; ++index, ++current)
                    value = (value << 8U) | static_cast<std::uint8_t>(*current);
                inner_.offset += amount;
                offset_ += amount;
                count -= amount;
            }
            return value;
        }

        std::size_t read_bytes(std::span<std::byte> bytes)
            requires(indexed_byte && sized && !std::ranges::contiguous_range<chunk_type>)
        {
            const auto requested = bytes.size();
            while (!bytes.empty()) {
                normalize();
                if (!has_chunk() || chunk_exhausted())
                    break;
                const auto amount  = std::min(bytes.size(), std::ranges::size(chunk()) - inner_.offset);
                auto       current = std::ranges::begin(chunk()) + static_cast<std::iter_difference_t<inner_iterator>>(inner_.offset);
                for (auto &byte : bytes.first(amount)) {
                    byte = static_cast<std::byte>(*current);
                    ++current;
                }
                inner_.offset += amount;
                offset_ += amount;
                bytes = bytes.subspan(amount);
            }
            return requested - bytes.size();
        }

        iterator &operator++() {
            normalize();
            assert(!end_marker_ && has_chunk());
            if constexpr (indexed_byte) {
                assert(inner_.offset < std::ranges::size(chunk()));
                ++inner_.offset;
            } else {
                assert(inner_.current && !inner_.exhausted);
                inner_.previous = *inner_.current;
                ++*inner_.current;

                // Owning node chunks can move with their outer container.
                // Their elements stay valid, but their old end need not.
                observe_boundary(std::ranges::end(chunk()));
                if (inner_.offset) {
                    if constexpr (sized)
                        ++*inner_.offset;
                    else
                        increment(*inner_.offset);
                }
            }

            // Admission checks the complete extent of sized chunks. Valid
            // traversal within that extent cannot overflow its known offset.
            if constexpr (sized) {
                ++offset_;
            } else if (offset_) {
                increment(*offset_);
            }
            return *this;
        }

        iterator operator++(int) {
            auto previous = *this;
            ++*this;
            return previous;
        }

        iterator &operator--() {
            assert(source_ != nullptr);
            if (end_marker_) {
                end_marker_ = false;
                if constexpr (sized) {
                    offset_ = source_->extent_;
                } else {
                    offset_.reset();
                }
                set_last_chunk();
            } else {
                normalize();
            }

            if constexpr (indexed_byte) {
                while (inner_.offset == 0) {
                    assert(!first_chunk());
                    previous_chunk();
                    set_chunk_end();
                }
                --inner_.offset;
            } else {
                while (inner_.current && *inner_.current == std::ranges::begin(chunk())) {
                    assert(!first_chunk());
                    previous_chunk();
                    set_chunk_end();
                }
                assert(inner_.current);
                --*inner_.current;
                inner_.exhausted = false;
                if (inner_.offset) {
                    assert(*inner_.offset != 0);
                    --*inner_.offset;
                }
            }

            if constexpr (sized) {
                assert(offset_ != 0);
                --offset_;
            } else if (offset_) {
                assert(*offset_ != 0);
                --*offset_;
            }
            return *this;
        }

        iterator operator--(int) {
            auto previous = *this;
            --*this;
            return previous;
        }

        friend bool operator==(const iterator &left, const iterator &right) {
            if (left.source_ != right.source_) {
                return false;
            }
            if (left.source_ == nullptr || (left.end_marker_ && right.end_marker_)) {
                return true;
            }
            if (left.end_marker_) {
                return right.at_end();
            }
            if (right.end_marker_) {
                return left.at_end();
            }

            const auto left_end  = left.at_end();
            const auto right_end = right.at_end();
            if (left_end || right_end) {
                return left_end && right_end;
            }
            if constexpr (std::same_as<inner_bookmark, indexed_bookmark>)
                return left.outer_ == right.outer_ && left.inner_.offset == right.inner_.offset;
            else
                return left.outer_ == right.outer_ && *left.inner_.current == *right.inner_.current;
        }

        // Begin-derived cursors always know their offset. For unsized chunks a
        // fresh end cursor cannot supply one without walking the payload.
        std::size_t offset() const {
            if constexpr (sized) {
                return end_marker_ ? source_->extent_ : offset_;
            } else {
                if (!offset_) {
                    throw std::logic_error("an unsized end cursor has no absolute byte offset");
                }
                return *offset_;
            }
        }

      private:
        friend class chunk_source;

        explicit iterator(const chunk_source *source, bool end_marker) : source_(source), end_marker_(end_marker) {
            if constexpr (!sized) {
                if (end_marker)
                    offset_.reset();
            }
        }

        static void increment(std::size_t &value) {
            if (value == std::numeric_limits<std::size_t>::max())
                throw std::length_error("CBOR source byte offset overflow");
            ++value;
        }

        bool has_chunk() const {
            if constexpr (indexed)
                return outer_ < source_->chunk_count_;
            else
                return outer_.has_value();
        }

        decltype(auto) chunk() const {
            if constexpr (indexed) {
                return *(std::ranges::begin(*source_->storage_) + static_cast<std::ranges::range_difference_t<Storage>>(outer_));
            } else {
                assert(outer_.has_value());
                return **outer_;
            }
        }

        bool first_chunk() const {
            if constexpr (indexed) {
                return outer_ == 0;
            } else {
                return *outer_ == std::ranges::begin(*source_->storage_);
            }
        }

        void previous_chunk() {
            if constexpr (indexed)
                --outer_;
            else
                --*outer_;
        }

        void set_last_chunk() {
            assert(source_->chunk_count_ != 0);
            if constexpr (indexed) {
                outer_ = source_->chunk_count_ - 1;
            } else {
                outer_ = std::prev(std::ranges::end(*source_->storage_));
            }
            set_chunk_end();
        }

        void observe_boundary(inner_iterator boundary) const
            requires(!indexed_byte)
        {
            inner_.exhausted = *inner_.current == boundary;
            if constexpr (!sized) {
                if (inner_.exhausted)
                    inner_.boundary = boundary;
                else
                    inner_.boundary.reset();
            }
        }

        void set_chunk_begin() const {
            inner_.offset = 0;
            if constexpr (!indexed_byte) {
                inner_.current = std::ranges::begin(chunk());
                inner_.previous.reset();
                observe_boundary(std::ranges::end(chunk()));
            }
        }

        void set_chunk_end() {
            if constexpr (indexed_byte) {
                inner_.offset = std::ranges::size(chunk());
            } else {
                inner_.current = std::ranges::end(chunk());
                observe_boundary(*inner_.current);
                if constexpr (sized)
                    inner_.offset = std::ranges::size(chunk());
                else
                    inner_.offset.reset();
                inner_.previous.reset();
                if (*inner_.current != std::ranges::begin(chunk()))
                    inner_.previous = std::prev(*inner_.current);
            }
        }

        // Only exhaustion needs recovery. An active node iterator remains
        // valid even when its owning descriptor moves to a new address.
        void refresh_exhausted() const
            requires(!indexed_byte)
        {
            const auto boundary = std::ranges::end(chunk());
            if (inner_.offset && *inner_.offset == 0) {
                inner_.current = std::ranges::begin(chunk());
            } else {
                const bool grew = [&] {
                    if constexpr (sized) {
                        assert(inner_.offset && *inner_.offset <= std::ranges::size(chunk()));
                        return *inner_.offset < std::ranges::size(chunk());
                    } else {
                        assert(inner_.boundary);
                        return *inner_.boundary != boundary;
                    }
                }();
                // Do not revisit the preceding element on unchanged refresh
                // or end probes. Unsized chunks need a changed boundary.
                if (grew && inner_.previous)
                    inner_.current = std::next(*inner_.previous);
                else
                    inner_.current = boundary;
            }
            observe_boundary(boundary);
        }

        void normalize() const {
            if (end_marker_ || source_ == nullptr) {
                return;
            }

            if constexpr (indexed_byte) {
                std::size_t extent{};
                (void)normalized_indexed_chunk(extent);
            } else {
                if (inner_.current && !inner_.exhausted)
                    return;
                normalize_chunks();
            }
        }

        // Resolve a descriptor and its extent together. The returned pointer
        // is borrowed only by the current synchronous call, never by a cursor.
        chunk_pointer normalized_indexed_chunk(std::size_t &extent) const
            requires(indexed_byte)
        {
            if (end_marker_ || source_ == nullptr || source_->chunk_count_ == 0)
                return nullptr;
            if (!has_chunk()) {
                if constexpr (indexed)
                    outer_ = 0;
                else
                    outer_ = std::ranges::begin(*source_->storage_);
                inner_.offset = 0;
            }

            for (;;) {
                auto &current = chunk();
                extent        = std::ranges::size(current);
                assert(inner_.offset <= extent);
                if (inner_.offset < extent)
                    return std::addressof(current);

                // A grown final chunk must be considered before its newly
                // appended successor. Exhaustion retains the final bookmark.
                if constexpr (indexed) {
                    if (outer_ + 1 == source_->chunk_count_)
                        return nullptr;
                    ++outer_;
                } else {
                    const auto next = std::next(*outer_);
                    if (next == std::ranges::end(*source_->storage_))
                        return nullptr;
                    outer_ = next;
                }
                inner_.offset = 0;
            }
        }

        void normalize_chunks() const {
            if (!has_chunk()) {
                if (source_->chunk_count_ == 0)
                    return;
                if constexpr (indexed)
                    outer_ = 0;
                else
                    outer_ = std::ranges::begin(*source_->storage_);
                set_chunk_begin();
            } else if constexpr (!indexed_byte) {
                if (inner_.current)
                    refresh_exhausted();
                else
                    set_chunk_begin();
            }

            // Keep the last descriptor at exhaustion. Its growth must be
            // considered before a successor appended in the same refresh.
            while (chunk_exhausted()) {
                if constexpr (indexed) {
                    if (outer_ + 1 == source_->chunk_count_)
                        return;
                    ++outer_;
                } else {
                    assert(outer_.has_value());
                    const auto next = std::next(*outer_);
                    if (next == std::ranges::end(*source_->storage_))
                        return;
                    outer_ = next;
                }
                set_chunk_begin();
            }
        }

        bool chunk_exhausted() const {
            if constexpr (indexed_byte) {
                assert(inner_.offset <= std::ranges::size(chunk()));
                return inner_.offset == std::ranges::size(chunk());
            } else {
                return inner_.exhausted;
            }
        }

        bool at_end() const {
            if constexpr (indexed_byte) {
                std::size_t extent{};
                return normalized_indexed_chunk(extent) == nullptr;
            } else {
                normalize();
                return !has_chunk() || chunk_exhausted();
            }
        }

        const chunk_source    *source_{};
        mutable outer_bookmark outer_{};
        mutable inner_bookmark inner_;
        byte_offset            offset_{0};
        bool                   end_marker_{};
    };

    explicit chunk_source(Storage &storage) : storage_(&storage) { refresh(); }

    chunk_source(const chunk_source &)            = delete;
    chunk_source &operator=(const chunk_source &) = delete;
    chunk_source(chunk_source &&)                 = delete;
    chunk_source &operator=(chunk_source &&)      = delete;

    iterator begin() const { return iterator(this, false); }
    iterator end() const { return iterator(this, true); }

    std::size_t size() const
        requires sized
    {
        return extent_;
    }

    iterator position_at_end() const
        requires sized
    {
        auto position = begin();
        if (chunk_count_ != 0)
            position.set_last_chunk();
        position.offset_ = extent_;
        return position;
    }

    std::size_t remaining_size(const iterator &from) const
        requires sized
    {
        assert(owns(from) && from.offset() <= extent_);
        return from.end_marker_ ? 0 : extent_ - from.offset_;
    }

    // Observe only the previous tail and newly appended descriptors. Unsized
    // byte ranges are never walked to calculate an extent.
    void refresh() {
        if constexpr (std::ranges::sized_range<Storage>) {
            assert(std::ranges::size(*storage_) >= chunk_count_ && "CBOR source descriptors cannot be removed");
        }

        if (last_chunk_) {
            if constexpr (sized) {
                auto      &tail  = chunk_at(*last_chunk_);
                const auto count = checked_size(tail);
                // Unchanged tails need neither extent updates nor publication.
                if (count != tail_extent_) {
                    assert(count >= tail_extent_ && "CBOR source tail cannot shrink");
                    add_available(count - tail_extent_);
                    tail_extent_ = count;
                }
            }
        }

        auto next = [&] {
            if constexpr (indexed)
                return last_chunk_ ? *last_chunk_ + 1 : std::size_t{};
            else
                return last_chunk_ ? std::next(*last_chunk_) : std::ranges::begin(*storage_);
        }();
        const auto at_end = [&] {
            if constexpr (indexed)
                return next == static_cast<std::size_t>(std::ranges::end(*storage_) - std::ranges::begin(*storage_));
            else
                return next == std::ranges::end(*storage_);
        };
        while (!at_end()) {
            assert(!closed_ && "cannot grow a closed CBOR source");
            if (chunk_count_ == std::numeric_limits<std::size_t>::max())
                throw std::length_error("CBOR source descriptor count overflow");
            if constexpr (sized) {
                tail_extent_ = checked_size(chunk_at(next));
                add_available(tail_extent_);
            }
            last_chunk_ = next;
            ++chunk_count_;
            ++next;
        }
    }

    bool eof() const { return closed_; }
    bool owns(const iterator &position) const { return position.source_ == this; }

    // Cleanup only restores known progress positions; an unsized end sentinel
    // is never a retry checkpoint and has no offset to inspect here.
    bool can_restore(const iterator &position, const iterator &committed, const iterator &working) const noexcept {
        if constexpr (sized) {
            return position.source_ == this && !position.end_marker_ && !committed.end_marker_ && !working.end_marker_ &&
                   position.offset_ >= committed.offset_ && position.offset_ <= working.offset_;
        } else {
            return position.source_ == this && position.offset_ && committed.offset_ && working.offset_ &&
                   *position.offset_ >= *committed.offset_ && *position.offset_ <= *working.offset_;
        }
    }

    void acquire() {
        if (active_) {
            throw std::logic_error("a CBOR source already has an active operation");
        }
        active_ = true;
    }

    void release() noexcept { active_ = false; }

    void close() {
        if (!closed_) {
            refresh();
            closed_ = true;
        }
    }

    auto remaining(const iterator &from) const {
        assert(from.source_ == this);
        return std::ranges::subrange(from, end());
    }

    std::optional<std::span<const std::byte>> available_span(iterator from) const { return borrow_window(from); }

    // Reconstruct contiguous access from the descriptor and byte offset so no
    // byte iterator survives a backing allocation or descriptor relocation.
    std::optional<std::span<const std::byte>> borrow_window(iterator &from) const {
        assert(from.source_ == this && !from.end_marker_);
        if constexpr (std::ranges::contiguous_range<chunk_type>) {
            std::size_t extent{};
            auto       *descriptor = from.normalized_indexed_chunk(extent);
            if (descriptor == nullptr)
                return std::span<const std::byte>{};
            const auto  offset  = from.inner_.offset;
            const auto *current = std::ranges::data(*descriptor) + offset;
            return std::span<const std::byte>{reinterpret_cast<const std::byte *>(current), extent - offset};
        } else {
            return std::nullopt;
        }
    }

    // Leave an exhausted cursor on its descriptor so its next access can
    // discover tail growth before proceeding to newly appended descriptors.
    void consume_window(iterator &from, std::size_t count) const {
        assert(from.source_ == this && !from.end_marker_);
        if (count == 0)
            return;

        if constexpr (indexed_byte) {
            assert(from.has_chunk());
            assert(from.inner_.offset <= std::ranges::size(from.chunk()));
            assert(count <= std::ranges::size(from.chunk()) - from.inner_.offset);
            assert(from.offset_ <= extent_ && count <= extent_ - from.offset_);
            from.inner_.offset += count;
            from.offset_ += count;
        } else {
            for (std::size_t consumed = 0; consumed != count; ++consumed)
                ++from;
        }
    }

    std::size_t advance(iterator &from, std::size_t count) const {
        assert(from.source_ == this && !from.end_marker_);
        std::size_t advanced = 0;
        while (advanced != count && !from.at_end()) {
            if constexpr (indexed_byte) {
                const auto available = std::ranges::size(from.chunk()) - from.inner_.offset;
                const auto amount    = std::min(available, count - advanced);
                consume_window(from, amount);
                advanced += amount;
            } else {
                ++from;
                ++advanced;
            }
        }
        return advanced;
    }

    std::optional<std::span<std::byte>> available_output_span(iterator from)
        requires std::indirectly_writable<inner_iterator, std::ranges::range_value_t<chunk_type>>
    {
        return borrow_output_window(from);
    }

    std::optional<std::span<std::byte>> borrow_output_window(iterator &from)
        requires std::indirectly_writable<inner_iterator, std::ranges::range_value_t<chunk_type>>
    {
        assert(from.source_ == this && !from.end_marker_);
        if constexpr (std::ranges::contiguous_range<chunk_type>) {
            std::size_t extent{};
            auto       *descriptor = from.normalized_indexed_chunk(extent);
            if (descriptor == nullptr)
                return std::span<std::byte>{};
            const auto offset  = from.inner_.offset;
            auto      *current = std::ranges::data(*descriptor) + offset;
            return std::span<std::byte>{reinterpret_cast<std::byte *>(current), extent - offset};
        } else {
            return std::nullopt;
        }
    }

    bool write_byte(iterator &to, std::byte value)
        requires std::indirectly_writable<inner_iterator, std::ranges::range_value_t<chunk_type>>
    {
        assert(to.source_ == this && !to.end_marker_);
        if (to.at_end()) {
            return false;
        }
        *to = static_cast<std::ranges::range_value_t<chunk_type>>(value);
        ++to;
        return true;
    }

    // Deques have no contiguous window, but a bounded write can still advance
    // a native iterator once per chunk instead of indexing once per byte.
    std::size_t write_bytes(iterator &to, std::span<const std::byte> bytes)
        requires(indexed_byte && sized && !std::ranges::contiguous_range<chunk_type> &&
                 std::indirectly_writable<inner_iterator, std::ranges::range_value_t<chunk_type>>)
    {
        const auto requested = bytes.size();
        while (!bytes.empty()) {
            to.normalize();
            if (!to.has_chunk() || to.chunk_exhausted())
                break;
            const auto amount  = std::min(bytes.size(), std::ranges::size(to.chunk()) - to.inner_.offset);
            auto       current = std::ranges::begin(to.chunk()) + static_cast<std::iter_difference_t<inner_iterator>>(to.inner_.offset);
            for (const auto byte : bytes.first(amount)) {
                *current = static_cast<std::ranges::range_value_t<chunk_type>>(byte);
                ++current;
            }
            to.inner_.offset += amount;
            to.offset_ += amount;
            bytes = bytes.subspan(amount);
        }
        return requested - bytes.size();
    }

  private:
    decltype(auto) chunk_at(outer_position position) const {
        if constexpr (indexed)
            return *(std::ranges::begin(*storage_) + static_cast<std::ranges::range_difference_t<Storage>>(position));
        else
            return *position;
    }

    template <typename Chunk> static std::size_t checked_size(Chunk &chunk) {
        const auto count = std::ranges::size(chunk);
        if (std::cmp_greater(count, std::numeric_limits<std::size_t>::max()))
            throw std::length_error("CBOR source byte extent overflow");
        return static_cast<std::size_t>(count);
    }

    void add_available(std::size_t amount) {
        assert((!closed_ || amount == 0) && "cannot grow a closed CBOR source");
        if (amount > std::numeric_limits<std::size_t>::max() - extent_)
            throw std::length_error("CBOR source byte extent overflow");
        extent_ += amount;
    }

    Storage                      *storage_;
    std::size_t                   chunk_count_{};
    std::size_t                   extent_{};
    std::optional<outer_position> last_chunk_;
    std::size_t                   tail_extent_{};
    bool                          closed_{};
    bool                          active_{};
};

// A flat byte container is one borrowed range; no descriptor allocation is
// needed. Nested storage remains referenced directly.
template <typename Storage, bool Flat = ResumableByteChunk<Storage>> struct source_storage {
    explicit source_storage(Storage &storage) : storage_(&storage) {}
    Storage &get() { return *storage_; }
    Storage *storage_;
};

template <typename Storage> struct source_storage<Storage, true> {
    explicit source_storage(Storage &storage) : storage_{std::ranges::ref_view<Storage>{storage}} {}
    auto                                         &get() { return storage_; }
    std::array<std::ranges::ref_view<Storage>, 1> storage_;
};

template <typename Storage> using source_chunks = std::remove_reference_t<decltype(std::declval<source_storage<Storage> &>().get())>;

} // namespace detail

template <detail::ResumableStorage Storage> class input_source {
    using chunks_type = detail::chunk_source<detail::source_chunks<Storage>>;

  public:
    using iterator      = typename chunks_type::iterator;
    using position_type = iterator;

    explicit input_source(Storage &storage) : storage_(storage), chunks_(storage_.get()), committed_(chunks_.begin()) {}
    input_source(const input_source &)            = delete;
    input_source &operator=(const input_source &) = delete;
    input_source(input_source &&)                 = delete;
    input_source &operator=(input_source &&)      = delete;

    iterator    begin() const { return chunks_.begin(); }
    iterator    end() const { return chunks_.end(); }
    iterator    committed_position() const noexcept { return committed_; }
    auto        remaining(const iterator &from) const { return chunks_.remaining(from); }
    std::size_t size() const
        requires std::ranges::sized_range<const chunks_type>
    {
        return chunks_.size();
    }
    std::size_t remaining_size(const iterator &from) const
        requires std::ranges::sized_range<const chunks_type>
    {
        return chunks_.remaining_size(from);
    }

    void refresh() { chunks_.refresh(); }
    void acquire() { chunks_.acquire(); }
    void release() noexcept { chunks_.release(); }
    void close() { chunks_.close(); }
    bool eof() const { return chunks_.eof(); }

    void commit(const iterator &position) {
        assert(chunks_.owns(position) && position.offset() >= committed_.offset());
        committed_ = position;
    }

    auto        available_span(iterator from) const { return chunks_.available_span(from); }
    auto        borrow_window(iterator &from) const { return chunks_.borrow_window(from); }
    void        consume_window(iterator &from, std::size_t count) const { chunks_.consume_window(from, count); }
    std::size_t advance(iterator &from, std::size_t count) const { return chunks_.advance(from, count); }

  private:
    detail::source_storage<Storage> storage_;
    chunks_type                     chunks_;
    iterator                        committed_;
};

template <detail::ResumableStorage Storage> auto make_input_source(Storage &storage) { return input_source<Storage>(storage); }

template <detail::ResumableOutputStorage Storage> class output_source {
    using chunks_type = detail::chunk_source<detail::source_chunks<Storage>>;
    static constexpr bool growable =
        detail::ResumableByteChunk<Storage> && CborAppendOutputBuffer<Storage> && std::ranges::sized_range<Storage>;

  public:
    using value_type    = std::byte;
    using size_type     = std::size_t;
    using iterator      = typename chunks_type::iterator;
    using position_type = iterator;

    explicit output_source(Storage &storage)
        : storage_(&storage), views_(storage), chunks_(views_.get()), working_([&] {
              if constexpr (growable)
                  return chunks_.position_at_end();
              else
                  return chunks_.begin();
          }()),
          committed_(working_) {}
    output_source(const output_source &)            = delete;
    output_source &operator=(const output_source &) = delete;
    output_source(output_source &&)                 = delete;
    output_source &operator=(output_source &&)      = delete;

    void refresh() { chunks_.refresh(); }
    bool eof() const { return chunks_.eof(); }
    void close() { chunks_.close(); }
    void acquire() { chunks_.acquire(); }
    void release() noexcept { chunks_.release(); }

    iterator      begin() const { return chunks_.begin(); }
    iterator      end() const { return working_; }
    size_type     size() const { return working_.offset(); }
    position_type position() const { return working_; }
    position_type committed_position() const noexcept { return committed_; }
    void          commit() { committed_ = working_; }

    void restore(const position_type &position) noexcept {
        assert(chunks_.can_restore(position, committed_, working_));
        working_ = position;
    }

    auto remaining_output() const { return chunks_.remaining(working_); }
    auto available_output_span() { return borrow_window(); }

    // Growable containers construct bytes through append, never by exposing
    // reserved but unconstructed capacity as a writable span.
    std::optional<std::span<std::byte>> borrow_window() {
        auto window = chunks_.borrow_output_window(working_);
        if constexpr (growable) {
            if (window && window->empty())
                return std::nullopt;
        }
        return window;
    }
    void consume_window(std::size_t count) { chunks_.consume_window(working_, count); }
    void advance(std::size_t count) {
        [[maybe_unused]] const auto advanced = chunks_.advance(working_, count);
        assert(advanced == count);
    }

    std::size_t write(std::span<const std::byte> bytes) {
        const auto requested = bytes.size();
        if constexpr (detail::HasBulkChunkWrite<chunks_type, iterator>) {
            bytes = bytes.subspan(chunks_.write_bytes(working_, bytes));
        } else {
            while (!bytes.empty() && working_ != chunks_.end()) {
                if (const auto available = chunks_.borrow_output_window(working_)) {
                    const auto amount = std::min(bytes.size(), available->size());
                    std::memcpy(available->data(), bytes.data(), amount);
                    chunks_.consume_window(working_, amount);
                    bytes = bytes.subspan(amount);
                } else {
                    [[maybe_unused]] const auto written = chunks_.write_byte(working_, bytes.front());
                    assert(written);
                    bytes = bytes.subspan(1);
                }
            }
        }
        if constexpr (growable) {
            if (!bytes.empty() && !eof()) {
                if (bytes.size() > std::numeric_limits<std::size_t>::max() - working_.offset())
                    throw std::length_error("CBOR output byte offset overflow");
                using byte_type = std::ranges::range_value_t<Storage>;
                if constexpr (std::same_as<byte_type, std::byte>) {
                    storage_->insert(std::ranges::end(*storage_), bytes.begin(), bytes.end());
                } else {
                    for (const auto byte : bytes)
                        storage_->push_back(static_cast<byte_type>(byte));
                }
                chunks_.refresh();
                working_ = chunks_.position_at_end();
                return requested;
            }
        }
        return requested - bytes.size();
    }

  private:
    Storage                        *storage_;
    detail::source_storage<Storage> views_;
    chunks_type                     chunks_;
    iterator                        working_;
    iterator                        committed_;
};

template <detail::ResumableOutputStorage Storage> auto make_output_source(Storage &storage) { return output_source<Storage>(storage); }

namespace detail {

// Ordinary encoders use this library-owned bridge for every public adapter.
// A consumer only implements the source protocol and reports partial writes.
template <ResumableOutputSource Source> struct is_segment_output_buffer<Source> : std::true_type {};

template <ResumableOutputSource Source> struct appender<Source, false> {
    void reserve_for_append(Source &, std::uint64_t) const {}
    void operator()(Source &source, std::byte value) { (*this)(source, std::span<const std::byte>{&value, 1}); }
    void operator()(Source &source, std::span<const std::byte> values) {
        if (source.write(values) != values.size())
            throw encode_status_exception{status_code::incomplete};
    }
    void operator()(Source &source, std::string_view value) {
        (*this)(source, {reinterpret_cast<const std::byte *>(value.data()), value.size()});
    }

    template <typename... Values> void multi_append(Source &source, Values... values) {
        const std::byte bytes[]{static_cast<std::byte>(values)...};
        (*this)(source, std::span<const std::byte>{bytes});
    }
};

} // namespace detail

} // namespace cbor::tags
