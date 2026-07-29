#include "test_util.h"

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/extensions/cbor_visualization.h>
#include <cbor_tags/extensions/smart_ptr.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace cbor::tags;
using namespace cbor::tags::ext::smart_ptr;

namespace smart_ptr_test {

class encode_table {
  private:
    struct entry {
        shared_ptr_encode_key  key{};
        shared_ptr_entry_state state{shared_ptr_entry_state::encoding};
    };

  public:
    explicit encode_table(std::size_t limit = std::numeric_limits<std::size_t>::max()) : limit_(limit) {}

    void reserve(std::size_t count) { entries_.reserve(count); }
    void reset() { entries_.clear(); }

    [[nodiscard]] expected<shared_ptr_observation, status_code> observe(const shared_ptr_encode_key &key) {
        for (std::size_t index = 0; index < entries_.size(); ++index) {
            auto &entry = entries_[index];
            if (entry.key != key) {
                continue;
            }
            if (entry.state != shared_ptr_entry_state::complete) {
                return cbor::tags::unexpected<status_code>{status_code::error};
            }
            return shared_ptr_observation{shared_ptr_observation_kind::reference, static_cast<std::uint64_t>(index)};
        }

        if (entries_.size() >= limit_) {
            return cbor::tags::unexpected<status_code>{status_code::size_limit_exceeded};
        }

        const auto index = entries_.size();
        entries_.push_back(entry{key, shared_ptr_entry_state::encoding});
        return shared_ptr_observation{shared_ptr_observation_kind::first, static_cast<std::uint64_t>(index)};
    }

    void mark_complete(std::uint64_t index) { entries_.at(static_cast<std::size_t>(index)).state = shared_ptr_entry_state::complete; }

  private:
    std::vector<entry> entries_{};
    std::size_t        limit_{};
};

class decode_table {
  public:
    explicit decode_table(std::size_t limit = std::numeric_limits<std::size_t>::max()) : limit_(limit) {}

    void reserve(std::size_t count) { entries_.reserve(count); }
    void reset() { entries_.clear(); }

    [[nodiscard]] expected<std::uint64_t, status_code> insert(const shared_ptr_decode_entry &entry) {
        if (entries_.size() >= limit_) {
            return cbor::tags::unexpected<status_code>{status_code::size_limit_exceeded};
        }
        const auto index = entries_.size();
        entries_.push_back(entry);
        return static_cast<std::uint64_t>(index);
    }

    [[nodiscard]] expected<shared_ptr_decode_entry, status_code> resolve(std::uint64_t index) {
        if (index >= entries_.size()) {
            return cbor::tags::unexpected<status_code>{status_code::error};
        }
        return entries_[static_cast<std::size_t>(index)];
    }

    void mark_complete(std::uint64_t index) { entries_.at(static_cast<std::size_t>(index)).state = shared_ptr_entry_state::complete; }

  private:
    std::vector<shared_ptr_decode_entry> entries_{};
    std::size_t                          limit_{};
};

static_assert(SharedPtrEncodeScope<encode_table>);
static_assert(SharedPtrDecodeScope<decode_table>);

struct partial_record {
    std::uint64_t first{};
    std::string   second;
};

struct single_field_record {
    std::uint64_t value{};
};

struct tagged_record {
    static_tag<42> cbor_tag;
    std::uint64_t  value{};
};

struct node {
    std::uint64_t         value{};
    std::shared_ptr<node> next;
};

template <typename T> class shared_handle {
  public:
    using element_type = T;

    shared_handle() = default;
    explicit shared_handle(std::shared_ptr<T> pointer) : pointer_(std::move(pointer)) {}

    [[nodiscard]] T *get() const noexcept { return pointer_.get(); }
    [[nodiscard]] T &operator*() const noexcept { return *pointer_; }
    explicit         operator bool() const noexcept { return static_cast<bool>(pointer_); }

    void reset() noexcept { pointer_.reset(); }
    void reset(T *raw) { pointer_.reset(raw); }

  private:
    std::shared_ptr<T> pointer_;
};

struct counting_deleter {
    std::size_t *calls{};

