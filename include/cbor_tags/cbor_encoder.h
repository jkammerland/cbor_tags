#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_detail.h"
#include "cbor_tags/cbor_integer.h"
#include "cbor_tags/cbor_operators.h"
#include "cbor_tags/cbor_reflection.h"
#include "cbor_tags/cbor_simple.h"
#include "tl/expected.hpp"

#include <bit>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
// #include <fmt/base.h>
// #include <nameof.hpp>
#include <type_traits>
#include <utility>
#include <variant>

namespace cbor::tags {

template <typename T> struct cbor_header_encoder;

template <typename OutputBuffer, IsOptions Options, template <typename> typename... Encoders>
    requires ValidCborBuffer<OutputBuffer>
struct encoder : Encoders<encoder<OutputBuffer, Options, Encoders...>>... {
    using self_t = encoder<OutputBuffer, Options, Encoders...>;
    using Encoders<self_t>::encode...;

    using byte_type     = typename OutputBuffer::value_type;
    using size_type     = typename OutputBuffer::size_type;
    using iterator_type = typename detail::iterator_type<OutputBuffer>::type;
    using subrange      = std::ranges::subrange<iterator_type>;
    using expected_type = typename Options::return_type;
    using options       = Options;

    constexpr explicit encoder(OutputBuffer &data) : data_(data) {}

    template <typename... T> expected_type operator()(const T &...args) noexcept {
        try {
            (encode(args), ...);
            return expected_type{};
        } catch (const std::bad_alloc &) { return unexpected<status_code>(status_code::out_of_memory); } catch (...) {
            // std::rethrow_exception(std::current_exception()); // for debugging, this handling is TODO!
            return unexpected<status_code>(status_code::error);
        }
    }

    constexpr void encode_major_and_size(std::uint64_t value, byte_type majorType) {
        if (value < 24) {
            appender_(data_, static_cast<byte_type>(value) | majorType);
        } else if (value <= 0xFF) {
            appender_.multi_append(data_, static_cast<byte_type>(static_cast<byte_type>(24) | majorType), static_cast<byte_type>(value));
        } else if (value <= 0xFFFF) {
            appender_.multi_append(data_, static_cast<byte_type>(static_cast<byte_type>(25) | majorType),
                                   static_cast<byte_type>(value >> 8), static_cast<byte_type>(value));
        } else if (value <= 0xFFFFFFFF) {
            appender_.multi_append(data_, static_cast<byte_type>(static_cast<byte_type>(26) | majorType),
                                   static_cast<byte_type>(value >> 24), static_cast<byte_type>(value >> 16),
                                   static_cast<byte_type>(value >> 8), static_cast<byte_type>(value));
        } else {
            appender_.multi_append(data_, static_cast<byte_type>(static_cast<byte_type>(27) | majorType),
                                   static_cast<byte_type>(value >> 56), static_cast<byte_type>(value >> 48),
                                   static_cast<byte_type>(value >> 40), static_cast<byte_type>(value >> 32),
                                   static_cast<byte_type>(value >> 24), static_cast<byte_type>(value >> 16),
                                   static_cast<byte_type>(value >> 8), static_cast<byte_type>(value));
        }
    }

    template <IsUnsigned T> constexpr void encode(T value) { encode_major_and_size(value, static_cast<byte_type>(0x00)); }

    template <IsSigned T> constexpr void encode(T value) {
        if (value >= 0) {
            encode_major_and_size(static_cast<std::uint64_t>(value), static_cast<byte_type>(0x00));
        } else {
            encode_major_and_size(static_cast<std::uint64_t>(-1 - value), static_cast<byte_type>(0x20));
        }
    }

    constexpr void encode(negative value) {
        encode_major_and_size(static_cast<std::uint64_t>(value.value - 1), static_cast<byte_type>(0x20));
    }

    constexpr void encode(integer value) {
        if (value.is_negative) {
            encode(negative{value.value});
        } else {
            encode(value.value);
        }
    }

    template <std::uint64_t N> constexpr void encode(static_tag<N>) { encode_major_and_size(N, static_cast<byte_type>(0xC0)); }
    template <IsUnsigned T> constexpr void    encode(dynamic_tag<T> value) {
        encode_major_and_size(value.cbor_tag, static_cast<byte_type>(0xC0));
    }

