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
concept IsOptions = requires(T) {
    typename T::is_options;
    typename T::return_type;
    { T::wrap_groups } -> std::convertible_to<bool>;
};

template <typename T>
concept ValidCborBuffer = requires(T) {
    std::is_convertible_v<typename T::value_type, std::byte>;
    std::is_convertible_v<typename T::size_type, std::size_t>;
    requires std::input_or_output_iterator<typename T::iterator>;
};

template <typename T> constexpr auto cbor_tag(const T &obj);
template <typename T> constexpr auto cbor_tag() {
    struct Anonymous {};
    return Anonymous{};
}

// Free function variants of coding functions
template <typename T, typename Class>
concept HasTranscodeFreeFunction = requires(T t, Class c) {
    { transcode(t, std::forward<Class>(c)).has_value() } -> std::convertible_to<bool>;
};

template <typename T, typename Class>
concept HasEncodeFreeFunction = requires(T t, Class c) {
    { encode(t, std::forward<Class>(c)).has_value() } -> std::convertible_to<bool>;
};

template <typename T, typename Class>
concept HasDecodeFreeFunction = requires(T t, Class c) {
    { decode(t, std::forward<Class>(c)).has_value() } -> std::convertible_to<bool>;
};

template <typename T>
concept IsContiguous = requires(T) { requires std::ranges::contiguous_range<T>; };

// Forward declaration of float16_t, implementation that can be used is in float16_ieee754.h
struct float16_t;

struct negative;

struct integer;

struct simple;

struct as_text_any {
    std::uint64_t size;
};

struct as_bstr_any {
    std::uint64_t size;
};

struct as_array_any {
    std::uint64_t size;
};

struct as_map_any {
    std::uint64_t size;
};

struct as_tag_any {
    std::uint64_t tag;
};

// TODO:
struct array_as_map;
struct map_as_array;

template <typename T>
concept IsTextHeader = std::is_same_v<T, as_text_any>;

template <typename T>
concept IsBinaryHeader = std::is_same_v<T, as_bstr_any>;

template <typename T>
concept IsArrayHeader = std::is_same_v<T, as_array_any>;

template <typename T>
concept IsMapHeader = std::is_same_v<T, as_map_any>;

template <typename T>
concept IsTagHeader = std::is_same_v<T, as_tag_any>;

template <typename T>
concept IsAnyHeader = IsArrayHeader<T> || IsMapHeader<T> || IsTagHeader<T> || IsTextHeader<T> || IsBinaryHeader<T>;

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
concept IsEnum = std::is_enum_v<T>;

template <typename T>
concept IsEnumUnsigned = IsEnum<T> && std::is_unsigned_v<std::underlying_type_t<T>>;

template <typename T>
concept IsEnumSigned = IsEnum<T> && std::is_signed_v<std::underlying_type_t<T>>;

template <typename T>
concept IsUnsigned = (std::is_unsigned_v<T> && std::is_integral_v<T> && !IsSimple<T>);

template <typename T>
concept IsUnsignedOrEnum = IsUnsigned<T> || IsEnumUnsigned<T>;

template <typename T>
concept IsSigned = ((std::is_signed_v<T> && std::is_integral_v<T> && !IsSimple<T>) || std::is_same_v<T, integer>);

template <typename T>
concept IsSignedOrEnum = IsSigned<T> || IsEnumSigned<T>;

template <typename T>
concept IsNegative = std::is_same_v<T, negative>;

template <typename T>
concept IsInteger = IsUnsigned<T> || IsSigned<T> || IsNegative<T>;

template <typename T>
concept IsView = std::ranges::view<T>;

template <typename T>
concept IsConstView = IsView<T> && std::is_const_v<typename T::element_type>;

template <typename T>
concept IsConstBinaryView = IsConstView<T> && std::is_same_v<typename T::value_type, std::byte>;

template <typename T>
concept IsConstTextView = IsConstView<T> && std::is_same_v<typename T::value_type, char>;

template <typename T>
concept IsTextString = IsTextHeader<T> || IsConstTextView<T> || requires(T t) {
    requires std::is_signed_v<typename T::value_type>;
    requires std::is_integral_v<typename T::value_type>;
    requires sizeof(typename T::value_type) == 1;
    { t.substr(0, 1) };
};