    void operator()(std::uint64_t *value) const noexcept {
        ++*calls;
        delete value;
    }
};

static_assert(IsSharedPointer<shared_handle<std::uint64_t>>);
static_assert(IsSharedPointer<std::shared_ptr<std::uint64_t>>);
static_assert(!IsSharedPointer<std::shared_ptr<std::uint64_t[]>>);
static_assert(IsUniquePointer<std::unique_ptr<std::uint64_t>>);
static_assert(IsUniquePointer<std::unique_ptr<std::uint64_t, counting_deleter>>);
static_assert(!IsUniquePointer<std::unique_ptr<std::uint64_t[]>>);
static_assert(IsSmartPointer<shared_handle<std::uint64_t>>);

template <typename T> std::vector<std::byte> encode_unique(const std::unique_ptr<T> &value) {
    std::vector<std::byte> bytes;
    auto                   enc = make_encoder<unique_ptr_codec>(bytes);
    REQUIRE(enc(value));
    return bytes;
}

} // namespace smart_ptr_test

TEST_CASE("unique_ptr codec uses native null or the pointee value") {
    const std::unique_ptr<std::uint64_t> empty;
    CHECK_EQ(to_hex(smart_ptr_test::encode_unique(empty)), "f6");

    const auto value = std::make_unique<std::uint64_t>(42U);
    const auto bytes = smart_ptr_test::encode_unique(value);
    CHECK_EQ(to_hex(bytes), "182a");

    std::unique_ptr<std::uint64_t> decoded;
    auto                           dec = make_decoder<unique_ptr_codec>(bytes);
    REQUIRE(dec(decoded));
    REQUIRE(decoded);
    CHECK_EQ(*decoded, 42U);
}

TEST_CASE("unique_ptr wire interoperates with optional values") {
    const auto value = std::make_unique<std::uint64_t>(7U);
    const auto bytes = smart_ptr_test::encode_unique(value);

    std::optional<std::uint64_t> optional;
    auto                         optional_dec = make_decoder(bytes);
    REQUIRE(optional_dec(optional));
    REQUIRE(optional);
    CHECK_EQ(*optional, 7U);

    std::vector<std::byte> optional_bytes;
    auto                   optional_enc = make_encoder(optional_bytes);
    REQUIRE(optional_enc(std::optional<std::uint64_t>{9U}));

    std::unique_ptr<std::uint64_t> pointer;
    auto                           pointer_dec = make_decoder<unique_ptr_codec>(optional_bytes);
    REQUIRE(pointer_dec(pointer));
    REQUIRE(pointer);
    CHECK_EQ(*pointer, 9U);
}

TEST_CASE("unique_ptr decode keeps terminal partial pointee state") {
    const auto value = std::make_unique<smart_ptr_test::partial_record>(smart_ptr_test::partial_record{.first = 7U, .second = "Ada"});
    auto       bytes = smart_ptr_test::encode_unique(value);
    REQUIRE(bytes.size() > 1U);
    bytes.pop_back();

    std::unique_ptr<smart_ptr_test::partial_record> decoded;
    auto                                            dec    = make_decoder<unique_ptr_codec>(bytes);
    const auto                                      result = dec(decoded);

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::incomplete);
    REQUIRE(decoded);
    CHECK_EQ(decoded->first, 7U);
}

TEST_CASE("unique pointer concept accepts a stateful custom deleter") {
    const auto bytes = to_bytes("182a");

    std::size_t delete_calls{};
    {
        std::unique_ptr<std::uint64_t, smart_ptr_test::counting_deleter> decoded{new std::uint64_t{1U},
                                                                                 smart_ptr_test::counting_deleter{&delete_calls}};
        auto                                                             dec = make_decoder<unique_ptr_codec>(bytes);
        REQUIRE(dec(decoded));
        REQUIRE(decoded);
        CHECK_EQ(*decoded, 42U);
        CHECK_EQ(delete_calls, 1U);
    }
    CHECK_EQ(delete_calls, 2U);
}

TEST_CASE("shared pointer concept accepts a user-defined pointer type") {
    auto                                         owner = std::make_shared<std::uint64_t>(42U);
    smart_ptr_test::shared_handle<std::uint64_t> first{owner};
    const auto                                   second = first;

    std::vector<std::byte> bytes;
    auto                   enc = make_encoder<shared_ptr_codec>(bytes);
    REQUIRE(enc(first, second));
    CHECK_EQ(to_hex(bytes), "d81c182ad81d00");

    smart_ptr_test::shared_handle<std::uint64_t> decoded_first;
    smart_ptr_test::shared_handle<std::uint64_t> decoded_second;
    auto                                         dec = make_decoder<shared_ptr_codec>(bytes);
    REQUIRE(dec(decoded_first, decoded_second));
    REQUIRE(decoded_first);
    REQUIRE(decoded_second);
    CHECK_EQ(*decoded_first, 42U);
    CHECK(decoded_first.get() == decoded_second.get());
}

