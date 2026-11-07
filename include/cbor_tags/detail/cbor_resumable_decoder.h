#pragma once

#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/detail/cbor_resumable_concepts.h"
#include "cbor_tags/detail/cbor_resumable_control.h"
#include "cbor_tags/detail/cbor_resumable_leaf.h"
#include "cbor_tags/detail/cbor_resumable_retry.h"
#include "cbor_tags/detail/cbor_resumable_source.h"

#include <algorithm>
#include <limits>
#include <tuple>

namespace cbor::tags::detail {

template <typename T> inline constexpr bool    resumable_wrap_array                      = false;
template <typename... T> inline constexpr bool resumable_wrap_array<wrap_as_array<T...>> = true;

// Keep capability detection separate from the dispatch that uses it.
template <typename Decoder, typename T>
concept HasRvalueTagDecode = requires(Decoder &decoder, T &tag) { decoder.decode(std::move(tag)); };

template <typename Appender, typename T>
concept HasAppendReservation = requires(Appender &append, T &value, std::uint64_t size) { append.reserve_for_append(value, size); };

template <typename Appender, typename T>
concept HasGeometricAppendReservation =
    requires(Appender &append, T &value, std::uint64_t size) { append.template reserve_for_append<true>(value, size); };

// Preserve the normal entrypoint selected by optional/variant/indefinite dispatch.
// The enclosing ordinary call still owns the complete retry boundary.
template <typename T, payload_decode_context Context> struct resume_context_value {
    T                                                      &value;
    template <typename Decoder> expected<void, status_code> decode(Decoder &decoder) {
        const auto [major, info] = decoder.read_initial_byte();
        status_code status;
        if constexpr (Context == payload_decode_context::tag) {
            std::uint64_t tag{};
            status = decoder.decode_tag_argument(info, tag);
            if (status == status_code::success)
                status = decoder.decode(value, tag);
        } else
            status = decoder.decode(value, major, info);
        if (status != status_code::success)
            return unexpected<status_code>{status};
        return {};
    }
};

#if CBOR_TAGS_HAS_NAMED_REFLECTION
// Without an extension field, a key longer than every field name can't match.
template <typename Object> consteval std::size_t named_longest_fixed_key() {
    std::size_t longest = 0;
    for (const auto key : named_fixed_member_keys<Object>())
        longest = std::max(longest, key.size());
    return longest;
}
#endif

struct resume_group_header {
    std::uint64_t                                           size;
    template <typename Decoder> expected<void, status_code> decode(Decoder &decoder) {
        const auto [major, info] = decoder.read_initial_byte();
        const auto status        = decoder.decode_wrapped_group_header(major, info, size);
        if (status != status_code::success)
            return unexpected<status_code>{status};
        return {};
    }
};

template <typename T> struct resume_tag_wrapper {
    T                                                      &tag;
    template <typename Decoder> expected<void, status_code> decode(Decoder &decoder) {
        const auto status = [&] {
            if constexpr (HasRvalueTagDecode<Decoder, T>)
                return decoder.decode(std::move(tag));
            else
                return decoder.decode(tag);
        }();
        if (status != status_code::success)
            return unexpected<status_code>{status};
        return {};
    }
};

template <typename Source, typename Options, template <typename> typename... Codecs>
class resumable_decode_context : public Codecs<resumable_decode_context<Source, Options, Codecs...>>... {
    using self_type     = resumable_decode_context;
    using ordinary_type = decoder<Source, Options, cbor_header_decoder, cbor_indefinite_decoder, Codecs...>;
    using mixins        = std::tuple<Codecs<self_type>...>;
    Source         &source_;
    resume_control &control_;
    ordinary_type   ordinary_;
    std::size_t     committed_offset_;

  public:
    using source_type                           = Source;
    using options                               = Options;
    static constexpr resume_state waiting_state = resume_state::need_input;

    // Borrowed views point into the source, same aliases as the one-shot decoder
    using bstr_view_t        = typename ordinary_type::bstr_view_t;
    using tstr_view_t        = typename ordinary_type::tstr_view_t;
    using encoded_item_view  = typename ordinary_type::encoded_item_view;
    using encoded_array_view = typename ordinary_type::encoded_array_view;
    using encoded_map_view   = typename ordinary_type::encoded_map_view;

    resumable_decode_context(Source &source, resume_control &control) : source_(source), control_(control), ordinary_(source) {
        ordinary_.reader_.position_       = source.committed_position();
        ordinary_.reader_.current_offset_ = ordinary_.reader_.position_.offset();
        committed_offset_                 = ordinary_.reader_.current_offset_;
    }
    Source        &source() noexcept { return source_; }
    std::size_t    offset() const noexcept { return committed_offset_; }
    auto           remaining_input() const { return source_.remaining(source_.committed_position()); }
    ordinary_type &ordinary() noexcept { return ordinary_; }
    auto           wait() noexcept { return control_.wait(); }
    bool           empty() const { return ordinary_.reader_.empty(source_); }
    bool           closed() const { return source_.closed(); }
    void           commit() {
        // The source owns the checkpoint; retain only its scalar progress here.
        source_.commit(ordinary_.reader_.position_);
        committed_offset_ = ordinary_.reader_.current_offset_;
    }
    void rollback() {
        ordinary_.reader_.position_       = source_.committed_position();
        ordinary_.reader_.current_offset_ = committed_offset_;
    }

    // A complete normal call is the sole retry boundary, including its nested calls.
    template <typename T> resumable_task<status_code> ordinary(T &value) {
        require_retry<T>();
        return attempt_ordinary(value);
    }

    template <typename T, payload_decode_context Context> resumable_task<status_code> ordinary(resume_context_value<T, Context> &value) {
        require_retry<T, Context>();
        return attempt_ordinary(value);
    }

    template <typename T> resumable_task<status_code> ordinary(resume_tag_wrapper<T> &value) {
        require_retry<T>();
        return attempt_ordinary(value);
    }

    // This internal adapter only checks the group header; it publishes no
    // destination or codec state before completing that check.
    resumable_task<status_code> ordinary(resume_group_header &value) { return attempt_ordinary(value); }

  private:
    template <typename T, payload_decode_context Context = payload_decode_context::direct> static consteval void require_retry() {
        static_assert(resumable_decode_retry_allowed<ordinary_type, T, Context, Codecs<ordinary_type>...>(),
                      "ordinary decoding requires a resumable hook or resumable_retry_traits decode opt-in");
    }

