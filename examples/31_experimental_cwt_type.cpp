// Experimental: CWT header APIs may change.
#include <cassert>
#include <cbor_tags/cwt/cwt.h>
#include <string>
#include <variant>

/**
protected-header = { 16 => "application/example+cwt" }
*/
int main() {
    using namespace cbor::tags;

    // The type describes the complete COSE object and belongs in its protected headers.
    cwt::header_map source;
    source.typ = std::string{"application/example+cwt"};

    auto protected_bytes = cwt::encode_protected_header(source);
    if (!protected_bytes) {
        return 1;
    }

    // Parsing exposes the type; signature verification authenticates it in a complete token.
    cwt::header_map received;
    if (!cwt::decode_protected_header(*protected_bytes, received)) {
        return 1;
    }

    assert(received.typ);
    assert(std::get<std::string>(*received.typ) == "application/example+cwt");
}
