#pragma once

#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/detail/cbor_resumable_encode.h"
#include "cbor_tags/detail/cbor_variant_dispatch.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace cbor::tags::detail {

template <typename T> auto capture_resumable_input(T &&value) {
    using type = std::remove_cvref_t<T>;
    if constexpr (is_wrap_as_array<type>::value) {
        static_assert(safe_resumable_array_wrapper<type>(), "resumable decode array wrappers must refer to stable lvalues");
    }
    if constexpr (std::is_lvalue_reference_v<T>) {
        return std::ref(value);
    } else {
        if constexpr (IsTuple<type> && !is_wrap_as_array<type>::value) {
            static_assert(resumable_tuple_references<type>(), "resumable decode tuple destinations must refer to stable lvalues");
        } else if constexpr (IsAnyBoundedSizeWrapper<type>) {
            using wrapped_type = std::remove_cvref_t<decltype(value.value())>;
            static_assert(std::is_lvalue_reference_v<decltype(std::declval<type &&>().value())> || IsIndefiniteWrapper<wrapped_type>,
                          "resumable decode bounded destinations must refer to stable lvalues");
        } else {
            static_assert(IsIndefiniteWrapper<type> || is_wrap_as_array<type>::value || is_static_tag_t<type>::value ||
                              is_dynamic_tag_t<type> || std::same_as<type, as_array> || std::same_as<type, as_map>,
                          "resumable decode destinations must be lvalues; temporary reference wrappers are allowed");
        }
        return type{std::forward<T>(value)};
    }
}

template <typename Context, typename T>
auto resumable_adl_decode(Context &context, T &&value) -> decltype(decode(context, std::forward<T>(value))) {
    return decode(context, std::forward<T>(value));
}

struct resumable_header {
    major_type major;
    std::byte  info;
};

inline resumable_task<resumable_header> resumable_read_header(resumable_io &io) {
    const auto initial = std::to_integer<std::uint8_t>(co_await resumable_read_byte{io});
    co_return resumable_header{.major = static_cast<major_type>(initial >> 5U), .info = static_cast<std::byte>(initial & 0x1fU)};
}

inline resumable_task<std::uint64_t> resumable_read_argument(resumable_io &io, std::byte additional) {
    const auto info = std::to_integer<std::uint8_t>(additional);
    if (!is_valid_cbor_argument_info(info))
        throw decode_status_exception{status_code::invalid_additional_info};
    if (info < 24U)
        co_return info;
    std::uint64_t value{};
    const auto    count = cbor_argument_payload_size(info);
    for (std::uint8_t index = 0; index < count; ++index) {
        value = (value << 8U) | std::to_integer<std::uint8_t>(co_await resumable_read_byte{io});
    }
    co_return value;
}

[[nodiscard]] inline bool resumable_break(resumable_header header) noexcept {
    return header.major == major_type::Simple && header.info == std::byte{31};
}

template <typename Options, template <typename> typename... Codecs>
struct resumable_decoder_context : Codecs<resumable_decoder_context<Options, Codecs...>>... {
    using self_type         = resumable_decoder_context;
    using options           = Options;
    using task_type         = resumable_task<status_code>;
    using input_buffer_type = resumable_io;
    resumable_io                   &io;
    std::optional<resumable_header> pending_header;

    explicit resumable_decoder_context(resumable_io &state) : io(state) {}
    [[nodiscard]] resumable_read_byte              byte() noexcept { return {io}; }
    [[nodiscard]] resumable_task<resumable_header> header() {
        if (pending_header) {
            const auto result = *pending_header;
            pending_header.reset();
            co_return result;
        }
        co_return co_await resumable_read_header(io);
    }
    [[nodiscard]] resumable_task<std::uint64_t> argument(std::byte info) { return resumable_read_argument(io, info); }

    template <std::size_t I = 0, typename Tuple> task_type members(Tuple &tuple) {
        if (io.terminal && io.status == status_code::cancelled)
            co_return status_code::cancelled;
        if constexpr (I < std::tuple_size_v<std::remove_cvref_t<Tuple>>) {
            auto status = co_await decode(resumable_unwrap(std::get<I>(tuple)));
            if (status != status_code::success)
                co_return status;
            co_return co_await members<I + 1>(tuple);
        } else {
            co_return status_code::success;
        }
    }

    template <typename Tuple> task_type group(Tuple &tuple) {
        constexpr auto count = std::tuple_size_v<std::remove_cvref_t<Tuple>>;
        if constexpr (count > 1U && Options::wrap_groups) {
            const auto h = co_await header();
            if (h.major != major_type::Array)
                co_return status_code::no_match_for_array_on_buffer;
            if (h.info == std::byte{31})
                co_return status_code::unexpected_group_size;
            const auto size = co_await argument(h.info);
            if (size != count)
                co_return status_code::unexpected_group_size;
        }
        co_return co_await members(tuple);
    }

