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

auto enc = make_encoder<nullable_ptr_codec>(bytes);
auto dec = make_decoder<nullable_ptr_codec>(bytes);
```

Multiple extensions can be installed together when their overloads are designed
to compose:

```cpp
auto enc = make_encoder<nullable_ptr_codec, shared_graph_codec>(bytes);
auto dec = make_decoder<nullable_ptr_codec, shared_graph_codec>(bytes);
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

- `cbor_tags/extensions/smart_ptr.h`: nullable `unique_ptr`/`shared_ptr` values
  and shared `shared_ptr` identity sessions.
- `cbor_tags/extensions/std_expected.h`: opt-in `std::expected` return type
  support in C++23 and newer builds.
- `cbor_tags/extensions/rfc8746_typed_arrays.h`: RFC 8746 typed-array helpers.
  See [RFC 8746 Typed Arrays](rfc8746_typed_arrays.md).
- `cbor_tags/extensions/cbor_visualization.h`: CDDL, annotation, and diagnostic
  rendering helpers.
