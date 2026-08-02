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
#include <type_traits>
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
    std::size_t                 index{};
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
concept SharedPtrEncodeScope = requires(Scope &scope, const shared_ptr_encode_key &key, std::size_t index) {
    { scope.observe(key) } -> std::same_as<expected<shared_ptr_observation, status_code>>;
    { scope.observe_untracked() } -> std::same_as<expected<void, status_code>>;
    { scope.mark_complete(index) } -> std::same_as<void>;
    { scope.reset() } -> std::same_as<void>;
};

template <typename Scope>
concept SharedPtrDecodeScope = requires(Scope &scope, const shared_ptr_decode_entry &entry, std::size_t index) {
    { scope.insert(entry) } -> std::same_as<expected<std::size_t, status_code>>;
    { scope.insert_untracked() } -> std::same_as<expected<void, status_code>>;
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
            return shared_ptr_observation{.kind = shared_ptr_observation_kind::reference, .index = found->second};
        }

        const auto index = entries_.size();
        entries_.push_back(entry{.key = key, .state = shared_ptr_entry_state::encoding});
        try {
            lookup_.emplace(key, index);
        } catch (...) {
            entries_.pop_back();
            throw;
        }
        return shared_ptr_observation{.kind = shared_ptr_observation_kind::first, .index = index};
    }

    [[nodiscard]] expected<void, status_code> observe_untracked() {
        entries_.push_back({});
        return {};
    }

    void mark_complete(std::size_t index) {
        if (index < entries_.size()) {
            entries_[index].state = shared_ptr_entry_state::complete;
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
    [[nodiscard]] expected<std::size_t, status_code> insert(const shared_ptr_decode_entry &entry) {
        const auto index = entries_.size();
        entries_.push_back(entry);
        return index;
    }

    [[nodiscard]] expected<void, status_code> insert_untracked() {
        entries_.push_back({});
        return {};
    }

    [[nodiscard]] expected<shared_ptr_decode_entry, status_code> resolve(std::size_t index) {
        if (index >= entries_.size()) {
            return unexpected<status_code>{status_code::error};
        }
        return entries_[index];
    }

    void mark_complete(std::size_t index) {
        if (index < entries_.size()) {
            entries_[index].state = shared_ptr_entry_state::complete;
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

namespace detail {

class encode_scope_ref {
  public:
    template <SharedPtrEncodeScope Scope>
    explicit encode_scope_ref(Scope &scope)
        : scope_(std::addressof(scope)),
          observe_([](void *raw, const shared_ptr_encode_key &key) { return static_cast<Scope *>(raw)->observe(key); }),
          observe_untracked_([](void *raw) { return static_cast<Scope *>(raw)->observe_untracked(); }),
          mark_complete_([](void *raw, std::size_t index) { static_cast<Scope *>(raw)->mark_complete(index); }),
          reset_([](void *raw) { static_cast<Scope *>(raw)->reset(); }) {}

    [[nodiscard]] expected<shared_ptr_observation, status_code> observe(const shared_ptr_encode_key &key) const {
        return observe_(scope_, key);
    }
    [[nodiscard]] expected<void, status_code> observe_untracked() const { return observe_untracked_(scope_); }
    void                                      mark_complete(std::size_t index) const { mark_complete_(scope_, index); }
    void                                      reset() const { reset_(scope_); }

  private:
    void *scope_{};
    expected<shared_ptr_observation, status_code> (*observe_)(void *, const shared_ptr_encode_key &){};
    expected<void, status_code> (*observe_untracked_)(void *){};
    void (*mark_complete_)(void *, std::size_t){};
    void (*reset_)(void *){};
};

class decode_scope_ref {
  public:
    template <SharedPtrDecodeScope Scope>
    explicit decode_scope_ref(Scope &scope)
        : scope_(std::addressof(scope)),
          insert_([](void *raw, const shared_ptr_decode_entry &entry) { return static_cast<Scope *>(raw)->insert(entry); }),
          insert_untracked_([](void *raw) { return static_cast<Scope *>(raw)->insert_untracked(); }),
          resolve_([](void *raw, std::size_t index) { return static_cast<Scope *>(raw)->resolve(index); }),
          mark_complete_([](void *raw, std::size_t index) { static_cast<Scope *>(raw)->mark_complete(index); }),
          reset_([](void *raw) { static_cast<Scope *>(raw)->reset(); }) {}

    [[nodiscard]] expected<std::size_t, status_code> insert(const shared_ptr_decode_entry &entry) const { return insert_(scope_, entry); }
    [[nodiscard]] expected<void, status_code>        insert_untracked() const { return insert_untracked_(scope_); }
    [[nodiscard]] expected<shared_ptr_decode_entry, status_code> resolve(std::size_t index) const { return resolve_(scope_, index); }
    void                                                         mark_complete(std::size_t index) const { mark_complete_(scope_, index); }
    void                                                         reset() const { reset_(scope_); }

  private:
    void *scope_{};
    expected<std::size_t, status_code> (*insert_)(void *, const shared_ptr_decode_entry &){};
    expected<void, status_code> (*insert_untracked_)(void *){};
    expected<shared_ptr_decode_entry, status_code> (*resolve_)(void *, std::size_t){};
    void (*mark_complete_)(void *, std::size_t){};
    void (*reset_)(void *){};
};

template <typename Self>
concept EncoderSelf = requires(Self &self, std::uint64_t value, typename Self::byte_type byte) { self.encode_major_and_size(value, byte); };

template <typename Self>
concept DecoderSelf = !EncoderSelf<Self>;

template <typename Codec, typename T>
concept CodecDecodesWithMajor = requires(Codec &codec, T &value, major_type major, std::byte additional_info) {
    { codec.decode(value, major, additional_info) } -> std::same_as<status_code>;
};

template <typename T, typename Decoder> struct extension_decodes_with_major : std::false_type {};

template <typename T, typename InputBuffer, typename Options, template <typename> typename... Decoders>
struct extension_decodes_with_major<T, cbor::tags::decoder<InputBuffer, Options, Decoders...>> {
    using decoder_type = cbor::tags::decoder<InputBuffer, Options, Decoders...>;

    static constexpr bool value = (CodecDecodesWithMajor<Decoders<decoder_type>, T> || ...);
};

template <typename T, typename Decoder>
inline constexpr bool extension_decodes_with_major_v =
    extension_decodes_with_major<std::remove_cvref_t<T>, std::remove_cvref_t<Decoder>>::value;

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
        const auto    status = cbor::tags::detail::decode_tag_argument(dec, additional_info, tag);
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
    if constexpr (SharedPointer<Pointer> && std::constructible_from<std::remove_cvref_t<Pointer>, std::shared_ptr<element_type>>) {
        // Establish ownership before converting to the structural pointer.
        // shared_ptr(raw) deletes raw if its control-block allocation fails.
        auto allocation = std::shared_ptr<element_type>{new element_type{}};
        value           = std::remove_cvref_t<Pointer>{std::move(allocation)};
    } else {
        auto  allocation = std::make_unique<element_type>();
        auto *raw        = allocation.release();
        value.reset(raw);
    }
}

template <typename Decoder, UniquePointer Pointer>
[[nodiscard]] status_code decode_unique_pointer(Decoder &dec, Pointer &value, major_type major, std::byte additional_info) {
    using element_type = pointer_element_t<Pointer>;
    static_assert(!known_null_wire_v<element_type>, "unique pointer cannot decode a pointee that also has a CBOR null state");

    if (major == major_type::Simple && additional_info == static_cast<std::byte>(SimpleType::Null)) {
        value.reset();
        return status_code::success;
    }

    reset_pointer_to_new(value);
    return decode_transparent_value(dec, *value, major, additional_info);
}

template <typename Decoder, UniquePointer Pointer>
[[nodiscard]] status_code decode_unique_pointer_tag(Decoder &dec, Pointer &value, std::uint64_t tag) {
    using element_type = pointer_element_t<Pointer>;
    static_assert(!known_null_wire_v<element_type>, "unique pointer cannot decode a pointee that also has a CBOR null state");

    if constexpr (IsTag<element_type>) {
        reset_pointer_to_new(value);
        return dec.decode(*value, tag);
    } else {
        (void)dec;
        (void)value;
        (void)tag;
        return status_code::no_match_for_tag;
    }
}

template <typename Decoder, typename T> consteval bool unique_variant_alternative_supported() {
    using type = std::remove_cvref_t<T>;

    if constexpr (UniquePointer<type>) {
        return std::default_initializable<type> && std::default_initializable<pointer_element_t<type>> &&
               !known_null_wire_v<pointer_element_t<type>> && unique_variant_alternative_supported<Decoder, pointer_element_t<type>>();
    } else if constexpr (IsVariant<type>) {
        return cbor::tags::detail::with_variant_alternatives<type>(
            []<typename... Ts>() { return (unique_variant_alternative_supported<Decoder, Ts>() && ...); });
    } else if constexpr (IsClassWithDecodingOverload<Decoder, type> || extension_decodes_with_major_v<type, Decoder>) {
        return false;
    } else if constexpr ((IsAggregate<type> || IsUntaggedTuple<type>) && !IsTag<type>) {
        using tuple_type          = pointer_tuple_t<type>;
        constexpr auto item_count = std::tuple_size_v<std::remove_cvref_t<tuple_type>>;
        if constexpr (item_count == 0U) {
            return false;
        } else if constexpr (item_count > 1U) {
            return Decoder::options::wrap_groups;
        } else {
            return unique_variant_alternative_supported<Decoder, std::tuple_element_t<0U, std::remove_cvref_t<tuple_type>>>();
        }
    } else {
        return IsCborMajor<type> || IsArray<type> || IsMap<type>;
    }
}

template <typename Decoder, bool CatchAllPass, typename T>
[[nodiscard]] constexpr bool unique_variant_matches(major_type major, std::byte additional_info, const std::optional<std::uint64_t> &tag) {
    using type = std::remove_cvref_t<T>;
    if constexpr (UniquePointer<type>) {
        if (major == major_type::Simple && additional_info == static_cast<std::byte>(SimpleType::Null)) {
            return true;
        }
        return unique_variant_matches<Decoder, CatchAllPass, pointer_element_t<type>>(major, additional_info, tag);
    } else if constexpr (IsVariant<type>) {
        return cbor::tags::detail::with_variant_alternatives<type>(
            [&]<typename... Ts>() { return (unique_variant_matches<Decoder, CatchAllPass, Ts>(major, additional_info, tag) || ...); });
    } else if (major == major_type::Tag) {
        if (!tag.has_value()) {
            return false;
        }
        if constexpr (IsTagHeader<type>) {
            return true;
        } else if constexpr (IsTag<type>) {
            return static_cast<std::uint64_t>(cbor::tags::detail::get_tag_from_any<type>()) == *tag;
        } else {
            return false;
        }
    } else if constexpr ((IsAggregate<type> || IsUntaggedTuple<type>) && !IsTag<type>) {
        using tuple_type          = pointer_tuple_t<type>;
        constexpr auto item_count = std::tuple_size_v<std::remove_cvref_t<tuple_type>>;
        if constexpr (item_count > 1U && Decoder::options::wrap_groups) {
            return major == major_type::Array;
        } else if constexpr (item_count == 1U) {
            return unique_variant_matches<Decoder, CatchAllPass, std::tuple_element_t<0U, std::remove_cvref_t<tuple_type>>>(
                major, additional_info, tag);
        } else {
            return false;
        }
    } else {
        if (!cbor::tags::detail::matches_major_dispatch<type>(major)) {
            return false;
        }
        if (major == major_type::Simple) {
            return cbor::tags::detail::matches_simple_dispatch<CatchAllPass, type>(additional_info);
        }
        return true;
    }
}

template <typename Decoder, typename T>
[[nodiscard]] status_code decode_unique_variant_alternative(Decoder &dec, T &value, major_type major, std::byte additional_info,
                                                            std::optional<std::uint64_t> &tag) {
    using type = std::remove_cvref_t<T>;
    if constexpr (UniquePointer<type>) {
        if (major == major_type::Tag) {
            return decode_unique_pointer_tag(dec, value, *tag);
        }
        return decode_unique_pointer(dec, value, major, additional_info);
    } else if constexpr (IsVariant<type>) {
        return dec.decode_unique_pointer_variant_impl(value, major, additional_info, tag);
    } else if constexpr (IsTag<type>) {
        return dec.decode(value, *tag);
    } else {
        return dec.decode(value, major, additional_info);
    }
}

template <typename Decoder, IsVariant Variant>
[[nodiscard]] status_code decode_unique_pointer_variant(Decoder &dec, Variant &value, major_type major, std::byte additional_info,
                                                        std::optional<std::uint64_t> &tag) {
    using variant_type = std::remove_cvref_t<Variant>;

    static_assert(cbor::tags::detail::with_variant_alternatives<variant_type>(
                      []<typename... Ts>() { return (unique_variant_alternative_supported<Decoder, Ts>() && ...); }),
                  "unique pointer variant alternatives must have codec-independent CBOR wire shapes; add an explicit codec for the "
                  "whole variant");
    static_assert(pointer_variant_is_unambiguous<variant_type, typename Decoder::options>(),
                  "Pointer variant alternatives overlap on the CBOR wire; add an application tag or choose a different decode type");
    static_assert(cbor::tags::detail::with_variant_alternatives<variant_type>(
                      []<typename... Ts>() { return (std::default_initializable<Ts> && ...); }),
                  "unique pointer variant alternatives must be default-initializable");

    if (major == major_type::Tag && !tag.has_value()) {
        std::uint64_t decoded_tag{};
        const auto    status = cbor::tags::detail::decode_tag_argument(dec, additional_info, decoded_tag);
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
                if (selected || !unique_variant_matches<Decoder, CatchAllPass, alternative_type>(major, additional_info, tag)) {
                    return;
                }
                selected = true;
                cbor::tags::detail::variant_assign<I>(value, alternative_type{});
                auto &alternative = cbor::tags::detail::variant_get<I>(value);
                result            = decode_unique_variant_alternative(dec, alternative, major, additional_info, tag);
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

template <typename Self> struct unique_ptr_codec : cbor_codec_mixin_base<Self> {
    using cbor_codec_mixin_base<Self>::decode;
    using cbor_codec_mixin_base<Self>::encode;

    template <IsUniquePointer Pointer> void encode(const Pointer &value) {
        using element_type = detail::pointer_element_t<Pointer>;
        static_assert(!detail::known_null_wire_v<element_type>, "unique pointer cannot encode a pointee that also has a CBOR null state");
        static_assert(detail::encodes_one_cbor_item<typename Self::options, element_type>(),
                      "smart pointer pointee must encode exactly one CBOR item");
        auto &enc = static_cast<Self &>(*this);
        value ? enc.encode(*value) : enc.encode(nullptr);
    }

    template <IsUniquePointer Pointer>
        requires std::default_initializable<detail::pointer_element_t<Pointer>>
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
        return detail::decode_unique_pointer_variant(static_cast<Self &>(*this), value, major, additional_info, tag);
    }

    template <IsVariant Variant>
    [[nodiscard]] status_code decode_unique_pointer_variant_impl(Variant &value, major_type major, std::byte additional_info,
                                                                 std::optional<std::uint64_t> &tag) {
        return detail::decode_unique_pointer_variant(static_cast<Self &>(*this), value, major, additional_info, tag);
    }
};

template <typename Self> struct shared_ptr_codec : cbor_codec_mixin_base<Self> {
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

    template <typename S = Self>
        requires detail::EncoderSelf<S>
    void observe_encoded_cbor_tag(std::uint64_t tag) {
        if (tag != detail::shareable_tag) {
            return;
        }
        auto observed = current_encode_scope().observe_untracked();
        if (!observed) {
            throw cbor::tags::detail::encode_status_exception{observed.error()};
        }
    }

    template <typename S = Self>
        requires detail::DecoderSelf<S>
    [[nodiscard]] status_code observe_decoded_cbor_tag(std::uint64_t tag) {
        if (tag != detail::shareable_tag) {
            return status_code::success;
        }
        auto inserted = current_decode_scope().insert_untracked();
        return inserted ? status_code::success : inserted.error();
    }

    template <IsSharedPointer Pointer> void encode(const Pointer &value) { encode_shared_pointer(value); }

    template <IsSharedPointer Pointer> void encode(const scoped_shared_ptr<Pointer> &value) { encode_shared_pointer(value.value()); }

    template <IsSharedPointer Pointer>
        requires std::default_initializable<typename std::remove_cvref_t<Pointer>::element_type>
    [[nodiscard]] status_code decode(Pointer &value, major_type major, std::byte additional_info) {
        return decode_shared_pointer(value, major, additional_info);
    }

    template <IsSharedPointer Pointer>
        requires std::default_initializable<typename std::remove_cvref_t<Pointer>::element_type>
    [[nodiscard]] status_code decode(scoped_shared_ptr<Pointer> &value, major_type major, std::byte additional_info) {
        return decode_shared_pointer(value.value(), major, additional_info);
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
    void encode(const Variant &) {
        static_assert(always_false<Variant>::value, "variants containing shared pointers require an explicit application codec");
    }

    template <IsVariant Variant>
        requires detail::contains_shared_pointer_v<Variant>
    [[nodiscard]] status_code decode(Variant &, major_type, std::byte) {
        static_assert(always_false<Variant>::value, "variants containing shared pointers require an explicit application codec");
        return status_code::error;
    }

  private:
    template <IsSharedPointer Pointer> void encode_shared_pointer(const Pointer &value) {
        using pointer_type = std::remove_cvref_t<Pointer>;
        using element_type = typename pointer_type::element_type;
        static_assert(detail::encodes_one_cbor_item<typename Self::options, element_type>(),
                      "smart pointer pointee must encode exactly one CBOR item");

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
            if (!std::in_range<std::uint64_t>(observation->index)) {
                throw cbor::tags::detail::encode_status_exception{status_code::size_limit_exceeded};
            }
            enc.encode(static_cast<std::uint64_t>(observation->index));
            return;
        }

        // observe() already reserved this tag 28 index.
        enc.encode_major_and_size(detail::shareable_tag, static_cast<typename Self::byte_type>(0xC0));
        enc.encode(*value);
        scope.mark_complete(observation->index);
    }

    template <IsSharedPointer Pointer>
        requires std::default_initializable<typename std::remove_cvref_t<Pointer>::element_type>
    [[nodiscard]] status_code decode_shared_pointer(Pointer &value, major_type major, std::byte additional_info) {
        if (major == major_type::Simple && additional_info == static_cast<std::byte>(SimpleType::Null)) {
            value.reset();
            return status_code::success;
        }
        if (major != major_type::Tag) {
            return status_code::no_match_for_tag_on_buffer;
        }

        std::uint64_t tag{};
        // Tag 28 is registered by decode_shareable() with the pointer entry itself.
        // Observing it here would also insert an untracked entry and shift every reference index.
        const auto status = cbor::tags::detail::decode_unsigned_argument(static_cast<Self &>(*this), additional_info, tag);
        if (status != status_code::success) {
            return status;
        }
        if (tag == detail::shareable_tag) {
            return decode_shareable(value);
        }
        if (tag == detail::sharedref_tag) {
            return decode_sharedref(value);
        }
        return status_code::no_match_for_tag;
    }

    template <IsSharedPointer Pointer>
        requires std::default_initializable<typename std::remove_cvref_t<Pointer>::element_type>
    [[nodiscard]] status_code decode_shareable(Pointer &value) {
        using pointer_type = std::remove_cvref_t<Pointer>;
        using element_type = typename pointer_type::element_type;

        std::shared_ptr<void> stored;
        if constexpr (std::constructible_from<pointer_type, std::shared_ptr<element_type>>) {
            auto owner = std::make_shared<element_type>();
            value      = pointer_type{owner};
            stored     = std::move(owner);
        } else {
            detail::reset_pointer_to_new(value);
            stored = std::make_shared<pointer_type>(value);
        }

        auto scope    = current_decode_scope();
        auto inserted = scope.insert(
            shared_ptr_decode_entry{std::move(stored), detail::graph_type_id<pointer_type>(), shared_ptr_entry_state::encoding});
        if (!inserted) {
            return inserted.error();
        }

        const auto status = static_cast<Self &>(*this).decode(*value);
        if (status != status_code::success) {
            return status;
        }
        scope.mark_complete(*inserted);
        return status_code::success;
    }

    template <IsSharedPointer Pointer> [[nodiscard]] status_code decode_sharedref(Pointer &value) {
        using pointer_type = std::remove_cvref_t<Pointer>;
        std::uint64_t wire_index{};
        const auto    status = static_cast<Self &>(*this).decode(wire_index);
        if (status != status_code::success) {
            return status;
        }
        if (!std::in_range<std::size_t>(wire_index)) {
            return status_code::error;
        }

        auto resolved = current_decode_scope().resolve(static_cast<std::size_t>(wire_index));
        if (!resolved) {
            return resolved.error();
        }
        if (resolved->state != shared_ptr_entry_state::complete || resolved->pointer_type != detail::graph_type_id<pointer_type>()) {
            return status_code::error;
        }

        if constexpr (std::constructible_from<pointer_type, std::shared_ptr<typename pointer_type::element_type>>) {
            value = pointer_type{std::static_pointer_cast<typename pointer_type::element_type>(resolved->pointer)};
        } else {
            value = *std::static_pointer_cast<pointer_type>(resolved->pointer);
        }
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
