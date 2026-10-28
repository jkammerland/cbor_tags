#pragma once

#include "fixtures.h"

#include <cbor_tags/cbor_traversal.h>

#ifndef CBOR_TAGS_BENCHMARK_ONE_SHOT_ONLY
#include <cbor_tags/cbor_resumable.h>
#include <list>
#endif

namespace resumable_bench {

template <typename T> bool decode_one_shot(const bytes &encoded, T &destination) {
    auto decoder = ct::make_decoder(encoded);
    return static_cast<bool>(decoder(destination)) && decoder.tell() == encoded.end();
}

template <typename T> bool encode_one_shot(const T &value, bytes &encoded) {
    auto output = std::span<std::byte>{encoded};
    return static_cast<bool>(ct::make_encoder(output)(value));
}

inline bool validate_one_shot(const bytes &encoded) {
    auto decoder = ct::make_decoder(encoded);
    return static_cast<bool>(ct::validate_item(decoder)) && decoder.tell() == encoded.end();
}

#ifndef CBOR_TAGS_BENCHMARK_ONE_SHOT_ONLY
template <typename T> bool decode_resumable(const bytes &encoded, T &destination, std::size_t chunk_size) {
    std::list<std::span<const std::byte>> descriptors;
    auto                                  source    = ct::make_input_source(descriptors);
    auto                                  decoder   = ct::make_resumable_decoder(source);
    auto                                  operation = decoder(destination);
    for (std::size_t offset = 0; offset < encoded.size(); offset += chunk_size) {
        operation(std::span<const std::byte>{encoded}.subspan(offset, std::min(chunk_size, encoded.size() - offset)));
    }
    const auto result = operation.result();
    return operation.done() && result && *result == encoded.size();
}

template <typename T> bool encode_resumable(const T &value, bytes &encoded, std::size_t chunk_size) {
    std::list<std::span<std::byte>> descriptors;
    auto                            sink      = ct::make_output_source(descriptors);
    auto                            encoder   = ct::make_resumable_encoder(sink);
    auto                            operation = encoder(value);
    for (std::size_t offset = 0; offset < encoded.size(); offset += chunk_size) {
        operation(std::span<std::byte>{encoded}.subspan(offset, std::min(chunk_size, encoded.size() - offset)));
    }
    const auto result = operation.result();
    return operation.done() && result && *result == encoded.size();
}

inline bool validate_resumable(const bytes &encoded, std::size_t chunk_size) {
    std::list<std::span<const std::byte>> descriptors;
    auto                                  source    = ct::make_input_source(descriptors);
    auto                                  operation = ct::make_resumable_validator(source);
    for (std::size_t offset = 0; offset < encoded.size(); offset += chunk_size) {
        operation(std::span<const std::byte>{encoded}.subspan(offset, std::min(chunk_size, encoded.size() - offset)));
    }
    const auto result = operation.result();
    return operation.done() && result && *result == encoded.size();
}
#endif

} // namespace resumable_bench
