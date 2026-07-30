#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts_checking.h"
#include "cbor_tags/cbor_extensions.h"
#include "cbor_tags/detail/cbor_encode_error.h"
#include "cbor_tags/detail/cbor_extension_decode.h"
#include "cbor_tags/detail/cbor_variant_dispatch.h"
#include "cbor_tags/detail/smart_ptr_traits.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace cbor::tags::ext::smart_ptr {

template <typename T>
concept IsSmartPointer = detail::SmartPointer<T>;

template <typename T>
concept IsUniquePointer = detail::UniquePointer<T>;

template <typename T>
concept IsSharedPointer = detail::SharedPointer<T>;

enum class shared_ptr_entry_state : std::uint8_t { encoding, complete };
enum class shared_ptr_observation_kind : std::uint8_t { first, reference };

struct shared_ptr_observation {
    shared_ptr_observation_kind kind{};
    std::uint64_t               index{};
};

struct shared_ptr_encode_key {
    const void *target{};
    const void *pointer_type{};

    bool operator==(const shared_ptr_encode_key &) const = default;
};

struct shared_ptr_decode_entry {
    std::shared_ptr<void>  pointer{};
    const void            *pointer_type{};
    shared_ptr_entry_state state{shared_ptr_entry_state::encoding};
};

template <typename Scope>
concept SharedPtrEncodeScope = requires(Scope &scope, const shared_ptr_encode_key &key, std::uint64_t index) {
    { scope.observe(key) } -> std::same_as<expected<shared_ptr_observation, status_code>>;
    { scope.mark_complete(index) } -> std::same_as<void>;
    { scope.reset() } -> std::same_as<void>;
};

template <typename Scope>
concept SharedPtrDecodeScope = requires(Scope &scope, const shared_ptr_decode_entry &entry, std::uint64_t index) {
    { scope.insert(entry) } -> std::same_as<expected<std::uint64_t, status_code>>;
    { scope.resolve(index) } -> std::same_as<expected<shared_ptr_decode_entry, status_code>>;
    { scope.mark_complete(index) } -> std::same_as<void>;
    { scope.reset() } -> std::same_as<void>;
};

class shared_ptr_encode_scope {
  private:
    struct key_hash {
        [[nodiscard]] std::size_t operator()(const shared_ptr_encode_key &key) const noexcept {
            const auto target_hash = std::hash<const void *>{}(key.target);
            const auto type_hash   = std::hash<const void *>{}(key.pointer_type);
            return target_hash ^ (type_hash + 0x9e3779b9U + (target_hash << 6U) + (target_hash >> 2U));
        }
    };

    struct entry {
        shared_ptr_encode_key  key{};
        shared_ptr_entry_state state{shared_ptr_entry_state::encoding};
    };

  public:
    [[nodiscard]] expected<shared_ptr_observation, status_code> observe(const shared_ptr_encode_key &key) {
        if (const auto found = lookup_.find(key); found != lookup_.end()) {
            const auto &existing = entries_[found->second];
            if (existing.state != shared_ptr_entry_state::complete) {
                return unexpected<status_code>{status_code::error};
            }
            return shared_ptr_observation{.kind  = shared_ptr_observation_kind::reference,
                                          .index = static_cast<std::uint64_t>(found->second)};
        }

        const auto index = entries_.size();
        entries_.push_back(entry{.key = key, .state = shared_ptr_entry_state::encoding});
        try {
            lookup_.emplace(key, index);
        } catch (...) {
            entries_.pop_back();
            throw;
        }
        return shared_ptr_observation{.kind = shared_ptr_observation_kind::first, .index = static_cast<std::uint64_t>(index)};
    }

    void mark_complete(std::uint64_t index) {
        if (index < entries_.size()) {
            entries_[static_cast<std::size_t>(index)].state = shared_ptr_entry_state::complete;
        }
    }

    void reset() {
        entries_.clear();
        lookup_.clear();
    }

    void reserve(std::size_t count) {
        entries_.reserve(count);
        lookup_.reserve(count);
    }

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

  private:
    std::vector<entry>                                               entries_{};
    std::unordered_map<shared_ptr_encode_key, std::size_t, key_hash> lookup_{};
};

class shared_ptr_decode_scope {
  public:
    [[nodiscard]] expected<std::uint64_t, status_code> insert(const shared_ptr_decode_entry &entry) {
        const auto index = entries_.size();
        entries_.push_back(entry);
        return static_cast<std::uint64_t>(index);
    }

    [[nodiscard]] expected<shared_ptr_decode_entry, status_code> resolve(std::uint64_t index) {
        if (index >= entries_.size()) {
            return unexpected<status_code>{status_code::error};
        }
        return entries_[static_cast<std::size_t>(index)];
    }

