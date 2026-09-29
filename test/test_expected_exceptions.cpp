#include "expected_test_types.h"

#include <cbor_tags/detail/expected.h>
#include <doctest/doctest.h>
#include <ostream>
#include <utility>

namespace ex = cbor::tags::detail::expected_impl;
using namespace expected_test;

namespace {
template <bool ValueNothrowMove> void failed_transition_preserves_previous_state() {
    using result = ex::expected<tracked<ValueNothrowMove>, tracked<!ValueNothrowMove>>;
    counts values, errors;
    {
        result value(std::in_place, values, 7);
        result error(ex::unexpect, errors, 9);
        values.fail_copy = true;
        CHECK_THROWS_AS(error = value, failure);
        REQUIRE_FALSE(error.has_value());
        CHECK(error.error().number == 9);
        CHECK(values.live == 1);
        CHECK(errors.live == 1);
        values.fail_copy = false;
        errors.fail_copy = true;
        CHECK_THROWS_AS(value = error, failure);
        REQUIRE(value.has_value());
        CHECK(value->number == 7);
        CHECK(values.live == 1);
        CHECK(errors.live == 1);
        errors.fail_copy = false;
        error            = value;
        CHECK(error->number == 7);
        CHECK(values.live == 2);
        CHECK(errors.live == 0);
    }
    CHECK(values.live == 0);
    CHECK(errors.live == 0);
}

template <bool ValueNothrowMove> void failed_swap_preserves_both_alternatives() {
    using result = ex::expected<tracked<ValueNothrowMove>, tracked<!ValueNothrowMove>>;
    counts values, errors;
    {
        result value(std::in_place, values, 7);
        result error(ex::unexpect, errors, 9);
        if constexpr (ValueNothrowMove)
            errors.fail_move = true;
        else
            values.fail_move = true;
        CHECK_THROWS_AS(value.swap(error), failure);
        REQUIRE(value.has_value());
        REQUIRE_FALSE(error.has_value());
        CHECK(value->number == 7);
        CHECK(error.error().number == 9);
        CHECK(values.live == 1);
        CHECK(errors.live == 1);
        CHECK_THROWS_AS(error.swap(value), failure);
        CHECK(value->number == 7);
        CHECK(error.error().number == 9);
        values.fail_move = false;
        errors.fail_move = false;
        swap(value, error);
        CHECK(value.error().number == 9);
        CHECK(error->number == 7);
        CHECK(values.live == 1);
        CHECK(errors.live == 1);
    }
    CHECK(values.live == 0);
    CHECK(errors.live == 0);
}
} // namespace

