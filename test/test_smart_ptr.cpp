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
            return shared_ptr_observation{shared_ptr_observation_kind::reference, index};
        }

        if (entries_.size() >= limit_) {
            return cbor::tags::unexpected<status_code>{status_code::size_limit_exceeded};
        }

        const auto index = entries_.size();
        entries_.push_back(entry{key, shared_ptr_entry_state::encoding});
        return shared_ptr_observation{shared_ptr_observation_kind::first, index};
    }

    void mark_complete(std::size_t index) { entries_.at(index).state = shared_ptr_entry_state::complete; }

  private:
    std::vector<entry> entries_{};
    std::size_t        limit_{};
};

class decode_table {
  public:
    explicit decode_table(std::size_t limit = std::numeric_limits<std::size_t>::max()) : limit_(limit) {}

    void reserve(std::size_t count) { entries_.reserve(count); }
    void reset() { entries_.clear(); }

    [[nodiscard]] expected<std::size_t, status_code> insert(const shared_ptr_decode_entry &entry) {
        if (entries_.size() >= limit_) {
            return cbor::tags::unexpected<status_code>{status_code::size_limit_exceeded};
        }
        const auto index = entries_.size();
        entries_.push_back(entry);
        return index;
    }

    [[nodiscard]] expected<shared_ptr_decode_entry, status_code> resolve(std::size_t index) {
        if (index >= entries_.size()) {
            return cbor::tags::unexpected<status_code>{status_code::error};
        }
        return entries_[index];
    }

    void mark_complete(std::size_t index) { entries_.at(index).state = shared_ptr_entry_state::complete; }

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

struct custom_record {
    std::uint64_t value{};
};

template <typename Self> struct custom_record_codec : cbor_codec_mixin_base<Self> {
    using cbor_codec_mixin_base<Self>::decode;
    using cbor_codec_mixin_base<Self>::encode;

    void encode(const custom_record &value) {
        auto &enc = static_cast<Self &>(*this);
        enc.encode(static_tag<100>{});
        enc.encode(value.value);
    }

    [[nodiscard]] status_code decode(custom_record &value, major_type major, std::byte additional_info) {
        if (major != major_type::Tag) {
            return status_code::no_match_for_tag_on_buffer;
        }

        auto         &dec = static_cast<Self &>(*this);
        std::uint64_t tag{};
        const auto    status = cbor::tags::detail::decode_unsigned_argument(dec, additional_info, tag);
        if (status != status_code::success) {
            return status;
        }
        if (tag != 100U) {
            return status_code::no_match_for_tag;
        }
        return dec.decode(value.value);
    }
};

using smart_ptr_only_decoder = decltype(make_decoder<unique_ptr_codec>(std::declval<std::vector<std::byte> &>()));
using composed_custom_record_decoder =
    decltype(make_decoder<unique_ptr_codec, custom_record_codec>(std::declval<std::vector<std::byte> &>()));

static_assert(!cbor::tags::ext::smart_ptr::detail::extension_decodes_with_major_v<custom_record, smart_ptr_only_decoder>);
static_assert(cbor::tags::ext::smart_ptr::detail::extension_decodes_with_major_v<custom_record, composed_custom_record_decoder>);

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

template <typename T> class unique_handle {
  public:
    using element_type = T;

    unique_handle()                                     = default;
    unique_handle(unique_handle &&) noexcept            = default;
    unique_handle &operator=(unique_handle &&) noexcept = default;
    unique_handle(const unique_handle &)                = delete;
    unique_handle &operator=(const unique_handle &)     = delete;

    [[nodiscard]] T *get() const noexcept { return pointer_.get(); }
    [[nodiscard]] T &operator*() const noexcept { return *pointer_; }
    explicit         operator bool() const noexcept { return static_cast<bool>(pointer_); }

    void reset() noexcept { pointer_.reset(); }
    void reset(T *raw) { pointer_.reset(raw); }

  private:
    std::unique_ptr<T> pointer_;
};

template <typename T> class indexable_shared_handle : public shared_handle<T> {
  public:
    using shared_handle<T>::shared_handle;

    [[nodiscard]] T &operator[](std::size_t) const noexcept { return **this; }
};

struct counting_deleter {
    std::size_t *calls{};