    template <IsString T> constexpr void encode(const T &value) {
        encode_major_and_size(value.size(), static_cast<byte_type>(get_major_3_bit_tag<T>()));
        appender_(data_, value);
    }

    template <IsArray T> constexpr void encode(const T &value) {
        encode_major_and_size(value.size(), static_cast<byte_type>(0x80));
        for (const auto &item : value) {
            encode(item);
        }
    }

    template <IsMap T> constexpr void encode(const T &value) {
        encode_major_and_size(value.size(), static_cast<byte_type>(0xA0));
        for (const auto &[key, mapped_value] : value) {
            encode(key);
            encode(mapped_value);
        }
    }

    template <typename U> constexpr void encode([[maybe_unused]] const as_named_map<U> &value) {
#if CBOR_TAGS_HAS_STD_REFLECTION
        encode_named_map(value.value_);
#else
        static_assert(always_false<std::remove_cvref_t<U>>::value, "as_named_map requires C++26 static reflection");
#endif
    }

    template <IsTaggedTuple T> constexpr void encode(const T &value) {
        /* Tag being first already garanteed by the concept */
        encode(std::get<0>(value));
        aggregate_encode(detail::tuple_tail(value));
    }

    template <typename T>
        requires(IsAggregate<T> && !IsClassWithEncodingOverload<self_t, T>)
    constexpr void encode(const T &value) {
        if constexpr (HasInlineTag<T>) {
            const auto &&tuple = to_tuple(value);
            encode_major_and_size(T::cbor_tag, static_cast<byte_type>(0xC0));
            std::apply(
                [this](const auto &...args) {
                    constexpr auto   size_        = sizeof...(args);
                    constexpr size_t has_tag_or_1 = std::conditional_t < HasStaticTag<T> || HasDynamicTag<T>,
                                     std::integral_constant<size_t, 2>, std::integral_constant < size_t, 1 >> ::value;
                    if constexpr (size_ > has_tag_or_1 && Options::wrap_groups) {
                        this->encode(as_array{size_});
                    }
                    (this->encode(args), ...);
                },
                tuple);
        } else if constexpr (IsTag<T>) {
            const auto &&tuple = to_tuple(value);
            static_assert(IsTag<std::remove_cvref_t<decltype(std::get<0>(tuple))>>, "Tag must be first element of tuple");
            encode_major_and_size(std::get<0>(tuple), static_cast<byte_type>(0xC0));
            aggregate_encode(detail::tuple_tail(tuple));
        } else {
            const auto &&tuple = to_tuple(value);
            aggregate_encode(std::forward<decltype(tuple)>(tuple));
        }
    }

    template <IsUntaggedTuple T> constexpr void encode(const T &value) { aggregate_encode(value); }

    template <typename C>
        requires(IsClassWithEncodingOverload<self_t, C>)
    constexpr void encode(const C &value) {
        constexpr bool has_transcode      = HasTranscodeMethod<self_t, C>;
        constexpr bool has_encode         = HasEncodeMethod<self_t, C>;
        constexpr bool has_free_encode    = HasEncodeFreeFunction<self_t, C>;
        constexpr bool has_free_transcode = HasTranscodeFreeFunction<self_t, C>;
        static_assert(
            has_transcode ^ has_encode ^ has_free_encode ^ has_free_transcode,
            "Class must have either (const) transcode or encode method, also do not forget to return value from the transcoding operation! "
            "Give friend access if members are private, i.e friend cbor::tags::Access (full namespace is required)");

        // Automatic tag encoding
        if constexpr (IsClassWithTagOverload<C>) {
            this->encode(detail::get_major_6_tag_from_class(value));
        }

        // For now, the only errors from encoding are exceptions. It will be caught by the operator(...) function, up top
        if constexpr (has_transcode) {
            [[maybe_unused]] auto result = Access::transcode(*this, value);
        } else if constexpr (has_encode) {
            [[maybe_unused]] auto result = Access::encode(*this, value);
        } else if constexpr (has_free_encode) {
            /* This requires an indirect call in order for some compilers to find the overload. */
            [[maybe_unused]] auto result = detail::adl_indirect_encode(*this, value);
        } else if constexpr (has_free_transcode) {
            /* Transcode does not require an indirect call, because no other methods exist with the same name (encode)*/
            [[maybe_unused]] auto result = transcode(*this, value);
        }
    }

    template <IsEnum T> constexpr void encode(T value) { this->encode(static_cast<std::underlying_type_t<T>>(value)); }

