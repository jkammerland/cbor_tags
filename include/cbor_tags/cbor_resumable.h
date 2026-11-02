#pragma once

#include "cbor_tags/detail/cbor_resumable_decoder.h"
#include "cbor_tags/detail/cbor_resumable_encoder.h"

namespace cbor::tags {

namespace detail {
template <typename Context> struct resumable_factory {
    typename Context::source_type &source;
    template <typename... T> auto  operator()(T &&...values) const { return resume_operation<Context>{source, std::forward<T>(values)...}; }
};
} // namespace detail

template <template <typename> typename... Codecs, typename Source> auto make_resumable_decoder(Source &source) {
    return detail::resumable_factory<detail::resumable_decode_context<Source, default_options, Codecs...>>{source};
}
template <IsOptions Options, template <typename> typename... Codecs, typename Source>
auto make_resumable_decoder_with_options(Source &source) {
    return detail::resumable_factory<detail::resumable_decode_context<Source, Options, Codecs...>>{source};
}

template <template <typename> typename... Codecs, typename Source> auto make_resumable_encoder(Source &source) {
    return detail::resumable_factory<detail::resumable_encode_context<Source, default_options, Codecs...>>{source};
}
template <IsOptions Options, template <typename> typename... Codecs, typename Source>
auto make_resumable_encoder_with_options(Source &source) {
    return detail::resumable_factory<detail::resumable_encode_context<Source, Options, Codecs...>>{source};
}
} // namespace cbor::tags

#include "cbor_tags/detail/cbor_resumable_validation.h"

namespace cbor::tags {

template <typename Source> auto make_resumable_validator(Source &source, std::size_t count = 1, validation_options options = {}) {
    return detail::resume_operation<detail::resumable_validation_context<Source>>{
        source, detail::validation_request{.count = count, .until_eof = false, .options = options}};
}
template <typename Source> auto make_resumable_validator(Source &source, until_end_of_input_t, validation_options options = {}) {
    return detail::resume_operation<detail::resumable_validation_context<Source>>{
        source, detail::validation_request{.count = 0, .until_eof = true, .options = options}};
}

template <typename T, typename Source, typename Options = default_options, template <typename> typename... Codecs>
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
    template <typename... Chunks> resume_state operator()(Chunks &&...chunks) {
        if (!operation_)
            throw std::logic_error("call generator.next() before supplying input");
        return (*operation_)(std::forward<Chunks>(chunks)...);
    }
    T       &value() noexcept { return value_; }
    const T &value() const noexcept { return value_; }
    bool     done() const noexcept { return ended_; }
};

template <typename T, template <typename> typename... Codecs, typename Source> auto make_resumable_generator(Source &source) {
    return resumable_generator<T, Source, default_options, Codecs...>{source};
}
} // namespace cbor::tags