    template <typename Tuple> task_type group_header(Tuple &tuple, resumable_header h) {
        constexpr auto count = std::tuple_size_v<std::remove_cvref_t<Tuple>>;
        if constexpr (count == 0U) {
            co_return status_code::unexpected_group_size;
        } else if constexpr (count > 1U && Options::wrap_groups) {
            if (h.major != major_type::Array)
                co_return status_code::no_match_for_array_on_buffer;
            if (h.info == std::byte{31})
                co_return status_code::unexpected_group_size;
            const auto size = co_await argument(h.info);
            if (size != count)
                co_return status_code::unexpected_group_size;
            co_return co_await members(tuple);
        } else {
            auto status = co_await decode_header(resumable_unwrap(std::get<0>(tuple)), h);
            if (status != status_code::success)
                co_return status;
            auto tail = tuple_tail(tuple);
            co_return co_await members(tail);
        }
    }

    template <typename Tuple> task_type captured_members(Tuple tuple) { co_return co_await members(tuple); }

    template <typename... Args> task_type operator()(Args &&...args) {
        auto tuple = std::tuple{capture_resumable_input(std::forward<Args>(args))...};
        return captured_members(std::move(tuple));
    }

    template <typename T>
    static constexpr bool has_codec = (requires(Codecs<self_type> &codec, T &value) {
                                          { codec.resumable_decode(value) } -> std::same_as<task_type>;
                                      } || ...) || (requires(Codecs<self_type> &codec, T &value) {
                                          { codec.decode(value) } -> std::same_as<task_type>;
                                      } || ...);
    template <typename T>
    static constexpr bool has_header_codec = (requires(Codecs<self_type> &codec, T &value, major_type major, std::byte info) {
                                                 { codec.resumable_decode(value, major, info) } -> std::same_as<task_type>;
                                             } || ...) || (requires(Codecs<self_type> &codec, T &value, major_type major, std::byte info) {
                                                 { codec.decode(value, major, info) } -> std::same_as<task_type>;
                                             } || ...);

    template <typename T>
    static constexpr bool has_sync_codec = (requires(Codecs<self_type> &codec, T &value) {
                                               { codec.decode(value) } -> std::same_as<status_code>;
                                           } || ...) || (requires(Codecs<self_type> &codec, T &value, major_type major, std::byte info) {
                                               { codec.decode(value, major, info) } -> std::same_as<status_code>;
                                           } || ...);

    template <typename T>
    static constexpr bool has_forwarding = requires(self_type &context, T &value) {
        { Access::decode(context, value) } -> std::same_as<task_type>;
    } || requires(self_type &context, T &value) {
        { Access::transcode(context, value) } -> std::same_as<task_type>;
    } || requires(self_type &context, T &value) {
        { resumable_adl_decode(context, value) } -> std::same_as<task_type>;
    } || requires(self_type &context, T &value) {
        { resumable_adl_decode(context, std::move(value)) } -> std::same_as<task_type>;
    } || requires(self_type &context, T &value) {
        { resumable_adl_transcode(context, value) } -> std::same_as<task_type>;
    } || requires(self_type &context, T &value) {
        { resumable_adl_transcode(context, std::move(value)) } -> std::same_as<task_type>;
    };

    template <typename T> task_type forward_custom(T &value, bool with_tag) {
        if constexpr (IsClassWithTagOverload<T>) {
            if (with_tag) {
                const auto h = co_await header();
                if (h.major != major_type::Tag)
                    co_return status_code::no_match_for_tag_on_buffer;
                const auto tag = co_await argument(h.info);
                if (tag != static_cast<std::uint64_t>(get_major_6_tag_from_class(value)))
                    co_return status_code::no_match_for_tag;
            }
        }
        if constexpr (requires {
                          { Access::decode(*this, value) } -> std::same_as<task_type>;
                      }) {
            co_return co_await Access::decode(*this, value);
        } else if constexpr (requires {
                                 { Access::transcode(*this, value) } -> std::same_as<task_type>;
                             }) {
            co_return co_await Access::transcode(*this, value);
        } else if constexpr (requires {
                                 { resumable_adl_decode(*this, value) } -> std::same_as<task_type>;
                             }) {
            co_return co_await resumable_adl_decode(*this, value);
        } else if constexpr (requires {
                                 { resumable_adl_decode(*this, std::move(value)) } -> std::same_as<task_type>;
                             }) {
            co_return co_await resumable_adl_decode(*this, std::move(value));
        } else if constexpr (requires {
                                 { resumable_adl_transcode(*this, value) } -> std::same_as<task_type>;
                             }) {
            co_return co_await resumable_adl_transcode(*this, value);
        } else {
            co_return co_await resumable_adl_transcode(*this, std::move(value));
        }
    }

    template <std::size_t I = 0, typename T> task_type invoke_codec(T &value) {
        if constexpr (I < sizeof...(Codecs)) {
            using codec_type = std::tuple_element_t<I, std::tuple<Codecs<self_type>...>>;
            if constexpr (requires(codec_type &codec) {
                              { codec.resumable_decode(value) } -> std::same_as<task_type>;
                          }) {
                co_return co_await static_cast<codec_type &>(*this).resumable_decode(value);
            } else if constexpr (requires(codec_type &codec) {
                                     { codec.decode(value) } -> std::same_as<task_type>;
                                 }) {
                co_return co_await static_cast<codec_type &>(*this).decode(value);
            } else
                co_return co_await invoke_codec<I + 1>(value);
        } else
            co_return status_code::unsupported_operation;
    }

