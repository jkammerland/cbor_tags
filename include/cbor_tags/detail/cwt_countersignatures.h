#pragma once

#include "cbor_tags/cwt/countersignature_types.h"
#include "cbor_tags/detail/cbor_encode_error.h"
#include "cbor_tags/detail/cwt_codec.h"

namespace cbor::tags::cwt::detail {

template <typename Self> struct countersignature_codec {

    template <typename Signature, bool Legacy> void encode(const countersignature_collection<Signature, Legacy> &value) {
        auto &enc = static_cast<Self &>(*this);
        if (Legacy || value.signatures.empty()) {
            throw tags::detail::encode_status_exception{status_code::error};
        }
        if (value.signatures.size() == 1) {
            enc.encode(value.signatures.front());
        } else {
            enc.encode(value.signatures);
        }
    }

    template <typename Signature, bool Legacy> status_code decode(countersignature_collection<Signature, Legacy> &value) {
        auto      &dec = static_cast<Self &>(*this);
        major_type major{};
        std::byte  info{};
        auto       status = tags::detail::read_initial_byte(dec, major, info);
        return status == status_code::success ? decode(value, major, info) : status;
    }

    template <typename Signature, bool Legacy>
    status_code decode(countersignature_collection<Signature, Legacy> &value, major_type major, std::byte info) try {
        auto                                          &dec = static_cast<Self &>(*this);
        countersignature_collection<Signature, Legacy> decoded;
        bool                                           singleton = false;
        std::size_t                                    index     = 0;
        Signature                                      single{};

        // The first element distinguishes a signature's bstr from a list's array.
        auto status = decode_group_entries(dec, major, info, major_type::Array, status_code::no_match_for_array_on_buffer,
                                           [&](major_type child_major, std::byte child_info) {
                                               if (index == 0) {
                                                   singleton = child_major != major_type::Array;
                                               }
                                               status_code result{};
                                               if (!singleton) {
                                                   Signature signature{};
                                                   result = dec.decode(signature, child_major, child_info);
                                                   if (result == status_code::success) {
                                                       decoded.signatures.push_back(std::move(signature));
                                                   }
                                               } else if (index == 0) {
                                                   result = dec.decode(single.protected_header, child_major, child_info);
                                               } else if (index == 1) {
                                                   result = dec.decode(single.unprotected, child_major, child_info);
                                               } else if (index == 2) {
                                                   result = dec.decode(single.signature, child_major, child_info);
                                               } else {
                                                   return status_code::unexpected_group_size;
                                               }
                                               ++index;
                                               return result;
                                           });
        if (status != status_code::success) {
            return status;
        }
        if (index == 0 || (singleton && index != 3)) {
            return status_code::unexpected_group_size;
        }
        if (singleton) {
            decoded.signatures.push_back(std::move(single));
        }
        value = std::move(decoded);
        return status_code::success;
    } catch (const tags::detail::decode_status_exception &error) { return error.status; } catch (const tags::parse_incomplete_exception &) {
        return status_code::incomplete;
    }
};

} // namespace cbor::tags::cwt::detail
