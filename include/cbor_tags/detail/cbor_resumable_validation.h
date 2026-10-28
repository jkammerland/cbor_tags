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

struct validation_request {
    std::size_t        count{1};
    bool               until_eof{};
    validation_options options{};
};

template <typename Source> class resumable_validation_context : public resumable_decode_context<Source, default_options> {
    using base = resumable_decode_context<Source, default_options>;

  public:
    using base::base;
    resumable<status_code> operator()(validation_request request) {
        for (std::size_t i = 0; request.until_eof || i < request.count; ++i) {
            while (this->empty()) {
                if (this->eof())
                    co_return request.until_eof ? status_code::success : status_code::incomplete;
                co_await this->wait();
            }
            const auto status = co_await read_item(0, request.options);
            if (status != status_code::success)
                co_return status;
        }
        co_return status_code::success;
    }

  private:
    resumable<status_code> read_item(std::size_t depth, validation_options options) {
        resumable_walk_token token;
        const auto           status = co_await this->ordinary(token);
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
        utf8_state utf8;
        bool       valid  = true;
        auto      &reader = this->ordinary().reader_;
        while (length != 0) {
            if (this->empty()) {
                if (this->eof())
                    co_return status_code::incomplete;
                co_await this->wait();
                continue;
            }
            if (const auto span = this->source().available_span(reader.position_); span && !span->empty()) {
                const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(length, span->size()));
                if constexpr (Text) {
                    for (const auto value : span->first(count)) {
                        valid = utf8.consume(utf8_byte(value)) && valid;
                    }
                }
                this->source().advance(reader.position_, count);
                reader.current_offset_ += count;
                length -= count;
            } else {
                const auto value = reader.read(this->source());
                if constexpr (Text) {
                    valid = utf8.consume(utf8_byte(value)) && valid;
                }
                --length;
            }
            this->commit();
        }
        co_return Text && (!valid || !utf8.complete()) ? status_code::invalid_utf8_sequence : status_code::success;
    }
    template <typename Header> resumable<status_code> sequence(Header header, std::size_t depth, validation_options options) {
        auto remaining = header.size;
        while (header.indefinite || remaining != 0) {
            resumable_walk_token child;
            auto                 status = co_await this->ordinary(child);
            if (status != status_code::success)
                co_return status;
            if (std::holds_alternative<indefinite_break>(child.value)) {
                co_return header.indefinite ? status_code::success : status_code::malformed_structure;
            }
            if constexpr (IsTextHeader<Header> || IsBinaryHeader<Header>) {
                const auto *chunk = std::get_if<Header>(&child.value);
                if (!chunk || chunk->indefinite)
                    co_return status_code::malformed_structure;
            }
            status = co_await start_item(child.value, depth + 1, options);
            if (status != status_code::success)
                co_return status;
            if constexpr (IsMapHeader<Header>) {
                status = co_await read_item(depth + 1, options);
                if (status != status_code::success)
                    co_return status;
            }
            if (!header.indefinite)
                --remaining;
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
