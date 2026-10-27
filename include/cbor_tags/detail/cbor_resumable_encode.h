#pragma once

#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/detail/cbor_resumable_state.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace cbor::tags::detail {

template <typename T> struct is_resumable_reference : std::false_type {};
template <typename T> struct is_resumable_reference<std::reference_wrapper<T>> : std::true_type {};
template <typename T> decltype(auto) resumable_unwrap(T &value) {
    if constexpr (is_resumable_reference<std::remove_cvref_t<T>>::value)
        return value.get();
    else
        return (value);
}

template <typename T> struct is_wrap_as_array : std::false_type {};
template <typename... T> struct is_wrap_as_array<wrap_as_array<T...>> : std::true_type {};

template <typename Tuple> struct resumable_owned_array {
    Tuple         values_;
    std::uint64_t size_;
};
template <typename T> struct is_resumable_owned_array : std::false_type {};
template <typename Tuple> struct is_resumable_owned_array<resumable_owned_array<Tuple>> : std::true_type {};

template <typename Tuple, std::size_t... I> consteval bool resumable_tuple_references(std::index_sequence<I...>) {
    using type = std::remove_reference_t<Tuple>;
    return ((std::is_lvalue_reference_v<std::tuple_element_t<I, type>> ||
             is_resumable_reference<std::remove_cvref_t<std::tuple_element_t<I, type>>>::value) &&
            ...);
}

template <typename Tuple> consteval bool resumable_tuple_references() {
    return resumable_tuple_references<Tuple>(std::make_index_sequence<std::tuple_size_v<std::remove_reference_t<Tuple>>>{});
}

template <typename Wrapper> consteval bool safe_resumable_array_wrapper() {
    using tuple = decltype(std::declval<Wrapper>().values_);
    if constexpr (std::is_lvalue_reference_v<tuple>)
        return true;
    else
        return resumable_tuple_references<tuple>();
}

template <typename T> auto capture_resumable_output(T &&value) {
    using type = std::remove_cvref_t<T>;
    if constexpr (is_wrap_as_array<type>::value) {
        if constexpr (std::is_lvalue_reference_v<T>) {
            static_assert(safe_resumable_array_wrapper<type>(), "an lvalue array wrapper must refer to stable lvalues");
            return std::cref(value);
        } else if constexpr (std::is_lvalue_reference_v<decltype(std::declval<type>().values_)>) {
            return type{std::forward<T>(value)};
        } else {
            auto captured =
                std::apply([](auto &&...items) { return std::tuple{capture_resumable_output(std::forward<decltype(items)>(items))...}; },
                           std::move(value.values_));
            return resumable_owned_array<decltype(captured)>{std::move(captured), value.size_};
        }
    } else if constexpr (std::is_lvalue_reference_v<T> && (IsArrayRangeWrapper<type> || IsMapRangeWrapper<type> ||
                                                           IsStringRangeWrapper<type> || IsAnyBoundedSizeWrapper<type>)) {
        return std::ref(value);
    } else if constexpr (std::is_lvalue_reference_v<T>) {
        return std::cref(value);
    } else {
        return type{std::forward<T>(value)};
    }
}

template <typename Context, typename T> auto resumable_adl_encode(Context &context, const T &value) -> decltype(encode(context, value)) {
    return encode(context, value);
}
template <typename Context, typename T>
auto resumable_adl_transcode(Context &context, T &&value) -> decltype(transcode(context, std::forward<T>(value))) {
    return transcode(context, std::forward<T>(value));
}

template <typename Options, template <typename> typename... Codecs>
struct resumable_encoder_context : Codecs<resumable_encoder_context<Options, Codecs...>>... {
    using self_type = resumable_encoder_context;
    using options   = Options;
    using task_type = resumable_task<status_code>;
    resumable_io &io;

    explicit resumable_encoder_context(resumable_io &state) : io(state) {}

    [[nodiscard]] resumable_write_byte byte(std::byte value) noexcept { return {.io = io, .value = value}; }

    task_type argument(std::uint8_t major, std::uint64_t value) {
        const auto header = encode_cbor_major_argument_header(value, static_cast<std::byte>(major));
        for (std::size_t index = 0; index < header.size; ++index)
            co_await byte(header.bytes[index]);
        co_return status_code::success;
    }