    template <typename T> resumable_task<status_code> attempt_ordinary(T &value) {
        const auto result = ordinary_(value);
        if (result) {
            commit();
            return status_code::success;
        }
        if (result.error() != status_code::incomplete)
            return result.error();
        rollback();
        if (closed())
            return status_code::incomplete;
        return retry(value);
    }

  public:
    template <typename... T> resumable_task<status_code> operator()(T &&...value) {
        return arguments(std::tuple<T...>{std::forward<T>(value)...});
    }

    // Bulk string transfer uses the same destination appender as one-shot decoding.
    template <typename T> resumable_task<status_code> payload(T &value, std::uint64_t length, appender<T> &append) {
        drain_payload(value, length, append);
        if (length == 0)
            return status_code::success;
        if (closed())
            return status_code::incomplete;
        return await_payload(value, length, append);
    }

  private:
    template <typename T> void drain_payload(T &value, std::uint64_t &length, appender<T> &append) {
        while (length != 0 && !empty()) {
            if (const auto span = resumable_source_access::borrow_window(source_, ordinary_.reader_.position_); span && !span->empty()) {
                const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(length, span->size()));
                append(value, span->first(count));
                resumable_source_access::consume_window(source_, ordinary_.reader_.position_, count);
                ordinary_.reader_.current_offset_ += count;
                length -= count;
            } else if constexpr (HasBulkByteRead<decltype(ordinary_.reader_.position_)>) {
                // This scratch range and every native iterator die before a
                // suspension; only copied bytes reach the destination.
                std::array<std::byte, 256> bytes;
                const auto                 requested = static_cast<std::size_t>(std::min<std::uint64_t>(length, bytes.size()));
                const auto                 count     = ordinary_.reader_.position_.read_bytes(std::span(bytes).first(requested));
                assert(count != 0);
                append(value, std::span<const std::byte>{bytes}.first(count));
                ordinary_.reader_.current_offset_ += count;
                length -= count;
            } else {
                append(value, static_cast<typename T::value_type>(ordinary_.reader_.read(source_)));
                --length;
            }
            commit();
        }
    }

    // Allocate a frame only when the current payload actually needs more input.
    template <typename T> resumable_task<status_code> await_payload(T &value, std::uint64_t length, appender<T> &append) {
        while (length != 0) {
            co_await wait();
            drain_payload(value, length, append);
            if (length != 0 && closed())
                co_return status_code::incomplete;
        }
        co_return status_code::success;
    }

    // Sinks get the payload in pieces as it arrives, so nothing waits for the whole string
    template <typename Sink> resumable_task<status_code> string_sink(Sink &sink) {
        using header_type = std::conditional_t<IsTstrSink<Sink>, as_text_any, as_bstr_any>;
        header_type header{};
        auto        status = co_await ordinary(header);
        if (status != status_code::success)
            co_return status;
        if (!header.indefinite)
            co_return co_await sink_payload(sink, header.size);

        // Indefinite strings are definite chunks of the same type up to a break
        const auto chunk_major = IsTstrSink<Sink> ? std::byte{0x60} : std::byte{0x40};
        for (;;) {
            status = co_await available();
            if (status != status_code::success)
                co_return status;
            const auto initial = static_cast<std::byte>(*ordinary_.reader_.position_);
            if (initial == std::byte{0xff}) {
                indefinite_break end{};
                co_return co_await ordinary(end);
            }
            if ((initial & std::byte{0xe0}) != chunk_major || (initial & std::byte{0x1f}) == std::byte{31})
                co_return status_code::malformed_structure;

            header_type chunk{};
            status = co_await ordinary(chunk);
            if (status != status_code::success)
                co_return status;
            status = co_await sink_payload(sink, chunk.size);
            if (status != status_code::success)
                co_return status;
        }
    }

    template <typename Sink> resumable_task<status_code> sink_payload(Sink &sink, std::uint64_t length) {
        const auto status = drain_sink(sink, length);
        if (status != status_code::success || length == 0)
            return status;
        if (closed())
            return status_code::incomplete;
        return await_sink_payload(sink, length);
    }

    template <typename Sink> resumable_task<status_code> await_sink_payload(Sink &sink, std::uint64_t length) {
        while (length != 0) {
            co_await wait();
            const auto status = drain_sink(sink, length);
            if (status != status_code::success)
                co_return status;
            if (length != 0 && closed())
                co_return status_code::incomplete;
        }
        co_return status_code::success;
    }

    // A piece points into the source when it lends windows, else into a local copy.
    // It's committed once the callback accepts it.
    template <typename Sink> status_code drain_sink(Sink &sink, std::uint64_t &length) {
        while (length != 0 && !empty()) {
            if (const auto window = resumable_source_access::borrow_window(source_, ordinary_.reader_.position_);
                window && !window->empty()) {
                const auto count  = static_cast<std::size_t>(std::min<std::uint64_t>(length, window->size()));
                const auto status = sink.deliver(window->first(count));
                if (status != status_code::success)
                    return status;
                resumable_source_access::consume_window(source_, ordinary_.reader_.position_, count);
                ordinary_.reader_.current_offset_ += count;
                length -= count;
            } else {
                std::array<std::byte, 256> bytes;
                const auto                 requested = static_cast<std::size_t>(std::min<std::uint64_t>(length, bytes.size()));
                std::size_t                count{};
                if constexpr (HasBulkByteRead<decltype(ordinary_.reader_.position_)>) {
                    count = ordinary_.reader_.position_.read_bytes(std::span(bytes).first(requested));
                    ordinary_.reader_.current_offset_ += count;
                } else {
                    while (count != requested && !empty())
                        bytes[count++] = static_cast<std::byte>(ordinary_.reader_.read(source_));
                }
                assert(count != 0);
                const auto status = sink.deliver(std::span<const std::byte>{bytes}.first(count));
                if (status != status_code::success)
                    return status;
                length -= count;
            }
            commit();
        }
        return status_code::success;
    }

