#pragma once

#include "cbor_concepts.h"
#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_concepts_checking.h"
#include "cbor_tags/cbor_detail.h"
#include "cbor_tags/cbor_integer.h"
#include "cbor_tags/cbor_reflection.h"
#include "cbor_tags/cbor_simple.h"
#include "cbor_tags/float16_ieee754.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
// #include <fmt/base.h>
// #include <fmt/ranges.h>
#include <exception>
// #include <fmt/base.h>
#include <iterator>
// #include <magic_enum/magic_enum.hpp>
// #include <nameof.hpp>
#include "cbor_tags/cbor_tags_config.h"

#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace cbor::tags {

template <typename T> struct cbor_header_decoder;
template <typename T> struct cbor_integer_decoder;
template <typename T> struct cbor_tag_decoder;
template <typename T> struct cbor_string_decoder;
template <typename T> struct cbor_range_decoder;
template <typename T> struct cbor_probe_decoder;
template <typename T> struct cbor_aggregate_decoder;
template <typename T> struct cbor_tuple_decoder;
template <typename T> struct cbor_class_decoder;
template <typename T> struct cbor_simple_decoder;
template <typename T> struct cbor_optional_decoder;
template <typename T> struct cbor_variant_decoder;

template <typename InputBuffer, IsOptions Options, template <typename> typename... Decoders>
    requires ValidCborBuffer<InputBuffer>
struct decoder;

namespace detail {
template <typename T> struct decoder_traits;

template <typename InputBuffer, IsOptions Options, template <typename> typename... Decoders>
struct decoder_traits<decoder<InputBuffer, Options, Decoders...>> {
    using byte              = std::byte;
    using input_buffer_type = InputBuffer;
    using size_type         = typename InputBuffer::size_type;
    using buffer_byte_t     = typename InputBuffer::value_type;
    using options           = Options;
};
} // namespace detail

template <typename InputBuffer, IsOptions Options, template <typename> typename... Decoders>
    requires ValidCborBuffer<InputBuffer>
struct decoder : public Decoders<decoder<InputBuffer, Options, Decoders...>>... {
    using self_t = decoder<InputBuffer, Options, Decoders...>;
    using Decoders<self_t>::decode...;

    using size_type         = typename InputBuffer::size_type;
    using buffer_byte_t     = typename InputBuffer::value_type;
    using byte              = std::byte;
    using input_buffer_type = InputBuffer;

    using iterator_t  = std::ranges::iterator_t<const InputBuffer>;
    using subrange    = std::ranges::subrange<iterator_t>;
    using bstr_view_t = bstr_view<subrange>;
    using tstr_view_t = tstr_view<subrange>;

    using expected_type   = typename Options::return_type;
    using unexpected_type = typename Options::error_type;
    using options         = Options;

    explicit decoder(const InputBuffer &data) : data_(data), reader_(data) {}

    template <typename... TArgs> expected_type operator()(TArgs &&...args) noexcept {
        try {
            status_collector<self_t> collect_status{*this};

            auto success = (collect_status(std::forward<TArgs>(args)) && ...);

            if (!success) {
                return unexpected<decltype(collect_status.result)>(collect_status.result);
            }
            return expected_type{};
        } catch (const std::bad_alloc &) {
            return unexpected<status_code>(status_code::out_of_memory);
        } catch ([[maybe_unused]] const std::exception &e) {
            debug::println("Caught exception: {}", e.what());
            return unexpected<status_code>(status_code::error);
        }
    }

    constexpr uint64_t decode_unsigned(byte additionalInfo) {
        if (additionalInfo < static_cast<byte>(24)) {
            return static_cast<uint64_t>(additionalInfo);
        }
        return read_unsigned(additionalInfo);
    }

    constexpr int64_t decode_integer(byte additionalInfo) {
        uint64_t value = decode_unsigned(additionalInfo);
        return -1 - static_cast<int64_t>(value);
    }

    constexpr auto decode_bstring(byte additionalInfo) {
        const auto length      = decode_unsigned(additionalInfo);
        const auto span_length = require_bytes(length);

        if constexpr (IsContiguous<InputBuffer>) {
            auto *begin  = std::ranges::data(data_) + reader_.position_;
            auto  result = std::span<const byte>(reinterpret_cast<const byte *>(begin), span_length);
            reader_.position_ += span_length;
            return result;
        } else {
            auto start = reader_.position_;
            auto it    = start;
            for (size_type i = 0; i < span_length; ++i) {
                ++it;
            }
            auto result       = subrange(start, it);
            reader_.position_ = it;
            return bstr_view_t{.range = result};
        }
    }

    constexpr auto decode_text(byte additionalInfo) {
        const auto length      = decode_unsigned(additionalInfo);
        const auto span_length = require_bytes(length);

        if constexpr (IsContiguous<InputBuffer>) {
            auto *begin  = std::ranges::data(data_) + reader_.position_;
            auto  result = std::string_view(reinterpret_cast<const char *>(begin), span_length);
            reader_.position_ += span_length;
            return result;
        } else {
            auto start = reader_.position_;
            auto it    = start;
            for (size_type i = 0; i < span_length; ++i) {
                ++it;
            }
            auto result       = subrange(start, it);
            reader_.position_ = it;
            return tstr_view_t{.range = result};
        }
    }

