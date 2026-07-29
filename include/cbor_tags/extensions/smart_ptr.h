#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts_checking.h"
#include "cbor_tags/cbor_extensions.h"
#include "cbor_tags/detail/cbor_encode_error.h"
#include "cbor_tags/detail/cbor_extension_decode.h"
#include "cbor_tags/detail/cbor_variant_dispatch.h"
#include "cbor_tags/detail/smart_ptr_traits.h"
#include "cbor_tags/extensions/cddl_traits.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace cbor::tags::ext::smart_ptr {

enum class shared_ptr_entry_state : std::uint8_t { encoding, complete };
enum class shared_ptr_observation_kind : std::uint8_t { first, reference };

struct shared_ptr_observation {
    shared_ptr_observation_kind kind{};
    std::uint64_t               index{};
};

struct shared_ptr_encode_key {
    std::weak_ptr<const void> owner{};
    const void               *target{};
    const void               *type{};
};

struct shared_ptr_decode_entry {
    std::shared_ptr<void>  value{};
    const void            *type{};
    shared_ptr_entry_state state{shared_ptr_entry_state::encoding};
};

template <typename Table>
concept SharedPtrEncodeTable = requires(Table &table, const shared_ptr_encode_key &key, std::uint64_t index) {
    { table.observe(key) } -> std::same_as<expected<shared_ptr_observation, status_code>>;
    { table.mark_complete(index) } -> std::same_as<void>;
};

template <typename Table>
concept SharedPtrDecodeTable = requires(Table &table, const shared_ptr_decode_entry &entry, std::uint64_t index) {
    { table.insert(entry) } -> std::same_as<expected<std::uint64_t, status_code>>;
    { table.resolve(index) } -> std::same_as<expected<shared_ptr_decode_entry, status_code>>;
    { table.mark_complete(index) } -> std::same_as<void>;
};

template <typename T> struct shared_ptr_cddl {
    using value_type = std::remove_cvref_t<T>;
};

template <typename T> struct shared_ptr_unscoped_cddl {
    using value_type = std::remove_cvref_t<T>;
};

template <typename T> class shared_ptr_root {
  public:
    shared_ptr_root(const shared_ptr_root &)            = default;
    shared_ptr_root &operator=(const shared_ptr_root &) = delete;
    shared_ptr_root(shared_ptr_root &&)                 = default;
    shared_ptr_root &operator=(shared_ptr_root &&)      = delete;

  private:
    explicit shared_ptr_root(T &value) : value_(value) {}

    T &value_;

    template <typename U> friend shared_ptr_root<U> as_shared_ptrs(U &);
    template <typename Self> friend struct shared_ptr_codec;
};

template <typename T, typename Table> class shared_ptr_unscoped_root {
  public:
    shared_ptr_unscoped_root(const shared_ptr_unscoped_root &)            = default;
    shared_ptr_unscoped_root &operator=(const shared_ptr_unscoped_root &) = delete;
    shared_ptr_unscoped_root(shared_ptr_unscoped_root &&)                 = default;
    shared_ptr_unscoped_root &operator=(shared_ptr_unscoped_root &&)      = delete;

  private:
    shared_ptr_unscoped_root(T &value, Table &table) : value_(value), table_(table) {}

    T     &value_;
    Table &table_;

    template <typename U, typename UTable> friend shared_ptr_unscoped_root<U, UTable> as_shared_ptrs_unscoped(U &, UTable &);
    template <typename Self> friend struct shared_ptr_codec;
};

template <typename T> [[nodiscard]] shared_ptr_root<T> as_shared_ptrs(T &value) { return shared_ptr_root<T>{value}; }

template <typename T, typename Table> [[nodiscard]] shared_ptr_unscoped_root<T, Table> as_shared_ptrs_unscoped(T &value, Table &table) {
    return shared_ptr_unscoped_root<T, Table>{value, table};
}

namespace detail {

struct encode_table_ref {
    void *table{};
    expected<shared_ptr_observation, status_code> (*observe_fn)(void *, const shared_ptr_encode_key &){};
    void (*mark_complete_fn)(void *, std::uint64_t){};