TEST_CASE("shared pointer pointees may be one-field and tagged aggregates") {
    auto one_field = std::make_shared<smart_ptr_test::single_field_record>(smart_ptr_test::single_field_record{.value = 7U});
    auto tagged    = std::make_shared<smart_ptr_test::tagged_record>(smart_ptr_test::tagged_record{.cbor_tag = {}, .value = 9U});

    std::vector<std::byte> bytes;
    auto                   enc = make_encoder<shared_ptr_codec>(bytes);
    REQUIRE(enc(one_field, tagged));
    CHECK_EQ(to_hex(bytes), "d81c07d81cd82a09");

    std::shared_ptr<smart_ptr_test::single_field_record> decoded_one_field;
    std::shared_ptr<smart_ptr_test::tagged_record>       decoded_tagged;
    auto                                                 dec = make_decoder<shared_ptr_codec>(bytes);
    REQUIRE(dec(decoded_one_field, decoded_tagged));
    REQUIRE(decoded_one_field);
    REQUIRE(decoded_tagged);
    CHECK_EQ(decoded_one_field->value, 7U);
    CHECK_EQ(decoded_tagged->value, 9U);
}

TEST_CASE("shared pointer identity includes the exact pointer type") {
    auto                                         owner = std::make_shared<std::uint64_t>(7U);
    smart_ptr_test::shared_handle<std::uint64_t> custom{owner};

    std::vector<std::byte> bytes;
    auto                   enc = make_encoder<shared_ptr_codec>(bytes);
    REQUIRE(enc(owner, custom));
    CHECK_EQ(to_hex(bytes), "d81c07d81c07");
}

TEST_CASE("scoped shared pointer wrapper copies lvalues and moves rvalues") {
    auto pointer = std::make_shared<std::uint64_t>(9U);
    auto copied  = as_scoped_shared_ptr(pointer);
    REQUIRE(pointer);

    auto moved = as_scoped_shared_ptr(std::move(pointer));
    CHECK_FALSE(pointer);

    std::vector<std::byte> bytes;
    auto                   enc = make_encoder<shared_ptr_codec>(bytes);
    REQUIRE(enc(copied, moved));
    CHECK_EQ(to_hex(bytes), "d81c09d81d00");

    scoped_shared_ptr<std::shared_ptr<std::uint64_t>> decoded_first;
    scoped_shared_ptr<std::shared_ptr<std::uint64_t>> decoded_second;
    auto                                              dec = make_decoder<shared_ptr_codec>(bytes);
    REQUIRE(dec(decoded_first, decoded_second));
    CHECK(decoded_first.value() == decoded_second.value());
}

TEST_CASE("shared_ptr codec uses IANA reference tags") {
    const auto                                        value = std::make_shared<std::uint64_t>(42U);
    const std::vector<std::shared_ptr<std::uint64_t>> sent{value, value};

    std::vector<std::byte> bytes;
    auto                   enc = make_encoder<shared_ptr_codec>(bytes);
    REQUIRE(enc(sent));
    CHECK_EQ(to_hex(bytes), "82d81c182ad81d00");

    std::vector<std::shared_ptr<std::uint64_t>> decoded;
    auto                                        dec = make_decoder<shared_ptr_codec>(bytes);
    REQUIRE(dec(decoded));
    REQUIRE(decoded.size() == 2U);
    REQUIRE(decoded[0]);
    REQUIRE(decoded[1]);
    CHECK_EQ(*decoded[0], 42U);
    CHECK(decoded[0] == decoded[1]);
}

TEST_CASE("shared_ptr null uses native CBOR null") {
    const std::shared_ptr<std::uint64_t> value;

    std::vector<std::byte> bytes;
    auto                   enc = make_encoder<shared_ptr_codec>(bytes);
    REQUIRE(enc(value));
    CHECK_EQ(to_hex(bytes), "f6");

    auto decoded = std::make_shared<std::uint64_t>(1U);
    auto dec     = make_decoder<shared_ptr_codec>(bytes);
    REQUIRE(dec(decoded));
    CHECK_FALSE(decoded);
}

