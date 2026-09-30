#include "expected_test_types.h"

#include <array>
#include <cbor_tags/detail/expected.h>
#include <doctest/doctest.h>
#include <memory>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>
#include <version>

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L
#include <expected>

namespace ex = cbor::tags::detail::expected_impl;
using namespace expected_test;

namespace {
template <typename T> constexpr bool member_swappable = requires(T &t) { t.swap(t); };

template <typename T, typename E> constexpr bool matching_traits() {
    using A = ex::expected<T, E>;
    using B = std::expected<T, E>;
    // C++23 requires assignment to T itself. libstdc++ 16 currently strips const
    // in those constraints, so use the normative contract for const values.
    constexpr bool assignment = [] {
        if constexpr (std::is_const_v<T> && !std::is_void_v<T>) {
            return !std::is_copy_assignable_v<A> && !std::is_move_assignable_v<A> && !std::is_swappable_v<A> && !member_swappable<A>;
        } else if constexpr (std::is_void_v<T> && std::is_same_v<E, copy_only>) {
            // LWG 4025 changed expected<cv void, E>'s move assignment from
            // conditionally deleted to constrained. Older libc++ still deletes
            // it, which also disables the generic swap fallback for copy_only.
            return std::is_copy_assignable_v<A> == std::is_copy_assignable_v<B> && std::is_move_assignable_v<A> &&
                   std::is_nothrow_move_assignable_v<A> && std::is_swappable_v<A> && std::is_nothrow_swappable_v<A> &&
                   !member_swappable<A> && !member_swappable<B>;
        } else {
            return std::is_copy_assignable_v<A> == std::is_copy_assignable_v<B> &&
                   std::is_move_assignable_v<A> == std::is_move_assignable_v<B> &&
                   std::is_nothrow_move_assignable_v<A> == std::is_nothrow_move_assignable_v<B> &&
                   std::is_swappable_v<A> == std::is_swappable_v<B> && std::is_nothrow_swappable_v<A> == std::is_nothrow_swappable_v<B> &&
                   member_swappable<A> == member_swappable<B>;
        }
    }();
    return assignment && std::is_default_constructible_v<A> == std::is_default_constructible_v<B> &&
           std::is_copy_constructible_v<A> == std::is_copy_constructible_v<B> &&
           std::is_move_constructible_v<A> == std::is_move_constructible_v<B> &&
           std::is_trivially_destructible_v<A> == std::is_trivially_destructible_v<B> &&
           std::is_trivially_copy_constructible_v<A> == std::is_trivially_copy_constructible_v<B> &&
           std::is_trivially_move_constructible_v<A> == std::is_trivially_move_constructible_v<B> &&
           std::is_nothrow_default_constructible_v<A> == std::is_nothrow_default_constructible_v<B> &&
           std::is_nothrow_move_constructible_v<A> == std::is_nothrow_move_constructible_v<B> &&
           std::is_constructible_v<A, int> == std::is_constructible_v<B, int> &&
           std::is_convertible_v<int, A> == std::is_convertible_v<int, B> &&
           std::is_constructible_v<A, std::in_place_t, int> == std::is_constructible_v<B, std::in_place_t, int> &&
           std::is_constructible_v<A, ex::unexpect_t, int> == std::is_constructible_v<B, std::unexpect_t, int>;
}
template <typename T> constexpr bool matching_error_types() {
    return matching_traits<T, int>() && matching_traits<T, std::string>() && matching_traits<T, std::unique_ptr<int>>() &&
           matching_traits<T, immovable>() && matching_traits<T, copy_only>() && matching_traits<T, tracked<true>>() &&
           matching_traits<T, tracked<false>>();
}
static_assert(matching_error_types<int>());
static_assert(matching_error_types<const int>());
static_assert(matching_error_types<std::string>());
static_assert(matching_error_types<std::unique_ptr<int>>());
static_assert(matching_error_types<immovable>());
static_assert(matching_error_types<copy_only>());
static_assert(matching_error_types<tracked<true>>());
static_assert(matching_error_types<tracked<false>>());
static_assert(matching_error_types<void>());
static_assert(matching_error_types<const void>());

struct builtin {
    template <typename T, typename E> using result = ex::expected<T, E>;
    static constexpr auto unexpect                 = ex::unexpect;
};
struct native {
    template <typename T, typename E> using result = std::expected<T, E>;
    static constexpr auto unexpect                 = std::unexpect;
};

template <typename Family> constexpr std::array<int, 8> transitions() {
    using result = typename Family::template result<int, int>;
    result             value(3), error(Family::unexpect, 7);
    std::array<int, 8> observations{};
    observations[0] = value.value_or(99);
    observations[1] = error.value_or(99);
    value.swap(error);
    observations[2] = value.error();
    observations[3] = *error;
    value           = error;
    observations[4] = *value;
    error           = result(Family::unexpect, 8);
    observations[5] = error.error();
    error.emplace(9);
    observations[6] = *error;
    typename Family::template result<bool, long> boolean(result(0));
    observations[7] = *boolean;
    return observations;
}
static_assert(transitions<builtin>() == std::array{3, 99, 7, 3, 3, 8, 9, 0});
static_assert(transitions<native>() == transitions<builtin>());

template <typename Family> std::array<int, 6> throwing_transitions() {
    using result = typename Family::template result<tracked<false>, tracked<true>>;
    counts             values, errors;
    std::array<int, 6> observations{};
    {
        result value(std::in_place, values, 4), error(Family::unexpect, errors, 7);
        values.fail_copy = true;
        try {
            error = value;
        } catch (const failure &) { ++observations[0]; }
        observations[1]  = error.error().number;
        values.fail_move = true;
        try {
            value.swap(error);
        } catch (const failure &) { ++observations[2]; }
        observations[3] = value->number;
    }
    observations[4] = values.live;
    observations[5] = errors.live;
    return observations;
}
} // namespace

