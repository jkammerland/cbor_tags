#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cwt/fields.h>
#include <vector>

using namespace cbor::tags;

int main() {
    int value{};

#if defined(CWT_FIELDS_DUPLICATE)
    auto map = cwt::fields(cwt::field<1>(value), cwt::fields(cwt::field<1U>(value)));
#elif defined(CWT_FIELDS_TEMPORARY)
    auto map = cwt::fields(cwt::field<1>(42));
#elif defined(CWT_FIELDS_BOOLEAN)
    auto map = cwt::fields(cwt::field<true>(value));
#else
    auto                   map = cwt::fields(cwt::field<1>(value));
    std::vector<std::byte> buffer;
#if defined(CWT_FIELDS_MISSING_ENCODER)
    make_encoder(buffer)(map);
#elif defined(CWT_FIELDS_MISSING_DECODER)
    make_decoder(buffer)(map);
#else
#error Select a compile-failure case.
#endif
#endif
}