    void mark_complete(std::uint64_t index) {
        if (index < entries_.size()) {
            entries_[static_cast<std::size_t>(index)].state = shared_ptr_entry_state::complete;
        }
    }

    void reset() { entries_.clear(); }
    void reserve(std::size_t count) { entries_.reserve(count); }

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

  private:
    std::vector<shared_ptr_decode_entry> entries_{};
};

static_assert(SharedPtrEncodeScope<shared_ptr_encode_scope>);
static_assert(SharedPtrDecodeScope<shared_ptr_decode_scope>);

template <IsSharedPointer Pointer> class scoped_shared_ptr {
  public:
    using pointer_type = std::remove_cvref_t<Pointer>;
    using element_type = typename pointer_type::element_type;

    scoped_shared_ptr()
        requires std::default_initializable<pointer_type>
    = default;

    explicit scoped_shared_ptr(pointer_type pointer) : pointer_(std::move(pointer)) {}

    [[nodiscard]] element_type *get() const noexcept(noexcept(pointer_.get())) { return pointer_.get(); }
    [[nodiscard]] element_type &operator*() const noexcept(noexcept(*pointer_)) { return *pointer_; }
    explicit operator bool() const noexcept(noexcept(static_cast<bool>(pointer_))) { return static_cast<bool>(pointer_); }

    void reset() noexcept(noexcept(pointer_.reset())) { pointer_.reset(); }
    void reset(element_type *raw) noexcept(noexcept(pointer_.reset(raw))) { pointer_.reset(raw); }

    [[nodiscard]] pointer_type       &value()       &noexcept { return pointer_; }
    [[nodiscard]] const pointer_type &value() const & noexcept { return pointer_; }
    [[nodiscard]] pointer_type      &&value()      &&noexcept { return std::move(pointer_); }

  private:
    pointer_type pointer_;
};

template <IsSharedPointer Pointer>
[[nodiscard]] auto as_scoped_shared_ptr(Pointer pointer) -> scoped_shared_ptr<std::remove_cvref_t<Pointer>> {
    return scoped_shared_ptr<std::remove_cvref_t<Pointer>>{std::move(pointer)};
}

template <IsSharedPointer Pointer>
    requires detail::HasRegisteredPointeeTypes<Pointer>
constexpr auto cbor_smart_pointer_pointee_types(pointee_types_for<scoped_shared_ptr<Pointer>>) {
    return cbor_smart_pointer_pointee_types(pointee_types_for<std::remove_cvref_t<Pointer>>{});
}

namespace detail {

class encode_scope_ref {
  public:
    template <SharedPtrEncodeScope Scope>
    explicit encode_scope_ref(Scope &scope)
        : scope_(std::addressof(scope)),
          observe_([](void *raw, const shared_ptr_encode_key &key) { return static_cast<Scope *>(raw)->observe(key); }),
          mark_complete_([](void *raw, std::uint64_t index) { static_cast<Scope *>(raw)->mark_complete(index); }),
          reset_([](void *raw) { static_cast<Scope *>(raw)->reset(); }) {}

    [[nodiscard]] expected<shared_ptr_observation, status_code> observe(const shared_ptr_encode_key &key) const {
        return observe_(scope_, key);
    }
    void mark_complete(std::uint64_t index) const { mark_complete_(scope_, index); }
    void reset() const { reset_(scope_); }

  private:
    void *scope_{};
    expected<shared_ptr_observation, status_code> (*observe_)(void *, const shared_ptr_encode_key &){};
    void (*mark_complete_)(void *, std::uint64_t){};
    void (*reset_)(void *){};
};

class decode_scope_ref {
  public:
    template <SharedPtrDecodeScope Scope>
    explicit decode_scope_ref(Scope &scope)
        : scope_(std::addressof(scope)),
          insert_([](void *raw, const shared_ptr_decode_entry &entry) { return static_cast<Scope *>(raw)->insert(entry); }),
          resolve_([](void *raw, std::uint64_t index) { return static_cast<Scope *>(raw)->resolve(index); }),
          mark_complete_([](void *raw, std::uint64_t index) { static_cast<Scope *>(raw)->mark_complete(index); }),
          reset_([](void *raw) { static_cast<Scope *>(raw)->reset(); }) {}

    [[nodiscard]] expected<std::uint64_t, status_code> insert(const shared_ptr_decode_entry &entry) const { return insert_(scope_, entry); }
    [[nodiscard]] expected<shared_ptr_decode_entry, status_code> resolve(std::uint64_t index) const { return resolve_(scope_, index); }
    void                                                         mark_complete(std::uint64_t index) const { mark_complete_(scope_, index); }
    void                                                         reset() const { reset_(scope_); }

