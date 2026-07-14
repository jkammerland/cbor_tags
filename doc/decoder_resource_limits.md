# Decoder Resource Limits

The synchronous typed decoder reads CBOR directly. It does not perform a structural preflight pass or impose a nesting limit. Apply an
application message-size limit before decoding:

```cpp
if (input.size() > max_message_bytes)
    return input_too_large;

return cbor::tags::make_decoder(input)(value);
```

For streaming or resumable parsing, enforce the same policy against the cumulative bytes received.

## Recursive Decode Paths

Typed decoding follows the destination C++ type. Container elements are processed in loops, so a large flat array does not create one
decoder call frame per element. Extra input nesting against a fixed, non-recursive destination instead produces a type mismatch.

Input-controlled stack growth requires a decode path that can invoke itself. The usual trigger is a recursively defined destination type:

```cpp
struct recursive_array {
    std::vector<recursive_array> children;

    template <typename Decoder>
    cbor::tags::expected<void, cbor::tags::status_code> decode(Decoder& decoder) {
        return decoder(children);
    }
};
```

Each child can decode another `recursive_array`, so nested one-element arrays add live C++ call frames. Mutually recursive destination
types, recursive smart-pointer graphs, and custom codecs that explicitly call back into a recursive decode path can do the same.

## Measured Boundary

These are observations from one machine, not portable decoder limits:

- Source revision: `6617765`
- Host: x86-64 Linux with an 8 MiB main-thread stack
- GCC 15.2.1 and Clang 21.1.8 with libstdc++ 15, C++20
- Address randomization disabled for repeatable boundary measurements

The probe decoded the `recursive_array` above from `D` one-element array headers (`0x81`) followed by an empty array (`0x80`). Its
buffer size was therefore `D + 1` bytes.

| Compiler configuration | Maximum successful depth | First failing depth | Buffer at first failure | Approx. stack per level |
|---|---:|---:|---:|---:|
| GCC 15.2.1, `-O0 -g` | 14,975 | 14,976 | 14,977 bytes | 560 bytes |
| GCC 15.2.1, `-O3 -DNDEBUG` | 52,421 | 52,422 | 52,423 bytes | 160 bytes |
| Clang 21.1.8, `-O0 -g` | 13,103 | 13,104 | 13,105 bytes | 640 bytes |
| Clang 21.1.8, `-O3 -DNDEBUG` | 87,369 | 87,370 | 87,371 bytes | 96 bytes |

The smallest observed buffer to exhaust the 8 MiB stack was 13,105 bytes. This boundary varies with the destination type, custom codec,
compiler, optimization level, standard library, caller stack use, and thread stack size.

[`test_decode_stack_floor.cc`](../test/test_decode_stack_floor.cc) checks the same recursive path at depth 1,024 as a regression floor;
it does not assert a portable crash boundary.
