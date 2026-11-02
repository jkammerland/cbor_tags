#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cstddef>
#include <initializer_list>
#include <list>
#include <vector>

using namespace cbor::tags;

struct unsized_output {
    using value_type     = std::byte;
    using size_type      = std::size_t;
    using iterator       = std::list<std::byte>::iterator;
    using const_iterator = std::list<std::byte>::const_iterator;

    std::list<std::byte> bytes;

    void                              push_back(std::byte value) { bytes.push_back(value); }
    template <typename Iterator> auto insert(const_iterator position, Iterator first, Iterator last) {
        return bytes.insert(position, first, last);
    }
    auto insert(const_iterator position, std::initializer_list<std::byte> values) { return bytes.insert(position, values); }
    auto begin() noexcept { return bytes.begin(); }
    auto end() noexcept { return bytes.end(); }
    auto begin() const noexcept { return bytes.begin(); }
    auto end() const noexcept { return bytes.end(); }
};

struct custom_conflicting_options {
    using is_options                            = void;
    using return_type                           = expected<std::size_t, status_code>;
    static constexpr bool wrap_groups           = true;
    static constexpr bool return_consumed_bytes = true;
};

int main() {
#if defined(CBOR_BYTE_COUNT_UNSIZED_OUTPUT)
    using count_options = Options<Option<expected<std::size_t, status_code>>, default_wrapping>;
    unsized_output output;
    return make_encoder_with_options<count_options>(output)(1U).has_value();
#else
    const std::vector<std::byte> input{std::byte{0x01}};
    unsigned                     value{};
#if defined(CBOR_BYTE_COUNT_CUSTOM_VIEW_CONFLICT)
    return make_decoder_with_options<custom_conflicting_options>(input)(value).has_value();
#else
    using conflicting_options = Options<Option<expected<std::size_t, status_code>>, default_wrapping, return_consumed_bytes>;
    return make_decoder_with_options<conflicting_options>(input)(value).has_value();
#endif
#endif
}
