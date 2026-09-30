#include "expected_test_types.h"

#include <array>
#include <cbor_tags/detail/expected.h>
#include <doctest/doctest.h>
#include <type_traits>
#include <utility>

namespace ex = cbor::tags::detail::expected_impl;
using namespace expected_test;

namespace {
struct comparison_counts {
    int comparisons{};
    int logical_operators{};
};
struct equality_proxy {
    bool               equal;
    comparison_counts *counts;
    constexpr          operator bool() const noexcept { return equal; }
    // Correct comparisons never call these deliberately poisoned operators.
    [[maybe_unused]] friend bool operator&&(bool, equality_proxy result) {
        ++result.counts->logical_operators;
        return !result.equal;
    }
    [[maybe_unused]] friend bool operator||(bool, equality_proxy result) {
        ++result.counts->logical_operators;
        return !result.equal;
    }
};
struct comparable {
    int                   number;
    comparison_counts    *counts;
    friend equality_proxy operator==(const comparable &left, const comparable &right) {
        ++left.counts->comparisons;
        return {left.number == right.number, left.counts};
    }
};

template <bool DeleteExplicit> struct fallback;
template <bool DeleteExplicit> struct converted_error {
    int number;
    constexpr explicit converted_error(int number) : number(number) {}
    constexpr explicit converted_error(const fallback<DeleteExplicit> &)
        requires(!DeleteExplicit)
        : number(11) {}
    explicit converted_error(const fallback<DeleteExplicit> &)
        requires DeleteExplicit
    = delete;
};
template <bool DeleteExplicit> struct fallback {
    int      *conversions;
    constexpr operator converted_error<DeleteExplicit>() const {
        ++*conversions;
        return converted_error<DeleteExplicit>(22);
    }
};

template <bool DeleteExplicit> constexpr std::array<int, 9> implicit_fallback_results() {
    using error_type = converted_error<DeleteExplicit>;
    int                            conversions{};
    const fallback<DeleteExplicit> alternative{&conversions};
    static_assert(std::is_convertible_v<decltype(alternative) &, error_type>);
    ex::expected<int, error_type>  value(3), error(ex::unexpect, 7);
    ex::expected<void, error_type> empty, failed(ex::unexpect, 8);
    return {std::as_const(value).error_or(alternative).number,
            std::move(value).error_or(alternative).number,
            std::as_const(empty).error_or(alternative).number,
            std::move(empty).error_or(alternative).number,
            std::as_const(error).error_or(alternative).number,
            std::move(error).error_or(alternative).number,
            std::as_const(failed).error_or(alternative).number,
            std::move(failed).error_or(alternative).number,
            conversions};
}
constexpr auto expected_fallback_results = std::array{22, 22, 22, 22, 7, 7, 8, 8, 4};
static_assert(implicit_fallback_results<false>() == expected_fallback_results);
static_assert(implicit_fallback_results<true>() == expected_fallback_results);

struct accepts_anything {
    template <typename U> constexpr explicit accepts_anything(U &&) {}
};
using tagged_result = ex::expected<accepts_anything, explicit_int>;
template <typename Tag>
constexpr bool rejects_error_tag = !std::is_constructible_v<tagged_result, Tag> && !std::is_constructible_v<tagged_result, Tag &> &&
                                   !std::is_constructible_v<tagged_result, Tag &&>;
static_assert(rejects_error_tag<ex::unexpect_t>);
static_assert(rejects_error_tag<const ex::unexpect_t>);
static_assert(rejects_error_tag<volatile ex::unexpect_t>);
static_assert(rejects_error_tag<const volatile ex::unexpect_t>);
static_assert(tagged_result(std::in_place, ex::unexpect).has_value());
static_assert(tagged_result(ex::unexpect, 9).error().number == 9);

struct transfer_counts {
    int live{};
    int constructed{};
    int destroyed{};
    int copies{};
    int moves{};
    int const_moves{};
};

// Both rvalue constructors work, but copying either throws or is deleted.
// Mutable state makes transfer from a const rvalue well-defined.
template <bool Copyable> struct const_movable {
    transfer_counts *state;
    mutable int      number;
    constexpr const_movable(transfer_counts &state, int number) noexcept : state(&state), number(number) {
        ++state.live;
        ++state.constructed;
    }
    const_movable(const const_movable &other)
        requires Copyable
        : state(other.state), number(other.number) {
        ++state->copies;
        throw failure{};
    }
    const_movable(const const_movable &)
        requires(!Copyable)
    = delete;
    constexpr const_movable(const_movable &&other) noexcept : const_movable(*other.state, std::exchange(other.number, -1)) {
        ++state->moves;
    }
    constexpr const_movable(const const_movable &&other) noexcept : const_movable(*other.state, std::exchange(other.number, -1)) {
        ++state->const_moves;
    }
    constexpr ~const_movable() {
        --state->live;
        ++state->destroyed;
    }
    friend constexpr void swap(const const_movable &left, const const_movable &right) noexcept { std::swap(left.number, right.number); }
};

struct constexpr_error {
    int number;
    constexpr explicit constexpr_error(int number) : number(number) {}
    constexpr constexpr_error(constexpr_error &&other) noexcept(false) : number(std::exchange(other.number, -1)) {}
    constexpr constexpr_error &operator=(int value) {
        number = value;
        return *this;
    }
    friend constexpr void swap(constexpr_error &left, constexpr_error &right) noexcept { std::swap(left.number, right.number); }
};

// A const payload can move and swap through an application-owned value. This
// avoids reading a mutable member during constant evaluation on GCC 12.
struct constexpr_const_movable {
    transfer_counts *state;
    int             *number;
    constexpr constexpr_const_movable(transfer_counts &state, int &number) noexcept : state(&state), number(&number) {
        ++state.live;
        ++state.constructed;
    }
    constexpr_const_movable(const constexpr_const_movable &) = delete;
    constexpr constexpr_const_movable(constexpr_const_movable &&other) noexcept : constexpr_const_movable(*other.state, *other.number) {
        ++state->moves;
    }
    constexpr constexpr_const_movable(const constexpr_const_movable &&other) noexcept
        : constexpr_const_movable(*other.state, *other.number) {
        ++state->const_moves;
    }
    constexpr ~constexpr_const_movable() {
        --state->live;
        ++state->destroyed;
    }
    friend constexpr void swap(const constexpr_const_movable &left, const constexpr_const_movable &right) noexcept {
        std::swap(*left.number, *right.number);
    }
};

template <typename Error> constexpr bool constant_const_transfers() {
    transfer_counts state;
    int             first = 7, second = 8;
    {
        using result = ex::expected<const constexpr_const_movable, Error>;
        result value(std::in_place, state, first), error(ex::unexpect, 9);
        value.swap(error);
        if (value.has_value() || !error.has_value() || error->number != &first || *error->number != 7)
            return false;
        value.swap(error);
        if (!value.has_value() || error.has_value() || value->number != &first || *value->number != 7)
            return false;
        value = ex::unexpected(11);
        if (value.has_value() || state.live != 0)
            return false;
        value.emplace(state, second);
        if (value->number != &second || *value->number != 8 || state.live != 1)
            return false;
    }
    return state.live == 0 && state.constructed == state.destroyed && state.copies == 0 && state.const_moves == 0;
}
static_assert(constant_const_transfers<int>());
static_assert(constant_const_transfers<constexpr_error>());

template <bool Copyable, bool ErrorNothrowMove> void const_value_swap() {
    using value_type = const const_movable<Copyable>;
    using result     = ex::expected<value_type, tracked<ErrorNothrowMove>>;
    static_assert(std::is_nothrow_move_constructible_v<value_type>);
    static_assert(std::is_nothrow_swappable_v<value_type>);
    static_assert(noexcept(std::declval<result &>().swap(std::declval<result &>())) == ErrorNothrowMove);
    transfer_counts values;
    counts          errors;
    {
        result value(std::in_place, values, 7), error(ex::unexpect, errors, 9);
        value.swap(error);
        REQUIRE_FALSE(value.has_value());
        REQUIRE(error.has_value());
        CHECK(value.error().number == 9);
        CHECK(error->number == 7);
        CHECK(values.live == 1);
        CHECK(errors.live == 1);
        // Exercise the error-to-value orientation, including its delegation.
        value.swap(error);
        REQUIRE(value.has_value());
        REQUIRE_FALSE(error.has_value());
        CHECK(value->number == 7);
        CHECK(error.error().number == 9);
        if constexpr (!ErrorNothrowMove) {
            errors.fail_move = true;
            CHECK_THROWS_AS(value.swap(error), failure);
            REQUIRE(value.has_value());
            REQUIRE_FALSE(error.has_value());
            CHECK(value->number == 7);
            CHECK(error.error().number == 9);
            CHECK_THROWS_AS(error.swap(value), failure);
            REQUIRE(value.has_value());
            REQUIRE_FALSE(error.has_value());
            CHECK(value->number == 7);
            CHECK(error.error().number == 9);
        }
        CHECK(values.live == 1);
        CHECK(errors.live == 1);
        CHECK(values.copies == 0);
        CHECK(values.const_moves == 0);
        CHECK(values.moves > 0);
    }
    CHECK(values.live == 0);
    CHECK(errors.live == 0);
    CHECK(values.constructed == values.destroyed);
}

template <bool Copyable> void const_value_assignment() {
    using result = ex::expected<const const_movable<Copyable>, tracked<false>>;
    static_assert(std::is_assignable_v<result &, ex::unexpected<tracked<false>>>);
    transfer_counts values;
    counts          errors;
    {
        result                         value(std::in_place, values, 7);
        ex::unexpected<tracked<false>> error(std::in_place, errors, 9);
        errors.fail_copy = true;
        CHECK_THROWS_AS(value = std::as_const(error), failure);
        REQUIRE(value.has_value());
        CHECK(value->number == 7);
        CHECK(values.live == 1);
        CHECK(errors.live == 1);
        errors.fail_copy = false;
        errors.fail_move = true;
        CHECK_THROWS_AS(value = std::move(error), failure);
        REQUIRE(value.has_value());
        CHECK(value->number == 7);
        CHECK(values.live == 1);
        CHECK(errors.live == 1);
        errors.fail_move = false;
        value            = error;
        REQUIRE_FALSE(value.has_value());
        CHECK(value.error().number == 9);
        CHECK(values.live == 0);
        CHECK(errors.live == 2);
        CHECK(values.copies == 0);
        CHECK(values.const_moves == 0);
    }
    CHECK(values.live == 0);
    CHECK(errors.live == 0);
    CHECK(values.constructed == values.destroyed);
}

template <bool Copyable> void public_const_move() {
    transfer_counts state;
    {
        using result = ex::expected<const const_movable<Copyable>, int>;
        static_assert(std::is_nothrow_move_constructible_v<result>);
        result original(std::in_place, state, 7);
        result moved(std::move(original));
        REQUIRE(original.has_value());
        REQUIRE(moved.has_value());
        CHECK(moved->number == 7);
        CHECK(original->number == -1);
        CHECK(state.live == 2);
        CHECK(state.copies == 0);
        CHECK(state.moves == 0);
        CHECK(state.const_moves == 1);
    }
    CHECK(state.live == 0);
    CHECK(state.constructed == state.destroyed);
}
} // namespace

