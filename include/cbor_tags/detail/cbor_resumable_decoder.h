#pragma once

#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/detail/cbor_resumable_control.h"
#include "cbor_tags/detail/cbor_resumable_leaf.h"
#include "cbor_tags/detail/cbor_resumable_source.h"

#include <algorithm>
#include <limits>
#include <tuple>

namespace cbor::tags::detail {

template <typename T> inline constexpr bool    resumable_wrap_array                      = false;
template <typename... T> inline constexpr bool resumable_wrap_array<wrap_as_array<T...>> = true;

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
            if constexpr (requires { decoder.decode(std::move(tag)); })
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
    Source                     &source_;
    resume_control             &control_;
    ordinary_type               ordinary_;
    decltype(ordinary_.reader_) committed_;

  public:
    using source_type                           = Source;
    using options                               = Options;
    static constexpr resume_state waiting_state = resume_state::need_input;

    resumable_decode_context(Source &source, resume_control &control)
        : source_(source), control_(control), ordinary_(source), committed_(ordinary_.reader_) {
        ordinary_.reader_.position_       = source.committed_position();
        ordinary_.reader_.current_offset_ = ordinary_.reader_.position_.offset();
        committed_                        = ordinary_.reader_;
    }
    Source        &source() noexcept { return source_; }
    std::size_t    offset() const noexcept { return committed_.current_offset_; }
    auto           remaining_input() const { return source_.remaining(committed_.position_); }
    ordinary_type &ordinary() noexcept { return ordinary_; }
    auto           wait() noexcept { return control_.wait(); }
    bool           empty() const { return ordinary_.reader_.empty(source_); }
    bool           eof() const { return source_.eof(); }
    void           commit() {
        committed_ = ordinary_.reader_;
        source_.commit(committed_.position_);
    }
    void rollback() { ordinary_.reader_ = committed_; }

    // A complete normal call is the sole retry boundary, including its nested calls.
    template <typename T> resumable<status_code> ordinary(T &value) {
        const auto result = ordinary_(value);
        if (result) {
            commit();
            return status_code::success;
        }
        if (result.error() != status_code::incomplete)
            return result.error();
        rollback();
        if (eof())
            return status_code::incomplete;
        return retry(value);
    }

    template <typename... T> resumable<status_code> operator()(T &&...value) {
        return arguments(std::tuple<T...>{std::forward<T>(value)...});
    }

    // Bulk string transfer uses the same destination appender as one-shot decoding.
    template <typename T> resumable<status_code> payload(T &value, std::uint64_t length, appender<T> &append) {
        drain_payload(value, length, append);
        if (length == 0)
            return status_code::success;
        if (eof())
            return status_code::incomplete;
        return await_payload(value, length, append);
    }

