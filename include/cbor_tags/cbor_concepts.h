#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <type_traits>
#include <utility>
#include <variant>

namespace cbor::tags {

enum class major_type : std::uint8_t {
    UnsignedInteger = 0,
    NegativeInteger = 1,
    ByteString      = 2,
    TextString      = 3,
    Array           = 4,
    Map             = 5,
    Tag             = 6,
    Simple          = 7
};

template <typename T>
concept ValidCborBuffer = requires(T) {
    std::is_convertible_v<typename T::value_type, std::byte>;
    std::is_convertible_v<typename T::size_type, std::size_t>;
    requires std::input_or_output_iterator<typename T::iterator>;
};

template <typename T>
concept IsContiguous = requires(T) { requires std::ranges::contiguous_range<T>; };

// Forward declaration of float16_t, implementation that can be used is in float16_ieee754.h
struct float16_t;

struct negative;

struct integer;

struct simple;

template <typename T>
concept IsFloat16 = std::is_same_v<T, float16_t>; // Do not require sizeof(T) == 2, let the memory layout be implementation defined

template <typename T>
concept IsFloat32 = std::is_same_v<T, float> && requires(T t) { sizeof(t) == 4; };

template <typename T>
concept IsFloat64 = std::is_same_v<T, double> && requires(T t) { sizeof(t) == 8; };

template <typename T>
concept IsBool = std::is_same_v<T, bool>;

template <typename T>
concept IsNull = std::is_same_v<T, std::nullptr_t>;

template <typename T>
concept IsSimple = IsFloat16<T> || IsFloat32<T> || IsFloat64<T> || IsBool<T> || IsNull<T> || std::is_same_v<T, simple>;

template <typename T>
concept IsUnsigned = std::is_unsigned_v<T> && std::is_integral_v<T> && !IsSimple<T>;

template <typename T>
concept IsSigned = (std::is_signed_v<T> && std::is_integral_v<T> && !IsSimple<T>) || std::is_same_v<T, integer>;

template <typename T>
concept IsNegative = std::is_same_v<T, negative>;

template <typename T>
concept IsInteger = IsUnsigned<T> || IsSigned<T> || IsNegative<T>;

template <typename T>
concept IsEnum = std::is_enum_v<T>;

template <typename T>
concept IsTextString = requires(T t) {
    requires std::is_signed_v<typename T::value_type>;
    requires std::is_integral_v<typename T::value_type>;
    requires sizeof(typename T::value_type) == 1;
    { t.substr(0, 1) };
};

template <typename T>
concept IsBinaryString = std::is_same_v<std::remove_cvref_t<std::ranges::range_value_t<T>>, std::byte>;

template <typename T>
concept IsString = IsTextString<T> || IsBinaryString<T>;

template <typename T>
concept IsRangeOfCborValues = std::ranges::range<T> && std::is_class_v<T> && (!IsString<T>);

template <typename T>
concept IsFixedArray = requires {
    typename T::value_type;
    typename T::size_type;
    typename std::tuple_size<T>::type;
    requires std::is_same_v<T, std::array<typename T::value_type, std::tuple_size<T>::value>> ||
                 std::is_same_v<T, std::span<typename T::value_type>>;
};

template <typename T>
concept IsArray = IsRangeOfCborValues<T> && !IsFixedArray<T>;

template <typename T>
concept IsMap = IsRangeOfCborValues<T> && requires(T t) {
    typename T::key_type;
    typename T::mapped_type;
    typename T::value_type;
    requires std::same_as<typename T::value_type, std::pair<const typename T::key_type, typename T::mapped_type>>;
    { t.find(std::declval<typename T::key_type>()) } -> std::same_as<typename T::iterator>;
    { t.at(std::declval<typename T::key_type>()) } -> std::same_as<typename T::mapped_type &>;
    { t[std::declval<typename T::key_type>()] } -> std::same_as<typename T::mapped_type &>;
};

template <typename T>
concept IsTuple = requires {
    typename std::tuple_size<T>::type;
    typename std::tuple_element<0, T>::type;
    requires(!IsFixedArray<T>);
};

template <typename T>
concept IsAggregate = std::is_aggregate_v<T> && !IsFixedArray<T>;

template <std::uint64_t T> struct static_tag;
template <IsUnsigned T> struct dynamic_tag;

template <typename T>
concept HasDynamicTag = std::is_same_v<T, dynamic_tag<typename T::value_type>> || requires(T t) {
    { t.cbor_tag } -> std::convertible_to<std::uint64_t>;
    requires std::is_same_v<decltype(t.cbor_tag), dynamic_tag<typename decltype(t.cbor_tag)::value_type>>;
};

template <typename T> struct is_static_tag_t : std::false_type {};
template <std::uint64_t T> struct is_static_tag_t<static_tag<T>> : std::true_type {};

template <typename T>
concept HasStaticTag = requires {
    { T::cbor_tag } -> std::convertible_to<std::uint64_t>;
    requires is_static_tag_t<decltype(T::cbor_tag)>::value;
    requires(!HasDynamicTag<T>);
}; // namespace cbor::tags

template <typename T>
concept HasInlineTag = requires {
    requires IsUnsigned<decltype(T::cbor_tag)>;
    { T::cbor_tag } -> std::convertible_to<std::uint64_t>;
    requires(!HasDynamicTag<T>);
    requires(!HasStaticTag<T>);
};

template <typename T>
concept IsTaggedTuple = requires(T t) {
    requires IsTuple<T>;
    requires is_static_tag_t<std::remove_cvref_t<decltype(std::get<0>(t))>>::value;
};

template <typename T>
concept IsUntaggedTuple = IsTuple<T> && !IsTaggedTuple<T>;

template <typename T>
concept IsTag = HasDynamicTag<T> || HasStaticTag<T> || HasInlineTag<T> || IsTaggedTuple<T>;

template <typename T>
concept IsOptional = requires(T t) {
    typename T::value_type; // Must have a value_type
    t.has_value();          // Must have has_value() method
    t.value();              // Must have value() method
    T{};                    // Must be default constructible (nullopt)
};

// Type trait to unwrap nested types
template <typename T> struct unwrap_type {
    using type = T;
};

// Specialization for std::optional
template <typename T> struct unwrap_type<std::optional<T>> {
    using type = typename unwrap_type<T>::type;
};

// Specialization for std::variant
template <typename... Ts> struct unwrap_type<std::variant<Ts...>> {
    // Get the first type's unwrapped version
    using type = typename unwrap_type<std::tuple_element_t<0, std::tuple<Ts...>>>::type;
};

// Helper alias
template <typename T> using unwrap_type_t = typename unwrap_type<T>::type;

template <typename... T> constexpr bool contains_signed_integer = (... || IsSigned<unwrap_type_t<T>>);
template <typename... T> constexpr bool contains_unsigned       = (... || IsUnsigned<unwrap_type_t<T>>);
template <typename... T> constexpr bool contains_negative       = (... || IsNegative<unwrap_type_t<T>>);

template <typename T> struct is_variant : std::false_type {};

template <typename... Args> struct is_variant<std::variant<Args...>> : std::true_type {};

template <typename T>
concept IsVariant = is_variant<std::remove_cvref_t<T>>::value;

template <typename T> struct variant_contains_integer : std::false_type {};
template <template <typename...> typename V, typename... T>
struct variant_contains_integer<V<T...>> : std::bool_constant<contains_signed_integer<T...>> {};

template <typename T> struct variant_contains_unsigned_or_negative : std::false_type {};
template <template <typename...> typename V, typename... T>
struct variant_contains_unsigned_or_negative<V<T...>> : std::bool_constant<contains_unsigned<T...> || contains_negative<T...>> {};

template <typename T>
concept IsVariantWithSignedInteger = IsVariant<T> && variant_contains_integer<T>::value;

template <typename T>
concept IsVariantWithUnsignedInteger = IsVariant<T> && variant_contains_unsigned_or_negative<T>::value;

template <typename T>
concept IsVariantWithOnlySignedInteger = IsVariantWithSignedInteger<T> && !IsVariantWithUnsignedInteger<T>;

template <typename T>
concept IsVariantWithOnlyUnsignedInteger = IsVariantWithUnsignedInteger<T> && !IsVariantWithSignedInteger<T>;

template <typename T>
concept IsVariantWithoutIntegers = !IsVariantWithSignedInteger<T> && !IsVariantWithUnsignedInteger<T>;

template <typename T>
concept IsStrictVariant = IsVariantWithOnlySignedInteger<T> || IsVariantWithOnlyUnsignedInteger<T> || IsVariantWithoutIntegers<T>;

// Helper to check if all types in a variant satisfy IsCborMajor
template <typename T> struct AllTypesAreCborMajor;
template <typename T, bool Map = IsMap<T>> struct ContainsCborMajor;

template <typename T>
concept IsCborMajor = IsUnsigned<T> || IsNegative<T> || IsSigned<T> || IsTextString<T> || IsBinaryString<T> ||
                      (IsArray<T> && ContainsCborMajor<T>::value) || (IsMap<T> && ContainsCborMajor<T>::value) || IsTag<T> || IsSimple<T> ||
                      (IsVariant<T> && AllTypesAreCborMajor<T>::value) || (IsOptional<T> && ContainsCborMajor<T>::value) || IsEnum<T>;

template <typename... Ts> struct AllTypesAreCborMajor<std::variant<Ts...>> {
    static constexpr bool value = (IsCborMajor<Ts> && ...);
};

// Helper for container like types, e.g optional
template <typename T> struct ContainsCborMajor<T, false> {
    static constexpr bool value = IsCborMajor<typename T::value_type>;
};

template <typename T> struct ContainsCborMajor<T, true> {
    static constexpr bool value = IsCborMajor<typename T::key_type> && IsCborMajor<typename T::mapped_type>;
};

template <typename Buffer>
    requires ValidCborBuffer<Buffer>
struct CborStream {
    Buffer           &buffer;
    Buffer::size_type head{};