    constexpr size_type require_bytes(std::uint64_t length) {
        if (length > static_cast<std::uint64_t>(std::numeric_limits<size_type>::max())) {
            throw std::runtime_error("CBOR item exceeds buffer limits");
        }
        const auto needed = static_cast<size_type>(length);
        if (needed == 0) {
            return 0;
        }
        if (reader_.empty(data_, needed - 1)) {
            throw std::runtime_error("Unexpected end of input");
        }
        return needed;
    }

    constexpr uint64_t read_unsigned(byte additionalInfo) {
        switch (static_cast<uint8_t>(additionalInfo)) {
        case 24: return read_uint8();
        case 25: return read_uint16();
        case 26: return read_uint32();
        case 27: return read_uint64();
        default: throw std::runtime_error("Invalid additional info for integer");
        }
    }

    constexpr uint8_t read_uint8() {
        if (reader_.empty(data_)) {
            throw std::runtime_error("Unexpected end of input");
        }
        return static_cast<uint8_t>(reader_.read(data_));
    }

    constexpr uint16_t read_uint16() {
        if (reader_.empty(data_, 1)) {
            throw std::runtime_error("Unexpected end of input");
        }
        auto byte0 = static_cast<uint16_t>(reader_.read(data_));
        auto byte1 = static_cast<uint16_t>(reader_.read(data_));
        return static_cast<uint16_t>(byte0 << 8 | byte1);
    }

    constexpr uint32_t read_uint32() {
        if (reader_.empty(data_, 3)) {
            throw std::runtime_error("Unexpected end of input");
        }
        auto byte0 = static_cast<uint32_t>(reader_.read(data_));
        auto byte1 = static_cast<uint32_t>(reader_.read(data_));
        auto byte2 = static_cast<uint32_t>(reader_.read(data_));
        auto byte3 = static_cast<uint32_t>(reader_.read(data_));
        return (byte0 << 24) | (byte1 << 16) | (byte2 << 8) | byte3;
    }

    constexpr uint64_t read_uint64() {
        if (reader_.empty(data_, 7)) {
            throw std::runtime_error("Unexpected end of input");
        }
        auto byte0 = static_cast<uint64_t>(reader_.read(data_));
        auto byte1 = static_cast<uint64_t>(reader_.read(data_));
        auto byte2 = static_cast<uint64_t>(reader_.read(data_));
        auto byte3 = static_cast<uint64_t>(reader_.read(data_));
        auto byte4 = static_cast<uint64_t>(reader_.read(data_));
        auto byte5 = static_cast<uint64_t>(reader_.read(data_));
        auto byte6 = static_cast<uint64_t>(reader_.read(data_));
        auto byte7 = static_cast<uint64_t>(reader_.read(data_));
        return (byte0 << 56) | (byte1 << 48) | (byte2 << 40) | (byte3 << 32) | (byte4 << 24) | (byte5 << 16) | (byte6 << 8) | byte7;
    }

    constexpr simple read_simple() {
        if (reader_.empty(data_)) {
            throw std::runtime_error("Unexpected end of input");
        }
        return simple{static_cast<simple::value_type>(reader_.read(data_))};
    }

    constexpr float16_t read_float16() {
        if (reader_.empty(data_, 1)) {
            throw std::runtime_error("Unexpected end of input");
        }
        auto byte0 = static_cast<uint16_t>(reader_.read(data_));
        auto byte1 = static_cast<uint16_t>(reader_.read(data_));
        return float16_t{static_cast<uint16_t>((byte0 << 8) | byte1)};
    }

    constexpr float read_float() {
        uint32_t bits = read_uint32();
        return std::bit_cast<float>(bits);
    }

    constexpr double read_double() {
        uint64_t bits = read_uint64();
        return std::bit_cast<double>(bits);
    }

    constexpr auto read_initial_byte() {
        if (reader_.empty(data_)) {
            throw std::runtime_error("Unexpected end of input");
        }

        const auto initialByte    = reader_.read(data_);
        const auto majorType      = static_cast<major_type>(static_cast<byte>(initialByte) >> 5);
        const auto additionalInfo = initialByte & static_cast<byte>(0x1F);

        return std::make_pair(majorType, additionalInfo);
    }

    template <typename Dec> struct status_collector {
        Dec                         &dec_;
        [[maybe_unused]] size_t      index{0};
        [[maybe_unused]] status_code result{status_code::success};

        template <typename U> constexpr bool operator()(U &&arg) {
            if constexpr (std::is_same_v<void, decltype(dec_.decode(arg))>) {
                dec_.decode(arg);
            } else {
                result = dec_.decode(arg);
                index++;
                return result == status_code::success;
            }
            return false;
        }
    };