TEST_SUITE("expected/contract") {
    TEST_CASE("comparison proxies never replace built-in short-circuit operators") {
        using result = ex::expected<comparable, comparable>;
        comparison_counts counts;
        auto              check_comparison = [&](auto compare, bool equal, int comparisons) {
            counts = {};
            CHECK(compare() == equal);
            CHECK(counts.comparisons == comparisons);
            CHECK(counts.logical_operators == 0);
        };
        for (bool left_value : {false, true}) {
            for (bool same_number : {false, true}) {
                comparable           left_payload{7, &counts}, right_payload{same_number ? 7 : 8, &counts};
                result               left = left_value ? result(left_payload) : result(ex::unexpect, left_payload);
                const ex::unexpected unexpected(right_payload);
                check_comparison([&] { return left == right_payload; }, left_value && same_number, left_value ? 1 : 0);
                check_comparison([&] { return right_payload == left; }, left_value && same_number, left_value ? 1 : 0);
                check_comparison([&] { return left == unexpected; }, !left_value && same_number, left_value ? 0 : 1);
                check_comparison([&] { return unexpected == left; }, !left_value && same_number, left_value ? 0 : 1);
                for (bool right_value : {false, true}) {
                    result     right       = right_value ? result(right_payload) : result(ex::unexpect, right_payload);
                    const bool equal       = left_value == right_value && same_number;
                    const int  comparisons = left_value == right_value ? 1 : 0;
                    check_comparison([&] { return left == right; }, equal, comparisons);
                    check_comparison([&] { return right == left; }, equal, comparisons);
                    check_comparison([&] { return left != right; }, !equal, comparisons);
                }
            }
        }
    }

    TEST_CASE("void comparison proxies only inspect errors when both operands failed") {
        using result = ex::expected<void, comparable>;
        comparison_counts counts;
        auto              check_comparison = [&](auto compare, bool equal, int comparisons) {
            counts = {};
            CHECK(compare() == equal);
            CHECK(counts.comparisons == comparisons);
            CHECK(counts.logical_operators == 0);
        };
        for (bool left_value : {false, true}) {
            for (bool same_number : {false, true}) {
                comparable           left_payload{7, &counts}, right_payload{same_number ? 7 : 8, &counts};
                result               left = left_value ? result() : result(ex::unexpect, left_payload);
                const ex::unexpected unexpected(right_payload);
                check_comparison([&] { return left == unexpected; }, !left_value && same_number, left_value ? 0 : 1);
                check_comparison([&] { return unexpected == left; }, !left_value && same_number, left_value ? 0 : 1);
                for (bool right_value : {false, true}) {
                    result     right       = right_value ? result() : result(ex::unexpect, right_payload);
                    const bool equal       = left_value == right_value && (left_value || same_number);
                    const int  comparisons = !left_value && !right_value ? 1 : 0;
                    check_comparison([&] { return left == right; }, equal, comparisons);
                    check_comparison([&] { return right == left; }, equal, comparisons);
                    check_comparison([&] { return left != right; }, !equal, comparisons);
                }
            }
        }
    }

    TEST_CASE("error_or implicitly converts its fallback only for successful results") {
        CHECK(implicit_fallback_results<false>() == expected_fallback_results);
        CHECK(implicit_fallback_results<true>() == expected_fallback_results);
        // value_or has an explicitly different conversion contract.
        int                                       conversions{};
        const fallback<false>                     alternative{&conversions};
        ex::expected<converted_error<false>, int> error(ex::unexpect, 7);
        CHECK(std::as_const(error).value_or(alternative).number == 11);
        CHECK(std::move(error).value_or(alternative).number == 11);
        CHECK(conversions == 0);
    }

    TEST_CASE("construction tags select errors unless a value is explicitly requested") {
        tagged_result value(std::in_place, ex::unexpect);
        tagged_result error(ex::unexpect, 9);
        CHECK(value.has_value());
        REQUIRE_FALSE(error.has_value());
        CHECK(error.error().number == 9);
    }

    TEST_CASE("const payload swaps transfer values without requiring lvalue copies") {
        const_value_swap<true, true>();
        const_value_swap<true, false>();
        const_value_swap<false, true>();
        const_value_swap<false, false>();
    }

    TEST_CASE("const payload assignment restores its value after error construction throws") {
        const_value_assignment<true>();
        const_value_assignment<false>();
    }

    TEST_CASE("public move construction retains const rvalue payload semantics") {
        public_const_move<true>();
        public_const_move<false>();
    }
}
