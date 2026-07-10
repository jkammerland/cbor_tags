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

The size operation must be constant time. Do not traverse an unsized range merely to enforce this check. Streaming and resumable
parsers should count bytes as chunks arrive and reject a message when its cumulative byte budget is exceeded.

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
