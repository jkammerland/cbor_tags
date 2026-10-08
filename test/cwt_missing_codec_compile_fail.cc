#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cwt/types.h>

int main() {
    std::vector<std::byte> bytes;
#ifdef CBOR_TAGS_CWT_VIEW
    cbor::tags::cwt::claims_view value;
#else
    cbor::tags::cwt::claims_set value;
#endif
#ifdef CBOR_TAGS_CWT_ENCODE
    return cbor::tags::make_encoder(bytes)(value) ? 0 : 1;
#else
    return cbor::tags::make_decoder(bytes)(value) ? 0 : 1;
#endif
}