    template <std::size_t I = 0, typename Tuple> task_type members(Tuple &tuple) {
        if (io.terminal && io.status == status_code::cancelled)
            co_return status_code::cancelled;
        if constexpr (I < std::tuple_size_v<std::remove_cvref_t<Tuple>>) {
            auto status = co_await encode(resumable_unwrap(std::get<I>(tuple)));
            if (status != status_code::success)
                co_return status;
            co_return co_await members<I + 1>(tuple);
        } else {
            co_return status_code::success;
        }
    }

    template <typename Tuple> task_type group(const Tuple &tuple) {
        constexpr auto count = std::tuple_size_v<std::remove_cvref_t<Tuple>>;
        if constexpr (count > 1U && Options::wrap_groups) {
            auto status = co_await argument(0x80U, count);
            if (status != status_code::success)
                co_return status;
        }
        co_return co_await members(tuple);
    }

    template <typename Tuple> task_type captured_members(Tuple tuple) { co_return co_await members(tuple); }

    template <typename... Args> task_type operator()(Args &&...args) {
        auto tuple = std::tuple{capture_resumable_output(std::forward<Args>(args))...};
        return captured_members(std::move(tuple));
    }

    template <typename T>
        requires(IsArrayRangeWrapper<T> || IsMapRangeWrapper<T> || IsStringRangeWrapper<T>)
    task_type encode(T &value) {
        return encode_range(value);
    }

    template <typename T> task_type encode_range(T &value) {
        if (io.terminal && io.status == status_code::cancelled)
            co_return status_code::cancelled;
        auto &range      = value.range_;
        using range_type = std::remove_reference_t<decltype(range)>;
        if constexpr (IsArrayRangeWrapper<T> || IsMapRangeWrapper<T>) {
            constexpr auto major = IsMapRangeWrapper<T> ? 0xa0U : 0x80U;
            if constexpr (std::ranges::sized_range<range_type>) {
                auto status = co_await argument(major, std::ranges::size(range));
                if (status != status_code::success)
                    co_return status;
            } else {
                co_await byte(IsMapRangeWrapper<T> ? std::byte{0xbf} : std::byte{0x9f});
            }
            for (auto &&item : range) {
                if constexpr (IsMapRangeWrapper<T>) {
                    auto status = co_await encode(pair_first(item));
                    if (status != status_code::success)
                        co_return status;
                    status = co_await encode(pair_second(item));
                    if (status != status_code::success)
                        co_return status;
                } else if constexpr (CborRangeComponent<decltype(item)>) {
                    auto status = co_await encode(item);
                    if (status != status_code::success)
                        co_return status;
                } else {
                    auto materialized = static_cast<std::ranges::range_value_t<range_type>>(item);
                    auto status       = co_await encode(materialized);
                    if (status != status_code::success)
                        co_return status;
                }
            }
            if constexpr (!std::ranges::sized_range<range_type>)
                co_await byte(std::byte{0xff});
        } else {
            constexpr auto major = IsTstrRangeWrapper<T> ? 0x60U : 0x40U;
            if constexpr (std::ranges::sized_range<range_type>) {
                auto status = co_await argument(major, std::ranges::size(range));
                if (status != status_code::success)
                    co_return status;
                for (auto item : range)
                    co_await byte(static_cast<std::byte>(cbor_byte_to_u8(item)));
            } else {
                if (value.chunk_size_ == 0U)
                    co_return status_code::unsupported_operation;
                co_await byte(IsTstrRangeWrapper<T> ? std::byte{0x7f} : std::byte{0x5f});
                auto       current = std::ranges::begin(range);
                const auto end     = std::ranges::end(range);
                while (current != end) {
                    std::vector<std::byte> chunk;
                    while (current != end && chunk.size() < value.chunk_size_) {
                        const std::ranges::range_value_t<range_type> item = *current;
                        chunk.push_back(static_cast<std::byte>(cbor_byte_to_u8(item)));
                        ++current;
                    }
                    auto status = co_await argument(major, chunk.size());
                    if (status != status_code::success)
                        co_return status;
                    for (auto item : chunk)
                        co_await byte(item);
                }
                co_await byte(std::byte{0xff});
            }
        }
        co_return status_code::success;
    }

    template <typename T>
        requires IsAnyBoundedSizeWrapper<T>
    task_type encode(T &value) {
        return encode_bounded(value);
    }

