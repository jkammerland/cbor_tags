#pragma once

#include "cbor_tags/detail/smart_ptr_decode.h"
#include "cbor_tags/smart_ptr/types.h"

namespace cbor::tags::codec {

template <typename Self> struct shared_ptr : base<Self> {
    using base<Self>::decode;
    using base<Self>::encode;

    template <smart_ptr::SharedPtrEncodeScope Scope, typename S = Self>
        requires smart_ptr::detail::EncoderSelf<S>
    void set_shared_ptr_scope(Scope &scope) {
        external_encode_scope_.emplace(scope);
    }

    template <smart_ptr::SharedPtrDecodeScope Scope, typename S = Self>
        requires smart_ptr::detail::DecoderSelf<S>
    void set_shared_ptr_scope(Scope &scope) {
        external_decode_scope_.emplace(scope);
    }

    template <typename S = Self>
        requires smart_ptr::detail::EncoderSelf<S>
    void use_default_shared_ptr_scope() {
        external_encode_scope_.reset();
    }

    template <typename S = Self>
        requires smart_ptr::detail::DecoderSelf<S>
    void use_default_shared_ptr_scope() {
        external_decode_scope_.reset();
    }

    // MSVC 19.50 miscompiles reset through the temporary erased wrapper returned by current_*_scope() in optimized builds.
    template <typename S = Self>
        requires smart_ptr::detail::EncoderSelf<S>
    void reset_shared_ptr_scope() {
        if (external_encode_scope_) {
            external_encode_scope_->reset();
        } else {
            default_encode_scope_.reset();
        }
    }

    template <typename S = Self>
        requires smart_ptr::detail::DecoderSelf<S>
    void reset_shared_ptr_scope() {
        if (external_decode_scope_) {
            external_decode_scope_->reset();
        } else {
            default_decode_scope_.reset();
        }
    }

    template <typename S = Self>
        requires smart_ptr::detail::EncoderSelf<S>
    void observe_encoded_cbor_tag(std::uint64_t tag) {
        if (tag != smart_ptr::detail::shareable_tag) {
            return;
        }
        auto observed = current_encode_scope().observe_untracked();
        if (!observed) {
            throw tags::detail::encode_status_exception{observed.error()};
        }
    }

    template <typename S = Self>
        requires smart_ptr::detail::DecoderSelf<S>
    [[nodiscard]] status_code observe_decoded_cbor_tag(std::uint64_t tag) {
        if (tag != smart_ptr::detail::shareable_tag) {
            return status_code::success;
        }
        auto inserted = current_decode_scope().insert_untracked();
        return inserted ? status_code::success : inserted.error();
    }

    template <smart_ptr::IsSharedPointer Pointer> void encode(const Pointer &value) { encode_shared_pointer(value); }

    template <smart_ptr::IsSharedPointer Pointer> void encode(const smart_ptr::scoped_shared_ptr<Pointer> &value) {
        encode_shared_pointer(value.value());
    }

    template <smart_ptr::IsSharedPointer Pointer>
        requires std::default_initializable<typename std::remove_cvref_t<Pointer>::element_type>
    [[nodiscard]] status_code decode(Pointer &value, major_type major, std::byte additional_info) {
        return decode_shared_pointer(value, major, additional_info);
    }

    template <smart_ptr::IsSharedPointer Pointer>
        requires std::default_initializable<typename std::remove_cvref_t<Pointer>::element_type>
    [[nodiscard]] status_code decode(smart_ptr::scoped_shared_ptr<Pointer> &value, major_type major, std::byte additional_info) {
        return decode_shared_pointer(value.value(), major, additional_info);
    }

    template <typename T>
        requires(smart_ptr::detail::has_pointer_null_wire_v<false, true, T> && !smart_ptr::detail::has_pointer_null_wire_v<true, false, T>)
    void encode(const std::optional<T> &) {
        static_assert(always_false<T>::value,
                      "std::optional<T> cannot contain a smart pointer null state because both empty states use CBOR null");
    }

    template <typename T>
        requires(smart_ptr::detail::has_pointer_null_wire_v<false, true, T> && !smart_ptr::detail::has_pointer_null_wire_v<true, false, T>)
    [[nodiscard]] status_code decode(std::optional<T> &, major_type, std::byte) {
        static_assert(always_false<T>::value,
                      "std::optional<T> cannot contain a smart pointer null state because both empty states use CBOR null");
        return status_code::error;
    }

    template <IsVariant Variant>
        requires smart_ptr::detail::contains_shared_pointer_v<Variant>
    void encode(const Variant &) {
        static_assert(always_false<Variant>::value, "variants containing shared pointers require an explicit application codec");
    }

    template <IsVariant Variant>
        requires smart_ptr::detail::contains_shared_pointer_v<Variant>
    [[nodiscard]] status_code decode(Variant &, major_type, std::byte) {
        static_assert(always_false<Variant>::value, "variants containing shared pointers require an explicit application codec");
        return status_code::error;
    }

  private:
    template <smart_ptr::IsSharedPointer Pointer> void encode_shared_pointer(const Pointer &value) {
        using pointer_type = std::remove_cvref_t<Pointer>;
        using element_type = typename pointer_type::element_type;
        static_assert(smart_ptr::detail::encodes_one_cbor_item<typename Self::options, element_type>(),
                      "smart pointer pointee must encode exactly one CBOR item");

        auto &enc = static_cast<Self &>(*this);
        if (!value) {
            enc.encode(nullptr);
            return;
        }

        auto scope       = current_encode_scope();
        auto observation = scope.observe(
            smart_ptr::shared_ptr_encode_key{static_cast<const void *>(value.get()), smart_ptr::detail::graph_type_id<pointer_type>()});
        if (!observation) {
            throw tags::detail::encode_status_exception{observation.error()};
        }
        if (observation->kind == smart_ptr::shared_ptr_observation_kind::reference) {
            enc.encode(static_tag<smart_ptr::detail::sharedref_tag>{});
            if (!std::in_range<std::uint64_t>(observation->index)) {
                throw tags::detail::encode_status_exception{status_code::size_limit_exceeded};
            }
            enc.encode(static_cast<std::uint64_t>(observation->index));
            return;
        }

        // observe() already reserved this tag 28 index.
        enc.encode_major_and_size(smart_ptr::detail::shareable_tag, static_cast<typename Self::byte_type>(0xC0));
        enc.encode(*value);
        scope.mark_complete(observation->index);
    }

    template <smart_ptr::IsSharedPointer Pointer>
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
        const auto status = tags::detail::decode_unsigned_argument(static_cast<Self &>(*this), additional_info, tag);
        if (status != status_code::success) {
            return status;
        }
        if (tag == smart_ptr::detail::shareable_tag) {
            return decode_shareable(value);
        }
        if (tag == smart_ptr::detail::sharedref_tag) {
            return decode_sharedref(value);
        }
        return status_code::no_match_for_tag;
    }

