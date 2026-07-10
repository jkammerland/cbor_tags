# Decoder Resource Limits

The synchronous typed decoder reads CBOR directly. It does not perform a structural preflight pass and does not impose a nesting
limit. Applications that accept untrusted or potentially large messages should bound the input before constructing the decoder.

```cpp
constexpr std::size_t max_message_bytes = /* application policy */;

if (input.size() > max_message_bytes) {
    return input_too_large;
}

auto decoder = cbor::tags::make_decoder(input);
return decoder(value);
```

The complete [`bounded_decode`](../examples/bounded_decode.cpp) example keeps this policy in application code and preserves the CBOR
decoder's existing status for malformed input.

The size operation must be constant time. Do not traverse an unsized range merely to enforce this check. Streaming and resumable
parsers should count bytes as chunks arrive and reject a message when its cumulative byte budget is exceeded.

## When Typed Decoding Can Recurse

The typed decoder does not build a generic recursive CBOR tree. It follows the destination C++ type, and container width is handled by
loops. For a non-recursive destination type, decoder call depth is therefore bounded by the nesting expressed in that type:

```cpp
std::vector<int> flat;                         // one array level
std::vector<std::vector<int>> matrix;          // two array levels
std::vector<std::vector<std::vector<int>>> cube; // three array levels
```

A million integers in `flat` do not create a million nested decoder calls. Likewise, extra array nesting in the input does not make
the decoder recurse past `matrix`; when an `int` is expected and another array header is found, decoding returns a type-mismatch
status.

Input-controlled call depth requires a recursive decode path. The usual case is a recursively defined destination type:

```cpp
struct recursive_array {
    std::vector<recursive_array> children;

    template <typename Decoder>
    cbor::tags::expected<void, cbor::tags::status_code> decode(Decoder &decoder) {
        return decoder(children);
    }
};
```

Each decoded child can decode another `recursive_array`, so a nested one-element array can add one live C++ call frame per input byte.
Recursive smart-pointer trees and mutually recursive types have the same property. A custom codec can also create a recursive call
path explicitly, even if the destination type is not syntactically self-referential. Very deeply but statically nested generated types
can use substantial stack too, but their maximum depth is fixed at compile time rather than controlled by arbitrary input nesting.

This distinction applies to normal typed decoding. Explicit structural operations such as raw-item views and `find_tags` must inspect
arbitrary CBOR structure; they use bounded iterative frame storage and report an error when that operation's depth limit is reached
instead of recursively growing the C++ call stack.

## Local Stack Measurement

The numbers below are observations from one machine, not portable decoder limits.

- Source revision: `6617765`
- Host: x86-64 Linux `7.0.13-100.fc43.x86_64`
- Main-thread stack soft limit: 8,388,608 bytes (`ulimit -s` reported 8192 KiB)
- Memory page size: 4,096 bytes
- GCC: 15.2.1 with libstdc++ 15
- Clang: 21.1.8 with libstdc++ 15
- Language mode: C++20
- Address randomization: disabled for repeatable boundary measurements

The probe materialized this recursive target through the normal class and `std::vector` decoder path:

```cpp
struct recursive_array {
    std::vector<recursive_array> children;

    template <typename Decoder>
    cbor::tags::expected<void, cbor::tags::status_code> decode(Decoder& decoder) {
        ++nodes_entered;
        auto result = decoder(children);
        if (result) {
            ++nodes_completed;
        }
        return result;
    }
};
```

For depth `D`, the input was `D` copies of `0x81` (a one-element array) followed by `0x80` (an empty array). It is well-formed CBOR,
uses the minimum one additional byte per nested level, and has a total size of `D + 1` bytes. Each candidate depth ran in a separate
process. The first failure was `SIGSEGV`; a debugger stopped in the vector allocation path with the stack at its guard region. Repeating
the measurement with 1, 2, 4, and 8 MiB stack limits produced linear depth scaling, confirming stack exhaustion.

| Compiler configuration | Maximum successful depth | First failing depth | Buffer at first failure | Approx. stack per level |
|---|---:|---:|---:|---:|
| GCC 15.2.1, `-O0 -g` | 14,975 | 14,976 | 14,977 bytes | 560 bytes |
| GCC 15.2.1, `-O3 -DNDEBUG` | 52,421 | 52,422 | 52,423 bytes | 160 bytes |
| Clang 21.1.8, `-O0 -g` | 13,103 | 13,104 | 13,105 bytes | 640 bytes |
| Clang 21.1.8, `-O3 -DNDEBUG` | 87,369 | 87,370 | 87,371 bytes | 96 bytes |

The smallest buffer observed to exhaust the 8 MiB stack was therefore 13,105 bytes in the Clang Debug build. The default GCC Debug
build first failed at 14,977 bytes. An 8 KiB input cap remained below every measured failure boundary in this experiment, but it is not
a general safety guarantee.

## Regression Floor

The `test_decode_stack_floor` CTest target decodes and destroys the same recursive vector shape at depth 1,024 in its own process. A
stack overflow therefore fails that CTest entry without terminating the rest of the suite. The floor is intentionally not the measured
crash boundary: it is four times deeper than the abandoned 256-level policy while retaining about 13 times of headroom from the
earliest failure measured above.

The test protects a useful lower bound instead of compiler-specific values. Exact crash depths are unsuitable assertions because
optimization, standard-library implementation, sanitizer instrumentation, executable stack settings, and operating-system defaults
can all move them. A future iterative or resumable decoder may exceed the documented measurements without invalidating the test.

## Why There Is No Universal Minimum

Stack use depends on the decoded C++ type and codec call path. A smaller custom codec probe consumed about 128 bytes per level in GCC
Debug and reached depth 65,523 before failing, while the recursive vector path used about 560 bytes per level. A codec can also declare
large local objects or recurse without consuming input, so no library-wide smallest crashing buffer exists.

Caller stack use and worker-thread stack sizes can lower the boundary. Destruction of a deeply recursive materialized object can also
exhaust the stack independently of decoding; the probe deliberately exited without destroying the completed object graph so that the
table isolates decoder stack use. Heap exhaustion or application-specific validation may occur first for other target shapes.

The practical contract is therefore:

1. Typed decoding does not scan ahead.
2. The application chooses a message-size limit appropriate for its stack, target types, and deployment.
3. Raw-item views, segmented-item traversal, and lazy tag search remain explicit structural operations because their requested result
   requires discovering item boundaries or tags. They are not invoked by normal typed decoding.
4. Future resumable parsing should preserve parser state and enforce a consumed-byte budget rather than add a preflight pass.