TEST_SUITE("expected/std_parity") {
    TEST_CASE("state changes and exception guarantees match native expected") {
        CHECK(transitions<builtin>() == transitions<native>());
        const auto builtin_failures = throwing_transitions<builtin>();
        const auto native_failures  = throwing_transitions<native>();
        CHECK(builtin_failures == std::array{1, 7, 1, 4, 0, 0});
        CHECK(builtin_failures == native_failures);
    }

#if __cpp_lib_expected >= 202211L
    TEST_CASE("monadic results and callback categories match native expected") {
        ex::expected<int, int>  value(4), error(ex::unexpect, 7);
        std::expected<int, int> native_value(4), native_error(std::unexpect, 7);
        CHECK(*value.transform(qualifier{}) == *native_value.transform(qualifier{}));
        CHECK(*std::as_const(value).transform(qualifier{}) == *std::as_const(native_value).transform(qualifier{}));
        CHECK(*std::move(value).transform(qualifier{}) == *std::move(native_value).transform(qualifier{}));
        CHECK(*std::move(std::as_const(value)).transform(qualifier{}) == *std::move(std::as_const(native_value)).transform(qualifier{}));
        CHECK(error.transform_error(qualifier{}).error() == native_error.transform_error(qualifier{}).error());
        CHECK(std::as_const(error).transform_error(qualifier{}).error() ==
              std::as_const(native_error).transform_error(qualifier{}).error());
        CHECK(std::move(error).transform_error(qualifier{}).error() == std::move(native_error).transform_error(qualifier{}).error());
        CHECK(std::move(std::as_const(error)).transform_error(qualifier{}).error() ==
              std::move(std::as_const(native_error)).transform_error(qualifier{}).error());
        auto ours   = value.transform([](int n) { return immovable(n + 1); });
        auto theirs = native_value.transform([](int n) { return immovable(n + 1); });
        CHECK(ours->number == theirs->number);
    }
#endif
}
#endif
