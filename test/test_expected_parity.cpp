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

#if __cpp_lib_expected >= 202211L
// Apply the same four reference categories to the expected and its callback.
template <std::size_t Category, typename T> constexpr decltype(auto) with_category(T &value) {
    static_assert(Category < 4);
    if constexpr (Category == 0)
        return (value);
    else if constexpr (Category == 1)
        return std::as_const(value);
    else if constexpr (Category == 2)
        return std::move(value);
    else
        return std::move(std::as_const(value));
}

struct invocation {
    int            calls{};
    int            callable_category{};
    int            argument_category{};
    int            argument{};
    constexpr bool operator==(const invocation &) const = default;
};

template <typename Family, typename Value, typename Error> struct binding_callback {
    using result = typename Family::template result<Value, Error>;
    invocation *observed;
    bool        succeed;

    template <typename... Args> constexpr result invoke(int category, Args &&...args) const {
        static_assert(sizeof...(Args) <= 1);
        ++observed->calls;
        observed->callable_category = category;
        if constexpr (sizeof...(Args) == 1) {
            observed->argument_category = (qualifier{}(std::forward<Args>(args)), ...);
            observed->argument          = (static_cast<int>(args), ...);
        }
        if (!succeed)
            return result(Family::unexpect, 23);
        if constexpr (std::is_void_v<Value>)
            return result();
        else
            return result(std::in_place, 42);
    }
    template <typename... Args> constexpr result operator()(Args &&...args) & { return invoke(1, std::forward<Args>(args)...); }
    template <typename... Args> constexpr result operator()(Args &&...args) const & { return invoke(2, std::forward<Args>(args)...); }
    template <typename... Args> constexpr result operator()(Args &&...args) && { return invoke(3, std::forward<Args>(args)...); }
    template <typename... Args> constexpr result operator()(Args &&...args) const && { return invoke(4, std::forward<Args>(args)...); }
};

struct binding_observation {
    invocation     called;
    bool           has_value{};
    int            payload{};
    bool           source_has_value{};
    constexpr bool operator==(const binding_observation &) const = default;
};

template <typename Family, typename Value> constexpr auto binding_source(bool has_value) {
    using result = typename Family::template result<Value, int>;
    if (!has_value)
        return result(Family::unexpect, 9);
    if constexpr (std::is_void_v<Value>)
        return result();
    else
        return result(std::in_place, 7);
}

template <typename Result> constexpr int binding_payload(const Result &result) {
    if (!result.has_value())
        return static_cast<int>(result.error());
    if constexpr (std::is_void_v<typename Result::value_type>)
        return 0;
    else
        return static_cast<int>(*result);
}

template <typename Family, typename Value, typename Next, std::size_t SourceCategory, std::size_t CallbackCategory>
constexpr binding_observation observe_and_then(bool has_value, bool succeed) {
    auto                                source = binding_source<Family, Value>(has_value);
    binding_observation                 observed;
    binding_callback<Family, Next, int> callback{&observed.called, succeed};
    auto                                result = with_category<SourceCategory>(source).and_then(with_category<CallbackCategory>(callback));
    static_assert(std::is_same_v<decltype(result), typename Family::template result<Next, int>>);
    observed.has_value        = result.has_value();
    observed.payload          = binding_payload(result);
    observed.source_has_value = source.has_value();
    return observed;
}

template <typename Family, typename Value, std::size_t SourceCategory, std::size_t CallbackCategory>
constexpr binding_observation observe_or_else(bool has_value, bool succeed) {
    auto                                  source = binding_source<Family, Value>(has_value);
    binding_observation                   observed;
    binding_callback<Family, Value, long> callback{&observed.called, succeed};
    auto                                  result = with_category<SourceCategory>(source).or_else(with_category<CallbackCategory>(callback));
    static_assert(std::is_same_v<decltype(result), typename Family::template result<Value, long>>);
    observed.has_value        = result.has_value();
    observed.payload          = binding_payload(result);
    observed.source_has_value = source.has_value();
    return observed;
}

