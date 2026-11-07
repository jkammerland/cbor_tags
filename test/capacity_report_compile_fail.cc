#include "resumable_ring_fixture.h"

#include <cbor_tags/cbor_resumable.h>
#include <cstdint>

namespace ct = cbor::tags;

// Each misdeclared at_capacity() must fail loudly instead of being ignored
#if defined(CBOR_TAGS_CAPACITY_NOT_NOEXCEPT)
struct misdeclared : resumable_test::ring_source<false, 11> {
    bool at_capacity() const { return false; }
};
#elif defined(CBOR_TAGS_CAPACITY_NOT_CONST)
struct misdeclared : resumable_test::ring_source<true, 11> {
    bool at_capacity() noexcept { return false; }
};
#elif defined(CBOR_TAGS_CAPACITY_NOT_BOOL)
struct misdeclared : resumable_test::ring_source<false, 11> {
    int at_capacity() const noexcept { return 0; }
};
#endif

int main() {
    misdeclared   source;
    std::uint64_t value{};
#if defined(CBOR_TAGS_CAPACITY_NOT_CONST)
    auto operation = ct::make_resumable_encoder(source)(value);
#else
    auto operation = ct::make_resumable_decoder(source)(value);
#endif
    (void)operation;
}
