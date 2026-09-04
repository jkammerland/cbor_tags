#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_extensions.h"
#include "cbor_tags/detail/cbor_encode_error.h"
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

} // namespace cbor::tags::ext::smart_ptr

#include "cbor_tags/detail/smart_ptr_decode.h"

namespace cbor::tags::ext::smart_ptr {

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

    // MSVC 19.50 miscompiles reset through the temporary erased wrapper returned by current_*_scope() in optimized builds.
    template <typename S = Self>
        requires detail::EncoderSelf<S>
    void reset_shared_ptr_scope() {
        if (external_encode_scope_) {
            external_encode_scope_->reset();
        } else {
            default_encode_scope_.reset();
        }
    }

    template <typename S = Self>
        requires detail::DecoderSelf<S>
    void reset_shared_ptr_scope() {
        if (external_decode_scope_) {
            external_decode_scope_->reset();
        } else {
            default_decode_scope_.reset();
        }
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
