# SOME/IP POC (header-only)

This worktree contains an experimental SOME/IP + SOME/IP-SD packet and payload codec that reuses the existing reflection engine (`cbor::tags::to_tuple`) but encodes to raw bytes (no CBOR).

## Components

- `include/someip/wire/*`: SOME/IP header, TP header, frame parsing helpers.
- `include/someip/iface/field.h`: Field descriptors + helpers (getter/setter/notifier).
- `include/someip/ser/*`: Reflection-driven payload encoder/decoder with configurable payload endianness.
- `include/someip/types/*`: Wrapper types that carry SOME/IP-specific serialization rules (strings/arrays/padding/union).
- `include/someip/sd/sd.h`: SOME/IP-SD packet builder/parser (entries + options).

## Endianness rules

- SOME/IP headers and SOME/IP-SD meta fields are always big-endian.
- SOME/IP payload "normal" scalars use `someip::ser::config::payload_endian`.
- SOME/IP payload meta-length fields (array/string/union length + union selector) are always big-endian.

## Quick example

```cpp
#include "someip/ser/config.h"
#include "someip/ser/decode.h"
#include "someip/wire/message.h"

struct Payload {
  std::uint16_t a{};
  std::int32_t  b{};
  bool          c{};
};

int main() {
  const someip::ser::config cfg{someip::wire::endian::big};

  someip::wire::header h{};
  h.msg.service_id    = 0x1234;
  h.msg.method_id     = 0x0001;
  h.req.client_id     = 0x0001;
  h.req.session_id    = 0x0002;
  h.interface_version = 1;
  h.msg_type          = someip::wire::message_type::request;

  Payload p{.a = 0x1234, .b = -2, .c = true};

  std::vector<std::byte> frame{};
  someip::wire::encode_message(frame, h, cfg, p);

  auto parsed = someip::wire::try_parse_frame(std::span<const std::byte>(frame.data(), frame.size()));
  Payload out{};
  someip::ser::decode(parsed->payload, cfg, out, 16); // base_offset=16 because payload starts after SOME/IP header
}
```

## Notes / limitations (POC)

- Dynamic arrays (`someip::types::dyn_array`) are limited to scalar element types (length is bytes, so element size must be known).
- SOME/IP "structured arguments with identifier and optional members" (TLV-ish) are not implemented.
