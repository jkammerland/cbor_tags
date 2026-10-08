#include "../examples/bounded_samples.h"

#include <doctest/doctest.h>

using namespace cbor::tags;

TEST_SUITE("roundtrip/bounded_codec_example") {
    TEST_CASE("application samples codec composes with the library namespace and runtime bounds") {
        const app::samples     input{{1, 2}};
        app::samples           output;
        std::vector<std::byte> bytes;

        REQUIRE(make_encoder<app::codec::samples>(bytes)(as_bounded_size(input, 1, 2)));
        REQUIRE(make_decoder<app::codec::samples>(bytes)(as_bounded_size(output, 1, 2)));
        CHECK(output.values == input.values);

        bytes.clear();
        const auto rejected = make_encoder<app::codec::samples>(bytes)(as_bounded_size(input, 0, 1));
        REQUIRE_FALSE(rejected);
        CHECK(rejected.error() == status_code::size_limit_exceeded);
        CHECK(bytes.empty());
    }

    TEST_CASE("application samples codec rejects an out of bounds incoming array") {
        const app::samples     input{{1, 2, 3}};
        app::samples           output{{9}};
        std::vector<std::byte> bytes;

        REQUIRE(make_encoder<app::codec::samples>(bytes)(as_bounded_size(input, 1, 3)));
        const auto rejected = make_decoder<app::codec::samples>(bytes)(as_bounded_size(output, 1, 2));
        REQUIRE_FALSE(rejected);
        CHECK(rejected.error() == status_code::size_limit_exceeded);
        CHECK(output.values == std::vector<int>{9});
    }

    TEST_CASE("application samples codec supports static bounds and empty runtime bounds") {
        app::samples           input{{4}};
        app::samples           output;
        std::vector<std::byte> bytes;

        REQUIRE(make_encoder<app::codec::samples>(bytes)(as_bounded_size<1, 1>(input)));
        REQUIRE(make_decoder<app::codec::samples>(bytes)(as_bounded_size<1, 1>(output)));
        CHECK(output.values == input.values);

        input.values.clear();
        output.values.clear();
        bytes.clear();
        REQUIRE(make_encoder<app::codec::samples>(bytes)(as_bounded_size(input, 0, 0)));
        REQUIRE(make_decoder<app::codec::samples>(bytes)(as_bounded_size(output, 0, 0)));
        CHECK(output.values.empty());
    }
}
