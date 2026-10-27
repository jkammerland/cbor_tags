#pragma once

#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/detail/cbor_argument.h"

#include <concepts>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>

namespace cbor::tags {

namespace detail {

template <typename R>
concept resumable_readable_range = std::ranges::forward_range<R> && IsCborBufferByte<std::ranges::range_value_t<R>>;

template <typename R>
concept resumable_writable_range =
    resumable_readable_range<R> && requires(std::ranges::iterator_t<R> it, std::ranges::range_value_t<R> byte) { *it = byte; };

template <typename R>
concept resumable_safe_range = std::is_lvalue_reference_v<R> || std::ranges::borrowed_range<R>;

struct readable_cursor {
    virtual ~readable_cursor()                                                                           = default;
    [[nodiscard]] virtual std::unique_ptr<readable_cursor>          clone() const                        = 0;
    [[nodiscard]] virtual bool                                      empty() const                        = 0;
    [[nodiscard]] virtual std::byte                                 get() const                          = 0;
    virtual void                                                    advance()                            = 0;
    [[nodiscard]] virtual bool                                      equal(const readable_cursor &) const = 0;
    [[nodiscard]] virtual std::optional<std::span<const std::byte>> contiguous() const                   = 0;
    [[nodiscard]] virtual std::unique_ptr<readable_cursor>          take(std::size_t, std::size_t &)     = 0;
};

template <typename Iterator, typename Sentinel> struct readable_cursor_model final : readable_cursor {
    Iterator current;
    Sentinel last;

    readable_cursor_model(const Iterator &begin, const Sentinel &end) : current(begin), last(end) {}

    [[nodiscard]] std::unique_ptr<readable_cursor> clone() const override { return std::make_unique<readable_cursor_model>(*this); }
    [[nodiscard]] bool                             empty() const override { return current == last; }
    [[nodiscard]] std::byte                        get() const override {
        const std::iter_value_t<Iterator> value = *current;
        return static_cast<std::byte>(cbor_byte_to_u8(value));
    }
    void advance() override { ++current; }

    [[nodiscard]] bool equal(const readable_cursor &other) const override {
        const auto *same = dynamic_cast<const readable_cursor_model *>(&other);
        return same != nullptr && current == same->current;
    }

    [[nodiscard]] std::optional<std::span<const std::byte>> contiguous() const override {
        if constexpr (std::contiguous_iterator<Iterator> && std::sized_sentinel_for<Sentinel, Iterator> &&
                      !std::is_volatile_v<std::remove_reference_t<std::iter_reference_t<Iterator>>>) {
            const auto count = static_cast<std::size_t>(last - current);
            return std::as_bytes(std::span{std::to_address(current), count});
        } else {
            return std::nullopt;
        }
    }

    [[nodiscard]] std::unique_ptr<readable_cursor> take(std::size_t limit, std::size_t &count) override {
        // Commit the cursor only after the borrowed prefix descriptor exists.
        auto        next = current;
        std::size_t used{};
        while (used != limit && next != last) {
            ++next;
            ++used;
        }
        auto prefix = std::make_unique<readable_cursor_model<Iterator, Iterator>>(current, next);
        current     = next;
        count       = used;
        return prefix;
    }
};

} // namespace detail

// A copied descriptor owns only its cursor; the original byte storage stays borrowed.
class readable_chunk {
    std::unique_ptr<detail::readable_cursor> cursor_;
    const void                              *identity_{};

    explicit readable_chunk(std::unique_ptr<detail::readable_cursor> cursor) : cursor_(std::move(cursor)) {}

  public:
    class iterator {
        std::unique_ptr<detail::readable_cursor> cursor_;

      public:
        using value_type        = std::byte;
        using difference_type   = std::ptrdiff_t;
        using iterator_concept  = std::forward_iterator_tag;
        using iterator_category = std::forward_iterator_tag;

        iterator() = default;
        explicit iterator(std::unique_ptr<detail::readable_cursor> cursor) : cursor_(std::move(cursor)) {}
        iterator(const iterator &other) : cursor_(other.cursor_ ? other.cursor_->clone() : nullptr) {}
        iterator &operator=(const iterator &other) {
            if (this != &other) {
                cursor_ = other.cursor_ ? other.cursor_->clone() : nullptr;
            }
            return *this;
        }
        iterator(iterator &&) noexcept            = default;
        iterator &operator=(iterator &&) noexcept = default;

        [[nodiscard]] std::byte operator*() const { return cursor_->get(); }
        iterator               &operator++() {
            cursor_->advance();
            return *this;
        }
        iterator operator++(int) {
            auto previous = *this;
            ++*this;
            return previous;
        }
        friend bool operator==(const iterator &left, const iterator &right) {
            if (!left.cursor_ || !right.cursor_) {
                return (!left.cursor_ || left.cursor_->empty()) && (!right.cursor_ || right.cursor_->empty());
            }
            return left.cursor_->equal(*right.cursor_);
        }
        friend bool operator==(const iterator &it, std::default_sentinel_t) { return !it.cursor_ || it.cursor_->empty(); }
    };