    // Elements and entries decode like container children, then go to the callback
    template <typename Sink> resumable_task<status_code> array_sink(Sink &sink) {
        using element_type = typename Sink::value_type;
        static_assert(!consumes_zero_bytes<element_type>(), "as_array_sink elements must consume input, or a large count never ends");

        as_array_any header{};
        auto         status = co_await ordinary(header);
        if (status != status_code::success)
            co_return status;
        for (std::uint64_t index = 0; header.indefinite || index != header.size; ++index) {
            if (header.indefinite) {
                status = co_await available();
                if (status != status_code::success)
                    co_return status;
                if (static_cast<std::byte>(*ordinary_.reader_.position_) == std::byte{0xff}) {
                    indefinite_break end{};
                    co_return co_await ordinary(end);
                }
            }

            element_type element{};
            status = header.indefinite ? co_await header_item(element) : co_await item(element);
            if (status != status_code::success)
                co_return status;
            status = sink.deliver(std::move(element));
            if (status != status_code::success)
                co_return status;
        }
        co_return status_code::success;
    }

    template <typename Sink> resumable_task<status_code> map_sink(Sink &sink) {
        using key_type    = typename Sink::key_type;
        using mapped_type = typename Sink::mapped_type;
        static_assert(!consumes_zero_bytes<key_type>() || !consumes_zero_bytes<mapped_type>(),
                      "as_map_sink entries must consume input, or a large count never ends");

        as_map_any header{};
        auto       status = co_await ordinary(header);
        if (status != status_code::success)
            co_return status;
        for (std::uint64_t index = 0; header.indefinite || index != header.size; ++index) {
            if (header.indefinite) {
                status = co_await available();
                if (status != status_code::success)
                    co_return status;
                if (static_cast<std::byte>(*ordinary_.reader_.position_) == std::byte{0xff}) {
                    indefinite_break end{};
                    co_return co_await ordinary(end);
                }
            }

            key_type key{};
            status = header.indefinite ? co_await header_item(key) : co_await item(key);
            if (status != status_code::success)
                co_return status;

            // A break between a key and its value is malformed
            if (header.indefinite) {
                status = co_await available();
                if (status != status_code::success)
                    co_return status;
                if (static_cast<std::byte>(*ordinary_.reader_.position_) == std::byte{0xff})
                    co_return status_code::malformed_structure;
            }
            mapped_type mapped{};
            status = header.indefinite ? co_await header_item(mapped) : co_await item(mapped);
            if (status != status_code::success)
                co_return status;

            status = sink.deliver(std::move(key), std::move(mapped));
            if (status != status_code::success)
                co_return status;
        }
        co_return status_code::success;
    }

    template <typename T> resumable_task<status_code> retry(T &value) {
        for (;;) {
            co_await wait();
            const auto result = ordinary_(value);
            if (result) {
                commit();
                co_return status_code::success;
            }
            if (result.error() != status_code::incomplete)
                co_return result.error();
            rollback();
            if (closed())
                co_return status_code::incomplete;
        }
    }

    template <typename Tuple> resumable_task<status_code> arguments(Tuple values) { co_return co_await arguments_at(values); }
    template <std::size_t I = 0, typename Tuple> resumable_task<status_code> arguments_at(Tuple &values) {
        if constexpr (I < std::tuple_size_v<Tuple>) {
            auto current = item(std::get<I>(values));
            if (!current.done())
                return await_arguments<I>(values, std::move(current));
            const auto status = current.result();
            if (status != status_code::success)
                return status;
            return arguments_at<I + 1>(values);
        } else
            return status_code::success;
    }
    // Already complete primitive fields do not allocate sequencing frames.
    template <std::size_t I, typename Tuple>
    resumable_task<status_code> await_arguments(Tuple &values, resumable_task<status_code> current) {
        const auto status = co_await std::move(current);
        if (status != status_code::success)
            co_return status;
        co_return co_await arguments_at<I + 1>(values);
    }

    template <typename T, std::size_t I = 0> static consteval bool has_mixin() {
        if constexpr (I == sizeof...(Codecs))
            return false;
        else if constexpr (HasResumableDecodeMixin<std::tuple_element_t<I, mixins>, T> ||
                           HasResumableTranscodeMixin<std::tuple_element_t<I, mixins>, T>)
            return true;
        else
            return has_mixin<T, I + 1>();
    }

    template <typename T, std::size_t I = 0> resumable_task<status_code> mixin(T &value) {
        using codec = std::tuple_element_t<I, mixins>;
        if constexpr (HasResumableDecodeMixin<codec, T>)
            return static_cast<codec &>(*this).resumable_decode(value);
        else if constexpr (HasResumableTranscodeMixin<codec, T>)
            return static_cast<codec &>(*this).resumable_transcode(value);
        else
            return mixin<T, I + 1>(value);
    }

    template <typename T> static consteval bool has_explicit_hook() {
        return HasResumableDecodeMethod<self_type, T> || HasResumableTranscodeMethod<self_type, T> ||
               HasResumableDecodeFreeFunction<self_type, T> || HasResumableTranscodeFreeFunction<self_type, T>;
    }

    template <typename T> resumable_task<status_code> item(T &value) {
        using U = std::remove_cvref_t<T>;
        if constexpr (has_explicit_hook<U>())
            return customization(value);
        else if constexpr (has_mixin<U>())
            return mixin(value);
        else if constexpr (codec_payload_has_customization<ordinary_type, U, payload_decode_context::direct>())
            // Explicit ordinary customizations precede every structural category.
            return ordinary(value); // NOLINT(bugprone-branch-clone)
        else if constexpr (IsStringSink<U>)
            return string_sink(value);
        else if constexpr (IsArraySink<U>)
            return array_sink(value);
        else if constexpr (IsMapSink<U>)
            return map_sink(value);
        else if constexpr (IsAnyBoundedSizeWrapper<U>) {
            using V = std::remove_cvref_t<decltype(value.value())>;
            static_assert(!IsSink<V>, "sinks take no size bounds, return a status_code from the callback instead");
            if constexpr (IsAnyHeader<V> || IsConstView<V>)
                return ordinary(value);
            else if constexpr (IsBoundedSizeWrapper<U>)
                return bounded_container(value.value(), U::min_size, U::max_size);
            else
                return bounded_container(value.value(), value.min_size(), value.max_size());
        } else if constexpr (IsIndefiniteWrapper<U>) {
            check_progress<indefinite_value_t<U>>();
            return bounded_container(value, 0, std::numeric_limits<std::uint64_t>::max());
        } else if constexpr (resumable_wrap_array<U>)
            return group(value.values_, true);
#if CBOR_TAGS_HAS_NAMED_REFLECTION
        else if constexpr (IsNamedMapWrapper<U>)
            return named_map(value.value_);
#endif
        else if constexpr (IsAnyHeader<U> || (IsTag<U> && !IsAggregate<U> && !IsTaggedTuple<U>))
            return ordinary(value);
        else if constexpr (IsString<U> && !IsConstView<U>)
            return container(value);
        else if constexpr (IsRangeOfCborValues<U>) {
            check_progress<U>();
            return container(value);
        } else if constexpr (IsOptional<U>)
            return optional(value);
        else if constexpr (IsVariant<U>)
            return variant(value);
        else if constexpr (IsTaggedTuple<U> || IsUntaggedTuple<U>)
            return tuple(value);
        else if constexpr (IsAggregate<U>)
            return aggregate(value);
        else
            return ordinary(value);
    }

