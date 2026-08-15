# Codec Extensions

`cbor_tags` keeps non-default wire policies behind explicit codec extensions.
Include the extension header and pass the codec mixin to `make_encoder` or
`make_decoder`:

```cpp
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/extensions/smart_ptr.h"

using namespace cbor::tags;
using namespace cbor::tags::ext::smart_ptr;

std::vector<std::byte> bytes;

auto enc = make_encoder<unique_ptr_codec>(bytes);
auto dec = make_decoder<unique_ptr_codec>(bytes);
```

Multiple extensions can be installed together when their overloads are designed
to compose:

```cpp
auto enc = make_encoder<unique_ptr_codec, shared_ptr_codec>(bytes);
auto dec = make_decoder<unique_ptr_codec, shared_ptr_codec>(bytes);
```

Extension codecs are class-template mixins over the final encoder or decoder
type. Encoder-only mixins should inherit `cbor_encoder_mixin_base<Self>`,
decoder-only mixins should inherit `cbor_decoder_mixin_base<Self>`, and
bidirectional codecs should inherit `cbor_codec_mixin_base<Self>`. Bring the
matching base overloads into scope so unsupported overloads remain deleted and
visible to overload resolution.

There are two layers to keep separate:

- User-facing customization functions should compose normal CBOR items through
  the public call operator, for example `return enc(wrap_as_array{a, b});`.
- Codec implementations may call `enc.encode(...)`, `dec.decode(...)`, and
  `dec.decode(value, major, additional_info)` directly. These are the internal
  dispatch primitives, especially after the initial byte has already been
  consumed and split into `major` and `additional_info`.

Do not add sequencing APIs such as `encode_all`; use `operator()(...)` for
normal public composition and direct dispatch only inside codec internals.

```cpp
template <typename Self>
struct my_codec : cbor::tags::cbor_codec_mixin_base<Self> {
    using cbor::tags::cbor_codec_mixin_base<Self>::decode;
    using cbor::tags::cbor_codec_mixin_base<Self>::encode;

    void encode(const my_type& value) {
        auto& enc = static_cast<Self&>(*this);
        enc.encode(cbor::tags::static_tag<100>{});
        enc.encode(value.payload);
    }

    [[nodiscard]] cbor::tags::status_code
    decode(my_type& value, cbor::tags::major_type major, std::byte additional_info) {
        auto& dec = static_cast<Self&>(*this);
        const auto tag_status = dec.decode(cbor::tags::static_tag<100>{}, major, additional_info);
        if (tag_status != cbor::tags::status_code::success) {
            return tag_status;
        }
        return dec.decode(value.payload);
    }
};
```

One-way extensions can use the narrower bases:

```cpp
template <typename Self>
struct my_encoder_only : cbor::tags::cbor_encoder_mixin_base<Self> {
    using cbor::tags::cbor_encoder_mixin_base<Self>::encode;

    void encode(const my_type& value) {
        static_cast<Self&>(*this).encode(value.payload);
    }
};

template <typename Self>
struct my_decoder_only : cbor::tags::cbor_decoder_mixin_base<Self> {
    using cbor::tags::cbor_decoder_mixin_base<Self>::decode;

    [[nodiscard]] cbor::tags::status_code
    decode(my_type& value, cbor::tags::major_type major, std::byte additional_info) {
        return static_cast<Self&>(*this).decode(value.payload, major, additional_info);
    }
};
```

## Custom Decoder Success Contract

This contract applies to member and free `decode`/`transcode`
customizations as well as opt-in decoder mixins:

- Success means that the customization accepted and consumed the complete CBOR
  representation assigned to it. For an ordinary custom type, that is exactly
  one CBOR payload item unless an API explicitly handles an enclosing tag,
  header, or other structural wrapper.
- A user-facing `decode(Decoder&)` or `transcode(Decoder&)` customization starts
  at the payload it owns (after any framework-managed tag) and must consume it
  through the decoder. Returning success without decoding that payload is a
  customization bug.
- A codec overload receiving `(value, major, additional_info)` runs after the
  initial byte has already been consumed. It must validate that header and
  consume the remaining payload. A valid one-byte item may require no
  additional read inside this overload; the already-consumed header is still
  its CBOR representation.
- On malformed, incomplete, or non-matching input, return and propagate the
  corresponding non-success status. Do not report success to make the caller
  retry or advance.

The library does not infer, repair, or sandbox the semantics of user-provided
customization code. For example, this zero-item decoder violates the contract:

```cpp
template <typename Decoder>
auto decode(Decoder&, my_type&&) {
    return typename Decoder::expected_type{}; // Invalid: no CBOR item decoded.
}
```

Delegate to the decoder so success represents an actual item:

```cpp
template <typename Decoder>
auto decode(Decoder& dec, my_type&& value) {
    return dec(value.payload);
}
```

Decode overloads receive the already-read initial byte split into `major` and
`additional_info`. Validate both, consume exactly the payload for the type, and
return `status_code` for malformed CBOR. Encode overloads may throw for API
misuse that cannot be represented by `status_code`; the public encoder catches
exceptions and returns `status_code::error`.

The built-in extension implementations also use small helpers under
`include/cbor_tags/detail/` for operations the public call operator cannot
express, such as reading a size from an already-consumed header or appending
generated payload bytes after a byte-string header. Those helpers are internal
project infrastructure, not public extension-author API.

Borrowed wrapper helpers should be lvalue-only when they store references to
user memory:

```cpp
template <typename T>
my_root<T> as_my_root(my_session& session, const T& value);

template <typename T>
my_root<T> as_my_root(my_session&, const T&&) = delete;
```

Current public extension headers:

- `cbor_tags/extensions/custom_codec_1.h`: schema-bound `tag(bstr)` payload
  codec. See [Custom Codec 1](custom_codec_1.md).
- `cbor_tags/extensions/smart_ptr.h`: unique ownership as `T / null` and shared
  ownership through tags 28/29 with codec-owned or user-owned reference tables.
- `cbor_tags/extensions/std_expected.h`: opt-in `std::expected` return type
  support in C++23 and newer builds.
- `cbor_tags/extensions/rfc8746_typed_arrays.h`: RFC 8746 typed-array helpers.
  See [RFC 8746 Typed Arrays](rfc8746_typed_arrays.md).
- `cbor_tags/extensions/cbor_visualization.h`: CDDL, annotation, and diagnostic
  rendering helpers.
- `cbor_tags/extensions/cddl_traits.h`: traits for describing custom extension
  types in generated CDDL.