TEST_CASE("default shared_ptr scope persists until explicitly reset") {
    auto p = std::make_shared<std::uint64_t>(1U);

    std::vector<std::byte> bytes;
    auto                   enc = make_encoder<shared_ptr_codec>(bytes);
    REQUIRE(enc(p));
    *p = 2U;
    REQUIRE(enc(p));
    CHECK_EQ(to_hex(bytes), "d81c01d81d00");

    enc.reset_shared_ptr_scope();
    REQUIRE(enc(p));
    CHECK_EQ(to_hex(bytes), "d81c01d81d00d81c02");

    std::shared_ptr<std::uint64_t> first;
    std::shared_ptr<std::uint64_t> second;
    auto                           dec = make_decoder<shared_ptr_codec>(bytes);
    REQUIRE(dec(first));
    REQUIRE(dec(second));
    REQUIRE(first);
    REQUIRE(second);
    CHECK_EQ(*first, 1U);
    CHECK_EQ(*second, 1U);
    CHECK(first == second);

    dec.reset_shared_ptr_scope();
    REQUIRE(dec(second));
    REQUIRE(second);
    CHECK_EQ(*second, 2U);
    CHECK(first != second);
}

TEST_CASE("shared_ptr use needs no root wrapper") {
    auto value = std::make_shared<std::uint64_t>(1U);

    std::vector<std::byte> bytes;
    auto                   enc = make_encoder<shared_ptr_codec>(bytes);
    REQUIRE(enc(value));
    CHECK_EQ(to_hex(bytes), "d81c01");

    std::shared_ptr<std::uint64_t> decoded;
    auto                           dec = make_decoder<shared_ptr_codec>(bytes);
    REQUIRE(dec(decoded));
    REQUIRE(decoded);
    CHECK_EQ(*decoded, 1U);
}

TEST_CASE("shared_ptr codec rejects cycles without rolling back the destination") {
    auto value   = std::make_shared<smart_ptr_test::node>();
    value->value = 7U;
    value->next  = value;

    std::vector<std::byte> bytes;
    auto                   enc    = make_encoder<shared_ptr_codec>(bytes);
    const auto             result = enc(value);
    value->next.reset();
    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::error);
    CHECK_FALSE(bytes.empty());

    const auto                            cycle_bytes = to_bytes("d81c8207d81d00");
    auto                                  dec         = make_decoder<shared_ptr_codec>(cycle_bytes);
    std::shared_ptr<smart_ptr_test::node> decoded;
    const auto                            decode_result = dec(decoded);
    REQUIRE_FALSE(decode_result);
    CHECK_EQ(decode_result.error(), status_code::error);
    REQUIRE(decoded);
    CHECK_EQ(decoded->value, 7U);
    CHECK_FALSE(decoded->next);
}

TEST_CASE("shared_ptr identity uses the exact pointer type and target address") {
    struct pair {
        std::uint64_t first{};
        std::uint64_t second{};
    };

    auto                                              owner  = std::make_shared<pair>(pair{1U, 2U});
    auto                                              first  = std::shared_ptr<std::uint64_t>{owner, &owner->first};
    auto                                              second = std::shared_ptr<std::uint64_t>{owner, &owner->second};
    const std::vector<std::shared_ptr<std::uint64_t>> values{first, second};

    std::vector<std::byte> bytes;
    auto                   enc = make_encoder<shared_ptr_codec>(bytes);
    REQUIRE(enc(values));
    CHECK_EQ(to_hex(bytes), "82d81c01d81c02");
}

TEST_CASE("same typed address collapses even with different control blocks") {
    std::uint64_t                                     raw    = 7U;
    const auto                                        noop   = [](std::uint64_t *) {};
    auto                                              first  = std::shared_ptr<std::uint64_t>{&raw, noop};
    auto                                              second = std::shared_ptr<std::uint64_t>{&raw, noop};
    const std::vector<std::shared_ptr<std::uint64_t>> values{first, second};

    std::vector<std::byte> bytes;
    auto                   enc = make_encoder<shared_ptr_codec>(bytes);
    REQUIRE(enc(values));
    CHECK_EQ(to_hex(bytes), "82d81c07d81d00");

    std::vector<std::shared_ptr<std::uint64_t>> decoded;
    auto                                        dec = make_decoder<shared_ptr_codec>(bytes);
    REQUIRE(dec(decoded));
    REQUIRE(decoded.size() == 2U);
    CHECK(decoded[0] == decoded[1]);
}