    void operator()(std::uint64_t *value) const noexcept {
        ++*calls;
        delete value;
    }
};

namespace adl_probe {

struct value {};

void match_standard_array_smart_pointer(const std::shared_ptr<value> *);

} // namespace adl_probe

static_assert(IsSharedPointer<shared_handle<std::uint64_t>>);
static_assert(IsSharedPointer<indexable_shared_handle<std::uint64_t>>);
static_assert(IsSharedPointer<std::shared_ptr<std::uint64_t>>);
static_assert(!IsSharedPointer<std::shared_ptr<std::uint64_t[]>>);
static_assert(!IsSharedPointer<std::shared_ptr<std::uint64_t[4]>>);
static_assert(!IsSharedPointer<const std::shared_ptr<std::uint64_t[]> &>);
static_assert(IsSharedPointer<std::shared_ptr<adl_probe::value>>);
static_assert(IsUniquePointer<std::unique_ptr<std::uint64_t>>);
static_assert(IsUniquePointer<unique_handle<std::uint64_t>>);
static_assert(IsUniquePointer<std::unique_ptr<std::uint64_t, counting_deleter>>);
static_assert(!IsUniquePointer<std::unique_ptr<std::uint64_t[]>>);
static_assert(!IsUniquePointer<std::unique_ptr<std::uint64_t[], counting_deleter>>);
static_assert(!IsUniquePointer<const std::unique_ptr<std::uint64_t[]> &>);
static_assert(IsSmartPointer<shared_handle<std::uint64_t>>);
static_assert(IsSmartPointer<unique_handle<std::uint64_t>>);

namespace rtti_polymorphism {

inline constexpr std::uint64_t dog_tag = 60010U;
inline constexpr std::uint64_t cat_tag = 60011U;

struct animal {
    virtual ~animal() = default;
};

struct dog final : animal {
    std::uint64_t age{};
    std::string   name;
};

struct cat final : animal {
    std::string   name;
    std::uint64_t lives{};
};

struct unknown final : animal {};

template <typename Encoder> typename Encoder::expected_type encode(Encoder &enc, const std::shared_ptr<animal> &value) {
    if (!value) {
        return enc(nullptr);
    }
    if (const auto *dog_value = dynamic_cast<const dog *>(value.get())) {
        return enc(static_tag<dog_tag>{}, wrap_as_array{dog_value->age, dog_value->name});
    }
    if (const auto *cat_value = dynamic_cast<const cat *>(value.get())) {
        return enc(static_tag<cat_tag>{}, wrap_as_array{cat_value->name, cat_value->lives});
    }
    return typename Encoder::expected_type{cbor::tags::unexpected<status_code>{status_code::error}};
}

template <typename Decoder> typename Decoder::expected_type decode(Decoder &dec, std::shared_ptr<animal> &&value) {
    value.reset();

    std::optional<as_tag_any> tag;
    auto                      result = dec(tag);
    if (!result || !tag) {
        return result;
    }

    switch (tag->tag) {
    case dog_tag: {
        auto dog_value = std::make_shared<dog>();
        value          = dog_value;
        return dec(wrap_as_array{dog_value->age, dog_value->name});
    }
    case cat_tag: {
        auto cat_value = std::make_shared<cat>();
        value          = cat_value;
        return dec(wrap_as_array{cat_value->name, cat_value->lives});
    }
    default: return typename Decoder::expected_type{cbor::tags::unexpected<status_code>{status_code::no_match_for_tag}};
    }
}

} // namespace rtti_polymorphism

namespace no_rtti_polymorphism {

inline constexpr std::uint64_t dog_tag = 60020U;
inline constexpr std::uint64_t cat_tag = 60021U;

enum class animal_kind : std::uint8_t { dog, cat, unknown };

struct animal {
    virtual ~animal() = default;

    [[nodiscard]] virtual animal_kind kind() const noexcept = 0;
};

struct dog final : animal {
    std::uint64_t age{};
    std::string   name;

    [[nodiscard]] animal_kind kind() const noexcept final { return animal_kind::dog; }
};

struct cat final : animal {
    std::string   name;
    std::uint64_t lives{};

