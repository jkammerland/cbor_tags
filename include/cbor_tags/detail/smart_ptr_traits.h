#pragma once

#include "cbor_tags/cbor_concepts_checking.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <variant>
#include <vector>

namespace cbor::tags::ext::smart_ptr::detail {

inline constexpr std::uint64_t shareable_tag = 28U;
inline constexpr std::uint64_t sharedref_tag = 29U;

template <typename T>
concept NullablePointerValue = !std::is_void_v<T> && !std::is_array_v<T> && !std::is_const_v<T>;

template <typename T> struct nullable_pointer_traits {
    static constexpr bool decodable = false;
    static constexpr bool shared    = false;
};

template <NullablePointerValue T> struct nullable_pointer_traits<std::unique_ptr<T>> {
    static constexpr bool decodable = std::default_initializable<T>;
    static constexpr bool shared    = false;
};

template <NullablePointerValue T> struct nullable_pointer_traits<std::shared_ptr<T>> {
    static constexpr bool decodable = std::default_initializable<T>;
    static constexpr bool shared    = true;
};

template <typename T> constexpr bool decodable_nullable_pointer_v = nullable_pointer_traits<std::remove_cvref_t<T>>::decodable;

template <typename T>
constexpr bool decodable_shared_pointer_v =
    nullable_pointer_traits<std::remove_cvref_t<T>>::decodable && nullable_pointer_traits<std::remove_cvref_t<T>>::shared;

template <typename... Ts>
constexpr std::size_t decodable_nullable_pointer_count_v =
    (std::size_t{0} + ... + (decodable_nullable_pointer_v<Ts> ? std::size_t{1} : std::size_t{0}));

template <typename... Ts> constexpr bool has_decodable_nullable_pointer_v = decodable_nullable_pointer_count_v<Ts...> > 0U;

template <typename T> struct decodable_shared_graph_vector : std::false_type {};

template <NullablePointerValue T, typename Allocator>
struct decodable_shared_graph_vector<std::vector<std::shared_ptr<T>, Allocator>>
    : std::bool_constant<std::default_initializable<T> && std::default_initializable<std::vector<std::shared_ptr<T>, Allocator>>> {};

template <typename T> constexpr bool decodable_shared_graph_vector_v = decodable_shared_graph_vector<std::remove_cvref_t<T>>::value;

template <typename... Ts>
constexpr std::size_t decodable_shared_graph_vector_count_v =
    (std::size_t{0} + ... + (decodable_shared_graph_vector_v<Ts> ? std::size_t{1} : std::size_t{0}));

template <typename... Ts> constexpr bool has_decodable_shared_graph_vector_v = decodable_shared_graph_vector_count_v<Ts...> > 0U;

template <typename Variant, std::uint64_t Tag>
constexpr bool variant_contains_static_tag_v = [] {
    constexpr auto tags      = ValidConceptMapping<Variant>::tags;
    constexpr auto tags_size = ValidConceptMapping<Variant>::number_of_tags;
    for (std::uint64_t index = 0; index < tags_size; ++index) {
        if (tags[index] == Tag) {
            return true;
        }
    }
    return false;
}();

template <typename Variant>
constexpr bool variant_has_any_tag_header_v = [] {
    using major_index           = cbor::tags::detail::MajorIndex;
    constexpr auto core_mapping = valid_concept_mapping_array_v<Variant>;
    return core_mapping[major_index::AnyTagHeader] != 0U;
}();

template <typename... Ts>
constexpr bool variant_has_shared_graph_tag_collision_v = [] {
    using variant_type = std::variant<Ts...>;
    return (decodable_shared_pointer_v<Ts> || ...) &&
           (variant_has_any_tag_header_v<variant_type> || variant_contains_static_tag_v<variant_type, shareable_tag> ||
            variant_contains_static_tag_v<variant_type, sharedref_tag>);
}();

struct nullable_ptr_codec_marker {};
struct shared_graph_codec_marker {};

template <typename T> constexpr bool has_nullable_ptr_codec_v = std::is_base_of_v<nullable_ptr_codec_marker, std::remove_cvref_t<T>>;

template <typename T> constexpr bool has_shared_graph_codec_v = std::is_base_of_v<shared_graph_codec_marker, std::remove_cvref_t<T>>;

} // namespace cbor::tags::ext::smart_ptr::detail
