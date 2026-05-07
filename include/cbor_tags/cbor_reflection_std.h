#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_detail.h"
#include "cbor_tags/cbor_reflection_config.h"
#include "cbor_tags/cbor_reflection_count.h"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#if CBOR_TAGS_HAS_STD_REFLECTION

namespace cbor::tags {

namespace detail {
constexpr std::size_t MAX_REFLECTION_MEMBERS = std::numeric_limits<std::size_t>::max() - 1;

template <typename T> consteval auto aggregate_member_count() {
    return std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::current()).size();
}

template <typename T, std::size_t I> consteval std::meta::info aggregate_member() {
    return std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::current())[I];
}

template <typename T, std::size_t I> consteval auto aggregate_member_name() { return std::meta::identifier_of(aggregate_member<T, I>()); }

template <typename T, std::size_t... Is> constexpr auto to_tuple_impl(T &&object, std::index_sequence<Is...>) noexcept {
    using type = std::remove_cvref_t<T>;
    return std::tie(object.[:aggregate_member<type, Is>():]...);
}

} // namespace detail

template <class T> constexpr auto to_tuple(T &&object) noexcept {
    using type = std::remove_cvref_t<T>;
    static_assert(IsAggregate<type>, "Type must be an aggregate");
    constexpr auto member_count = detail::aggregate_member_count<type>();
    return detail::to_tuple_impl(std::forward<T>(object), std::make_index_sequence<member_count>{});
}

