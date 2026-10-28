#pragma once

namespace cbor::tags::detail {

struct encoded_element_complete {
    constexpr void operator()() const noexcept {}
};

struct encode_elements_available {
    constexpr bool operator()() const noexcept { return true; }
};

// The ordinary loop has no progress bookkeeping. Resumable callers can retain
// the completed prefix without changing codec dispatch or element binding.
template <typename Encoder, typename Iterator, typename Sentinel, typename Completed = encoded_element_complete,
          typename Available = encode_elements_available>
constexpr void encode_array_elements(Encoder &encoder, Iterator &current, const Sentinel &last, Completed completed = {},
                                     Available available = {}) {
    for (; current != last && available(); ++current) {
        const auto &item = *current;
        encoder.encode(item);
        completed();
    }
}

} // namespace cbor::tags::detail
