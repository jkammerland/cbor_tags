# CWT and COSE

A CBOR Web Token (CWT) carries claims such as an issuer, subject and expiry.
CBOR Object Signing and Encryption (COSE) provides structures for protecting
those claims; this library supports signing and verifying COSE messages.

**Experimental:** CWT and COSE model and codec APIs may change.

Types that own their data and views that borrow existing data live in
`cbor_tags/cwt/types.h` under
`cbor::tags::cwt`. Their optional codec is `cbor::tags::codec::cwt`, provided by
`cbor_tags/codec/cwt.h`. The convenience header `cbor_tags/cwt/cwt.h` includes
the codec and serialization/tag helpers.

## Encode and decode claims

Start with the [compiled claims example](../examples/23_experimental_cwt_claims.cpp)
for a complete encode/decode program with its CDDL and explicit result checks.

```cpp
#include <cbor_tags/cwt/cwt.h>

using namespace cbor::tags;

cwt::claims_set claims{
    .issuer = "coap://as.example.com",
    .subject = "erikw",
    .expiration = std::int64_t{1444064944},
};

auto bytes = cwt::encode_to_bytes(claims);
if (bytes) {
    cwt::claims_set decoded{};
    auto result = make_decoder<codec::cwt>(*bytes)(decoded);
    // Check result before using decoded.
}
```

Claims use the integer labels in [RFC 8392 §3.1](https://www.rfc-editor.org/rfc/rfc8392.html#section-3.1).
Encoding claims alone does not sign them. The application validates issuer,
audience and time constraints after signature verification.

`claims_set::audience` is `std::optional<cwt::audience_claim>`; the variant
holds one string or a vector of strings. Empty arrays are accepted.

```cpp
claims.audience = cwt::audience_claim{std::string{"coap://light.example.com"}};
claims.audience = cwt::audience_claim{
    std::vector<std::string>{"coap://light.example.com", "coap://sensor.example.com"}};
```

`encode_to_bytes` installs the CWT codec. With an encoder or decoder of your own,
select it explicitly with `make_encoder<codec::cwt>` or
`make_decoder<codec::cwt>`. Omitting it for CWT models is a compile-time error.

## Borrowed models and application layouts

| Owning model | Borrowed model |
| --- | --- |
| `claims_set` | `claims_view` |
| `header_map` | `header_map_view` |
| `cose_signature` | `cose_signature_view` |
| `cose_sign` | `cose_sign_view` |
| `cose_sign1` | `cose_sign1_view` |
| `sig_structure` | `sig_structure_view` |

Views use `std::string_view` and `std::span<const std::byte>` for text and byte
payloads. Scalars are copied; vectors hold descriptors such as signatures and
critical labels and may allocate. `claims_view::audience` is an optional
`audience_view`: one `std::string_view` or a vector of them. `as_view` preserves
that shape and borrows each audience string. `as_view(value)` creates a view of
an existing object; temporary objects are rejected. Keep the owner alive and its
referenced storage stable until all views are finished.

Application types can use any field names and nesting. Refer to the required
fields through a claims view:

```cpp
struct application_token {
    struct identity {
        std::string provider;
        std::string user;
    } identity;
    std::int64_t deadline;
};

// Keep the application data alive while its text is borrowed.
application_token token{
    .identity = {.provider = "provider", .user = "alice"},
    .deadline = 1444064944,
};

// Borrow the two strings and copy the integer deadline.
cwt::claims_view claims{
    .issuer = token.identity.provider,
    .subject = token.identity.user,
    .expiration = token.deadline,
};

// Encode the claims into an independent byte buffer.
auto bytes = cwt::encode_to_bytes(claims);
```

Decode a view directly from contiguous input:

```cpp
if (bytes) {
    cwt::claims_view decoded;
    auto result = make_decoder<codec::cwt>(*bytes)(decoded);
    // On success, decoded borrows from *bytes. Keep that buffer alive and stable.
}
```

Borrowed decoding accepts definite text/byte fields within definite or indefinite
claim maps and COSE messages. Fragmented indefinite text/bytes require an owning
model; a single view cannot represent their separate chunks. Noncontiguous input
returns `contiguous_view_on_non_contiguous_data`. As with owning models, errors
preserve the destination and leave the decoder cursor terminal.

## Build

```cmake
target_link_libraries(app PRIVATE cbor::cwt) # header-only, crypto-free
```

`-DCBOR_TAGS_STL_ONLY=ON` selects C++26 standard-library facilities without
public third-party dependencies. Conan's aggregate target is `cbor::all`.

`as_cwt`, `as_cose_sign1` and `as_cose_sign` add tags 61, 18 and 98.

## Decode and validation rules

- Claim maps and COSE messages accept definite or indefinite lengths. Messages
  require exact field counts and closing breaks; `COSE_Sign` requires at least
  one signature. `Sig_structure` encoding uses definite arrays.
- Complete CWT/COSE decodes replace the destination only on success. Failure
  preserves it; the decoder cursor remains terminal, without rollback.
- Duplicate integer or text claim labels are rejected, including unknown and
  noncanonical spellings. Unknown integer and text claims are consumed and discarded.
- Integer dates must fit `int64_t`; floating dates must be finite.
- Header labels accept integers or text. Duplicate labels within each map,
  unsupported/empty `crit`, absent critical targets and trailing protected-header
  bytes are rejected. Supported critical targets are `alg` (1) and `kid` (4).

## Signing and verification

See [COSE signing and verification](cose_signing.md) for the backend contract and
signing policy. The optional [OpenSSL ES256 backend](cwt_openssl.md) provides
a first-party implementation.