TEST_SUITE("expected/contract") {
    TEST_CASE("failed construction during state changes preserves the old value or error") {
        failed_transition_preserves_previous_state<true>();
        failed_transition_preserves_previous_state<false>();
    }

    TEST_CASE("failed move assignment restores the opposite alternative") {
        counts values, errors;
        {
            using throwing_value = ex::expected<tracked<false>, tracked<true>>;
            throwing_value value(std::in_place, values, 3), error(ex::unexpect, errors, 7);
            values.fail_move = true;
            CHECK_THROWS_AS(error = std::move(value), failure);
            REQUIRE_FALSE(error.has_value());
            REQUIRE(value.has_value());
            CHECK(error.error().number == 7);
            CHECK(value->number == 3);
            CHECK(values.live == 1);
            CHECK(errors.live == 1);
        }
        CHECK(values.live == 0);
        CHECK(errors.live == 0);
        {
            using throwing_error = ex::expected<tracked<true>, tracked<false>>;
            throwing_error value(std::in_place, values, 4), error(ex::unexpect, errors, 8);
            errors.fail_move = true;
            CHECK_THROWS_AS(value = std::move(error), failure);
            REQUIRE(value.has_value());
            REQUIRE_FALSE(error.has_value());
            CHECK(value->number == 4);
            CHECK(error.error().number == 8);
            CHECK(values.live == 1);
            CHECK(errors.live == 1);
            errors.fail_move = false;
            value            = std::move(error);
            CHECK(value.error().number == 8);
            CHECK(error.error().number == -1);
        }
        CHECK(values.live == 0);
        CHECK(errors.live == 0);
    }

    TEST_CASE("failed cross-state swap restores either nonthrowing alternative") {
        failed_swap_preserves_both_alternatives<true>();
        failed_swap_preserves_both_alternatives<false>();
    }

    TEST_CASE("failed copies and same-state assignments preserve lifetimes") {
        counts values, errors;
        using result = ex::expected<tracked<true>, tracked<true>>;
        {
            result value(std::in_place, values, 1);
            result error(ex::unexpect, errors, 2);
            values.fail_copy = true;
            CHECK_THROWS_AS(static_cast<void>(result{value}), failure);
            CHECK(values.live == 1);
            errors.fail_copy = true;
            CHECK_THROWS_AS(static_cast<void>(result{error}), failure);
            CHECK(errors.live == 1);
            values.fail_copy = false;
            errors.fail_copy = false;
            result another_value(value);
            result another_error(error);
            values.fail_assign = true;
            errors.fail_assign = true;
            CHECK_THROWS_AS(value = another_value, failure);
            CHECK_THROWS_AS(error = another_error, failure);
            CHECK(value->number == 1);
            CHECK(error.error().number == 2);
            CHECK(values.live == 2);
            CHECK(errors.live == 2);
        }
        CHECK(values.live == 0);
        CHECK(errors.live == 0);
    }

    TEST_CASE("value and unexpected assignment preserve the original state on failure") {
        counts values, errors;
        {
            using result = ex::expected<tracked<false>, tracked<true>>;
            tracked<false> new_value(values, 12);
            result         target(ex::unexpect, errors, 13);
            values.fail_copy = true;
            CHECK_THROWS_AS(target = new_value, failure);
            CHECK(target.error().number == 13);
            CHECK(errors.live == 1);
            values.fail_copy = false;
            target           = new_value;
            ex::unexpected<tracked<true>> new_error(std::in_place, errors, 14);
            errors.fail_copy = true;
            CHECK_THROWS_AS(target = new_error, failure);
            CHECK(target->number == 12);
            CHECK(values.live == 2);
            CHECK(errors.live == 1);
        }
        CHECK(values.live == 0);
        CHECK(errors.live == 0);
    }

    TEST_CASE("void error construction and swap may throw without losing the success state") {
        counts errors;
        {
            using result = ex::expected<void, tracked<false>>;
            result success;
            result error(ex::unexpect, errors, 4);
            errors.fail_copy = true;
            CHECK_THROWS_AS(success = error, failure);
            CHECK(success.has_value());
            CHECK(error.error().number == 4);
            CHECK(errors.live == 1);
            errors.fail_move = true;
            CHECK_THROWS_AS(success.swap(error), failure);
            CHECK(success.has_value());
            CHECK(error.error().number == 4);
            CHECK(errors.live == 1);
            errors.fail_copy = false;
            errors.fail_move = false;
            success          = error;
            CHECK(success.error().number == 4);
            CHECK(errors.live == 2);
            error.emplace();
            CHECK(errors.live == 1);
            swap(success, error);
            CHECK(success.has_value());
            CHECK(error.error().number == 4);
        }
        CHECK(errors.live == 0);
    }

    TEST_CASE("same-state swaps use payload ADL swap and emplace destroys the previous object") {
        counts values, errors;
        {
            using result = ex::expected<tracked<true>, tracked<true>>;
            result first(std::in_place, values, 1);
            result second(std::in_place, values, 2);
            swap(first, second);
            CHECK(first->number == 2);
            CHECK(second->number == 1);
            result first_error(ex::unexpect, errors, 3);
            result second_error(ex::unexpect, errors, 4);
            swap(first_error, second_error);
            CHECK(first_error.error().number == 4);
            CHECK(second_error.error().number == 3);
            first_error.emplace(values, 5);
            first_error.emplace(values, 6);
            CHECK(first_error->number == 6);
            CHECK(values.live == 3);
            CHECK(errors.live == 1);
        }
        CHECK(values.live == 0);
        CHECK(errors.live == 0);
    }
}