    template <typename T> task_type encode_bounded(T &value) {
        if (io.terminal && io.status == status_code::cancelled)
            co_return status_code::cancelled;
        using type    = std::remove_cvref_t<T>;
        auto &wrapped = value.value();
        auto &target  = [&]() -> auto  &{
            using wrapped_type = std::remove_cvref_t<decltype(wrapped)>;
            if constexpr (IsIndefiniteWrapper<wrapped_type>)
                return wrapped.value_;
            else if constexpr (IsArrayRangeWrapper<wrapped_type> || IsMapRangeWrapper<wrapped_type> || IsStringRangeWrapper<wrapped_type>)
                return wrapped.range_;
            else
                return wrapped;
        }();
        static_assert(std::ranges::sized_range<decltype(target)>, "bounded resumable encoding requires a sized range");
        const auto size = std::ranges::size(target);
        const auto min  = [&] {
            if constexpr (IsDynamicBoundedSizeWrapper<type>)
                return value.min_size();
            else
                return type::min_size;
        }();
        const auto max = [&] {
            if constexpr (IsDynamicBoundedSizeWrapper<type>)
                return value.max_size();
            else
                return type::max_size;
        }();
        if (bounded_size_status(size, min, max) != status_code::success)
            co_return status_code::size_limit_exceeded;
        co_return co_await encode(wrapped);
    }

    template <typename T>
    static constexpr bool has_codec = (requires(Codecs<self_type> &codec, const T &value) {
                                          { codec.resumable_encode(value) } -> std::same_as<task_type>;
                                      } || ...) || (requires(Codecs<self_type> &codec, const T &value) {
                                          { codec.encode(value) } -> std::same_as<task_type>;
                                      } || ...);

    template <typename T>
    static constexpr bool has_sync_codec = (requires(Codecs<self_type> &codec, const T &value) {
        { codec.encode(value) } -> std::same_as<void>;
    } || ...);

    template <std::size_t I = 0, typename T> task_type invoke_codec(const T &value) {
        if constexpr (I < sizeof...(Codecs)) {
            using codec_type = std::tuple_element_t<I, std::tuple<Codecs<self_type>...>>;
            if constexpr (requires(codec_type &codec) {
                              { codec.resumable_encode(value) } -> std::same_as<task_type>;
                          }) {
                co_return co_await static_cast<codec_type &>(*this).resumable_encode(value);
            } else if constexpr (requires(codec_type &codec) {
                                     { codec.encode(value) } -> std::same_as<task_type>;
                                 }) {
                co_return co_await static_cast<codec_type &>(*this).encode(value);
            } else {
                co_return co_await invoke_codec<I + 1>(value);
            }
        } else {
            co_return status_code::unsupported_operation;
        }
    }