    template <typename Tuple> constexpr auto decode_wrapped_group(Tuple &&tuple) {
        using tuple_type      = std::decay_t<Tuple>;
        constexpr auto size_  = std::tuple_size_v<tuple_type>;
        auto           status = status_code::success;
        if constexpr (size_ > 1 && Options::wrap_groups) {
            status = this->decode(as_array{size_});
        }
        return status;
    }

    template <typename... Args> constexpr auto applier(Args &&...args) {
        status_collector<self_t> collect_status{*this};
        [[maybe_unused]] auto    success = (collect_status(std::forward<Args>(args)) && ...);
        return collect_status.result;
    }

    constexpr auto tell() const noexcept {
        if constexpr (IsContiguous<InputBuffer>) {
            return data_.begin() + reader_.position_;
        } else {
            return reader_.position_;
        }
    }

    template <typename Value> constexpr status_code decode(Value &value) {
        if (reader_.empty(data_)) {
            return status_code::incomplete;
        }
        const auto [majorType, additionalInfo] = read_initial_byte();

        return decode(value, majorType, additionalInfo);
    }

    const InputBuffer          &data_;
    detail::reader<InputBuffer> reader_;
};

template <typename T> struct cbor_integer_decoder {
    using traits = detail::decoder_traits<T>;
    using byte   = typename traits::byte;

    template <IsSigned U> constexpr status_code decode(U &value, major_type major, byte additionalInfo) {
        auto &self = detail::underlying<T>(this);
        if (major == major_type::UnsignedInteger) {
            value = static_cast<U>(self.decode_unsigned(additionalInfo));
        } else if (major == major_type::NegativeInteger) {
            value = static_cast<U>(self.decode_integer(additionalInfo));
        } else {
            return status_code::no_match_for_int_on_buffer;
        }

        return status_code::success;
    }

    template <IsUnsigned U> constexpr status_code decode(U &value, major_type major, byte additionalInfo) {
        auto &self = detail::underlying<T>(this);
        if (major != major_type::UnsignedInteger) {
            return status_code::no_match_for_uint_on_buffer;
        }
        value = static_cast<U>(self.decode_unsigned(additionalInfo));

        return status_code::success;
    }

    constexpr status_code decode(negative &value, major_type major, byte additionalInfo) {
        auto &self = detail::underlying<T>(this);
        if (major != major_type::NegativeInteger) {
            return status_code::no_match_for_nint_on_buffer;
        }
        value = negative(self.decode_unsigned(additionalInfo) + 1);

        return status_code::success;
    }

    template <IsEnum U> constexpr status_code decode(U &value, major_type major, byte additionalInfo) {
        using underlying_type = std::underlying_type_t<U>;
        if constexpr (IsSigned<underlying_type>) {
            if (major > major_type::NegativeInteger) {

                return status_code::no_match_for_enum_on_buffer;
            }
        } else if constexpr (IsUnsigned<underlying_type>) {
            if (major != major_type::UnsignedInteger) {
                return status_code::no_match_for_enum_on_buffer;
            }
        } else {

            return status_code::no_match_for_enum_on_buffer;
        }

        underlying_type result;
        auto            status = detail::underlying<T>(this).decode(result, major, additionalInfo);
        value                  = static_cast<U>(result);
        return status;
    }

    template <IsEnum U> constexpr status_code decode(U &value) {
        auto &self                   = detail::underlying<T>(this);
        auto [major, additionalInfo] = self.read_initial_byte();
        return self.decode(value, major, additionalInfo);
    }
};

template <typename T> struct cbor_tag_decoder {
    using traits = detail::decoder_traits<T>;
    using byte   = typename traits::byte;

    template <std::uint64_t N> constexpr status_code decode(static_tag<N>, major_type major, byte additionalInfo) {
        auto &self = detail::underlying<T>(this);
        if (major != major_type::Tag) {
            return status_code::no_match_for_tag_on_buffer;
        }
        if (self.decode_unsigned(additionalInfo) != N) {
            return status_code::no_match_for_tag;
        }
        return status_code::success;
    }

    template <std::uint64_t N> constexpr status_code decode(static_tag<N>, std::uint64_t tag) {
        if (tag != N) {
            return status_code::no_match_for_tag;
        }
        return status_code::success;
    }

    template <std::uint64_t N> constexpr status_code decode(static_tag<N> value) {
        auto &self                   = detail::underlying<T>(this);
        auto [major, additionalInfo] = self.read_initial_byte();
        return self.decode(value, major, additionalInfo);
    }

    template <IsUnsigned U> constexpr status_code decode(dynamic_tag<U> &value, major_type major, byte additionalInfo) {
        auto &self = detail::underlying<T>(this);
        if (major != major_type::Tag) {
            return status_code::no_match_for_tag_on_buffer;
        }

        auto decoded_value = dynamic_tag<U>{self.decode_unsigned(additionalInfo)};
        if (decoded_value.cbor_tag != value.cbor_tag) {
            return status_code::no_match_for_tag;
        }

        return status_code::success;
    }

    template <IsUnsigned U> constexpr status_code decode(dynamic_tag<U> &value) {
        auto &self                   = detail::underlying<T>(this);
        auto [major, additionalInfo] = self.read_initial_byte();
        return self.decode(value, major, additionalInfo);
    }

    template <IsTaggedTuple Tuple> constexpr status_code decode(Tuple &t, major_type major, byte additionalInfo) {
        auto &self = detail::underlying<T>(this);
        if (major != major_type::Tag) {
            return status_code::no_match_for_tag_on_buffer;
        }

        auto tag = self.decode_unsigned(additionalInfo);
        if (tag != std::get<0>(t)) {
            return status_code::no_match_for_tag;
        }

        auto array_header_status = self.decode_wrapped_group(detail::tuple_tail(t));
        if (array_header_status != status_code::success) {
            return array_header_status;
        }
        return std::apply([&self](auto &&...args) { return self.applier(std::forward<decltype(args)>(args)...); }, detail::tuple_tail(t));
    }

    template <IsTaggedTuple Tuple> constexpr status_code decode(Tuple &t, std::uint64_t tag) {
        auto &self = detail::underlying<T>(this);
        if (tag != std::get<0>(t)) {
            return status_code::no_match_for_tag;
        }

        auto array_header_status = self.decode_wrapped_group(detail::tuple_tail(t));
        if (array_header_status != status_code::success) {
            return array_header_status;
        }
        return std::apply([&self](auto &&...args) { return self.applier(std::forward<decltype(args)>(args)...); }, detail::tuple_tail(t));
    }
};