  private:
    void *scope_{};
    expected<std::uint64_t, status_code> (*insert_)(void *, const shared_ptr_decode_entry &){};
    expected<shared_ptr_decode_entry, status_code> (*resolve_)(void *, std::uint64_t){};
    void (*mark_complete_)(void *, std::uint64_t){};
    void (*reset_)(void *){};
};

template <typename Self>
concept EncoderSelf = requires(Self &self, std::uint64_t value, typename Self::byte_type byte) { self.encode_major_and_size(value, byte); };

template <typename Self>
concept DecoderSelf = !EncoderSelf<Self>;

template <typename Decoder, typename T>
[[nodiscard]] status_code decode_transparent_value(Decoder &dec, T &value, major_type major, std::byte additional_info) {
    static_assert(encodes_one_cbor_item<typename Decoder::options, T>(), "smart pointer pointee must encode exactly one CBOR item");

    if constexpr (IsClassWithDecodingOverload<Decoder, T> || extension_decodes_with_major_v<T, Decoder>) {
        return dec.decode(value, major, additional_info);
    } else if constexpr (IsTag<T>) {
        if (major != major_type::Tag) {
            return status_code::no_match_for_tag_on_buffer;
        }

        std::uint64_t tag{};
        const auto    status = cbor::tags::detail::decode_unsigned_argument(dec, additional_info, tag);
        return status == status_code::success ? dec.decode(value, tag) : status;
    } else if constexpr (IsAggregate<T> || IsUntaggedTuple<T>) {
        auto &&tuple = [&]() -> decltype(auto) {
            if constexpr (IsAggregate<T>) {
                return to_tuple(value);
            } else {
                return (value);
            }
        }();
        using tuple_type             = std::remove_cvref_t<decltype(tuple)>;
        constexpr auto element_count = std::tuple_size_v<tuple_type>;
        constexpr bool wrapped_group = element_count > 1U && Decoder::options::wrap_groups;

        auto result = status_code::success;
        if constexpr (wrapped_group) {
            std::uint64_t encoded_count{};
            result = cbor::tags::detail::decode_definite_array_size(dec, major, additional_info, encoded_count);
            if (result != status_code::success) {
                return result;
            }
            if (encoded_count != element_count) {
                return status_code::unexpected_group_size;
            }
            std::apply([&](auto &...members) { ((result == status_code::success ? result = dec.decode(members) : result), ...); }, tuple);
        } else {
            result = dec.decode(std::get<0>(tuple), major, additional_info);
        }
        return result;
    } else {
        return dec.decode(value, major, additional_info);
    }
}

template <typename Pointer> void reset_pointer_to_new(Pointer &value) {
    using element_type = pointer_element_t<Pointer>;
    value.reset(new element_type{});
}

template <typename Encoder, SmartPointer Pointer> void encode_registered_pointee(Encoder &enc, const Pointer &value) {
    using pointer_type = std::remove_cvref_t<Pointer>;
    using tuple_type   = registered_pointee_types_t<pointer_type>;

    static_assert(registered_pointee_types_are_valid<pointer_type>(),
                  "registered smart pointer pointees must be non-empty, uniquely fixed-tagged, default-initializable public derived "
                  "types accepted by Pointer::reset; tags 28 and 29 are reserved; the polymorphic base must have a virtual "
                  "destructor");
    static_assert(
        []<std::size_t... Is>(std::index_sequence<Is...>) {
            return (!extension_encodes_value_v<std::tuple_element_t<Is, tuple_type>, Encoder> && ...);
        }(std::make_index_sequence<std::tuple_size_v<tuple_type>>{}),
        "registered smart pointer pointees cannot use a composed encoder codec; encode the whole pointer in an application codec");

    bool encoded = false;
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        auto try_encode = [&]<std::size_t I>() {
            using pointee_type = std::tuple_element_t<I, tuple_type>;
            if (encoded) {
                return;
            }
            if (typeid(*value.get()) == typeid(pointee_type)) {
                const auto *pointee = dynamic_cast<const pointee_type *>(value.get());
                static_assert(encodes_one_cbor_item<typename Encoder::options, pointee_type>(),
                              "registered smart pointer pointee must encode exactly one CBOR item");
                enc.encode(*pointee);
                encoded = true;
            }
        };
        (try_encode.template operator()<Is>(), ...);
    }(std::make_index_sequence<std::tuple_size_v<tuple_type>>{});

    if (!encoded) {
        throw cbor::tags::detail::encode_status_exception{status_code::error};
    }
}

