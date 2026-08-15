# Security Policy

## Ownership Boundary

`cbor_tags` owns parsing the requested CBOR segment from the stable input range
admitted by the caller. The library is responsible for behavior introduced by
its implementation while supported inputs and customizations honor their
documented contracts.

The application owns:

- input lifetime, transport framing, admission limits, and the stable admitted
  range supplied to a one-shot decoder call;
- the selected C++ destination schema and the data model it authorizes;
- custom codecs and member or free encoder, decoder, and transcoder functions;
- allocator policy and application-specific resource limits; and
- truthful range, iterator, extent, allocator, and lifetime semantics.

A statically typed recursive destination explicitly authorizes recursive
decoding. If input exercises recursion implemented by that schema, its depth and
resource policy remain application-owned. Input controlling the depth does not
make the recursion library-owned.

## Finding Acceptance

A security report must identify an unsafe operation introduced by the library
and the documented guarantee it violates. A valid customization can expose a
library defect, but the defect must exist beyond the natural behavior of the
customization.

The following are not library vulnerabilities by themselves:

- recursion explicitly implemented by an application schema or custom codec;
- a customization that reports success without consuming or producing its
  required CBOR item;
- allocation, blocking, exceptions, termination, or other failure behavior
  directly implemented by user code;
- dishonest range sizes, invalid iterators, dangling storage, or violated
  allocator and lifetime contracts; and
- resource exhaustion that is already assigned to caller-owned framing,
  admission, or application limits.

Before submitting a report, provide a minimal reproducer and answer:

1. Which exact library-owned operation introduces the unsafe behavior?
2. Does it remain when user code that directly implements the behavior is
   removed?
3. Which documented library guarantee is violated?
4. Is the behavior inherent to the application's statically selected schema?
5. Does the caller-owned admission or resource contract already cover it?

Reports without a concrete library-owned operation and violated guarantee are
treated as application behavior or hardening suggestions rather than security
vulnerabilities.

For the one-shot decoder's detailed ownership, input, failure, and destination
semantics, see the
[decoder contract](doc/decoder_resource_limits.md#decoder-contract).