    readable_chunk() = default;

    template <typename R>
        requires(detail::resumable_readable_range<R> && detail::resumable_safe_range<R> &&
                 !std::same_as<std::remove_cvref_t<R>, readable_chunk>)
    explicit readable_chunk(R &&range)
        : cursor_(std::make_unique<detail::readable_cursor_model<std::ranges::iterator_t<R>, std::ranges::sentinel_t<R>>>(
              std::ranges::begin(range), std::ranges::end(range))) {
        if constexpr (std::is_lvalue_reference_v<R>)
            identity_ = std::addressof(range);
    }

    readable_chunk(const readable_chunk &other) : cursor_(other.cursor_ ? other.cursor_->clone() : nullptr), identity_(other.identity_) {}
    readable_chunk &operator=(const readable_chunk &other) {
        if (this != &other) {
            cursor_   = other.cursor_ ? other.cursor_->clone() : nullptr;
            identity_ = other.identity_;
        }
        return *this;
    }
    readable_chunk(readable_chunk &&) noexcept            = default;
    readable_chunk &operator=(readable_chunk &&) noexcept = default;

    [[nodiscard]] bool                                      empty() const { return !cursor_ || cursor_->empty(); }
    [[nodiscard]] const void                               *identity() const noexcept { return identity_; }
    [[nodiscard]] iterator                                  begin() const { return iterator{cursor_ ? cursor_->clone() : nullptr}; }
    [[nodiscard]] std::default_sentinel_t                   end() const noexcept { return {}; }
    [[nodiscard]] std::optional<std::span<const std::byte>> try_span() const {
        return cursor_ ? cursor_->contiguous() : std::optional<std::span<const std::byte>>{std::span<const std::byte>{}};
    }

    [[nodiscard]] std::byte read() {
        const auto value = cursor_->get();
        cursor_->advance();
        return value;
    }

    [[nodiscard]] readable_chunk take(std::size_t limit, std::size_t &count) {
        if (!cursor_) {
            count = 0;
            return {};
        }
        auto prefix      = readable_chunk{cursor_->take(limit, count)};
        prefix.identity_ = identity_;
        return prefix;
    }
};

namespace detail {

struct writable_cursor {
    virtual ~writable_cursor()                                              = default;
    [[nodiscard]] virtual std::unique_ptr<writable_cursor> clone() const    = 0;
    [[nodiscard]] virtual bool                             empty() const    = 0;
    virtual void                                           write(std::byte) = 0;
    [[nodiscard]] virtual readable_chunk                   produced() const = 0;
    virtual void                                           reset_begin()    = 0;
};

template <typename Iterator, typename Sentinel> struct writable_cursor_model final : writable_cursor {
    Iterator first;
    Iterator current;
    Sentinel last;

    writable_cursor_model(const Iterator &begin, const Sentinel &end) : first(begin), current(begin), last(end) {}
    [[nodiscard]] std::unique_ptr<writable_cursor> clone() const override { return std::make_unique<writable_cursor_model>(*this); }
    [[nodiscard]] bool                             empty() const override { return current == last; }
    void                                           write(std::byte value) override {
        using byte_type = std::iter_value_t<Iterator>;
        *current        = static_cast<byte_type>(std::to_integer<unsigned char>(value));
        ++current;
    }
    [[nodiscard]] readable_chunk produced() const override {
        return readable_chunk{std::ranges::subrange<Iterator, Iterator>{first, current}};
    }
    void reset_begin() override { first = current; }
};

} // namespace detail

class writable_chunk {
    std::unique_ptr<detail::writable_cursor> cursor_;

  public:
    writable_chunk() = default;
    template <typename R>
        requires(detail::resumable_writable_range<R> && detail::resumable_safe_range<R> &&
                 !std::same_as<std::remove_cvref_t<R>, writable_chunk>)
    explicit writable_chunk(R &&range)
        : cursor_(std::make_unique<detail::writable_cursor_model<std::ranges::iterator_t<R>, std::ranges::sentinel_t<R>>>(
              std::ranges::begin(range), std::ranges::end(range))) {}
    writable_chunk(const writable_chunk &other) : cursor_(other.cursor_ ? other.cursor_->clone() : nullptr) {}
    writable_chunk &operator=(const writable_chunk &other) {
        if (this != &other) {
            cursor_ = other.cursor_ ? other.cursor_->clone() : nullptr;
        }
        return *this;
    }
    writable_chunk(writable_chunk &&) noexcept            = default;
    writable_chunk &operator=(writable_chunk &&) noexcept = default;

    [[nodiscard]] bool           empty() const { return !cursor_ || cursor_->empty(); }
    void                         write(std::byte value) { cursor_->write(value); }
    [[nodiscard]] readable_chunk produced() const { return cursor_ ? cursor_->produced() : readable_chunk{}; }
    void                         reset_begin() {
        if (cursor_)
            cursor_->reset_begin();
    }
};

} // namespace cbor::tags

template <> inline constexpr bool std::ranges::enable_borrowed_range<cbor::tags::readable_chunk> = true;