template <typename Decoder, SmartPointer Pointer>
[[nodiscard]] status_code decode_registered_pointee(Decoder &dec, Pointer &value, std::uint64_t tag) {
    using pointer_type = std::remove_cvref_t<Pointer>;
    using tuple_type   = registered_pointee_types_t<pointer_type>;

    static_assert(registered_pointee_types_are_valid<pointer_type>(),
                  "registered smart pointer pointees must be non-empty, uniquely fixed-tagged, default-initializable public derived "
                  "types accepted by Pointer::reset; tags 28 and 29 are reserved; the polymorphic base must have a virtual "
                  "destructor");
    static_assert(
        []<std::size_t... Is>(std::index_sequence<Is...>) {
            return (!extension_decodes_with_major_v<std::tuple_element_t<Is, tuple_type>, Decoder> && ...);
        }(std::make_index_sequence<std::tuple_size_v<tuple_type>>{}),
        "registered smart pointer pointees cannot use a composed decoder codec; decode the whole pointer in an application codec");

    status_code result   = status_code::no_match_for_tag;
    bool        selected = false;
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        auto try_decode = [&]<std::size_t I>() {
            using pointee_type = std::tuple_element_t<I, tuple_type>;
            if (selected || tag != registered_pointee_tag<pointee_type>()) {
                return;
            }
            selected          = true;
            auto *new_pointee = new pointee_type{};
            value.reset(new_pointee);
            result = dec.decode(*new_pointee, tag);
        };
        (try_decode.template operator()<Is>(), ...);
    }(std::make_index_sequence<std::tuple_size_v<tuple_type>>{});
    return result;
}

template <typename Decoder, UniquePointer Pointer>
[[nodiscard]] status_code decode_unique_pointer(Decoder &dec, Pointer &value, major_type major, std::byte additional_info) {
    using element_type = pointer_element_t<Pointer>;
    static_assert(!known_null_wire_v<element_type>, "unique pointer cannot decode a pointee that also has a CBOR null state");

    if (major == major_type::Simple && additional_info == static_cast<std::byte>(SimpleType::Null)) {
        value.reset();
        return status_code::success;
    }

    if constexpr (HasRegisteredPointeeTypes<Pointer>) {
        if (major != major_type::Tag) {
            return status_code::no_match_for_tag_on_buffer;
        }
        std::uint64_t tag{};
        const auto    status = cbor::tags::detail::decode_unsigned_argument(dec, additional_info, tag);
        return status == status_code::success ? decode_registered_pointee(dec, value, tag) : status;
    } else {
        reset_pointer_to_new(value);
        return decode_transparent_value(dec, *value, major, additional_info);
    }
}

template <typename Decoder, typename T>
[[nodiscard]] status_code decode_pointer_variant_alternative(Decoder &dec, T &value, major_type major, std::byte additional_info,
                                                             std::optional<std::uint64_t> &tag);

template <typename Decoder, IsVariant Variant>
[[nodiscard]] status_code decode_pointer_variant(Decoder &dec, Variant &value, major_type major, std::byte additional_info,
                                                 std::optional<std::uint64_t> &tag);

template <typename Decoder, typename T> [[nodiscard]] status_code decode_pointer_known_tag(Decoder &dec, T &value, std::uint64_t tag) {
    using type = std::remove_cvref_t<T>;
    if constexpr (UniquePointer<type>) {
        using element_type = pointer_element_t<type>;
        static_assert(!known_null_wire_v<element_type>, "unique pointer cannot decode a pointee that also has a CBOR null state");
        if constexpr (HasRegisteredPointeeTypes<type>) {
            return decode_registered_pointee(dec, value, tag);
        } else {
            reset_pointer_to_new(value);
            return decode_pointer_known_tag(dec, *value, tag);
        }
    } else if constexpr (IsVariant<type>) {
        auto                known_tag = std::optional<std::uint64_t>{tag};
        constexpr std::byte unused_additional_info{};
        return decode_pointer_variant(dec, value, major_type::Tag, unused_additional_info, known_tag);
    } else if constexpr (IsOptional<type>) {
        using value_type = std::remove_cvref_t<typename type::value_type>;
        auto decoded     = cbor::tags::detail::make_decode_value_for_optional<value_type>(value);
        value            = std::move(decoded);
        return decode_pointer_known_tag(dec, value.value(), tag);
    } else if constexpr (IsTag<type>) {
        return dec.decode(value, tag);
    } else if constexpr (IsAggregate<type> || IsUntaggedTuple<type>) {
        auto &&tuple = [&]() -> decltype(auto) {
            if constexpr (IsAggregate<type>) {
                return to_tuple(value);
            } else {
                return (value);
            }
        }();
        using tuple_type = std::remove_cvref_t<decltype(tuple)>;
        if constexpr (std::tuple_size_v<tuple_type> == 1U) {
            return decode_pointer_known_tag(dec, std::get<0>(tuple), tag);
        } else {
            return status_code::no_match_for_tag;
        }
    } else {
        return status_code::no_match_for_tag;
    }
}