    template <typename T> task_type encode(const T &value) {
        if (io.terminal && io.status == status_code::cancelled)
            co_return status_code::cancelled;
        using type = std::remove_cvref_t<T>;
        if constexpr (has_codec<type>) {
            co_return co_await invoke_codec(value);
        } else if constexpr (has_sync_codec<type>) {
            static_assert(!has_sync_codec<type>, "synchronous codec requires an explicit resumable encode hook");
        } else if constexpr (requires {
                                 { resumable_encode(*this, value) } -> std::same_as<task_type>;
                             }) {
            if constexpr (IsClassWithTagOverload<type>) {
                auto status = co_await argument(0xc0U, static_cast<std::uint64_t>(get_major_6_tag_from_class(value)));
                if (status != status_code::success)
                    co_return status;
            }
            co_return co_await resumable_encode(*this, value);
        } else if constexpr (requires {
                                 { resumable_transcode(*this, value) } -> std::same_as<task_type>;
                             }) {
            if constexpr (IsClassWithTagOverload<type>) {
                auto status = co_await argument(0xc0U, static_cast<std::uint64_t>(get_major_6_tag_from_class(value)));
                if (status != status_code::success)
                    co_return status;
            }
            co_return co_await resumable_transcode(*this, value);
        } else if constexpr (
            requires {
                { Access::encode(*this, value) } -> std::same_as<task_type>;
            } ||
            requires {
                { Access::transcode(*this, value) } -> std::same_as<task_type>;
            } ||
            requires {
                { resumable_adl_encode(*this, value) } -> std::same_as<task_type>;
            } ||
            requires {
                { resumable_adl_transcode(*this, value) } -> std::same_as<task_type>;
            }) {
            if constexpr (IsClassWithTagOverload<type>) {
                auto status = co_await argument(0xc0U, static_cast<std::uint64_t>(get_major_6_tag_from_class(value)));
                if (status != status_code::success)
                    co_return status;
            }
            if constexpr (requires {
                              { Access::encode(*this, value) } -> std::same_as<task_type>;
                          }) {
                co_return co_await Access::encode(*this, value);
            } else if constexpr (requires {
                                     { Access::transcode(*this, value) } -> std::same_as<task_type>;
                                 }) {
                co_return co_await Access::transcode(*this, value);
            } else if constexpr (requires {
                                     { resumable_adl_encode(*this, value) } -> std::same_as<task_type>;
                                 }) {
                co_return co_await resumable_adl_encode(*this, value);
            } else {
                co_return co_await resumable_adl_transcode(*this, value);
            }
        } else if constexpr (std::same_as<type, bool>) {
            co_await byte(value ? std::byte{0xf5} : std::byte{0xf4});
        } else if constexpr (std::same_as<type, std::nullptr_t>) {
            co_await byte(std::byte{0xf6});
        } else if constexpr (std::same_as<type, simple>) {
            co_return co_await argument(0xe0U, value.value);
        } else if constexpr (std::same_as<type, integer>) {
            co_return co_await argument(value.is_negative ? 0x20U : 0x00U, value.is_negative ? value.value - 1U : value.value);
        } else if constexpr (std::same_as<type, negative>) {
            co_return co_await argument(0x20U, value.value - 1U);
        } else if constexpr (std::is_enum_v<type>) {
            co_return co_await encode(static_cast<std::underlying_type_t<type>>(value));
        } else if constexpr (IsUnsigned<type>) {
            co_return co_await argument(0x00U, static_cast<std::uint64_t>(value));
        } else if constexpr (IsSigned<type>) {
            if (value < 0)
                co_return co_await argument(0x20U, static_cast<std::uint64_t>(-1 - value));
            co_return co_await argument(0x00U, static_cast<std::uint64_t>(value));
        } else if constexpr (is_static_tag_t<type>::value || is_dynamic_tag_t<type>) {
            co_return co_await argument(0xc0U, static_cast<std::uint64_t>(value));
        } else if constexpr (std::same_as<type, float16_t> || std::same_as<type, float> || std::same_as<type, double>) {
            constexpr auto width = std::same_as<type, float16_t> ? 2U : sizeof(type);
            std::uint64_t  bits{};
            if constexpr (std::same_as<type, float16_t>)
                bits = value.value;
            else if constexpr (std::same_as<type, float>)
                bits = std::bit_cast<std::uint32_t>(value);
            else
                bits = std::bit_cast<std::uint64_t>(value);
            co_await byte(static_cast<std::byte>(width == 2U ? 0xf9U : width == 4U ? 0xfaU : 0xfbU));
            for (auto index = width; index != 0U; --index)
                co_await byte(static_cast<std::byte>(bits >> ((index - 1U) * 8U)));
        } else if constexpr (std::same_as<type, as_array>) {
            co_return co_await argument(0x80U, value.size_);
        } else if constexpr (std::same_as<type, as_map>) {
            co_return co_await argument(0xa0U, value.size_);
        } else if constexpr (is_wrap_as_array<type>::value || is_resumable_owned_array<type>::value) {
            auto status = co_await argument(0x80U, value.size_);
            if (status != status_code::success)
                co_return status;
            co_return co_await members(value.values_);
        } else if constexpr (std::same_as<type, as_indefinite_array>) {
            co_await byte(std::byte{0x9f});
        } else if constexpr (std::same_as<type, as_indefinite_map>) {
            co_await byte(std::byte{0xbf});
        } else if constexpr (std::same_as<type, as_indefinite_text_string>) {
            co_await byte(std::byte{0x7f});
        } else if constexpr (std::same_as<type, as_indefinite_byte_string>) {
            co_await byte(std::byte{0x5f});
        } else if constexpr (std::same_as<type, end_array> || std::same_as<type, end_map> || std::same_as<type, end_string> ||
                             std::same_as<type, indefinite_break>) {
            co_await byte(std::byte{0xff});
        } else if constexpr (IsArrayRangeWrapper<type> || IsMapRangeWrapper<type> || IsStringRangeWrapper<type>) {
            co_return co_await encode_range(value);
        } else if constexpr (IsAnyBoundedSizeWrapper<type>) {
            co_return co_await encode_bounded(value);
        } else if constexpr (IsIndefiniteWrapper<type>) {
            using wrapped_type = std::remove_cvref_t<decltype(value.value_)>;
            if constexpr (IsTextString<wrapped_type> || IsBinaryString<wrapped_type>) {
                co_await byte(IsTextString<wrapped_type> ? std::byte{0x7f} : std::byte{0x5f});
                auto status = co_await encode(value.value_);
                if (status != status_code::success)
                    co_return status;
            } else if constexpr (IsMap<wrapped_type>) {
                co_await byte(std::byte{0xbf});
                for (const auto &entry : value.value_) {
                    auto status = co_await encode(pair_first(entry));
                    if (status != status_code::success)
                        co_return status;
                    status = co_await encode(pair_second(entry));
                    if (status != status_code::success)
                        co_return status;
                }
            } else if constexpr (IsArray<wrapped_type>) {
                co_await byte(std::byte{0x9f});
                for (const auto &item : value.value_) {
                    auto status = co_await encode(item);
                    if (status != status_code::success)
                        co_return status;
                }
            } else {
                co_return status_code::unsupported_operation;
            }
            co_await byte(std::byte{0xff});
        } else if constexpr (IsOptional<type>) {
            if (value.has_value())
                co_return co_await encode(value.value());
            co_await byte(std::byte{0xf6});
        } else if constexpr (IsVariant<type>) {
            auto child = variant_visit([this](const auto &item) { return this->encode(item); }, value);
            co_return co_await std::move(child);
        } else if constexpr (IsString<type>) {
            static_assert(std::ranges::sized_range<const type>, "definite string encoding requires a sized range");
            auto status = co_await argument(IsTextString<type> ? 0x60U : 0x40U, std::ranges::size(value));
            if (status != status_code::success)
                co_return status;
            for (auto item : value)
                co_await byte(static_cast<std::byte>(cbor_byte_to_u8(item)));
        } else if constexpr (IsMap<type>) {
            static_assert(std::ranges::sized_range<const type>, "definite map encoding requires a sized range");
            auto status = co_await argument(0xa0U, std::ranges::size(value));
            if (status != status_code::success)
                co_return status;
            for (const auto &entry : value) {
                status = co_await encode(pair_first(entry));
                if (status != status_code::success)
                    co_return status;
                status = co_await encode(pair_second(entry));
                if (status != status_code::success)
                    co_return status;
            }
        } else if constexpr (IsArray<type>) {
            static_assert(std::ranges::sized_range<const type>, "definite array encoding requires a sized range");
            auto status = co_await argument(0x80U, std::ranges::size(value));
            if (status != status_code::success)
                co_return status;
            for (const auto &item : value) {
                status = co_await encode(item);
                if (status != status_code::success)
                    co_return status;
            }
        } else if constexpr (IsTaggedTuple<type>) {
            auto status = co_await encode(std::get<0>(value));
            if (status != status_code::success)
                co_return status;
            auto fields = tuple_tail(value);
            co_return co_await group(fields);
        } else if constexpr (IsUntaggedTuple<type>) {
            co_return co_await group(value);
        } else if constexpr (IsAggregate<type>) {
            static_assert(!HasRawEncodeMethod<self_type, type> && !HasRawTranscodeMethod<self_type, type> &&
                              !HasRawEncodeFreeFunction<self_type, type> && !HasRawTranscodeFreeFunction<self_type, type>,
                          "synchronous customizations require an explicit resumable_encode or resumable_transcode hook");
            auto fields = to_tuple(value);
            if constexpr (HasInlineTag<type>) {
                auto status = co_await argument(0xc0U, type::cbor_tag);
                if (status != status_code::success)
                    co_return status;
                co_return co_await group(fields);
            } else if constexpr (IsTag<type>) {
                auto status = co_await encode(std::get<0>(fields));
                if (status != status_code::success)
                    co_return status;
                auto tail = tuple_tail(fields);
                co_return co_await group(tail);
            } else {
                co_return co_await group(fields);
            }
        } else {
            static_assert(always_false<type>::value, "unsupported type: provide an explicit resumable_encode hook");
        }
        co_return status_code::success;
    }
};

template <typename Options, template <typename> typename... Codecs, typename Tuple>
resumable_task<status_code> resumable_encode_root(resumable_io &io, Tuple values) {
    resumable_encoder_context<Options, Codecs...> encoder{io};
    co_return co_await encoder.members(values);
}

} // namespace cbor::tags::detail