template <typename T>
concept IsBinaryString =
    IsBinaryHeader<T> || std::is_same_v<std::remove_cvref_t<std::ranges::range_value_t<T>>, std::byte> || IsConstBinaryView<T>;

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
concept IsMap = IsMapHeader<T> || (IsRangeOfCborValues<T> && requires(T t) {
                    typename T::key_type;
                    typename T::mapped_type;
                    typename T::value_type;
                    requires std::same_as<typename T::value_type, std::pair<const typename T::key_type, typename T::mapped_type>>;
                    { t.find(std::declval<typename T::key_type>()) } -> std::same_as<typename T::iterator>;
                });

template <typename T>
concept IsMultiMap = IsMap<T> && requires(T t) {
    { t.equal_range(std::declval<typename T::key_type>()) } -> std::same_as<std::pair<typename T::iterator, typename T::iterator>>;
    requires !requires {
        { t.insert_or_assign(std::declval<typename T::key_type>(), std::declval<typename T::mapped_type>()) };
    };
};

template <typename T>
concept IsArray = IsArrayHeader<T> || (IsRangeOfCborValues<T> && !IsMap<T>);

template <typename T>
concept IsTuple = requires {
    typename std::tuple_size<std::remove_cvref_t<T>>::type;
    typename std::tuple_element<0, std::remove_cvref_t<T>>::type;
    requires(!IsFixedArray<std::remove_cvref_t<T>>);
};

namespace detail {

struct FalseType {};

} // namespace detail

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
concept is_dynamic_tag_t = std::is_same_v<T, dynamic_tag<uint8_t>> || std::is_same_v<T, dynamic_tag<uint16_t>> ||
                           std::is_same_v<T, dynamic_tag<uint32_t>> || std::is_same_v<T, dynamic_tag<uint64_t>>;

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

// A proxy that can have access to a number of predefined functions
struct Access {
    // Transcode function
    template <typename T, typename Class> static constexpr auto transcode(T &transcoder, Class &&obj) {
        if constexpr (requires { obj.transcode(transcoder).has_value(); }) {
            return obj.transcode(transcoder);
        } else {
            return detail::FalseType{};
        }
    }

    // Encode function
    template <typename T, typename Class> static constexpr auto encode(T &encoder, Class &&obj) {
        if constexpr (requires { obj.encode(encoder).has_value(); }) {
            return obj.encode(encoder);
        } else {
            return detail::FalseType{};
        }
    }

    // Decode function
    template <typename T, typename Class> static constexpr auto decode(T &decoder, Class &&obj) {
        if constexpr (requires { obj.decode(decoder).has_value(); }) {
            return obj.decode(decoder);
        } else {
            return detail::FalseType{};
        }
    }

    template <typename T> static constexpr auto cbor_tag(const T &obj) {
        if constexpr (requires { obj.cbor_tag; }) {
            return obj.cbor_tag;
        } else {
            return detail::FalseType{};
        }
    }

    // cbor_tag function
    template <typename T> static constexpr auto cbor_tag() {
        if constexpr (requires { T::cbor_tag; }) {
            if constexpr (is_static_tag_t<decltype(T::cbor_tag)>::value) {
                return decltype(T::cbor_tag){};
            } else if constexpr (HasInlineTag<T>) {
                return T::cbor_tag;
            } else {
                return detail::FalseType{};
            }
        } else if constexpr (requires { cbor_tag<T>(); }) {
            return cbor_tag<T>();
        } else if constexpr (requires { cbor_tag(T{}); }) {
            return cbor_tag(T{});
        } else {
            return detail::FalseType{};
        }
    }
};

// Overload of coding functions, as member function
template <typename T, typename Class>
concept HasTranscodeMethod = requires(T t, Class c) {
    { Access::transcode(t, c).has_value() } -> std::convertible_to<bool>;
};

template <typename T, typename Class>
concept HasEncodeMethod = requires(T t, Class c) {
    { Access::encode(t, c).has_value() } -> std::convertible_to<bool>;
};

template <typename T, typename Class>
concept HasDecodeMethod = requires(T t, Class c) {
    { Access::decode(t, c).has_value() } -> std::convertible_to<bool>;
};

template <typename T>
concept HasTagNonConstructible = requires(T) {
    { cbor::tags::cbor_tag<T>() } -> std::convertible_to<std::uint64_t>;
};

// Free function variants of tag functions
template <typename T>
concept HasTagFreeFunction = requires(T t) {
    { cbor_tag(t) } -> std::convertible_to<std::uint64_t>;
};