template <typename Decoder, UniquePointer Pointer>
[[nodiscard]] status_code decode_unique_pointer_tag(Decoder &dec, Pointer &value, std::uint64_t tag) {
    return decode_pointer_known_tag(dec, value, tag);
}

template <typename Decoder, typename T>
[[nodiscard]] status_code decode_pointer_variant_alternative(Decoder &dec, T &value, major_type major, std::byte additional_info,
                                                             std::optional<std::uint64_t> &tag) {
    using type = std::remove_cvref_t<T>;
    if constexpr (UniquePointer<type>) {
        if (major == major_type::Tag) {
            return decode_unique_pointer_tag(dec, value, *tag);
        }
        return decode_unique_pointer(dec, value, major, additional_info);
    } else if constexpr (SharedPointer<type>) {
        if (major == major_type::Tag) {
            return dec.decode_shared_pointer_tag_impl(value, *tag);
        }
        return dec.decode(value, major, additional_info);
    } else if constexpr (IsOptional<type>) {
        if (major == major_type::Tag) {
            return decode_pointer_known_tag(dec, value, *tag);
        }
        return dec.decode(value, major, additional_info);
    } else if constexpr (IsVariant<type>) {
        return decode_pointer_variant(dec, value, major, additional_info, tag);
    } else if constexpr (IsTag<type>) {
        return dec.decode(value, *tag);
    } else if constexpr (IsAggregate<type> || IsUntaggedTuple<type>) {
        auto &&tuple = [&]() -> decltype(auto) {
            if constexpr (IsAggregate<type>) {
                return to_tuple(value);
            } else {
                return (value);
            }
        }();
        using tuple_type = std::remove_cvref_t<decltype(tuple)>;
        if constexpr (std::tuple_size_v<tuple_type> == 1U) {
            return decode_pointer_variant_alternative(dec, std::get<0>(tuple), major, additional_info, tag);
        } else {
            return decode_transparent_value(dec, value, major, additional_info);
        }
    } else {
        return dec.decode(value, major, additional_info);
    }
}

template <typename Decoder, IsVariant Variant>
[[nodiscard]] status_code decode_pointer_variant(Decoder &dec, Variant &value, major_type major, std::byte additional_info,
                                                 std::optional<std::uint64_t> &tag) {
    using variant_type = std::remove_cvref_t<Variant>;

    static_assert(pointer_decoder_variant_is_supported<variant_type, Decoder>(),
                  "smart pointer variant has an unsupported alternative; composed codecs must decode the whole variant explicitly");
    static_assert(pointer_decoder_variant_is_unambiguous<variant_type, Decoder>(),
                  "Pointer variant alternatives overlap on the CBOR wire; add an application tag or choose a different decode type");
    static_assert(cbor::tags::detail::with_variant_alternatives<variant_type>(
                      []<typename... Ts>() { return (std::default_initializable<Ts> && ...); }),
                  "smart pointer variant alternatives must be default-initializable");

    if (major == major_type::Tag && !tag.has_value()) {
        std::uint64_t decoded_tag{};
        const auto    status = cbor::tags::detail::decode_unsigned_argument(dec, additional_info, decoded_tag);
        if (status != status_code::success) {
            return status;
        }
        tag = decoded_tag;
    }

    status_code result   = status_code::no_match_in_variant_on_buffer;
    bool        selected = false;

    auto select_pass = [&]<bool CatchAllPass>() {
        cbor::tags::detail::with_variant_alternative_indices<variant_type>([&]<std::size_t... Is>() {
            auto select = [&]<std::size_t I>() {
                using alternative_type = cbor::tags::detail::variant_alternative_t<I, variant_type>;
                if (selected || !pointer_variant_matches<CatchAllPass, alternative_type, Decoder>(major, additional_info, tag)) {
                    return;
                }
                selected = true;
                cbor::tags::detail::variant_assign<I>(value, alternative_type{});
                auto &alternative = cbor::tags::detail::variant_get<I>(value);
                result            = decode_pointer_variant_alternative(dec, alternative, major, additional_info, tag);
            };
            (select.template operator()<Is>(), ...);
        });
    };
    select_pass.template operator()<false>();
    if (!selected && major == major_type::Simple) {
        select_pass.template operator()<true>();
    }

    return result;
}

} // namespace detail

template <typename Self> struct shared_ptr_codec;