    template <std::size_t I = 0, typename T> task_type invoke_header_codec(T &value, resumable_header h) {
        if constexpr (I < sizeof...(Codecs)) {
            using codec_type = std::tuple_element_t<I, std::tuple<Codecs<self_type>...>>;
            if constexpr (requires(codec_type &codec) {
                              { codec.resumable_decode(value, h.major, h.info) } -> std::same_as<task_type>;
                          }) {
                co_return co_await static_cast<codec_type &>(*this).resumable_decode(value, h.major, h.info);
            } else if constexpr (requires(codec_type &codec) {
                                     { codec.decode(value, h.major, h.info) } -> std::same_as<task_type>;
                                 }) {
                co_return co_await static_cast<codec_type &>(*this).decode(value, h.major, h.info);
            } else
                co_return co_await invoke_header_codec<I + 1>(value, h);
        } else
            co_return status_code::unsupported_operation;
    }

    template <typename T> task_type decode(T &value) {
        if (io.terminal && io.status == status_code::cancelled)
            co_return status_code::cancelled;
        using type = std::remove_cvref_t<T>;
        if constexpr (has_codec<type>) {
            co_return co_await invoke_codec(value);
        } else if constexpr (has_header_codec<type> || requires(self_type &context, T &item, major_type major, std::byte info) {
                                 { resumable_decode(context, item, major, info) } -> std::same_as<task_type>;
                             }) {
            const auto h = co_await header();
            co_return co_await decode_header(value, h);
        } else if constexpr (has_sync_codec<type>) {
            static_assert(!has_sync_codec<type>, "synchronous codec requires an explicit resumable decode hook");
        } else if constexpr (requires {
                                 { resumable_decode(*this, value) } -> std::same_as<task_type>;
                             }) {
            if constexpr (IsClassWithTagOverload<type>) {
                const auto h = co_await header();
                if (h.major != major_type::Tag)
                    co_return status_code::no_match_for_tag_on_buffer;
                if ((co_await argument(h.info)) != static_cast<std::uint64_t>(get_major_6_tag_from_class(value)))
                    co_return status_code::no_match_for_tag;
            }
            co_return co_await resumable_decode(*this, value);
        } else if constexpr (requires {
                                 { resumable_transcode(*this, value) } -> std::same_as<task_type>;
                             }) {
            if constexpr (IsClassWithTagOverload<type>) {
                const auto h = co_await header();
                if (h.major != major_type::Tag)
                    co_return status_code::no_match_for_tag_on_buffer;
                if ((co_await argument(h.info)) != static_cast<std::uint64_t>(get_major_6_tag_from_class(value)))
                    co_return status_code::no_match_for_tag;
            }
            co_return co_await resumable_transcode(*this, value);
        } else if constexpr (has_forwarding<type>) {
            co_return co_await forward_custom(value, true);
        } else if constexpr (IsAnyBoundedSizeWrapper<type>) {
            const auto h = co_await header();
            co_return co_await decode_header(value, h);
        } else if constexpr (IsTaggedTuple<type>) {
            auto status = co_await decode(std::get<0>(value));
            if (status != status_code::success)
                co_return status;
            auto fields = tuple_tail(value);
            co_return co_await group(fields);
        } else if constexpr (IsUntaggedTuple<type>) {
            co_return co_await group(value);
        } else if constexpr (IsAggregate<type> && !IsIndefiniteWrapper<type> && !is_wrap_as_array<type>::value &&
                             !std::same_as<type, float16_t> && !std::same_as<type, negative> && !std::same_as<type, integer> &&
                             !std::same_as<type, simple> && !is_static_tag_t<type>::value && !is_dynamic_tag_t<type>) {
            static_assert(!HasRawDecodeMethod<self_type, type> && !HasRawTranscodeMethod<self_type, type> &&
                              !HasRawDecodeFreeFunction<self_type, type> && !HasRawTranscodeFreeFunction<self_type, type>,
                          "synchronous customizations require an explicit resumable_decode or resumable_transcode hook");
            auto fields = to_tuple(value);
            if constexpr (HasInlineTag<type>) {
                static_tag<type::cbor_tag> tag;
                auto                       status = co_await decode(tag);
                if (status != status_code::success)
                    co_return status;
                co_return co_await group(fields);
            } else if constexpr (IsTag<type>) {
                auto status = co_await decode(std::get<0>(fields));
                if (status != status_code::success)
                    co_return status;
                auto tail = tuple_tail(fields);
                co_return co_await group(tail);
            } else
                co_return co_await group(fields);
        } else {
            const auto h = co_await header();
            co_return co_await decode_header(value, h);
        }
    }

    template <typename T> [[nodiscard]] bool aliases_input(const T &target) const {
        if constexpr (!input_output_alias_check_option_v<Options>)
            return false;
        for (const auto &chunk : io.input) {
            if (chunk.identity() == std::addressof(target))
                return true;
            if constexpr (std::ranges::contiguous_range<const T> && std::ranges::sized_range<const T>) {
                const auto span = chunk.try_span();
                if (span && !span->empty() && !std::ranges::empty(target)) {
                    const auto *begin  = std::ranges::data(target);
                    const auto *end    = begin + std::ranges::size(target);
                    const auto  before = std::less<const void *>{};
                    if (before(span->data(), end) && before(begin, span->data() + span->size()))
                        return true;
                }
            }
        }
        return false;
    }