// Const-rvalue inputs and callbacks must also work during constant evaluation.
constexpr binding_observation failed_continuation{{1, 4, 4, 7}, false, 23, true};
constexpr binding_observation recovered_void{{1, 4, 4, 9}, true, 0, false};
static_assert(observe_and_then<builtin, int, long, 3, 3>(true, false) == failed_continuation);
static_assert(observe_and_then<native, int, long, 3, 3>(true, false) == failed_continuation);
static_assert(observe_or_else<builtin, void, 3, 3>(false, true) == recovered_void);
static_assert(observe_or_else<native, void, 3, 3>(false, true) == recovered_void);

template <typename Value, typename Next, std::size_t SourceCategory, std::size_t CallbackCategory> void check_and_then() {
    CAPTURE(SourceCategory);
    CAPTURE(CallbackCategory);
    CAPTURE(std::is_void_v<Value>);
    CAPTURE(std::is_void_v<Next>);
    for (bool has_value : {false, true}) {
        for (bool succeed : {false, true}) {
            CAPTURE(has_value);
            CAPTURE(succeed);
            const auto ours             = observe_and_then<builtin, Value, Next, SourceCategory, CallbackCategory>(has_value, succeed);
            const auto theirs           = observe_and_then<native, Value, Next, SourceCategory, CallbackCategory>(has_value, succeed);
            const int  success_payload  = std::is_void_v<Next> ? 0 : 42;
            const int  expected_payload = has_value ? (succeed ? success_payload : 23) : 9;
            CHECK(ours == theirs);
            CHECK(ours.has_value == (has_value && succeed));
            CHECK(ours.payload == expected_payload);
            CHECK(ours.source_has_value == has_value);
            CHECK(ours.called.calls == (has_value ? 1 : 0));
            CHECK(ours.called.callable_category == (has_value ? CallbackCategory + 1 : 0));
            CHECK(ours.called.argument_category == (has_value && !std::is_void_v<Value> ? SourceCategory + 1 : 0));
            CHECK(ours.called.argument == (has_value && !std::is_void_v<Value> ? 7 : 0));
        }
    }
}

template <typename Value, std::size_t SourceCategory, std::size_t CallbackCategory> void check_or_else() {
    CAPTURE(SourceCategory);
    CAPTURE(CallbackCategory);
    CAPTURE(std::is_void_v<Value>);
    for (bool has_value : {false, true}) {
        for (bool succeed : {false, true}) {
            CAPTURE(has_value);
            CAPTURE(succeed);
            const auto ours              = observe_or_else<builtin, Value, SourceCategory, CallbackCategory>(has_value, succeed);
            const auto theirs            = observe_or_else<native, Value, SourceCategory, CallbackCategory>(has_value, succeed);
            const int  preserved_payload = std::is_void_v<Value> ? 0 : 7;
            const int  recovered_payload = std::is_void_v<Value> ? 0 : 42;
            const int  expected_payload  = has_value ? preserved_payload : (succeed ? recovered_payload : 23);
            CHECK(ours == theirs);
            CHECK(ours.has_value == (has_value || succeed));
            CHECK(ours.payload == expected_payload);
            CHECK(ours.source_has_value == has_value);
            CHECK(ours.called.calls == (has_value ? 0 : 1));
            CHECK(ours.called.callable_category == (has_value ? 0 : CallbackCategory + 1));
            CHECK(ours.called.argument_category == (has_value ? 0 : SourceCategory + 1));
            CHECK(ours.called.argument == (has_value ? 0 : 9));
        }
    }
}

template <std::size_t SourceCategory, std::size_t... CallbackCategories>
void check_binding_callbacks(std::index_sequence<CallbackCategories...>) {
    (check_and_then<int, long, SourceCategory, CallbackCategories>(), ...);
    (check_and_then<int, void, SourceCategory, CallbackCategories>(), ...);
    (check_and_then<void, long, SourceCategory, CallbackCategories>(), ...);
    (check_and_then<void, void, SourceCategory, CallbackCategories>(), ...);
    (check_or_else<int, SourceCategory, CallbackCategories>(), ...);
    (check_or_else<void, SourceCategory, CallbackCategories>(), ...);
}
template <std::size_t... SourceCategories> void check_binding_sources(std::index_sequence<SourceCategories...>) {
    (check_binding_callbacks<SourceCategories>(std::make_index_sequence<4>{}), ...);
}