    constexpr void encode(float16_t value) {
        appender_.multi_append(data_, static_cast<byte_type>(0xF9), static_cast<byte_type>(value.value >> 8),
                               static_cast<byte_type>(value.value & 0xFF));
    }

    constexpr void encode(float value) {
        const auto bits = std::bit_cast<std::uint32_t>(value);
        appender_.multi_append(data_, static_cast<byte_type>(0xFA), static_cast<byte_type>(bits >> 24), static_cast<byte_type>(bits >> 16),
                               static_cast<byte_type>(bits >> 8), static_cast<byte_type>(bits));
    }

    constexpr void encode(double value) {
        const auto bits = std::bit_cast<std::uint64_t>(value);
        appender_.multi_append(data_, static_cast<byte_type>(0xFB), static_cast<byte_type>(bits >> 56), static_cast<byte_type>(bits >> 48),
                               static_cast<byte_type>(bits >> 40), static_cast<byte_type>(bits >> 32), static_cast<byte_type>(bits >> 24),
                               static_cast<byte_type>(bits >> 16), static_cast<byte_type>(bits >> 8), static_cast<byte_type>(bits));
    }

    constexpr void encode(bool value) { appender_(data_, value ? static_cast<byte_type>(0xF5) : static_cast<byte_type>(0xF4)); }

    constexpr void encode(std::nullptr_t) { appender_(data_, static_cast<byte_type>(0xF6)); }

    constexpr void encode(simple value) { encode_major_and_size(value.value, static_cast<byte_type>(0xE0)); }

    // Variadic friends only in c++26, must be public
    detail::appender<OutputBuffer> appender_;
    OutputBuffer                  &data_;

  private:
#if CBOR_TAGS_HAS_STD_REFLECTION
    template <typename Object> constexpr std::uint64_t named_map_pair_count(const Object &object) {
        using value_type = std::remove_cvref_t<Object>;
        return named_map_pair_count_impl(object, std::make_index_sequence<detail::aggregate_member_count<value_type>()>{});
    }

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

    template <typename Object> constexpr void encode_named_map(const Object &object) {
        encode_major_and_size(named_map_pair_count(object), static_cast<byte_type>(0xA0));
        encode_named_entries_for_root<Object>(object);
    }

    template <typename RootObject, typename Object> constexpr void encode_named_entries_for_root(const Object &object) {
        using value_type = std::remove_cvref_t<Object>;
        encode_named_entries_impl<RootObject>(object, std::make_index_sequence<detail::aggregate_member_count<value_type>()>{});
    }

    template <typename RootObject, typename Object, std::size_t... Is>
    constexpr void encode_named_entries_impl(const Object &object, std::index_sequence<Is...>) {
        (encode_named_member<RootObject, Object, Is>(object), ...);
    }

    template <typename RootObject, typename Object, std::size_t I> constexpr void encode_named_member(const Object &object) {
        using value_type  = std::remove_cvref_t<Object>;
        const auto  tuple = to_tuple(object);
        const auto &field = std::get<I>(tuple);
        using field_type  = std::remove_cvref_t<decltype(field)>;

        if constexpr (IsNamedGroupWrapper<field_type>) {
            encode_named_entries_for_root<RootObject>(field.value_);
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
                encode(key);
                encode(mapped_value);
            }
        } else if constexpr (IsOptional<field_type>) {
            if (field.has_value()) {
                encode(std::string_view{detail::aggregate_member_name<value_type, I>()});
                encode(*field);
            }
        } else {
            encode(std::string_view{detail::aggregate_member_name<value_type, I>()});
            encode(field);
        }
    }

    template <typename Object> constexpr bool named_key_matches_fixed_member(std::string_view key) const {
        using value_type = std::remove_cvref_t<Object>;
        return named_key_matches_fixed_member_impl<value_type>(key,
                                                               std::make_index_sequence<detail::aggregate_member_count<value_type>()>{});
    }

    template <typename Object, std::size_t... Is>
    constexpr bool named_key_matches_fixed_member_impl(std::string_view key, std::index_sequence<Is...>) const {
        return (named_key_matches_fixed_member<Object, Is>(key) || ...);
    }