    template <SharedPtrEncodeTable Table>
    explicit encode_table_ref(Table &value)
        : table(std::addressof(value)),
          observe_fn([](void *raw, const shared_ptr_encode_key &key) { return static_cast<Table *>(raw)->observe(key); }),
          mark_complete_fn([](void *raw, std::uint64_t index) { static_cast<Table *>(raw)->mark_complete(index); }) {}

    [[nodiscard]] expected<shared_ptr_observation, status_code> observe(const shared_ptr_encode_key &key) const {
        return observe_fn(table, key);
    }

    void mark_complete(std::uint64_t index) const { mark_complete_fn(table, index); }
};

struct decode_table_ref {
    void *table{};
    expected<std::uint64_t, status_code> (*insert_fn)(void *, const shared_ptr_decode_entry &){};
    expected<shared_ptr_decode_entry, status_code> (*resolve_fn)(void *, std::uint64_t){};
    void (*mark_complete_fn)(void *, std::uint64_t){};

    template <SharedPtrDecodeTable Table>
    explicit decode_table_ref(Table &value)
        : table(std::addressof(value)),
          insert_fn([](void *raw, const shared_ptr_decode_entry &entry) { return static_cast<Table *>(raw)->insert(entry); }),
          resolve_fn([](void *raw, std::uint64_t index) { return static_cast<Table *>(raw)->resolve(index); }),
          mark_complete_fn([](void *raw, std::uint64_t index) { static_cast<Table *>(raw)->mark_complete(index); }) {}

    [[nodiscard]] expected<std::uint64_t, status_code> insert(const shared_ptr_decode_entry &entry) const {
        return insert_fn(table, entry);
    }

    [[nodiscard]] expected<shared_ptr_decode_entry, status_code> resolve(std::uint64_t index) const { return resolve_fn(table, index); }

    void mark_complete(std::uint64_t index) const { mark_complete_fn(table, index); }
};

template <typename Ref> class active_table_scope {
  public:
    active_table_scope(Ref *&active, Ref &next) : active_(active), previous_(active) { active_ = std::addressof(next); }

    active_table_scope(const active_table_scope &)            = delete;
    active_table_scope &operator=(const active_table_scope &) = delete;

    ~active_table_scope() { active_ = previous_; }

  private:
    Ref *&active_;
    Ref  *previous_{};
};

class default_encode_table {
  private:
    struct entry {
        shared_ptr_encode_key  key{};
        shared_ptr_entry_state state{shared_ptr_entry_state::encoding};
    };

    using owner_map = std::map<std::weak_ptr<const void>, std::size_t, std::owner_less<std::weak_ptr<const void>>>;

  public:
    [[nodiscard]] expected<shared_ptr_observation, status_code> observe(const shared_ptr_encode_key &key) {
        if (const auto found = owners_.find(key.owner); found != owners_.end()) {
            const auto &existing = entries_[found->second];
            if (existing.key.target != key.target || existing.key.type != key.type || existing.state != shared_ptr_entry_state::complete) {
                return unexpected<status_code>{status_code::error};
            }
            return shared_ptr_observation{shared_ptr_observation_kind::reference, static_cast<std::uint64_t>(found->second)};
        }

        const auto index = entries_.size();
        entries_.push_back(entry{key, shared_ptr_entry_state::encoding});
        try {
            owners_.emplace(key.owner, index);
        } catch (...) {
            entries_.pop_back();
            throw;
        }
        return shared_ptr_observation{shared_ptr_observation_kind::first, static_cast<std::uint64_t>(index)};
    }

    void mark_complete(std::uint64_t index) {
        if (index < entries_.size()) {
            entries_[static_cast<std::size_t>(index)].state = shared_ptr_entry_state::complete;
        }
    }

  private:
    std::vector<entry> entries_{};
    owner_map          owners_{};
};

class default_decode_table {
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

  private:
    std::vector<shared_ptr_decode_entry> entries_{};
};

template <typename Decoder, PointerValue T>
[[nodiscard]] status_code decode_transparent_value(Decoder &dec, T &value, major_type major, std::byte additional_info) {
    if constexpr (IsTag<T>) {
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

        static_assert(element_count > 0U, "unique_ptr<T> cannot transparently encode a zero-width tuple or aggregate");

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
            [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                ((result == status_code::success ? result = dec.decode(std::get<Is + 1U>(tuple)) : result), ...);
            }(std::make_index_sequence<element_count - 1U>{});
        }
        return result;
    } else {
        return dec.decode(value, major, additional_info);
    }
}