    template <typename T> task_type borrowed_string(T &value, std::uint64_t length) {
        if constexpr (is_static_extent_span_v<T>) {
            if (length != value.size())
                co_return status_code::unexpected_group_size;
        }
        if (length > std::numeric_limits<std::size_t>::max())
            co_return status_code::size_limit_exceeded;
        if (length == 0U) {
            if constexpr (!is_static_extent_span_v<T> && std::default_initializable<T>)
                value = T{};
            co_return status_code::success;
        }
        while (true) {
            if (!io.has_input()) {
                if (io.eof)
                    co_return status_code::incomplete;
                co_await resumable_wait_more_input{io};
                continue;
            }
            const auto span = io.input[io.input_index].try_span();
            if (!span)
                co_return status_code::contiguous_view_on_non_contiguous_data;
            if (span->size() >= length) {
                using element    = typename type_identity_for_view<T>::type;
                const auto *data = reinterpret_cast<const element *>(span->data());
                if (length > std::numeric_limits<std::size_t>::max() - io.total)
                    co_return status_code::size_limit_exceeded;
                if constexpr (requires { value.range; }) {
                    using range_type = std::remove_cvref_t<decltype(value.range)>;
                    if constexpr (std::constructible_from<range_type, const element *, std::size_t>) {
                        value = T{range_type{data, static_cast<std::size_t>(length)}};
                    } else {
                        co_return status_code::contiguous_view_on_non_contiguous_data;
                    }
                } else {
                    value = T{data, static_cast<std::size_t>(length)};
                }
                std::size_t count{};
                auto        consumed = io.input[io.input_index].take(static_cast<std::size_t>(length), count);
                (void)consumed;
                io.total += count;
                co_return status_code::success;
            }
            for (auto index = io.input_index + 1U; index < io.input.size(); ++index) {
                if (!io.input[index].empty())
                    co_return status_code::contiguous_view_on_non_contiguous_data;
            }
            if (io.eof)
                co_return status_code::incomplete;
            co_await resumable_wait_more_input{io};
        }
    }

    template <typename T> struct type_identity_for_view {
        using type = typename T::value_type;
    };

    template <typename T> task_type string_payload(T &value, std::uint64_t length) {
        using type = std::remove_cvref_t<T>;
        if constexpr (IsConstView<type>) {
            co_return co_await borrowed_string(value, length);
        } else {
            if (aliases_input(value))
                co_return status_code::input_output_aliasing;
            if constexpr (IsFixedArray<type>) {
                if (length != value.size())
                    co_return status_code::unexpected_group_size;
                auto out = std::ranges::begin(value);
                for (std::uint64_t index = 0; index < length; ++index) {
                    *out = static_cast<typename type::value_type>(std::to_integer<unsigned char>(co_await byte()));
                    ++out;
                }
            } else {
                appender<type> append;
                for (std::uint64_t index = 0; index < length; ++index) {
                    append(value, static_cast<typename type::value_type>(std::to_integer<unsigned char>(co_await byte())));
                }
            }
            co_return status_code::success;
        }
    }

    template <typename T>
    task_type string(T &value, resumable_header h, std::uint64_t min = 0, std::uint64_t max = std::numeric_limits<std::uint64_t>::max(),
                     bool require_indefinite = false) {
        using type                    = std::remove_cvref_t<T>;
        constexpr auto expected_major = IsTextString<type> ? major_type::TextString : major_type::ByteString;
        if (h.major != expected_major)
            co_return IsTextString<type> ? status_code::no_match_for_tstr_on_buffer : status_code::no_match_for_bstr_on_buffer;
        if (h.info != std::byte{31}) {
            if (require_indefinite)
                co_return major_type_mismatch_status(expected_major);
            const auto length = co_await argument(h.info);
            if (length < min || length > max)
                co_return status_code::size_limit_exceeded;
            co_return co_await string_payload(value, length);
        }
        if constexpr (IsConstView<type>) {
            co_return IsTextString<type> ? status_code::no_match_for_tstr_on_buffer : status_code::no_match_for_bstr_on_buffer;
        } else if constexpr (IsFixedArray<type>) {
            co_return status_code::unexpected_group_size;
        } else {
            std::uint64_t size{};
            while (true) {
                const auto chunk = co_await header();
                if (resumable_break(chunk))
                    co_return size < min ? status_code::size_limit_exceeded : status_code::success;
                if (chunk.major != expected_major || chunk.info == std::byte{31})
                    co_return status_code::malformed_structure;
                const auto length = co_await argument(chunk.info);
                if (length > max - size)
                    co_return status_code::size_limit_exceeded;
                auto status = co_await string_payload(value, length);
                if (status != status_code::success)
                    co_return status;
                size += length;
            }
        }
    }

    template <typename T> static consteval bool zero_width_builtin() { return builtin_decode_consumes_zero_bytes<self_type, T>(); }

