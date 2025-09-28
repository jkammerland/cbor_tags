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
template <typename T> struct cbor_integer_encoder;
template <typename T> struct cbor_tag_encoder;
template <typename T> struct cbor_string_encoder;
template <typename T> struct cbor_array_encoder;
template <typename T> struct cbor_map_encoder;
template <typename T> struct cbor_tuple_encoder;
template <typename T> struct cbor_aggregate_encoder;
template <typename T> struct cbor_class_encoder;
template <typename T> struct cbor_float_encoder;
template <typename T> struct cbor_simple_encoder;

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

    // Variadic friends only in c++26, must be public
    detail::appender<OutputBuffer> appender_;
    OutputBuffer                  &data_;
};

template <typename T> struct cbor_integer_encoder {
    template <IsUnsigned U> constexpr void encode(U value) {
        auto &self = detail::underlying<T>(this);
        self.encode_major_and_size(value, static_cast<typename T::byte_type>(0x00));
    }

    template <IsSigned U> constexpr void encode(U value) {
        auto &self = detail::underlying<T>(this);
        if (value >= 0) {
            self.encode_major_and_size(static_cast<std::uint64_t>(value), static_cast<typename T::byte_type>(0x00));
        } else {
            self.encode_major_and_size(static_cast<std::uint64_t>(-1 - value), static_cast<typename T::byte_type>(0x20));
        }
    }

    constexpr void encode(negative value) {
        auto &self = detail::underlying<T>(this);
        self.encode_major_and_size(static_cast<std::uint64_t>(value.value - 1), static_cast<typename T::byte_type>(0x20));
    }

    constexpr void encode(integer value) {
        auto &self = detail::underlying<T>(this);
        if (value.is_negative) {
            self.encode(negative{value.value});
        } else {
            self.encode(value.value);
        }
    }

    template <IsEnum U> constexpr void encode(U value) {
        auto &self = detail::underlying<T>(this);
        self.encode(static_cast<std::underlying_type_t<U>>(value));
    }
};

template <typename T> struct cbor_tag_encoder {
    template <std::uint64_t N> constexpr void encode(static_tag<N>) {
        auto &self = detail::underlying<T>(this);
        self.encode_major_and_size(N, static_cast<typename T::byte_type>(0xC0));
    }

    template <IsUnsigned U> constexpr void encode(dynamic_tag<U> value) {
        auto &self = detail::underlying<T>(this);
        self.encode_major_and_size(value.cbor_tag, static_cast<typename T::byte_type>(0xC0));
    }

    template <IsTaggedTuple Tuple> constexpr void encode(const Tuple &value) {
        auto &self = detail::underlying<T>(this);
        self.encode_major_and_size(value.first, static_cast<typename T::byte_type>(0xC0));
        self.encode(value.second);
    }
};

template <typename T> struct cbor_string_encoder {
    template <IsString String> constexpr void encode(const String &value) {
        auto &self = detail::underlying<T>(this);
        self.encode_major_and_size(value.size(), static_cast<typename T::byte_type>(get_major_3_bit_tag<String>()));
        self.appender_(self.data_, value);
    }
};

template <typename T> struct cbor_array_encoder {
    template <IsArray Array> constexpr void encode(const Array &value) {
        auto &self = detail::underlying<T>(this);
        self.encode_major_and_size(value.size(), static_cast<typename T::byte_type>(0x80));
        for (const auto &item : value) {
            self.encode(item);
        }
    }
};

template <typename T> struct cbor_map_encoder {
    template <IsMap Map> constexpr void encode(const Map &value) {
        auto &self = detail::underlying<T>(this);
        self.encode_major_and_size(value.size(), static_cast<typename T::byte_type>(0xA0));
        for (const auto &[key, mapped_value] : value) {
            self.encode(key);
            self.encode(mapped_value);
        }
    }
};

template <typename T> struct cbor_aggregate_encoder {
    template <typename Tuple> constexpr void aggregate_encode(Tuple &&tuple) {
        auto &self = detail::underlying<T>(this);
        std::apply(
            [&self](const auto &...args) {
                constexpr auto size_ = sizeof...(args);
                if constexpr (size_ > 1 && T::options::wrap_groups) {
                    self.encode(as_array{size_});
                }
                (self.encode(args), ...);
            },
            std::forward<Tuple>(tuple));
    }

    template <typename Value>
        requires(IsAggregate<Value> && !IsClassWithEncodingOverload<T, Value>)
    constexpr void encode(const Value &value) {
        auto &self = detail::underlying<T>(this);
        if constexpr (HasInlineTag<Value>) {
            const auto &&tuple = to_tuple(value);
            self.encode_major_and_size(Value::cbor_tag, static_cast<typename T::byte_type>(0xC0));
            std::apply(
                [&self](const auto &...args) {
                    constexpr auto   size_        = sizeof...(args);
                    constexpr size_t has_tag_or_1 = std::conditional_t < HasStaticTag<Value> || HasDynamicTag<Value>,
                                     std::integral_constant<size_t, 2>, std::integral_constant < size_t, 1 >> ::value;
                    if constexpr (size_ > has_tag_or_1 && T::options::wrap_groups) {
                        self.encode(as_array{size_});
                    }
                    (self.encode(args), ...);
                },
                tuple);
        } else if constexpr (IsTag<Value>) {
            const auto &&tuple = to_tuple(value);
            static_assert(IsTag<std::remove_cvref_t<decltype(std::get<0>(tuple))>>, "Tag must be first element of tuple");
            self.encode_major_and_size(std::get<0>(tuple), static_cast<typename T::byte_type>(0xC0));
            self.aggregate_encode(detail::tuple_tail(tuple));
        } else {
            const auto &&tuple = to_tuple(value);
            self.aggregate_encode(std::forward<decltype(tuple)>(tuple));
        }
    }
};