template <typename T> struct cbor_string_decoder {
    using traits            = detail::decoder_traits<T>;
    using byte              = typename traits::byte;
    using input_buffer_type = typename traits::input_buffer_type;

    template <IsBinaryString TString> constexpr status_code decode(TString &value, major_type major, byte additionalInfo) {
        auto &self = detail::underlying<T>(this);
        static_assert(!IsView<TString> || IsConstView<TString>, "if T is a view, it must be const, e.g std::span<const std::byte>");

        if (major != major_type::ByteString) {
            return status_code::no_match_for_bstr_on_buffer;
        }

        auto       bstring      = self.decode_bstring(additionalInfo);
        const auto bstring_size = static_cast<std::size_t>(std::ranges::distance(bstring.begin(), bstring.end()));

        if constexpr (std::is_same_v<TString, decltype(bstring)>) {
            value = std::move(bstring);
        } else if constexpr (IsConstView<TString> && !IsContiguous<decltype(bstring)>) {
            return status_code::contiguous_view_on_non_contiguous_data;
        } else if constexpr (IsFixedArray<TString>) {
            if (bstring_size != value.size()) {
                debug::println("Fixed array size mismatch: {} != {}", bstring_size, value.size());
                return status_code::unexpected_group_size;
            }
            std::ranges::copy(bstring, value.begin());
        } else {
            value = TString(bstring.begin(), bstring.end());
        }

        return status_code::success;
    }

    template <IsTextString TString> constexpr status_code decode(TString &value, major_type major, byte additionalInfo) {
        auto &self = detail::underlying<T>(this);
        static_assert(!IsView<TString> || IsConstView<TString>, "if T is a view, it must be const, e.g tstr_view<std::deque<char>>");

        if constexpr (IsConstView<TString> && (!IsContiguous<input_buffer_type> && IsContiguous<TString>)) {
            return status_code::contiguous_view_on_non_contiguous_data;
        }

        if (major != major_type::TextString) {
            return status_code::no_match_for_tstr_on_buffer;
        }

        value = self.decode_text(additionalInfo);

        return status_code::success;
    }

    constexpr status_code decode(std::string &value, major_type major, byte additionalInfo) {
        auto &self = detail::underlying<T>(this);
        if (major != major_type::TextString) {
            return status_code::no_match_for_tstr_on_buffer;
        }
        auto text = self.decode_text(additionalInfo);
        value     = std::string(text);
        return status_code::success;
    }

    constexpr status_code decode(std::basic_string_view<std::byte> &value, major_type major, byte additionalInfo) {
        auto &self = detail::underlying<T>(this);
        if constexpr (!IsContiguous<input_buffer_type>) {
            return status_code::contiguous_view_on_non_contiguous_data;
        } else {
            if (major != major_type::ByteString) {
                return status_code::no_match_for_bstr_on_buffer;
            }
            auto length = self.decode_unsigned(additionalInfo);
            if (self.reader_.empty(self.data_, length - 1)) {
                return status_code::incomplete;
            }
            value = std::basic_string_view<std::byte>(reinterpret_cast<const std::byte *>(&self.data_[self.reader_.position_]),
                                                      static_cast<std::size_t>(length));
            return status_code::success;
        }
    }

    constexpr status_code decode(std::string_view &value, major_type major, byte additionalInfo) {
        auto &self = detail::underlying<T>(this);
        if constexpr (!IsContiguous<input_buffer_type>) {
            return status_code::contiguous_view_on_non_contiguous_data;
        } else {
            if (major != major_type::TextString) {
                return status_code::no_match_for_tstr_on_buffer;
            }
            value = self.decode_text(additionalInfo);
            return status_code::success;
        }
    }
};