    template <bool Bounded = false, typename T>
    task_type container(T &value, resumable_header h, std::uint64_t min = 0, std::uint64_t max = std::numeric_limits<std::uint64_t>::max(),
                        bool require_indefinite = false) {
        using type         = std::remove_cvref_t<T>;
        constexpr bool map = IsMap<type>;
        if constexpr (!Bounded && map) {
            static_assert(!zero_width_builtin<typename type::key_type>() || !zero_width_builtin<typename type::mapped_type>(),
                          "Zero-byte container entries require an explicit count bound");
        } else if constexpr (!Bounded && !IsFixedArray<type>) {
            static_assert(!zero_width_builtin<typename type::value_type>(), "Zero-byte container entries require an explicit count bound");
        }
        if (h.major != (map ? major_type::Map : major_type::Array))
            co_return map ? status_code::no_match_for_map_on_buffer : status_code::no_match_for_array_on_buffer;
        const bool indefinite = h.info == std::byte{31};
        if (require_indefinite && !indefinite)
            co_return major_type_mismatch_status(map ? major_type::Map : major_type::Array);
        std::uint64_t length{};
        if (!indefinite) {
            length = co_await argument(h.info);
            if (length < min || length > max)
                co_return status_code::size_limit_exceeded;
            if constexpr (IsFixedArray<type>) {
                if (length != value.size())
                    co_return status_code::unexpected_group_size;
            }
        } else if constexpr (IsFixedArray<type>)
            co_return status_code::unexpected_group_size;
        std::uint64_t count{};
        auto          fixed = [&] {
            if constexpr (IsFixedArray<type>)
                return std::ranges::begin(value);
            else
                return 0;
        }();
        appender<type> append;
        while (indefinite || count < length) {
            resumable_header child{};
            if (indefinite) {
                child = co_await header();
                if (resumable_break(child))
                    co_return count < min ? status_code::size_limit_exceeded : status_code::success;
                if (count == max)
                    co_return status_code::size_limit_exceeded;
            }
            if constexpr (map) {
                using key_type    = typename type::key_type;
                using mapped_type = typename type::mapped_type;
                auto key          = make_decode_value_for<key_type>(value);
                auto mapped       = make_decode_value_for<mapped_type>(value);
                auto status       = indefinite ? co_await decode_header(key, child) : co_await decode(key);
                if (status != status_code::success)
                    co_return status;
                if (indefinite) {
                    const auto mapped_header = co_await header();
                    if (resumable_break(mapped_header))
                        co_return status_code::malformed_structure;
                    status = co_await decode_header(mapped, mapped_header);
                } else
                    status = co_await decode(mapped);
                if (status != status_code::success)
                    co_return status;
                append(value, std::pair<key_type, mapped_type>{std::move(key), std::move(mapped)});
            } else if constexpr (IsFixedArray<type>) {
                auto status = co_await decode(*fixed);
                if (status != status_code::success)
                    co_return status;
                ++fixed;
            } else {
                using item_type = typename type::value_type;
                auto item       = make_decode_value_for<item_type>(value);
                auto status     = indefinite ? co_await decode_header(item, child) : co_await decode(item);
                if (status != status_code::success)
                    co_return status;
                append(value, std::move(item));
            }
            ++count;
        }
        co_return status_code::success;
    }

    template <typename T> task_type decode_tagged(T &value, std::uint64_t tag) {
        using type = std::remove_cvref_t<T>;
        if constexpr (IsOptional<type>) {
            using item_type = typename type::value_type;
            auto item       = make_decode_value_for_optional<item_type>(value);
            auto status     = co_await decode_tagged(item, tag);
            if (status == status_code::success)
                value = std::move(item);
            co_return status;
        } else if constexpr (IsVariant<type>) {
            std::optional<std::uint64_t> cached{tag};
            co_return co_await variant(value, resumable_header{.major = major_type::Tag, .info = std::byte{}}, cached);
        } else if constexpr (std::same_as<type, as_tag_any>) {
            value.tag = tag;
            co_return status_code::success;
        } else if constexpr (is_static_tag_t<type>::value || is_dynamic_tag_t<type>) {
            co_return tag == static_cast<std::uint64_t>(value) ? status_code::success : status_code::no_match_for_tag;
        } else if constexpr (IsTaggedTuple<type>) {
            if (tag != static_cast<std::uint64_t>(std::get<0>(value)))
                co_return status_code::no_match_for_tag;
            auto fields = tuple_tail(value);
            co_return co_await group(fields);
        } else if constexpr (has_forwarding<type> && IsClassWithTagOverload<type>) {
            if (tag != static_cast<std::uint64_t>(get_major_6_tag_from_class(value)))
                co_return status_code::no_match_for_tag;
            co_return co_await forward_custom(value, false);
        } else if constexpr (requires {
                                 { resumable_decode(*this, value) } -> std::same_as<task_type>;
                             } && IsClassWithTagOverload<type>) {
            if (tag != static_cast<std::uint64_t>(get_major_6_tag_from_class(value)))
                co_return status_code::no_match_for_tag;
            co_return co_await resumable_decode(*this, value);
        } else if constexpr (IsAggregate<type> && HasInlineTag<type>) {
            if (tag != type::cbor_tag)
                co_return status_code::no_match_for_tag;
            auto fields = to_tuple(value);
            co_return co_await group(fields);
        } else if constexpr (IsAggregate<type> && IsTag<type>) {
            auto fields = to_tuple(value);
            if (tag != static_cast<std::uint64_t>(std::get<0>(fields)))
                co_return status_code::no_match_for_tag;
            auto tail = tuple_tail(fields);
            co_return co_await group(tail);
        } else if constexpr (requires {
                                 { resumable_decode(*this, value, tag) } -> std::same_as<task_type>;
                             }) {
            co_return co_await resumable_decode(*this, value, tag);
        } else {
            co_return status_code::unsupported_operation;
        }
    }