  private:
    template <typename T> void drain_payload(T &value, std::uint64_t &length, appender<T> &append) {
        while (length != 0 && !empty()) {
            if (const auto span = source_.borrow_window(ordinary_.reader_.position_); span && !span->empty()) {
                const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(length, span->size()));
                append(value, span->first(count));
                source_.consume_window(ordinary_.reader_.position_, count);
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
    template <typename T> resumable<status_code> await_payload(T &value, std::uint64_t length, appender<T> &append) {
        while (length != 0) {
            co_await wait();
            drain_payload(value, length, append);
            if (length != 0 && eof())
                co_return status_code::incomplete;
        }
        co_return status_code::success;
    }

    template <typename T> resumable<status_code> retry(T &value) {
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
            if (eof())
                co_return status_code::incomplete;
        }
    }

    template <typename Tuple> resumable<status_code>                    arguments(Tuple values) { co_return co_await arguments_at(values); }
    template <std::size_t I = 0, typename Tuple> resumable<status_code> arguments_at(Tuple &values) {
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
    template <std::size_t I, typename Tuple> resumable<status_code> await_arguments(Tuple &values, resumable<status_code> current) {
        const auto status = co_await std::move(current);
        if (status != status_code::success)
            co_return status;
        co_return co_await arguments_at<I + 1>(values);
    }

    template <typename T, std::size_t I = 0> static consteval bool has_mixin() {
        if constexpr (I == sizeof...(Codecs))
            return false;
        else if constexpr (
            requires(std::tuple_element_t<I, mixins> &codec, T &value) {
                { codec.resumable_decode(value) } -> std::same_as<resumable<status_code>>;
            } ||
            requires(std::tuple_element_t<I, mixins> &codec, T &value) {
                { codec.resumable_transcode(value) } -> std::same_as<resumable<status_code>>;
            })
            return true;
        else
            return has_mixin<T, I + 1>();
    }

    template <typename T, std::size_t I = 0> resumable<status_code> mixin(T &value) {
        using codec = std::tuple_element_t<I, mixins>;
        if constexpr (requires(codec &c) {
                          { c.resumable_decode(value) } -> std::same_as<resumable<status_code>>;
                      })
            return static_cast<codec &>(*this).resumable_decode(value);
        else if constexpr (requires(codec &c) {
                               { c.resumable_transcode(value) } -> std::same_as<resumable<status_code>>;
                           })
            return static_cast<codec &>(*this).resumable_transcode(value);
        else
            return mixin<T, I + 1>(value);
    }

    template <typename T> static consteval bool has_explicit_hook() {
        return requires(self_type &self, T &value) {
            { Access::resumable_decode(self, value) } -> std::same_as<resumable<status_code>>;
        } || requires(self_type &self, T &value) {
            { Access::resumable_transcode(self, value) } -> std::same_as<resumable<status_code>>;
        } || requires(self_type &self, T &value) {
            { resumable_decode(self, value) } -> std::same_as<resumable<status_code>>;
        } || requires(self_type &self, T &value) {
            { resumable_transcode(self, value) } -> std::same_as<resumable<status_code>>;
        };
    }

    template <typename T> resumable<status_code> item(T &value) {
        using U = std::remove_cvref_t<T>;
        if constexpr (has_explicit_hook<U>())
            return customization(value);
        else if constexpr (has_mixin<U>())
            return mixin(value);
        else if constexpr (codec_payload_has_customization<ordinary_type, U, payload_decode_context::direct>())
            // Explicit ordinary customizations precede every structural category.
            return ordinary(value); // NOLINT(bugprone-branch-clone)
        else if constexpr (IsAnyBoundedSizeWrapper<U>) {
            using V = std::remove_cvref_t<decltype(value.value())>;
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

    template <typename T> resumable<status_code> customization(T &value) {
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
        if constexpr (requires {
                          { Access::resumable_decode(*this, value) } -> std::same_as<resumable<status_code>>;
                      })
            co_return co_await Access::resumable_decode(*this, value);
        else if constexpr (requires {
                               { Access::resumable_transcode(*this, value) } -> std::same_as<resumable<status_code>>;
                           })
            co_return co_await Access::resumable_transcode(*this, value);
        else if constexpr (requires {
                               { resumable_decode(*this, value) } -> std::same_as<resumable<status_code>>;
                           })
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
    template <typename T> resumable<status_code> bounded_container(T &value, std::uint64_t minimum, std::uint64_t maximum) {
        if constexpr (IsIndefiniteWrapper<T>)
            return container(value.value_, minimum, maximum, true);
        else
            return container(value, minimum, maximum);
    }

    template <payload_decode_context Context, typename T> resumable<status_code> contextual_ordinary(T &value) {
        resume_context_value<T, Context> wrapper{value};
        co_return co_await ordinary(wrapper);
    }

    template <typename T> resumable<status_code> header_item(T &value) {
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
    template <typename Tuple> resumable<status_code> header_group(Tuple values) {
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

    template <typename Tuple> resumable<status_code> group(Tuple values, bool force = false) {
        constexpr auto count = std::tuple_size_v<std::remove_cvref_t<Tuple>>;
        if (force || (count > 1 && Options::wrap_groups)) {
            auto       header = as_array{count};
            const auto status = co_await ordinary(header);
            if (status != status_code::success)
                co_return status;
        }
        co_return co_await arguments(std::move(values));
    }
    template <typename T> resumable<status_code> tuple(T &value) {
        if constexpr (IsTaggedTuple<T>)
            return tagged_tuple(value);
        else
            return group(std::apply([](auto &...fields) { return std::tie(fields...); }, value));
    }

    template <typename T> resumable<status_code> tagged_tuple(T &value) {
        const auto status = co_await ordinary(std::get<0>(value));
        if (status != status_code::success)
            co_return status;
        co_return co_await group(tuple_tail(value));
    }

    template <typename T> resumable<status_code> aggregate(T &value) {
        if constexpr (HasInlineTag<T> || IsTag<T>)
            return tagged_aggregate(value);
        else
            return group(to_tuple(value));
    }

    template <typename T> resumable<status_code> tagged_aggregate(T &value) {
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

    resumable<status_code> available() {
        if (!empty())
            return status_code::success;
        if (eof())
            return status_code::incomplete;
        return await_available();
    }

    resumable<status_code> await_available() {
        do {
            co_await wait();
            if (!empty())
                co_return status_code::success;
        } while (!eof());
        co_return status_code::incomplete;
    }
    template <typename T> resumable<status_code> optional(T &value) {
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
        const auto result = ordinary_(value);
        rollback();
        return result ? status_code::success : result.error();
    }

    template <typename T> resumable<status_code> peek(T &value) {
        const auto status = peek_once(value);
        if (status != status_code::incomplete || eof())
            return status;
        return await_peek(value);
    }

    template <typename T> resumable<status_code> await_peek(T &value) {
        for (;;) {
            co_await wait();
            const auto status = peek_once(value);
            if (status != status_code::incomplete || eof())
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

    template <typename T> resumable<status_code> variant(T &value) {
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
    resumable<status_code> variant_alternative(T &value, major_type major, std::byte info, std::uint64_t tag) {
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
    template <std::size_t I, typename T> resumable<status_code> selected_alternative(T &value) {
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

    template <typename T> static consteval bool ordinary_leaf_override() {
        return (requires(Codecs<ordinary_type> &codec, T &value) { codec.decode(value); } || ...) ||
               (requires(Codecs<ordinary_type> &codec, T &value) { codec.decode(value, major_type{}, std::byte{}); } || ...);
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
    template <typename T> status_code segmented_leaf_elements(T &value, appender<T> &append, std::uint64_t &remaining) {
        auto       completed = ordinary_.reader_;
        const auto status    = attempt_decode(
            [&] { return decode_array_elements(ordinary_, value, append, remaining, [&] { completed = ordinary_.reader_; }); });

        ordinary_.reader_ = completed;
        commit();
        return status;
    }

    template <typename T>
    status_code leaf_window(T &value, appender<T> &append, std::uint64_t &remaining, std::span<const std::byte> window) {
        decoder<std::span<const std::byte>, Options> contiguous(window);
        std::size_t                                  completed{};
        const auto                                   status = attempt_decode([&] {
            // Keep conversion and strictness policy in the core scalar codec.
            return decode_array_elements(
                contiguous, value, append, remaining, [&] { completed = contiguous.reader_.position_; },
                [&] { return window.size() - contiguous.reader_.position_ >= resumable_leaf_decode_size<typename T::value_type>(); });
        });

        source_.advance(ordinary_.reader_.position_, completed);
        ordinary_.reader_.current_offset_ += completed;
        commit();
        return status;
    }

    // Discover leaf spans through a cursor copy and publish the source position
    // only after the shared scalar loop has finished.
    template <typename T> status_code leaf_elements(T &value, appender<T> &append, std::uint64_t &remaining) {
        while (remaining != 0) {
            const auto window = source_.available_span(ordinary_.reader_.position_);
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
            const auto  window = source_.available_span(ordinary_.reader_.position_);
            status_code status;
            if (window && window->size() >= resumable_leaf_decode_size<typename T::value_type>()) {
                decoder<std::span<const std::byte>, Options> contiguous(*window);
                std::size_t                                  completed{};
                status = attempt_decode([&] {
                    return decode_indefinite_array_elements<true>(
                        contiguous, value, append, count, ended, minimum, maximum, [&] { completed = contiguous.reader_.position_; },
                        [&] {
                            return window->size() - contiguous.reader_.position_ >= resumable_leaf_decode_size<typename T::value_type>();
                        });
                });
                source_.advance(ordinary_.reader_.position_, completed);
                ordinary_.reader_.current_offset_ += completed;
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
            const auto  window = source_.available_span(ordinary_.reader_.position_);
            status_code status;
            if (window && window->size() >= maximum_size) {
                decoder<std::span<const std::byte>, Options> contiguous(*window);
                std::size_t                                  completed{};
                status = attempt_decode([&] {
                    return decode_map_elements<Indefinite, true>(
                        contiguous, value, append, entry, key_complete, count, ended, minimum, maximum,
                        [&] { completed = contiguous.reader_.position_; },
                        [&] { return window->size() - contiguous.reader_.position_ >= maximum_size; });
                });
                source_.advance(ordinary_.reader_.position_, completed);
                ordinary_.reader_.current_offset_ += completed;
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
    resumable<status_code> leaf_map(T &value, appender<T> &append, std::uint64_t minimum, std::uint64_t maximum) {
        std::optional<decoded_map_pair<T>> entry;
        bool                               key_complete{};
        bool                               ended{};
        std::uint64_t                      count{};
        while (!ended && (Indefinite || count != maximum)) {
            const auto status = leaf_map_elements<Indefinite>(value, append, entry, key_complete, count, ended, minimum, maximum);
            if (status != status_code::incomplete || eof())
                co_return status;
            co_await wait();
        }
        co_return status_code::success;
    }

    template <typename T>
    resumable<status_code> container(T &value, std::uint64_t minimum = 0, std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max(),
                                     bool require_indefinite = false) {
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
        if constexpr (AppendReservable<T> && requires { append.reserve_for_append(value, header.size); }) {
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
                    if (status != status_code::incomplete || eof())
                        co_return status;
                    co_await wait();
                }
                co_return status_code::success;
            } else {
                std::uint64_t count{};
                bool          ended{};
                while (!ended) {
                    status = indefinite_leaf_elements(value, append, count, ended, minimum, maximum);
                    if (status != status_code::incomplete || eof())
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
                if constexpr (AppendReservable<T> && requires { append.template reserve_for_append<true>(value, chunk.size); }) {
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
