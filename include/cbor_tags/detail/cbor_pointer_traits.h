#pragma once

#include "cbor_tags/detail/cbor_variant_traits.h"

#include <concepts>
#include <cstddef>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace cbor::tags::detail {

template <typename T>
concept SmartPointerElement = !std::is_void_v<T> && !std::is_array_v<T> && !std::is_const_v<T>;

// The standard permits some shared_ptr array observer declarations to differ
// between implementations. Reject standard array owners by type instead of
// inferring scalar ownership from those observers.
template <typename Pointer> struct is_unsupported_standard_array_smart_pointer : std::false_type {};

template <typename T> struct is_unsupported_standard_array_smart_pointer<std::shared_ptr<T[]>> : std::true_type {};

template <typename T, std::size_t Size> struct is_unsupported_standard_array_smart_pointer<std::shared_ptr<T[Size]>> : std::true_type {};

template <typename T, typename Deleter>
struct is_unsupported_standard_array_smart_pointer<std::unique_ptr<T[], Deleter>> : std::true_type {};

template <typename Pointer>
concept UnsupportedStandardArraySmartPointer = is_unsupported_standard_array_smart_pointer<std::remove_cvref_t<Pointer>>::value;

template <typename Pointer>
concept SmartPointerCore =
    requires { typename std::remove_cvref_t<Pointer>::element_type; } &&
    SmartPointerElement<typename std::remove_cvref_t<Pointer>::element_type> && (!UnsupportedStandardArraySmartPointer<Pointer>) &&
    requires(std::remove_cvref_t<Pointer> &pointer, const std::remove_cvref_t<Pointer> &const_pointer,
             typename std::remove_cvref_t<Pointer>::element_type *raw) {
        { const_pointer.get() } -> std::same_as<typename std::remove_cvref_t<Pointer>::element_type *>;
        { *const_pointer } -> std::same_as<typename std::remove_cvref_t<Pointer>::element_type &>;
        { static_cast<bool>(const_pointer) } -> std::same_as<bool>;
        { pointer.reset() } -> std::same_as<void>;
        { pointer.reset(raw) } -> std::same_as<void>;
    };

template <typename Pointer>
concept HasSmartPointerCopyOperations = requires(std::remove_cvref_t<Pointer> &destination, const std::remove_cvref_t<Pointer> &source) {
    { std::remove_cvref_t<Pointer>{source} } -> std::same_as<std::remove_cvref_t<Pointer>>;
    { destination = source } -> std::same_as<std::remove_cvref_t<Pointer> &>;
};

template <typename Pointer>
concept HasSmartPointerMoveOperations = requires(std::remove_cvref_t<Pointer> &destination, std::remove_cvref_t<Pointer> &&source) {
    { std::remove_cvref_t<Pointer>{std::move(source)} } -> std::same_as<std::remove_cvref_t<Pointer>>;
    { destination = std::move(source) } -> std::same_as<std::remove_cvref_t<Pointer> &>;
};

template <typename Pointer>
concept HasSmartPointerCopyConstruction = requires(const std::remove_cvref_t<Pointer> &source) {
    { std::remove_cvref_t<Pointer>{source} } -> std::same_as<std::remove_cvref_t<Pointer>>;
};

template <typename Pointer>
concept IsSharedPointer = SmartPointerCore<Pointer> && HasSmartPointerCopyOperations<Pointer>;

template <typename Pointer>
concept IsUniquePointer =
    SmartPointerCore<Pointer> && HasSmartPointerMoveOperations<Pointer> && (!HasSmartPointerCopyConstruction<Pointer>);

template <typename Pointer>
concept IsSmartPointer = IsUniquePointer<Pointer> || IsSharedPointer<Pointer>;

template <typename Pointer> using smart_pointer_element_t = typename std::remove_cvref_t<Pointer>::element_type;

template <typename T> consteval bool has_known_null_wire_impl() {
    using type = std::remove_cvref_t<T>;
    if constexpr (std::same_as<type, std::nullptr_t> || IsSmartPointer<type> || IsOptional<type>) {
        return true;
    } else if constexpr (IsVariant<type>) {
        return with_variant_alternatives<type>([]<typename... Ts>() { return (has_known_null_wire_impl<Ts>() || ...); });
    } else {
        return false;
    }
}

template <typename T> struct has_known_null_wire : std::bool_constant<has_known_null_wire_impl<T>()> {};

template <typename T> inline constexpr bool has_known_null_wire_v = has_known_null_wire_impl<T>();

template <typename T> inline constexpr bool is_supported_smart_pointer_v = IsSmartPointer<T>;

} // namespace cbor::tags::detail