template <typename Decoder, PointerValue T>
[[nodiscard]] status_code decode_unique_pointer(Decoder &dec, std::unique_ptr<T> &value, major_type major, std::byte additional_info) {
    static_assert(!known_null_wire_v<T>,
                  "unique_ptr<T> cannot decode T when T also has a CBOR null state; the pointer states would not round-trip");

    if (major == major_type::Simple && additional_info == static_cast<std::byte>(SimpleType::Null)) {
        value.reset();
        return status_code::success;
    }

    value = std::make_unique<T>();
    return decode_transparent_value(dec, *value, major, additional_info);
}

template <typename Decoder, PointerValue T>
[[nodiscard]] status_code decode_unique_pointer_tag(Decoder &dec, std::unique_ptr<T> &value, std::uint64_t tag) {
    static_assert(!known_null_wire_v<T>,
                  "unique_ptr<T> cannot decode T when T also has a CBOR null state; the pointer states would not round-trip");

    if constexpr (IsTag<T>) {
        value = std::make_unique<T>();
        return dec.decode(*value, tag);
    } else {
        (void)dec;
        (void)value;
        (void)tag;
        return status_code::no_match_for_tag;
    }
}

template <bool AllowUnique, bool AllowShared, typename T> consteval bool pointer_variant_alternative_supported() {
    using type = std::remove_cvref_t<T>;
    if constexpr (decodable_unique_pointer_v<type>) {
        return AllowUnique && !known_null_wire_v<typename unique_pointer_traits<type>::element_type>;
    } else if constexpr (decodable_shared_pointer_v<type>) {
        return AllowShared;
    } else if constexpr (IsOptional<type>) {
        return IsCborMajor<type>;
    } else if constexpr (IsVariant<type>) {
        return cbor::tags::detail::with_variant_alternatives<type>(
            []<typename... Ts>() { return (pointer_variant_alternative_supported<AllowUnique, AllowShared, Ts>() && ...); });
    } else {
        return IsCborMajor<type> || IsArray<type> || IsMap<type>;
    }
}

template <typename T>
[[nodiscard]] constexpr bool pointer_variant_matches(major_type major, std::byte additional_info, const std::optional<std::uint64_t> &tag) {
    using type = std::remove_cvref_t<T>;
    if constexpr (decodable_unique_pointer_v<type>) {
        if (major == major_type::Simple && additional_info == static_cast<std::byte>(SimpleType::Null)) {
            return true;
        }
        using element_type = typename unique_pointer_traits<type>::element_type;
        return pointer_variant_matches<element_type>(major, additional_info, tag);
    } else if constexpr (decodable_shared_pointer_v<type>) {
        if (major == major_type::Simple && additional_info == static_cast<std::byte>(SimpleType::Null)) {
            return true;
        }
        return major == major_type::Tag && tag.has_value() && (*tag == shareable_tag || *tag == sharedref_tag);
    } else if constexpr (IsVariant<type>) {
        return cbor::tags::detail::with_variant_alternatives<type>(
            [&]<typename... Ts>() { return (pointer_variant_matches<Ts>(major, additional_info, tag) || ...); });
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
    } else {
        if (!cbor::tags::detail::matches_major_dispatch<type>(major)) {
            return false;
        }
        if (major == major_type::Simple) {
            return cbor::tags::detail::matches_simple_dispatch<false, type>(additional_info) ||
                   cbor::tags::detail::matches_simple_dispatch<true, type>(additional_info);
        }
        return true;
    }
}

template <bool AllowUnique, bool AllowShared, typename Decoder, typename T>
[[nodiscard]] status_code decode_pointer_variant_alternative(Decoder &dec, T &value, major_type major, std::byte additional_info,
                                                             std::optional<std::uint64_t> &tag) {
    using type = std::remove_cvref_t<T>;
    if constexpr (decodable_unique_pointer_v<type>) {
        if (major == major_type::Tag) {
            return decode_unique_pointer_tag(dec, value, *tag);
        }
        return decode_unique_pointer(dec, value, major, additional_info);
    } else if constexpr (decodable_shared_pointer_v<type>) {
        if (major == major_type::Tag) {
            return dec.decode_shared_pointer_after_tag(value, *tag);
        }
        return dec.decode(value, major, additional_info);
    } else if constexpr (IsVariant<type>) {
        return dec.template decode_pointer_variant_impl<AllowUnique, AllowShared>(value, major, additional_info, tag);
    } else if constexpr (IsTag<type>) {
        return dec.decode(value, *tag);
    } else {
        return dec.decode(value, major, additional_info);
    }
}

