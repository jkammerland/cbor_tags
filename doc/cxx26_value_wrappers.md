# C++26 value-wrapper codecs

Include the opt-in extension and select `std_indirect_codec` in your factories:

```cpp
#include <cbor_tags/extensions/std_indirect.h>
#include <vector>

using cbor::tags::ext::std_indirect::std_indirect_codec;
std::indirect<int> value(42);
std::vector<std::byte> bytes;
auto encoded = cbor::tags::make_encoder<std_indirect_codec>(bytes)(value);
std::indirect<int> decoded;
auto result = cbor::tags::make_decoder<std_indirect_codec>(bytes)(decoded);
// On success, *decoded == 42.
```

`std::indirect<T>` uses `T`'s wire representation, which must be one complete
CBOR item. Encoding a valueless wrapper returns `error`; null is only valid
when it is part of `T`'s representation. Decoding requires a default-initializable
`T`, preserves the destination allocator, and updates an engaged payload in
place (including container append behavior). A valueless destination is rebuilt
with its own allocator; allocation failure leaves it valueless. Decode failures
are terminal and may leave a partial payload.

Keep variant discrimination inside the wrapper:

```cpp
using value = std::indirect<std::variant<int, std::string>>;
// Variant alternatives containing indirect wrappers need an application wire model.
```

For `std::polymorphic<Base>`, define the concrete types and their wire mapping
in an application codec. The compiled [animal codec example](../examples/cxx26_value_wrappers.h)
uses a tagged variant for `dog` and `cat`, reconstructs the concrete type with
the destination allocator, and rejects unknown tags or unsupported dynamic types.
Select `animal_codec` in the factory pack to compose it with other codecs.

CDDL follows `cbor::tags::cddl::cddl_wire_type<T>::type`: the indirect extension
maps to `T`, and the animal example maps to its explicit `animal_wire` variant.
This customization supplies a schema; it does not register runtime conversions.
Aliases must end at a supported schema type without cycles, and customized
variant alternatives require an explicit wire variant. The animal example uses
the default wrapped-group option so each tagged payload is one item.

The extension needs `__cpp_lib_indirect >= 202502L`; the animal example also
needs `__cpp_lib_polymorphic >= 202502L`.
