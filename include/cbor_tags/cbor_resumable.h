#pragma once

#include "cbor_tags/detail/cbor_resumable_generator.h"
#include "cbor_tags/detail/cbor_resumable_operation.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <new>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace cbor::tags {

namespace detail {

template <resumable_direction Direction> [[nodiscard]] std::shared_ptr<resumable_io> make_resumable_io() noexcept {
    try {
        auto io       = std::make_shared<resumable_io>();
        io->direction = Direction;
        return io;
    } catch (const std::bad_alloc &) { return {}; }
}

} // namespace detail

template <typename Options = default_options, template <typename> typename... Codecs> struct resumable_decoder_factory {
    template <typename... T> [[nodiscard]] auto operator()(T &&...values) const {
        auto io = detail::make_resumable_io<detail::resumable_direction::input>();
        if (io) {
            try {
                auto captured = std::tuple{detail::capture_resumable_input(std::forward<T>(values))...};
                io->root      = detail::resumable_decode_root<Options, Codecs...>(*io, std::move(captured));
            } catch (...) { detail::classify_exception(*io); }
        }
        return resumable_operation<detail::resumable_direction::input>{std::move(io)};
    }

    template <typename T> [[nodiscard]] auto items() const {
        static_assert(detail::resumable_complete_item<Options, T, Codecs...>(),
                      "generator element must encode exactly one complete CBOR item");
        auto                              io = detail::make_resumable_io<detail::resumable_direction::input>();
        std::shared_ptr<std::optional<T>> slot;
        if (io) {
            try {
                slot     = std::make_shared<std::optional<T>>();
                io->root = detail::resumable_items_root<T, Options, Codecs...>(*io, slot);
            } catch (...) { detail::classify_exception(*io); }
        }
        return resumable_generator<T, detail::resumable_direction::input>{std::move(io), std::move(slot)};
    }

    template <typename T> [[nodiscard]] auto elements() const {
        static_assert(detail::resumable_complete_item<Options, T, Codecs...>(),
                      "generator element must encode exactly one complete CBOR item");
        auto                              io = detail::make_resumable_io<detail::resumable_direction::input>();
        std::shared_ptr<std::optional<T>> slot;
        if (io) {
            try {
                slot     = std::make_shared<std::optional<T>>();
                io->root = detail::resumable_elements_root<T, Options, Codecs...>(*io, slot);
            } catch (...) { detail::classify_exception(*io); }
        }
        return resumable_generator<T, detail::resumable_direction::input>{std::move(io), std::move(slot)};
    }

  private:
    [[nodiscard]] static auto payload(major_type major) {
        auto                                           io = detail::make_resumable_io<detail::resumable_direction::input>();
        std::shared_ptr<std::optional<readable_chunk>> slot;
        if (io) {
            try {
                slot     = std::make_shared<std::optional<readable_chunk>>();
                io->root = detail::resumable_payload_root(*io, major, slot);
            } catch (...) { detail::classify_exception(*io); }
        }
        return resumable_generator<readable_chunk, detail::resumable_direction::input>{std::move(io), std::move(slot)};
    }

  public:
    [[nodiscard]] auto bstr() const { return payload(major_type::ByteString); }
    [[nodiscard]] auto tstr() const { return payload(major_type::TextString); }
};

template <typename Options = default_options, template <typename> typename... Codecs> struct resumable_encoder_factory {
    template <typename... T> [[nodiscard]] auto operator()(T &&...values) const {
        auto io = detail::make_resumable_io<detail::resumable_direction::output>();
        if (io) {
            try {
                auto captured = std::tuple{detail::capture_resumable_output(std::forward<T>(values))...};
                io->root      = detail::resumable_encode_root<Options, Codecs...>(*io, std::move(captured));
            } catch (...) { detail::classify_exception(*io); }
        }
        return resumable_operation<detail::resumable_direction::output>{std::move(io)};
    }

    template <typename... T> [[nodiscard]] auto chunks(T &&...values) const {
        auto io = detail::make_resumable_io<detail::resumable_direction::output_generator>();
        if (io) {
            try {
                auto captured = std::tuple{detail::capture_resumable_output(std::forward<T>(values))...};
                io->root      = detail::resumable_encode_root<Options, Codecs...>(*io, std::move(captured));
            } catch (...) { detail::classify_exception(*io); }
        }
        return resumable_generator<readable_chunk, detail::resumable_direction::output_generator>{std::move(io), {}};
    }
};

template <template <typename> typename... Codecs> [[nodiscard]] auto make_resumable_decoder() {
    return resumable_decoder_factory<default_options, Codecs...>{};
}
template <typename Options, template <typename> typename... Codecs> [[nodiscard]] auto make_resumable_decoder_with_options() {
    return resumable_decoder_factory<Options, Codecs...>{};
}
template <template <typename> typename... Codecs> [[nodiscard]] auto make_resumable_encoder() {
    return resumable_encoder_factory<default_options, Codecs...>{};
}
template <typename Options, template <typename> typename... Codecs> [[nodiscard]] auto make_resumable_encoder_with_options() {
    return resumable_encoder_factory<Options, Codecs...>{};
}

[[nodiscard]] inline auto make_resumable_validator(std::size_t count = 1U, validation_options options = {}) {
    auto io = detail::make_resumable_io<detail::resumable_direction::input>();
    if (io) {
        try {
            io->root = detail::resumable_validate_root(*io, count, false, options);
        } catch (...) { detail::classify_exception(*io); }
    }
    return resumable_operation<detail::resumable_direction::input>{std::move(io)};
}
[[nodiscard]] inline auto make_resumable_validator(until_eof_t, validation_options options = {}) {
    auto io = detail::make_resumable_io<detail::resumable_direction::input>();
    if (io) {
        try {
            io->root = detail::resumable_validate_root(*io, 0U, true, options);
        } catch (...) { detail::classify_exception(*io); }
    }
    return resumable_operation<detail::resumable_direction::input>{std::move(io)};
}

} // namespace cbor::tags
