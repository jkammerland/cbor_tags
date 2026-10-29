#pragma once

#include "cbor_tags/cbor_traversal.h"
#include "cbor_tags/detail/cbor_resumable_decoder.h"

namespace cbor::tags::detail {

// Reuse the ordinary traversal token decoder, including strict simple encodings.
struct resumable_walk_token {
    catch_all_variant                                       value;
    template <typename Decoder> expected<void, status_code> decode(Decoder &decoder) {
        const auto status = read_walk_token(decoder, value, true);
        if (status != status_code::success)
            return unexpected<status_code>{status};
        return {};
    }
};

// A batch stays inside one sibling sequence. Structural tokens are consumed
// here but handed back to that sequence for depth, payload, and break handling.
template <bool Map = false> struct resumable_scalar_batch {
    resumable_walk_token token{};
    std::uint64_t        remaining{};
    std::size_t          accepted{};
    bool                 indefinite{};
    bool                 value_pending{};
    bool                 deferred{};

    bool finished() const { return !indefinite && remaining == 0; }

    void complete_item() {
        if constexpr (Map) {
            value_pending = !value_pending;
            if (value_pending)
                return;
        }
        if (!indefinite)
            --remaining;
    }

    template <typename Decoder> expected<void, status_code> decode(Decoder &decoder) {
        while (!finished() && !decoder.reader_.empty(decoder.data_, 8)) {
            const auto status = read_walk_token(decoder, token.value, true);
            if (status != status_code::success)
                return unexpected<status_code>{status};

            // A later malformed token must not discard preceding siblings or
            // publish the malformed token's partially advanced local reader.
            accepted = decoder.reader_.position_;
            deferred = std::visit(
                [](const auto &item) {
                    using T = std::remove_cvref_t<decltype(item)>;
                    return IsTextHeader<T> || IsBinaryHeader<T> || IsArrayHeader<T> || IsMapHeader<T> || IsTagHeader<T> ||
                           std::same_as<T, indefinite_break>;
                },
                token.value);
            if (deferred)
                break;
            complete_item();
        }
        return {};
    }
};

struct validation_request {
    std::size_t        count{1};
    bool               until_eof{};
    validation_options options{};
};

template <typename Source> class resumable_validation_context : public resumable_decode_context<Source, default_options> {
    using base           = resumable_decode_context<Source, default_options>;
    using window_decoder = decoder<std::span<const std::byte>, default_options, cbor_header_decoder, cbor_indefinite_decoder>;

    std::span<const std::byte> window_;
    std::size_t                pending_{};

    // Publish complete tokens and consumed payload bytes on every exit,
    // including allocation failure after decoding a nested item's header.
    struct window_guard {
        resumable_validation_context &context;
        ~window_guard() { context.flush_window(); }
    };

  public:
    using base::base;
    resumable<status_code> operator()(validation_request request) {
        const window_guard       guard{*this};
        resumable_scalar_batch<> batch{.remaining = request.count, .indefinite = request.until_eof};
        while (!batch.finished()) {
            while (this->empty()) {
                if (this->eof())
                    co_return request.until_eof ? status_code::success : status_code::incomplete;
                co_await this->wait();
            }

            auto status = read_scalars(batch);
            if (status != status_code::success)
                co_return status;
            if (batch.finished())
                break;
            if (batch.accepted != 0 && !batch.deferred && window_.empty())
                continue;

            status = batch.deferred ? co_await start_item(batch.token.value, 0, request.options) : co_await read_item(0, request.options);
            if (status != status_code::success)
                co_return status;
            batch.complete_item();
        }
        co_return status_code::success;
    }

  private:
    void flush_window() {
        if (pending_ != 0) {
            auto &reader = this->ordinary().reader_;
            this->source().consume_window(reader.position_, pending_);
            reader.current_offset_ += pending_;
            this->commit();
        }
        window_  = {};
        pending_ = 0;
    }

    bool empty() {
        if (!window_.empty())
            return false;

        // An exhausted local window can precede the source's committed cursor.
        flush_window();
        return base::empty();
    }

    void refresh_window() {
        flush_window();
        if (const auto window = this->source().borrow_window(this->ordinary().reader_.position_))
            window_ = *window;
    }

    void advance_window(std::size_t count) {
        pending_ += count;
        window_ = window_.subspan(count);
    }

    template <bool Map> status_code read_scalars(resumable_scalar_batch<Map> &batch) {
        batch.accepted = 0;
        batch.deferred = false;
        if (window_.size() < 9)
            refresh_window();
        if (window_.size() < 9)
            return status_code::success;

        // One ordinary checked call covers all scalar siblings in this window.
        // Publish its accepted checkpoint even if a later token failed.
        window_decoder contiguous(window_);
        const auto     result = contiguous(batch);
        advance_window(batch.accepted);
        return result ? status_code::success : result.error();
    }

    resumable<status_code> read_token(resumable_walk_token &token) {
        if (window_.size() < 9)
            refresh_window();

        // Every traversal token fits in nine bytes. Decode through the normal
        // codec and retain only successful progress; payloads remain separate.
        if (window_.size() >= 9) {
            window_decoder contiguous(window_);
            const auto     result = contiguous(token);
            if (!result)
                return result.error();

            advance_window(contiguous.reader_.position_);
            return status_code::success;
        }

        // Only a boundary token needs the chunk-aware reader and retry path.
        // Flush before it can suspend or traverse already admitted chunks.
        flush_window();
        return this->ordinary(token);
    }

    resumable<status_code> read_item(std::size_t depth, validation_options options) {
        resumable_walk_token token;
        const auto           status = co_await read_token(token);
        if (status != status_code::success)
            co_return status;
        co_return co_await start_item(token.value, depth, options);
    }
    resumable<status_code> start_item(const catch_all_variant &token, std::size_t depth, validation_options options) {
        return std::visit(
            [&](const auto &item) -> resumable<status_code> {
                using T = std::remove_cvref_t<decltype(item)>;
                if constexpr (std::same_as<T, indefinite_break>)
                    return status_code::malformed_structure;
                else if constexpr (IsTextHeader<T> || IsBinaryHeader<T> || IsArrayHeader<T> || IsMapHeader<T> || IsTagHeader<T>) {
                    if (depth >= options.max_depth)
                        return status_code::size_limit_exceeded;
                    if constexpr (IsTagHeader<T>)
                        return read_item(depth + 1, options);
                    else if constexpr (IsTextHeader<T> || IsBinaryHeader<T>) {
                        if (!item.indefinite)
                            return string_payload<IsTextHeader<T>>(item.size);
                        return sequence(item, depth, options);
                    } else
                        return sequence(item, depth, options);
                } else
                    return status_code::success;
            },
            token);
    }
    template <bool Text> resumable<status_code> string_payload(std::uint64_t length) {
        if (length == 0)
            return status_code::success;
        if (window_.empty())
            refresh_window();

        // An already available payload needs no suspended state. Retain the
        // same window for its following siblings, including after invalid UTF-8.
        if (length <= window_.size()) {
            const auto count = static_cast<std::size_t>(length);
            bool       valid = true;
            if constexpr (Text) {
                utf8_state utf8;
                valid = utf8.consume_span(window_.first(count)) && utf8.complete();
            }
            advance_window(count);
            return valid ? status_code::success : status_code::invalid_utf8_sequence;
        }
        return await_string_payload<Text>(length);
    }
    template <bool Text> resumable<status_code> await_string_payload(std::uint64_t length) {
        utf8_state utf8;
        bool       valid  = true;
        auto      &reader = this->ordinary().reader_;
        while (length != 0) {
            // A populated window already proves availability; consult the
            // source end only when no contiguous bytes can be borrowed.
            if (window_.empty()) {
                refresh_window();
                if (window_.empty() && base::empty()) {
                    if (this->eof())
                        co_return status_code::incomplete;
                    co_await this->wait();
                    continue;
                }
            }

            // Keep headers and payloads in the same window, including adjacent
            // indefinite-string chunks. Only input boundaries publish progress.
            if (!window_.empty()) {
                const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(length, window_.size()));
                if constexpr (Text) {
                    valid = utf8.consume_span(window_.first(count)) && valid;
                }
                advance_window(count);
                length -= count;
            } else {
                const auto value = reader.read(this->source());
                if constexpr (Text) {
                    valid = utf8.consume(utf8_byte(value)) && valid;
                }
                --length;
                this->commit();
            }
        }
        co_return Text && (!valid || !utf8.complete()) ? status_code::invalid_utf8_sequence : status_code::success;
    }
    template <typename Header> resumable<status_code> sequence(Header header, std::size_t depth, validation_options options) {
        resumable_scalar_batch<IsMapHeader<Header>> batch{.remaining = header.size, .indefinite = header.indefinite};
        while (!batch.finished()) {
            auto status = status_code::success;
            if constexpr (IsArrayHeader<Header> || IsMapHeader<Header>) {
                status = read_scalars(batch);
                if (status != status_code::success)
                    co_return status;
                if (batch.finished())
                    break;
            }

            if (!batch.deferred)
                status = co_await read_token(batch.token);
            if (status != status_code::success)
                co_return status;
            if (std::holds_alternative<indefinite_break>(batch.token.value)) {
                co_return header.indefinite && !batch.value_pending ? status_code::success : status_code::malformed_structure;
            }
            if constexpr (IsTextHeader<Header> || IsBinaryHeader<Header>) {
                const auto *chunk = std::get_if<Header>(&batch.token.value);
                if (!chunk || chunk->indefinite)
                    co_return status_code::malformed_structure;
            }
            status = co_await start_item(batch.token.value, depth + 1, options);
            if (status != status_code::success)
                co_return status;
            batch.complete_item();
        }
        co_return status_code::success;
    }
};

// A sequence consumer requests one value at a time. No value is prefetched.
template <typename Source, typename T, typename Options, template <typename> typename... Codecs>
class resumable_generator_context : public resumable_decode_context<Source, Options, Codecs...> {
    using base = resumable_decode_context<Source, Options, Codecs...>;

  public:
    using base::base;
    resumable<status_code> operator()(T &value, bool &ended) {
        while (this->empty()) {
            if (this->eof()) {
                ended = true;
                co_return status_code::success;
            }
            co_await this->wait();
        }
        co_return co_await base::operator()(value);
    }
};
} // namespace cbor::tags::detail
