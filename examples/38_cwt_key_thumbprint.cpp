// Experimental: public key and thumbprint APIs may change.
#include "cwt_key_sample.h"

#include <cassert>

/**
 * public-key = { 1: 2, -1: 1, -2: bstr .size 32, -3: bstr .size 32, ? 3: int, ? 4: [* int] }
 * thumbprint-input = { 1: 2, -1: 1, -2: bstr .size 32, -3: bstr .size 32 }
 */
int main() {
    using namespace cbor::tags;

    auto key = example_holder_key();

    // Hash the required public parameters, excluding algorithm and operation restrictions.
    auto digest = cwt::key_thumbprint<cwt::openssl_sha256>(key);
    if (!digest) {
        return 1;
    }

    // Segments own ordered headers and borrow key bytes, useful for streaming hash providers.
    auto parts = cwt::make_key_thumbprint_input(key);
    if (!parts) {
        return 1;
    }

    assert(digest->size() == 32);
}