    template <smart_ptr::IsSharedPointer Pointer>
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
            smart_ptr::detail::reset_pointer_to_new(value);
            stored = std::make_shared<pointer_type>(value);
        }

        auto scope    = current_decode_scope();
        auto inserted = scope.insert(smart_ptr::shared_ptr_decode_entry{std::move(stored), smart_ptr::detail::graph_type_id<pointer_type>(),
                                                                        smart_ptr::shared_ptr_entry_state::encoding});
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

    template <smart_ptr::IsSharedPointer Pointer> [[nodiscard]] status_code decode_sharedref(Pointer &value) {
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
        if (resolved->state != smart_ptr::shared_ptr_entry_state::complete ||
            resolved->pointer_type != smart_ptr::detail::graph_type_id<pointer_type>()) {
            return status_code::error;
        }

        if constexpr (std::constructible_from<pointer_type, std::shared_ptr<typename pointer_type::element_type>>) {
            value = pointer_type{std::static_pointer_cast<typename pointer_type::element_type>(resolved->pointer)};
        } else {
            value = *std::static_pointer_cast<pointer_type>(resolved->pointer);
        }
        return status_code::success;
    }

    [[nodiscard]] smart_ptr::detail::encode_scope_ref current_encode_scope() {
        if (external_encode_scope_) {
            return *external_encode_scope_;
        }
        return smart_ptr::detail::encode_scope_ref{default_encode_scope_};
    }

    [[nodiscard]] smart_ptr::detail::decode_scope_ref current_decode_scope() {
        if (external_decode_scope_) {
            return *external_decode_scope_;
        }
        return smart_ptr::detail::decode_scope_ref{default_decode_scope_};
    }

    smart_ptr::shared_ptr_encode_scope                 default_encode_scope_{};
    smart_ptr::shared_ptr_decode_scope                 default_decode_scope_{};
    std::optional<smart_ptr::detail::encode_scope_ref> external_encode_scope_{};
    std::optional<smart_ptr::detail::decode_scope_ref> external_decode_scope_{};
};

} // namespace cbor::tags::codec