template <typename T> struct cbor_range_decoder {
    using traits            = detail::decoder_traits<T>;
    using byte              = typename traits::byte;
    using input_buffer_type = typename traits::input_buffer_type;

    template <IsRangeOfCborValues Range> constexpr status_code decode(Range &value, major_type major, byte additionalInfo) {
        auto &self = detail::underlying<T>(this);
        if constexpr (IsMap<Range>) {
            if (major != major_type::Map) {
                return status_code::no_match_for_map_on_buffer;
            }
        } else {
            if (major != major_type::Array) {
                return status_code::no_match_for_array_on_buffer;
            }
        }

        const auto length = self.decode_unsigned(additionalInfo);
        if constexpr (HasReserve<Range>) {
            value.reserve(length);
        }
        detail::appender<Range> appender_;
        for (auto i = length; i > 0; --i) {
            if constexpr (IsMap<Range>) {
                using value_type = std::pair<typename Range::key_type, typename Range::mapped_type>;
                value_type result;
                auto &[key, mapped_value] = result;
                auto status               = self.decode(key);
                status                    = status == status_code::success ? self.decode(mapped_value) : status;
                if (status != status_code::success) {
                    return status;
                }
                appender_(value, result);
            } else {
                using value_type = typename Range::value_type;
                value_type result;
                auto       status = self.decode(result);
                if (status != status_code::success) {
                    return status;
                }
                appender_(value, result);
            }
        }

        return status_code::success;
    }
};

template <typename T> struct cbor_probe_decoder {
    using traits            = detail::decoder_traits<T>;
    using byte              = typename traits::byte;
    using input_buffer_type = typename traits::input_buffer_type;

    constexpr status_code decode(as_text_any &value, major_type major, byte additionalInfo) {
        auto &self = detail::underlying<T>(this);
        if (major != major_type::TextString) {
            return status_code::no_match_for_tstr_on_buffer;
        }
        value.size = self.decode_unsigned(additionalInfo);

        if constexpr (IsContiguous<input_buffer_type>) {
            self.reader_.position_ += value.size;
        } else {
            self.reader_.position_ = std::next(self.reader_.position_, value.size);
        }

        return status_code::success;
    }
    constexpr status_code decode(as_bstr_any &value, major_type major, byte additionalInfo) {
        auto &self = detail::underlying<T>(this);
        if (major != major_type::ByteString) {
            return status_code::no_match_for_bstr_on_buffer;
        }
        value.size = self.decode_unsigned(additionalInfo);

        if constexpr (IsContiguous<input_buffer_type>) {
            self.reader_.position_ += value.size;
        } else {
            self.reader_.position_ = std::next(self.reader_.position_, value.size);
        }

        return status_code::success;
    }
    constexpr status_code decode(as_array_any &value, major_type major, byte additionalInfo) {
        auto &self = detail::underlying<T>(this);
        if (major != major_type::Array) {
            return status_code::no_match_for_array_on_buffer;
        }
        value.size = self.decode_unsigned(additionalInfo);
        return status_code::success;
    }
    constexpr status_code decode(as_map_any &value, major_type major, byte additionalInfo) {
        auto &self = detail::underlying<T>(this);
        if (major != major_type::Map) {
            return status_code::no_match_for_map_on_buffer;
        }
        value.size = self.decode_unsigned(additionalInfo);
        return status_code::success;
    }
    constexpr status_code decode(as_tag_any &value, major_type major, byte additionalInfo) {
        auto &self = detail::underlying<T>(this);
        if (major != major_type::Tag) {
            return status_code::no_match_for_tag_on_buffer;
        }
        value.tag = self.decode_unsigned(additionalInfo);
        return status_code::success;
    }
    constexpr status_code decode(as_tag_any &value, std::uint64_t tag) {
        value.tag = tag;
        return status_code::success;
    }
};

