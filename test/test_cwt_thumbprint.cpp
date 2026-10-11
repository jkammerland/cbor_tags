#include <cbor_tags/cwt/thumbprint.h>
#include <doctest/doctest.h>

using namespace cbor::tags;

namespace {
// A deliberately failing provider proves that errors reach the caller unchanged.
struct unavailable_hash {
    template <std::ranges::input_range Parts> static expected<void, status_code> digest(Parts &&, std::span<std::byte, 32>) {
        return cbor::tags::unexpected<status_code>{status_code::incomplete};
    }
};

template <typename T>
concept TemporaryThumbprint = requires { cwt::make_key_thumbprint_input(T{}); };
template <typename T>
concept TemporaryKeyView = requires { cwt::as_view(T{}); };
static_assert(!TemporaryThumbprint<cwt::ec2_public_key>);
static_assert(!TemporaryThumbprint<cwt::ec2_public_key_view>);
static_assert(!TemporaryKeyView<cwt::ec2_public_key>);
static_assert(!TemporaryKeyView<cwt::okp_public_key>);
static_assert(!TemporaryKeyView<cwt::rsa_public_key>);
} // namespace

TEST_SUITE_BEGIN("roundtrip/cwt/keys");

TEST_CASE("cwt thumbprint propagates custom provider errors") {
    cwt::okp_public_key key;
    key.x.assign(32, std::byte{1});
    auto result = cwt::key_thumbprint<unavailable_hash>(key);
    REQUIRE_FALSE(result);
    CHECK(result.error() == status_code::incomplete);
}

TEST_SUITE_END();
