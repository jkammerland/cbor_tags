#pragma once

#include "cbor_tags/cwt/thumbprint.h"

#include <memory>
#include <openssl/evp.h>

namespace cbor::tags::cwt {

struct openssl_sha256 {
    template <std::ranges::input_range Parts> static expected<void, status_code> digest(Parts &&parts, std::span<std::byte, 32> output) {
        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx{EVP_MD_CTX_new(), EVP_MD_CTX_free};
        if (!ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1)
            return unexpected<status_code>{status_code::error};
        for (auto bytes : parts) {
            if (!bytes.empty() && EVP_DigestUpdate(ctx.get(), bytes.data(), bytes.size()) != 1)
                return unexpected<status_code>{status_code::error};
        }
        unsigned int size{};
        if (EVP_DigestFinal_ex(ctx.get(), reinterpret_cast<unsigned char *>(output.data()), &size) != 1 || size != 32)
            return unexpected<status_code>{status_code::error};
        return {};
    }
};

} // namespace cbor::tags::cwt