    template <typename T> resumable_task<status_code> customization(T &value) {
        if constexpr (IsClassWithTagOverload<T>) {
            auto        tag = get_major_6_tag_from_class(value);
            status_code status;
            if constexpr (is_customization_tag_wrapper_v<std::remove_cvref_t<decltype(tag)>>) {
                resume_tag_wrapper wrapper{tag};
                status = co_await ordinary(wrapper);
            } else {
                auto normalized = dynamic_tag<std::uint64_t>{static_cast<std::uint64_t>(std::move(tag))};
                status          = co_await ordinary(normalized);
            }
            if (status != status_code::success)
                co_return status;
        }
        if constexpr (HasResumableDecodeMethod<self_type, T>)
            co_return co_await Access::resumable_decode(*this, value);
        else if constexpr (HasResumableTranscodeMethod<self_type, T>)
            co_return co_await Access::resumable_transcode(*this, value);
        else if constexpr (HasResumableDecodeFreeFunction<self_type, T>)
            co_return co_await resumable_decode(*this, value);
        else
            co_return co_await resumable_transcode(*this, value);
    }
    template <typename T> static consteval bool consumes_zero_bytes() {
        if constexpr (has_explicit_hook<T>() || has_mixin<T>())
            return false;
        else
            return builtin_decode_consumes_zero_bytes<ordinary_type, T>();
    }
    template <typename T> static consteval void check_progress() {
        if constexpr (IsMap<T>) {
            static_assert(!consumes_zero_bytes<typename T::key_type>() || !consumes_zero_bytes<typename T::mapped_type>(),
                          "Zero-byte container entries require an explicit count bound");
        } else if constexpr (IsRangeOfCborValues<T> && !IsFixedArray<T>) {
            static_assert(!consumes_zero_bytes<typename T::value_type>(), "Zero-byte container entries require an explicit count bound");
        }
    }
    template <typename T> resumable_task<status_code> bounded_container(T &value, std::uint64_t minimum, std::uint64_t maximum) {
        if constexpr (IsIndefiniteWrapper<T>)
            return container(value.value_, minimum, maximum, true);
        else
            return container(value, minimum, maximum);
    }

    template <payload_decode_context Context, typename T> resumable_task<status_code> contextual_ordinary(T &value) {
        resume_context_value<T, Context> wrapper{value};
        co_return co_await ordinary(wrapper);
    }

    template <typename T> resumable_task<status_code> header_item(T &value) {
        using U = std::remove_cvref_t<T>;
        if constexpr (has_explicit_hook<U>() || has_mixin<U>())
            // Opt-in hooks own dispatch; the final branch handles ordinary direct-compatible items.
            return item(value); // NOLINT(bugprone-branch-clone)
        else if constexpr (codec_payload_has_customization<ordinary_type, U, payload_decode_context::header>())
            return contextual_ordinary<payload_decode_context::header>(value);
        else if constexpr (IsAggregate<U> && !IsTag<U>)
            return header_group(to_tuple(value));
        else if constexpr (IsUntaggedTuple<U>)
            return header_group(std::apply([](auto &...fields) { return std::tie(fields...); }, value));
        else
            return item(value);
    }
    template <typename Tuple> resumable_task<status_code> header_group(Tuple values) {
        constexpr auto count = std::tuple_size_v<Tuple>;
        if constexpr (count == 0) {
            const auto status = co_await available();
            co_return status == status_code::success ? status_code::unexpected_group_size : status;
        } else if constexpr (count > 1 && Options::wrap_groups) {
            resume_group_header header{count};
            const auto          status = co_await ordinary(header);
            if (status != status_code::success)
                co_return status;
            co_return co_await arguments(std::move(values));
        } else {
            const auto status = co_await header_item(std::get<0>(values));
            if (status != status_code::success)
                co_return status;
            co_return co_await arguments(tuple_tail(values));
        }
    }

    template <typename Tuple> resumable_task<status_code> group(Tuple values, bool force = false) {
        constexpr auto count = std::tuple_size_v<std::remove_cvref_t<Tuple>>;
        if (force || (count > 1 && Options::wrap_groups)) {
            auto       header = as_array{count};
            const auto status = co_await ordinary(header);
            if (status != status_code::success)
                co_return status;
        }
        co_return co_await arguments(std::move(values));
    }
    template <typename T> resumable_task<status_code> tuple(T &value) {
        if constexpr (IsTaggedTuple<T>)
            return tagged_tuple(value);
        else
            return group(std::apply([](auto &...fields) { return std::tie(fields...); }, value));
    }

    template <typename T> resumable_task<status_code> tagged_tuple(T &value) {
        const auto status = co_await ordinary(std::get<0>(value));
        if (status != status_code::success)
            co_return status;
        co_return co_await group(tuple_tail(value));
    }

    template <typename T> resumable_task<status_code> aggregate(T &value) {
        if constexpr (HasInlineTag<T> || IsTag<T>)
            return tagged_aggregate(value);
        else
            return group(to_tuple(value));
    }