    constexpr explicit CborStream(Buffer &buffer) : buffer(buffer) {}
    template <typename... Args> void operator()(const Args &...) { /* (de)serialize cbor onto buffer */ }
};

template <typename T, typename... Args>
concept IsBracesContructible = requires(Args... args) { T{args...}; };

namespace {
template <class T> static constexpr bool is_optional_v                   = false;
template <class T> static constexpr bool is_optional_v<std::optional<T>> = true;
} // namespace

struct any {
    template <class T> constexpr operator T() const {
        if constexpr (is_optional_v<T>) {
            return T{typename T::value_type{}};
        } else {
            return T{};
        }
    }
};

template <std::uint64_t N> struct static_tag {
    static constexpr std::uint64_t cbor_tag = N;

    constexpr operator std::uint64_t() const { return cbor_tag; }

    // Spacechip operator
    template <std::uint64_t M> constexpr auto operator<=>(const static_tag<M> &) const {
        if constexpr (N < M) {
            return std::strong_ordering::less;
        } else if constexpr (N > M) {
            return std::strong_ordering::greater;
        } else {
            return std::strong_ordering::equal;
        }
    }
};

template <IsUnsigned T> struct dynamic_tag {
    using value_type = T;
    value_type value;
    constexpr  operator value_type() const { return value; }
};

template <typename T>
concept HasReserve = requires(T t) {
    { t.reserve(std::declval<typename T::size_type>()) };
};

template <typename T>
concept IsEncoder = requires(T t) {
    { t.appender_ };
    { t.data_ };
    { t.encode_major_and_size(std::declval<std::uint64_t>(), std::declval<std::byte>()) };
};

template <typename T>
concept IsDecoder = requires(T t) {
    { t.reader_ };
    { t.data_ };
};

template <typename T> struct crtp_base {
    constexpr T       &underlying() { return static_cast<T &>(*this); }
    constexpr const T &underlying() const { return static_cast<const T &>(*this); }
};

template <typename T> struct always_false : std::false_type {};

namespace detail {

// requires(!std::same_as<T, std::span<typename T::value_type>>)
template <typename T> struct iterator_type {
    using type = typename T::const_iterator;
};

template <typename T> struct iterator_type<std::span<T>> {
    using type = typename std::span<T>::iterator;
};

// Helper for decimal parsing
template <char... Chars> constexpr std::uint64_t parse_decimal() {
    std::uint64_t result = 0;
    ((result = result * 10 + (Chars - '0')), ...);
    return result;
}

// Helper for hexadecimal parsing
template <char C> constexpr std::uint64_t hex_to_int() {
    if (C >= '0' && C <= '9')
        return C - '0';
    if (C >= 'a' && C <= 'f')
        return C - 'a' + 10;
    if (C >= 'A' && C <= 'F')
        return C - 'A' + 10;
    return 0; // Invalid hex character
}

template <char... Chars> constexpr std::uint64_t parse_hex() {
    std::uint64_t result = 0;
    ((result = (result << 4) | hex_to_int<Chars>()), ...);
    return result;
}
} // namespace detail

namespace literals {

// Decimal tag literal
template <char... Chars> constexpr auto operator"" _tag() { return static_tag<detail::parse_decimal<Chars...>()>{}; }

// Hexadecimal tag literal
template <char... Chars> constexpr auto operator"" _hex_tag() { return static_tag<detail::parse_hex<Chars...>()>{}; }

} // namespace literals

} // namespace cbor::tags