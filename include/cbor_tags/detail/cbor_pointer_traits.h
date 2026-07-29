#pragma once

#include "cbor_tags/detail/cbor_variant_traits.h"

#include <concepts>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace cbor::tags::detail {

template <typename T>
concept NullablePointerValue = !std::is_void_v<T> && !std::is_array_v<T> && !std::is_const_v<T>;

template <typename T> void                   match_standard_array_smart_pointer(const std::shared_ptr<T[]> *);
template <typename T, typename Deleter> void match_standard_array_smart_pointer(const std::unique_ptr<T[], Deleter> *);

template <typename Pointer>
concept StandardArraySmartPointer = requires(const std::remove_cvref_t<Pointer> *pointer) { match_standard_array_smart_pointer(pointer); };

template <typename Pointer>
concept NullablePointerCore =
    requires { typename std::remove_cvref_t<Pointer>::element_type; } &&
    NullablePointerValue<typename std::remove_cvref_t<Pointer>::element_type> && (!StandardArraySmartPointer<Pointer>) &&
    requires(std::remove_cvref_t<Pointer> &pointer, const std::remove_cvref_t<Pointer> &const_pointer,
             typename std::remove_cvref_t<Pointer>::element_type *raw) {
        { const_pointer.get() } -> std::same_as<typename std::remove_cvref_t<Pointer>::element_type *>;
        { *const_pointer } -> std::same_as<typename std::remove_cvref_t<Pointer>::element_type &>;
        { static_cast<bool>(const_pointer) } -> std::same_as<bool>;
        { pointer.reset() } -> std::same_as<void>;
        { pointer.reset(raw) } -> std::same_as<void>;
    };

template <typename Pointer>
concept IsSharedPointer = NullablePointerCore<Pointer> && std::copy_constructible<std::remove_cvref_t<Pointer>> &&
                          std::is_copy_assignable_v<std::remove_cvref_t<Pointer>>;

template <typename Pointer>
concept IsUniquePointer = NullablePointerCore<Pointer> && std::move_constructible<std::remove_cvref_t<Pointer>> &&
                          std::is_move_assignable_v<std::remove_cvref_t<Pointer>> && !std::copy_constructible<std::remove_cvref_t<Pointer>>;

template <typename Pointer>
concept IsNullablePointer = IsUniquePointer<Pointer> || IsSharedPointer<Pointer>;

template <typename Pointer> using nullable_pointer_element_t = typename std::remove_cvref_t<Pointer>::element_type;

template <typename T> consteval bool has_known_null_wire_impl() {
    using type = std::remove_cvref_t<T>;
    if constexpr (std::same_as<type, std::nullptr_t> || IsNullablePointer<type> || IsOptional<type>) {
        return true;
    } else if constexpr (IsVariant<type>) {
        return with_variant_alternatives<type>([]<typename... Ts>() { return (has_known_null_wire_impl<Ts>() || ...); });
    } else {
        return false;
    }
}

template <typename T> struct has_known_null_wire : std::bool_constant<has_known_null_wire_impl<T>()> {};

template <typename T> inline constexpr bool has_known_null_wire_v = has_known_null_wire_impl<T>();

template <typename T> inline constexpr bool is_supported_nullable_pointer_v = IsNullablePointer<T>;

} // namespace cbor::tags::detail