    [[nodiscard]] animal_kind kind() const noexcept final { return animal_kind::cat; }
};

struct unknown final : animal {
    [[nodiscard]] animal_kind kind() const noexcept final { return animal_kind::unknown; }
};

template <typename Encoder> typename Encoder::expected_type encode(Encoder &enc, const std::shared_ptr<animal> &value) {
    if (!value) {
        return enc(nullptr);
    }

    switch (value->kind()) {
    case animal_kind::dog: {
        const auto &dog_value = static_cast<const dog &>(*value);
        return enc(static_tag<dog_tag>{}, wrap_as_array{dog_value.age, dog_value.name});
    }
    case animal_kind::cat: {
        const auto &cat_value = static_cast<const cat &>(*value);
        return enc(static_tag<cat_tag>{}, wrap_as_array{cat_value.name, cat_value.lives});
    }
    case animal_kind::unknown: return typename Encoder::expected_type{cbor::tags::unexpected<status_code>{status_code::error}};
    }
    return typename Encoder::expected_type{cbor::tags::unexpected<status_code>{status_code::error}};
}

template <typename Decoder> typename Decoder::expected_type decode(Decoder &dec, std::shared_ptr<animal> &&value) {
    value.reset();

    std::optional<as_tag_any> tag;
    auto                      result = dec(tag);
    if (!result || !tag) {
        return result;
    }

    switch (tag->tag) {
    case dog_tag: {
        auto dog_value = std::make_shared<dog>();
        value          = dog_value;
        return dec(wrap_as_array{dog_value->age, dog_value->name});
    }
    case cat_tag: {
        auto cat_value = std::make_shared<cat>();
        value          = cat_value;
        return dec(wrap_as_array{cat_value->name, cat_value->lives});
    }
    default: return typename Decoder::expected_type{cbor::tags::unexpected<status_code>{status_code::no_match_for_tag}};
    }
}

} // namespace no_rtti_polymorphism

namespace virtual_encoder_polymorphism {

using buffer_type  = std::vector<std::byte>;
using encoder_type = decltype(make_encoder(std::declval<buffer_type &>()));
using result_type  = typename encoder_type::expected_type;

inline constexpr std::uint64_t dog_tag = 60030U;
inline constexpr std::uint64_t cat_tag = 60031U;

struct cbor_encodable {
    virtual ~cbor_encodable() = default;

    virtual result_type encode(encoder_type &) const = 0;
};

struct animal : cbor_encodable {
    ~animal() override = default;
};

struct dog final : animal {
    std::uint64_t age{};
    std::string   name;

    result_type encode(encoder_type &enc) const final { return enc(static_tag<dog_tag>{}, wrap_as_array{age, name}); }
};

struct cat final : animal {
    std::string   name;
    std::uint64_t lives{};

    result_type encode(encoder_type &enc) const final { return enc(static_tag<cat_tag>{}, wrap_as_array{name, lives}); }
};

inline result_type encode(encoder_type &enc, const std::shared_ptr<animal> &value) { return value ? enc(*value) : enc(nullptr); }

template <typename Decoder> typename Decoder::expected_type decode(Decoder &dec, std::shared_ptr<animal> &&value) {
    value.reset();

    std::optional<as_tag_any> tag;
    auto                      result = dec(tag);
    if (!result || !tag) {
        return result;
    }

    switch (tag->tag) {
    case dog_tag: {
        auto dog_value = std::make_shared<dog>();
        value          = dog_value;
        return dec(wrap_as_array{dog_value->age, dog_value->name});
    }
    case cat_tag: {
        auto cat_value = std::make_shared<cat>();
        value          = cat_value;
        return dec(wrap_as_array{cat_value->name, cat_value->lives});
    }
    default: return typename Decoder::expected_type{cbor::tags::unexpected<status_code>{status_code::no_match_for_tag}};
    }
}

} // namespace virtual_encoder_polymorphism

template <typename T> std::vector<std::byte> encode_unique(const std::unique_ptr<T> &value) {
    std::vector<std::byte> bytes;
    auto                   enc = make_encoder<unique_ptr_codec>(bytes);
    REQUIRE(enc(value));
    return bytes;
}

} // namespace smart_ptr_test

TEST_CASE("application shared pointer overload selects derived types with RTTI") {
    using namespace smart_ptr_test::rtti_polymorphism;

    {
        auto dog_value               = std::make_shared<dog>();
        dog_value->age               = 7U;
        dog_value->name              = "Rex";
        std::shared_ptr<animal> sent = dog_value;

        std::vector<std::byte> bytes;
        auto                   enc = make_encoder(bytes);
        REQUIRE(enc(sent));

        std::shared_ptr<animal> decoded;
        auto                    dec = make_decoder(bytes);
        REQUIRE(dec(decoded));

        const auto decoded_dog = std::dynamic_pointer_cast<dog>(decoded);
        REQUIRE(decoded_dog);
        CHECK_EQ(decoded_dog->age, 7U);
        CHECK_EQ(decoded_dog->name, "Rex");
    }

    {
        auto cat_value               = std::make_shared<cat>();
        cat_value->name              = "Mog";
        cat_value->lives             = 9U;
        std::shared_ptr<animal> sent = cat_value;

        std::vector<std::byte> bytes;
        auto                   enc = make_encoder(bytes);
        REQUIRE(enc(sent));

        std::shared_ptr<animal> decoded;
        auto                    dec = make_decoder(bytes);
        REQUIRE(dec(decoded));

        const auto decoded_cat = std::dynamic_pointer_cast<cat>(decoded);
        REQUIRE(decoded_cat);
        CHECK_EQ(decoded_cat->name, "Mog");
        CHECK_EQ(decoded_cat->lives, 9U);
    }

    {
        const std::shared_ptr<animal> sent;
        std::vector<std::byte>        bytes;
        auto                          enc = make_encoder(bytes);
        REQUIRE(enc(sent));
        CHECK_EQ(to_hex(bytes), "f6");

        auto decoded = std::shared_ptr<animal>{std::make_shared<dog>()};
        auto dec     = make_decoder(bytes);
        REQUIRE(dec(decoded));
        CHECK_FALSE(decoded);
    }
}

