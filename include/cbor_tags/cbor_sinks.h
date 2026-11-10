#pragma once

#include "cbor_tags/cbor.h"

#include <concepts>
#include <cstddef>
#include <functional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace cbor::tags {

namespace detail {

// A void callback takes everything, a status_code one stops the decode with anything but success
template <typename F, typename... Args>
concept SinkCallback = std::invocable<F &, Args...> && (std::is_void_v<std::invoke_result_t<F &, Args...>> ||
                                                        std::same_as<std::invoke_result_t<F &, Args...>, status_code>);

template <typename F, typename... Args> constexpr status_code invoke_sink(F &callback, Args &&...args) {
    if constexpr (std::is_void_v<std::invoke_result_t<F &, Args...>>) {
        std::invoke(callback, std::forward<Args>(args)...);
        return status_code::success;
    } else {
        return std::invoke(callback, std::forward<Args>(args)...);
    }
}

template <typename T>
concept SinkElement = std::same_as<T, std::remove_cvref_t<T>> && std::default_initializable<T> && std::move_constructible<T>;

} // namespace detail

// Decode targets that hand data to a callback instead of storing it. The
// decoders call deliver(); everything else goes through the as_*_sink functions.
template <typename F> class bstr_sink {
  public:
    constexpr explicit bstr_sink(F callback) : callback_(std::move(callback)) {}
    constexpr status_code deliver(std::span<const std::byte> piece) { return detail::invoke_sink(callback_, piece); }

  private:
    F callback_;
};

template <typename F> class tstr_sink {
  public:
    constexpr explicit tstr_sink(F callback) : callback_(std::move(callback)) {}
    status_code deliver(std::span<const std::byte> piece) {
        return detail::invoke_sink(callback_, std::string_view{reinterpret_cast<const char *>(piece.data()), piece.size()});
    }

  private:
    F callback_;
};

template <typename T, typename F> class array_sink {
  public:
    using value_type = T;

    constexpr explicit array_sink(F callback) : callback_(std::move(callback)) {}
    constexpr status_code deliver(T &&element) { return detail::invoke_sink(callback_, std::move(element)); }

  private:
    F callback_;
};

template <typename K, typename V, typename F> class map_sink {
  public:
    using key_type    = K;
    using mapped_type = V;

    constexpr explicit map_sink(F callback) : callback_(std::move(callback)) {}
    constexpr status_code deliver(K &&key, V &&value) { return detail::invoke_sink(callback_, std::move(key), std::move(value)); }

  private:
    F callback_;
};

namespace detail {

template <typename T> struct is_bstr_sink : std::false_type {};
template <typename F> struct is_bstr_sink<bstr_sink<F>> : std::true_type {};
template <typename T> struct is_tstr_sink : std::false_type {};
template <typename F> struct is_tstr_sink<tstr_sink<F>> : std::true_type {};
template <typename T> struct is_array_sink : std::false_type {};
template <typename T, typename F> struct is_array_sink<array_sink<T, F>> : std::true_type {};
template <typename T> struct is_map_sink : std::false_type {};
template <typename K, typename V, typename F> struct is_map_sink<map_sink<K, V, F>> : std::true_type {};

template <typename T>
concept IsBstrSink = is_bstr_sink<std::remove_cvref_t<T>>::value;
template <typename T>
concept IsTstrSink = is_tstr_sink<std::remove_cvref_t<T>>::value;
template <typename T>
concept IsStringSink = IsBstrSink<T> || IsTstrSink<T>;
template <typename T>
concept IsArraySink = is_array_sink<std::remove_cvref_t<T>>::value;
template <typename T>
concept IsMapSink = is_map_sink<std::remove_cvref_t<T>>::value;
template <typename T>
concept IsSink = IsStringSink<T> || IsArraySink<T> || IsMapSink<T>;

} // namespace detail

// Each piece is only valid during the call. Where pieces split depends on
// the input's buffers, so don't count on them matching CBOR chunks.
template <typename F>
    requires detail::SinkCallback<std::decay_t<F>, std::span<const std::byte>>
[[nodiscard]] constexpr auto as_bstr_sink(F &&callback) {
    return bstr_sink<std::decay_t<F>>{std::forward<F>(callback)};
}

// Same as as_bstr_sink, and a piece can end inside a UTF-8 sequence
template <typename F>
    requires detail::SinkCallback<std::decay_t<F>, std::string_view>
[[nodiscard]] constexpr auto as_tstr_sink(F &&callback) {
    return tstr_sink<std::decay_t<F>>{std::forward<F>(callback)};
}

// Decodes one element at a time and moves it into the callback
template <typename T, typename F>
    requires detail::SinkElement<T> && detail::SinkCallback<std::decay_t<F>, T &&>
[[nodiscard]] constexpr auto as_array_sink(F &&callback) {
    return array_sink<T, std::decay_t<F>>{std::forward<F>(callback)};
}

template <typename K, typename V, typename F>
    requires detail::SinkElement<K> && detail::SinkElement<V> && detail::SinkCallback<std::decay_t<F>, K &&, V &&>
[[nodiscard]] constexpr auto as_map_sink(F &&callback) {
    return map_sink<K, V, std::decay_t<F>>{std::forward<F>(callback)};
}

} // namespace cbor::tags
