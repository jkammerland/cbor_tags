#pragma once

// DEPRECATED - use std::ranges or ranges-v3 instead

#include <concepts>
#include <ranges>
#include <stdexcept>
#include <tuple>
#include <type_traits>

namespace cbor::tags {

// Primary template for concat_view
template <std::ranges::input_range... Views>
    requires(std::ranges::view<Views> && ...)
class concat_view : public std::ranges::view_interface<concat_view<Views...>> {
  private:
    std::tuple<Views...> views_;

  public:
    concat_view() = default;

    constexpr explicit concat_view(Views... views) : views_(std::move(views)...) {}

    // Iterator implementation that traverses multiple ranges
    class iterator {
      public:
        using value_type        = std::common_type_t<std::ranges::range_value_t<Views>...>;
        using difference_type   = std::common_type_t<std::ranges::range_difference_t<Views>...>;
        using reference         = std::common_reference_t<std::ranges::range_reference_t<Views>...>;
        using iterator_category = std::input_iterator_tag;

      private:
        concat_view                                  *parent_      = nullptr;
        size_t                                        current_idx_ = 0;
        std::tuple<std::ranges::iterator_t<Views>...> iterators_;

        // Check if current iterator is at its end
        bool at_end() const {
            bool result = false;
            [&]<size_t... Is>(std::index_sequence<Is...>) {
                ((current_idx_ == Is ? (result = (std::get<Is>(iterators_) == std::ranges::end(std::get<Is>(parent_->views_)))) : false),
                 ...);
            }(std::index_sequence_for<Views...>{});
            return result;
        }

        // Move to next non-empty view
        void next_view() {
            ++current_idx_;
            while (current_idx_ < sizeof...(Views)) {
                // Initialize iterator for the new current view
                [this]<size_t... Is>(std::index_sequence<Is...>) {
                    ((current_idx_ == Is ? (std::get<Is>(iterators_) = std::ranges::begin(std::get<Is>(parent_->views_)), 0) : 0), ...);
                }(std::index_sequence_for<Views...>{});

                if (!at_end())
                    break;
                ++current_idx_;
            }
        }

        // Recursive helper template to get the reference from the correct iterator
        template <size_t I = 0> reference visit_iterator() const {
            if constexpr (I >= sizeof...(Views)) {
                throw std::out_of_range("Invalid iterator index");
            } else if (current_idx_ == I) {
                return *std::get<I>(iterators_);
            } else {
                return visit_iterator<I + 1>();
            }
        }

      public:
        iterator() = default;

        explicit iterator(concat_view *parent, bool at_end = false) : parent_(parent), current_idx_(at_end ? sizeof...(Views) : 0) {
            if (!at_end) {
                // Initialize iterators
                [this]<size_t... Is>(std::index_sequence<Is...>) {
                    ((std::get<Is>(iterators_) = std::ranges::begin(std::get<Is>(parent_->views_))), ...);
                }(std::index_sequence_for<Views...>{});

                // Skip empty initial views
                if (this->at_end()) {
                    next_view();
                }
            }
        }

        reference operator*() const { return visit_iterator(); }

        iterator &operator++() {
            [&]<size_t... Is>(std::index_sequence<Is...>) {
                ((current_idx_ == Is ? (++std::get<Is>(iterators_), true) : false), ...);
            }(std::index_sequence_for<Views...>{});

            if (at_end()) {
                next_view();
            }
            return *this;
        }

        iterator operator++(int) {
            auto tmp = *this;
            ++(*this);
            return tmp;
        }

        bool operator==(const iterator &other) const {
            if (current_idx_ != other.current_idx_)
                return false;
            if (current_idx_ == sizeof...(Views))
                return true;

            bool result = false;
            [&]<size_t... Is>(std::index_sequence<Is...>) {
                ((current_idx_ == Is ? (result = (std::get<Is>(iterators_) == std::get<Is>(other.iterators_))) : false), ...);
            }(std::index_sequence_for<Views...>{});
            return result;
        }

        bool operator!=(const iterator &other) const { return !(*this == other); }
    };

    auto begin() { return iterator{this}; }
    auto end() { return iterator{this, true}; }
};

// Deduction guide
template <typename... Ranges> concat_view(Ranges &&...ranges) -> concat_view<std::views::all_t<Ranges>...>;

// Factory function that creates a concat_view
struct concat_fn {
    template <typename... Rngs>
        requires((std::ranges::viewable_range<Rngs> && std::ranges::input_range<Rngs>) && ...)
    constexpr auto operator()(Rngs &&...rngs) const {
        return concat_view<std::views::all_t<Rngs>...>{std::views::all(std::forward<Rngs>(rngs))...};
    }
};

inline constexpr concat_fn concat{};
} // namespace cbor::tags