template <bool AllowUnique, bool AllowShared, typename Decoder, IsVariant Variant>
[[nodiscard]] status_code decode_pointer_variant(Decoder &dec, Variant &value, major_type major, std::byte additional_info,
                                                 std::optional<std::uint64_t> &tag) {
    using variant_type = std::remove_cvref_t<Variant>;

    static_assert(cbor::tags::detail::with_variant_alternatives<variant_type>(
                      []<typename... Ts>() { return (pointer_variant_alternative_supported<AllowUnique, AllowShared, Ts>() && ...); }),
                  "Pointer variant alternatives must have supported, decodable CBOR wire shapes");
    static_assert(pointer_variant_is_unambiguous<variant_type>(),
                  "Pointer variant alternatives overlap on the CBOR wire; add an application tag or choose a different decode type");
    static_assert(cbor::tags::detail::with_variant_alternatives<variant_type>(
                      []<typename... Ts>() { return (std::default_initializable<Ts> && ...); }),
                  "Pointer variant alternatives must be default-initializable");

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

    cbor::tags::detail::with_variant_alternative_indices<variant_type>([&]<std::size_t... Is>() {
        auto select = [&]<std::size_t I>() {
            using alternative_type = cbor::tags::detail::variant_alternative_t<I, variant_type>;
            if (selected || !pointer_variant_matches<alternative_type>(major, additional_info, tag)) {
                return;
            }

            selected = true;
            cbor::tags::detail::variant_assign<I>(value, alternative_type{});
            auto &alternative = cbor::tags::detail::variant_get<I>(value);
            result            = decode_pointer_variant_alternative<AllowUnique, AllowShared>(dec, alternative, major, additional_info, tag);
        };
        (select.template operator()<Is>(), ...);
    });

    return result;
}

} // namespace detail

template <typename Self> struct unique_ptr_codec : detail::unique_ptr_codec_marker, cbor_codec_mixin_base<Self> {
    using cbor_codec_mixin_base<Self>::decode;
    using cbor_codec_mixin_base<Self>::encode;

    template <detail::PointerValue T> void encode(const std::unique_ptr<T> &value) {
        static_assert(!detail::known_null_wire_v<T>,
                      "unique_ptr<T> cannot encode T when T also has a CBOR null state; the pointer states would not round-trip");
        auto &enc = static_cast<Self &>(*this);
        if (value) {
            enc.encode(*value);
        } else {
            enc.encode(nullptr);
        }
    }

    template <detail::PointerValue T>
        requires std::default_initializable<T>
    [[nodiscard]] status_code decode(std::unique_ptr<T> &value, major_type major, std::byte additional_info) {
        return detail::decode_unique_pointer(static_cast<Self &>(*this), value, major, additional_info);
    }

    template <typename T>
        requires(detail::has_pointer_null_wire_v<true, false, T> && !detail::has_shared_ptr_codec_v<Self>)
    void encode(const std::optional<T> &) {
        static_assert(always_false<T>::value,
                      "std::optional<T> cannot contain a unique_ptr null state because both empty states use CBOR null");
    }

    template <typename T>
        requires(detail::has_pointer_null_wire_v<true, false, T> && !detail::has_shared_ptr_codec_v<Self>)
    [[nodiscard]] status_code decode(std::optional<T> &, major_type, std::byte) {
        static_assert(always_false<T>::value,
                      "std::optional<T> cannot contain a unique_ptr null state because both empty states use CBOR null");
        return status_code::error;
    }

    template <IsVariant Variant>
        requires(detail::contains_decodable_unique_pointer_v<Variant> && !detail::has_shared_ptr_codec_v<Self>)
    [[nodiscard]] status_code decode(Variant &value, major_type major, std::byte additional_info) {
        std::optional<std::uint64_t> tag;
        return detail::decode_pointer_variant<true, false>(static_cast<Self &>(*this), value, major, additional_info, tag);
    }
};

