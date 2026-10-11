# Experimental countersignatures

Include `cbor_tags/cwt/countersignatures.h` for [RFC 9338](https://www.rfc-editor.org/rfc/rfc9338.html) full and abbreviated countersignatures. The API is experimental; OpenSSL remains optional, and custom providers use the existing segmented signing contract.

`countersign<Backend>(key, target, counter, aad)` returns an owning full counter. The output-span overload returns bytes written without attaching them. `countersign0` produces an abbreviated signature; its algorithm and key come from the application. Full counters use their own protected algorithm, independently of the original signer's algorithm. `verify_countersignature` and `verify_countersignature0` verify only the selected counter. Always verify the original signature and every additional layer required by the application.

A Sign1 target binds its original protected bytes, embedded or explicitly supplied detached payload, and original signature. An individual COSE_Sign signer or full counter binds its own protected bytes and signature. Its counter does not directly bind the parent message's payload; the parent's signature must also be verified. Whole COSE_Sign targets are deliberately unsupported.

`make_countersign_tbs` and `make_countersign0_tbs` return ordered segments: owned CBOR framing followed by views into the original protected headers, AAD, payload, and signature. Keep these named models and buffers alive and unchanged until the provider finishes. The provider consumes the segments once. Counter AAD is independent of the original signer's AAD and is not stored in the message.

Select `counter_headers<>` as a header model's `Extra` to carry full counters under label 11, abbreviated bytes under label 12, and received legacy full counters under label 7. `counter_headers_view` borrows string bytes while owning collection descriptors. Full singleton/list encodings normalize into `counter_collection::signatures`; empty collections are rejected. Recursive chains exist only in models that explicitly select these headers. `counter_headers<Borrowed, Extra>` adds application fields through `cwt_fields(Extra&)`; `Extra` must not recursively contain the same counter headers.

The typed-header overloads accept a parsed-header destination and additional codec template arguments. Verification also accepts a critical-parameter processor and publishes parsed headers only after cryptographic verification and critical processing succeed. These outputs may borrow the counter's protected bytes, so pass a named counter. Unsupported critical parameters fail closed in the simple overloads.

Use `as_countersignature` for standalone tag 19. Encoding a legacy full collection fails. `verify_legacy_countersignature` explicitly implements the older input construction: for Sign1 it does not bind the original signature. There is no implicit fallback, legacy generation, or legacy abbreviated operation. Existing wire bytes are never relabeled or reconstructed for verification.

Examples [33](../examples/33_experimental_cwt_countersignature.cpp), [34](../examples/34_experimental_cwt_abbreviated_counter.cpp), and [35](../examples/35_experimental_cwt_counter_chain.cpp) introduce full counters, caller storage with abbreviated counters, and borrowed counter chains respectively.
