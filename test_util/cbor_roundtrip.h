#pragma once

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <concepts>
#include <cstddef>
#include <doctest/doctest.h>
#include <ranges>
#include <vector>

namespace cbor::tags::test {
namespace detail {

template <template <typename> typename... Codecs, CborOutputBuffer Buffer> auto make_roundtrip_encoder(Buffer &buffer) {
    if constexpr (sizeof...(Codecs) == 0) {
        return make_encoder(buffer);
    } else {
        return make_encoder<Codecs...>(buffer);
    }
}

template <template <typename> typename... Codecs, CborInputBuffer Buffer> auto make_roundtrip_decoder(Buffer &buffer) {
    if constexpr (sizeof...(Codecs) == 0) {
        return make_decoder(buffer);
    } else {
        return make_decoder<Codecs...>(buffer);
    }
}

} // namespace detail

template <template <typename> typename... Codecs, typename Input, typename Output> void roundtrip_into(const Input &input, Output &output) {
    std::vector<std::byte> buffer;
    auto                   enc = detail::make_roundtrip_encoder<Codecs...>(buffer);
    REQUIRE(enc(input));

    auto dec = detail::make_roundtrip_decoder<Codecs...>(buffer);
    REQUIRE(dec(output));
    REQUIRE(dec.tell() == std::ranges::end(buffer));
}

template <template <typename> typename... Codecs, std::default_initializable T> T roundtrip(const T &input) {
    T output{};
    roundtrip_into<Codecs...>(input, output);
    return output;
}

} // namespace cbor::tags::test
