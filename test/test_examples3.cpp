#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace cbor::tags;

class PrivateDataClass {
  public:
    PrivateDataClass() = default;
    explicit PrivateDataClass(int id, std::string name) : id_(id), name_(std::move(name)) {}

    bool operator==(const PrivateDataClass &other) const = default;

  private:
    int         id_ = 0;
    std::string name_;

    static_tag<12345> cbor_tag;

    // Method 1: Use member functions for encoding/decoding
    friend cbor::tags::Access; // Grant access to the library

    template <typename Encoder> constexpr auto encode(Encoder &enc) const {
        return enc(wrap_as_array{id_, name_}); // Encode as array with 2 elements
    }

    template <typename Decoder> constexpr auto decode(Decoder &dec) {
        return dec(wrap_as_array{id_, name_}); // Decode as array with 2 elements
    }
};

TEST_CASE("Private Data Class Serialization") {
    // Create an object with private data
    PrivateDataClass obj{123, "Private Data"};

    // Encode the object
    std::vector<uint8_t> buffer;
    auto                 enc = make_encoder(buffer);
    { [[maybe_unused]] auto _ = enc(obj); }
    // Decode into a new object
    auto             dec = make_decoder(buffer);
    PrivateDataClass decoded_obj;
    { [[maybe_unused]] auto _ = dec(decoded_obj); }

    // Objects should be equal
    CHECK_EQ(obj, decoded_obj);
}