    template <typename Variant, std::size_t I = 0, bool CatchAll = false>
    task_type variant(Variant &value, resumable_header h, std::optional<std::uint64_t> &tag) {
        if constexpr (I == variant_size_v<Variant>) {
            if constexpr (!CatchAll) {
                if (h.major == major_type::Simple)
                    co_return co_await variant<Variant, 0, true>(value, h, tag);
            }
            co_return status_code::no_match_in_variant_on_buffer;
        } else {
            using alt = variant_alternative_t<I, Variant>;
            if (matches_major_dispatch<alt>(h.major) && (h.major != major_type::Simple || matches_simple_dispatch<CatchAll, alt>(h.info))) {
                alt         decoded{};
                status_code status{};
                if constexpr (IsVariant<alt>) {
                    status = co_await variant(decoded, h, tag);
                } else if constexpr (IsTag<alt> || IsOptional<alt>) {
                    if (h.major == major_type::Tag)
                        status = co_await decode_tagged(decoded, *tag);
                    else
                        status = co_await decode_header(decoded, h);
                } else {
                    status = co_await decode_header(decoded, h);
                }
                if (status == status_code::success) {
                    variant_assign<I>(value, std::move(decoded));
                    co_return status;
                }
                if (!is_retriable_variant_mismatch(status))
                    co_return status;
            }
            co_return co_await variant<Variant, I + 1, CatchAll>(value, h, tag);
        }
    }

