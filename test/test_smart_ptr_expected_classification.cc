#include <version>

#if __has_include(<expected>) && defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L

#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/detail/cbor_pointer_traits.h"
#include "cbor_tags/extensions/std_expected.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

int main() {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::std_expected;

    using result_type = std::expected<std::uint64_t, std::string>;
    static_assert(!IsOptional<result_type>);
    static_assert(!cbor::tags::detail::has_known_null_wire_v<result_type>);

    const result_type sent{std::unexpected<std::string>{"bad"}};

    std::vector<std::byte> bytes;
    auto                   enc = make_encoder<std_expected_codec>(bytes);
    if (!enc(sent)) {
        return 1;
    }

    result_type decoded;
    auto        dec = make_decoder<std_expected_codec>(bytes);
    if (!dec(decoded) || decoded.has_value() || decoded.error() != "bad") {
        return 2;
    }
    return 0;
}

#else

int main() { return 0; }

#endif