    template <typename T> resumable_task<status_code> tagged_aggregate(T &value) {
        auto values = to_tuple(value);
        if constexpr (HasInlineTag<T>) {
            auto       tag    = static_tag<T::cbor_tag>{};
            const auto status = co_await ordinary(tag);
            if (status != status_code::success)
                co_return status;
        } else if constexpr (IsTag<T>) {
            const auto status = co_await ordinary(std::get<0>(values));
            if (status != status_code::success)
                co_return status;
        }
        if constexpr (!HasInlineTag<T> && IsTag<T>)
            co_return co_await group(tuple_tail(values));
        else
            co_return co_await group(std::move(values));
    }

#if CBOR_TAGS_HAS_NAMED_REFLECTION
    // Named maps resume per entry, like positional records. Every complete
    // key/value pair stays committed, so a suspension never repeats a field.
    // The rules mirror the one-shot decode_named_map.
    template <typename Object> resumable_task<status_code> named_map(Object &object) {
        using object_type = std::remove_cvref_t<Object>;
        static_assert(named_fixed_member_keys_are_unique<object_type>(),
                      "as_named_map fixed field names must be unique after flattening as_named_group members");
        static_assert(named_flattened_extension_count<object_type>() <= 1U,
                      "as_named_map may contain at most one as_named_extension field after flattening as_named_group members");

        as_map_any header{};
        auto       status = co_await ordinary(header);
        if (status != status_code::success)
            co_return status;

        reset_named_optionals_and_extensions(object);
        named_decode_seen<object_type> seen;
        for (std::uint64_t index = 0; header.indefinite || index != header.size; ++index) {
            if (header.indefinite) {
                status = co_await available();
                if (status != status_code::success)
                    co_return status;
                if (static_cast<std::byte>(*ordinary_.reader_.position_) == std::byte{0xff}) {
                    indefinite_break end{};
                    status = co_await ordinary(end);
                    if (status != status_code::success)
                        co_return status;
                    break;
                }
            }
            status = co_await named_map_entry(object, seen);
            if (status != status_code::success)
                co_return status;
        }
        co_return validate_required_named_members(object, seen) ? status_code::success : status_code::unexpected_group_size;
    }

    template <typename Object, typename Seen> resumable_task<status_code> named_map_entry(Object &object, Seen &seen) {
        using object_type = std::remove_cvref_t<Object>;

        // The key lives in this frame until its value has been decoded.
        std::string key;
        status_code status;
        if constexpr (named_flattened_extension_count<object_type>() == 0) {
            // Reject a key that can't match before reserving its declared length.
            status = co_await container(key, 0, named_longest_fixed_key<object_type>());
            if (status == status_code::size_limit_exceeded)
                co_return status_code::unexpected_group_size;
        } else
            status = co_await item(key);
        if (status != status_code::success)
            co_return status;

        co_return co_await named_map_value(object, std::string_view{key}, seen);
    }

    // Matching is synchronous; only the matched value's decode can suspend.
    template <typename Object, typename Seen>
    resumable_task<status_code> named_map_value(Object &object, std::string_view key, Seen &seen) {
        std::optional<resumable_task<status_code>> value;
        auto                                  status = status_code::success;
        if (!named_member_by_key(object, key, seen, status, value) && !named_extension_by_key(object, key, status, value))
            return status_code::unexpected_group_size;
        if (value)
            return std::move(*value);
        return status;
    }

    template <typename Object, typename Seen>
    bool named_member_by_key(Object &object, std::string_view key, Seen &seen, status_code &status,
                             std::optional<resumable_task<status_code>> &value) {
        return [&]<std::size_t... I>(std::index_sequence<I...>) {
            return (named_member<I>(object, key, seen, status, value) || ...);
        }(std::make_index_sequence<aggregate_member_count<std::remove_cvref_t<Object>>()>{});
    }

    template <std::size_t I, typename Object, typename Seen>
    bool named_member(Object &object, std::string_view key, Seen &seen, status_code &status,
                      std::optional<resumable_task<status_code>> &value) {
        using object_type = std::remove_cvref_t<Object>;
        auto  fields      = to_tuple(object);
        auto &field       = std::get<I>(fields);
        using field_type  = std::remove_cvref_t<decltype(field)>;

        if constexpr (IsNamedGroupWrapper<field_type>)
            return named_member_by_key(field.value_, key, seen, status, value);
        else if constexpr (IsNamedExtensionWrapper<field_type>)
            return false;
        else {
            constexpr std::string_view name = aggregate_member_name<object_type, I>();
            if (key != name)
                return false;
            if (named_key_seen(seen, key)) {
                status = status_code::unexpected_group_size;
                return true;
            }
            seen.add(name);
            value.emplace(named_field(field));
            return true;
        }
    }

    // An optional field takes a value only once it has decoded completely.
    template <typename Field> resumable_task<status_code> named_field(Field &field) {
        if constexpr (IsOptional<Field>)
            return named_optional_field(field);
        else
            return item(field);
    }

    template <typename Field> resumable_task<status_code> named_optional_field(Field &field) {
        typename Field::value_type decoded{};
        const auto                 status = co_await item(decoded);
        if (status == status_code::success)
            field = std::move(decoded);
        co_return status;
    }

    template <typename Object>
    bool named_extension_by_key(Object &object, std::string_view key, status_code &status,
                                std::optional<resumable_task<status_code>> &value) {
        return [&]<std::size_t... I>(std::index_sequence<I...>) {
            return (named_extension<I>(object, key, status, value) || ...);
        }(std::make_index_sequence<aggregate_member_count<std::remove_cvref_t<Object>>()>{});
    }

    template <std::size_t I, typename Object>
    bool named_extension(Object &object, std::string_view key, status_code &status, std::optional<resumable_task<status_code>> &value) {
        auto  fields     = to_tuple(object);
        auto &field      = std::get<I>(fields);
        using field_type = std::remove_cvref_t<decltype(field)>;

        if constexpr (IsNamedGroupWrapper<field_type>)
            return named_extension_by_key(field.value_, key, status, value);
        else if constexpr (IsNamedExtensionWrapper<field_type>) {
            using extension_type = named_extension_value_t<field_type>;
            static_assert(IsMap<extension_type> && IsTextString<typename extension_type::key_type>,
                          "as_named_extension requires a map with text-string keys");
            static_assert(std::constructible_from<typename extension_type::key_type, std::string_view>,
                          "as_named_extension key type must be constructible from std::string_view");
            using key_type = typename extension_type::key_type;

            // Resumable input is never contiguous, so a borrowed key can't point into it.
            if constexpr (is_basic_string_view_v<key_type>) {
                status = status_code::contiguous_view_on_non_contiguous_data;
            } else {
                auto extension_key = make_decode_value_for<key_type>(field.value_);
                if constexpr (requires { extension_key.assign(key.data(), key.size()); })
                    extension_key.assign(key.data(), key.size());
                else
                    extension_key = key_type{key};
                if (field.value_.find(extension_key) != field.value_.end())
                    status = status_code::unexpected_group_size;
                else
                    value.emplace(named_extension_value(field.value_, std::move(extension_key)));
            }
            return true;
        } else
            return false;
    }

