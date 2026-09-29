#include "expected_test_types.h"

#include <array>
// This suite intentionally instantiates volatile values. C++23's T-returning
// value_or declarations trigger a Clang deprecation diagnostic in the template
// definition, even though the test uses only reference observers.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-volatile"
#endif
#include <cbor_tags/detail/expected.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#include <cbor_tags/expected.h>
#include <doctest/doctest.h>
#include <memory>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ex = cbor::tags::detail::expected_impl;
using namespace expected_test;

namespace {
struct trivial_copy_nontrivial_move {
    int number{};
    trivial_copy_nontrivial_move()                                     = default;
    trivial_copy_nontrivial_move(const trivial_copy_nontrivial_move &) = default;
    trivial_copy_nontrivial_move(trivial_copy_nontrivial_move &&other) : number(std::exchange(other.number, -1)) {}
};
struct trivial_move_only {
    int number{};
    trivial_move_only()                          = default;
    trivial_move_only(const trivial_move_only &) = delete;
    trivial_move_only(trivial_move_only &&)      = default;
};
using const_trivial = ex::expected<const trivial_copy_nontrivial_move, trivial_move_only>;
// Independent language probe: GCC currently misclassifies a defaulted union
// move that uses the const member's trivial copy constructor. The library must
// preserve triviality when the compiler supports it, and support moving either way.
union const_move_language_probe {
    const trivial_copy_nontrivial_move value;
    trivial_move_only                  error;
    const_move_language_probe(const_move_language_probe &&) = default;
};
static_assert(std::is_move_constructible_v<const_trivial>);
static_assert(std::is_trivially_move_constructible_v<const_trivial> == std::is_trivially_move_constructible_v<const_move_language_probe>);

struct assignment_counter {
    int assignments{};
    assignment_counter()                           = default;
    assignment_counter(const assignment_counter &) = default;
    assignment_counter(assignment_counter &&)      = default;
    assignment_counter &operator=(const assignment_counter &) {
        ++assignments;
        return *this;
    }
};

constexpr bool constant_evaluation() {
    ex::expected<int, int> value = 7;
    ex::expected<int, int> error(ex::unexpect, 9);
    value.swap(error);
    if (value.error() != 9 || *error != 7)
        return false;
    value = error;
    value = ex::unexpected(3);
    value.emplace(4);
    auto result    = value.and_then([](int x) { return ex::expected<int, int>(x + 1); }).transform([](int x) { return x * 2; });
    auto mapped    = ex::expected<int, int>(ex::unexpect, 2).transform_error([](int x) { return x + 3; });
    auto recovered = mapped.or_else([](int e) { return ex::expected<int, long>(e); });
    ex::expected<void, int> empty;
    ex::expected<void, int> failed(ex::unexpect, 8);
    empty.swap(failed);
    if (empty.error() != 8 || !failed)
        return false;
    empty.emplace();
    auto next = empty.and_then([] { return ex::expected<int, int>(11); });
    return *result == 10 && *recovered == 5 && *next == 11 && failed.error_or(12) == 12;
}
static_assert(constant_evaluation());
static_assert(std::is_trivially_copy_constructible_v<ex::expected<int, int>>);
static_assert(std::is_trivially_move_constructible_v<ex::expected<int, int>>);
static_assert(std::is_trivially_destructible_v<ex::expected<int, int>>);
static_assert(std::is_trivially_copy_constructible_v<ex::expected<void, int>>);
static_assert(std::is_trivially_move_constructible_v<ex::expected<void, int>>);
static_assert(std::is_trivially_destructible_v<ex::expected<void, int>>);
static_assert(!std::is_copy_constructible_v<ex::expected<immovable, int>>);
static_assert(!std::is_move_constructible_v<ex::expected<immovable, int>>);
static_assert(!std::is_copy_constructible_v<ex::expected<int, std::unique_ptr<int>>>);
static_assert(!std::is_default_constructible_v<ex::expected<immovable, int>>);
static_assert(!std::is_copy_assignable_v<ex::expected<tracked<false>, tracked<false>>>);
static_assert(!std::is_move_assignable_v<ex::expected<tracked<false>, tracked<false>>>);
static_assert(!std::is_convertible_v<int, ex::expected<explicit_int, int>>);
static_assert(std::is_constructible_v<ex::expected<explicit_int, int>, int>);
static_assert(std::is_same_v<ex::expected<int, long>::rebind<double>, ex::expected<double, long>>);
static_assert(std::is_same_v<decltype(*std::declval<ex::expected<int, int> &>()), int &>);
static_assert(std::is_same_v<decltype(*std::declval<const ex::expected<int, int> &>()), const int &>);
static_assert(std::is_same_v<decltype(*std::declval<ex::expected<int, int> &&>()), int &&>);
static_assert(std::is_same_v<decltype(*std::declval<const ex::expected<int, int> &&>()), const int &&>);
static_assert(std::is_same_v<decltype(std::declval<ex::expected<int, int> &>().error()), int &>);
static_assert(std::is_same_v<decltype(std::declval<const ex::expected<void, int> &&>().error()), const int &&>);
static_assert(std::is_same_v<decltype(std::declval<ex::expected<int, int> &&>().value()), int &&>);
static_assert(std::is_same_v<decltype(std::declval<const ex::expected<int, int> &&>().value()), const int &&>);
static_assert(!std::is_copy_assignable_v<ex::expected<const int, int>>);
static_assert(!std::is_move_assignable_v<ex::expected<const int, int>>);
static_assert(!std::is_swappable_v<ex::expected<const int, int>>);
static_assert(std::is_same_v<decltype(std::declval<ex::unexpected<int> &>().error()), int &>);
static_assert(std::is_same_v<decltype(std::declval<const ex::unexpected<int> &>().error()), const int &>);
static_assert(std::is_same_v<decltype(std::declval<ex::unexpected<int> &&>().error()), int &&>);
static_assert(std::is_same_v<decltype(std::declval<const ex::unexpected<int> &&>().error()), const int &&>);
static_assert(std::is_same_v<decltype(std::declval<ex::bad_expected_access<int> &>().error()), int &>);
static_assert(std::is_same_v<decltype(std::declval<const ex::bad_expected_access<int> &>().error()), const int &>);
static_assert(std::is_same_v<decltype(std::declval<ex::bad_expected_access<int> &&>().error()), int &&>);
static_assert(std::is_same_v<decltype(std::declval<const ex::bad_expected_access<int> &&>().error()), const int &&>);
static_assert(!std::is_convertible_v<ex::unexpected<int>, ex::expected<int, explicit_int>>);
static_assert(!std::is_convertible_v<ex::expected<int, int>, ex::expected<explicit_int, int>>);
static_assert(!std::is_convertible_v<ex::expected<void, int>, ex::expected<void, explicit_int>>);
static_assert(!std::is_constructible_v<ex::expected<int, int>, ex::expected<void, int>>);
static_assert(!std::is_constructible_v<ex::expected<void, int>, ex::expected<int, int>>);
static_assert(noexcept(std::declval<ex::expected<int, int> &>().swap(std::declval<ex::expected<int, int> &>())));
static_assert(!noexcept(std::declval<ex::expected<tracked<false>, int> &>().swap(std::declval<ex::expected<tracked<false>, int> &>())));
} // namespace

