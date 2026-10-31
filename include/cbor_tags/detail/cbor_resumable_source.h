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
    using outer_iterator = std::ranges::iterator_t<Storage>;
    using inner_iterator = std::ranges::iterator_t<chunk_type>;

    static constexpr bool indexed = std::ranges::random_access_range<Storage>;
    static constexpr bool sized   = std::ranges::sized_range<chunk_type>;
    using outer_position          = std::conditional_t<indexed, std::size_t, outer_iterator>;

  public:
    class iterator {
      public:
        using value_type        = std::ranges::range_value_t<chunk_type>;
        using difference_type   = std::ptrdiff_t;
        using reference         = std::ranges::range_reference_t<chunk_type>;
        using pointer           = void;
        using iterator_category = std::bidirectional_iterator_tag;
        using iterator_concept  = std::bidirectional_iterator_tag;

        iterator() = default;

        reference operator*() const {
            normalize();
            assert(!end_marker_ && current_ && limit_);
            assert(*current_ != *limit_);
            return **current_;
        }

        iterator &operator++() {
            normalize();
            assert(!end_marker_ && current_ && limit_);
            assert(*current_ != *limit_);
            previous_ = *current_;
            ++*current_;
            exhausted_ = *current_ == *limit_;
            if (inner_offset_) {
                increment(*inner_offset_);
            }
            if (offset_) {
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

            while (current_ && *current_ == std::ranges::begin(chunk())) {
                assert(!first_chunk());
                previous_chunk();
                set_chunk_end();
            }

            assert(current_);
            --*current_;
            exhausted_ = false;
            if (inner_offset_) {
                assert(*inner_offset_ != 0);
                --*inner_offset_;
            }
            if (offset_) {
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
            assert(left.current_ && right.current_);
            return left.outer_ == right.outer_ && *left.current_ == *right.current_;
        }

        // Begin-derived cursors always know their offset. For unsized chunks a
        // fresh end cursor cannot supply one without walking the payload.
        std::size_t offset() const {
            if (end_marker_) {
                if constexpr (sized) {
                    return source_->extent_;
                }
            }
            if (!offset_) {
                throw std::logic_error("an unsized end cursor has no absolute byte offset");
            }
            return *offset_;
        }

      private:
        friend class chunk_source;

        explicit iterator(const chunk_source *source, bool end_marker) : source_(source), end_marker_(end_marker) {
            if (end_marker) {
                offset_.reset();
            }
        }

        static void increment(std::size_t &value) {
            if (value == std::numeric_limits<std::size_t>::max())
                throw std::length_error("CBOR source byte offset overflow");
            ++value;
        }

        decltype(auto) chunk() const {
            if constexpr (indexed) {
                return *(std::ranges::begin(*source_->storage_) + static_cast<std::ranges::range_difference_t<Storage>>(outer_));
            } else {
                return *outer_;
            }
        }

        bool first_chunk() const {
            if constexpr (indexed) {
                return outer_ == 0;
            } else {
                return outer_ == std::ranges::begin(*source_->storage_);
            }
        }

        void previous_chunk() { --outer_; }

        void set_last_chunk() {
            assert(std::ranges::begin(*source_->storage_) != std::ranges::end(*source_->storage_));
            if constexpr (indexed) {
                outer_ = source_->chunk_count_ - 1;
            } else {
                outer_ = std::prev(std::ranges::end(*source_->storage_));
            }
            set_chunk_end();
            observed_generation_ = source_->generation_;
        }

        void set_chunk_end() {
            current_   = std::ranges::end(chunk());
            limit_     = *current_;
            exhausted_ = true;
            if constexpr (sized)
                inner_offset_ = std::ranges::size(chunk());
            else
                inner_offset_.reset();
            previous_.reset();
            if (*current_ != std::ranges::begin(chunk()))
                previous_ = std::prev(*current_);
        }

        // Reacquire random-access iterators after allocation changes. Stable
        // node iterators only need their end refreshed; an exhausted cursor
        // retains the preceding element to discover appended nodes.
        void refresh_current() const {
            if (observed_generation_ == source_->generation_)
                return;
            observed_generation_ = source_->generation_;
            if (!current_)
                return;
            if constexpr (std::ranges::random_access_range<chunk_type>) {
                assert(inner_offset_);
                current_ = std::ranges::begin(chunk()) + static_cast<std::iter_difference_t<inner_iterator>>(*inner_offset_);
            } else if (inner_offset_ && *inner_offset_ == 0) {
                current_ = std::ranges::begin(chunk());
            } else if (exhausted_ && previous_) {
                // Reusing an unchanged end must not traverse the last byte a
                // second time. Sized node ranges detect growth by their extent;
                // unsized subranges expose it through their changed boundary.
                const bool grew = [&] {
                    if constexpr (sized)
                        return inner_offset_ && *inner_offset_ != std::ranges::size(chunk());
                    else
                        return *limit_ != std::ranges::end(chunk());
                }();
                if (grew)
                    current_ = std::next(*previous_);
                else
                    current_ = std::ranges::end(chunk());
            }
            limit_     = std::ranges::end(chunk());
            exhausted_ = *current_ == *limit_;
        }

        void normalize() const {
            if (end_marker_ || source_ == nullptr) {
                return;
            }
            refresh_current();
            if (!current_) {
                if (std::ranges::begin(*source_->storage_) == std::ranges::end(*source_->storage_)) {
                    return;
                }
                if constexpr (indexed) {
                    outer_ = 0;
                } else {
                    outer_ = std::ranges::begin(*source_->storage_);
                }
                current_      = std::ranges::begin(chunk());
                limit_        = std::ranges::end(chunk());
                inner_offset_ = 0;
                exhausted_    = *current_ == *limit_;
            }

            // Keep the last descriptor at exhaustion: its successor can be
            // discovered after append without retaining an invalidated end.
            assert(current_ && limit_);
            while (*current_ == *limit_) {
                auto next = outer_;
                ++next;
                if constexpr (indexed) {
                    if (next == source_->chunk_count_) {
                        return;
                    }
                } else {
                    if (next == std::ranges::end(*source_->storage_)) {
                        return;
                    }
                }
                outer_        = next;
                current_      = std::ranges::begin(chunk());
                limit_        = std::ranges::end(chunk());
                inner_offset_ = 0;
                previous_.reset();
                exhausted_ = *current_ == *limit_;
            }
        }

        bool at_end() const {
            normalize();
            if (!current_) {
                return true;
            }
            assert(limit_);
            return *current_ == *limit_;
        }

        const chunk_source                   *source_{};
        mutable outer_position                outer_{};
        mutable std::optional<inner_iterator> current_;
        mutable std::optional<inner_iterator> limit_;
        mutable std::optional<inner_iterator> previous_;
        mutable std::optional<std::size_t>    inner_offset_{0};
        mutable std::size_t                   observed_generation_{};
        mutable bool                          exhausted_{};
        std::optional<std::size_t>            offset_{0};
        bool                                  end_marker_{};
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

    std::size_t remaining_size(iterator from) const
        requires sized
    {
        assert(owns(from) && from.offset() <= extent_);
        return extent_ - from.offset();
    }

    // Observe only the previous tail and newly appended descriptors. Unsized
    // byte ranges are never walked to calculate an extent.
    void refresh() {
        if (generation_ == std::numeric_limits<std::size_t>::max())
            throw std::length_error("CBOR source refresh generation overflow");
        if constexpr (std::ranges::sized_range<Storage>) {
            if (std::ranges::size(*storage_) < chunk_count_)
                throw std::logic_error("CBOR source descriptors cannot be removed");
        }

        if (last_chunk_) {
            if constexpr (sized) {
                auto      &tail  = chunk_at(*last_chunk_);
                const auto count = checked_size(tail);
                if (count < tail_extent_)
                    throw std::logic_error("CBOR source tail cannot shrink");
                add_available(count - tail_extent_);
                tail_extent_ = count;
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
            if (closed_)
                throw std::logic_error("cannot grow a closed CBOR source");
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
        ++generation_;
    }

    std::size_t generation() const { return generation_; }
    bool        eof() const { return closed_; }
    bool        owns(iterator position) const { return position.source_ == this; }

    // Cleanup only restores known progress positions; an unsized end sentinel
    // is never a retry checkpoint and has no offset to inspect here.
    bool can_restore(const iterator &position, const iterator &committed, const iterator &working) const noexcept {
        return position.source_ == this && position.offset_ && committed.offset_ && working.offset_ &&
               *position.offset_ >= *committed.offset_ && *position.offset_ <= *working.offset_;
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

    auto remaining(iterator from) const {
        assert(from.source_ == this);
        return std::ranges::subrange(from, end());
    }

    std::optional<std::span<const std::byte>> available_span(iterator from) const { return borrow_window(from); }

    // Normalize the working cursor once, retaining its descriptor position.
    std::optional<std::span<const std::byte>> borrow_window(iterator &from) const {
        assert(from.source_ == this && !from.end_marker_);
        if constexpr (std::ranges::contiguous_range<chunk_type>) {
            from.normalize();
            if (!from.current_) {
                return std::span<const std::byte>{};
            }
            assert(from.limit_);
            if (*from.current_ == *from.limit_) {
                return std::span<const std::byte>{};
            }
            return std::span<const std::byte>{reinterpret_cast<const std::byte *>(std::to_address(*from.current_)),
                                              static_cast<std::size_t>(*from.limit_ - *from.current_)};
        } else {
            return std::nullopt;
        }
    }

    // The caller stays within the just-borrowed contiguous window. Leave an
    // exhausted cursor on its descriptor so the next borrow can discover append.
    void consume_window(iterator &from, std::size_t count) const {
        assert(from.source_ == this && !from.end_marker_);
        assert(from.current_ && from.limit_);
        if constexpr (std::ranges::contiguous_range<chunk_type>) {
            assert(count <= static_cast<std::size_t>(*from.limit_ - *from.current_));
        }
        if (from.offset_ && count > std::numeric_limits<std::size_t>::max() - *from.offset_)
            throw std::length_error("CBOR source byte offset overflow");
        std::ranges::advance(*from.current_, static_cast<std::iter_difference_t<inner_iterator>>(count));
        if (from.inner_offset_)
            *from.inner_offset_ += count;
        from.exhausted_ = *from.current_ == *from.limit_;
        if (from.offset_) {
            *from.offset_ += count;
        }
    }

    std::size_t advance(iterator &from, std::size_t count) const {
        assert(from.source_ == this && !from.end_marker_);
        std::size_t advanced = 0;
        while (advanced != count && !from.at_end()) {
            if constexpr (std::ranges::random_access_range<chunk_type>) {
                assert(from.current_ && from.limit_);
                const auto available = static_cast<std::size_t>(*from.limit_ - *from.current_);
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
            from.normalize();
            if (!from.current_) {
                return std::span<std::byte>{};
            }
            assert(from.limit_);
            if (*from.current_ == *from.limit_) {
                return std::span<std::byte>{};
            }
            return std::span<std::byte>{reinterpret_cast<std::byte *>(std::to_address(*from.current_)),
                                        static_cast<std::size_t>(*from.limit_ - *from.current_)};
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
        assert(to.current_);
        **to.current_ = static_cast<std::ranges::range_value_t<chunk_type>>(value);
        ++to;
        return true;
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
        if (closed_ && amount != 0)
            throw std::logic_error("cannot grow a closed CBOR source");
        if (amount > std::numeric_limits<std::size_t>::max() - extent_)
            throw std::length_error("CBOR source byte extent overflow");
        extent_ += amount;
    }

    Storage                      *storage_;
    std::size_t                   chunk_count_{};
    std::size_t                   extent_{};
    std::size_t                   generation_{};
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
    auto        remaining(iterator from) const { return chunks_.remaining(from); }
    std::size_t size() const
        requires std::ranges::sized_range<const chunks_type>
    {
        return chunks_.size();
    }
    std::size_t remaining_size(iterator from) const
        requires std::ranges::sized_range<const chunks_type>
    {
        return chunks_.remaining_size(from);
    }

    void        refresh() { chunks_.refresh(); }
    void        acquire() { chunks_.acquire(); }
    void        release() noexcept { chunks_.release(); }
    void        close() { chunks_.close(); }
    bool        eof() const { return chunks_.eof(); }
    std::size_t generation() const { return chunks_.generation(); }

    void commit(iterator position) {
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

    void        refresh() { chunks_.refresh(); }
    std::size_t generation() const { return chunks_.generation(); }
    bool        eof() const { return chunks_.eof(); }
    void        close() { chunks_.close(); }
    void        acquire() { chunks_.acquire(); }
    void        release() noexcept { chunks_.release(); }

    iterator      begin() const { return chunks_.begin(); }
    iterator      end() const { return working_; }
    size_type     size() const { return working_.offset(); }
    position_type position() const { return working_; }
    position_type committed_position() const noexcept { return committed_; }
    void          commit() { committed_ = working_; }

    void restore(position_type position) noexcept {
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