template <typename Self> struct unique_ptr_codec : cbor_codec_mixin_base<Self> {
    using smart_pointer_codec_marker = void;
    using cbor_codec_mixin_base<Self>::decode;
    using cbor_codec_mixin_base<Self>::encode;

    template <IsUniquePointer Pointer> void encode(const Pointer &value) {
        using element_type = detail::pointer_element_t<Pointer>;
        static_assert(!detail::known_null_wire_v<element_type>, "unique pointer cannot encode a pointee that also has a CBOR null state");
        auto &enc = static_cast<Self &>(*this);
        if (!value) {
            enc.encode(nullptr);
        } else if constexpr (detail::HasRegisteredPointeeTypes<Pointer>) {
            detail::encode_registered_pointee(enc, value);
        } else {
            static_assert(detail::encodes_one_cbor_item<typename Self::options, element_type>(),
                          "smart pointer pointee must encode exactly one CBOR item");
            enc.encode(*value);
        }
    }

    template <IsUniquePointer Pointer>
        requires(std::default_initializable<detail::pointer_element_t<Pointer>> || detail::HasRegisteredPointeeTypes<Pointer>)
    [[nodiscard]] status_code decode(Pointer &value, major_type major, std::byte additional_info) {
        return detail::decode_unique_pointer(static_cast<Self &>(*this), value, major, additional_info);
    }

    template <typename T>
        requires detail::has_pointer_null_wire_v<true, false, T>
    void encode(const std::optional<T> &) {
        static_assert(always_false<T>::value,
                      "std::optional<T> cannot contain a unique pointer null state because both empty states use CBOR null");
    }

    template <typename T>
        requires detail::has_pointer_null_wire_v<true, false, T>
    [[nodiscard]] status_code decode(std::optional<T> &, major_type, std::byte) {
        static_assert(always_false<T>::value,
                      "std::optional<T> cannot contain a unique pointer null state because both empty states use CBOR null");
        return status_code::error;
    }

    template <IsVariant Variant>
        requires(detail::contains_decodable_unique_pointer_v<Variant> && !detail::contains_shared_pointer_v<Variant>)
    [[nodiscard]] status_code decode(Variant &value, major_type major, std::byte additional_info) {
        std::optional<std::uint64_t> tag;
        return detail::decode_pointer_variant(static_cast<Self &>(*this), value, major, additional_info, tag);
    }

    template <IsVariant Variant>
        requires(detail::contains_decodable_unique_pointer_v<Variant> && detail::contains_shared_pointer_v<Variant> &&
                 !std::derived_from<Self, shared_ptr_codec<Self>>)
    [[nodiscard]] status_code decode(Variant &, major_type, std::byte) {
        static_assert(always_false<Variant>::value,
                      "a variant containing unique and shared pointers requires both unique_ptr_codec and shared_ptr_codec");
        return status_code::error;
    }
};

