#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/codec/typed_array.h>
#include <cstddef>
#include <cstdint>
#include <list>
#include <ranges>
#include <span>
#include <vector>

namespace typed_array_destination_test {

struct deep_const_view : std::ranges::view_interface<deep_const_view> {
    std::list<std::int32_t> *values;

    explicit deep_const_view(std::list<std::int32_t> &sequence) noexcept : values(&sequence) {}

    auto begin() noexcept { return values->begin(); }
    auto end() noexcept { return values->end(); }
    auto begin() const noexcept { return values->cbegin(); }
    auto end() const noexcept { return values->cend(); }
};

} // namespace typed_array_destination_test

template <> inline constexpr bool std::ranges::enable_borrowed_range<typed_array_destination_test::deep_const_view> = true;

int main() {
    using namespace cbor::tags;
    using namespace cbor::tags::rfc8746;

    const std::vector<std::byte>    input;
    const std::vector<std::int32_t> values{1, 2, 3};
    auto                            dec = make_decoder<codec::typed_array>(input);

#if defined(CBOR_TYPED_DEST_CONST_VECTOR)
    return dec(as_typed_array(values)).has_value();
#elif defined(CBOR_TYPED_DEST_CONST_SPAN)
    return dec(as_typed_array(std::span<const std::int32_t>{values})).has_value();
#elif defined(CBOR_TYPED_DEST_CONST_VECTOR_BE)
    return dec(as_typed_array_be(values)).has_value();
#elif defined(CBOR_TYPED_DEST_CONST_SPAN_BE)
    return dec(as_typed_array_be(std::span<const std::int32_t>{values})).has_value();
#elif defined(CBOR_TYPED_DEST_CONST_LIST)
    const std::list<std::int32_t> sequence{1, 2, 3};
    return dec(as_typed_array(sequence)).has_value();
#elif defined(CBOR_TYPED_DEST_CONST_LIST_BE)
    const std::list<std::int32_t> sequence{1, 2, 3};
    return dec(as_typed_array_be(sequence)).has_value();
#elif defined(CBOR_TYPED_DEST_CONST_VIEW)
    std::list<std::int32_t>                             sequence{1, 2, 3};
    const typed_array_destination_test::deep_const_view view{sequence};
    return dec(as_typed_array(view)).has_value();
#endif
}