    template <typename Map> resumable_task<status_code> named_extension_value(Map &map, typename Map::key_type key) {
        auto       mapped = make_decode_value_for<typename Map::mapped_type>(map);
        const auto status = co_await item(mapped);
        if (status == status_code::success)
            map.emplace(std::move(key), std::move(mapped));
        co_return status;
    }
#endif

    resumable_task<status_code> available() {
        if (!empty())
            return status_code::success;
        if (closed())
            return status_code::incomplete;
        return await_available();
    }

    resumable_task<status_code> await_available() {
        do {
            co_await wait();
            if (!empty())
                co_return status_code::success;
        } while (!closed());
        co_return status_code::incomplete;
    }
    template <typename T> resumable_task<status_code> optional(T &value) {
        auto status = co_await available();
        if (status != status_code::success)
            co_return status;
        if (static_cast<std::byte>(*ordinary_.reader_.position_) == std::byte{0xf6}) {
            std::nullptr_t null{};
            status = co_await ordinary(null);
            if (status == status_code::success)
                value.reset();
        } else {
            auto result = make_decode_value_for_optional<typename T::value_type>(value);
            status      = co_await header_item(result);
            if (status == status_code::success)
                value = std::move(result);
        }
        co_return status;
    }

    // Lookahead uses the same ordinary decoder without committing the speculative header.
    template <typename T> status_code peek_once(T &value) {
        require_retry<T>();
        const auto result = ordinary_(value);
        rollback();
        return result ? status_code::success : result.error();
    }

    template <typename T> resumable_task<status_code> peek(T &value) {
        const auto status = peek_once(value);
        if (status != status_code::incomplete || closed())
            return status;
        return await_peek(value);
    }

    template <typename T> resumable_task<status_code> await_peek(T &value) {
        for (;;) {
            co_await wait();
            const auto status = peek_once(value);
            if (status != status_code::incomplete || closed())
                co_return status;
        }
    }
    template <typename T> static bool matches_tag(std::uint64_t tag) {
        if constexpr (IsOptional<T>)
            return matches_tag<typename T::value_type>(tag);
        else if constexpr (IsVariant<T>)
            return with_variant_alternatives<T>([tag]<typename... U>() { return (matches_tag<U>(tag) || ...); });
        else if constexpr (IsTagHeader<T>)
            return true;
        else if constexpr (IsTag<T>)
            return tag == static_cast<std::uint64_t>(get_tag_from_any<T>());
        else
            return false;
    }

    template <typename T> resumable_task<status_code> variant(T &value) {
        require_unambiguous_variant_dispatch<T>();
        auto status = co_await available();
        if (status != status_code::success)
            co_return status;
        const auto [major, info] = ordinary_.read_initial_byte();
        rollback();
        as_tag_any tag{};
        if (major == major_type::Tag) {
            status = co_await peek(tag);
            if (status != status_code::success)
                co_return status;
        }
        const auto start = offset();
        status           = co_await variant_alternative<false>(value, major, info, tag.tag);
        if (status == status_code::no_match_in_variant_on_buffer && major == major_type::Simple && offset() == start) {
            // A rejected tentative candidate may leave its header consumed.
            // Explicitly committed progress is terminal and cannot be retried.
            rollback();
            status = co_await variant_alternative<true>(value, major, info, tag.tag);
        }
        co_return status;
    }
    template <bool CatchAll, typename T, std::size_t I = 0>
    resumable_task<status_code> variant_alternative(T &value, major_type major, std::byte info, std::uint64_t tag) {
        if constexpr (I == variant_size_v<T>)
            return status_code::no_match_in_variant_on_buffer;
        else {
            using U            = variant_alternative_t<I, T>;
            const bool matches = major == major_type::Simple ? matches_simple_dispatch<CatchAll, U>(info)
                                 : major == major_type::Tag  ? matches_tag<U>(tag)
                                                             : matches_major_dispatch<U>(major);
            if (matches)
                return selected_alternative<I>(value);
            return variant_alternative<CatchAll, T, I + 1>(value, major, info, tag);
        }
    }

    // A selected alternative stays alive in its owning frame until decoding
    // finishes. Only metadata-only candidate selection can return eagerly.
    template <std::size_t I, typename T> resumable_task<status_code> selected_alternative(T &value) {
        using U = variant_alternative_t<I, T>;
        U           decoded{};
        status_code status;
        if constexpr (has_explicit_hook<U>() || has_mixin<U>())
            status = co_await item(decoded);
        else if constexpr (IsTag<U> && !IsVariant<U> && !IsOptional<U>) {
            if constexpr (codec_payload_has_customization<ordinary_type, U, payload_decode_context::tag>())
                status = co_await contextual_ordinary<payload_decode_context::tag>(decoded);
            else
                status = co_await item(decoded);
        } else
            status = co_await header_item(decoded);
        if (status == status_code::success)
            variant_assign<I>(value, std::move(decoded));
        co_return is_retriable_variant_mismatch(status) ? status_code::no_match_in_variant_on_buffer : status;
    }

    // Probe one codec at a time so missing overloads remain substitution failures
    // on MSVC instead of hard errors inside a pack-expanded requires expression.
    template <typename Codec, typename T> static consteval bool ordinary_codec_leaf_override() {
        return requires(Codec &codec, T &value) { codec.decode(value); } ||
               requires(Codec &codec, T &value) { codec.decode(value, major_type{}, std::byte{}); };
    }

    template <typename T> static consteval bool ordinary_leaf_override() {
        return (ordinary_codec_leaf_override<Codecs<ordinary_type>, T>() || ...);
    }

    template <typename T> static consteval bool builtin_leaf() {
        if constexpr (!resumable_builtin_leaf<T> || has_explicit_hook<T>() || has_mixin<T>()) {
            // Explicit hooks own dispatch without instantiating ordinary probes.
            return false;
        } else {
            return !ordinary_leaf_override<T>() && !codec_payload_has_customization<ordinary_type, T, payload_decode_context::direct>() &&
                   !codec_payload_has_customization<ordinary_type, T, payload_decode_context::header>();
        }
    }

    template <typename T> static consteval bool builtin_leaf_array() {
        if constexpr (IsArray<T> && !IsString<T>)
            return builtin_leaf<typename T::value_type>();
        else
            return false;
    }

