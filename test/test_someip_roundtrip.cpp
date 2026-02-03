#include <doctest/doctest.h>

#include "someip/ser/decode.h"
#include "someip/wire/message.h"

#include <cstdint>
#include <vector>

namespace {
struct Payload {
    std::uint16_t a{};
    std::int32_t  b{};
    bool          c{};
};
} // namespace

TEST_CASE("someip: end-to-end encode_message -> try_parse_frame -> decode payload") {
    for (auto endian : {someip::wire::endian::big, someip::wire::endian::little}) {
        const someip::ser::config cfg{endian};
        const Payload             in{.a = 0x1234, .b = -2, .c = true};

        someip::wire::header h{};
        h.msg.service_id       = 0x1234;
        h.msg.method_id        = 0x0001;
        h.req.client_id        = 0x0001;
        h.req.session_id       = 0x0002;
        h.protocol_version     = 1;
        h.interface_version    = 1;
        h.msg_type             = someip::wire::message_type::request;
        h.return_code          = 0;

        std::vector<std::byte> frame{};
        REQUIRE(someip::wire::encode_message(frame, h, cfg, in).has_value());

        auto parsed = someip::wire::try_parse_frame(std::span<const std::byte>(frame.data(), frame.size()));
        REQUIRE(parsed.has_value());
        CHECK(parsed->consumed == frame.size());
        CHECK(parsed->payload.size() == 7);

        Payload out{};
        REQUIRE(someip::ser::decode(parsed->payload, cfg, out, 16).has_value());
        CHECK(out.a == in.a);
        CHECK(out.b == in.b);
        CHECK(out.c == in.c);
    }
}
