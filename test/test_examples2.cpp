#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace cbor::tags;

// A class we can't modify (perhaps from a third-party library)
class ExternalClass {
  public:
    ExternalClass() = default;
    ExternalClass(int id, std::string name) : id_(id), name_(std::move(name)) {}

    bool operator==(const ExternalClass &other) const = default;

    // Getters for accessing private data
    int                getId() const { return id_; }
    const std::string &getName() const { return name_; }

    // Setters for modifying private data
    void setId(int id) { id_ = id; }
    void setName(const std::string &name) { name_ = name; }

  private:
    int         id_ = 0;
    std::string name_;
};

// Method 2: Define free functions for encoding and decoding
// This is used when you cannot modify the class itself

// Tag function (optional) - defines a tag for this type when used in a variant
constexpr auto cbor_tag(const ExternalClass &) { return static_tag<54321>{}; }

// Encode function - converts the object to CBOR
template <typename Encoder> constexpr auto encode(Encoder &enc, const ExternalClass &obj) {
    // Access the private data through getters
    return enc(wrap_as_array{obj.getId(), obj.getName()});
}

// Decode function - reconstructs the object from CBOR
template <typename Decoder> constexpr auto decode(Decoder &dec, ExternalClass &&obj) {
    // Temporary variables to hold the decoded values
    int         id;
    std::string name;

    // Decode into temporaries
    auto result = dec(wrap_as_array{id, name});

    // If successful, update the object using setters
    if (result) {
        obj.setId(id);
        obj.setName(name);
    }

    return result;
}

TEST_CASE("ExternalClass serialization") {
    // Create an object
    ExternalClass obj{456, "External Data"};

    // Encode the object
    std::vector<uint8_t> buffer;
    auto                 enc = make_encoder(buffer);
    REQUIRE(enc(obj));

    // Decode back into a new object
    ExternalClass decodedObj;
    auto          dec = make_decoder(buffer);
    REQUIRE(dec(decodedObj));

    CHECK_EQ(decodedObj, obj);
}
