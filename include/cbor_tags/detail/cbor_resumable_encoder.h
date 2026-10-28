#pragma once

#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/detail/cbor_item_shape.h"
#include "cbor_tags/detail/cbor_resumable_control.h"
#include "cbor_tags/detail/cbor_resumable_source.h"

#include <algorithm>
#include <limits>
#include <tuple>
#include <vector>

namespace cbor::tags::detail {

template <typename T> inline constexpr bool    resumable_encode_wrap_array                      = false;
template <typename... T> inline constexpr bool resumable_encode_wrap_array<wrap_as_array<T...>> = true;

template <typename Source, typename Options, template <typename> typename... Codecs>
class resumable_encode_context : public Codecs<resumable_encode_context<Source, Options, Codecs...>>... {
    using self_type = resumable_encode_context;
    using ordinary_type =
        encoder<Source, Options, cbor_header_encoder, cbor_indefinite_encoder, cbor_optional_encoder, cbor_variant_encoder, Codecs...>;
    using mixins = std::tuple<Codecs<self_type>...>;

    Source                        &source_;
    resume_control                &control_;
    ordinary_type                  ordinary_;
    typename Source::position_type start_;

  public:
    using source_type                           = Source;
    using options                               = Options;
    static constexpr resume_state waiting_state = resume_state::need_output;

    resumable_encode_context(Source &source, resume_control &control)
        : source_(source), control_(control), ordinary_(source), start_(source.committed_position()) {}

    Source        &source() noexcept { return source_; }
    std::size_t    offset() const { return source_.committed_position().offset(); }
    auto           produced() const { return source_.produced(start_, source_.committed_position()); }
    auto           remaining_output() const { return source_.remaining_output(); }
    ordinary_type &ordinary() noexcept { return ordinary_; }
    auto           wait() noexcept { return control_.wait(); }
    bool           eof() const { return source_.eof(); }
    void           commit() { source_.commit(); }
    void           rollback() { source_.restore(source_.committed_position()); }

    // A complete normal call is the retry boundary, including nested customizations.
    // Its writes stay tentative until the whole call succeeds.
    template <typename T> resumable<status_code> ordinary(T &value) {
        return attempt([this, &value] {
            const auto result = ordinary_(value);
            return result ? status_code::success : result.error();
        });
    }

    template <typename... T> resumable<status_code> operator()(T &&...value) {
        return arguments(std::tuple<T...>{std::forward<T>(value)...});
    }

    // Payload chunks commit independently, so a long string never has to fit in
    // one output window. Contiguous ranges keep the source's bulk-write path.
    template <typename Range> resumable<status_code> payload(Range &value) {
        if constexpr (std::ranges::contiguous_range<Range> && std::ranges::sized_range<Range>) {
            auto bytes = as_byte_span(value);
            while (!bytes.empty()) {
                const auto available = source_.available_output_span();
                if (available && !available->empty()) {
                    const auto count = std::min(bytes.size(), available->size());
                    source_.write(bytes.first(count));
                    commit();
                    bytes = bytes.subspan(count);
                } else if (available) {
                    if (eof())
                        co_return status_code::incomplete;
                    co_await wait();
                } else {
                    const auto status = co_await byte(bytes.front());
                    if (status != status_code::success)
                        co_return status;
                    bytes = bytes.subspan(1);
                }
            }
        } else {
            for (auto &&value_byte : value) {
                const auto status = co_await byte(static_cast<std::byte>(value_byte));
                if (status != status_code::success)
                    co_return status;
            }
        }
        co_return status_code::success;
    }

  private:
    template <typename Write> status_code write_once(Write &write) {
        status_code status;
        try {
            status = write();
        } catch (const encode_status_exception &error) { status = error.status; } catch (...) {
            status = resumable_exception_status();
        }

        if (status == status_code::success)
            commit();
        else
            rollback();
        return status;
    }