template <typename Family> std::array<int, 10> move_only_bindings() {
    using source_type    = typename Family::template result<std::unique_ptr<int>, std::unique_ptr<int>>;
    using next_type      = typename Family::template result<long, std::unique_ptr<int>>;
    using recovered_type = typename Family::template result<std::unique_ptr<int>, long>;
    source_type value(std::in_place, std::make_unique<int>(7)), error(Family::unexpect, std::make_unique<int>(9));
    source_type preserved(std::in_place, std::make_unique<int>(10)), failed(Family::unexpect, std::make_unique<int>(11));
    int         continued{}, recovered{};
    auto        next = [&](std::unique_ptr<int> input) -> next_type {
        ++continued;
        return *input + 1L;
    };
    auto recover = [&](std::unique_ptr<int> input) -> recovered_type {
        ++recovered;
        return std::make_unique<int>(*input + 1);
    };
    auto first  = std::move(value).and_then(next);
    auto second = std::move(error).and_then(next);
    auto third  = std::move(preserved).or_else(recover);
    auto fourth = std::move(failed).or_else(recover);
    REQUIRE(first.has_value());
    REQUIRE_FALSE(second.has_value());
    REQUIRE(third.has_value());
    REQUIRE(fourth.has_value());
    REQUIRE(second.error());
    REQUIRE(*third);
    REQUIRE(*fourth);
    REQUIRE(value.has_value());
    REQUIRE_FALSE(error.has_value());
    REQUIRE(preserved.has_value());
    REQUIRE_FALSE(failed.has_value());
    return {static_cast<int>(*first),
            *second.error(),
            **third,
            **fourth,
            continued,
            recovered,
            *value == nullptr,
            error.error() == nullptr,
            *preserved == nullptr,
            failed.error() == nullptr};
}

template <typename Family, typename Value> void throwing_bindings() {
    using next_type      = typename Family::template result<long, int>;
    using recovered_type = typename Family::template result<Value, long>;
    auto value           = binding_source<Family, Value>(true);
    auto error           = binding_source<Family, Value>(false);
    auto next            = [](auto &&...) -> next_type { throw failure{}; };
    auto recover         = [](int) -> recovered_type { throw failure{}; };
    CHECK_THROWS_AS(static_cast<void>(value.and_then(next)), failure);
    CHECK_THROWS_AS(static_cast<void>(error.or_else(recover)), failure);
    REQUIRE(value.has_value());
    REQUIRE_FALSE(error.has_value());
    CHECK(binding_payload(value) == (std::is_void_v<Value> ? 0 : 7));
    CHECK(error.error() == 9);
    // A throwing callback is harmless when its alternative is inactive.
    CHECK(error.and_then(next).error() == 9);
    CHECK(binding_payload(value.or_else(recover)) == (std::is_void_v<Value> ? 0 : 7));
}
#endif
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
    TEST_CASE("and_then and or_else match native results and reference forwarding") {
        check_binding_sources(std::make_index_sequence<4>{});
    }

    TEST_CASE("and_then and or_else transfer move-only payload ownership") {
        const auto ours   = move_only_bindings<builtin>();
        const auto theirs = move_only_bindings<native>();
        CHECK(ours == std::array{8, 9, 10, 12, 1, 1, 1, 1, 1, 1});
        CHECK(ours == theirs);
    }

    TEST_CASE("and_then and or_else propagate callback exceptions in both backends") {
        throwing_bindings<builtin, int>();
        throwing_bindings<native, int>();
        throwing_bindings<builtin, void>();
        throwing_bindings<native, void>();
    }

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
