# Encoder And Decoder Options

`cbor_tags` has a small compile-time options API for behavior that should be
resolved by the encoder or decoder type, not checked dynamically for every
value.

The examples assume:

```cpp
using namespace cbor::tags;
```

Options are selected through `Options<...>` marker packs. The default option set
is:

```cpp
using default_options = Options<default_expected, default_wrapping>;
```

## Decoder Options

Use `make_decoder_with_options<Options>(buffer)` when a decoder should use a
non-default policy.

```cpp
auto dec = make_decoder_with_options<strict_integer_decoder_options>(buffer);
```

Extensions still compose with decoder options:

```cpp
auto dec = make_decoder_with_options<strict_integer_decoder_options, my_extension>(buffer);
```

## Strict Integer Decode

By default, decoding CBOR integers into fixed-width native integer types slices
through the target type.

For unsigned targets this follows the normal modulo conversion. For signed
targets the library uses the target implementation's native signed conversion
behavior; on the supported two's-complement toolchains this preserves the low
bits.

Use `strict_integer_decoder_options` when integer representability should be
validated during decode. With strict integer decoding enabled, out-of-range
native integer targets fail instead of slicing.

```cpp
std::uint32_t value{};
auto result = dec(value);
if (!result) {
    auto status = result.error();
}
```

Decode to `integer`, `positive`, or `negative` when the full CBOR integer
domain matters.

### Wire-domain integer values

`negative` and `integer` preserve values that do not fit any native signed
integer. Their `value` member is a magnitude: `negative{1}` represents `-1`,
and `negative{std::numeric_limits<std::uint64_t>::max()}` represents
`-18446744073709551615`. `negative{0}` is the sentinel for the remaining CBOR
value, `-18446744073709551616` (`-2^64`):

```cpp
negative minus_one{1};
negative cbor_min{0};
integer  either_sign = cbor_min;

assert(cbor_min < minus_one);
```

These types support construction, equality, ordering, encoding, and decoding.
They intentionally do not provide arithmetic because the `negative{0}`
sentinel has no native unsigned magnitude and arithmetic could silently wrap.
Convert a non-sentinel value to an application numeric type only after checking
that the destination can represent it. `positive` remains an alias for
`std::uint64_t`.

## Wrapping Groups

`default_wrapping` controls whether reflected aggregates and tuple-like grouped
values are wrapped as CBOR arrays when they contain multiple payload items. It
is part of `default_options`.

```cpp
using no_group_wrapping = Options<default_expected>;
```

The public factories currently use `default_options` for encoders. Advanced
code can instantiate the `encoder<...>` type directly when it needs a custom
encoder option set.

## Custom Option Sets

Option sets must model `IsOptions`:

```cpp
struct my_decoder_options {
    using is_options  = void;
    using return_type = expected<void, status_code>;
    using error_type  = status_code;

    static constexpr bool wrap_groups = true;
};
```

If `strict_integer_decode` is omitted, the decoder uses the default slicing
integer policy.
