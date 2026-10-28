#pragma once

#include "cbor_tags/cbor_detail.h"
#include "cbor_tags/detail/cbor_encode_error.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <optional>
#include <stdexcept>

namespace cbor::tags {

namespace detail {

template <typename Chunk>
concept ResumableByteChunk =
    std::ranges::bidirectional_range<Chunk> && std::ranges::common_range<Chunk> && IsCborBufferByte<std::ranges::range_value_t<Chunk>> &&
    std::is_lvalue_reference_v<std::ranges::range_reference_t<Chunk>> &&
    (!std::is_volatile_v<std::remove_reference_t<std::ranges::range_reference_t<Chunk>>>);

template <typename Storage>
concept ResumableChunkStorage =
    std::ranges::bidirectional_range<Storage> && std::ranges::common_range<Storage> &&
    std::is_lvalue_reference_v<std::ranges::range_reference_t<Storage>> && ResumableByteChunk<std::ranges::range_value_t<Storage>> &&
    std::ranges::borrowed_range<std::ranges::range_value_t<Storage>>;

template <typename Storage, typename Chunk>
concept ResumableChunkArgument = ResumableByteChunk<Chunk> && (std::is_lvalue_reference_v<Chunk> || std::ranges::borrowed_range<Chunk>) &&
                                 std::constructible_from<std::ranges::range_value_t<Storage>, Chunk>;

template <typename Storage, typename Batch>
concept ResumableChunkBatch =
    std::ranges::input_range<Batch> && (std::is_lvalue_reference_v<Batch> || std::ranges::borrowed_range<Batch>) &&
    ResumableChunkArgument<Storage, std::ranges::range_reference_t<Batch>>;

// The caller owns both descriptors and bytes. Append through this adapter, and
// retain descriptors until every cursor that refers to them has been discarded.
// Non-random-access descriptor storage must preserve iterators on push_back.
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
            ++*current_;
            if (offset_) {
                ++*offset_;
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
                current_ = std::ranges::end(chunk());
                limit_   = *current_;
            }

            assert(current_);
            --*current_;
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
            current_ = std::ranges::end(chunk());
            limit_   = *current_;
        }

