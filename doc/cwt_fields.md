# Experimental application-selected CWT fields

Include `<cbor_tags/codec/cwt.h>` and opt into `codec::cwt`. `cwt::field<label>(value)` binds a named object to an integer or text map label; `cwt::fields(...)` forms one complete map. Nested `fields(...)` groups flatten into the same map, and duplicate schema labels are rejected at compile time.

[Example 30](../examples/30_experimental_cwt_fields.cpp) sends an issuer and optional role, then receives only the issuer as a view. Re-encoding that receiver layout includes only its selected fields. These are experimental APIs.

A nonoptional destination is required. An optional destination may be absent; successful decoding clears absent optionals. A present optional decodes its underlying type, so `optional<int>` does not accept CBOR null. Use an explicitly nullable underlying type when the schema permits null.

Unknown values are skipped, but all input labels participate in duplicate detection. Malformed or incomplete maps fail. Decoding is terminal: fields decoded before a failure may remain modified and the reader is not rolled back.

The proxy owns only field descriptors. Keep referenced objects alive while using it; views decoded into them borrow the input buffer. Owning destinations support noncontiguous input. Values use the active codec pack and ordinary ADL customization, so application-defined fields can compose other supported types.