template <typename T> struct cbor_aggregate_decoder {
    using traits = detail::decoder_traits<T>;
    using byte   = typename traits::byte;

    template <typename Value>
        requires(IsAggregate<Value> && !IsClassWithDecodingOverload<T, Value>)
    constexpr status_code decode(Value &value) {
        auto  &self  = detail::underlying<T>(this);
        auto &&tuple = to_tuple(value);

        auto result = status_code::success;
        if constexpr (HasInlineTag<Value>) {
            result = self.decode(static_tag<Value::cbor_tag>{});
        } else if constexpr (IsTag<Value>) {
            result = self.decode(std::get<0>(tuple));
        }
        if (result != status_code::success) {
            return result;
        }

        auto group_status = status_code::success;
        if constexpr (HasInlineTag<Value> || !IsTag<Value>) {
            group_status = self.decode_wrapped_group(tuple);
        } else {
            group_status = self.decode_wrapped_group(detail::tuple_tail(tuple));
        }
        if (group_status != status_code::success) {
            return group_status;
        }

        if constexpr (HasInlineTag<Value> || !IsTag<Value>) {
            return std::apply([&self](auto &&...args) { return self.applier(std::forward<decltype(args)>(args)...); }, tuple);
        } else {
            return std::apply([&self](auto &&...args) { return self.applier(std::forward<decltype(args)>(args)...); },
                              detail::tuple_tail(tuple));
        }
    }

    template <typename Value>
        requires(IsAggregate<Value> && !IsClassWithDecodingOverload<T, Value>)
    constexpr status_code decode(Value &value, major_type major, byte additionalInfo) {
        auto &self = detail::underlying<T>(this);
        if (major != major_type::Tag) {
            return status_code::no_match_for_tag_on_buffer;
        }

        auto &&tuple = to_tuple(value);
        auto   tag   = self.decode_unsigned(additionalInfo);
        return self.decode_tagged_aggregate(value, tag, tuple);
    }

    template <typename Value>
        requires(IsAggregate<Value> && !IsClassWithDecodingOverload<T, Value>)
    constexpr status_code decode(Value &value, std::uint64_t tag) {
        auto  &self  = detail::underlying<T>(this);
        auto &&tuple = to_tuple(value);
        return self.decode_tagged_aggregate(value, tag, tuple);
    }

    template <typename Value, typename Tuple> constexpr status_code decode_tagged_aggregate(Value &, std::uint64_t tag, Tuple &&tuple) {
        auto &self = detail::underlying<T>(this);
        static_assert(IsTag<Value>, "Only tagged objects end up here. Otherwise they should have called the array overload because of "
                                    "tuple cast. Are you calling this directly? Please don't.");

        if constexpr (HasInlineTag<Value>) {
            if (tag != Value::cbor_tag) {
                return status_code::no_match_for_tag;
            }
            auto array_header_status = self.decode_wrapped_group(tuple);
            if (array_header_status != status_code::success) {
                return array_header_status;
            }
            return std::apply([&self](auto &&...args) { return self.applier(std::forward<decltype(args)>(args)...); }, tuple);
        } else {
            if (tag != std::get<0>(tuple)) {
                return status_code::no_match_for_tag;
            }
            auto array_header_status = self.decode_wrapped_group(detail::tuple_tail(tuple));
            if (array_header_status != status_code::success) {
                return array_header_status;
            }
            return std::apply([&self](auto &&...args) { return self.applier(std::forward<decltype(args)>(args)...); },
                              detail::tuple_tail(tuple));
        }
    }
};

template <typename T> struct cbor_tuple_decoder {
    template <IsUntaggedTuple Tuple> constexpr status_code decode(Tuple &value) {
        auto &self         = detail::underlying<T>(this);
        auto  group_status = self.decode_wrapped_group(value);
        if (group_status != status_code::success) {
            return group_status;
        }
        return std::apply([&self](auto &&...args) { return self.applier(std::forward<decltype(args)>(args)...); }, value);
    }
};

template <typename T> struct cbor_optional_decoder {
    using traits = detail::decoder_traits<T>;
    using byte   = typename traits::byte;

    template <typename U> constexpr status_code decode(std::optional<U> &value, major_type major, byte additionalInfo) {
        auto &self = detail::underlying<T>(this);
        if (major == major_type::Simple && additionalInfo == static_cast<byte>(22)) {
            value = std::nullopt;
            return status_code::success;
        } else {
            using value_type = std::remove_cvref_t<U>;
            value_type t;
            auto       result = self.decode(t, major, additionalInfo);
            if (result == status_code::success) {
                value = std::move(t);
            }
            return result;
        }
        return status_code::no_match_for_optional_on_buffer;
    }
};