TEST_CASE("external scopes preserve references across calls") {
    auto value = std::make_shared<std::uint64_t>(1U);

    smart_ptr_test::encode_table encode_table;
    encode_table.reserve(4U);
    std::vector<std::byte> bytes;
    auto                   enc = make_encoder<shared_ptr_codec>(bytes);
    enc.set_shared_ptr_scope(encode_table);
    REQUIRE(enc(value));
    REQUIRE(enc(value));
    CHECK_EQ(to_hex(bytes), "d81c01d81d00");

    smart_ptr_test::decode_table decode_table;
    decode_table.reserve(4U);
    std::shared_ptr<std::uint64_t> first;
    std::shared_ptr<std::uint64_t> second;
    auto                           dec = make_decoder<shared_ptr_codec>(bytes);
    dec.set_shared_ptr_scope(decode_table);
    REQUIRE(dec(first));
    REQUIRE(dec(second));
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first == second);
    CHECK_EQ(*first, 1U);
}

TEST_CASE("external scopes deliberately retain the first cross-call snapshot") {
    auto value = std::make_shared<std::uint64_t>(1U);

    smart_ptr_test::encode_table table;
    std::vector<std::byte>       bytes;
    auto                         enc = make_encoder<shared_ptr_codec>(bytes);
    enc.set_shared_ptr_scope(table);
    REQUIRE(enc(value));
    *value = 2U;
    REQUIRE(enc(value));
    CHECK_EQ(to_hex(bytes), "d81c01d81d00");

    table.reset();
    REQUIRE(enc(value));
    CHECK_EQ(to_hex(bytes), "d81c01d81d00d81c02");
}

TEST_CASE("external table status failures propagate unchanged") {
    auto value = std::make_shared<std::uint64_t>(1U);

    smart_ptr_test::encode_table table{0U};
    std::vector<std::byte>       bytes;
    auto                         enc = make_encoder<shared_ptr_codec>(bytes);
    enc.set_shared_ptr_scope(table);
    const auto result = enc(value);
    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::size_limit_exceeded);
    CHECK(bytes.empty());
}

TEST_CASE("failed external encode calls leave caller-owned table state terminal") {
    auto value   = std::make_shared<smart_ptr_test::node>();
    value->value = 3U;
    value->next  = value;

    smart_ptr_test::encode_table table;
    std::vector<std::byte>       bytes;
    auto                         enc = make_encoder<shared_ptr_codec>(bytes);
    enc.set_shared_ptr_scope(table);
    REQUIRE_FALSE(enc(value));

    const auto retained_failure = enc(value);
    REQUIRE_FALSE(retained_failure);
    CHECK_EQ(retained_failure.error(), status_code::error);

    table.reset();
    value->next.reset();
    REQUIRE(enc(value));
}

TEST_CASE("failed external decode calls leave caller-owned table state terminal") {
    smart_ptr_test::decode_table table;

    const auto incomplete_bytes = to_bytes("d81c8207");
    auto       incomplete_dec   = make_decoder<shared_ptr_codec>(incomplete_bytes);
    incomplete_dec.set_shared_ptr_scope(table);
    std::shared_ptr<smart_ptr_test::partial_record> partial;
    const auto                                      incomplete_result = incomplete_dec(partial);
    REQUIRE_FALSE(incomplete_result);
    CHECK_EQ(incomplete_result.error(), status_code::incomplete);
    REQUIRE(partial);
    CHECK_EQ(partial->first, 7U);

    const auto reference_bytes = to_bytes("d81d00");
    auto       reference_dec   = make_decoder<shared_ptr_codec>(reference_bytes);
    reference_dec.set_shared_ptr_scope(table);
    std::shared_ptr<smart_ptr_test::partial_record> reference;
    const auto                                      reference_result = reference_dec(reference);
    REQUIRE_FALSE(reference_result);
    CHECK_EQ(reference_result.error(), status_code::error);
    CHECK_FALSE(reference);

    table.reset();
    const auto complete_bytes = to_bytes("d81c82076178");
    auto       complete_dec   = make_decoder<shared_ptr_codec>(complete_bytes);
    complete_dec.set_shared_ptr_scope(table);
    REQUIRE(complete_dec(reference));
    REQUIRE(reference);
    CHECK_EQ(reference->first, 7U);
    CHECK_EQ(reference->second, "x");
}