template <typename T> struct cbor_tuple_encoder {
    template <IsUntaggedTuple Tuple> constexpr void encode(const Tuple &value) { detail::underlying<T>(this).aggregate_encode(value); }
};

template <typename T> struct cbor_class_encoder {
    template <typename C>
        requires(IsClassWithEncodingOverload<T, C>)
    constexpr void encode(const C &value) {
        auto          &self               = detail::underlying<T>(this);
        constexpr bool has_transcode      = HasTranscodeMethod<T, C>;
        constexpr bool has_encode         = HasEncodeMethod<T, C>;
        constexpr bool has_free_encode    = HasEncodeFreeFunction<T, C>;
        constexpr bool has_free_transcode = HasTranscodeFreeFunction<T, C>;
        static_assert(
            has_transcode ^ has_encode ^ has_free_encode ^ has_free_transcode,
            "Class must have either (const) transcode or encode method, also do not forget to return value from the transcoding operation! "
            "Give friend access if members are private, i.e friend cbor::tags::Access (full namespace is required)");

        if constexpr (IsClassWithTagOverload<C>) {
            self.encode(detail::get_major_6_tag_from_class(value));
        }

        if constexpr (has_transcode) {
            [[maybe_unused]] auto result = Access::transcode(self, value);
        } else if constexpr (has_encode) {
            [[maybe_unused]] auto result = Access::encode(self, value);
        } else if constexpr (has_free_encode) {
            [[maybe_unused]] auto result = detail::adl_indirect_encode(self, value);
        } else if constexpr (has_free_transcode) {
            [[maybe_unused]] auto result = transcode(self, value);
        }
    }
};

template <typename T> struct cbor_float_encoder {
    constexpr void encode(float16_t value) {
        auto &self = detail::underlying<T>(this);
        self.appender_.multi_append(self.data_, static_cast<typename T::byte_type>(0xF9),
                                    static_cast<typename T::byte_type>(value.value >> 8),
                                    static_cast<typename T::byte_type>(value.value & 0xFF));
    }

    constexpr void encode(float value) {
        auto      &self = detail::underlying<T>(this);
        const auto bits = std::bit_cast<std::uint32_t>(value);
        self.appender_.multi_append(self.data_, static_cast<typename T::byte_type>(0xFA), static_cast<typename T::byte_type>(bits >> 24),
                                    static_cast<typename T::byte_type>(bits >> 16), static_cast<typename T::byte_type>(bits >> 8),
                                    static_cast<typename T::byte_type>(bits));
    }

    constexpr void encode(double value) {
        auto      &self = detail::underlying<T>(this);
        const auto bits = std::bit_cast<std::uint64_t>(value);
        self.appender_.multi_append(self.data_, static_cast<typename T::byte_type>(0xFB), static_cast<typename T::byte_type>(bits >> 56),
                                    static_cast<typename T::byte_type>(bits >> 48), static_cast<typename T::byte_type>(bits >> 40),
                                    static_cast<typename T::byte_type>(bits >> 32), static_cast<typename T::byte_type>(bits >> 24),
                                    static_cast<typename T::byte_type>(bits >> 16), static_cast<typename T::byte_type>(bits >> 8),
                                    static_cast<typename T::byte_type>(bits));
    }
};

template <typename T> struct cbor_simple_encoder {
    constexpr void encode(bool value) {
        auto &self = detail::underlying<T>(this);
        self.appender_(self.data_, value ? static_cast<typename T::byte_type>(0xF5) : static_cast<typename T::byte_type>(0xF4));
    }

    constexpr void encode(std::nullptr_t) {
        auto &self = detail::underlying<T>(this);
        self.appender_(self.data_, static_cast<typename T::byte_type>(0xF6));
    }

    constexpr void encode(simple value) {
        auto &self = detail::underlying<T>(this);
        self.encode_major_and_size(value.value, static_cast<typename T::byte_type>(0xE0));
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
    return encoder<OutputBuffer, Options<default_expected, default_wrapping>, cbor_header_encoder, cbor_optional_encoder,
                   cbor_variant_encoder, cbor_tag_encoder, cbor_integer_encoder, cbor_string_encoder, cbor_array_encoder, cbor_map_encoder,
                   cbor_aggregate_encoder, cbor_tuple_encoder, cbor_class_encoder, cbor_float_encoder, cbor_simple_encoder>(buffer);
}
} // namespace cbor::tags
