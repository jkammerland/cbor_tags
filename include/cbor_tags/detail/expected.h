#pragma once

// C++20 implementation of the C++23 expected interface (P0323 and P2505).
// Storage never allocates and always contains either a value or an error.
#include <cassert>
#include <exception>
#include <functional>
#include <initializer_list>
#include <memory>
#include <type_traits>
#include <utility>

namespace cbor::tags::detail::expected_impl {

template <typename T, typename E> class expected;
template <typename E> class unexpected;
template <typename E> class bad_expected_access;

struct unexpect_t {
    explicit constexpr unexpect_t() = default;
};
inline constexpr unexpect_t unexpect{};

namespace impl {
template <typename> inline constexpr bool               is_expected                  = false;
template <typename T, typename E> inline constexpr bool is_expected<expected<T, E>>  = true;
template <typename> inline constexpr bool               is_unexpected                = false;
template <typename E> inline constexpr bool             is_unexpected<unexpected<E>> = true;
template <typename E>
inline constexpr bool valid_error =
    std::is_object_v<E> && !std::is_array_v<E> && std::is_same_v<E, std::remove_cv_t<E>> && !is_unexpected<E>;
template <typename T>
inline constexpr bool valid_value =
    std::is_void_v<T> || (std::is_object_v<T> && !std::is_array_v<T> && !is_unexpected<std::remove_cv_t<T>> &&
                          !std::is_same_v<std::remove_cv_t<T>, std::in_place_t> && !std::is_same_v<std::remove_cv_t<T>, unexpect_t>);

template <typename To, typename From>
inline constexpr bool constructs_from_expected = std::is_constructible_v<To, From &> || std::is_constructible_v<To, const From &> ||
                                                 std::is_constructible_v<To, From &&> || std::is_constructible_v<To, const From &&>;
template <typename To, typename From>
inline constexpr bool converts_from_expected =
    constructs_from_expected<To, From> || std::is_convertible_v<From &, To> || std::is_convertible_v<const From &, To> ||
    std::is_convertible_v<From &&, To> || std::is_convertible_v<const From &&, To>;

struct empty_t {};
struct invoke_value_t {};
struct invoke_error_t {};

// LWG 3891 permits unqualified storage, while public references retain T's cv.
// For const T, a defaulted move must copy the value. Keeping that choice in a
// member wrapper preserves trivial construction even when U's move is nontrivial.
template <typename T> struct value_storage {
    std::remove_cv_t<T> object;
    template <typename... Args>
    constexpr explicit value_storage(std::in_place_t,
                                     Args &&...args) noexcept(std::is_nothrow_constructible_v<std::remove_cv_t<T>, Args...>)
        : object(std::forward<Args>(args)...) {}
    template <typename F, typename... Args>
    constexpr explicit value_storage(invoke_value_t, F &&f, Args &&...args)
        : object(std::invoke(std::forward<F>(f), std::forward<Args>(args)...)) {}
};

template <typename T>
    requires std::is_const_v<T>
struct value_storage<T> : value_storage<std::remove_const_t<T>> {
    using base = value_storage<std::remove_const_t<T>>;
    using base::base;
    value_storage(const value_storage &) = default;
};

template <typename T, typename E> union storage {
    value_storage<T> value;
    E                error;

    constexpr explicit storage(empty_t) noexcept {}
    template <typename... Args>
    constexpr explicit storage(std::in_place_t, Args &&...args) : value(std::in_place, std::forward<Args>(args)...) {}
    template <typename... Args> constexpr explicit storage(unexpect_t, Args &&...args) : error(std::forward<Args>(args)...) {}
    template <typename F, typename... Args>
    constexpr explicit storage(invoke_value_t, F &&f, Args &&...args)
        : value(invoke_value_t{}, std::forward<F>(f), std::forward<Args>(args)...) {}
    template <typename F, typename... Args>
    constexpr explicit storage(invoke_error_t, F &&f, Args &&...args)
        : error(std::invoke(std::forward<F>(f), std::forward<Args>(args)...)) {}
    storage(const storage &)            = default;
    storage(storage &&)                 = default;
    storage &operator=(const storage &) = default;
    storage &operator=(storage &&)      = default;
    ~storage()
        requires(std::is_trivially_destructible_v<T> && std::is_trivially_destructible_v<E>)
    = default;
    constexpr ~storage() {}
};

// Restore the old union member if construction of its replacement throws.
// Its move construction is known not to throw. This also works without exceptions.
template <typename T> struct restore_on_failure {
    T *destination;
    T &saved;
    constexpr ~restore_on_failure() {
        if (destination)
            std::construct_at(destination, std::move(saved));
    }
    constexpr void release() noexcept { destination = nullptr; }
};

template <typename New, typename Old, typename... Args> constexpr void replace(New &target, Old &previous, Args &&...args) {
    if constexpr (std::is_nothrow_constructible_v<New, Args...>) {
        std::destroy_at(std::addressof(previous));
        std::construct_at(std::addressof(target), std::forward<Args>(args)...);
    } else if constexpr (std::is_nothrow_move_constructible_v<New>) {
        New next(std::forward<Args>(args)...);
        std::destroy_at(std::addressof(previous));
        std::construct_at(std::addressof(target), std::move(next));
    } else {
        static_assert(std::is_nothrow_move_constructible_v<Old>);
        Old saved(std::move(previous));
        std::destroy_at(std::addressof(previous));
        restore_on_failure<Old> restore{std::addressof(previous), saved};
        std::construct_at(std::addressof(target), std::forward<Args>(args)...);
        restore.release();
    }
}

template <typename E, typename U> [[noreturn]] inline void bad_access(U &&error) {
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
    throw bad_expected_access<E>(std::forward<U>(error));
#else
    static_cast<void>(error);
    std::terminate();
#endif
}

struct operations {
    template <typename Self, typename F> static constexpr auto and_then(Self &&self, F &&f) {
        using S = std::remove_cvref_t<Self>;
        if constexpr (std::is_void_v<typename S::value_type>) {
            using R = std::remove_cvref_t<std::invoke_result_t<F>>;
            static_assert(is_expected<R>, "and_then must return an expected");
            static_assert(std::is_same_v<typename R::error_type, typename S::error_type>, "and_then must preserve the error type");
            if (self.has_value())
                return R(std::invoke(std::forward<F>(f)));
            return R(unexpect, std::forward<Self>(self).error());
        } else {
            using R = std::remove_cvref_t<std::invoke_result_t<F, decltype(*std::forward<Self>(self))>>;
            static_assert(is_expected<R>, "and_then must return an expected");
            static_assert(std::is_same_v<typename R::error_type, typename S::error_type>, "and_then must preserve the error type");
            if (self.has_value())
                return R(std::invoke(std::forward<F>(f), *std::forward<Self>(self)));
            return R(unexpect, std::forward<Self>(self).error());
        }
    }
    template <typename Self, typename F> static constexpr auto or_else(Self &&self, F &&f) {
        using T = typename std::remove_cvref_t<Self>::value_type;
        using R = std::remove_cvref_t<std::invoke_result_t<F, decltype(std::forward<Self>(self).error())>>;
        static_assert(is_expected<R>, "or_else must return an expected");
        static_assert(std::is_same_v<typename R::value_type, T>, "or_else must preserve the value type");
        if (!self.has_value())
            return R(std::invoke(std::forward<F>(f), std::forward<Self>(self).error()));
        if constexpr (std::is_void_v<T>)
            return R();
        else
            return R(std::in_place, *std::forward<Self>(self));
    }
    template <typename Self, typename F> static constexpr auto transform(Self &&self, F &&f) {
        using S = std::remove_cvref_t<Self>;
        using E = typename S::error_type;
        if constexpr (std::is_void_v<typename S::value_type>) {
            using U = std::remove_cv_t<std::invoke_result_t<F>>;
            static_assert(valid_value<U>, "transform must produce a valid expected value type");
            if (!self.has_value())
                return expected<U, E>(unexpect, std::forward<Self>(self).error());
            if constexpr (std::is_void_v<U>) {
                std::invoke(std::forward<F>(f));
                return expected<U, E>();
            } else
                return expected<U, E>(invoke_value_t{}, std::forward<F>(f));
        } else {
            using U = std::remove_cv_t<std::invoke_result_t<F, decltype(*std::forward<Self>(self))>>;
            static_assert(valid_value<U>, "transform must produce a valid expected value type");
            if (!self.has_value())
                return expected<U, E>(unexpect, std::forward<Self>(self).error());
            if constexpr (std::is_void_v<U>) {
                std::invoke(std::forward<F>(f), *std::forward<Self>(self));
                return expected<U, E>();
            } else
                return expected<U, E>(invoke_value_t{}, std::forward<F>(f), *std::forward<Self>(self));
        }
    }
    template <typename Self, typename F> static constexpr auto transform_error(Self &&self, F &&f) {
        using T = typename std::remove_cvref_t<Self>::value_type;
        using G = std::remove_cv_t<std::invoke_result_t<F, decltype(std::forward<Self>(self).error())>>;
        static_assert(valid_error<G>, "transform_error must produce a valid error type");
        if (!self.has_value())
            return expected<T, G>(invoke_error_t{}, std::forward<F>(f), std::forward<Self>(self).error());
        if constexpr (std::is_void_v<T>)
            return expected<T, G>();
        else
            return expected<T, G>(std::in_place, *std::forward<Self>(self));
    }
};
} // namespace impl

template <typename E> class unexpected {
    static_assert(impl::valid_error<E>, "unexpected requires an unqualified non-array object error type, excluding unexpected");
    E error_;

  public:
    constexpr unexpected(const unexpected &)            = default;
    constexpr unexpected(unexpected &&)                 = default;
    constexpr unexpected &operator=(const unexpected &) = default;
    constexpr unexpected &operator=(unexpected &&)      = default;
    template <typename G = E>
        requires(!std::is_same_v<std::remove_cvref_t<G>, unexpected> && !std::is_same_v<std::remove_cvref_t<G>, std::in_place_t> &&
                 std::is_constructible_v<E, G>)
    constexpr explicit unexpected(G &&error) noexcept(std::is_nothrow_constructible_v<E, G>) : error_(std::forward<G>(error)) {}
    template <typename... Args>
        requires std::is_constructible_v<E, Args...>
    constexpr explicit unexpected(std::in_place_t, Args &&...args) : error_(std::forward<Args>(args)...) {}
    template <typename U, typename... Args>
        requires std::is_constructible_v<E, std::initializer_list<U> &, Args...>
    constexpr explicit unexpected(std::in_place_t, std::initializer_list<U> list, Args &&...args)
        : error_(list, std::forward<Args>(args)...) {}
    constexpr E        &error()        &noexcept { return error_; }
    constexpr const E  &error() const  &noexcept { return error_; }
    constexpr E       &&error()       &&noexcept { return std::move(error_); }
    constexpr const E &&error() const && noexcept { return std::move(error_); }
    constexpr void      swap(unexpected &other) noexcept(std::is_nothrow_swappable_v<E>)
        requires std::is_swappable_v<E>
    {
        using std::swap;
        swap(error_, other.error_);
    }
    friend constexpr void swap(unexpected &left, unexpected &right) noexcept(noexcept(left.swap(right)))
        requires std::is_swappable_v<E>
    {
        left.swap(right);
    }
    template <typename G> friend constexpr bool operator==(const unexpected &left, const unexpected<G> &right) {
        return left.error_ == right.error();
    }
};
template <typename E> unexpected(E) -> unexpected<E>;

template <> class bad_expected_access<void> : public std::exception {
  protected:
    bad_expected_access() noexcept                                       = default;
    bad_expected_access(const bad_expected_access &) noexcept            = default;
    bad_expected_access(bad_expected_access &&) noexcept                 = default;
    bad_expected_access &operator=(const bad_expected_access &) noexcept = default;
    bad_expected_access &operator=(bad_expected_access &&) noexcept      = default;
    ~bad_expected_access() override                                      = default;

  public:
    const char *what() const noexcept override { return "bad access to expected"; }
};
template <typename E> class bad_expected_access : public bad_expected_access<void> {
    E error_;

  public:
    explicit bad_expected_access(E error) : error_(std::move(error)) {}
    E        &error()        &noexcept { return error_; }
    const E  &error() const  &noexcept { return error_; }
    E       &&error()       &&noexcept { return std::move(error_); }
    const E &&error() const && noexcept { return std::move(error_); }
};

// The two expected specializations below deliberately keep their standard
// interfaces separate: success for expected<void, E> has no value object.
template <typename T, typename E> class expected {
    static_assert(impl::valid_value<T> && !std::is_void_v<T>,
                  "expected requires a non-array object or void value type, excluding construction tags and unexpected");
    static_assert(impl::valid_error<E>, "expected requires a valid unexpected error type");
    // Some compilers cannot generate a trivial union move when a const value
    // falls back to copying. Use the ordinary constructor path in that case.
    static constexpr bool trivial_move = std::is_trivially_move_constructible_v<T> && std::is_trivially_move_constructible_v<E> &&
                                         std::is_trivially_move_constructible_v<impl::storage<T, E>>;
    impl::storage<T, E>   data_;
    bool                  has_;

    friend struct impl::operations;
    template <typename F, typename... Args>
    constexpr expected(impl::invoke_value_t tag, F &&f, Args &&...args)
        : data_(tag, std::forward<F>(f), std::forward<Args>(args)...), has_(true) {}
    template <typename F, typename... Args>
    constexpr expected(impl::invoke_error_t tag, F &&f, Args &&...args)
        : data_(tag, std::forward<F>(f), std::forward<Args>(args)...), has_(false) {}

    template <typename U> constexpr void assign_value(U &&value) {
        if (has_)
            data_.value.object = std::forward<U>(value);
        else {
            impl::replace(data_.value, data_.error, std::in_place, std::forward<U>(value));
            has_ = true;
        }
    }
    template <typename G> constexpr void assign_error(G &&error) {
        if (!has_)
            data_.error = std::forward<G>(error);
        else {
            impl::replace(data_.error, data_.value, std::forward<G>(error));
            has_ = false;
        }
    }
    constexpr void destroy() noexcept {
        if (has_)
            std::destroy_at(std::addressof(data_.value));
        else
            std::destroy_at(std::addressof(data_.error));
    }

  public:
    using value_type                   = T;
    using error_type                   = E;
    using unexpected_type              = unexpected<E>;
    template <typename U> using rebind = expected<U, E>;

    constexpr expected() noexcept(std::is_nothrow_default_constructible_v<T>)
        requires std::is_default_constructible_v<T>
        : data_(std::in_place), has_(true) {}

    expected(const expected &) = delete;
    expected(const expected &)
        requires(std::is_trivially_copy_constructible_v<T> && std::is_trivially_copy_constructible_v<E>)
    = default;
    constexpr expected(const expected &other) noexcept(std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_copy_constructible_v<E>)
        requires(std::is_copy_constructible_v<T> && std::is_copy_constructible_v<E> &&
                 !(std::is_trivially_copy_constructible_v<T> && std::is_trivially_copy_constructible_v<E>))
        : data_(impl::empty_t{}), has_(other.has_) {
        if (has_)
            std::construct_at(std::addressof(data_.value), std::in_place, *other);
        else
            std::construct_at(std::addressof(data_.error), other.data_.error);
    }
    expected(expected &&)
        requires trivial_move
    = default;
    constexpr expected(expected &&other) noexcept(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_constructible_v<E>)
        requires(std::is_move_constructible_v<T> && std::is_move_constructible_v<E> && !trivial_move)
        : data_(impl::empty_t{}), has_(other.has_) {
        if (has_)
            std::construct_at(std::addressof(data_.value), std::in_place, *std::move(other));
        else
            std::construct_at(std::addressof(data_.error), std::move(other.data_.error));
    }

    template <typename U, typename G>
        requires(!std::is_void_v<U> && std::is_constructible_v<T, const U &> && std::is_constructible_v<E, const G &> &&
                 (std::is_same_v<std::remove_cv_t<T>, bool> || !impl::converts_from_expected<T, expected<U, G>>) &&
                 !impl::constructs_from_expected<unexpected<E>, expected<U, G>>)
    constexpr explicit(!std::is_convertible_v<const U &, T> || !std::is_convertible_v<const G &, E>) expected(const expected<U, G> &other)
        : data_(impl::empty_t{}), has_(other.has_value()) {
        if (has_)
            std::construct_at(std::addressof(data_.value), std::in_place, *other);
        else
            std::construct_at(std::addressof(data_.error), other.error());
    }
    template <typename U, typename G>
        requires(!std::is_void_v<U> && std::is_constructible_v<T, U> && std::is_constructible_v<E, G> &&
                 (std::is_same_v<std::remove_cv_t<T>, bool> || !impl::converts_from_expected<T, expected<U, G>>) &&
                 !impl::constructs_from_expected<unexpected<E>, expected<U, G>>)
    constexpr explicit(!std::is_convertible_v<U, T> || !std::is_convertible_v<G, E>) expected(expected<U, G> &&other)
        : data_(impl::empty_t{}), has_(other.has_value()) {
        if (has_)
            std::construct_at(std::addressof(data_.value), std::in_place, *std::move(other));
        else
            std::construct_at(std::addressof(data_.error), std::move(other).error());
    }
    template <typename U = std::remove_cv_t<T>>
        requires(!std::is_same_v<std::remove_cvref_t<U>, expected> && !std::is_same_v<std::remove_cvref_t<U>, std::in_place_t> &&
                 !impl::is_unexpected<std::remove_cvref_t<U>> && std::is_constructible_v<T, U> &&
                 (!std::is_same_v<std::remove_cv_t<T>, bool> || !impl::is_expected<std::remove_cvref_t<U>>))
    constexpr explicit(!std::is_convertible_v<U, T>) expected(U &&value) noexcept(std::is_nothrow_constructible_v<T, U>)
        : data_(std::in_place, std::forward<U>(value)), has_(true) {}

    template <typename G>
        requires std::is_constructible_v<E, const G &>
    constexpr explicit(!std::is_convertible_v<const G &, E>)
        expected(const unexpected<G> &error) noexcept(std::is_nothrow_constructible_v<E, const G &>)
        : data_(unexpect, error.error()), has_(false) {}
    template <typename G>
        requires std::is_constructible_v<E, G>
    constexpr explicit(!std::is_convertible_v<G, E>) expected(unexpected<G> &&error) noexcept(std::is_nothrow_constructible_v<E, G>)
        : data_(unexpect, std::move(error).error()), has_(false) {}
    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit expected(std::in_place_t, Args &&...args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
        : data_(std::in_place, std::forward<Args>(args)...), has_(true) {}
    template <typename U, typename... Args>
        requires std::is_constructible_v<T, std::initializer_list<U> &, Args...>
    constexpr explicit expected(std::in_place_t, std::initializer_list<U> list, Args &&...args)
        : data_(std::in_place, list, std::forward<Args>(args)...), has_(true) {}
    template <typename... Args>
        requires std::is_constructible_v<E, Args...>
    constexpr explicit expected(unexpect_t, Args &&...args) noexcept(std::is_nothrow_constructible_v<E, Args...>)
        : data_(unexpect, std::forward<Args>(args)...), has_(false) {}
    template <typename U, typename... Args>
        requires std::is_constructible_v<E, std::initializer_list<U> &, Args...>
    constexpr explicit expected(unexpect_t, std::initializer_list<U> list, Args &&...args)
        : data_(unexpect, list, std::forward<Args>(args)...), has_(false) {}

    ~expected()
        requires(std::is_trivially_destructible_v<T> && std::is_trivially_destructible_v<E>)
    = default;
    constexpr ~expected() { destroy(); }

    expected &operator=(const expected &) = delete;
    // Self-assignment delegates to the active payload, as required by C++23.
    constexpr expected &
    // NOLINTNEXTLINE(cert-oop54-cpp)
    operator=(const expected &other) noexcept(std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_copy_assignable_v<T> &&
                                              std::is_nothrow_copy_constructible_v<E> && std::is_nothrow_copy_assignable_v<E>)
        requires(std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T> && std::is_copy_constructible_v<E> &&
                 std::is_copy_assignable_v<E> && (std::is_nothrow_move_constructible_v<T> || std::is_nothrow_move_constructible_v<E>))
    {
        if (other.has_)
            assign_value(*other);
        else
            assign_error(other.data_.error);
        return *this;
    }
    constexpr expected &
    operator=(expected &&other) noexcept(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T> &&
                                         std::is_nothrow_move_constructible_v<E> && std::is_nothrow_move_assignable_v<E>)
        requires(std::is_move_constructible_v<T> && std::is_move_assignable_v<T> && std::is_move_constructible_v<E> &&
                 std::is_move_assignable_v<E> && (std::is_nothrow_move_constructible_v<T> || std::is_nothrow_move_constructible_v<E>))
    {
        if (other.has_)
            assign_value(*std::move(other));
        else
            assign_error(std::move(other.data_.error));
        return *this;
    }
    template <typename U = T>
        requires(!std::is_same_v<std::remove_cvref_t<U>, expected> && !impl::is_unexpected<std::remove_cvref_t<U>> &&
                 std::is_constructible_v<T, U> && std::is_assignable_v<T &, U> &&
                 (std::is_nothrow_constructible_v<T, U> || std::is_nothrow_move_constructible_v<T> ||
                  std::is_nothrow_move_constructible_v<E>))
    constexpr expected &operator=(U &&value) {
        assign_value(std::forward<U>(value));
        return *this;
    }
    template <typename G>
        requires(std::is_constructible_v<E, const G &> && std::is_assignable_v<E &, const G &> &&
                 (std::is_nothrow_constructible_v<E, const G &> || std::is_nothrow_move_constructible_v<T> ||
                  std::is_nothrow_move_constructible_v<E>))
    constexpr expected &operator=(const unexpected<G> &error) {
        assign_error(error.error());
        return *this;
    }
    template <typename G>
        requires(std::is_constructible_v<E, G> && std::is_assignable_v<E &, G> &&
                 (std::is_nothrow_constructible_v<E, G> || std::is_nothrow_move_constructible_v<T> ||
                  std::is_nothrow_move_constructible_v<E>))
    constexpr expected &operator=(unexpected<G> &&error) {
        assign_error(std::move(error).error());
        return *this;
    }

    template <typename... Args>
        requires std::is_nothrow_constructible_v<T, Args...>
    constexpr T &emplace(Args &&...args) noexcept {
        destroy();
        auto *value = std::construct_at(std::addressof(data_.value), std::in_place, std::forward<Args>(args)...);
        has_        = true;
        return value->object;
    }
    template <typename U, typename... Args>
        requires std::is_nothrow_constructible_v<T, std::initializer_list<U> &, Args...>
    constexpr T &emplace(std::initializer_list<U> list, Args &&...args) noexcept {
        destroy();
        auto *value = std::construct_at(std::addressof(data_.value), std::in_place, list, std::forward<Args>(args)...);
        has_        = true;
        return value->object;
    }

    constexpr void swap(expected &other) noexcept(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_swappable_v<T> &&
                                                  std::is_nothrow_move_constructible_v<E> && std::is_nothrow_swappable_v<E>)
        requires(std::is_swappable_v<T> && std::is_swappable_v<E> && std::is_move_constructible_v<T> && std::is_move_constructible_v<E> &&
                 (std::is_nothrow_move_constructible_v<T> || std::is_nothrow_move_constructible_v<E>))
    {
        using std::swap;
        if (has_ && other.has_)
            swap(data_.value.object, other.data_.value.object);
        else if (!has_ && !other.has_)
            swap(data_.error, other.data_.error);
        else if (!has_)
            other.swap(*this);
        else {
            // Move the nonthrowing alternative aside before replacing the other.
            if constexpr (std::is_nothrow_move_constructible_v<E>) {
                E saved(std::move(other.data_.error));
                std::destroy_at(std::addressof(other.data_.error));
                impl::restore_on_failure<E> restore{std::addressof(other.data_.error), saved};
                std::construct_at(std::addressof(other.data_.value), std::move(data_.value));
                restore.release();
                std::destroy_at(std::addressof(data_.value));
                std::construct_at(std::addressof(data_.error), std::move(saved));
            } else {
                impl::value_storage<T> saved(std::move(data_.value));
                std::destroy_at(std::addressof(data_.value));
                impl::restore_on_failure<impl::value_storage<T>> restore{std::addressof(data_.value), saved};
                std::construct_at(std::addressof(data_.error), std::move(other.data_.error));
                restore.release();
                std::destroy_at(std::addressof(other.data_.error));
                std::construct_at(std::addressof(other.data_.value), std::move(saved));
            }
            has_       = false;
            other.has_ = true;
        }
    }
    friend constexpr void swap(expected &left, expected &right) noexcept(noexcept(left.swap(right)))
        requires requires { left.swap(right); }
    {
        left.swap(right);
    }

    constexpr const T *operator->() const noexcept {
        assert(has_);
        return std::addressof(data_.value.object);
    }
    constexpr T *operator->() noexcept {
        assert(has_);
        return std::addressof(data_.value.object);
    }
    constexpr const T &operator*() const & noexcept {
        assert(has_);
        return data_.value.object;
    }
    constexpr T &operator*() & noexcept {
        assert(has_);
        return data_.value.object;
    }
    constexpr const T &&operator*() const && noexcept {
        assert(has_);
        return std::move(data_.value.object);
    }
    constexpr T &&operator*() && noexcept {
        assert(has_);
        return std::move(data_.value.object);
    }
    constexpr explicit operator bool() const noexcept { return has_; }
    constexpr bool     has_value() const noexcept { return has_; }
    constexpr const T &value() const & {
        static_assert(std::is_copy_constructible_v<E>, "value requires a copy-constructible error type");
        if (!has_)
            impl::bad_access<E>(data_.error);
        return data_.value.object;
    }
    constexpr T &value() & {
        static_assert(std::is_copy_constructible_v<E>, "value requires a copy-constructible error type");
        if (!has_)
            impl::bad_access<E>(std::as_const(data_.error));
        return data_.value.object;
    }
    constexpr const T &&value() const && {
        static_assert(std::is_copy_constructible_v<E> && std::is_constructible_v<E, const E &&>,
                      "value requires a copy-constructible error type and construction from the selected error reference");
        if (!has_)
            impl::bad_access<E>(std::move(data_.error));
        return std::move(data_.value.object);
    }
    constexpr T &&value() && {
        static_assert(std::is_copy_constructible_v<E> && std::is_move_constructible_v<E>,
                      "value requires a copy-constructible error type and construction from the selected error reference");
        if (!has_)
            impl::bad_access<E>(std::move(data_.error));
        return std::move(data_.value.object);
    }
    constexpr const E &error() const & noexcept {
        assert(!has_);
        return data_.error;
    }
    constexpr E &error() & noexcept {
        assert(!has_);
        return data_.error;
    }
    constexpr const E &&error() const && noexcept {
        assert(!has_);
        return std::move(data_.error);
    }
    constexpr E &&error() && noexcept {
        assert(!has_);
        return std::move(data_.error);
    }
    template <typename U = T> constexpr T value_or(U &&fallback) const & {
        static_assert(std::is_copy_constructible_v<T> && std::is_convertible_v<U, T>);
        return has_ ? **this : static_cast<T>(std::forward<U>(fallback));
    }
    template <typename U = T> constexpr T value_or(U &&fallback) && {
        static_assert(std::is_move_constructible_v<T> && std::is_convertible_v<U, T>);
        return has_ ? *std::move(*this) : static_cast<T>(std::forward<U>(fallback));
    }
    template <typename G = E> constexpr E error_or(G &&fallback) const & {
        static_assert(std::is_copy_constructible_v<E> && std::is_convertible_v<G, E>);
        return has_ ? static_cast<E>(std::forward<G>(fallback)) : data_.error;
    }
    template <typename G = E> constexpr E error_or(G &&fallback) && {
        static_assert(std::is_move_constructible_v<E> && std::is_convertible_v<G, E>);
        return has_ ? static_cast<E>(std::forward<G>(fallback)) : std::move(data_.error);
    }

    template <typename F>
    constexpr auto and_then(F &&f) &
        requires std::is_constructible_v<E, E &>
    {
        return impl::operations::and_then(*this, std::forward<F>(f));
    }
    template <typename F>
    constexpr auto and_then(F &&f) const &
        requires std::is_constructible_v<E, const E &>
    {
        return impl::operations::and_then(*this, std::forward<F>(f));
    }
    template <typename F>
    constexpr auto and_then(F &&f) &&
        requires std::is_constructible_v<E, E &&>
    {
        return impl::operations::and_then(std::move(*this), std::forward<F>(f));
    }
    template <typename F>
    constexpr auto and_then(F &&f) const &&
        requires std::is_constructible_v<E, const E &&>
    {
        return impl::operations::and_then(std::move(*this), std::forward<F>(f));
    }
    template <typename F>
    constexpr auto or_else(F &&f) &
        requires std::is_constructible_v<T, T &>
    {
        return impl::operations::or_else(*this, std::forward<F>(f));
    }
    template <typename F>
    constexpr auto or_else(F &&f) const &
        requires std::is_constructible_v<T, const T &>
    {
        return impl::operations::or_else(*this, std::forward<F>(f));
    }
    template <typename F>
    constexpr auto or_else(F &&f) &&
        requires std::is_constructible_v<T, T &&>
    {
        return impl::operations::or_else(std::move(*this), std::forward<F>(f));
    }
    template <typename F>
    constexpr auto or_else(F &&f) const &&
        requires std::is_constructible_v<T, const T &&>
    {
        return impl::operations::or_else(std::move(*this), std::forward<F>(f));
    }
    template <typename F>
    constexpr auto transform(F &&f) &
        requires std::is_constructible_v<E, E &>
    {
        return impl::operations::transform(*this, std::forward<F>(f));
    }
    template <typename F>
    constexpr auto transform(F &&f) const &
        requires std::is_constructible_v<E, const E &>
    {
        return impl::operations::transform(*this, std::forward<F>(f));
    }
    template <typename F>
    constexpr auto transform(F &&f) &&
        requires std::is_constructible_v<E, E &&>
    {
        return impl::operations::transform(std::move(*this), std::forward<F>(f));
    }
    template <typename F>
    constexpr auto transform(F &&f) const &&
        requires std::is_constructible_v<E, const E &&>
    {
        return impl::operations::transform(std::move(*this), std::forward<F>(f));
    }
    template <typename F>
    constexpr auto transform_error(F &&f) &
        requires std::is_constructible_v<T, T &>
    {
        return impl::operations::transform_error(*this, std::forward<F>(f));
    }
    template <typename F>
    constexpr auto transform_error(F &&f) const &
        requires std::is_constructible_v<T, const T &>
    {
        return impl::operations::transform_error(*this, std::forward<F>(f));
    }
    template <typename F>
    constexpr auto transform_error(F &&f) &&
        requires std::is_constructible_v<T, T &&>
    {
        return impl::operations::transform_error(std::move(*this), std::forward<F>(f));
    }
    template <typename F>
    constexpr auto transform_error(F &&f) const &&
        requires std::is_constructible_v<T, const T &&>
    {
        return impl::operations::transform_error(std::move(*this), std::forward<F>(f));
    }

    template <typename U, typename G>
        requires(!std::is_void_v<U>)
    friend constexpr bool operator==(const expected &left, const expected<U, G> &right) {
        return left.has_ == right.has_value() && (left.has_ ? *left == *right : left.data_.error == right.error());
    }
    template <typename U>
        requires(!impl::is_expected<U> && !impl::is_unexpected<U>)
    friend constexpr bool operator==(const expected &left, const U &right) {
        return left.has_ && *left == right;
    }
    template <typename G> friend constexpr bool operator==(const expected &left, const unexpected<G> &right) {
        return !left.has_ && left.data_.error == right.error();
    }
};

template <typename T, typename E>
    requires std::is_void_v<T>
class expected<T, E> {
    static_assert(impl::valid_error<E>, "expected requires a valid unexpected error type");
    impl::storage<impl::empty_t, E> data_;
    bool                            has_;
    friend struct impl::operations;
    template <typename F, typename... Args>
    constexpr expected(impl::invoke_error_t tag, F &&f, Args &&...args)
        : data_(tag, std::forward<F>(f), std::forward<Args>(args)...), has_(false) {}
    template <typename G> constexpr void assign_error(G &&error) {
        if (!has_)
            data_.error = std::forward<G>(error);
        else {
            std::construct_at(std::addressof(data_.error), std::forward<G>(error));
            has_ = false;
        }
    }

  public:
    using value_type                   = T;
    using error_type                   = E;
    using unexpected_type              = unexpected<E>;
    template <typename U> using rebind = expected<U, E>;

    constexpr expected() noexcept : data_(std::in_place), has_(true) {}
    constexpr explicit expected(std::in_place_t) noexcept : expected() {}
    expected(const expected &) = delete;
    expected(const expected &)
        requires std::is_trivially_copy_constructible_v<E>
    = default;
    constexpr expected(const expected &other) noexcept(std::is_nothrow_copy_constructible_v<E>)
        requires(std::is_copy_constructible_v<E> && !std::is_trivially_copy_constructible_v<E>)
        : data_(std::in_place), has_(other.has_) {
        if (!has_)
            std::construct_at(std::addressof(data_.error), other.data_.error);
    }
    expected(expected &&)
        requires std::is_trivially_move_constructible_v<E>
    = default;
    constexpr expected(expected &&other) noexcept(std::is_nothrow_move_constructible_v<E>)
        requires(std::is_move_constructible_v<E> && !std::is_trivially_move_constructible_v<E>)
        : data_(std::in_place), has_(other.has_) {
        if (!has_)
            std::construct_at(std::addressof(data_.error), std::move(other.data_.error));
    }
    template <typename U, typename G>
        requires(std::is_void_v<U> && std::is_constructible_v<E, const G &> &&
                 !impl::constructs_from_expected<unexpected<E>, expected<U, G>>)
    constexpr explicit(!std::is_convertible_v<const G &, E>) expected(const expected<U, G> &other)
        : data_(std::in_place), has_(other.has_value()) {
        if (!has_)
            std::construct_at(std::addressof(data_.error), other.error());
    }
    template <typename U, typename G>
        requires(std::is_void_v<U> && std::is_constructible_v<E, G> && !impl::constructs_from_expected<unexpected<E>, expected<U, G>>)
    constexpr explicit(!std::is_convertible_v<G, E>) expected(expected<U, G> &&other) : data_(std::in_place), has_(other.has_value()) {
        if (!has_)
            std::construct_at(std::addressof(data_.error), std::move(other).error());
    }
    template <typename G>
        requires std::is_constructible_v<E, const G &>
    constexpr explicit(!std::is_convertible_v<const G &, E>)
        expected(const unexpected<G> &error) noexcept(std::is_nothrow_constructible_v<E, const G &>)
        : data_(unexpect, error.error()), has_(false) {}
    template <typename G>
        requires std::is_constructible_v<E, G>
    constexpr explicit(!std::is_convertible_v<G, E>) expected(unexpected<G> &&error) noexcept(std::is_nothrow_constructible_v<E, G>)
        : data_(unexpect, std::move(error).error()), has_(false) {}
    template <typename... Args>
        requires std::is_constructible_v<E, Args...>
    constexpr explicit expected(unexpect_t, Args &&...args) noexcept(std::is_nothrow_constructible_v<E, Args...>)
        : data_(unexpect, std::forward<Args>(args)...), has_(false) {}
    template <typename U, typename... Args>
        requires std::is_constructible_v<E, std::initializer_list<U> &, Args...>
    constexpr explicit expected(unexpect_t, std::initializer_list<U> list, Args &&...args)
        : data_(unexpect, list, std::forward<Args>(args)...), has_(false) {}

    ~expected()
        requires std::is_trivially_destructible_v<E>
    = default;
    constexpr ~expected() {
        if (!has_)
            std::destroy_at(std::addressof(data_.error));
    }
    expected &operator=(const expected &) = delete;
    // Self-assignment delegates to the active error; it never replaces storage.
    // NOLINTNEXTLINE(cert-oop54-cpp)
    constexpr expected &operator=(const expected &other) noexcept(std::is_nothrow_copy_constructible_v<E> &&
                                                                  std::is_nothrow_copy_assignable_v<E>)
        requires(std::is_copy_constructible_v<E> && std::is_copy_assignable_v<E>)
    {
        if (other.has_)
            emplace();
        else
            assign_error(other.data_.error);
        return *this;
    }
    constexpr expected &operator=(expected &&other) noexcept(std::is_nothrow_move_constructible_v<E> &&
                                                             std::is_nothrow_move_assignable_v<E>)
        requires(std::is_move_constructible_v<E> && std::is_move_assignable_v<E>)
    {
        if (other.has_)
            emplace();
        else
            assign_error(std::move(other.data_.error));
        return *this;
    }
    template <typename G>
        requires(std::is_constructible_v<E, const G &> && std::is_assignable_v<E &, const G &>)
    constexpr expected &operator=(const unexpected<G> &error) {
        assign_error(error.error());
        return *this;
    }
    template <typename G>
        requires(std::is_constructible_v<E, G> && std::is_assignable_v<E &, G>)
    constexpr expected &operator=(unexpected<G> &&error) {
        assign_error(std::move(error).error());
        return *this;
    }
    constexpr void emplace() noexcept {
        if (!has_) {
            std::destroy_at(std::addressof(data_.error));
            std::construct_at(std::addressof(data_.value), std::in_place);
            has_ = true;
        }
    }
    constexpr void swap(expected &other) noexcept(std::is_nothrow_move_constructible_v<E> && std::is_nothrow_swappable_v<E>)
        requires(std::is_swappable_v<E> && std::is_move_constructible_v<E>)
    {
        if (!has_ && !other.has_) {
            using std::swap;
            swap(data_.error, other.data_.error);
        } else if (has_ && !other.has_) {
            std::construct_at(std::addressof(data_.error), std::move(other.data_.error));
            has_ = false;
            other.emplace();
        } else if (!has_ && other.has_)
            other.swap(*this);
    }
    friend constexpr void swap(expected &left, expected &right) noexcept(noexcept(left.swap(right)))
        requires requires { left.swap(right); }
    {
        left.swap(right);
    }

    constexpr explicit operator bool() const noexcept { return has_; }
    constexpr bool     has_value() const noexcept { return has_; }
    constexpr void     operator*() const noexcept { assert(has_); }
    constexpr void     value() const     &{
        static_assert(std::is_copy_constructible_v<E>, "value requires a copy-constructible error type");
        if (!has_)
            impl::bad_access<E>(data_.error);
    }
    constexpr void value() && {
        static_assert(std::is_copy_constructible_v<E> && std::is_move_constructible_v<E>,
                      "value requires a copy-constructible error type and construction from the selected error reference");
        if (!has_)
            impl::bad_access<E>(std::move(data_.error));
    }
    constexpr const E &error() const & noexcept {
        assert(!has_);
        return data_.error;
    }
    constexpr E &error() & noexcept {
        assert(!has_);
        return data_.error;
    }
    constexpr const E &&error() const && noexcept {
        assert(!has_);
        return std::move(data_.error);
    }
    constexpr E &&error() && noexcept {
        assert(!has_);
        return std::move(data_.error);
    }
    template <typename G = E> constexpr E error_or(G &&fallback) const & {
        static_assert(std::is_copy_constructible_v<E> && std::is_convertible_v<G, E>);
        return has_ ? static_cast<E>(std::forward<G>(fallback)) : data_.error;
    }
    template <typename G = E> constexpr E error_or(G &&fallback) && {
        static_assert(std::is_move_constructible_v<E> && std::is_convertible_v<G, E>);
        return has_ ? static_cast<E>(std::forward<G>(fallback)) : std::move(data_.error);
    }

    template <typename F>
    constexpr auto and_then(F &&f) &
        requires std::is_constructible_v<E, E &>
    {
        return impl::operations::and_then(*this, std::forward<F>(f));
    }
    template <typename F>
    constexpr auto and_then(F &&f) const &
        requires std::is_constructible_v<E, const E &>
    {
        return impl::operations::and_then(*this, std::forward<F>(f));
    }
    template <typename F>
    constexpr auto and_then(F &&f) &&
        requires std::is_constructible_v<E, E &&>
    {
        return impl::operations::and_then(std::move(*this), std::forward<F>(f));
    }
    template <typename F>
    constexpr auto and_then(F &&f) const &&
        requires std::is_constructible_v<E, const E &&>
    {
        return impl::operations::and_then(std::move(*this), std::forward<F>(f));
    }
    template <typename F> constexpr auto or_else(F &&f) & { return impl::operations::or_else(*this, std::forward<F>(f)); }
    template <typename F> constexpr auto or_else(F &&f) const & { return impl::operations::or_else(*this, std::forward<F>(f)); }
    template <typename F> constexpr auto or_else(F &&f) && { return impl::operations::or_else(std::move(*this), std::forward<F>(f)); }
    template <typename F> constexpr auto or_else(F &&f) const && { return impl::operations::or_else(std::move(*this), std::forward<F>(f)); }
    template <typename F>
    constexpr auto transform(F &&f) &
        requires std::is_constructible_v<E, E &>
    {
        return impl::operations::transform(*this, std::forward<F>(f));
    }
    template <typename F>
    constexpr auto transform(F &&f) const &
        requires std::is_constructible_v<E, const E &>
    {
        return impl::operations::transform(*this, std::forward<F>(f));
    }
    template <typename F>
    constexpr auto transform(F &&f) &&
        requires std::is_constructible_v<E, E &&>
    {
        return impl::operations::transform(std::move(*this), std::forward<F>(f));
    }
    template <typename F>
    constexpr auto transform(F &&f) const &&
        requires std::is_constructible_v<E, const E &&>
    {
        return impl::operations::transform(std::move(*this), std::forward<F>(f));
    }
    template <typename F> constexpr auto transform_error(F &&f) & { return impl::operations::transform_error(*this, std::forward<F>(f)); }
    template <typename F> constexpr auto transform_error(F &&f) const & {
        return impl::operations::transform_error(*this, std::forward<F>(f));
    }
    template <typename F> constexpr auto transform_error(F &&f) && {
        return impl::operations::transform_error(std::move(*this), std::forward<F>(f));
    }
    template <typename F> constexpr auto transform_error(F &&f) const && {
        return impl::operations::transform_error(std::move(*this), std::forward<F>(f));
    }

    template <typename U, typename G>
        requires std::is_void_v<U>
    friend constexpr bool operator==(const expected &left, const expected<U, G> &right) {
        return left.has_ == right.has_value() && (left.has_ || left.data_.error == right.error());
    }
    template <typename G> friend constexpr bool operator==(const expected &left, const unexpected<G> &right) {
        return !left.has_ && left.data_.error == right.error();
    }
};

} // namespace cbor::tags::detail::expected_impl