    template <typename T> task_type decode_header(T &value, resumable_header h) {
        if (io.terminal && io.status == status_code::cancelled)
            co_return status_code::cancelled;
        using type = std::remove_cvref_t<T>;
        if constexpr (has_header_codec<type>) {
            co_return co_await invoke_header_codec(value, h);
        } else if constexpr (has_sync_codec<type> && !has_codec<type>) {
            static_assert(!has_sync_codec<type>, "synchronous codec requires an explicit resumable decode hook");
        } else if constexpr (requires {
                                 { resumable_decode(*this, value, h.major, h.info) } -> std::same_as<task_type>;
                             }) {
            co_return co_await resumable_decode(*this, value, h.major, h.info);
        } else if constexpr (has_forwarding<type>) {
            if constexpr (IsClassWithTagOverload<type>) {
                if (h.major != major_type::Tag)
                    co_return status_code::no_match_for_tag_on_buffer;
                if ((co_await argument(h.info)) != static_cast<std::uint64_t>(get_major_6_tag_from_class(value)))
                    co_return status_code::no_match_for_tag;
            } else {
                pending_header = h;
            }
            co_return co_await forward_custom(value, false);
        } else if constexpr (
            has_codec<type> ||
            requires {
                { resumable_decode(*this, value) } -> std::same_as<task_type>;
            } ||
            requires {
                { resumable_transcode(*this, value) } -> std::same_as<task_type>;
            }) {
            if constexpr (IsClassWithTagOverload<type>) {
                if (h.major != major_type::Tag)
                    co_return status_code::no_match_for_tag_on_buffer;
                if ((co_await argument(h.info)) != static_cast<std::uint64_t>(get_major_6_tag_from_class(value)))
                    co_return status_code::no_match_for_tag;
            } else {
                pending_header = h;
            }
            if constexpr (has_codec<type>)
                co_return co_await invoke_codec(value);
            else if constexpr (requires {
                                   { resumable_decode(*this, value) } -> std::same_as<task_type>;
                               })
                co_return co_await resumable_decode(*this, value);
            else
                co_return co_await resumable_transcode(*this, value);
        } else if constexpr (std::same_as<type, bool>) {
            if (h.major != major_type::Simple)
                co_return status_code::no_match_for_simple_on_buffer;
            if (h.info != std::byte{20} && h.info != std::byte{21})
                co_return status_code::no_match_for_tag_simple_on_buffer;
            value = h.info == std::byte{21};
        } else if constexpr (std::same_as<type, std::nullptr_t>) {
            if (h.major != major_type::Simple)
                co_return status_code::no_match_for_simple_on_buffer;
            if (h.info != std::byte{22})
                co_return status_code::no_match_for_tag_simple_on_buffer;
            value = nullptr;
        } else if constexpr (std::same_as<type, simple>) {
            if (h.major != major_type::Simple)
                co_return status_code::no_match_for_simple_on_buffer;
            if (h.info > std::byte{24})
                co_return status_code::no_match_for_tag_simple_on_buffer;
            value = simple{static_cast<simple::value_type>(co_await argument(h.info))};
        } else if constexpr (std::same_as<type, integer>) {
            if (h.major != major_type::UnsignedInteger && h.major != major_type::NegativeInteger)
                co_return status_code::no_match_for_int_on_buffer;
            const auto decoded = co_await argument(h.info);
            value              = h.major == major_type::NegativeInteger ? integer{negative{decoded + 1U}} : integer{decoded};
        } else if constexpr (std::same_as<type, negative>) {
            if (h.major != major_type::NegativeInteger)
                co_return status_code::no_match_for_nint_on_buffer;
            value = negative{(co_await argument(h.info)) + 1U};
        } else if constexpr (std::is_enum_v<type>) {
            std::underlying_type_t<type> decoded{};
            auto                         status = co_await decode_header(decoded, h);
            if (status != status_code::success)
                co_return status_code::no_match_for_enum_on_buffer;
            value = static_cast<type>(decoded);
        } else if constexpr (IsUnsigned<type>) {
            if (h.major != major_type::UnsignedInteger)
                co_return status_code::no_match_for_uint_on_buffer;
            const auto decoded = co_await argument(h.info);
            if constexpr (strict_integer_decode_option_v<Options>) {
                if (!unsigned_value_fits<type>(decoded))
                    co_return status_code::no_match_for_uint_on_buffer;
            }
            value = static_cast<type>(decoded);
        } else if constexpr (IsSigned<type>) {
            if (h.major != major_type::UnsignedInteger && h.major != major_type::NegativeInteger)
                co_return status_code::no_match_for_int_on_buffer;
            const auto decoded = co_await argument(h.info);
            if constexpr (strict_integer_decode_option_v<Options>) {
                if ((h.major == major_type::UnsignedInteger && !unsigned_value_fits<type>(decoded)) ||
                    (h.major == major_type::NegativeInteger && !negative_argument_fits<type>(decoded)))
                    co_return status_code::no_match_for_int_on_buffer;
            }
            value = h.major == major_type::UnsignedInteger ? static_cast<type>(decoded)
                                                           : static_cast<type>(type{-1} - static_cast<type>(decoded));
        } else if constexpr (is_static_tag_t<type>::value || is_dynamic_tag_t<type>) {
            if (h.major != major_type::Tag)
                co_return status_code::no_match_for_tag_on_buffer;
            if ((co_await argument(h.info)) != static_cast<std::uint64_t>(value))
                co_return status_code::no_match_for_tag;
        } else if constexpr (std::same_as<type, float16_t> || std::same_as<type, float> || std::same_as<type, double>) {
            constexpr std::uint8_t info = std::same_as<type, float16_t> ? 25U : std::same_as<type, float> ? 26U : 27U;
            if (h.major != major_type::Simple)
                co_return status_code::no_match_for_simple_on_buffer;
            if (h.info != static_cast<std::byte>(info))
                co_return status_code::no_match_for_tag_simple_on_buffer;
            const auto bits = co_await argument(h.info);
            if constexpr (std::same_as<type, float16_t>)
                value = float16_t{static_cast<std::uint16_t>(bits)};
            else if constexpr (std::same_as<type, float>)
                value = std::bit_cast<float>(static_cast<std::uint32_t>(bits));
            else
                value = std::bit_cast<double>(bits);
        } else if constexpr (std::same_as<type, as_tag_any>) {
            if (h.major != major_type::Tag)
                co_return status_code::no_match_for_tag_on_buffer;
            value.tag = co_await argument(h.info);
        } else if constexpr (IsTextHeader<type> || IsBinaryHeader<type> || IsArrayHeader<type> || IsMapHeader<type>) {
            constexpr auto expected = IsTextHeader<type>     ? major_type::TextString
                                      : IsBinaryHeader<type> ? major_type::ByteString
                                      : IsArrayHeader<type>  ? major_type::Array
                                                             : major_type::Map;
            if (h.major != expected)
                co_return major_type_mismatch_status(expected);
            value.indefinite = h.info == std::byte{31};
            value.size       = value.indefinite ? 0 : co_await argument(h.info);
        } else if constexpr (std::same_as<type, indefinite_break>) {
            if (!resumable_break(h))
                co_return status_code::no_match_for_simple_on_buffer;
        } else if constexpr (std::same_as<type, as_array> || std::same_as<type, as_map>) {
            constexpr auto expected = std::same_as<type, as_array> ? major_type::Array : major_type::Map;
            if (h.major != expected)
                co_return major_type_mismatch_status(expected);
            if (h.info == std::byte{31})
                co_return status_code::unexpected_group_size;
            if ((co_await argument(h.info)) != value.size_)
                co_return status_code::unexpected_group_size;
        } else if constexpr (is_wrap_as_array<type>::value) {
            if (h.major != major_type::Array)
                co_return status_code::no_match_for_array_on_buffer;
            if (h.info == std::byte{31})
                co_return status_code::unexpected_group_size;
            if ((co_await argument(h.info)) != value.size_)
                co_return status_code::unexpected_group_size;
            co_return co_await members(value.values_);
        } else if constexpr (IsAnyBoundedSizeWrapper<type>) {
            auto      &wrapped = value.value();
            const auto min     = [&] {
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
            auto &target = [&]() -> auto & {
                if constexpr (IsIndefiniteWrapper<std::remove_cvref_t<decltype(wrapped)>>)
                    return wrapped.value_;
                else
                    return wrapped;
            }();
            constexpr bool indefinite = IsIndefiniteWrapper<std::remove_cvref_t<decltype(wrapped)>>;
            using target_type         = std::remove_cvref_t<decltype(target)>;
            if constexpr (IsTextHeader<target_type> || IsBinaryHeader<target_type> || IsArrayHeader<target_type> ||
                          IsMapHeader<target_type>) {
                constexpr auto expected = IsTextHeader<target_type>     ? major_type::TextString
                                          : IsBinaryHeader<target_type> ? major_type::ByteString
                                          : IsArrayHeader<target_type>  ? major_type::Array
                                                                        : major_type::Map;
                if (h.major != expected)
                    co_return major_type_mismatch_status(expected);
                if (h.info == std::byte{31}) {
                    if (min != 0U || std::cmp_less(max, std::numeric_limits<std::uint64_t>::max()))
                        co_return status_code::size_limit_exceeded;
                    target = {.size = 0U, .indefinite = true};
                } else {
                    const auto size = co_await argument(h.info);
                    if (size < min || size > max)
                        co_return status_code::size_limit_exceeded;
                    target = {.size = size, .indefinite = false};
                }
                co_return status_code::success;
            } else if constexpr (IsString<target_type>)
                co_return co_await string(target, h, min, max, indefinite);
            else
                co_return co_await container<true>(target, h, min, max, indefinite);
        } else if constexpr (IsIndefiniteWrapper<type>) {
            using target_type = std::remove_cvref_t<decltype(value.value_)>;
            if constexpr (IsString<target_type>)
                co_return co_await string(value.value_, h, 0, std::numeric_limits<std::uint64_t>::max(), true);
            else
                co_return co_await container(value.value_, h, 0, std::numeric_limits<std::uint64_t>::max(), true);
        } else if constexpr (IsOptional<type>) {
            if (h.major == major_type::Simple && h.info == std::byte{22}) {
                value.reset();
                co_return status_code::success;
            }
            using item_type = typename type::value_type;
            auto item       = make_decode_value_for_optional<item_type>(value);
            auto status     = co_await decode_header(item, h);
            if (status == status_code::success)
                value = std::move(item);
            co_return status;
        } else if constexpr (IsVariant<type>) {
            require_unambiguous_variant_dispatch<type>();
            std::optional<std::uint64_t> tag;
            if (h.major == major_type::Tag)
                tag = co_await argument(h.info);
            co_return co_await variant(value, h, tag);
        } else if constexpr (IsString<type>) {
            co_return co_await string(value, h);
        } else if constexpr (IsMap<type> || IsArray<type>) {
            co_return co_await container(value, h);
        } else if constexpr (IsTaggedTuple<type>) {
            if (h.major != major_type::Tag)
                co_return status_code::no_match_for_tag_on_buffer;
            if ((co_await argument(h.info)) != static_cast<std::uint64_t>(std::get<0>(value)))
                co_return status_code::no_match_for_tag;
            auto fields = tuple_tail(value);
            co_return co_await group(fields);
        } else if constexpr (IsUntaggedTuple<type>) {
            co_return co_await group_header(value, h);
        } else if constexpr (IsAggregate<type>) {
            auto fields = to_tuple(value);
            if constexpr (HasInlineTag<type>) {
                if (h.major != major_type::Tag)
                    co_return status_code::no_match_for_tag_on_buffer;
                if ((co_await argument(h.info)) != type::cbor_tag)
                    co_return status_code::no_match_for_tag;
                co_return co_await group(fields);
            } else if constexpr (IsTag<type>) {
                if (h.major != major_type::Tag)
                    co_return status_code::no_match_for_tag_on_buffer;
                if ((co_await argument(h.info)) != static_cast<std::uint64_t>(std::get<0>(fields)))
                    co_return status_code::no_match_for_tag;
                auto tail = tuple_tail(fields);
                co_return co_await group(tail);
            } else
                co_return co_await group_header(fields, h);
        } else {
            static_assert(always_false<type>::value, "unsupported type: provide an explicit resumable_decode hook");
        }
        co_return status_code::success;
    }
};

// Shape checks trust coroutine hooks to consume the item they advertise, just
// as the synchronous codec boundary trusts an application's customization.
template <typename Options, template <typename> typename... Codecs>
struct codec_payload_mixin_customization<resumable_decoder_context<Options, Codecs...>> {
    using self_type = resumable_decoder_context<Options, Codecs...>;

    template <typename T, payload_decode_context> static consteval bool accepts() {
        return self_type::template has_codec<T> || self_type::template has_header_codec<T> || self_type::template has_forwarding<T> ||
               requires(self_type &context, T &value) {
                   { resumable_decode(context, value) } -> std::same_as<resumable_task<status_code>>;
               } || requires(self_type &context, T &value) {
                   { resumable_transcode(context, value) } -> std::same_as<resumable_task<status_code>>;
               } || requires(self_type &context, T &value, major_type major, std::byte info) {
                   { resumable_decode(context, value, major, info) } -> std::same_as<resumable_task<status_code>>;
               };
    }
};

template <typename Options, template <typename> typename... Codecs, typename Tuple>
resumable_task<status_code> resumable_decode_root(resumable_io &io, Tuple values) {
    resumable_decoder_context<Options, Codecs...> decoder{io};
    co_return co_await decoder.members(values);
}

} // namespace cbor::tags::detail