template <typename Self> struct shared_ptr_codec : detail::shared_ptr_codec_marker, cbor_codec_mixin_base<Self> {
    using cbor_codec_mixin_base<Self>::decode;
    using cbor_codec_mixin_base<Self>::encode;

    template <typename T> void encode(shared_ptr_root<T> root) {
        auto &enc = static_cast<Self &>(*this);
        enc.encode(static_tag<detail::shared_namespace_tag>{});

        detail::default_encode_table table;
        detail::encode_table_ref     table_ref{table};
        detail::active_table_scope   scope{active_encode_table_, table_ref};
        enc.encode(root.value_);
    }

    template <typename T> [[nodiscard]] status_code decode(shared_ptr_root<T> root) {
        auto &dec = static_cast<Self &>(*this);

        const auto tag_status = dec.decode(static_tag<detail::shared_namespace_tag>{});
        if (tag_status != status_code::success) {
            return tag_status;
        }

        detail::default_decode_table table;
        detail::decode_table_ref     table_ref{table};
        detail::active_table_scope   scope{active_decode_table_, table_ref};
        return dec.decode(root.value_);
    }

    template <typename T, SharedPtrEncodeTable Table> void encode(shared_ptr_unscoped_root<T, Table> root) {
        detail::encode_table_ref   table_ref{root.table_};
        detail::active_table_scope scope{active_encode_table_, table_ref};
        static_cast<Self &>(*this).encode(root.value_);
    }

    template <typename T, SharedPtrDecodeTable Table> [[nodiscard]] status_code decode(shared_ptr_unscoped_root<T, Table> root) {
        detail::decode_table_ref   table_ref{root.table_};
        detail::active_table_scope scope{active_decode_table_, table_ref};
        return static_cast<Self &>(*this).decode(root.value_);
    }

    template <detail::PointerValue T> void encode(const std::shared_ptr<T> &value) {
        if (active_encode_table_ == nullptr) {
            throw cbor::tags::detail::encode_status_exception{status_code::error};
        }

        auto &enc = static_cast<Self &>(*this);
        if (!value) {
            enc.encode(nullptr);
            return;
        }

        const auto erased = std::shared_ptr<const void>{value, static_cast<const void *>(value.get())};
        const auto key =
            shared_ptr_encode_key{std::weak_ptr<const void>{erased}, static_cast<const void *>(value.get()), detail::graph_type_id<T>()};
        auto observation = active_encode_table_->observe(key);
        if (!observation) {
            throw cbor::tags::detail::encode_status_exception{observation.error()};
        }

        if (observation->kind == shared_ptr_observation_kind::reference) {
            enc.encode(static_tag<detail::sharedref_tag>{});
            enc.encode(observation->index);
            return;
        }

        enc.encode(static_tag<detail::shareable_tag>{});
        enc.encode(*value);
        active_encode_table_->mark_complete(observation->index);
    }

    template <detail::PointerValue T>
        requires std::default_initializable<T>
    [[nodiscard]] status_code decode(std::shared_ptr<T> &value, major_type major, std::byte additional_info) {
        if (active_decode_table_ == nullptr) {
            return status_code::error;
        }

        if (major == major_type::Simple && additional_info == static_cast<std::byte>(SimpleType::Null)) {
            value.reset();
            return status_code::success;
        }
        if (major != major_type::Tag) {
            return status_code::no_match_for_tag_on_buffer;
        }

        std::uint64_t tag{};
        auto          status = cbor::tags::detail::decode_unsigned_argument(static_cast<Self &>(*this), additional_info, tag);
        if (status != status_code::success) {
            return status;
        }
        return decode_shared_pointer_after_tag(value, tag);
    }

    template <typename T>
        requires(detail::has_pointer_null_wire_v<detail::has_unique_ptr_codec_v<Self>, true, T>)
    void encode(const std::optional<T> &) {
        static_assert(always_false<T>::value,
                      "std::optional<T> cannot contain a smart pointer null state because both empty states use CBOR null");
    }

    template <typename T>
        requires(detail::has_pointer_null_wire_v<detail::has_unique_ptr_codec_v<Self>, true, T>)
    [[nodiscard]] status_code decode(std::optional<T> &, major_type, std::byte) {
        static_assert(always_false<T>::value,
                      "std::optional<T> cannot contain a smart pointer null state because both empty states use CBOR null");
        return status_code::error;
    }

    template <IsVariant Variant>
        requires(detail::contains_decodable_shared_pointer_v<Variant> ||
                 (detail::has_unique_ptr_codec_v<Self> && detail::contains_decodable_unique_pointer_v<Variant>))
    [[nodiscard]] status_code decode(Variant &value, major_type major, std::byte additional_info) {
        if (detail::contains_decodable_shared_pointer_v<Variant> && active_decode_table_ == nullptr) {
            return status_code::error;
        }

        std::optional<std::uint64_t> tag;
        return detail::decode_pointer_variant<detail::has_unique_ptr_codec_v<Self>, true>(static_cast<Self &>(*this), value, major,
                                                                                          additional_info, tag);
    }

    template <detail::PointerValue T>
        requires std::default_initializable<T>
    [[nodiscard]] status_code decode_shared_pointer_after_tag(std::shared_ptr<T> &value, std::uint64_t tag) {
        if (active_decode_table_ == nullptr) {
            return status_code::error;
        }
        if (tag == detail::shareable_tag) {
            return decode_shareable(value);
        }
        if (tag == detail::sharedref_tag) {
            return decode_sharedref(value);
        }
        return status_code::no_match_for_tag;
    }

    template <bool AllowUnique, bool AllowShared, IsVariant Variant>
    [[nodiscard]] status_code decode_pointer_variant_impl(Variant &value, major_type major, std::byte additional_info,
                                                          std::optional<std::uint64_t> &tag) {
        return detail::decode_pointer_variant<AllowUnique, AllowShared>(static_cast<Self &>(*this), value, major, additional_info, tag);
    }

  private:
    template <detail::PointerValue T>
        requires std::default_initializable<T>
    [[nodiscard]] status_code decode_shareable(std::shared_ptr<T> &value) {
        auto decoded = std::make_shared<T>();
        value        = decoded;

        auto inserted = active_decode_table_->insert(
            shared_ptr_decode_entry{std::shared_ptr<void>{decoded}, detail::graph_type_id<T>(), shared_ptr_entry_state::encoding});
        if (!inserted) {
            return inserted.error();
        }

        const auto status = static_cast<Self &>(*this).decode(*decoded);
        if (status != status_code::success) {
            return status;
        }

        active_decode_table_->mark_complete(*inserted);
        return status_code::success;
    }

    template <detail::PointerValue T>
        requires std::default_initializable<T>
    [[nodiscard]] status_code decode_sharedref(std::shared_ptr<T> &value) {
        std::uint64_t index{};
        const auto    status = static_cast<Self &>(*this).decode(index);
        if (status != status_code::success) {
            return status;
        }

        auto resolved = active_decode_table_->resolve(index);
        if (!resolved) {
            return resolved.error();
        }
        if (resolved->state != shared_ptr_entry_state::complete || resolved->type != detail::graph_type_id<T>()) {
            return status_code::error;
        }

        value = std::static_pointer_cast<T>(resolved->value);
        return status_code::success;
    }

    detail::encode_table_ref *active_encode_table_{};
    detail::decode_table_ref *active_decode_table_{};
};

} // namespace cbor::tags::ext::smart_ptr

namespace cbor::tags::cddl {

template <typename T> struct cddl_scope_traits<ext::smart_ptr::shared_ptr_cddl<T>> {
    using value_type = typename ext::smart_ptr::shared_ptr_cddl<T>::value_type;

    static constexpr cddl_shared_pointer_mode shared_pointer_mode  = cddl_shared_pointer_mode::shared_graph;
    static constexpr bool                     tag_shared_namespace = true;
};

template <typename T> struct cddl_scope_traits<ext::smart_ptr::shared_ptr_unscoped_cddl<T>> {
    using value_type = typename ext::smart_ptr::shared_ptr_unscoped_cddl<T>::value_type;

    static constexpr cddl_shared_pointer_mode shared_pointer_mode  = cddl_shared_pointer_mode::shared_graph;
    static constexpr bool                     tag_shared_namespace = false;
};

} // namespace cbor::tags::cddl
