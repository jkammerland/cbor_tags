#pragma once

#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/detail/cbor_item.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>

namespace cbor::tags {

namespace detail {

template <std::uint64_t... Tags> struct static_tag_filter {
    constexpr bool operator()(std::uint64_t tag) const { return ((tag == Tags) || ...); }
};

template <typename Buffer>
concept LazyTagViewableBuffer =
    requires(Buffer &buffer) { std::views::all(buffer); } && CborInputBuffer<decltype(std::views::all(std::declval<Buffer &>()))>;

} // namespace detail

template <CborInputBuffer Buffer, template <typename> typename... Extensions> class tag_payload_decoder {
  public:
    using buffer_type = std::remove_cvref_t<Buffer>;
    using iterator    = std::ranges::iterator_t<const buffer_type>;
    using subrange    = std::ranges::subrange<iterator>;

    constexpr explicit tag_payload_decoder(subrange range) : range_(std::move(range)) {}

    template <typename... T> [[nodiscard]] auto operator()(T &&...values) {
        auto dec = make_payload_decoder();
        return dec(std::forward<T>(values)...);
    }

    template <typename T> [[nodiscard]] auto decode(T &value) { return (*this)(value); }

  private:
    [[nodiscard]] auto make_payload_decoder() {
        if constexpr (sizeof...(Extensions) == 0) {
            return cbor::tags::make_decoder(range_);
        } else {
            return cbor::tags::make_decoder<Extensions...>(range_);
        }
    }

    subrange range_;
};

template <CborInputBuffer Buffer> class tag_match {
  public:
    using buffer_type      = std::remove_cvref_t<Buffer>;
    using iterator         = std::ranges::iterator_t<const buffer_type>;
    using subrange         = std::ranges::subrange<iterator>;
    using payload_iterator = cast_view_iterator<iterator, std::byte>;
    using payload_subrange = std::ranges::subrange<payload_iterator>;
    using payload_view     = bstr_view<payload_subrange>;

    constexpr tag_match() = default;
    constexpr tag_match(std::uint64_t tag, iterator payload_begin, iterator payload_end)
        : tag_(tag), payload_begin_(payload_begin), payload_end_(payload_end) {}

    [[nodiscard]] constexpr std::uint64_t tag() const noexcept { return tag_; }
    [[nodiscard]] constexpr payload_view  payload_range() const {
        return payload_view{payload_subrange{payload_iterator{payload_begin_}, payload_iterator{payload_end_}}};
    }

    [[nodiscard]] auto payload_span() const
        requires std::ranges::contiguous_range<const buffer_type>
    {
        const auto size = static_cast<std::size_t>(std::ranges::distance(payload_begin_, payload_end_));
        return std::span<const std::byte>{reinterpret_cast<const std::byte *>(std::to_address(payload_begin_)), size};
    }

    template <template <typename> typename... Extensions> [[nodiscard]] auto make_decoder() const {
        return tag_payload_decoder<buffer_type, Extensions...>{subrange{payload_begin_, payload_end_}};
    }

    template <template <typename> typename... Extensions, typename T> [[nodiscard]] auto decode(T &value) const {
        auto dec = make_decoder<Extensions...>();
        return dec(value);
    }

  private:
    std::uint64_t tag_{};
    iterator      payload_begin_{};
    iterator      payload_end_{};
};

template <CborInputBuffer Buffer, typename Predicate> class tag_view : public std::ranges::view_interface<tag_view<Buffer, Predicate>> {
  public:
    using buffer_type = std::remove_cvref_t<Buffer>;
    using iterator_t  = std::ranges::iterator_t<const buffer_type>;
    using match_type  = tag_match<buffer_type>;

    class iterator {
      public:
        using value_type        = match_type;
        using difference_type   = std::ptrdiff_t;
        using iterator_category = std::input_iterator_tag;
        using iterator_concept  = std::input_iterator_tag;

        iterator() = default;
        explicit iterator(tag_view *view)
            : view_(view), walker_(std::ranges::begin(std::as_const(view->buffer_)), std::ranges::end(std::as_const(view->buffer_))) {
            find_next();
        }

        const match_type &operator*() const { return current_; }
        const match_type *operator->() const { return &current_; }

        iterator &operator++() {
            find_next();
            return *this;
        }

        void operator++(int) { ++(*this); }

        friend bool operator==(const iterator &lhs, std::default_sentinel_t) { return lhs.done_; }
        friend bool operator==(std::default_sentinel_t, const iterator &rhs) { return rhs.done_; }

      private:
        void fail(status_code status) {
            view_->status_ = status;
            done_          = true;
        }

        void find_next() {
            if (done_) {
                return;
            }

            detail::cbor_tag_event<iterator_t> event{};
            while (walker_.next_tag(event)) {
                if (!std::invoke(view_->predicate_, event.tag)) {
                    continue;
                }

                auto        payload_end = event.payload_begin;
                status_code status      = status_code::success;
                if (!detail::cbor_item_skipper<max_stack_depth>::skip_item(payload_end, walker_.end(), status, walker_.stack_depth())) {
                    fail(status);
                    return;
                }

                current_ = match_type{event.tag, event.payload_begin, payload_end};
                return;
            }

            view_->status_ = walker_.status();
            done_          = true;
        }

        static constexpr std::size_t                          max_stack_depth = 256;
        tag_view                                             *view_{};
        detail::cbor_item_walker<max_stack_depth, iterator_t> walker_{};
        match_type                                            current_{};
        bool                                                  done_{};
    };

    constexpr tag_view(buffer_type buffer, Predicate predicate) : buffer_(std::move(buffer)), predicate_(std::move(predicate)) {}

    iterator begin() {
        status_ = status_code::success;
        return iterator{this};
    }
    std::default_sentinel_t end() const noexcept { return {}; }

    [[nodiscard]] status_code status() const noexcept { return status_; }
    [[nodiscard]] bool        failed() const noexcept { return status_ != status_code::success; }

  private:
    buffer_type buffer_;
    Predicate   predicate_;
    status_code status_{status_code::success};

    friend class iterator;
};

template <std::uint64_t... Tags, detail::LazyTagViewableBuffer Buffer> [[nodiscard]] auto find_tags(Buffer &buffer) {
    auto view = std::views::all(buffer);
    return tag_view<decltype(view), detail::static_tag_filter<Tags...>>{std::move(view), {}};
}

template <std::uint64_t... Tags, CborInputBuffer Buffer>
auto find_tags(Buffer &&buffer)
    requires(!std::is_lvalue_reference_v<Buffer>)
= delete;

template <detail::LazyTagViewableBuffer Buffer, typename Predicate> [[nodiscard]] auto find_tags(Buffer &buffer, Predicate predicate) {
    auto view = std::views::all(buffer);
    return tag_view<decltype(view), std::remove_cvref_t<Predicate>>{std::move(view), std::forward<Predicate>(predicate)};
}

template <CborInputBuffer Buffer, typename Predicate>
auto find_tags(Buffer &&buffer, Predicate &&predicate)
    requires(!std::is_lvalue_reference_v<Buffer>)
= delete;

} // namespace cbor::tags
