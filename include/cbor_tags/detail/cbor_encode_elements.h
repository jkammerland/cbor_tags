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

// Commit keys and mapped values separately so a suspended pair never replays
// its already published key. Ordinary callers use the no-op callback.
template <typename Encoder, typename Iterator, typename Sentinel, typename Completed = encoded_element_complete,
          typename Available = encode_elements_available>
constexpr void encode_map_elements(Encoder &encoder, Iterator &current, const Sentinel &last, bool &key_complete, Completed completed = {},
                                   Available available = {}) {
    for (; current != last && available(); ++current) {
        const auto &entry = *current;
        if (!key_complete) {
            encoder.encode(pair_first(entry));
            key_complete = true;
            completed();
        }
        encoder.encode(pair_second(entry));
        key_complete = false;
        completed();
    }
}

} // namespace cbor::tags::detail