template <typename Self> struct shared_ptr_codec : cbor_codec_mixin_base<Self> {
    using smart_pointer_codec_marker = void;
    using cbor_codec_mixin_base<Self>::decode;
    using cbor_codec_mixin_base<Self>::encode;

    template <SharedPtrEncodeScope Scope, typename S = Self>
        requires detail::EncoderSelf<S>
    void set_shared_ptr_scope(Scope &scope) {
        external_encode_scope_.emplace(scope);
    }

    template <SharedPtrDecodeScope Scope, typename S = Self>
        requires detail::DecoderSelf<S>
    void set_shared_ptr_scope(Scope &scope) {
        external_decode_scope_.emplace(scope);
    }

    template <typename S = Self>
        requires detail::EncoderSelf<S>
    void use_default_shared_ptr_scope() {
        external_encode_scope_.reset();
    }

    template <typename S = Self>
        requires detail::DecoderSelf<S>
    void use_default_shared_ptr_scope() {
        external_decode_scope_.reset();
    }

    template <typename S = Self>
        requires detail::EncoderSelf<S>
    void reset_shared_ptr_scope() {
        current_encode_scope().reset();
    }

    template <typename S = Self>
        requires detail::DecoderSelf<S>
    void reset_shared_ptr_scope() {
        current_decode_scope().reset();
    }

    template <IsSharedPointer Pointer> void encode(const Pointer &value) { encode_shared_pointer(value); }

    template <IsSharedPointer Pointer> void encode(const scoped_shared_ptr<Pointer> &value) { encode_shared_pointer(value.value()); }

    template <IsSharedPointer Pointer>
        requires(std::default_initializable<typename std::remove_cvref_t<Pointer>::element_type> ||
                 detail::HasRegisteredPointeeTypes<Pointer>)
    [[nodiscard]] status_code decode(Pointer &value, major_type major, std::byte additional_info) {
        return decode_shared_pointer(value, major, additional_info);
    }

    template <IsSharedPointer Pointer>
        requires(std::default_initializable<typename std::remove_cvref_t<Pointer>::element_type> ||
                 detail::HasRegisteredPointeeTypes<Pointer>)
    [[nodiscard]] status_code decode(scoped_shared_ptr<Pointer> &value, major_type major, std::byte additional_info) {
        return decode_shared_pointer(value.value(), major, additional_info);
    }

    template <IsSharedPointer Pointer>
        requires(std::default_initializable<typename std::remove_cvref_t<Pointer>::element_type> ||
                 detail::HasRegisteredPointeeTypes<Pointer>)
    [[nodiscard]] status_code decode_shared_pointer_tag_impl(Pointer &value, std::uint64_t tag) {
        if (tag == detail::shareable_tag) {
            return decode_shareable(value);
        }
        if (tag == detail::sharedref_tag) {
            return decode_sharedref(value);
        }
        return status_code::no_match_for_tag;
    }

    template <IsSharedPointer Pointer>
        requires(std::default_initializable<typename std::remove_cvref_t<Pointer>::element_type> ||
                 detail::HasRegisteredPointeeTypes<Pointer>)
    [[nodiscard]] status_code decode_shared_pointer_tag_impl(scoped_shared_ptr<Pointer> &value, std::uint64_t tag) {
        return decode_shared_pointer_tag_impl(value.value(), tag);
    }

    template <typename T>
        requires(detail::has_pointer_null_wire_v<false, true, T> && !detail::has_pointer_null_wire_v<true, false, T>)
    void encode(const std::optional<T> &) {
        static_assert(always_false<T>::value,
                      "std::optional<T> cannot contain a smart pointer null state because both empty states use CBOR null");
    }

    template <typename T>
        requires(detail::has_pointer_null_wire_v<false, true, T> && !detail::has_pointer_null_wire_v<true, false, T>)
    [[nodiscard]] status_code decode(std::optional<T> &, major_type, std::byte) {
        static_assert(always_false<T>::value,
                      "std::optional<T> cannot contain a smart pointer null state because both empty states use CBOR null");
        return status_code::error;
    }

    template <IsVariant Variant>
        requires detail::contains_shared_pointer_v<Variant>
    [[nodiscard]] status_code decode(Variant &value, major_type major, std::byte additional_info) {
        static_assert(!detail::contains_decodable_unique_pointer_v<Variant> || std::derived_from<Self, unique_ptr_codec<Self>>,
                      "a variant containing unique and shared pointers requires both unique_ptr_codec and shared_ptr_codec");
        std::optional<std::uint64_t> tag;
        return detail::decode_pointer_variant(static_cast<Self &>(*this), value, major, additional_info, tag);
    }

  private:
    template <IsSharedPointer Pointer> void encode_shared_pointer(const Pointer &value) {
        using pointer_type = std::remove_cvref_t<Pointer>;
        using element_type = typename pointer_type::element_type;

        auto &enc = static_cast<Self &>(*this);
        if (!value) {
            enc.encode(nullptr);
            return;
        }

        auto scope = current_encode_scope();
        auto observation =
            scope.observe(shared_ptr_encode_key{static_cast<const void *>(value.get()), detail::graph_type_id<pointer_type>()});
        if (!observation) {
            throw cbor::tags::detail::encode_status_exception{observation.error()};
        }
        if (observation->kind == shared_ptr_observation_kind::reference) {
            enc.encode(static_tag<detail::sharedref_tag>{});
            enc.encode(observation->index);
            return;
        }

        enc.encode(static_tag<detail::shareable_tag>{});
        if constexpr (detail::HasRegisteredPointeeTypes<pointer_type>) {
            detail::encode_registered_pointee(enc, value);
        } else {
            static_assert(detail::encodes_one_cbor_item<typename Self::options, element_type>(),
                          "smart pointer pointee must encode exactly one CBOR item");
            enc.encode(*value);
        }
        scope.mark_complete(observation->index);
    }

    template <IsSharedPointer Pointer>
        requires(std::default_initializable<typename std::remove_cvref_t<Pointer>::element_type> ||
                 detail::HasRegisteredPointeeTypes<Pointer>)
    [[nodiscard]] status_code decode_shared_pointer(Pointer &value, major_type major, std::byte additional_info) {
        if (major == major_type::Simple && additional_info == static_cast<std::byte>(SimpleType::Null)) {
            value.reset();
            return status_code::success;
        }
        if (major != major_type::Tag) {
            return status_code::no_match_for_tag_on_buffer;
        }

        std::uint64_t tag{};
        const auto    status = cbor::tags::detail::decode_unsigned_argument(static_cast<Self &>(*this), additional_info, tag);
        if (status != status_code::success) {
            return status;
        }
        return decode_shared_pointer_tag_impl(value, tag);
    }

    template <IsSharedPointer Pointer>
        requires(std::default_initializable<typename std::remove_cvref_t<Pointer>::element_type> ||
                 detail::HasRegisteredPointeeTypes<Pointer>)
    [[nodiscard]] status_code insert_and_decode_shareable(Pointer &value, auto &&decode_pointee) {
        using pointer_type = std::remove_cvref_t<Pointer>;

        auto scope    = current_decode_scope();
        auto stored   = std::make_shared<pointer_type>(value);
        auto inserted = scope.insert(
            shared_ptr_decode_entry{std::move(stored), detail::graph_type_id<pointer_type>(), shared_ptr_entry_state::encoding});
        if (!inserted) {
            return inserted.error();
        }

        const auto status = decode_pointee();
        if (status != status_code::success) {
            return status;
        }
        scope.mark_complete(*inserted);
        return status_code::success;
    }

    template <IsSharedPointer Pointer>
        requires(std::default_initializable<typename std::remove_cvref_t<Pointer>::element_type> ||
                 detail::HasRegisteredPointeeTypes<Pointer>)
    [[nodiscard]] status_code decode_shareable(Pointer &value) {
        using pointer_type = std::remove_cvref_t<Pointer>;
        if constexpr (detail::HasRegisteredPointeeTypes<pointer_type>) {
            using tuple_type = detail::registered_pointee_types_t<pointer_type>;
            static_assert(
                detail::registered_pointee_types_are_valid<pointer_type>(),
                "registered smart pointer pointees must be non-empty, uniquely fixed-tagged, default-initializable public derived "
                "types accepted by Pointer::reset; tags 28 and 29 are reserved; the polymorphic base must have a virtual "
                "destructor");
            static_assert(
                []<std::size_t... Is>(std::index_sequence<Is...>) {
                    return (!detail::extension_decodes_with_major_v<std::tuple_element_t<Is, tuple_type>, Self> && ...);
                }(std::make_index_sequence<std::tuple_size_v<tuple_type>>{}),
                "registered smart pointer pointees cannot use a composed decoder codec; decode the whole pointer in an application "
                "codec");

            const auto [major, additional_info] = static_cast<Self &>(*this).read_initial_byte();
            if (major != major_type::Tag) {
                return status_code::no_match_for_tag_on_buffer;
            }

            std::uint64_t tag{};
            const auto    tag_status = cbor::tags::detail::decode_unsigned_argument(static_cast<Self &>(*this), additional_info, tag);
            if (tag_status != status_code::success) {
                return tag_status;
            }

            status_code result   = status_code::no_match_for_tag;
            bool        selected = false;
            [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                auto try_decode = [&]<std::size_t I>() {
                    using pointee_type = std::tuple_element_t<I, tuple_type>;
                    if (selected || tag != detail::registered_pointee_tag<pointee_type>()) {
                        return;
                    }
                    selected          = true;
                    auto *new_pointee = new pointee_type{};
                    value.reset(new_pointee);
                    result = insert_and_decode_shareable(value, [&]() { return static_cast<Self &>(*this).decode(*new_pointee, tag); });
                };
                (try_decode.template operator()<Is>(), ...);
            }(std::make_index_sequence<std::tuple_size_v<tuple_type>>{});
            return result;
        } else {
            detail::reset_pointer_to_new(value);
            return insert_and_decode_shareable(value, [&]() { return static_cast<Self &>(*this).decode(*value); });
        }
    }

    template <IsSharedPointer Pointer> [[nodiscard]] status_code decode_sharedref(Pointer &value) {
        using pointer_type = std::remove_cvref_t<Pointer>;
        std::uint64_t index{};
        const auto    status = static_cast<Self &>(*this).decode(index);
        if (status != status_code::success) {
            return status;
        }

        auto resolved = current_decode_scope().resolve(index);
        if (!resolved) {
            return resolved.error();
        }
        if (resolved->state != shared_ptr_entry_state::complete || resolved->pointer_type != detail::graph_type_id<pointer_type>()) {
            return status_code::error;
        }

        value = *std::static_pointer_cast<pointer_type>(resolved->pointer);
        return status_code::success;
    }

    [[nodiscard]] detail::encode_scope_ref current_encode_scope() {
        if (external_encode_scope_) {
            return *external_encode_scope_;
        }
        return detail::encode_scope_ref{default_encode_scope_};
    }

    [[nodiscard]] detail::decode_scope_ref current_decode_scope() {
        if (external_decode_scope_) {
            return *external_decode_scope_;
        }
        return detail::decode_scope_ref{default_decode_scope_};
    }

    shared_ptr_encode_scope                 default_encode_scope_{};
    shared_ptr_decode_scope                 default_decode_scope_{};
    std::optional<detail::encode_scope_ref> external_encode_scope_{};
    std::optional<detail::decode_scope_ref> external_decode_scope_{};
};

} // namespace cbor::tags::ext::smart_ptr
