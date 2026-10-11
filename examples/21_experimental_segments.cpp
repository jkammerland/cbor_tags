// Experimental: segment APIs and borrowing behavior may change.
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_ranges.h"
#include "cbor_tags/cbor_segments.h"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

using namespace cbor::tags;

/**
items = (tstr, bstr)
*/
int main() {
    // Store headers and views into the original data as ordered output pieces.
    byte_segments segments;

    // Keep both original payloads alive and unchanged until the segments are consumed.
    std::string text = "ready";
    std::vector<std::byte> payload{std::byte{1}, std::byte{2}, std::byte{3}};

    // Layout: owned text header, original text view, owned byte header, original byte view.
    auto encoder = make_encoder(segments);
    if (!encoder(as_tstr_range(text), as_bstr_range(payload))) {
        return 1;
    }

    // Check the ordered header-and-view layout without flattening it.
    if (segments.size() != 4 || !segments[0].is_owned() || !segments[1].is_borrowed() ||
        !segments[2].is_owned() || !segments[3].is_borrowed()) {
        return 2;
    }

    // The two borrowed payload segments point directly into the original data.
    auto original_text = std::as_bytes(std::span{text.data(), text.size()});
    if (segments[1].bytes().data() != original_text.data() ||
        segments[1].bytes().size() != original_text.size() ||
        segments[3].bytes().data() != payload.data() ||
        segments[3].bytes().size() != payload.size()) {
        return 2;
    }

    // Borrow the two owned headers as separate decoder inputs.
    auto text_header_bytes = segments[0].bytes();
    auto byte_header_bytes = segments[2].bytes();

    // Declare receivers for the text and byte-string headers.
    as_text_any text_header;
    as_bstr_any byte_header;

    // Decode each header independently of its borrowed payload.
    auto text_decoder = make_decoder(text_header_bytes);
    auto byte_decoder = make_decoder(byte_header_bytes);
    if (!text_decoder(text_header) || !byte_decoder(byte_header)) {
        return 1;
    }

    // Check both header types and their declared lengths.
    if (text_header.indefinite || text_header.size != text.size() || byte_header.indefinite ||
        byte_header.size != payload.size()) {
        return 2;
    }
    return 0;
}
