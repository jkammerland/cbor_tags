# CBOR Conformance

`cbor_tags` uses the CBOR data model and wire format defined by
[RFC 8949](https://www.rfc-editor.org/rfc/rfc8949.html), but the core typed
decoder is not a strict RFC 8949 well-formedness validator. Its primary contract
is to map the next requested CBOR item or items into C++ values.

This document records the library's interoperability profile, including
intentional permissive behavior. It is not a certification statement.

## API Boundaries

The public APIs have different purposes and therefore different validation
boundaries:

- The core encoder and typed decoder map between C++ types and CBOR items. The
  requested destination type determines which wire forms are accepted.
- Raw item views, segmented-item validation, and lazy tag discovery perform a
  structural scan so that they can identify complete item boundaries. They use
  the library's permissive CBOR syntax profile; they are not strict RFC 8949
  validators.
- The visualization extension is a diagnostic parser. It performs checks needed
  to render an item and can be stricter than the core decoder.
- Extension codecs may implement additional RFCs or application profiles. Their
  conformance and opt-in requirements are documented separately.

Core decoding is a one-shot, terminal operation. It parses the requested item or
sequence and does not require the supplied buffer to end afterward. Consequently,
a successful typed decode does not prove that an entire buffer contains exactly
one conforming CBOR item. See the
[decoder contract](decoder_resource_limits.md#decoder-contract) for input,
failure, and destination-state guarantees.

## Intentional Permissive Behavior

### Extended simple values

The `simple` type represents the complete `std::uint8_t` value space. The core
decoder intentionally accepts the two-byte form `f8 00` through `f8 1f`, and the
encoder emits `f8 18` through `f8 1f` for `simple{24}` through `simple{31}`.
Structural item scanners accept these forms as well.

RFC 8949 Section 3.3 classifies an `f8` initial byte followed by a value below 32
as not well-formed. This library deliberately permits those encodings to preserve
its extended-simple-value model and existing wire compatibility. Applications
that exchange data with strict RFC 8949 implementations must not send
`simple{24}` through `simple{31}` and must not use the core decoder or structural
scanner as a strict well-formedness gate.

The visualization extension currently rejects `f8 18` through `f8 1f`. That
diagnostic boundary is stricter than core encoding, decoding, and structural
scanning.

### Text strings and UTF-8

The core decoder preserves the payload bytes of a CBOR text string and does not
validate that they form UTF-8. This keeps byte handling separate from application
text policy. The visualization extension can validate text with
`CDDLOptions::check_tstr_utf8`.

Applications that require RFC-valid text strings must validate UTF-8 at their
admission boundary or use a codec that enforces that policy.

### Integer conversion

By default, decoding an integer into a narrower C++ destination uses the
documented slicing behavior. This is a C++ value-mapping policy rather than a
different CBOR wire encoding. Use `strict_integer_decoder_options` when the
decoded value must be representable by the destination type. See
[Encoder And Decoder Options](options.md#strict-integer-decode).

### Maps and duplicate keys

Map decoding follows the insertion and duplicate-key behavior of the destination
container. The core decoder does not impose an additional, universal duplicate-key
policy. Applications that require unique keys independently of the destination
type must enforce that profile explicitly.

## Preferred And Deterministic Encoding

The default encoder generally selects compact argument widths for its supported
C++ mappings, but the library does not make a blanket guarantee of RFC 8949
preferred serialization or deterministic encoding. For example, floating-point
width follows the source C++ type, and map order follows the source range or
container.

Protocols that require deterministic CBOR must define and test their own profile,
including key ordering, floating-point representation, duplicate-key handling,
UTF-8 policy, and the permitted simple values.

## Other Standards And Extensions

- CDDL generation follows [RFC 8610](https://www.rfc-editor.org/rfc/rfc8610.html)
  within the documented feature coverage. Generated CDDL can be stricter than
  the default decoder policy; see [CDDL Standard Coverage](cddl_standard_coverage.md).
- RFC 8746 typed arrays are provided by an opt-in codec; see
  [RFC 8746 Typed Arrays](rfc8746_typed_arrays.md).
- Experimental raw views, range wrappers, lazy tag scanning, and segmented output
  are described in [Experimental Range And Segment APIs](experimental_ranges.md).

Support for a CBOR feature does not imply that every target C++ type accepts it.
The encoder/decoder overload selected by the application's type, options, and
extension codecs defines the effective profile.
