#pragma once

#include "cbor_tags/cbor_resumable_source.h"
#include "cbor_tags/detail/cbor_resumable_decoder.h"
#include "cbor_tags/detail/cbor_resumable_encoder.h"

namespace cbor::tags {

namespace detail {
// Decoders name their borrowed view types like the one-shot decoder,
// e.g. decltype(decoder)::tstr_view_t. Encoders have none.
template <typename Context> struct resumable_view_types {};
template <typename Context>
    requires requires { typename Context::tstr_view_t; }
struct resumable_view_types<Context> {
    using bstr_view_t        = typename Context::bstr_view_t;
    using tstr_view_t        = typename Context::tstr_view_t;
    using encoded_item_view  = typename Context::encoded_item_view;
    using encoded_array_view = typename Context::encoded_array_view;
    using encoded_map_view   = typename Context::encoded_map_view;
};

template <typename Context> struct resumable_factory : resumable_view_types<Context> {
    typename Context::source_type              &source;
    template <typename... T> [[nodiscard]] auto operator()(T &&...values) const {
        return resume_operation<Context>{source, std::forward<T>(values)...};
    }
};

// The adapter's cursors refer back to it. Keep it at the factory's address and
// lend it only from a named factory that outlives its operations and views.
template <typename Context> class storage_resumable_factory : public resumable_view_types<Context> {
    typename Context::source_type source_;

  public:
    template <typename Storage> explicit storage_resumable_factory(Storage &storage) : source_(storage) {}
    storage_resumable_factory(const storage_resumable_factory &)            = delete;
    storage_resumable_factory &operator=(const storage_resumable_factory &) = delete;
    storage_resumable_factory(storage_resumable_factory &&)                 = delete;
    storage_resumable_factory &operator=(storage_resumable_factory &&)      = delete;

    template <typename... T> [[nodiscard]] auto operator()(T &&...values) & {
        return resume_operation<Context>{source_, std::forward<T>(values)...};
    }
    template <typename... T> auto operator()(T &&...) && = delete;
};
} // namespace detail

template <template <typename> typename... Codecs, ResumableInputSource Source> auto make_resumable_decoder(Source &source) {
    return detail::resumable_factory<detail::resumable_decode_context<Source, default_options, Codecs...>>{{}, source};
}
template <template <typename> typename... Codecs, typename Storage>
    requires(!ResumableInputSource<Storage>) && requires(Storage &storage) { make_input_source(storage); }
auto make_resumable_decoder(Storage &storage) {
    using source_type = decltype(make_input_source(storage));
    return detail::storage_resumable_factory<detail::resumable_decode_context<source_type, default_options, Codecs...>>{storage};
}
template <IsOptions Options, template <typename> typename... Codecs, ResumableInputSource Source>
auto make_resumable_decoder_with_options(Source &source) {
    return detail::resumable_factory<detail::resumable_decode_context<Source, Options, Codecs...>>{{}, source};
}
template <IsOptions Options, template <typename> typename... Codecs, typename Storage>
    requires(!ResumableInputSource<Storage>) && requires(Storage &storage) { make_input_source(storage); }
auto make_resumable_decoder_with_options(Storage &storage) {
    using source_type = decltype(make_input_source(storage));
    return detail::storage_resumable_factory<detail::resumable_decode_context<source_type, Options, Codecs...>>{storage};
}

template <template <typename> typename... Codecs, ResumableOutputSource Source> auto make_resumable_encoder(Source &source) {
    return detail::resumable_factory<detail::resumable_encode_context<Source, default_options, Codecs...>>{{}, source};
}
template <template <typename> typename... Codecs, typename Storage>
    requires(!ResumableOutputSource<Storage>) && requires(Storage &storage) { make_output_source(storage); }
auto make_resumable_encoder(Storage &storage) {
    using source_type = decltype(make_output_source(storage));
    return detail::storage_resumable_factory<detail::resumable_encode_context<source_type, default_options, Codecs...>>{storage};
}
template <IsOptions Options, template <typename> typename... Codecs, ResumableOutputSource Source>
auto make_resumable_encoder_with_options(Source &source) {
    return detail::resumable_factory<detail::resumable_encode_context<Source, Options, Codecs...>>{{}, source};
}
template <IsOptions Options, template <typename> typename... Codecs, typename Storage>
    requires(!ResumableOutputSource<Storage>) && requires(Storage &storage) { make_output_source(storage); }
auto make_resumable_encoder_with_options(Storage &storage) {
    using source_type = decltype(make_output_source(storage));
    return detail::storage_resumable_factory<detail::resumable_encode_context<source_type, Options, Codecs...>>{storage};
}
} // namespace cbor::tags

#include "cbor_tags/detail/cbor_resumable_validation.h"

namespace cbor::tags {

template <ResumableInputSource Source>
[[nodiscard]] auto make_resumable_validator(Source &source, std::size_t count = 1, validation_options options = {}) {
    return detail::resume_operation<detail::resumable_validation_context<Source>>{
        source, detail::validation_request{.count = count, .until_eof = false, .options = options}};
}
template <ResumableInputSource Source>
[[nodiscard]] auto make_resumable_validator(Source &source, until_closed_t, validation_options options = {}) {
    return detail::resume_operation<detail::resumable_validation_context<Source>>{
        source, detail::validation_request{.count = 0, .until_eof = true, .options = options}};
}

template <typename T, ResumableInputSource Source, typename Options = default_options, template <typename> typename... Codecs>
class resumable_generator {
    using context_type   = detail::resumable_generator_context<Source, T, Options, Codecs...>;
    using operation_type = detail::resume_operation<context_type>;
    Source                       &source_;
    T                             value_{};
    bool                          ended_{};
    std::optional<operation_type> operation_;

  public:
    explicit resumable_generator(Source &source) : source_(source) {}
    resumable_generator(const resumable_generator &)            = delete;
    resumable_generator &operator=(const resumable_generator &) = delete;
    operation_type      &next() {
        if (operation_ && !operation_->done())
            throw std::logic_error("generator next is still pending");
        operation_.reset();
        value_ = T{};
        operation_.emplace(source_, value_, ended_);
        return *operation_;
    }
    resume_state resume() {
        if (!operation_)
            throw std::logic_error("call generator.next() before resuming");
        return operation_->resume();
    }
    resume_state close_input() {
        if (!operation_)
            throw std::logic_error("call generator.next() before closing its input");
        return operation_->close_input();
    }
    T       &value() noexcept { return value_; }
    const T &value() const noexcept { return value_; }
    bool     done() const noexcept { return ended_; }
};

template <typename T, template <typename> typename... Codecs, ResumableInputSource Source> auto make_resumable_generator(Source &source) {
    return resumable_generator<T, Source, default_options, Codecs...>{source};
}
} // namespace cbor::tags
