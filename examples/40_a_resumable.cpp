#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_resumable.h"
#include "example_support.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ct = cbor::tags;

struct Message {
    std::string name;
    std::vector<std::uint32_t> values;
};

/**
message = [tstr, [* uint]]
*/
int main() {
    Message m{};
    std::vector<std::byte> buffer{example::to_bytes("826773746174696f6e8301")};
    /*
    82                   # array(2)
       67                #   text(7)
          73746174696f6e #     "station"
       83                #   array(3) <-- ONLY 1 element below !?
          01             #     unsigned(1)
    */

    // Try to decode incomplete object. Message::values only has 1 element, expected 3
    {
        auto dec = ct::make_decoder(buffer);
        auto result = dec(m);
        assert(!result && result.error() == ct::status_code::incomplete);
        // This decoding is now incomplete, and there is no way to reasonably resume decoding with the current API.
    }

    // Instead, if this can happen in your setup, the solution is to use the resumable APIs!
    // (Reset 'm' first, a failed decode can leave half-decoded stuff in it, see doc/status_errors.md)
    m = Message{};
    auto resumable_dec = ct::make_resumable_decoder(buffer);
    auto session = resumable_dec(m);
    assert(!session.done());
    assert(m.name == "station");
    assert(m.values.size() == 1);

    // Let us append 2 more uints. In reality, you would use a resumable encoder, but for simplicity...
    auto enc = ct::make_encoder(buffer);
    const auto appended = enc(10u, 1000u);
    assert(appended);
    // See example 41 for resumable encoding.

    // In theory, we should be able to resume and complete the 3 element array decoding.
    const auto state = session.resume();
    assert(state == ct::resume_state::done);
    const auto result = session.result();
    assert(result);
    assert(*result == buffer.size());

    // Sanity check the decoded message 'm'
    assert(m.name == "station");
    assert(m.values.size() == 3);
    assert(m.values[0] == 1);
    assert(m.values[1] == 10);
    assert(m.values[2] == 1000);

    return 0;
}
