#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <cstddef>
#include <string>
#include <vector>

namespace ct = cbor::tags;

// File-local on purpose. The declaration below gets internal linkage from it,
// and Clang warns about a flag in there that nobody reads. This file is built
// with that warning as an error, and label only ever gets encoded
namespace {
struct label {
    std::string text;

    template <typename Encoder> auto encode(Encoder &encoder) const { return encoder(text); }
};
} // namespace

// A declaration has both flags, leaving one out is the compile-fail case
template <> struct cbor::tags::resumable_retry_traits<label> {
    static constexpr bool encode = true;
#if !defined(CBOR_TAGS_RETRY_ONE_FLAG)
    static constexpr bool decode = false;
#endif
};

int main() {
    std::vector<std::byte> bytes;
    const label            value{"only ever encoded"};

    auto encoder   = ct::make_resumable_encoder(bytes);
    auto operation = encoder(value);
    return operation.done() && operation.result() ? 0 : 1;
}
