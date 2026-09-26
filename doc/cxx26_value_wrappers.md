# C++26 value-wrapper codecs

## Design contract

This design uses the library's existing codec composition and typed wire
models. `std::indirect<T>` has one fixed payload type, so its reusable opt-in
codec forwards one complete CBOR item to `T`. `std::polymorphic<Base>` does
not identify the application's concrete types or protocol; the executable
animal codec example supplies that mapping using a tagged `std::variant`.
There is no global type registry, RTTI name on the wire, or ownership tag.
Existing smart-pointer codecs and tags 28/29 are unchanged.

### Indirect values

- Include `cbor_tags/extensions/std_indirect.h` and select
  `cbor::tags::ext::std_indirect::std_indirect_codec` with the normal
  `make_encoder<codec...>` / `make_decoder<codec...>` factories.
- The wire representation is exactly the representation of `T`, without an
  extra tag, array, or null state. `T` must produce and consume one complete
  item. Header-only types, empty reflected groups, and unwrapped multi-item
  groups are rejected at compile time. Application codecs/customizations own
  this same item contract.
- Encoding a moved-from, valueless wrapper returns `status_code::error`
  before writing that wrapper's item. It never dereferences the absent value.
  A payload that legitimately encodes null still can do so; null does not
  mean that the wrapper itself is valueless.
- Decoding requires a default-initializable `T`. An engaged destination is
  decoded in place: its allocator and existing payload are retained, including
  normal container append behavior. A valueless destination is first rebuilt
  with its own `get_allocator()`, then decoded. Allocation/construction uses
  the standard wrapper's allocator rules; no default allocator is substituted.
- Decoder errors propagate from the payload. `std::bad_alloc` and
  `std::length_error` map to `out_of_memory`, other `std::exception` failures
  to `error`, through the existing public decoder boundary. After failure the
  input is terminal and the payload may contain a decoded prefix. An allocation
  failure while rebuilding a valueless destination leaves it valueless.
- Put a discriminated payload **inside** the wrapper, for example
  `std::indirect<std::variant<int, std::string>>`. A wrapper used as a variant
  alternative needs an application-owned wire variant; the codec rejects
  direct/nested variant alternatives containing indirect wrappers. It does
  not guess a discriminant from a composed codec's wire representation.

### Polymorphic values

`examples/cxx26_value_wrappers.h` contains an opt-in `animal_codec`. Its wire
model is `animal_wire`, a variant of two tagged tuples:

| Concrete type | Complete item |
| --- | --- |
| `dog` | `#6.60010([uint, tstr])`, age then name |
| `cat` | `#6.60011([tstr, uint])`, name then lives |

The example's concrete types are final. Exact dynamic type selection is local
to the encoder; decoding uses the ordinary typed variant decoder and then
constructs `std::polymorphic<animal, Alloc>` with `allocator_arg`, the
destination allocator, and `in_place_type<dog/cat>`. This preserves the concrete
type without slicing. Applications with another identification scheme can use
the same codec hooks, including virtual discriminants without RTTI.

The tagged wire variant is the registration list: duplicate tags are rejected
by the existing variant decoder's compile-time checks. Unknown tags return the
ordinary `no_match_in_variant_on_buffer` status, as do payload type mismatches
classified as retriable by that decoder. Unsupported dynamic types and moved-from
values return `error` before the item is written. Null is not accepted as a
wrapper state. Malformed/truncated payloads fail through the existing decoder.
The example constructs the replacement only after a complete wire value has
decoded, so a payload/constructor failure leaves the old destination value.
Input is still terminal on failure; this is not a retry or rollback facility.

For composition, select `animal_codec` in the same factory pack as other
codecs. Arrays of polymorphic values and indirect polymorphic values use that
same dispatch. Keep variant discrimination in the explicit wire model; do not
put the opaque polymorphic wrapper directly in a decoder variant.

### CDDL and availability

`cbor::tags::cddl::cddl_wire_type<T>::type` is an opt-in schema customization
for types whose codec uses an explicit wire model. It supplies a schema, not a
runtime conversion or decoder registration. `std_indirect.h` specializes it
for `std::indirect<T, Alloc>` as `T`. The animal example specializes it as
`animal_wire`. CDDL generation recursively uses that wire type, including
inside arrays and aggregates. Variant alternatives with a wire-type
customization require an explicit wire variant, matching the runtime policy.
Wire aliases must lead to a supported schema type without forming an alias cycle.
The animal codec requires the default wrapped-group option, which keeps each
tag's two-field payload inside one array.

The extension requires `__cpp_lib_indirect >= 202502L`; the example also
requires `__cpp_lib_polymorphic >= 202502L`. C++20 consumers keep their existing
headers and behavior. Tests are gated on actual library features, configuration
reports availability, and the dedicated C++26 job requires both facilities.

## Usage

```cpp
#include <cbor_tags/extensions/std_indirect.h>
#include <vector>

using cbor::tags::ext::std_indirect::std_indirect_codec;
std::indirect<int> value(42);
std::vector<std::byte> bytes;
auto encoded = cbor::tags::make_encoder<std_indirect_codec>(bytes)(value);
std::indirect<int> decoded;
auto result = cbor::tags::make_decoder<std_indirect_codec>(bytes)(decoded);
// bytes == {0x18, 0x2a}; *decoded == 42
```

See the compiled example and `test/test_std_value_wrappers.cpp` for two concrete
polymorphic types, nested composition, allocator behavior, and failure cases.
As with all application-selected recursive schemas, the caller owns admission,
resource limits, synchronization, and input/output lifetimes.