    template <typename Object, std::size_t I> constexpr bool named_key_matches_fixed_member(std::string_view key) const {
        using value_type = std::remove_cvref_t<Object>;
        using tuple_type = std::remove_cvref_t<decltype(to_tuple(std::declval<value_type &>()))>;
        using field_type = std::remove_cvref_t<std::tuple_element_t<I, tuple_type>>;

        if constexpr (IsNamedGroupWrapper<field_type>) {
            return named_key_matches_fixed_member<named_group_value_t<field_type>>(key);
        } else if constexpr (IsNamedExtensionWrapper<field_type>) {
            return false;
        } else {
            return key == std::string_view{detail::aggregate_member_name<value_type, I>()};
        }
    }
#endif

    // Helper method to avoid code duplication
    template <typename Tuple> void aggregate_encode(Tuple &&tuple) {
        std::apply(
            [this](const auto &...args) {
                constexpr auto size_ = sizeof...(args);
                if constexpr (size_ > 1 && Options::wrap_groups) {
                    this->encode(as_array{size_});
                }
                (this->encode(args), ...);
            },
            std::forward<Tuple>(tuple));
    }
};

template <typename T> struct cbor_indefinite_encoder {
    template <typename U> constexpr void encode(as_indefinite<U> value) {
        auto &enc = detail::underlying<T>(this);
        if constexpr (IsBinaryString<U> && !IsBinaryHeader<U>) {
            enc.appender_(enc.data_, static_cast<typename T::byte_type>(get_indefinite_start<as_indefinite_byte_string>()));
            enc.encode(value.value_);
            enc.appender_(enc.data_, static_cast<typename T::byte_type>(0xFF));
        } else if constexpr (IsTextString<U> && !IsTextHeader<U>) {
            enc.appender_(enc.data_, static_cast<typename T::byte_type>(get_indefinite_start<as_indefinite_text_string>()));
            enc.encode(value.value_);
            enc.appender_(enc.data_, static_cast<typename T::byte_type>(0xFF));
        } else if constexpr (IsMap<U> && !IsMapHeader<U>) {
            enc.appender_(enc.data_, static_cast<typename T::byte_type>(get_indefinite_start<as_indefinite_map>()));
            for (const auto &[key, mapped_value] : value.value_) {
                enc.encode(key);
                enc.encode(mapped_value);
            }
            enc.appender_(enc.data_, static_cast<typename T::byte_type>(0xFF));
        } else if constexpr (IsArray<U> && !IsArrayHeader<U>) {
            enc.appender_(enc.data_, static_cast<typename T::byte_type>(get_indefinite_start<as_indefinite_array>()));
            for (const auto &item : value.value_) {
                enc.encode(item);
            }
            enc.appender_(enc.data_, static_cast<typename T::byte_type>(0xFF));
        } else {
            throw std::runtime_error("Invalid type for indefinite encoding");
        }
    }
};

template <typename T> struct cbor_variant_encoder {
    template <typename... Ts> constexpr void encode(const std::variant<Ts...> &value) {
        // encoding a variant is less strict than decoding
        std::visit([this](const auto &v) { detail::underlying<T>(this).encode(v); }, value);
    }
};

template <typename T> struct cbor_optional_encoder {
    template <typename U> constexpr void encode(const std::optional<U> &value) {
        if (value.has_value()) {
            detail::underlying<T>(this).encode(*value);
        } else {
            detail::underlying<T>(this).appender_(detail::underlying<T>(this).data_, static_cast<typename T::byte_type>(0xF6));
        }
    }
};

template <typename T> struct cbor_header_encoder {
    constexpr void encode(const as_array &value) {
        detail::underlying<T>(this).encode_major_and_size(value.size_, static_cast<typename T::byte_type>(0x80));
    }
    template <typename... Ts> constexpr void encode(const wrap_as_array<Ts...> &value) {
        detail::underlying<T>(this).encode_major_and_size(value.size_, static_cast<typename T::byte_type>(0x80));
        std::apply([this](const auto &...args) { (detail::underlying<T>(this).encode(args), ...); }, value.values_);
    }
    constexpr void encode(const as_map &value) {
        detail::underlying<T>(this).encode_major_and_size(value.size_, static_cast<typename T::byte_type>(0xA0));
    }
};

template <typename OutputBuffer> inline auto make_encoder(OutputBuffer &buffer) {
    return encoder<OutputBuffer, Options<default_expected, default_wrapping>, cbor_header_encoder, cbor_indefinite_encoder,
                   cbor_optional_encoder, cbor_variant_encoder>(buffer);
}
} // namespace cbor::tags