    template <typename T> static consteval bool builtin_leaf_map() {
        if constexpr (IsMap<T>)
            return builtin_leaf<typename T::key_type>() && builtin_leaf<typename T::mapped_type>();
        else
            return false;
    }

    template <typename Decode> static status_code attempt_decode(Decode &&decode) {
        try {
            return decode();
        } catch (const decode_status_exception &error) { return error.status; } catch (const parse_incomplete_exception &) {
            return status_code::incomplete;
        } catch (...) { return resumable_exception_status(); }
    }

    // Drain ordinary elements synchronously. A failed child leaves the logical
    // count and destination at the last complete element, even after an exception.
    template <typename T>
#if defined(__GNUC__) && !defined(__clang__)
    // The cursor and byte counter form a scalar dependency chain. Packing their
    // updates into SIMD registers adds transfers on every decoded byte.
    [[gnu::optimize("no-tree-slp-vectorize")]]
#endif
    status_code segmented_leaf_elements(T &value, appender<T> &append, std::uint64_t &remaining) {
        auto completed = ordinary_.reader_;
        // Standard arithmetic vectors have identical copy/move semantics. A
        // const scalar keeps the simple push_back path in this segmented loop.
        auto append_element = [&append](T &output, typename T::value_type &&element) {
            if constexpr (std::is_arithmetic_v<typename T::value_type> && std::same_as<T, std::vector<typename T::value_type>>)
                append(output, std::as_const(element));
            else
                append(output, std::move(element));
        };
        const auto status = attempt_decode(
            [&] { return decode_array_elements(ordinary_, value, append_element, remaining, [&] { completed = ordinary_.reader_; }); });

        ordinary_.reader_ = completed;
        commit();
        return status;
    }

    struct window_decode_result {
        status_code status;
        std::size_t completed;
    };

    // Keep the bounded scalar loop independent of source rebinding and retry
    // state. Only complete children contribute to the published byte count.
    template <std::size_t MinimumSize, typename Decode>
    static window_decode_result decode_leaf_window(std::span<const std::byte> window, Decode &&decode) {
        decoder<std::span<const std::byte>, Options> contiguous(window);
        window_decode_result                         result{status_code::success, 0};
        result.status = attempt_decode([&] {
            return decode(
                contiguous, [&] { result.completed = contiguous.reader_.position_; },
                [&] { return window.size() - contiguous.reader_.position_ >= MinimumSize; });
        });
        return result;
    }

    template <typename T>
    status_code leaf_window(T &value, appender<T> &append, std::uint64_t &remaining, std::span<const std::byte> window) {
        const auto result = decode_leaf_window<resumable_leaf_decode_size<typename T::value_type>()>(
            window, [&](auto &contiguous, auto completed, auto available) {
                // Keep conversion and strictness policy in the core scalar codec.
                return decode_array_elements(contiguous, value, append, remaining, completed, available);
            });

        resumable_source_access::consume_window(source_, ordinary_.reader_.position_, result.completed);
        ordinary_.reader_.current_offset_ += result.completed;
        commit();
        return result.status;
    }

    // Borrow through the working cursor and publish the source position only
    // after the shared scalar loop has finished.
    template <typename T> status_code leaf_elements(T &value, appender<T> &append, std::uint64_t &remaining) {
        while (remaining != 0) {
            const auto window = resumable_source_access::borrow_window(source_, ordinary_.reader_.position_);
            if (!window)
                return segmented_leaf_elements(value, append, remaining);

            if (window->size() >= resumable_leaf_decode_size<typename T::value_type>()) {
                const auto status = leaf_window(value, append, remaining, *window);
                if (status != status_code::success)
                    return status;
            } else {
                // Only the boundary child needs the chunk-aware reader. It can
                // cross already admitted chunks without suspending the container.
                std::uint64_t one    = 1;
                const auto    status = segmented_leaf_elements(value, append, one);
                remaining -= 1 - one;
                if (status != status_code::success)
                    return status;
            }
        }
        return status_code::success;
    }

    template <typename T>
    status_code indefinite_leaf_elements(T &value, appender<T> &append, std::uint64_t &count, bool &ended, std::uint64_t minimum,
                                         std::uint64_t maximum) {
        while (!ended) {
            const auto  window = resumable_source_access::borrow_window(source_, ordinary_.reader_.position_);
            status_code status;
            if (window && window->size() >= resumable_leaf_decode_size<typename T::value_type>()) {
                const auto result = decode_leaf_window<resumable_leaf_decode_size<typename T::value_type>()>(
                    *window, [&](auto &contiguous, auto completed, auto available) {
                        return decode_indefinite_array_elements<true>(contiguous, value, append, count, ended, minimum, maximum, completed,
                                                                      available);
                    });
                resumable_source_access::consume_window(source_, ordinary_.reader_.position_, result.completed);
                ordinary_.reader_.current_offset_ += result.completed;
                status = result.status;
            } else {
                auto       completed = ordinary_.reader_;
                const auto start     = count;
                status               = attempt_decode([&] {
                    return decode_indefinite_array_elements<true>(
                        ordinary_, value, append, count, ended, minimum, maximum, [&] { completed = ordinary_.reader_; },
                        [&] { return !window || count == start; });
                });
                ordinary_.reader_    = completed;
            }
            commit();
            if (status != status_code::success)
                return status;
        }
        return status_code::success;
    }

    template <bool Indefinite, typename T>
    status_code leaf_map_elements(T &value, appender<T> &append, std::optional<decoded_map_pair<T>> &entry, bool &key_complete,
                                  std::uint64_t &count, bool &ended, std::uint64_t minimum, std::uint64_t maximum) {
        constexpr auto maximum_size =
            resumable_leaf_decode_size<typename T::key_type>() + resumable_leaf_decode_size<typename T::mapped_type>();
        while (!ended && (Indefinite || count != maximum)) {
            const auto  window = resumable_source_access::borrow_window(source_, ordinary_.reader_.position_);
            status_code status;
            if (window && window->size() >= maximum_size) {
                const auto result = decode_leaf_window<maximum_size>(*window, [&](auto &contiguous, auto completed, auto available) {
                    return decode_map_elements<Indefinite, true>(contiguous, value, append, entry, key_complete, count, ended, minimum,
                                                                 maximum, completed, available);
                });
                resumable_source_access::consume_window(source_, ordinary_.reader_.position_, result.completed);
                ordinary_.reader_.current_offset_ += result.completed;
                status = result.status;
            } else {
                auto       completed = ordinary_.reader_;
                const auto start     = count;
                status               = attempt_decode([&] {
                    return decode_map_elements<Indefinite, true>(
                        ordinary_, value, append, entry, key_complete, count, ended, minimum, maximum,
                        [&] { completed = ordinary_.reader_; }, [&] { return !window || count == start; });
                });
                ordinary_.reader_    = completed;
            }
            commit();
            if (status != status_code::success)
                return status;
        }
        return status_code::success;
    }

