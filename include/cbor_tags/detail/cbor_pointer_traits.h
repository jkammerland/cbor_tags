#pragma once

#include <memory>
#include <type_traits>
#include <vector>

namespace cbor::tags::detail {

template <typename T> struct is_std_unique_ptr : std::false_type {};
template <typename T, typename Deleter> struct is_std_unique_ptr<std::unique_ptr<T, Deleter>> : std::true_type {
    using element_type = T;
    using deleter_type = Deleter;
};

template <typename T> struct is_std_shared_ptr : std::false_type {};
template <typename T> struct is_std_shared_ptr<std::shared_ptr<T>> : std::true_type {
    using element_type = T;
};

template <typename T> struct is_std_vector_of_shared_ptr : std::false_type {};
template <typename T, typename Allocator> struct is_std_vector_of_shared_ptr<std::vector<std::shared_ptr<T>, Allocator>> : std::true_type {
    using element_type   = T;
    using allocator_type = Allocator;
};

template <typename T>
concept NullablePointerValue = !std::is_void_v<T> && !std::is_array_v<T> && !std::is_const_v<T>;

template <typename T>
concept IsNullablePointer = is_std_unique_ptr<std::remove_cvref_t<T>>::value || is_std_shared_ptr<std::remove_cvref_t<T>>::value;

template <typename T> struct nullable_pointer_element;
template <typename T, typename Deleter> struct nullable_pointer_element<std::unique_ptr<T, Deleter>> {
    using type = T;
};
template <typename T> struct nullable_pointer_element<std::shared_ptr<T>> {
    using type = T;
};

template <typename T> using nullable_pointer_element_t = typename nullable_pointer_element<std::remove_cvref_t<T>>::type;

template <typename T> struct is_supported_nullable_pointer_owner : std::false_type {};
template <typename T, typename Deleter>
struct is_supported_nullable_pointer_owner<std::unique_ptr<T, Deleter>>
    : std::bool_constant<std::is_same_v<Deleter, std::default_delete<T>>> {};
template <typename T> struct is_supported_nullable_pointer_owner<std::shared_ptr<T>> : std::true_type {};

template <typename T>
constexpr bool is_supported_nullable_pointer_v =
    NullablePointerValue<nullable_pointer_element_t<T>> && is_supported_nullable_pointer_owner<std::remove_cvref_t<T>>::value;

} // namespace cbor::tags::detail