template <typename T> struct cbor_variant_decoder {
    using traits = detail::decoder_traits<T>;
    using byte   = typename traits::byte;

    template <typename... U> constexpr status_code decode(std::variant<U...> &value, major_type major, byte additionalInfo) {
        using namespace detail;
        static_assert((IsCborMajor<U> && ...),
                      "All types must be CBOR major types, most likely you have a struct or class without a \"cbor_tag\" in the variant.");

        static_assert(
            (std::is_default_constructible_v<U> && ...),
            "All types must be default constructible. Because in order to decode into the type, it must be default constructed first.");

        using Variant                                     = std::variant<U...>;
        constexpr auto no_ambigous_major_types_in_variant = valid_concept_mapping_v<Variant>;
        constexpr auto matching_major_types               = valid_concept_mapping_array_v<Variant>;
        static_assert(matching_major_types[MajorIndex::Unsigned] <= 1, "Multiple types match against major type 0 (unsigned integer)");
        static_assert(matching_major_types[MajorIndex::Negative] <= 1, "Multiple types match against major type 1 (negative integer)");
        static_assert(matching_major_types[MajorIndex::BStr] <= 1, "Multiple types match against major type 2 (byte string)");
        static_assert(matching_major_types[MajorIndex::TStr] <= 1, "Multiple types match against major type 3 (text string)");
        static_assert(matching_major_types[MajorIndex::Array] <= 1, "Multiple types match against major type 4 (array)");
        static_assert(matching_major_types[MajorIndex::Map] <= 1, "Multiple types match against major type 5 (map)");
        static_assert(matching_major_types[MajorIndex::Tag] <= 1, "Multiple types match against major type 6 (tag)");
        static_assert(matching_major_types[MajorIndex::SimpleValued] <= 1, "Multiple types match against major type 7 (simple)");
        static_assert(matching_major_types[MajorIndex::Boolean] <= 1, "Multiple types match against major type 7 (boolean)");
        static_assert(matching_major_types[MajorIndex::Null] <= 1, "Multiple types match against major type 7 (null)");
        static_assert(matching_major_types[MajorIndex::float16] <= 1, "Multiple types match against major type 7 (float16)");
        static_assert(matching_major_types[MajorIndex::float32] <= 1, "Multiple types match against major type 7 (float32)");
        static_assert(matching_major_types[MajorIndex::float64] <= 1, "Multiple types match against major type 7 (float64)");
        static_assert(matching_major_types[MajorIndex::DynamicTag] == 0,
                      "Variant cannot contain dynamic tags, must be known at compile time, use as_tag_any to catch any tag");

        static_assert(
            no_ambigous_major_types_in_variant,
            "Variant has ambigous major types, if this would compile, only the first type (among the ambigous) would get decoded.");

        [[maybe_unused]] std::optional<std::uint64_t> tag;

        auto &self       = detail::underlying<T>(this);
        auto  try_decode = [&self, major, additionalInfo, &value, &tag]<typename Alt>() -> bool {
            if (!is_valid_major<major_type, Alt>(major)) {
                return false;
            }

            if constexpr (IsSimple<Alt>) {
                if (!compare_simple_value<Alt>(additionalInfo)) {
                    return false;
                }
            }

            Alt         decoded_value;
            status_code result;
            if constexpr (IsTag<Alt>) {
                if (!tag) {
                    tag = self.decode_unsigned(additionalInfo);
                }
                result = self.decode(decoded_value, *tag);
            } else {
                result = self.decode(decoded_value, major, additionalInfo);
            }

            if (result == status_code::success) {
                value = std::move(decoded_value);
                return true;
            } else {
                return false;
            }
        };

        bool found = (try_decode.template operator()<U>() || ...);
        if (!found) {
            return status_code::no_match_in_variant_on_buffer;
        }
        return status_code::success;
    }
};

template <typename T> struct cbor_class_decoder {
    using traits = detail::decoder_traits<T>;
    using byte   = typename traits::byte;

    template <typename C>
        requires(IsClassWithDecodingOverload<T, C>)
    constexpr status_code decode(C &value) {
        return decode_class_impl<C, true>(value);
    }

    template <typename C>
        requires(IsClassWithDecodingOverload<T, C>)
    constexpr status_code decode_without_tag(C &value) {
        return decode_class_impl<C, false>(value);
    }

    template <typename C>
        requires(IsClassWithDecodingOverload<T, C>)
    constexpr status_code decode(C &value, major_type, byte) {
        auto &self = detail::underlying<T>(this);
        self.reader_.seek(-1);
        return self.decode(value);
    }

    template <typename C>
        requires(IsClassWithTagOverload<C> && IsClassWithDecodingOverload<T, C>)
    constexpr status_code decode(C &value, std::uint64_t tag) {
        std::uint64_t class_tag;
        if constexpr (HasTagMember<C>) {
            class_tag = Access::cbor_tag(value);
        } else if constexpr (HasTagNonConstructible<C>) {
            class_tag = cbor_tag<C>();
        } else if constexpr (HasTagFreeFunction<C>) {
            class_tag = cbor_tag(value);
        } else {
            return status_code::no_match_for_tag;
        }

        if (static_cast<std::uint64_t>(class_tag) != tag) {
            return status_code::no_match_for_tag;
        }
        auto &self = detail::underlying<T>(this);
        return self.decode_without_tag(value);
    }

  private:
    template <typename C, bool DecodeTag = true>
        requires(IsClassWithDecodingOverload<T, C>)
    constexpr status_code decode_class_impl(C &value) {
        auto          &self               = detail::underlying<T>(this);
        constexpr bool has_transcode      = HasTranscodeMethod<T, C>;
        constexpr bool has_decode         = HasDecodeMethod<T, C>;
        constexpr bool has_free_decode    = HasDecodeFreeFunction<T, C>;
        constexpr bool has_free_transcode = HasTranscodeFreeFunction<T, C>;

        static_assert(has_transcode ^ has_decode ^ has_free_decode ^ has_free_transcode,
                      "Class must have either (non-const) transcode(T& transcoder, O&& obj) or decode method, also do not forget to return "
                      "value from the "
                      "transcoding operation! "
                      "Give friend access if members are private, i.e friend cbor::tags::Access (full namespace is required)");

        if constexpr (DecodeTag && IsClassWithTagOverload<C>) {
            self.decode(detail::get_major_6_tag_from_class(value));
        }

        if constexpr (has_transcode) {
            auto result = Access::transcode(self, value);
            return result ? status_code::success : result.error();
        } else if constexpr (has_decode) {
            auto result = Access::decode(self, value);
            return result ? status_code::success : result.error();
        } else if constexpr (has_free_decode) {
            auto result = detail::adl_indirect_decode(self, std::forward<C>(value));
            return result ? status_code::success : result.error();
        } else if constexpr (has_free_transcode) {
            auto result = transcode(self, std::forward<C>(value));
            return result ? status_code::success : result.error();
        }

        return status_code::error;
    }
};