    template <bool Indefinite, typename T>
    resumable_task<status_code> leaf_map(T &value, appender<T> &append, std::uint64_t minimum, std::uint64_t maximum) {
        std::optional<decoded_map_pair<T>> entry;
        bool                               key_complete{};
        bool                               ended{};
        std::uint64_t                      count{};
        while (!ended && (Indefinite || count != maximum)) {
            const auto status = leaf_map_elements<Indefinite>(value, append, entry, key_complete, count, ended, minimum, maximum);
            if (status != status_code::incomplete || closed())
                co_return status;
            co_await wait();
        }
        co_return status_code::success;
    }

    template <typename T>
    resumable_task<status_code> container(T &value, std::uint64_t minimum = 0,
                                          std::uint64_t maximum            = std::numeric_limits<std::uint64_t>::max(),
                                          bool          require_indefinite = false) {
        using header_type =
            std::conditional_t<IsTextString<T>, as_text_any,
                               std::conditional_t<IsBinaryString<T>, as_bstr_any, std::conditional_t<IsMap<T>, as_map_any, as_array_any>>>;
        header_type header{};
        auto        status = co_await ordinary(header);
        if (status != status_code::success)
            co_return status;
        if (require_indefinite && !header.indefinite) {
            co_return major_type_mismatch_status(IsTextString<T>     ? major_type::TextString
                                                 : IsBinaryString<T> ? major_type::ByteString
                                                 : IsMap<T>          ? major_type::Map
                                                                     : major_type::Array);
        }
        if (!header.indefinite && (header.size < minimum || header.size > maximum))
            co_return status_code::size_limit_exceeded;
        if constexpr (IsFixedArray<T>) {
            if (header.indefinite || header.size != value.size())
                co_return status_code::unexpected_group_size;
        }
        appender<T> append;
        // The caller owns allocation limits. Appenders opt into element-count
        // reservation; some containers reserve descriptors rather than elements.
        if constexpr (AppendReservable<T> && HasAppendReservation<appender<T>, T>) {
            if (!header.indefinite && header.size != 0)
                append.reserve_for_append(value, header.size);
        }

        if constexpr (IsString<T>) {
            if (!header.indefinite)
                co_return co_await payload(value, header.size, append);
        }
        if constexpr (builtin_leaf_map<T>()) {
            if (header.indefinite)
                co_return co_await leaf_map<true>(value, append, minimum, maximum);
            else
                co_return co_await leaf_map<false>(value, append, 0, header.size);
        }
        if constexpr (builtin_leaf_array<T>()) {
            if (!header.indefinite) {
                auto remaining = header.size;
                while (remaining != 0) {
                    status = leaf_elements(value, append, remaining);
                    if (status != status_code::incomplete || closed())
                        co_return status;
                    co_await wait();
                }
                co_return status_code::success;
            } else {
                std::uint64_t count{};
                bool          ended{};
                while (!ended) {
                    status = indefinite_leaf_elements(value, append, count, ended, minimum, maximum);
                    if (status != status_code::incomplete || closed())
                        co_return status;
                    co_await wait();
                }
                co_return status_code::success;
            }
        }

        std::uint64_t count{};
        while (header.indefinite || count < header.size) {
            if (header.indefinite) {
                status = co_await available();
                if (status != status_code::success)
                    co_return status;
            }
            if (header.indefinite && static_cast<std::byte>(*ordinary_.reader_.position_) == std::byte{0xff}) {
                indefinite_break end{};
                status = co_await ordinary(end);
                if (status != status_code::success)
                    co_return status;
                co_return count < minimum ? status_code::size_limit_exceeded : status_code::success;
            }
            if constexpr (IsString<T>) {
                const auto initial        = static_cast<std::byte>(*ordinary_.reader_.position_);
                const auto expected_major = IsTextString<T> ? std::byte{0x60} : std::byte{0x40};
                if ((initial & std::byte{0xe0}) != expected_major || (initial & std::byte{0x1f}) == std::byte{31})
                    co_return status_code::malformed_structure;
                header_type chunk{};
                status = co_await ordinary(chunk);
                if (status != status_code::success)
                    co_return status;
                if (chunk.indefinite)
                    co_return status_code::malformed_structure;
                if (chunk.size > maximum - count)
                    co_return status_code::size_limit_exceeded;
                if constexpr (AppendReservable<T> && HasGeometricAppendReservation<appender<T>, T>) {
                    if (chunk.size != 0)
                        append.template reserve_for_append<true>(value, chunk.size);
                }
                status = co_await payload(value, chunk.size, append);
                count += chunk.size;
            } else {
                if (count == maximum)
                    co_return status_code::size_limit_exceeded;
                if constexpr (IsMap<T>) {
                    auto key    = make_decode_value_for<typename T::key_type>(value);
                    auto mapped = make_decode_value_for<typename T::mapped_type>(value);
                    status      = header.indefinite ? co_await header_item(key) : co_await item(key);
                    if (status != status_code::success)
                        co_return status;
                    if (header.indefinite) {
                        status = co_await available();
                        if (status != status_code::success)
                            co_return status;
                        if (static_cast<std::byte>(*ordinary_.reader_.position_) == std::byte{0xff})
                            co_return status_code::malformed_structure;
                    }
                    status = header.indefinite ? co_await header_item(mapped) : co_await item(mapped);
                    if (status == status_code::success)
                        append(value, std::pair{std::move(key), std::move(mapped)});
                } else {
                    auto decoded = make_decode_value_for<typename T::value_type>(value);
                    status       = header.indefinite ? co_await header_item(decoded) : co_await item(decoded);
                    if (status == status_code::success)
                        append(value, std::move(decoded));
                }
                ++count;
            }
            if (status != status_code::success)
                co_return status;
        }
        co_return status_code::success;
    }
};
} // namespace cbor::tags::detail