TEST_CASE("application shared pointer overload selects derived types without RTTI") {
    using namespace smart_ptr_test::no_rtti_polymorphism;

    {
        auto dog_value               = std::make_shared<dog>();
        dog_value->age               = 8U;
        dog_value->name              = "Pip";
        std::shared_ptr<animal> sent = dog_value;

        std::vector<std::byte> bytes;
        auto                   enc = make_encoder(bytes);
        REQUIRE(enc(sent));

        std::shared_ptr<animal> decoded;
        auto                    dec = make_decoder(bytes);
        REQUIRE(dec(decoded));
        REQUIRE(decoded);
        REQUIRE_EQ(decoded->kind(), animal_kind::dog);

        const auto decoded_dog = std::static_pointer_cast<dog>(decoded);
        CHECK_EQ(decoded_dog->age, 8U);
        CHECK_EQ(decoded_dog->name, "Pip");
    }

    {
        auto cat_value               = std::make_shared<cat>();
        cat_value->name              = "Nox";
        cat_value->lives             = 6U;
        std::shared_ptr<animal> sent = cat_value;

        std::vector<std::byte> bytes;
        auto                   enc = make_encoder(bytes);
        REQUIRE(enc(sent));

        std::shared_ptr<animal> decoded;
        auto                    dec = make_decoder(bytes);
        REQUIRE(dec(decoded));
        REQUIRE(decoded);
        REQUIRE_EQ(decoded->kind(), animal_kind::cat);

        const auto decoded_cat = std::static_pointer_cast<cat>(decoded);
        CHECK_EQ(decoded_cat->name, "Nox");
        CHECK_EQ(decoded_cat->lives, 6U);
    }

    {
        const std::shared_ptr<animal> sent;
        std::vector<std::byte>        bytes;
        auto                          enc = make_encoder(bytes);
        REQUIRE(enc(sent));

        auto decoded = std::shared_ptr<animal>{std::make_shared<cat>()};
        auto dec     = make_decoder(bytes);
        REQUIRE(dec(decoded));
        CHECK_FALSE(decoded);
    }
}

TEST_CASE("fixed virtual encoder interface roundtrips derived pointers") {
    using namespace smart_ptr_test::virtual_encoder_polymorphism;

    {
        auto dog_value               = std::make_shared<dog>();
        dog_value->age               = 4U;
        dog_value->name              = "Fido";
        std::shared_ptr<animal> sent = dog_value;

        buffer_type  bytes;
        encoder_type enc{bytes};
        REQUIRE(enc(sent));

        std::shared_ptr<animal> received;
        auto                    dec = make_decoder(bytes);
        REQUIRE(dec(received));
        REQUIRE(received);

        const auto received_dog = std::static_pointer_cast<dog>(received);
        CHECK_EQ(received_dog->age, 4U);
        CHECK_EQ(received_dog->name, "Fido");
    }

    {
        auto cat_value               = std::make_shared<cat>();
        cat_value->name              = "Luna";
        cat_value->lives             = 8U;
        std::shared_ptr<animal> sent = cat_value;

        buffer_type  bytes;
        encoder_type enc{bytes};
        REQUIRE(enc(sent));

        std::shared_ptr<animal> received;
        auto                    dec = make_decoder(bytes);
        REQUIRE(dec(received));
        REQUIRE(received);

        const auto received_cat = std::static_pointer_cast<cat>(received);
        CHECK_EQ(received_cat->name, "Luna");
        CHECK_EQ(received_cat->lives, 8U);
    }

    {
        const std::shared_ptr<animal> sent;
        buffer_type                   bytes;
        encoder_type                  enc{bytes};
        REQUIRE(enc(sent));

        auto received = std::shared_ptr<animal>{std::make_shared<dog>()};
        auto dec      = make_decoder(bytes);
        REQUIRE(dec(received));
        CHECK_FALSE(received);
    }
}