    // Keep the common completed call inline; only an incomplete call needs a frame.
    template <typename Write> resumable<status_code> attempt(Write write) {
        const auto status = write_once(write);
        if (status != status_code::incomplete || eof())
            return status;
        return retry(std::move(write));
    }

    template <typename Write> resumable<status_code> retry(Write write) {
        for (;;) {
            co_await wait();
            const auto status = write_once(write);
            if (status != status_code::incomplete || eof())
                co_return status;
        }
    }

    resumable<status_code> header(std::uint64_t size, std::byte major) {
        return attempt([this, size, major] {
            ordinary_.encode_major_and_size(size, major);
            return status_code::success;
        });
    }

    resumable<status_code> byte(std::byte value) {
        return attempt([this, value] {
            ordinary_.appender_(source_, value);
            return status_code::success;
        });
    }

    template <typename Tuple> resumable<status_code> arguments(Tuple values) { co_return co_await arguments_at(values); }

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

    // Ready primitive fields sequence without allocating their own coroutine frames.
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
                { codec.resumable_encode(value) } -> std::same_as<resumable<status_code>>;
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
                          { c.resumable_encode(value) } -> std::same_as<resumable<status_code>>;
                      })
            return static_cast<codec &>(*this).resumable_encode(value);
        else if constexpr (requires(codec &c) {
                               { c.resumable_transcode(value) } -> std::same_as<resumable<status_code>>;
                           })
            return static_cast<codec &>(*this).resumable_transcode(value);
        else
            return mixin<T, I + 1>(value);
    }

    template <typename T> static consteval int explicit_hook_kind() {
        using context = self_type;
        if constexpr (requires(context &self, T &value) {
                          { Access::resumable_encode(self, value) } -> std::same_as<resumable<status_code>>;
                      })
            return 1;
        else if constexpr (requires(context &self, T &value) {
                               { Access::resumable_transcode(self, value) } -> std::same_as<resumable<status_code>>;
                           })
            return 2;
        else if constexpr (requires(context &self, T &value) {
                               { resumable_encode(self, value) } -> std::same_as<resumable<status_code>>;
                           })
            return 3;
        else if constexpr (requires(context &self, T &value) {
                               { resumable_transcode(self, value) } -> std::same_as<resumable<status_code>>;
                           })
            return 4;
        else
            return 0;
    }

    template <int Kind, typename T> resumable<status_code> explicit_hook(T &value) {
        // Member and ADL hooks keep the ordinary class metadata tag convention.
        // Mixin hooks implement complete items and manage their own tags.
        if constexpr (IsClassWithTagOverload<std::remove_cvref_t<T>>) {
            auto        tag = get_major_6_tag_from_class(value);
            status_code status;
            if constexpr (is_customization_tag_wrapper_v<std::remove_cvref_t<decltype(tag)>>) {
                status = co_await attempt([this, &tag] {
                    const auto result = ordinary_(std::move(tag));
                    return result ? status_code::success : result.error();
                });
            } else {
                auto normalized = dynamic_tag<std::uint64_t>{static_cast<std::uint64_t>(std::move(tag))};
                status          = co_await ordinary(normalized);
            }
            if (status != status_code::success)
                co_return status;
        }

        if constexpr (Kind == 1)
            co_return co_await Access::resumable_encode(*this, value);
        else if constexpr (Kind == 2)
            co_return co_await Access::resumable_transcode(*this, value);
        else if constexpr (Kind == 3)
            co_return co_await resumable_encode(*this, value);
        else
            co_return co_await resumable_transcode(*this, value);
    }

    template <typename T> resumable<status_code> item(T &value) {
        using U             = std::remove_cvref_t<T>;
        constexpr auto hook = explicit_hook_kind<T>();
        if constexpr (hook != 0)
            return explicit_hook<hook>(value);
        else if constexpr (has_mixin<T>())
            return mixin(value);
        else if constexpr (codec_payload_has_customization<ordinary_type, U, payload_decode_context::direct>())
            // Explicit ordinary customizations precede every structural category.
            return ordinary(value); // NOLINT(bugprone-branch-clone)
        else if constexpr (IsAnyBoundedSizeWrapper<U>) {
            if constexpr (IsBoundedSizeWrapper<U>)
                return bounded(value.value(), U::min_size, U::max_size);
            else
                return bounded(value.value(), value.min_size(), value.max_size());
        } else if constexpr (IsIndefiniteWrapper<U>)
            return indefinite(value.value_);
        else if constexpr (resumable_encode_wrap_array<U>)
            return group(tuple_references(value.values_), true);
        else if constexpr (is_static_tag_t<U>::value || is_dynamic_tag_t<U> || IsAnyHeader<U>)
            return ordinary(value);
        else if constexpr (IsArrayRangeWrapper<U>)
            return container<false>(value.range_);
        else if constexpr (IsMapRangeWrapper<U>)
            return container<true>(value.range_);
        else if constexpr (IsStringRangeWrapper<U>)
            return string_range(value.range_, value.chunk_size_, IsBstrRangeWrapper<U> ? std::byte{0x40} : std::byte{0x60});
        else if constexpr (IsEncodedItemView<U>)
            return raw_view(value);
        else if constexpr (IsString<U>)
            return string(value);
        else if constexpr (IsMap<U>)
            return container<true>(value);
        else if constexpr (IsArray<U>)
            return container<false>(value);
        else if constexpr (IsOptional<U>)
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

    template <typename Tuple> static auto tuple_references(Tuple &value) {
        return std::apply([](auto &...field) { return std::tie(field...); }, value);
    }

    template <typename Tuple> resumable<status_code> group(Tuple values, bool force = false) {
        constexpr auto count = std::tuple_size_v<Tuple>;
        if (force || (count > 1 && Options::wrap_groups)) {
            const auto status = co_await header(count, std::byte{0x80});
            if (status != status_code::success)
                co_return status;
        }
        co_return co_await arguments_at(values);
    }

    template <typename T> resumable<status_code> tuple(T &value) {
        if constexpr (IsTaggedTuple<std::remove_cvref_t<T>>) {
            const auto status = co_await ordinary(std::get<0>(value));
            if (status != status_code::success)
                co_return status;
            co_return co_await group(tuple_tail(value));
        } else
            co_return co_await group(tuple_references(value));
    }

    template <typename T> resumable<status_code> aggregate(T &value) {
        using U     = std::remove_cvref_t<T>;
        auto values = to_tuple(value);
        if constexpr (HasInlineTag<U>) {
            auto       tag    = static_tag<U::cbor_tag>{};
            const auto status = co_await ordinary(tag);
            if (status != status_code::success)
                co_return status;
        } else if constexpr (IsTag<U>) {
            const auto status = co_await ordinary(std::get<0>(values));
            if (status != status_code::success)
                co_return status;
        }
        if constexpr (!HasInlineTag<U> && IsTag<U>)
            co_return co_await group(tuple_tail(values));
        else
            co_return co_await group(std::move(values));
    }

    template <typename T> resumable<status_code> optional(T &value) {
        if (value.has_value())
            return item(*value);
        return attempt([this] {
            const auto result = ordinary_(nullptr);
            return result ? status_code::success : result.error();
        });
    }

    template <typename T> resumable<status_code> variant(T &value) {
        return variant_visit([this](auto &alternative) { return this->item(alternative); }, value);
    }

    template <typename T> resumable<status_code> bounded(T &value, std::size_t minimum, std::size_t maximum) {
        auto &&range = [&]() -> decltype(auto) {
            if constexpr (IsIndefiniteWrapper<T>)
                return (value.value_);
            else if constexpr (ExplicitRangeWrapperReference<T>)
                return (value.range_);
            else
                return (value);
        }();
        static_assert(std::ranges::sized_range<decltype(range)>, "bounded encoding requires a sized range");
        const auto size = std::ranges::size(range);
        if (std::cmp_greater(size, std::numeric_limits<std::uint64_t>::max()) ||
            bounded_size_status(static_cast<std::uint64_t>(size), minimum, maximum) != status_code::success)
            return status_code::size_limit_exceeded;
        return item(value);
    }

    template <typename T> resumable<status_code> string(T &value) {
        const auto status = co_await header(value.size(), get_major_3_bit_tag<std::remove_cvref_t<T>>());
        if (status != status_code::success)
            co_return status;
        co_return co_await payload(value);
    }

    template <typename T> resumable<status_code> raw_view(T &value) {
        auto bytes = value.bytes();
        co_return co_await payload(bytes);
    }

    template <typename Range> static consteval bool has_builtin_integer_elements() {
        using reference = std::ranges::range_reference_t<Range>;
        using element   = std::remove_reference_t<reference>;
        using value     = std::remove_cv_t<element>;
        if constexpr (!std::integral<value> || !(IsUnsigned<value> || IsSigned<value>) || sizeof(value) > 8 ||
                      !std::is_lvalue_reference_v<reference> || !std::same_as<value, std::ranges::range_value_t<Range>>) {
            return false;
        } else {
            if constexpr (explicit_hook_kind<element>() != 0 || has_mixin<element>()) {
                // A bypassed ordinary overload may have a deduced return type whose
                // body is intentionally invalid for this context. Do not probe it.
                return false;
            } else {
                // Include mutable-only codec overloads, even though the ordinary
                // array loop itself binds every source element as const.
                constexpr bool ordinary_override = (requires(Codecs<ordinary_type> &codec, element &item) { codec.encode(item); } || ...);
                return !ordinary_override && !codec_payload_has_customization<ordinary_type, value, payload_decode_context::direct>();
            }
        }
    }

    template <typename Iterator, typename Sentinel, typename Available = encode_elements_available>
    status_code segmented_integer_elements(Iterator &current, const Sentinel &last, Available available = {}) {
        auto        completed = source_.position();
        status_code status;
        try {
            encode_array_elements(ordinary_, current, last, [&] { completed = source_.position(); }, available);
            status = status_code::success;
        } catch (const encode_status_exception &error) { status = error.status; } catch (...) {
            status = resumable_exception_status();
        }

        // Keep successfully encoded children, and retry only the unfinished
        // element. The shared loop leaves its iterator on that element.
        source_.restore(completed);
        commit();
        return status;
    }

    template <typename Iterator, typename Sentinel>
    status_code integer_window(Iterator &current, const Sentinel &last, std::span<std::byte> window) {
        encoder<std::span<std::byte>, Options> contiguous(window);
        constexpr auto                         maximum_size = 1 + sizeof(std::iter_value_t<Iterator>);
        status_code                            status;
        try {
            // Capacity for the native type's largest representation guarantees
            // that the ordinary primitive finishes without a partial write.
            encode_array_elements(contiguous, current, last, encoded_element_complete{},
                                  [&] { return window.size() - contiguous.appender_.head_ >= maximum_size; });
            status = status_code::success;
        } catch (const encode_status_exception &error) { status = error.status; } catch (...) {
            status = resumable_exception_status();
        }

        source_.advance(contiguous.appender_.head_);
        commit();
        return status;
    }

    template <typename Iterator, typename Sentinel> status_code integer_elements(Iterator &current, const Sentinel &last) {
        constexpr auto maximum_size = 1 + sizeof(std::iter_value_t<Iterator>);
        while (current != last) {
            const auto window = source_.available_output_span();
            if (!window)
                return segmented_integer_elements(current, last);

            status_code status;
            if (window->size() >= maximum_size) {
                status = integer_window(current, last, *window);
            } else {
                // Only the boundary child needs the chunk-aware writer. It can
                // cross already supplied chunks before returning to the span loop.
                status = segmented_integer_elements(current, last, [first = true]() mutable { return std::exchange(first, false); });
            }
            if (status != status_code::success)
                return status;
        }
        return status_code::success;
    }

    template <bool Map, typename Range> resumable<status_code> container(Range &range, bool force_indefinite = false) {
        constexpr auto major      = Map ? std::byte{0xa0} : std::byte{0x80};
        const bool     indefinite = force_indefinite || !std::ranges::sized_range<Range>;
        status_code    status;
        if constexpr (std::ranges::sized_range<Range>) {
            status = co_await (indefinite ? byte(major | std::byte{0x1f}) : header(std::ranges::size(range), major));
        } else
            status = co_await byte(major | std::byte{0x1f});
        if (status != status_code::success)
            co_return status;

        if constexpr (!Map && has_builtin_integer_elements<Range>()) {
            auto       current = std::ranges::begin(range);
            const auto last    = std::ranges::end(range);
            while (current != last) {
                status = integer_elements(current, last);
                if (status != status_code::incomplete || eof()) {
                    if (status != status_code::success)
                        co_return status;
                    break;
                }
                co_await wait();
            }
        } else {
            for (auto &&entry : range) {
                if constexpr (Map)
                    status = co_await (*this)(pair_first(entry), pair_second(entry));
                else if constexpr (CborRangeComponent<decltype(entry)> ||
                                   std::same_as<std::remove_cvref_t<decltype(entry)>, std::ranges::range_value_t<Range>>)
                    status = co_await item(entry);
                else {
                    // Input-range proxies must be materialized before suspension.
                    using value_type  = std::ranges::range_value_t<Range>;
                    auto materialized = static_cast<value_type>(entry);
                    status            = co_await item(materialized);
                }
                if (status != status_code::success)
                    co_return status;
            }
        }
        if (indefinite)
            co_return co_await byte(std::byte{0xff});
        co_return status_code::success;
    }

    template <typename T> resumable<status_code> indefinite(T &value) {
        using U = std::remove_cvref_t<T>;
        if constexpr (IsString<U>) {
            auto status = co_await byte(get_major_3_bit_tag<U>() | std::byte{0x1f});
            if (status != status_code::success)
                co_return status;
            status = co_await item(value);
            if (status != status_code::success)
                co_return status;
            co_return co_await byte(std::byte{0xff});
        } else if constexpr (IsMap<U>)
            co_return co_await container<true>(value, true);
        else if constexpr (IsArray<U>)
            co_return co_await container<false>(value, true);
        else
            co_return status_code::unsupported_operation;
    }

    template <typename Range> resumable<status_code> string_range(Range &range, std::size_t chunk_size, std::byte major) {
        if (chunk_size == 0)
            co_return status_code::error;
        if constexpr (std::ranges::sized_range<Range>) {
            const auto status = co_await header(std::ranges::size(range), major);
            if (status != status_code::success)
                co_return status;
            co_return co_await payload(range);
        } else {
            auto status = co_await byte(major | std::byte{0x1f});
            if (status != status_code::success)
                co_return status;

            // Consume single-pass ranges once, retaining just one declared chunk.
            std::vector<std::byte> chunk;
            chunk.reserve(chunk_size);
            auto       current = std::ranges::begin(range);
            const auto end     = std::ranges::end(range);
            while (current != end) {
                chunk.clear();
                while (current != end && chunk.size() != chunk_size) {
                    chunk.push_back(static_cast<std::byte>(*current));
                    ++current;
                }
                status = co_await header(chunk.size(), major);
                if (status != status_code::success)
                    co_return status;
                status = co_await payload(chunk);
                if (status != status_code::success)
                    co_return status;
            }
            co_return co_await byte(std::byte{0xff});
        }
    }
};

} // namespace cbor::tags::detail