namespace detail {

template <typename Object> constexpr std::uint64_t                named_map_pair_count(const Object &object);
template <typename Object, std::size_t I> constexpr std::uint64_t named_member_pair_count(const Object &object);
template <typename RootObject, typename Object, typename Encoder>
constexpr void encode_named_entries_for_root(Encoder &enc, const Object &object);

template <typename Object, std::size_t... Is>
constexpr std::uint64_t named_map_pair_count_impl(const Object &object, std::index_sequence<Is...>) {
    return (std::uint64_t{} + ... + named_member_pair_count<Object, Is>(object));
}

template <typename Object, std::size_t I> constexpr std::uint64_t named_member_pair_count(const Object &object) {
    const auto  tuple = to_tuple(object);
    const auto &field = std::get<I>(tuple);
    using field_type  = std::remove_cvref_t<decltype(field)>;
    if constexpr (IsNamedGroupWrapper<field_type>) {
        return named_map_pair_count(field.value_);
    } else if constexpr (IsNamedExtensionWrapper<field_type>) {
        using extension_type = named_extension_value_t<field_type>;
        static_assert(IsMap<extension_type> && IsTextString<typename extension_type::key_type>,
                      "as_named_extension requires a map with text-string keys");
        return static_cast<std::uint64_t>(field.value_.size());
    } else if constexpr (IsOptional<field_type>) {
        return field.has_value() ? 1U : 0U;
    } else {
        return 1U;
    }
}

template <typename Object> constexpr std::uint64_t named_map_pair_count(const Object &object) {
    using value_type = std::remove_cvref_t<Object>;
    return named_map_pair_count_impl(object, std::make_index_sequence<aggregate_member_count<value_type>()>{});
}

template <typename Object, std::size_t I> constexpr bool named_key_matches_fixed_member(std::string_view key);

template <typename Object, std::size_t... Is>
constexpr bool named_key_matches_fixed_member_impl(std::string_view key, std::index_sequence<Is...>) {
    return (named_key_matches_fixed_member<Object, Is>(key) || ...);
}

template <typename Object> constexpr bool named_key_matches_fixed_member(std::string_view key) {
    using value_type = std::remove_cvref_t<Object>;
    return named_key_matches_fixed_member_impl<value_type>(key, std::make_index_sequence<aggregate_member_count<value_type>()>{});
}

template <typename Object, std::size_t I> constexpr bool named_key_matches_fixed_member(std::string_view key) {
    using value_type = std::remove_cvref_t<Object>;
    using tuple_type = std::remove_cvref_t<decltype(to_tuple(std::declval<value_type &>()))>;
    using field_type = std::remove_cvref_t<std::tuple_element_t<I, tuple_type>>;

    if constexpr (IsNamedGroupWrapper<field_type>) {
        return named_key_matches_fixed_member<named_group_value_t<field_type>>(key);
    } else if constexpr (IsNamedExtensionWrapper<field_type>) {
        return false;
    } else {
        return key == std::string_view{aggregate_member_name<value_type, I>()};
    }
}

template <typename RootObject, typename Object, typename Encoder, std::size_t I>
constexpr void encode_named_member(Encoder &enc, const Object &object) {
    using value_type  = std::remove_cvref_t<Object>;
    const auto  tuple = to_tuple(object);
    const auto &field = std::get<I>(tuple);
    using field_type  = std::remove_cvref_t<decltype(field)>;

    if constexpr (IsNamedGroupWrapper<field_type>) {
        encode_named_entries_for_root<RootObject>(enc, field.value_);
    } else if constexpr (IsNamedExtensionWrapper<field_type>) {
        using extension_type = named_extension_value_t<field_type>;
        static_assert(IsMap<extension_type> && IsTextString<typename extension_type::key_type>,
                      "as_named_extension requires a map with text-string keys");
        static_assert(std::constructible_from<std::string_view, const typename extension_type::key_type &>,
                      "as_named_extension key type must be convertible to std::string_view");
        for (const auto &[key, mapped_value] : field.value_) {
            if (named_key_matches_fixed_member<RootObject>(std::string_view{key})) {
                throw std::runtime_error("as_named_extension key shadows a named map field");
            }
            enc.encode(key);
            enc.encode(mapped_value);
        }
    } else if constexpr (IsOptional<field_type>) {
        if (field.has_value()) {
            enc.encode(std::string_view{aggregate_member_name<value_type, I>()});
            enc.encode(*field);
        }
    } else {
        enc.encode(std::string_view{aggregate_member_name<value_type, I>()});
        enc.encode(field);
    }
}

template <typename RootObject, typename Object, typename Encoder, std::size_t... Is>
constexpr void encode_named_entries_impl(Encoder &enc, const Object &object, std::index_sequence<Is...>) {
    (encode_named_member<RootObject, Object, Encoder, Is>(enc, object), ...);
}

template <typename RootObject, typename Object, typename Encoder>
constexpr void encode_named_entries_for_root(Encoder &enc, const Object &object) {
    using value_type = std::remove_cvref_t<Object>;
    encode_named_entries_impl<RootObject>(enc, object, std::make_index_sequence<aggregate_member_count<value_type>()>{});
}

template <typename Encoder, typename Object> constexpr void encode_named_map(Encoder &enc, const Object &object) {
    enc.encode_major_and_size(named_map_pair_count(object), static_cast<typename Encoder::byte_type>(0xA0));
    encode_named_entries_for_root<Object>(enc, object);
}

static constexpr bool named_key_seen(const std::vector<std::string> &seen, std::string_view key) {
    return std::ranges::any_of(seen, [key](const std::string &candidate) { return candidate == key; });
}

template <typename Object> constexpr void                reset_named_optionals_and_extensions(Object &object);
template <typename Object, std::size_t I> constexpr void reset_named_member(Object &object);

template <typename Object, std::size_t... Is>
constexpr void reset_named_optionals_and_extensions_impl(Object &object, std::index_sequence<Is...>) {
    (reset_named_member<Object, Is>(object), ...);
}

template <typename Object> constexpr void reset_named_optionals_and_extensions(Object &object) {
    using value_type = std::remove_cvref_t<Object>;
    reset_named_optionals_and_extensions_impl(object, std::make_index_sequence<aggregate_member_count<value_type>()>{});
}

template <typename Object, std::size_t I> constexpr void reset_named_member(Object &object) {
    auto  tuple      = to_tuple(object);
    auto &field      = std::get<I>(tuple);
    using field_type = std::remove_cvref_t<decltype(field)>;
    if constexpr (IsNamedGroupWrapper<field_type>) {
        reset_named_optionals_and_extensions(field.value_);
    } else if constexpr (IsNamedExtensionWrapper<field_type>) {
        field.value_.clear();
    } else if constexpr (IsOptional<field_type>) {
        field.reset();
    }
}

template <typename Object> constexpr bool validate_required_named_members(const Object &object, const std::vector<std::string> &seen);
template <typename Object, typename Decoder>
constexpr status_code decode_named_map_entry(Decoder &dec, Object &object, std::vector<std::string> &seen);
template <typename Object, typename Decoder>
constexpr status_code decode_named_map_entry(Decoder &dec, Object &object, major_type key_major, std::byte key_additionalInfo,
                                             std::vector<std::string> &seen);

template <typename Decoder, typename Object> constexpr status_code decode_named_map(Decoder &dec, Object &object) {
    auto [major, additionalInfo] = dec.read_initial_byte();
    if (major != major_type::Map) {
        return status_code::no_match_for_map_on_buffer;
    }

    reset_named_optionals_and_extensions(object);
    std::vector<std::string> seen;

    if (additionalInfo == static_cast<std::byte>(31)) {
        while (true) {
            auto [key_major, key_additionalInfo] = dec.read_initial_byte();
            if (key_major == major_type::Simple && key_additionalInfo == static_cast<std::byte>(31)) {
                return validate_required_named_members(object, seen) ? status_code::success : status_code::unexpected_group_size;
            }
            auto entry_status = decode_named_map_entry(dec, object, key_major, key_additionalInfo, seen);
            if (entry_status != status_code::success) {
                return entry_status;
            }
        }
    }

    const auto pair_count = dec.decode_unsigned(additionalInfo);
    seen.reserve(static_cast<std::size_t>(pair_count));
    for (std::uint64_t index = 0; index < pair_count; ++index) {
        auto entry_status = decode_named_map_entry(dec, object, seen);
        if (entry_status != status_code::success) {
            return entry_status;
        }
    }

    return validate_required_named_members(object, seen) ? status_code::success : status_code::unexpected_group_size;
}

template <typename Object, typename Decoder>
constexpr status_code decode_named_map_value(Decoder &dec, Object &object, std::string_view key, std::vector<std::string> &seen);

template <typename Object, typename Decoder>
constexpr status_code decode_named_map_entry(Decoder &dec, Object &object, std::vector<std::string> &seen) {
    std::string key;
    auto        key_status = dec.decode(key);
    if (key_status != status_code::success) {
        return key_status;
    }
    return decode_named_map_value(dec, object, key, seen);
}

template <typename Object, typename Decoder>
constexpr status_code decode_named_map_entry(Decoder &dec, Object &object, major_type key_major, std::byte key_additionalInfo,
                                             std::vector<std::string> &seen) {
    std::string key;
    auto        key_status = dec.decode(key, key_major, key_additionalInfo);
    if (key_status != status_code::success) {
        return key_status;
    }
    return decode_named_map_value(dec, object, key, seen);
}

template <typename Object, typename Decoder>
constexpr bool decode_named_member_by_key(Decoder &dec, Object &object, std::string_view key, std::vector<std::string> &seen,
                                          status_code &status);
template <typename Object, typename Decoder>
constexpr bool decode_named_extension_by_key(Decoder &dec, Object &object, std::string_view key, status_code &status);

template <typename Object, typename Decoder>
constexpr status_code decode_named_map_value(Decoder &dec, Object &object, std::string_view key, std::vector<std::string> &seen) {
    auto value_status = status_code::success;
    if (decode_named_member_by_key(dec, object, key, seen, value_status)) {
        return value_status;
    }
    if (decode_named_extension_by_key(dec, object, key, value_status)) {
        return value_status;
    }
    return status_code::unexpected_group_size;
}

template <typename Field, typename Decoder> constexpr status_code decode_named_field_value(Decoder &dec, Field &field) {
    using field_type = std::remove_cvref_t<Field>;
    if constexpr (IsOptional<field_type>) {
        typename field_type::value_type value{};
        auto                            status = dec.decode(value);
        if (status == status_code::success) {
            field = std::move(value);
        }
        return status;
    } else {
        return dec.decode(field);
    }
}

template <typename Object, typename Decoder, std::size_t I>
constexpr bool decode_named_member(Decoder &dec, Object &object, std::string_view key, std::vector<std::string> &seen,
                                   status_code &status) {
    using value_type = std::remove_cvref_t<Object>;
    auto  tuple      = to_tuple(object);
    auto &field      = std::get<I>(tuple);
    using field_type = std::remove_cvref_t<decltype(field)>;

    if constexpr (IsNamedGroupWrapper<field_type>) {
        return decode_named_member_by_key(dec, field.value_, key, seen, status);
    } else if constexpr (IsNamedExtensionWrapper<field_type>) {
        return false;
    } else {
        constexpr auto field_name = aggregate_member_name<value_type, I>();
        if (key != std::string_view{field_name}) {
            return false;
        }
        if (named_key_seen(seen, key)) {
            status = status_code::unexpected_group_size;
            return true;
        }
        seen.emplace_back(key);
        status = decode_named_field_value(dec, field);
        return true;
    }
}

template <typename Object, typename Decoder, std::size_t... Is>
constexpr bool decode_named_member_by_key_impl(Decoder &dec, Object &object, std::string_view key, std::vector<std::string> &seen,
                                               status_code &status, std::index_sequence<Is...>) {
    bool matched = false;
    ((matched = matched || decode_named_member<Object, Decoder, Is>(dec, object, key, seen, status)), ...);
    return matched;
}

template <typename Object, typename Decoder>
constexpr bool decode_named_member_by_key(Decoder &dec, Object &object, std::string_view key, std::vector<std::string> &seen,
                                          status_code &status) {
    using value_type = std::remove_cvref_t<Object>;
    return decode_named_member_by_key_impl(dec, object, key, seen, status,
                                           std::make_index_sequence<aggregate_member_count<value_type>()>{});
}

template <typename Object, typename Decoder, std::size_t I>
constexpr bool decode_named_extension_member(Decoder &dec, Object &object, std::string_view key, status_code &status) {
    auto  tuple      = to_tuple(object);
    auto &field      = std::get<I>(tuple);
    using field_type = std::remove_cvref_t<decltype(field)>;
    if constexpr (IsNamedGroupWrapper<field_type>) {
        return decode_named_extension_by_key(dec, field.value_, key, status);
    } else if constexpr (IsNamedExtensionWrapper<field_type>) {
        using extension_type = named_extension_value_t<field_type>;
        static_assert(IsMap<extension_type> && IsTextString<typename extension_type::key_type>,
                      "as_named_extension requires a map with text-string keys");
        static_assert(std::constructible_from<typename extension_type::key_type, std::string_view>,
                      "as_named_extension key type must be constructible from std::string_view");
        using key_type    = typename extension_type::key_type;
        using mapped_type = typename extension_type::mapped_type;
        key_type extension_key{key};
        if (field.value_.find(extension_key) != field.value_.end()) {
            status = status_code::unexpected_group_size;
            return true;
        }
        mapped_type mapped_value{};
        status = dec.decode(mapped_value);
        if (status == status_code::success) {
            field.value_.emplace(std::move(extension_key), std::move(mapped_value));
        }
        return true;
    } else {
        return false;
    }
}

template <typename Object, typename Decoder, std::size_t... Is>
constexpr bool decode_named_extension_by_key_impl(Decoder &dec, Object &object, std::string_view key, status_code &status,
                                                  std::index_sequence<Is...>) {
    bool matched = false;
    ((matched = matched || decode_named_extension_member<Object, Decoder, Is>(dec, object, key, status)), ...);
    return matched;
}

template <typename Object, typename Decoder>
constexpr bool decode_named_extension_by_key(Decoder &dec, Object &object, std::string_view key, status_code &status) {
    using value_type = std::remove_cvref_t<Object>;
    return decode_named_extension_by_key_impl(dec, object, key, status, std::make_index_sequence<aggregate_member_count<value_type>()>{});
}

template <typename Object, std::size_t I>
constexpr bool required_named_member_present(const Object &object, const std::vector<std::string> &seen) {
    using value_type  = std::remove_cvref_t<Object>;
    const auto  tuple = to_tuple(object);
    const auto &field = std::get<I>(tuple);
    using field_type  = std::remove_cvref_t<decltype(field)>;
    if constexpr (IsNamedGroupWrapper<field_type>) {
        return validate_required_named_members(field.value_, seen);
    } else if constexpr (IsNamedExtensionWrapper<field_type> || IsOptional<field_type>) {
        return true;
    } else {
        return named_key_seen(seen, std::string_view{aggregate_member_name<value_type, I>()});
    }
}

template <typename Object, std::size_t... Is>
constexpr bool validate_required_named_members_impl(const Object &object, const std::vector<std::string> &seen,
                                                    std::index_sequence<Is...>) {
    return (required_named_member_present<Object, Is>(object, seen) && ...);
}

template <typename Object> constexpr bool validate_required_named_members(const Object &object, const std::vector<std::string> &seen) {
    using value_type = std::remove_cvref_t<Object>;
    return validate_required_named_members_impl(object, seen, std::make_index_sequence<aggregate_member_count<value_type>()>{});
}

} // namespace detail

} // namespace cbor::tags

#endif