// Member function variants of tag functions
template <typename T>
concept HasTagMember = requires(T t) {
    { Access::cbor_tag(t) } -> std::convertible_to<std::uint64_t>;
};

template <typename T>
concept IsTaggedTuple = requires(T t) {
    requires IsTuple<T>;
    requires(is_static_tag_t<std::remove_cvref_t<decltype(std::get<0>(t))>>::value ||
             is_dynamic_tag_t<std::remove_cvref_t<decltype(std::get<0>(t))>>);
};

template <typename T>
concept IsUntaggedTuple = IsTuple<T> && !IsTaggedTuple<T> && !IsAnyHeader<T>;

// Must have either a cbor_tag(T) exlusive or a .cbor_tag member
template <typename T>
concept IsClassWithTagOverload =
    std::is_class_v<T> && static_cast<bool>(HasTagFreeFunction<T> ^ HasTagMember<T> ^ HasTagNonConstructible<T>);

template <typename T>
concept IsTag = HasDynamicTag<T> || HasStaticTag<T> || HasInlineTag<T> || IsTaggedTuple<T> || IsTagHeader<T> || IsClassWithTagOverload<T>;

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

template <typename T, typename C>
concept IsClassWithEncodingOverload = std::is_class_v<C> && (HasTranscodeMethod<T, C> || HasEncodeMethod<T, C> ||
                                                             HasTranscodeFreeFunction<T, C> || HasEncodeFreeFunction<T, C>);

template <typename T, typename C>
concept IsClassWithDecodingOverload = std::is_class_v<C> && (HasTranscodeMethod<T, C> || HasDecodeMethod<T, C> ||
                                                             HasTranscodeFreeFunction<T, C> || HasDecodeFreeFunction<T, C>);

template <typename T>
concept IsAggregate = std::is_aggregate_v<T> && !IsFixedArray<T> && !IsAnyHeader<T> && !IsString<T>;

// Helper to check if all types in a variant satisfy IsCborMajor
template <typename T> struct AllTypesAreCborMajor;
template <typename T, bool Map = IsMap<T>> struct ContainsCborMajor;

template <typename T>
concept ContainsCborMajorConcept = ContainsCborMajor<T>::value;

template <typename T>
concept AllTypesAreCborMajorConcept = AllTypesAreCborMajor<T>::value;

// TODO: cleanup or simplify
template <typename T>
concept IsCborMajor = IsAnyHeader<T> || IsUnsigned<T> || IsNegative<T> || IsSigned<T> || IsTextString<T> || IsBinaryString<T> ||
                      (IsArray<T> && ContainsCborMajorConcept<T>) || (IsMap<T> && ContainsCborMajorConcept<T>) || IsTag<T> || IsSimple<T> ||
                      (IsVariant<T> && AllTypesAreCborMajorConcept<T>) || (IsOptional<T> && ContainsCborMajorConcept<T>) || IsEnum<T> ||
                      (IsClassWithTagOverload<T>);

template <typename... Ts> struct AllTypesAreCborMajor<std::variant<Ts...>> {
    static constexpr bool value = (IsCborMajor<Ts> && ...);
};

// Helper for container like types, e.g optional
template <typename T> struct ContainsCborMajor<T, false> {
    static constexpr bool value = IsAnyHeader<T> || IsCborMajor<typename T::value_type>;
};

template <typename T> struct ContainsCborMajor<T, true> {
    static constexpr bool value = IsAnyHeader<T> || (IsCborMajor<typename T::key_type> && IsCborMajor<typename T::mapped_type>);
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

template <class T> constexpr bool is_optional_v                   = false;
template <class T> constexpr bool is_optional_v<std::optional<T>> = true;

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
    value_type cbor_tag;
    constexpr  operator value_type() const { return cbor_tag; }
};

template <typename T>
concept HasReserve = requires(T t) {
    { t.reserve(std::declval<typename T::size_type>()) };
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
template <char... Chars> constexpr auto operator""_tag() { return static_tag<detail::parse_decimal<Chars...>()>{}; }

// Hexadecimal tag literal
template <char... Chars> constexpr auto operator""_hex_tag() { return static_tag<detail::parse_hex<Chars...>()>{}; }

} // namespace literals

template <typename T, typename... Ts> static constexpr bool contains() { return (std::is_same_v<T, Ts> || ...); }
} // namespace cbor::tags