TEST_CASE("external decode tables reject references with a different pointee type") {
    smart_ptr_test::decode_table table;

    const auto first_bytes = to_bytes("d81c01");
    auto       first_dec   = make_decoder<shared_ptr_codec>(first_bytes);
    first_dec.set_shared_ptr_scope(table);
    std::shared_ptr<std::uint64_t> first;
    REQUIRE(first_dec(first));

    const auto reference_bytes = to_bytes("d81d00");
    auto       reference_dec   = make_decoder<shared_ptr_codec>(reference_bytes);
    reference_dec.set_shared_ptr_scope(table);
    std::shared_ptr<std::string> wrong_type;
    const auto                   result = reference_dec(wrong_type);
    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::error);
    CHECK_FALSE(wrong_type);
}

TEST_CASE("shared_ptr decoder rejects invalid and incomplete references") {
    SUBCASE("reference is outside the table") {
        const auto                     bytes = to_bytes("d81d00");
        auto                           dec   = make_decoder<shared_ptr_codec>(bytes);
        std::shared_ptr<std::uint64_t> value;
        const auto                     result = dec(value);
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
    }

    SUBCASE("shareable pointee is incomplete but remains assigned") {
        auto source = std::make_shared<smart_ptr_test::partial_record>(smart_ptr_test::partial_record{.first = 11U, .second = "Ada"});
        std::vector<std::byte> bytes;
        auto                   enc = make_encoder<shared_ptr_codec>(bytes);
        REQUIRE(enc(source));
        bytes.pop_back();

        auto                                            dec = make_decoder<shared_ptr_codec>(bytes);
        std::shared_ptr<smart_ptr_test::partial_record> value;
        const auto                                      result = dec(value);
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
        REQUIRE(value);
        CHECK_EQ(value->first, 11U);
    }
}

TEST_CASE("smart pointer codecs decode unsized non-contiguous input") {
    auto                                        value = std::make_shared<std::uint64_t>(42U);
    std::vector<std::shared_ptr<std::uint64_t>> sent{value, value};

    std::vector<std::byte> bytes;
    auto                   enc = make_encoder<shared_ptr_codec>(bytes);
    REQUIRE(enc(sent));

    const std::list<std::byte> storage(bytes.begin(), bytes.end());
    auto                       input = std::ranges::subrange(storage.begin(), storage.end());
    static_assert(!std::ranges::sized_range<decltype(input)>);
    static_assert(!std::ranges::contiguous_range<decltype(input)>);

    std::vector<std::shared_ptr<std::uint64_t>> decoded;
    auto                                        dec = make_decoder<shared_ptr_codec>(input);
    REQUIRE(dec(decoded));
    REQUIRE(decoded.size() == 2U);
    CHECK(decoded[0] == decoded[1]);
}

TEST_CASE("unambiguous unique_ptr variants dispatch by wire shape once") {
    using variant_type = std::variant<std::unique_ptr<std::uint64_t>, std::string>;

    {
        const auto   pointer = std::make_unique<std::uint64_t>(5U);
        const auto   bytes   = smart_ptr_test::encode_unique(pointer);
        auto         dec     = make_decoder<unique_ptr_codec>(bytes);
        variant_type value;
        REQUIRE(dec(value));
        REQUIRE(std::holds_alternative<std::unique_ptr<std::uint64_t>>(value));
        REQUIRE(std::get<std::unique_ptr<std::uint64_t>>(value));
        CHECK_EQ(*std::get<std::unique_ptr<std::uint64_t>>(value), 5U);
    }

    {
        std::vector<std::byte> bytes;
        auto                   enc = make_encoder(bytes);
        REQUIRE(enc(std::string{"Ada"}));
        auto         dec = make_decoder<unique_ptr_codec>(bytes);
        variant_type value;
        REQUIRE(dec(value));
        CHECK_EQ(std::get<std::string>(value), "Ada");
    }
}

TEST_CASE("CDDL uses native null and shared-reference tags") {
    std::string unique_schema;
    cddl_schema_to<std::unique_ptr<int>>(unique_schema);
    CHECK_EQ(unique_schema, "root = int / null");

    std::string shared_schema;
    cddl_schema_to<std::shared_ptr<int>>(shared_schema);
    CHECK_EQ(shared_schema, "root = null / #6.28(int) / #6.29(uint)");
}