        void normalize() const {
            if (end_marker_ || source_ == nullptr) {
                return;
            }
            if (!current_) {
                if (std::ranges::begin(*source_->storage_) == std::ranges::end(*source_->storage_)) {
                    return;
                }
                if constexpr (indexed) {
                    outer_ = 0;
                } else {
                    outer_ = std::ranges::begin(*source_->storage_);
                }
                current_ = std::ranges::begin(chunk());
                limit_   = std::ranges::end(chunk());
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
                outer_   = next;
                current_ = std::ranges::begin(chunk());
                limit_   = std::ranges::end(chunk());
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
        std::optional<std::size_t>            offset_{0};
        bool                                  end_marker_{};
    };

    explicit chunk_source(Storage &storage) : storage_(&storage) {
        for (auto &&chunk : storage) {
            add_extent(chunk);
            ++chunk_count_;
        }
    }

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

    template <typename... Chunks>
        requires((ResumableChunkArgument<Storage, Chunks> || ResumableChunkBatch<Storage, Chunks>) && ...) &&
                requires(Storage &storage, chunk_type chunk) { storage.push_back(std::move(chunk)); }
    std::size_t append(Chunks &&...chunks) {
        if (closed_) {
            throw std::logic_error("cannot append to a closed CBOR source");
        }
        const auto previous_count = chunk_count_;
        (append_argument(std::forward<Chunks>(chunks)), ...);
        return chunk_count_ - previous_count;
    }

    std::size_t generation() const { return generation_; }
    bool        eof() const { return closed_; }
    bool        owns(iterator position) const { return position.source_ == this; }

    void acquire() {
        if (active_) {
            throw std::logic_error("a CBOR source already has an active operation");
        }
        active_ = true;
    }

    void release() noexcept { active_ = false; }

    void close() {
        if (!closed_) {
            closed_ = true;
            ++generation_;
        }
    }

    auto remaining(iterator from) const {
        assert(from.source_ == this);
        return std::ranges::subrange(from, end());
    }

    std::optional<std::span<const std::byte>> available_span(iterator from) const {
        assert(from.source_ == this && !from.end_marker_);
        if constexpr (std::ranges::contiguous_range<chunk_type>) {
            from.normalize();
            if (from.at_end()) {
                return std::span<const std::byte>{};
            }
            assert(from.current_ && from.limit_);
            return std::span<const std::byte>{reinterpret_cast<const std::byte *>(std::to_address(*from.current_)),
                                              static_cast<std::size_t>(*from.limit_ - *from.current_)};
        } else {
            return std::nullopt;
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
                *from.current_ += static_cast<std::iter_difference_t<inner_iterator>>(amount);
                if (from.offset_) {
                    *from.offset_ += amount;
                }
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
        assert(from.source_ == this && !from.end_marker_);
        if constexpr (std::ranges::contiguous_range<chunk_type>) {
            from.normalize();
            if (from.at_end()) {
                return std::span<std::byte>{};
            }
            assert(from.current_ && from.limit_);
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
    template <typename Chunk> void add_extent(Chunk &chunk) {
        if constexpr (sized) {
            const auto amount = static_cast<std::size_t>(std::ranges::size(chunk));
            if (amount > std::numeric_limits<std::size_t>::max() - extent_) {
                throw std::length_error("CBOR source byte extent overflow");
            }
            extent_ += amount;
        }
    }

    template <typename Chunk> void append_chunk(Chunk &&chunk) {
        chunk_type descriptor(std::forward<Chunk>(chunk));
        if (std::ranges::empty(descriptor)) {
            return;
        }

        const auto previous_extent = extent_;
        add_extent(descriptor);
        try {
            storage_->push_back(std::move(descriptor));
        } catch (...) {
            extent_ = previous_extent;
            throw;
        }
        ++chunk_count_;
        ++generation_;
    }

    template <typename Arg> void append_argument(Arg &&arg) {
        if constexpr (ResumableChunkArgument<Storage, Arg>) {
            append_chunk(std::forward<Arg>(arg));
        } else {
            for (auto &&chunk : arg) {
                append_chunk(std::forward<decltype(chunk)>(chunk));
            }
        }
    }

    Storage    *storage_;
    std::size_t chunk_count_{};
    std::size_t extent_{};
    std::size_t generation_{};
    bool        closed_{};
    bool        active_{};
};

} // namespace detail

template <detail::ResumableChunkStorage Storage> class input_source : public detail::chunk_source<Storage> {
  public:
    using iterator      = typename detail::chunk_source<Storage>::iterator;
    using position_type = iterator;

    explicit input_source(Storage &storage) : detail::chunk_source<Storage>(storage), committed_(this->begin()) {}

    iterator committed_position() const { return committed_; }

    void commit(iterator position) {
        assert(this->owns(position) && position.offset() >= committed_.offset());
        committed_ = position;
    }

  private:
    iterator committed_;
};

template <detail::ResumableChunkStorage Storage> auto make_input_source(Storage &storage) { return input_source<Storage>(storage); }

template <detail::ResumableChunkStorage Storage>
    requires std::indirectly_writable<std::ranges::iterator_t<std::ranges::range_value_t<Storage>>,
                                      std::ranges::range_value_t<std::ranges::range_value_t<Storage>>>
class output_source {
  public:
    using value_type    = std::byte;
    using size_type     = std::size_t;
    using iterator      = typename detail::chunk_source<Storage>::iterator;
    using position_type = iterator;

    explicit output_source(Storage &storage) : chunks_(storage), working_(chunks_.begin()), committed_(working_) {}

    template <typename... Chunks>
        requires requires(detail::chunk_source<Storage> &source, Chunks &&...chunks) { source.append(std::forward<Chunks>(chunks)...); }
    std::size_t append(Chunks &&...chunks) {
        return chunks_.append(std::forward<Chunks>(chunks)...);
    }

    std::size_t generation() const { return chunks_.generation(); }
    bool        eof() const { return chunks_.eof(); }
    void        close() { chunks_.close(); }
    void        acquire() { chunks_.acquire(); }
    void        release() noexcept { chunks_.release(); }

    iterator      begin() const { return chunks_.begin(); }
    iterator      end() const { return working_; }
    size_type     size() const { return working_.offset(); }
    position_type position() const { return working_; }
    position_type committed_position() const { return committed_; }
    void          commit() { committed_ = working_; }

    void restore(position_type position) {
        assert(chunks_.owns(position) && position.offset() >= committed_.offset() && position.offset() <= working_.offset());
        working_ = position;
    }

    auto produced(position_type from, position_type to) const { return std::ranges::subrange(from, to); }
    auto remaining_output() const { return chunks_.remaining(working_); }
    auto available_output_span() { return chunks_.available_output_span(working_); }

    // Publish bytes already written through the current contiguous window.
    void advance(std::size_t count) {
        [[maybe_unused]] const auto advanced = chunks_.advance(working_, count);
        assert(advanced == count);
    }

    void push_back(std::byte value) {
        if (!chunks_.write_byte(working_, value)) {
            throw detail::encode_status_exception{status_code::incomplete};
        }
    }

    void write(std::span<const std::byte> bytes) {
        while (!bytes.empty()) {
            if (auto available = available_output_span()) {
                if (available->empty()) {
                    throw detail::encode_status_exception{status_code::incomplete};
                }
                const auto amount = std::min(bytes.size(), available->size());
                std::memcpy(available->data(), bytes.data(), amount);
                chunks_.advance(working_, amount);
                bytes = bytes.subspan(amount);
            } else {
                push_back(bytes.front());
                bytes = bytes.subspan(1);
            }
        }
    }

    template <std::input_iterator It, std::sentinel_for<It> Sentinel> void insert(iterator at, It first, Sentinel last) {
        assert(at == end());
        if (first == last) {
            return;
        }
        if constexpr (std::contiguous_iterator<It> && std::sized_sentinel_for<Sentinel, It> && IsCborBufferByte<std::iter_value_t<It>>) {
            write({reinterpret_cast<const std::byte *>(std::to_address(first)), static_cast<std::size_t>(last - first)});
        } else {
            for (; first != last; ++first) {
                push_back(static_cast<std::byte>(*first));
            }
        }
    }

    void insert(iterator at, std::initializer_list<std::byte> bytes) { insert(at, bytes.begin(), bytes.end()); }

  private:
    detail::chunk_source<Storage> chunks_;
    iterator                      working_;
    iterator                      committed_;
};

template <detail::ResumableChunkStorage Storage>
    requires requires(Storage &storage) { output_source<Storage>(storage); }
auto make_output_source(Storage &storage) {
    return output_source<Storage>(storage);
}

namespace detail {

template <typename Storage> struct appender<output_source<Storage>, false> {
    void reserve_for_append(output_source<Storage> &, std::uint64_t) const {}
    void operator()(output_source<Storage> &source, std::byte value) { source.push_back(value); }
    void operator()(output_source<Storage> &source, std::span<const std::byte> values) { source.write(values); }
    void operator()(output_source<Storage> &source, std::string_view value) {
        source.write({reinterpret_cast<const std::byte *>(value.data()), value.size()});
    }

    template <typename... Values> void multi_append(output_source<Storage> &source, Values... values) {
        const std::byte bytes[]{static_cast<std::byte>(values)...};
        source.write(bytes);
    }
};

} // namespace detail

} // namespace cbor::tags
