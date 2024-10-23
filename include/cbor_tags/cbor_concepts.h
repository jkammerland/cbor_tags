#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <type_traits>
#include <utility>
#include <variant>

namespace cbor::tags {
template <typename T>
concept ValidCborBuffer = requires(T) {
    std::is_convertible_v<typename T::value_type, std::byte>;
    std::is_convertible_v<typename T::size_type, std::size_t>;
    requires std::input_or_output_iterator<typename T::iterator>;
};

template <typename T>
concept IsContiguous = requires(T) { requires std::ranges::contiguous_range<T>; };

template <typename T>
concept IsUnsigned = std::is_unsigned_v<T>;

template <typename T>
concept IsSigned = std::is_signed_v<T>;

// Forward declaration of float16_t, implementation that can be used is in float16_ieee754.h
struct float16_t;

template <typename T>
concept IsSimple =
    std::is_floating_point_v<T> || std::is_same_v<T, float16_t> || std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, bool>;

template <typename T>
concept IsRange = std::ranges::range<T> && std::is_class_v<T>;

template <typename T>
concept IsMap = IsRange<T> && requires(T t) {
    typename T::key_type;
    typename T::mapped_type;
    typename T::value_type;
    requires std::same_as<typename T::value_type, std::pair<const typename T::key_type, typename T::mapped_type>>;
    { t.find(std::declval<typename T::key_type>()) } -> std::same_as<typename T::iterator>;
    { t.at(std::declval<typename T::key_type>()) } -> std::same_as<typename T::mapped_type &>;
    { t[std::declval<typename T::key_type>()] } -> std::same_as<typename T::mapped_type &>;
};

template <typename T>
concept IsArray = requires {
    typename T::value_type;
    typename T::size_type;
    typename std::tuple_size<T>::type;
    requires std::is_same_v<T, std::array<typename T::value_type, std::tuple_size<T>::value>> ||
                 std::is_same_v<T, std::span<typename T::value_type>>;
};

template <typename T>
concept IsTextString = requires(T t) {
    requires std::is_signed_v<typename T::value_type>;
    requires std::is_integral_v<typename T::value_type>;
    requires sizeof(typename T::value_type) == 1;
    { t.substr(0, 1) };
};

template <typename T>
concept IsBinaryString = IsRange<T> && std::is_same_v<std::decay_t<std::ranges::range_value_t<T>>, std::byte>;

template <typename T>
concept IsString = IsTextString<T> || IsBinaryString<T>;

template <typename T>
concept IsTuple = requires {
    typename std::tuple_size<T>::type;
    typename std::tuple_element<0, T>::type;
    requires(!IsArray<T>);
};

template <typename T>
concept HasCborTag = requires {
    { T::cbor_tag } -> std::convertible_to<std::uint64_t>;
};

template <typename T>
concept IsTaggedTuple = requires(T t) {
    requires IsTuple<T>;
    requires HasCborTag<typename T::first_type>;
};

template <typename T>
concept IsTagged = HasCborTag<T> || IsTaggedTuple<T>;

template <typename T>
concept IsOptional = requires(T t) {
    typename T::value_type;
    { T{typename T::value_type{}} } -> std::same_as<T>;
    { T{std::nullopt} } -> std::same_as<T>;
    { t.value() } -> std::same_as<typename T::value_type &>;
};

template <typename T>
concept IsVariant = requires(T t) {
    { std::variant_size_v<T> } -> std::convertible_to<size_t>;
    { t.index() } -> std::convertible_to<size_t>;
};

template <typename T>
concept IsAggregate = std::is_aggregate_v<T>;

template <typename T>
concept IsNonAggregate = !IsAggregate<T>;

// Major Type  | Meaning           | Content
// ------------|-------------------|-------------------------
// 0           | unsigned integer  | N
// 1           | negative integer  | -1-N
// 2           | byte string       | N bytes
// 3           | text string       | N bytes (UTF-8 text)
// 4           | array             | N data items (elements)
// 5           | map               | 2N data items (key/value pairs)
// 6           | tag of number N   | 1 data item
// 7           | simple/float      | -

// Helper struct to assign numbers to concepts
template <typename ByteType, typename T>
struct ConceptType : std::integral_constant<ByteType, static_cast<ByteType>(IsUnsigned<T>       ? 0
                                                                            : IsSigned<T>       ? 1
                                                                            : IsBinaryString<T> ? 2
                                                                            : IsTextString<T>   ? 3
                                                                            : IsMap<T>          ? 4
                                                                            : IsArray<T>        ? 5
                                                                            : IsTagged<T>       ? 6
                                                                            : IsRange<T>        ? 5
                                                                                                : 255)> {};

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

struct any {
    template <class T> constexpr operator T() const {
        if constexpr (is_optional_v<T>) {
            return T{typename T::value_type{}};
        } else {
            return T{};
        }
    }

  private:
    template <class T> static constexpr bool is_optional_v                   = false;
    template <class T> static constexpr bool is_optional_v<std::optional<T>> = true;
};

template <std::uint64_t N> struct tag {
    static constexpr std::uint64_t cbor_tag = N;

    constexpr operator std::uint64_t() const { return cbor_tag; }

    // Spacechip operator
    template <std::uint64_t M> constexpr auto operator<=>(const tag<M> &) const {
        if constexpr (N < M) {
            return std::strong_ordering::less;
        } else if constexpr (N > M) {
            return std::strong_ordering::greater;
        } else {
            return std::strong_ordering::equal;
        }
    }
};

template <typename T>
concept HasReserve = requires(T t) {
    { t.reserve(std::declval<typename T::size_type>()) };
};

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
template <char... Chars> constexpr auto operator"" _tag() { return tag<detail::parse_decimal<Chars...>()>{}; }

// Hexadecimal tag literal
template <char... Chars> constexpr auto operator"" _hex_tag() { return tag<detail::parse_hex<Chars...>()>{}; }

} // namespace literals

} // namespace cbor::tags