template <typename T> struct cbor_simple_decoder {
    using traits = detail::decoder_traits<T>;
    using byte   = typename traits::byte;

    constexpr status_code decode(bool &value, major_type major, byte additionalInfo) {
        auto &self = detail::underlying<T>(this);
        if (major != major_type::Simple) {
            return status_code::no_match_for_simple_on_buffer;
        }
        if (additionalInfo == static_cast<byte>(20)) {
            value = false;
        } else if (additionalInfo == static_cast<byte>(21)) {
            value = true;
        } else {

            return status_code::no_match_for_tag_simple_on_buffer;
        }
        return status_code::success;
    }

    constexpr status_code decode(std::nullptr_t &value, major_type major, byte additionalInfo) {
        auto &self = detail::underlying<T>(this);
        if (major != major_type::Simple) {
            return status_code::no_match_for_simple_on_buffer;
        } else if (additionalInfo != static_cast<byte>(22)) {
            return status_code::no_match_for_tag_simple_on_buffer;
        }
        value = nullptr;
        return status_code::success;
    }

    constexpr status_code decode(float16_t &value, major_type major, byte additionalInfo) {
        auto &self = detail::underlying<T>(this);
        if (major != major_type::Simple) {
            return status_code::no_match_for_simple_on_buffer;
        } else if (additionalInfo != static_cast<byte>(25)) {
            return status_code::no_match_for_tag_simple_on_buffer;
        }
        value = self.read_float16();
        return status_code::success;
    }

    constexpr status_code decode(float &value, major_type major, byte additionalInfo) {
        auto &self = detail::underlying<T>(this);
        if (major != major_type::Simple) {
            return status_code::no_match_for_simple_on_buffer;
        } else if (additionalInfo != static_cast<byte>(26)) {
            return status_code::no_match_for_tag_simple_on_buffer;
        }
        value = self.read_float();
        return status_code::success;
    }

    constexpr status_code decode(double &value, major_type major, byte additionalInfo) {
        auto &self = detail::underlying<T>(this);
        if (major != major_type::Simple) {
            return status_code::no_match_for_simple_on_buffer;
        } else if (additionalInfo != static_cast<byte>(27)) {
            return status_code::no_match_for_tag_simple_on_buffer;
        }
        value = self.read_double();
        return status_code::success;
    }

    constexpr status_code decode(simple &value, major_type major, byte additionalInfo) {
        auto &self = detail::underlying<T>(this);
        if (major != major_type::Simple) {
            return status_code::no_match_for_simple_on_buffer;
        } else if (additionalInfo < static_cast<byte>(24)) {
            value = simple{static_cast<simple::value_type>(additionalInfo)};
        } else if (additionalInfo == static_cast<byte>(24)) {
            value = simple{static_cast<simple::value_type>(self.read_uint8())};
        } else {
            return status_code::no_match_for_tag_simple_on_buffer;
        }

        return status_code::success;
    }
};

template <typename T> struct cbor_header_decoder {
    constexpr auto get_and_validate_header(major_type expectedMajorType) {
        auto [initialByte, additionalInfo] = detail::underlying<T>(this).read_initial_byte();
        if (initialByte != expectedMajorType) {
            throw std::runtime_error("Invalid major type");
        }
        return additionalInfo;
    }

    constexpr auto validate_size(major_type expectedMajor, std::uint64_t expectedSize) {
        auto additionalInfo = get_and_validate_header(expectedMajor);
        auto size           = detail::underlying<T>(this).decode_unsigned(additionalInfo);
        return (size == expectedSize) ? status_code::success : status_code::unexpected_group_size;
    }

    constexpr status_code                           decode(as_array value) { return validate_size(major_type::Array, value.size_); }
    template <typename... Ts> constexpr status_code decode(wrap_as_array<Ts...> value) {
        auto result = validate_size(major_type::Array, value.size_);
        if (result != status_code::success) {
            return result;
        }
        return std::apply([this](auto &&...args) { return detail::underlying<T>(this).applier(std::forward<decltype(args)>(args)...); },
                          value.values_);
    }
    constexpr status_code decode(as_map value) { return validate_size(major_type::Map, value.size_); }
};

template <typename InputBuffer> inline auto make_decoder(InputBuffer &buffer) {
    return decoder<InputBuffer, Options<default_expected, default_wrapping>, cbor_header_decoder, cbor_optional_decoder,
                   cbor_variant_decoder, cbor_tag_decoder, cbor_integer_decoder, cbor_string_decoder, cbor_range_decoder,
                   cbor_probe_decoder, cbor_aggregate_decoder, cbor_tuple_decoder, cbor_class_decoder, cbor_simple_decoder>(buffer);
}

} // namespace cbor::tags