TEST_SUITE("expected/contract") {
    TEST_CASE("constructors distinguish value and error including identical types") {
        ex::expected<int, int> zero;
        ex::expected<int, int> value(42);
        ex::expected<int, int> error(ex::unexpect, 42);
        CHECK(zero.value() == 0);
        CHECK(value.has_value());
        CHECK_FALSE(error.has_value());
        CHECK(error.error() == 42);
        ex::unexpected deduced(9);
        static_assert(std::is_same_v<decltype(deduced), ex::unexpected<int>>);
        CHECK(deduced.error() == 9);
        ex::expected<int, int> copied_error(deduced);
        CHECK(copied_error.error() == 9);
        ex::expected<const int, int> constant(std::in_place, 8);
        CHECK(*constant == 8);
        ex::expected<const void, int> empty;
        empty.value();
        CHECK(empty.has_value());
    }

    TEST_CASE("qualified values preserve their public type when storage is reconstructed") {
        constexpr auto replaced = [] {
            ex::expected<const int, int> value(ex::unexpect, 1);
            value.emplace(1);
            value = ex::unexpected(3);
            value.emplace(2);
            ex::expected<const int, int> copied(value);
            return *copied;
        }();
        static_assert(replaced == 2);
        ex::expected<const std::string, int> original(std::in_place, "retained");
        ex::expected<const std::string, int> copied(original);
        ex::expected<const std::string, int> moved(std::move(original));
        CHECK(*copied == "retained");
        CHECK(*moved == "retained");
        CHECK(*original == "retained");
        CHECK(std::move(original).value_or("fallback") == "retained");
        CHECK(*original == "retained");
        ex::expected<volatile explicit_int, int> volatile_value(std::in_place, 3);
        static_assert(std::is_same_v<decltype(*volatile_value), volatile explicit_int &>);
        auto &volatile_reference = volatile_value.emplace(4);
        CHECK(volatile_reference.number == 4);
        const_trivial trivial(std::in_place);
        const_trivial moved_trivial(std::move(trivial));
        CHECK(moved_trivial->number == 0);
        CHECK(trivial->number == 0);
        const_trivial trivial_error(ex::unexpect);
        trivial_error.error().number = 9;
        const_trivial moved_trivial_error(std::move(trivial_error));
        CHECK(moved_trivial_error.error().number == 9);
        CHECK_FALSE(trivial_error.has_value());
    }

    TEST_CASE("self-copy assignment delegates to the active payload exactly once") {
        ex::expected<assignment_counter, assignment_counter> value;
        ex::expected<assignment_counter, assignment_counter> error(ex::unexpect);
        ex::expected<void, assignment_counter>               empty_error(ex::unexpect);
        value       = std::as_const(value);
        error       = std::as_const(error);
        empty_error = std::as_const(empty_error);
        CHECK(value->assignments == 1);
        CHECK(error.error().assignments == 1);
        CHECK(empty_error.error().assignments == 1);
    }

    TEST_CASE("void assignments and copies preserve the success or error alternative") {
        using result = ex::expected<void, std::string>;
        result success, error(ex::unexpect, "missing");
        result success_copy(success), error_copy(error);
        CHECK(success_copy.has_value());
        CHECK(error_copy.error() == "missing");
        result success_move(std::move(success_copy)), error_move(std::move(error_copy));
        CHECK(success_move.has_value());
        CHECK(error_move.error() == "missing");
        success = error;
        CHECK(success.error() == "missing");
        success = result{};
        CHECK(success.has_value());
        error = success;
        CHECK(error.has_value());
        error   = ex::unexpected(std::string("again"));
        success = std::move(error);
        CHECK(success.error() == "again");
        CHECK_FALSE(error.has_value());
        const ex::unexpected<std::string> copy_error("copied");
        error = copy_error;
        CHECK(error.error() == "copied");
        swap(success, error);
        CHECK(success.error() == "copied");
        CHECK(error.error() == "again");
        success.emplace();
        error.emplace();
        success.swap(error);
        CHECK(success == error);
        ex::expected<void, long> converted(ex::expected<void, int>(ex::unexpect, 9));
        CHECK(converted.error() == 9);
        ex::expected<void, std::unique_ptr<int>>       owned(ex::unexpect, std::make_unique<int>(10));
        ex::expected<void, std::unique_ptr<const int>> converted_owner(std::move(owned));
        CHECK(*converted_owner.error() == 10);
    }

    TEST_CASE("in-place construction supports argument packs and initializer lists") {
        ex::expected<std::string, std::vector<int>> text(std::in_place, 3, 'x');
        CHECK(*text == "xxx");
        ex::expected<std::vector<int>, std::string> sequence(std::in_place, {1, 2, 3});
        CHECK(*sequence == std::vector<int>{1, 2, 3});
        ex::expected<int, std::vector<int>> error(ex::unexpect, {4, 5});
        CHECK(error.error() == std::vector<int>{4, 5});
        ex::expected<void, std::vector<int>> empty_error(ex::unexpect, {6, 7});
        CHECK(empty_error.error() == std::vector<int>{6, 7});
        ex::unexpected<std::vector<int>> unexpected(std::in_place, {8, 9});
        CHECK(unexpected.error() == std::vector<int>{8, 9});
        ex::expected<list_sum, int> sum(ex::unexpect, 2);
        CHECK(sum.emplace({1, 2}, 4).number == 7);
        ex::expected<immovable, int> fixed(std::in_place, 10);
        CHECK(fixed.emplace(11).number == 11);
    }

    TEST_CASE("converting construction propagates the selected alternative") {
        ex::expected<short, short> source(12);
        ex::expected<long, long>   value(source);
        CHECK(*value == 12);
        source = ex::unexpected<short>(5);
        ex::expected<long, long> error(source);
        CHECK(error.error() == 5);
        ex::expected<explicit_int, explicit_int> explicit_error(source);
        CHECK(explicit_error.error().number == 5);
        ex::expected<void, short> empty_error(ex::unexpect, 6);
        ex::expected<void, long>  converted(empty_error);
        CHECK(converted.error() == 6);
        ex::expected<std::unique_ptr<int>, short>      owned(std::in_place, std::make_unique<int>(7));
        ex::expected<std::unique_ptr<const int>, long> moved(std::move(owned));
        CHECK(**moved == 7);
        CHECK(owned.has_value());
        CHECK(*owned == nullptr);
        ex::expected<short, std::unique_ptr<int>>      owned_error(ex::unexpect, std::make_unique<int>(8));
        ex::expected<long, std::unique_ptr<const int>> moved_error(std::move(owned_error));
        CHECK(*moved_error.error() == 8);
    }

    TEST_CASE("boolean conversion uses the contained value and nested expected preserves nesting") {
        ex::expected<int, int>   zero(0);
        ex::expected<bool, long> result(zero);
        REQUIRE(result.has_value());
        CHECK_FALSE(*result);
        ex::expected<bool, long> nonzero(ex::expected<int, int>(2));
        CHECK(*nonzero);
        ex::expected<bool, long> error(ex::expected<int, int>(ex::unexpect, 9));
        CHECK(error.error() == 9);
        ex::expected<ex::expected<int, int>, long> nested(ex::expected<int, int>(ex::unexpect, 10));
        REQUIRE(nested.has_value());
        CHECK(nested->error() == 10);
    }

    TEST_CASE("copy and move assignment cover both states without changing source state") {
        using result = ex::expected<std::string, int>;
        result left("a");
        result right("b");
        left = right;
        CHECK(*left == "b");
        right = ex::unexpected(7);
        left  = right;
        CHECK(left.error() == 7);
        right = "c";
        left  = right;
        CHECK(*left == "c");
        left  = ex::unexpected(8);
        right = ex::unexpected(9);
        left  = std::move(right);
        CHECK(left.error() == 9);
        CHECK_FALSE(right.has_value());
        right = "d";
        left  = std::move(right);
        CHECK(*left == "d");
        CHECK(right.has_value());
        left = std::as_const(left);
        CHECK(*left == "d");
        result error(ex::unexpect, 11);
        left = std::move(error);
        CHECK(left.error() == 11);
        CHECK_FALSE(error.has_value());
    }

    TEST_CASE("move-only errors and values can be propagated without copies") {
        ex::expected<std::unique_ptr<int>, std::unique_ptr<int>> result(std::in_place, std::make_unique<int>(3));
        auto                                                     extracted = std::move(result).value_or(std::make_unique<int>(4));
        CHECK(*extracted == 3);
        result          = ex::unexpected(std::make_unique<int>(5));
        auto propagated = std::move(result).and_then([](std::unique_ptr<int>) { return ex::expected<int, std::unique_ptr<int>>(6); });
        CHECK(*propagated.error() == 5);
        auto error = std::move(propagated).error_or(std::make_unique<int>(7));
        CHECK(*error == 5);
        ex::expected<void, std::unique_ptr<int>> empty_error(ex::unexpect, std::make_unique<int>(8));
        auto                                     mapped = std::move(empty_error).transform([] { return 9; });
        CHECK(*mapped.error() == 8);
    }

    TEST_CASE("observers and comparisons do not inspect the inactive alternative") {
        ex::expected<std::string, int>  value("yes");
        ex::expected<std::string, long> same("yes");
        ex::expected<std::string, int>  error(ex::unexpect, 7);
        CHECK(value == same);
        CHECK(value == "yes");
        CHECK("yes" == value);
        CHECK(error == ex::unexpected(7L));
        CHECK(ex::unexpected(7L) == error);
        CHECK(value != error);
        CHECK(value->size() == 3);
        CHECK(value.value_or("no") == "yes");
        CHECK(error.value_or("no") == "no");
        CHECK(value.error_or(8) == 8);
        CHECK(error.error_or(8) == 7);
        ex::expected<void, int>        empty;
        ex::expected<const void, long> other;
        CHECK(empty == other);
        CHECK(empty != ex::unexpected(1));
        ex::unexpected<int> first(1), second(2);
        swap(first, second);
        CHECK(first.error() == 2);
        CHECK(second == ex::unexpected(1L));
    }

    TEST_CASE("bad value access carries the error for object and void results") {
        using result = ex::expected<int, std::string>;
        result error(ex::unexpect, "missing");
        CHECK_THROWS_AS(error.value(), ex::bad_expected_access<std::string>);
        CHECK_THROWS_AS(std::as_const(error).value(), ex::bad_expected_access<void>);
        try {
            static_cast<void>(std::move(error).value());
            FAIL("value must throw");
        } catch (const ex::bad_expected_access<std::string> &failure) {
            CHECK(failure.error() == "missing");
            CHECK(std::string(failure.what()).size() > 0);
        }
        const result constant(ex::unexpect, "constant");
        CHECK_THROWS_AS(std::move(constant).value(), ex::bad_expected_access<std::string>);
        ex::expected<void, std::string> empty_error(ex::unexpect, "empty");
        try {
            empty_error.value();
            FAIL("void value must throw");
        } catch (const ex::bad_expected_access<std::string> &failure) { CHECK(failure.error() == "empty"); }
        CHECK_THROWS_AS(std::move(empty_error).value(), ex::bad_expected_access<std::string>);
        ex::bad_expected_access<std::string> access("before");
        access.error() = "after";
        CHECK(std::move(access).error() == "after");
    }
}
