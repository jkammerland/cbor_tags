---
name: cbor-tags-codecs
description: Use when working in the cbor_tags C++ library to add, review, or test opt-in encoder/decoder codec mixins, CBOR tag extensions, RFC 8746 typed arrays, lazy tag payload decoding, or custom view codecs.
---

# cbor_tags Codecs

## Extension Boundary

- Keep the default encoder/decoder behavior in the primary headers.
- Put specialized semantic codecs under `include/cbor_tags/extensions/`.
- Users opt in by including the extension header and passing codec mixins:

```cpp
#include <cbor_tags/extensions/my_codec.h>

auto enc = cbor::tags::make_encoder<my_codec>(buffer);
auto dec = cbor::tags::make_decoder<my_codec>(buffer);
```

Multiple codecs are allowed:

```cpp
auto enc = cbor::tags::make_encoder<codec_a, codec_b>(buffer);
auto dec = cbor::tags::make_decoder<codec_a, codec_b>(buffer);
```

## Mixin Pattern

Codec mixins are templates over the final encoder or decoder type. Inherit
`cbor_codec_mixin_base<Self>` so unsupported overloads stay deleted and visible
to overload resolution.

```cpp
template <typename Self>
struct my_codec : cbor::tags::cbor_codec_mixin_base<Self> {
    using cbor::tags::cbor_codec_mixin_base<Self>::decode;
    using cbor::tags::cbor_codec_mixin_base<Self>::encode;

    void encode(const my_type& value) {
        auto& enc = static_cast<Self&>(*this);
        enc(cbor::tags::make_tag_pair(cbor::tags::static_tag<100>{}, value.payload));
    }

    [[nodiscard]] cbor::tags::status_code
    decode(my_type& value, cbor::tags::major_type major, std::byte additional_info) {
        auto& dec = static_cast<Self&>(*this);
        if (major != cbor::tags::major_type::tag) {
            return cbor::tags::status_code::unexpected_major;
        }

        std::uint64_t tag{};
        auto status = dec.decode_unsigned(tag, additional_info);
        if (status != cbor::tags::status_code::success) {
            return status;
        }
        if (tag != 100U) {
            return cbor::tags::status_code::unexpected_tag;
        }
        return dec(value.payload);
    }
};
```

Decode overloads receive the already-read initial byte split into `major` and
`additional_info`. Validate both, consume exactly the payload for the type, and
return `status_code` instead of throwing for malformed CBOR.

## Borrowing Rules

- Borrowed encode wrappers must be lvalue-only when they reference user memory.
- Do not add compatibility forwarding headers for removed extension APIs.
- Contiguous decoded views require contiguous input buffers; provide owning
  decode paths for non-contiguous buffers.
- Lazy tag matches borrow from the source buffer. `match.make_decoder<codec>()`
  and `match.decode<codec>(value)` decode only the matched payload with the
  same opt-in codec pack style as top-level decoders.

## RFC 8746 Typed Arrays

Use only:

```cpp
#include <cbor_tags/extensions/rfc8746_typed_arrays.h>

using namespace cbor::tags;
using namespace cbor::tags::ext::rfc8746;

std::vector<std::int32_t> values{1, 2, 3};

std::vector<std::byte> buffer;
auto enc = make_encoder<typed_array_codec>(buffer);
enc(as_typed_array(values));

typed_array<std::int32_t> decoded;
auto dec = make_decoder<typed_array_codec>(buffer);
dec(decoded);
```

For lazy tag payloads:

```cpp
auto tags = find_tags<100>(buffer);
auto it = tags.begin();

typed_array<std::int32_t> decoded;
it->decode<typed_array_codec>(decoded);
```

## Test Checklist

- Add focused doctest cases beside related coverage, usually `test/test_cbor_typed_arrays.cpp`,
  `test/test_lazy_tags.cpp`, or range/raw-view tests.
- Cover compile-time opt-in behavior, roundtrip encode/decode, malformed input,
  borrowed lifetime constraints, non-contiguous buffers, and lazy payload decode
  when relevant.
- Run at least:

```bash
cmake --build --preset=debug --parallel
ctest --preset=debug --output-on-failure
scripts/format-cxx.sh --check
scripts/lint-cxx.sh
```