TEST_CASE("application shared pointer overload rejects unknown subtypes and tags") {
    using namespace smart_ptr_test::rtti_polymorphism;

    {
        const std::shared_ptr<animal> sent = std::make_shared<unknown>();
        std::vector<std::byte>        bytes;
        auto                          enc    = make_encoder(bytes);
        const auto                    result = enc(sent);
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        CHECK(bytes.empty());
    }

    {
        std::vector<std::byte> bytes;
        auto                   enc = make_encoder(bytes);
        REQUIRE(enc(static_tag<60012>{}, wrap_as_array{1U}));

        auto       decoded = std::shared_ptr<animal>{std::make_shared<dog>()};
        auto       dec     = make_decoder(bytes);
        const auto result  = dec(decoded);
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_for_tag);
        CHECK_FALSE(decoded);
    }
}

TEST_CASE("application shared pointer decode keeps terminal partial derived state") {
    using namespace smart_ptr_test::rtti_polymorphism;

    auto dog_value                     = std::make_shared<dog>();
    dog_value->age                     = 7U;
    dog_value->name                    = "Rex";
    const std::shared_ptr<animal> sent = dog_value;

    std::vector<std::byte> bytes;
    auto                   enc = make_encoder(bytes);
    REQUIRE(enc(sent));
    REQUIRE(bytes.size() > 1U);
    bytes.pop_back();

    std::shared_ptr<animal> decoded;
    auto                    dec    = make_decoder(bytes);
    const auto              result = dec(decoded);
    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::incomplete);

    const auto decoded_dog = std::dynamic_pointer_cast<dog>(decoded);
    REQUIRE(decoded_dog);
    CHECK_EQ(decoded_dog->age, 7U);
}

TEST_CASE("application shared pointer overloads encode aliases as independent values") {
    using namespace smart_ptr_test::rtti_polymorphism;

    auto dog_value                     = std::make_shared<dog>();
    dog_value->age                     = 5U;
    dog_value->name                    = "Ada";
    const std::shared_ptr<animal> sent = dog_value;

    std::vector<std::byte> bytes;
    auto                   enc = make_encoder(bytes);
    REQUIRE(enc(sent, sent));

    std::shared_ptr<animal> first;
    std::shared_ptr<animal> second;
    auto                    dec = make_decoder(bytes);
    REQUIRE(dec(first, second));
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first != second);
    const auto first_dog  = std::dynamic_pointer_cast<dog>(first);
    const auto second_dog = std::dynamic_pointer_cast<dog>(second);
    REQUIRE(first_dog);
    REQUIRE(second_dog);
    CHECK_EQ(first_dog->name, "Ada");
    CHECK_EQ(second_dog->name, "Ada");
}

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

TEST_CASE("unique_ptr codec delegates aggregate pointees to composed codecs") {
    const auto value = std::make_unique<smart_ptr_test::custom_record>(smart_ptr_test::custom_record{42U});

    std::vector<std::byte> bytes;
    auto                   enc = make_encoder<unique_ptr_codec, smart_ptr_test::custom_record_codec>(bytes);
    REQUIRE(enc(value));
    CHECK_EQ(to_hex(bytes), "d864182a");

    std::unique_ptr<smart_ptr_test::custom_record> decoded;
    auto                                           dec = make_decoder<unique_ptr_codec, smart_ptr_test::custom_record_codec>(bytes);
    REQUIRE(dec(decoded));
    REQUIRE(decoded);
    CHECK_EQ(decoded->value, 42U);
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

TEST_CASE("unique pointer concept accepts a user-defined pointer type") {
    smart_ptr_test::unique_handle<std::uint64_t> sent;
    sent.reset(new std::uint64_t{42U});

    std::vector<std::byte> bytes;
    auto                   enc = make_encoder<unique_ptr_codec>(bytes);
    REQUIRE(enc(sent));
    CHECK_EQ(to_hex(bytes), "182a");

    smart_ptr_test::unique_handle<std::uint64_t> decoded;
    auto                                         dec = make_decoder<unique_ptr_codec>(bytes);
    REQUIRE(dec(decoded));
    REQUIRE(decoded);
    CHECK_EQ(*decoded, 42U);
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

    SUBCASE("wire reference does not narrow to the host table index") {
        const auto                     bytes = to_bytes("d81d1bffffffffffffffff");
        auto                           dec   = make_decoder<shared_ptr_codec>(bytes);
        std::shared_ptr<std::uint64_t> value;
        const auto                     result = dec(value);
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        CHECK_FALSE(value);
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
