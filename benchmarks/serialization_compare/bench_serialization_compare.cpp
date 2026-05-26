#include "comparison_generated.h"

#include <algorithm>
#include <array>
#include <bit>
#include <bitsery/adapter/buffer.h>
#include <bitsery/bitsery.h>
#include <bitsery/deserializer.h>
#include <bitsery/serializer.h>
#include <bitsery/traits/string.h>
#include <bitsery/traits/vector.h>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/serialization/split_free.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_ranges.h>
#include <cereal/archives/binary.hpp>
#include <cereal/cereal.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <flatbuffers/flatbuffers.h>
#include <fstream>
#include <glaze/cbor.hpp>
#include <ios>
#include <iostream>
#include <limits>
#include <nanobench.h>
#include <new>
#include <ostream>
#include <ranges>
#include <span>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#include <zpp_bits.h>

#ifndef CBOR_TAGS_BENCH_BUILD_TYPE
#define CBOR_TAGS_BENCH_BUILD_TYPE "unknown"
#endif

#ifndef CBOR_TAGS_BENCH_BITSERY_VERSION
#define CBOR_TAGS_BENCH_BITSERY_VERSION "unknown"
#endif

#ifndef CBOR_TAGS_BENCH_ZPP_BITS_VERSION
#define CBOR_TAGS_BENCH_ZPP_BITS_VERSION "unknown"
#endif

#ifndef CBOR_TAGS_BENCH_CEREAL_VERSION
#define CBOR_TAGS_BENCH_CEREAL_VERSION "unknown"
#endif

#ifndef CBOR_TAGS_BENCH_FLATBUFFERS_VERSION
#define CBOR_TAGS_BENCH_FLATBUFFERS_VERSION "unknown"
#endif

#ifndef CBOR_TAGS_BENCH_GLAZE_VERSION
#define CBOR_TAGS_BENCH_GLAZE_VERSION "unknown"
#endif

namespace bench_compare {

using byte_buffer = std::vector<std::uint8_t>;

namespace allocation_probe {
inline thread_local bool        enabled = false;
inline thread_local std::size_t count   = 0;
inline thread_local std::size_t bytes   = 0;

void reset() noexcept {
    count = 0;
    bytes = 0;
}

void record(std::size_t size) noexcept {
    if (enabled) {
        ++count;
        bytes += size;
    }
}
} // namespace allocation_probe

void *allocate_bytes(std::size_t size) {
    if (size == 0) {
        size = 1;
    }
    allocation_probe::record(size);
    if (auto *ptr = std::malloc(size); ptr != nullptr) {
        return ptr;
    }
    throw std::bad_alloc{};
}

void *allocate_aligned_bytes(std::size_t size, std::align_val_t alignment) {
    if (size == 0) {
        size = 1;
    }
    const auto align = static_cast<std::size_t>(alignment);
    allocation_probe::record(size);
    void *ptr = nullptr;
    if (posix_memalign(&ptr, align < sizeof(void *) ? sizeof(void *) : align, size) == 0) {
        return ptr;
    }
    throw std::bad_alloc{};
}

} // namespace bench_compare

void *operator new(std::size_t size) { return bench_compare::allocate_bytes(size); }
void *operator new[](std::size_t size) { return bench_compare::allocate_bytes(size); }
void *operator new(std::size_t size, std::align_val_t alignment) { return bench_compare::allocate_aligned_bytes(size, alignment); }
void *operator new[](std::size_t size, std::align_val_t alignment) { return bench_compare::allocate_aligned_bytes(size, alignment); }
void  operator delete(void *ptr) noexcept { std::free(ptr); }
void  operator delete[](void *ptr) noexcept { std::free(ptr); }
void  operator delete(void *ptr, std::size_t) noexcept { std::free(ptr); }
void  operator delete[](void *ptr, std::size_t) noexcept { std::free(ptr); }
void  operator delete(void *ptr, std::align_val_t) noexcept { std::free(ptr); }
void  operator delete[](void *ptr, std::align_val_t) noexcept { std::free(ptr); }
void  operator delete(void *ptr, std::size_t, std::align_val_t) noexcept { std::free(ptr); }
void  operator delete[](void *ptr, std::size_t, std::align_val_t) noexcept { std::free(ptr); }

namespace bench_compare {

struct allocation_sample {
    std::size_t count{};
    std::size_t bytes{};
};

class allocation_guard {
  public:
    allocation_guard() {
        allocation_probe::reset();
        allocation_probe::enabled = true;
    }

    allocation_guard(const allocation_guard &)            = delete;
    allocation_guard &operator=(const allocation_guard &) = delete;

    ~allocation_guard() { allocation_probe::enabled = false; }

    allocation_sample sample() const noexcept { return {.count = allocation_probe::count, .bytes = allocation_probe::bytes}; }

    void stop() noexcept { allocation_probe::enabled = false; }
};

template <typename Fn> allocation_sample count_allocations(Fn &&fn) {
    allocation_guard guard;
    auto             result = std::forward<Fn>(fn)();
    ankerl::nanobench::doNotOptimizeAway(result);
    auto sample = guard.sample();
    guard.stop();
    return sample;
}

struct FlatRecord {
    std::int32_t              id{};
    std::uint64_t             sequence{};
    bool                      active{};
    std::uint8_t              kind{};
    float                     temperature{};
    double                    score{};
    std::string               name;
    std::vector<std::int32_t> samples;

    bool operator==(const FlatRecord &) const = default;

    template <typename Encoder> constexpr auto encode(Encoder &enc) const {
        return enc(cbor::tags::wrap_as_array{id, sequence, active, kind, temperature, score, name, samples});
    }

    template <typename Decoder> constexpr auto decode(Decoder &dec) {
        return dec(cbor::tags::wrap_as_array{id, sequence, active, kind, temperature, score, name, samples});
    }
};

struct KeyValue {
    std::string  key;
    std::int64_t value{};

    bool operator==(const KeyValue &) const = default;

    template <typename Encoder> constexpr auto encode(Encoder &enc) const { return enc(cbor::tags::wrap_as_array{key, value}); }

    template <typename Decoder> constexpr auto decode(Decoder &dec) { return dec(cbor::tags::wrap_as_array{key, value}); }
};

struct RecordBatch {
    std::vector<FlatRecord> records;

    bool operator==(const RecordBatch &) const = default;

    template <typename Encoder> constexpr auto encode(Encoder &enc) const { return enc(cbor::tags::wrap_as_array{records}); }

    template <typename Decoder> constexpr auto decode(Decoder &dec) { return dec(cbor::tags::wrap_as_array{records}); }
};

struct NestedSnapshot {
    std::string             title;
    std::vector<FlatRecord> records;
    std::vector<KeyValue>   counters;
    byte_buffer             payload;

    bool operator==(const NestedSnapshot &) const = default;

    template <typename Encoder> constexpr auto encode(Encoder &enc) const {
        return enc(cbor::tags::wrap_as_array{title, records, counters, payload});
    }

    template <typename Decoder> constexpr auto decode(Decoder &dec) {
        return dec(cbor::tags::wrap_as_array{title, records, counters, payload});
    }
};

std::uint64_t mix(std::uint64_t seed, std::uint64_t value) { return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U)); }

std::uint64_t mix_string(std::uint64_t seed, std::string_view value) {
    seed = mix(seed, value.size());
    for (unsigned char c : value) {
        seed = mix(seed, c);
    }
    return seed;
}

template <typename T> std::uint64_t bit_pattern(T value) {
    if constexpr (sizeof(T) == sizeof(std::uint32_t)) {
        return std::bit_cast<std::uint32_t>(value);
    } else {
        return std::bit_cast<std::uint64_t>(value);
    }
}

std::uint64_t checksum(const FlatRecord &value) {
    auto seed = std::uint64_t{0x123456789abcdef0ULL};
    seed      = mix(seed, static_cast<std::uint32_t>(value.id));
    seed      = mix(seed, value.sequence);
    seed      = mix(seed, value.active ? 1U : 0U);
    seed      = mix(seed, value.kind);
    seed      = mix(seed, bit_pattern(value.temperature));
    seed      = mix(seed, bit_pattern(value.score));
    seed      = mix_string(seed, value.name);
    seed      = mix(seed, value.samples.size());
    for (auto item : value.samples) {
        seed = mix(seed, static_cast<std::uint32_t>(item));
    }
    return seed;
}

std::uint64_t checksum(const KeyValue &value) {
    auto seed = std::uint64_t{0x0fedcba987654321ULL};
    seed      = mix_string(seed, value.key);
    seed      = mix(seed, static_cast<std::uint64_t>(value.value));
    return seed;
}

std::uint64_t checksum(const RecordBatch &value) {
    auto seed = std::uint64_t{0x1020304050607080ULL};
    seed      = mix(seed, value.records.size());
    for (const auto &record : value.records) {
        seed = mix(seed, checksum(record));
    }
    return seed;
}

std::uint64_t checksum(const NestedSnapshot &value) {
    auto seed = std::uint64_t{0x8877665544332211ULL};
    seed      = mix_string(seed, value.title);
    seed      = mix(seed, value.records.size());
    for (const auto &record : value.records) {
        seed = mix(seed, checksum(record));
    }
    seed = mix(seed, value.counters.size());
    for (const auto &counter : value.counters) {
        seed = mix(seed, checksum(counter));
    }
    seed = mix(seed, value.payload.size());
    for (auto byte : value.payload) {
        seed = mix(seed, byte);
    }
    return seed;
}

template <typename T>
    requires(std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::int64_t> || std::is_same_v<T, double>)
std::uint64_t checksum_numeric_values(std::span<const T> value) {
    auto seed = std::uint64_t{0x33445566778899aaULL};
    seed      = mix(seed, sizeof(T));
    seed      = mix(seed, value.size());
    for (auto item : value) {
        if constexpr (std::is_floating_point_v<T>) {
            seed = mix(seed, bit_pattern(item));
        } else {
            seed = mix(seed, static_cast<std::make_unsigned_t<T>>(item));
        }
    }
    return seed;
}

template <typename T>
    requires(std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::int64_t> || std::is_same_v<T, double>)
std::uint64_t checksum(const std::vector<T> &value) {
    return checksum_numeric_values(std::span<const T>{value.data(), value.size()});
}

FlatRecord make_flat_record(int index) {
    FlatRecord value;
    value.id          = (index * 17) - 11;
    value.sequence    = 0x100000000ULL + static_cast<std::uint64_t>(index) * 977ULL;
    value.active      = (index % 3) != 0;
    value.kind        = static_cast<std::uint8_t>(index % 4);
    value.temperature = 18.5F + static_cast<float>(index) * 0.125F;
    value.score       = (1000.0 / static_cast<double>(index + 3)) + static_cast<double>(index) * 1.25;
    value.name        = "sensor-" + std::to_string(index % 32) + "-zone-" + std::to_string(index % 5);
    value.samples.reserve(12);
    for (int sample = 0; sample < 12; ++sample) {
        value.samples.push_back((index * 100) + (sample * sample) - 3);
    }
    return value;
}

RecordBatch make_record_batch() {
    RecordBatch value;
    value.records.reserve(128);
    for (int index = 0; index < 128; ++index) {
        value.records.push_back(make_flat_record(index));
    }
    return value;
}

NestedSnapshot make_nested_snapshot() {
    NestedSnapshot value;
    value.title = "range-comparison-snapshot";
    value.records.reserve(64);
    for (int index = 0; index < 64; ++index) {
        value.records.push_back(make_flat_record(index + 200));
    }
    value.counters.reserve(24);
    for (int index = 0; index < 24; ++index) {
        value.counters.push_back(KeyValue{.key = "counter-" + std::to_string(index), .value = (index * 1009) - 17});
    }
    value.payload.resize(2048);
    for (std::size_t index = 0; index < value.payload.size(); ++index) {
        value.payload[index] = static_cast<std::uint8_t>((index * 131U + 7U) & 0xffU);
    }
    return value;
}

std::vector<std::int32_t> make_int32_series() {
    std::vector<std::int32_t> value;
    value.reserve(4096);
    for (std::int32_t index = 0; index < 4096; ++index) {
        value.push_back((index * 17) - ((index % 11) * 409));
    }
    return value;
}

std::vector<std::int64_t> make_int64_series() {
    std::vector<std::int64_t> value;
    value.reserve(4096);
    for (std::int64_t index = 0; index < 4096; ++index) {
        value.push_back((index * 0x100000001LL) - ((index % 19) * 0x12345LL));
    }
    return value;
}

std::vector<double> make_float64_series() {
    std::vector<double> value;
    value.reserve(4096);
    for (int index = 0; index < 4096; ++index) {
        value.push_back((static_cast<double>(index) * 0.5) + (1.0 / static_cast<double>((index % 31) + 1)));
    }
    return value;
}

class byte_input_streambuf : public std::streambuf {
  public:
    explicit byte_input_streambuf(const byte_buffer &buffer) {
        auto *begin = reinterpret_cast<char *>(const_cast<std::uint8_t *>(buffer.data()));
        setg(begin, begin, begin + static_cast<std::ptrdiff_t>(buffer.size()));
    }
};

class byte_input_stream : public std::istream {
  public:
    explicit byte_input_stream(const byte_buffer &buffer) : std::istream(nullptr), buffer_(buffer) { rdbuf(&buffer_); }

  private:
    byte_input_streambuf buffer_;
};

} // namespace bench_compare

namespace boost::serialization {

template <typename Archive> void save(Archive &archive, const bench_compare::FlatRecord &value, const unsigned int) {
    archive & value.id;
    archive & value.sequence;
    archive & value.active;
    archive & value.kind;
    archive & value.temperature;
    archive & value.score;
    archive & value.name;
    archive & value.samples;
}

template <typename Archive> void load(Archive &archive, bench_compare::FlatRecord &value, const unsigned int) {
    archive & value.id;
    archive & value.sequence;
    archive & value.active;
    archive & value.kind;
    archive & value.temperature;
    archive & value.score;
    archive & value.name;
    archive & value.samples;
}

template <typename Archive> void serialize(Archive &archive, bench_compare::FlatRecord &value, const unsigned int version) {
    split_free(archive, value, version);
}

template <typename Archive> void save(Archive &archive, const bench_compare::KeyValue &value, const unsigned int) {
    archive & value.key;
    archive & value.value;
}

template <typename Archive> void load(Archive &archive, bench_compare::KeyValue &value, const unsigned int) {
    archive & value.key;
    archive & value.value;
}

template <typename Archive> void serialize(Archive &archive, bench_compare::KeyValue &value, const unsigned int version) {
    split_free(archive, value, version);
}

template <typename Archive> void save(Archive &archive, const bench_compare::RecordBatch &value, const unsigned int) {
    archive & value.records;
}

template <typename Archive> void load(Archive &archive, bench_compare::RecordBatch &value, const unsigned int) { archive & value.records; }

template <typename Archive> void serialize(Archive &archive, bench_compare::RecordBatch &value, const unsigned int version) {
    split_free(archive, value, version);
}

template <typename Archive> void save(Archive &archive, const bench_compare::NestedSnapshot &value, const unsigned int) {
    archive & value.title;
    archive & value.records;
    archive & value.counters;
    archive & value.payload;
}

template <typename Archive> void load(Archive &archive, bench_compare::NestedSnapshot &value, const unsigned int) {
    archive & value.title;
    archive & value.records;
    archive & value.counters;
    archive & value.payload;
}

template <typename Archive> void serialize(Archive &archive, bench_compare::NestedSnapshot &value, const unsigned int version) {
    split_free(archive, value, version);
}

} // namespace boost::serialization

template <> struct glz::meta<bench_compare::FlatRecord> {
    using T                     = bench_compare::FlatRecord;
    static constexpr auto value = object("id", &T::id, "sequence", &T::sequence, "active", &T::active, "kind", &T::kind, "temperature",
                                         &T::temperature, "score", &T::score, "name", &T::name, "samples", &T::samples);
};

template <> struct glz::meta<bench_compare::KeyValue> {
    using T                     = bench_compare::KeyValue;
    static constexpr auto value = object("key", &T::key, "value", &T::value);
};

template <> struct glz::meta<bench_compare::RecordBatch> {
    using T                     = bench_compare::RecordBatch;
    static constexpr auto value = object("records", &T::records);
};

template <> struct glz::meta<bench_compare::NestedSnapshot> {
    using T                     = bench_compare::NestedSnapshot;
    static constexpr auto value = object("title", &T::title, "records", &T::records, "counters", &T::counters, "payload", &T::payload);
};

namespace bench_compare {

template <typename T> struct object_id;
template <> struct object_id<FlatRecord> : std::integral_constant<std::uint32_t, 1> {};
template <> struct object_id<RecordBatch> : std::integral_constant<std::uint32_t, 2> {};
template <> struct object_id<NestedSnapshot> : std::integral_constant<std::uint32_t, 3> {};
template <> struct object_id<std::vector<std::int32_t>> : std::integral_constant<std::uint32_t, 4> {};
template <> struct object_id<std::vector<std::int64_t>> : std::integral_constant<std::uint32_t, 5> {};
template <> struct object_id<std::vector<double>> : std::integral_constant<std::uint32_t, 6> {};

template <typename T> constexpr auto object_id_v = object_id<std::remove_cvref_t<T>>::value;

template <typename T> struct typed_array_tag;
template <> struct typed_array_tag<std::int32_t> : std::integral_constant<std::uint64_t, 78> {};
template <> struct typed_array_tag<std::int64_t> : std::integral_constant<std::uint64_t, 79> {};
template <> struct typed_array_tag<double> : std::integral_constant<std::uint64_t, 86> {};

template <typename T> constexpr auto typed_array_tag_v = typed_array_tag<std::remove_cvref_t<T>>::value;

template <typename T>
    requires(std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::int64_t> || std::is_same_v<T, double>)
auto little_endian_word(T value) {
    if constexpr (sizeof(T) == sizeof(std::uint32_t)) {
        return std::bit_cast<std::uint32_t>(value);
    } else {
        return std::bit_cast<std::uint64_t>(value);
    }
}

template <typename T>
    requires(std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::int64_t> || std::is_same_v<T, double>)
auto little_endian_byte_view(std::span<const T> values) {
    return std::views::iota(std::size_t{0}, values.size() * sizeof(T)) | std::views::transform([values](std::size_t index) {
               const auto word       = little_endian_word(values[index / sizeof(T)]);
               const auto byte_shift = static_cast<unsigned>((index % sizeof(T)) * std::numeric_limits<unsigned char>::digits);
               return static_cast<std::uint8_t>((word >> byte_shift) & 0xffU);
           });
}

template <typename Emit> void emit_cbor_major_and_size(Emit &&emit, std::uint8_t major_prefix, std::uint64_t value) {
    auto emit_big_endian = [&](std::uint8_t additional_info, std::size_t width) {
        emit(static_cast<std::uint8_t>(major_prefix | additional_info));
        for (std::size_t index = width; index > 0; --index) {
            emit(static_cast<std::uint8_t>(value >> ((index - 1U) * std::numeric_limits<unsigned char>::digits)));
        }
    };

    if (value < 24U) {
        emit(static_cast<std::uint8_t>(major_prefix | value));
    } else if (value <= 0xffU) {
        emit_big_endian(24, 1);
    } else if (value <= 0xffffU) {
        emit_big_endian(25, 2);
    } else if (value <= 0xffffffffULL) {
        emit_big_endian(26, 4);
    } else {
        emit_big_endian(27, 8);
    }
}

template <typename Emit> void emit_typed_array_header(Emit &&emit, std::uint64_t tag, std::uint64_t payload_size) {
    emit_cbor_major_and_size(emit, 0xc0U, tag);
    emit_cbor_major_and_size(emit, 0x40U, payload_size);
}

void append_typed_array_header(byte_buffer &buffer, std::uint64_t tag, std::uint64_t payload_size) {
    emit_typed_array_header([&](std::uint8_t byte) { buffer.push_back(byte); }, tag, payload_size);
}

template <typename T>
    requires(std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::int64_t> || std::is_same_v<T, double>)
std::span<const std::uint8_t> native_little_endian_bytes(const std::vector<T> &value) {
    if constexpr (std::endian::native == std::endian::little) {
        const auto *begin = reinterpret_cast<const std::uint8_t *>(value.data());
        return {begin, value.size() * sizeof(T)};
    } else {
        throw std::runtime_error{"zero-copy typed-array benchmark requires a little-endian host"};
    }
}

template <typename T>
    requires(std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::int64_t> || std::is_same_v<T, double>)
void append_little_endian_payload(byte_buffer &buffer, const std::vector<T> &value) {
    if constexpr (std::endian::native == std::endian::little) {
        const auto bytes = native_little_endian_bytes(value);
        buffer.insert(buffer.end(), bytes.begin(), bytes.end());
    } else {
        auto bytes = little_endian_byte_view(std::span<const T>{value.data(), value.size()});
        for (auto byte : bytes) {
            buffer.push_back(byte);
        }
    }
}

struct typed_array_segments {
    std::array<std::uint8_t, 24> header{};
    std::size_t                  header_size{};
    const std::uint8_t          *payload{};
    std::size_t                  payload_size{};

    [[nodiscard]] std::size_t size() const noexcept { return header_size + payload_size; }

    [[nodiscard]] std::span<const std::uint8_t> header_span() const noexcept { return {header.data(), header_size}; }

    [[nodiscard]] std::span<const std::uint8_t> payload_span() const noexcept { return {payload, payload_size}; }
};

template <typename T>
    requires(std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::int64_t> || std::is_same_v<T, double>)
void encode_typed_array_segments_into(const std::vector<T> &value, typed_array_segments &segments) {
    const auto  payload = native_little_endian_bytes(value);
    std::size_t offset{};
    emit_typed_array_header(
        [&](std::uint8_t byte) {
            if (offset >= segments.header.size()) {
                throw std::runtime_error{"typed-array segment header buffer is too small"};
            }
            segments.header[offset++] = byte;
        },
        typed_array_tag_v<T>, payload.size());
    segments.header_size  = offset;
    segments.payload      = payload.data();
    segments.payload_size = payload.size();
}

template <typename T>
    requires(std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::int64_t> || std::is_same_v<T, double>)
typed_array_segments encode_typed_array_segments(const std::vector<T> &value) {
    typed_array_segments segments;
    encode_typed_array_segments_into(value, segments);
    return segments;
}

template <std::uint64_t Tag, typename T> byte_buffer encode_typed_array_as_tag(const std::vector<T> &value) {
    byte_buffer buffer;
    auto        bytes  = little_endian_byte_view(std::span<const T>{value.data(), value.size()});
    auto        enc    = cbor::tags::make_encoder(buffer);
    auto        result = enc(cbor::tags::static_tag<Tag>{}, cbor::tags::as_bstr_range(bytes));
    if (!result) {
        throw std::runtime_error(std::string{"cbor_tags typed array encode failed: "} +
                                 std::string{cbor::tags::status_message(result.error())});
    }
    return buffer;
}

template <typename T> byte_buffer encode_typed_array_as_bulk_copy(const std::vector<T> &value) {
    byte_buffer buffer;
    append_typed_array_header(buffer, typed_array_tag_v<T>, value.size() * sizeof(T));
    append_little_endian_payload(buffer, value);
    return buffer;
}

template <typename T> void encode_typed_array_as_bulk_copy_into(const std::vector<T> &value, byte_buffer &buffer) {
    buffer.clear();
    append_typed_array_header(buffer, typed_array_tag_v<T>, value.size() * sizeof(T));
    append_little_endian_payload(buffer, value);
}

std::uint8_t read_byte(std::span<const std::uint8_t> buffer, std::size_t &offset) {
    if (offset >= buffer.size()) {
        throw std::runtime_error{"typed array CBOR is truncated"};
    }
    return buffer[offset++];
}

std::uint64_t read_cbor_argument(std::span<const std::uint8_t> buffer, std::size_t &offset, std::uint8_t additional_info) {
    if (additional_info < 24U) {
        return additional_info;
    }
    auto read_big_endian = [&](std::size_t width) {
        std::uint64_t value{};
        for (std::size_t index = 0; index < width; ++index) {
            value = (value << std::numeric_limits<unsigned char>::digits) | read_byte(buffer, offset);
        }
        return value;
    };
    switch (additional_info) {
    case 24: return read_big_endian(1);
    case 25: return read_big_endian(2);
    case 26: return read_big_endian(4);
    case 27: return read_big_endian(8);
    default: throw std::runtime_error{"typed array CBOR uses an invalid or indefinite length argument"};
    }
}

std::uint64_t read_cbor_header_argument(std::span<const std::uint8_t> buffer, std::size_t &offset, std::uint8_t expected_major) {
    const auto initial         = read_byte(buffer, offset);
    const auto major           = static_cast<std::uint8_t>(initial >> 5U);
    const auto additional_info = static_cast<std::uint8_t>(initial & 0x1fU);
    if (major != expected_major) {
        throw std::runtime_error{"typed array CBOR major type mismatch"};
    }
    return read_cbor_argument(buffer, offset, additional_info);
}

template <typename T> std::vector<T> decode_typed_array_values(const byte_buffer &buffer) {
    constexpr auto tag = typed_array_tag_v<T>;

    const auto  input = std::span<const std::uint8_t>{buffer.data(), buffer.size()};
    std::size_t offset{};
    const auto  decoded_tag = read_cbor_header_argument(input, offset, 6);
    if (decoded_tag != tag) {
        throw std::runtime_error{"cbor_tags typed array tag mismatch"};
    }
    const auto payload_size = static_cast<std::size_t>(read_cbor_header_argument(input, offset, 2));
    if (payload_size > buffer.size() - offset) {
        throw std::runtime_error{"cbor_tags typed array payload is truncated"};
    }
    if ((payload_size % sizeof(T)) != 0U) {
        throw std::runtime_error{"cbor_tags typed array payload length is not a whole number of elements"};
    }
    const auto payload_begin = offset;
    offset += payload_size;
    if (offset != buffer.size()) {
        throw std::runtime_error{"cbor_tags typed array has trailing bytes"};
    }

    std::vector<T> values;
    values.reserve(payload_size / sizeof(T));
    for (std::size_t element_offset = 0; element_offset < payload_size; element_offset += sizeof(T)) {
        using word_type = decltype(little_endian_word(T{}));
        word_type word{};
        for (std::size_t index = 0; index < sizeof(T); ++index) {
            word |= static_cast<word_type>(buffer[payload_begin + element_offset + index])
                    << (index * std::numeric_limits<unsigned char>::digits);
        }
        values.push_back(std::bit_cast<T>(word));
    }
    return values;
}

template <typename T> void validate_typed_array_segments(const typed_array_segments &segments) {
    constexpr auto tag = typed_array_tag_v<T>;

    std::size_t offset{};
    const auto  decoded_tag = read_cbor_header_argument(segments.header_span(), offset, 6);
    if (decoded_tag != tag) {
        throw std::runtime_error{"cbor_tags typed-array segment tag mismatch"};
    }
    const auto payload_size = static_cast<std::size_t>(read_cbor_header_argument(segments.header_span(), offset, 2));
    if (payload_size != segments.payload_size) {
        throw std::runtime_error{"cbor_tags typed-array segment payload length mismatch"};
    }
    if ((payload_size % sizeof(T)) != 0U) {
        throw std::runtime_error{"cbor_tags typed-array segment payload length is not a whole number of elements"};
    }
    if (offset != segments.header_size) {
        throw std::runtime_error{"cbor_tags typed-array segment header has trailing bytes"};
    }
    if (segments.payload == nullptr && segments.payload_size != 0U) {
        throw std::runtime_error{"cbor_tags typed-array segment payload pointer is null"};
    }
    if ((reinterpret_cast<std::uintptr_t>(segments.payload) % alignof(T)) != 0U) {
        throw std::runtime_error{"cbor_tags typed-array segment payload is not aligned for zero-copy typed access"};
    }
}

template <typename T> std::uint64_t decode_typed_array_segment_checksum(const typed_array_segments &segments) {
    if constexpr (std::endian::native != std::endian::little) {
        throw std::runtime_error{"zero-copy typed-array decode benchmark requires a little-endian host"};
    } else {
        validate_typed_array_segments<T>(segments);
        const auto *values = reinterpret_cast<const T *>(segments.payload);
        return checksum_numeric_values(std::span<const T>{values, segments.payload_size / sizeof(T)});
    }
}

struct cbor_tags_codec {
    static constexpr std::string_view name = "cbor_tags";

    template <typename T> static void encode_into(const T &value, byte_buffer &buffer) {
        buffer.clear();
        auto enc    = cbor::tags::make_encoder(buffer);
        auto result = enc(value);
        if (!result) {
            throw std::runtime_error(std::string{"cbor_tags encode failed: "} + std::string{cbor::tags::status_message(result.error())});
        }
    }

    template <typename T> static byte_buffer encode(const T &value) {
        byte_buffer buffer;
        encode_into(value, buffer);
        return buffer;
    }

    template <typename T> static std::uint64_t decode_checksum(const byte_buffer &buffer) {
        T    value;
        auto dec    = cbor::tags::make_decoder(buffer);
        auto result = dec(value);
        if (!result) {
            throw std::runtime_error(std::string{"cbor_tags decode failed: "} + std::string{cbor::tags::status_message(result.error())});
        }
        return checksum(value);
    }
};

struct cbor_tags_enveloped_codec {
    static constexpr std::string_view name = "cbor_tags_enveloped";

    template <typename T> static void encode_into(const T &value, byte_buffer &buffer) {
        buffer.clear();
        auto enc    = cbor::tags::make_encoder(buffer);
        auto result = enc(cbor::tags::static_tag<object_id_v<T>>{}, value);
        if (!result) {
            throw std::runtime_error(std::string{"cbor_tags enveloped encode failed: "} +
                                     std::string{cbor::tags::status_message(result.error())});
        }
    }

    template <typename T> static byte_buffer encode(const T &value) {
        byte_buffer buffer;
        encode_into(value, buffer);
        return buffer;
    }

    template <typename T> static std::uint64_t decode_checksum(const byte_buffer &buffer) {
        T    value;
        auto dec    = cbor::tags::make_decoder(buffer);
        auto result = dec(cbor::tags::static_tag<object_id_v<T>>{}, value);
        if (!result) {
            throw std::runtime_error(std::string{"cbor_tags enveloped decode failed: "} +
                                     std::string{cbor::tags::status_message(result.error())});
        }
        return checksum(value);
    }
};

struct cbor_tags_typed_array_range_codec {
    static constexpr std::string_view name = "cbor_tags_rfc8746_typed_array_range";

    template <typename T> static void encode_into(const std::vector<T> &value, byte_buffer &buffer) {
        buffer.clear();
        auto bytes  = little_endian_byte_view(std::span<const T>{value.data(), value.size()});
        auto enc    = cbor::tags::make_encoder(buffer);
        auto result = enc(cbor::tags::static_tag<typed_array_tag_v<T>>{}, cbor::tags::as_bstr_range(bytes));
        if (!result) {
            throw std::runtime_error(std::string{"cbor_tags typed array encode failed: "} +
                                     std::string{cbor::tags::status_message(result.error())});
        }
    }

    template <typename T> static byte_buffer encode(const std::vector<T> &value) {
        return encode_typed_array_as_tag<typed_array_tag_v<T>>(value);
    }

    template <typename T> static std::uint64_t decode_checksum(const byte_buffer &buffer) {
        return checksum(decode_typed_array_values<typename T::value_type>(buffer));
    }
};

struct cbor_tags_typed_array_bulk_copy_codec {
    static constexpr std::string_view name = "cbor_tags_rfc8746_typed_array_bulk_copy";

    template <typename T> static void encode_into(const std::vector<T> &value, byte_buffer &buffer) {
        encode_typed_array_as_bulk_copy_into(value, buffer);
    }

    template <typename T> static byte_buffer encode(const std::vector<T> &value) { return encode_typed_array_as_bulk_copy(value); }

    template <typename T> static std::uint64_t decode_checksum(const byte_buffer &buffer) {
        return checksum(decode_typed_array_values<typename T::value_type>(buffer));
    }
};

struct cbor_tags_typed_array_segments_codec {
    static constexpr std::string_view name = "cbor_tags_rfc8746_typed_array_segments";

    template <typename T> static void encode_into(const std::vector<T> &value, typed_array_segments &segments) {
        encode_typed_array_segments_into(value, segments);
    }

    template <typename T> static typed_array_segments encode(const std::vector<T> &value) { return encode_typed_array_segments(value); }

    template <typename T> static std::uint64_t decode_checksum(const typed_array_segments &segments) {
        return decode_typed_array_segment_checksum<typename T::value_type>(segments);
    }
};

struct glaze_cbor_codec {
    static constexpr std::string_view name = "glaze_cbor";

    template <typename T> static void encode_into(const T &value, byte_buffer &buffer) {
        buffer.clear();
        auto result = glz::write_cbor(value, buffer);
        if (result) {
            throw std::runtime_error{"Glaze CBOR encode failed"};
        }
    }

    template <typename T> static byte_buffer encode(const T &value) {
        byte_buffer buffer;
        encode_into(value, buffer);
        return buffer;
    }

    template <typename T> static std::uint64_t decode_checksum(const byte_buffer &buffer) {
        T    value;
        auto result = glz::read_cbor(value, buffer);
        if (result) {
            throw std::runtime_error{"Glaze CBOR decode failed"};
        }
        return checksum(value);
    }
};

struct glaze_cbor_enveloped_codec {
    static constexpr std::string_view name = "glaze_cbor_enveloped";

    template <typename T> static void encode_into(const T &value, byte_buffer &buffer) {
        buffer.clear();
        const auto envelope = std::tuple<std::uint32_t, const T &>{object_id_v<T>, value};
        auto       result   = glz::write_cbor(envelope, buffer);
        if (result) {
            throw std::runtime_error{"Glaze CBOR enveloped encode failed"};
        }
    }

    template <typename T> static byte_buffer encode(const T &value) {
        byte_buffer buffer;
        encode_into(value, buffer);
        return buffer;
    }

    template <typename T> static std::uint64_t decode_checksum(const byte_buffer &buffer) {
        auto envelope = std::tuple<std::uint32_t, T>{};
        auto result   = glz::read_cbor(envelope, buffer);
        if (result) {
            throw std::runtime_error{"Glaze CBOR enveloped decode failed"};
        }
        if (std::get<0>(envelope) != object_id_v<T>) {
            throw std::runtime_error{"Glaze CBOR enveloped object id mismatch"};
        }
        return checksum(std::get<1>(envelope));
    }
};

struct bitsery_codec {
    static constexpr std::string_view name = "bitsery";

    static constexpr std::size_t max_string_size = 128;
    static constexpr std::size_t max_samples     = 64;
    static constexpr std::size_t max_records     = 256;
    static constexpr std::size_t max_counters    = 64;
    static constexpr std::size_t max_payload     = 4096;
    static constexpr std::size_t max_series      = 8192;

    template <typename Archive, typename Record> static void process_record(Archive &archive, Record &value) {
        archive.value4b(value.id);
        archive.value8b(value.sequence);
        archive.boolValue(value.active);
        archive.value1b(value.kind);
        archive.value4b(value.temperature);
        archive.value8b(value.score);
        archive.text1b(value.name, max_string_size);
        archive.container4b(value.samples, max_samples);
    }

    template <typename Archive, typename Entry> static void process_key_value(Archive &archive, Entry &value) {
        archive.text1b(value.key, max_string_size);
        archive.value8b(value.value);
    }

    template <typename Archive, typename T> static void process(Archive &archive, T &value) {
        if constexpr (std::is_same_v<std::remove_cvref_t<T>, FlatRecord>) {
            process_record(archive, value);
        } else if constexpr (std::is_same_v<std::remove_cvref_t<T>, KeyValue>) {
            process_key_value(archive, value);
        } else if constexpr (std::is_same_v<std::remove_cvref_t<T>, RecordBatch>) {
            archive.container(value.records, max_records,
                              [](auto &nested_archive, auto &record) { process_record(nested_archive, record); });
        } else if constexpr (std::is_same_v<std::remove_cvref_t<T>, NestedSnapshot>) {
            archive.text1b(value.title, max_string_size);
            archive.container(value.records, max_records,
                              [](auto &nested_archive, auto &record) { process_record(nested_archive, record); });
            archive.container(value.counters, max_counters,
                              [](auto &nested_archive, auto &entry) { process_key_value(nested_archive, entry); });
            archive.container1b(value.payload, max_payload);
        } else if constexpr (std::is_same_v<std::remove_cvref_t<T>, std::vector<std::int32_t>>) {
            archive.container4b(value, max_series);
        } else if constexpr (std::is_same_v<std::remove_cvref_t<T>, std::vector<std::int64_t>>) {
            archive.container8b(value, max_series);
        } else if constexpr (std::is_same_v<std::remove_cvref_t<T>, std::vector<double>>) {
            archive.container8b(value, max_series);
        }
    }

    template <typename T> static void encode_into(const T &value, byte_buffer &buffer) {
        buffer.clear();
        using output_adapter = bitsery::OutputBufferAdapter<byte_buffer>;
        bitsery::Serializer<output_adapter> serializer{output_adapter{buffer}};
        process(serializer, value);
        serializer.adapter().flush();
        buffer.resize(serializer.adapter().writtenBytesCount());
    }

    template <typename T> static byte_buffer encode(const T &value) {
        byte_buffer buffer;
        encode_into(value, buffer);
        return buffer;
    }

    template <typename T> static std::uint64_t decode_checksum(const byte_buffer &buffer) {
        T value;
        using input_adapter = bitsery::InputBufferAdapter<byte_buffer>;
        bitsery::Deserializer<input_adapter> deserializer{input_adapter{buffer.begin(), buffer.size()}};
        process(deserializer, value);
        if (deserializer.adapter().error() != bitsery::ReaderError::NoError || !deserializer.adapter().isCompletedSuccessfully()) {
            throw std::runtime_error{"bitsery decode failed"};
        }
        return checksum(value);
    }
};

struct bitsery_enveloped_codec {
    static constexpr std::string_view name = "bitsery_enveloped";

    template <typename T> static void encode_into(const T &value, byte_buffer &buffer) {
        buffer.clear();
        using output_adapter = bitsery::OutputBufferAdapter<byte_buffer>;
        bitsery::Serializer<output_adapter> serializer{output_adapter{buffer}};
        auto                                id = object_id_v<T>;
        serializer.value4b(id);
        bitsery_codec::process(serializer, value);
        serializer.adapter().flush();
        buffer.resize(serializer.adapter().writtenBytesCount());
    }

    template <typename T> static byte_buffer encode(const T &value) {
        byte_buffer buffer;
        encode_into(value, buffer);
        return buffer;
    }

    template <typename T> static std::uint64_t decode_checksum(const byte_buffer &buffer) {
        T value;
        using input_adapter = bitsery::InputBufferAdapter<byte_buffer>;
        bitsery::Deserializer<input_adapter> deserializer{input_adapter{buffer.begin(), buffer.size()}};
        std::uint32_t                        id{};
        deserializer.value4b(id);
        if (id != object_id_v<T>) {
            throw std::runtime_error{"bitsery enveloped object id mismatch"};
        }
        bitsery_codec::process(deserializer, value);
        if (deserializer.adapter().error() != bitsery::ReaderError::NoError || !deserializer.adapter().isCompletedSuccessfully()) {
            throw std::runtime_error{"bitsery enveloped decode failed"};
        }
        return checksum(value);
    }
};

struct zpp_bits_codec {
    static constexpr std::string_view name = "zpp_bits";

    template <typename T> static void encode_into(const T &value, byte_buffer &buffer) {
        buffer.clear();
        zpp::bits::out out{buffer};
        out(value).or_throw();
    }

    template <typename T> static byte_buffer encode(const T &value) {
        byte_buffer buffer;
        encode_into(value, buffer);
        return buffer;
    }

    template <typename T> static std::uint64_t decode_checksum(const byte_buffer &buffer) {
        T             value;
        zpp::bits::in in{buffer};
        in(value).or_throw();
        return checksum(value);
    }
};

struct zpp_bits_enveloped_codec {
    static constexpr std::string_view name = "zpp_bits_enveloped";

    template <typename T> static void encode_into(const T &value, byte_buffer &buffer) {
        buffer.clear();
        auto           id = object_id_v<T>;
        zpp::bits::out out{buffer};
        out(id, value).or_throw();
    }

    template <typename T> static byte_buffer encode(const T &value) {
        byte_buffer buffer;
        encode_into(value, buffer);
        return buffer;
    }

    template <typename T> static std::uint64_t decode_checksum(const byte_buffer &buffer) {
        T             value;
        std::uint32_t id{};
        zpp::bits::in in{buffer};
        in(id).or_throw();
        if (id != object_id_v<T>) {
            throw std::runtime_error{"zpp_bits enveloped object id mismatch"};
        }
        in(value).or_throw();
        return checksum(value);
    }
};

struct cereal_codec {
    static constexpr std::string_view name = "cereal";

    template <typename Archive, typename Record> static void process_record(Archive &archive, Record &value) {
        archive(value.id, value.sequence, value.active, value.kind, value.temperature, value.score, value.name, value.samples);
    }

    template <typename Archive, typename Entry> static void process_key_value(Archive &archive, Entry &value) {
        archive(value.key, value.value);
    }

    template <typename Archive> static void save_records(Archive &archive, const std::vector<FlatRecord> &records) {
        archive(cereal::make_size_tag(static_cast<cereal::size_type>(records.size())));
        for (const auto &record : records) {
            process_record(archive, record);
        }
    }

    template <typename Archive> static void load_records(Archive &archive, std::vector<FlatRecord> &records) {
        cereal::size_type size{};
        archive(cereal::make_size_tag(size));
        records.resize(size);
        for (auto &record : records) {
            process_record(archive, record);
        }
    }

    template <typename Archive> static void save_counters(Archive &archive, const std::vector<KeyValue> &counters) {
        archive(cereal::make_size_tag(static_cast<cereal::size_type>(counters.size())));
        for (const auto &entry : counters) {
            process_key_value(archive, entry);
        }
    }

    template <typename Archive> static void load_counters(Archive &archive, std::vector<KeyValue> &counters) {
        cereal::size_type size{};
        archive(cereal::make_size_tag(size));
        counters.resize(size);
        for (auto &entry : counters) {
            process_key_value(archive, entry);
        }
    }

    template <typename Archive> static void process(Archive &archive, const FlatRecord &value) { process_record(archive, value); }

    template <typename Archive> static void process(Archive &archive, FlatRecord &value) { process_record(archive, value); }

    template <typename Archive> static void process(Archive &archive, const KeyValue &value) { process_key_value(archive, value); }

    template <typename Archive> static void process(Archive &archive, KeyValue &value) { process_key_value(archive, value); }

    template <typename Archive> static void process(Archive &archive, const RecordBatch &value) { save_records(archive, value.records); }

    template <typename Archive> static void process(Archive &archive, RecordBatch &value) { load_records(archive, value.records); }

    template <typename Archive> static void process(Archive &archive, const NestedSnapshot &value) {
        archive(value.title);
        save_records(archive, value.records);
        save_counters(archive, value.counters);
        archive(value.payload);
    }

    template <typename Archive> static void process(Archive &archive, NestedSnapshot &value) {
        archive(value.title);
        load_records(archive, value.records);
        load_counters(archive, value.counters);
        archive(value.payload);
    }

    template <typename Archive, typename T>
        requires(std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::int64_t> || std::is_same_v<T, double>)
    static void process(Archive &archive, const std::vector<T> &value) {
        archive(value);
    }

    template <typename Archive, typename T>
        requires(std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::int64_t> || std::is_same_v<T, double>)
    static void process(Archive &archive, std::vector<T> &value) {
        archive(value);
    }

    template <typename T> static void encode_into(const T &value, byte_buffer &buffer) {
        std::ostringstream          stream(std::ios::out | std::ios::binary);
        cereal::BinaryOutputArchive archive(stream);
        process(archive, value);
        auto text = stream.str();
        buffer.assign(text.begin(), text.end());
    }

    template <typename T> static byte_buffer encode(const T &value) {
        byte_buffer buffer;
        encode_into(value, buffer);
        return buffer;
    }

    template <typename T> static std::uint64_t decode_checksum(const byte_buffer &buffer) {
        T                          value;
        byte_input_stream          stream(buffer);
        cereal::BinaryInputArchive archive(stream);
        process(archive, value);
        return checksum(value);
    }
};

struct cereal_enveloped_codec {
    static constexpr std::string_view name = "cereal_enveloped";

    template <typename T> static void encode_into(const T &value, byte_buffer &buffer) {
        std::ostringstream          stream(std::ios::out | std::ios::binary);
        cereal::BinaryOutputArchive archive(stream);
        auto                        id = object_id_v<T>;
        archive(id);
        cereal_codec::process(archive, value);
        auto text = stream.str();
        buffer.assign(text.begin(), text.end());
    }

    template <typename T> static byte_buffer encode(const T &value) {
        byte_buffer buffer;
        encode_into(value, buffer);
        return buffer;
    }

    template <typename T> static std::uint64_t decode_checksum(const byte_buffer &buffer) {
        T                          value;
        byte_input_stream          stream(buffer);
        cereal::BinaryInputArchive archive(stream);
        std::uint32_t              id{};
        archive(id);
        if (id != object_id_v<T>) {
            throw std::runtime_error{"cereal enveloped object id mismatch"};
        }
        cereal_codec::process(archive, value);
        return checksum(value);
    }
};

struct boost_serialization_codec {
    static constexpr std::string_view name = "boost_serialization";

    template <typename T> static void encode_into(const T &value, byte_buffer &buffer) {
        std::ostringstream              stream(std::ios::out | std::ios::binary);
        boost::archive::binary_oarchive archive(stream, boost::archive::no_header);
        archive & value;
        auto text = stream.str();
        buffer.assign(text.begin(), text.end());
    }

    template <typename T> static byte_buffer encode(const T &value) {
        byte_buffer buffer;
        encode_into(value, buffer);
        return buffer;
    }

    template <typename T> static std::uint64_t decode_checksum(const byte_buffer &buffer) {
        T                               value;
        byte_input_stream               stream(buffer);
        boost::archive::binary_iarchive archive(stream, boost::archive::no_header);
        archive & value;
        return checksum(value);
    }
};

struct boost_serialization_enveloped_codec {
    static constexpr std::string_view name = "boost_serialization_enveloped";

    template <typename T> static void encode_into(const T &value, byte_buffer &buffer) {
        std::ostringstream              stream(std::ios::out | std::ios::binary);
        boost::archive::binary_oarchive archive(stream, boost::archive::no_header);
        auto                            id = object_id_v<T>;
        archive & id;
        archive & value;
        auto text = stream.str();
        buffer.assign(text.begin(), text.end());
    }

    template <typename T> static byte_buffer encode(const T &value) {
        byte_buffer buffer;
        encode_into(value, buffer);
        return buffer;
    }

    template <typename T> static std::uint64_t decode_checksum(const byte_buffer &buffer) {
        T                               value;
        byte_input_stream               stream(buffer);
        boost::archive::binary_iarchive archive(stream, boost::archive::no_header);
        std::uint32_t                   id{};
        archive & id;
        if (id != object_id_v<T>) {
            throw std::runtime_error{"boost_serialization enveloped object id mismatch"};
        }
        archive & value;
        return checksum(value);
    }
};

namespace fb = cbor_tags_bench;

template <typename T> constexpr const char *flatbuffer_identifier();
template <> constexpr const char           *flatbuffer_identifier<FlatRecord>() { return "FREC"; }
template <> constexpr const char           *flatbuffer_identifier<RecordBatch>() { return "RBAT"; }
template <> constexpr const char           *flatbuffer_identifier<NestedSnapshot>() { return "NSNP"; }
template <> constexpr const char           *flatbuffer_identifier<std::vector<std::int32_t>>() { return "I32S"; }
template <> constexpr const char           *flatbuffer_identifier<std::vector<std::int64_t>>() { return "I64S"; }
template <> constexpr const char           *flatbuffer_identifier<std::vector<double>>() { return "F64S"; }

flatbuffers::Offset<fb::FlatRecord> create_flat_record(flatbuffers::FlatBufferBuilder &builder, const FlatRecord &value) {
    auto name    = builder.CreateString(value.name);
    auto samples = builder.CreateVector(value.samples);
    return fb::CreateFlatRecord(builder, value.id, value.sequence, value.active, value.kind, value.temperature, value.score, name, samples);
}

flatbuffers::Offset<fb::KeyValue> create_key_value(flatbuffers::FlatBufferBuilder &builder, const KeyValue &value) {
    auto key = builder.CreateString(value.key);
    return fb::CreateKeyValue(builder, key, value.value);
}

flatbuffers::Offset<fb::Int32Series> create_int32_series(flatbuffers::FlatBufferBuilder &builder, const std::vector<std::int32_t> &value) {
    return fb::CreateInt32Series(builder, builder.CreateVector(value));
}

flatbuffers::Offset<fb::Int64Series> create_int64_series(flatbuffers::FlatBufferBuilder &builder, const std::vector<std::int64_t> &value) {
    return fb::CreateInt64Series(builder, builder.CreateVector(value));
}

flatbuffers::Offset<fb::Float64Series> create_float64_series(flatbuffers::FlatBufferBuilder &builder, const std::vector<double> &value) {
    return fb::CreateFloat64Series(builder, builder.CreateVector(value));
}

std::uint64_t checksum_flat_record(const fb::FlatRecord &value) {
    auto seed = std::uint64_t{0x123456789abcdef0ULL};
    seed      = mix(seed, static_cast<std::uint32_t>(value.id()));
    seed      = mix(seed, value.sequence());
    seed      = mix(seed, value.active() ? 1U : 0U);
    seed      = mix(seed, value.kind());
    seed      = mix(seed, bit_pattern(value.temperature()));
    seed      = mix(seed, bit_pattern(value.score()));
    if (auto name = value.name(); name != nullptr) {
        seed = mix_string(seed, std::string_view{name->c_str(), name->size()});
    } else {
        seed = mix_string(seed, std::string_view{});
    }
    if (auto samples = value.samples(); samples != nullptr) {
        seed = mix(seed, samples->size());
        for (flatbuffers::uoffset_t index = 0; index < samples->size(); ++index) {
            seed = mix(seed, static_cast<std::uint32_t>(samples->Get(index)));
        }
    } else {
        seed = mix(seed, 0);
    }
    return seed;
}

std::uint64_t checksum_key_value(const fb::KeyValue &value) {
    auto seed = std::uint64_t{0x0fedcba987654321ULL};
    if (auto key = value.key(); key != nullptr) {
        seed = mix_string(seed, std::string_view{key->c_str(), key->size()});
    } else {
        seed = mix_string(seed, std::string_view{});
    }
    seed = mix(seed, static_cast<std::uint64_t>(value.value()));
    return seed;
}

template <typename T, typename FlatbufferVector> std::uint64_t checksum_flatbuffer_numeric_vector(const FlatbufferVector *values) {
    auto seed = std::uint64_t{0x33445566778899aaULL};
    seed      = mix(seed, sizeof(T));
    if (values == nullptr) {
        return mix(seed, 0);
    }
    seed = mix(seed, values->size());
    for (flatbuffers::uoffset_t index = 0; index < values->size(); ++index) {
        const auto item = values->Get(index);
        if constexpr (std::is_floating_point_v<T>) {
            seed = mix(seed, bit_pattern(item));
        } else {
            seed = mix(seed, static_cast<std::make_unsigned_t<T>>(item));
        }
    }
    return seed;
}

template <typename FlatbufferRoot>
const FlatbufferRoot &checked_flatbuffer_root(const byte_buffer &buffer, const char *identifier = nullptr) {
    flatbuffers::Verifier verifier(buffer.data(), buffer.size());
    if (!verifier.VerifyBuffer<FlatbufferRoot>(identifier)) {
        throw std::runtime_error{"flatbuffers verification failed"};
    }
    return *flatbuffers::GetRoot<FlatbufferRoot>(buffer.data());
}

struct flatbuffers_codec {
    static constexpr std::string_view name = "flatbuffers";

    static flatbuffers::FlatBufferBuilder make_builder(const byte_buffer &buffer) {
        return flatbuffers::FlatBufferBuilder{std::max<std::size_t>(1024, buffer.capacity())};
    }

    static void copy_finished(flatbuffers::FlatBufferBuilder &builder, byte_buffer &buffer) {
        const auto *begin = builder.GetBufferPointer();
        buffer.assign(begin, begin + builder.GetSize());
    }

    template <typename Offset>
    static void finish_and_copy(flatbuffers::FlatBufferBuilder &builder, Offset offset, byte_buffer &buffer,
                                const char *identifier = nullptr) {
        builder.Finish(offset, identifier);
        copy_finished(builder, buffer);
    }

    static void encode_into(const FlatRecord &value, byte_buffer &buffer, const char *identifier = nullptr) {
        buffer.clear();
        auto builder = make_builder(buffer);
        finish_and_copy(builder, create_flat_record(builder, value), buffer, identifier);
    }

    static byte_buffer encode(const FlatRecord &value) {
        byte_buffer buffer;
        encode_into(value, buffer);
        return buffer;
    }

    static void encode_into(const RecordBatch &value, byte_buffer &buffer, const char *identifier = nullptr) {
        buffer.clear();
        auto                                             builder = make_builder(buffer);
        std::vector<flatbuffers::Offset<fb::FlatRecord>> records;
        records.reserve(value.records.size());
        for (const auto &record : value.records) {
            records.push_back(create_flat_record(builder, record));
        }
        auto records_vector = builder.CreateVector(records);
        finish_and_copy(builder, fb::CreateRecordBatch(builder, records_vector), buffer, identifier);
    }

    static byte_buffer encode(const RecordBatch &value) {
        byte_buffer buffer;
        encode_into(value, buffer);
        return buffer;
    }

    static void encode_into(const NestedSnapshot &value, byte_buffer &buffer, const char *identifier = nullptr) {
        buffer.clear();
        auto builder = make_builder(buffer);
        auto title   = builder.CreateString(value.title);

        std::vector<flatbuffers::Offset<fb::FlatRecord>> records;
        records.reserve(value.records.size());
        for (const auto &record : value.records) {
            records.push_back(create_flat_record(builder, record));
        }
        auto records_vector = builder.CreateVector(records);

        std::vector<flatbuffers::Offset<fb::KeyValue>> counters;
        counters.reserve(value.counters.size());
        for (const auto &entry : value.counters) {
            counters.push_back(create_key_value(builder, entry));
        }
        auto counters_vector = builder.CreateVector(counters);
        auto payload_vector  = builder.CreateVector(value.payload);

        finish_and_copy(builder, fb::CreateNestedSnapshot(builder, title, records_vector, counters_vector, payload_vector), buffer,
                        identifier);
    }

    static byte_buffer encode(const NestedSnapshot &value) {
        byte_buffer buffer;
        encode_into(value, buffer);
        return buffer;
    }

    static void encode_into(const std::vector<std::int32_t> &value, byte_buffer &buffer, const char *identifier = nullptr) {
        buffer.clear();
        auto builder = make_builder(buffer);
        finish_and_copy(builder, create_int32_series(builder, value), buffer, identifier);
    }

    static byte_buffer encode(const std::vector<std::int32_t> &value) {
        byte_buffer buffer;
        encode_into(value, buffer);
        return buffer;
    }

    static void encode_into(const std::vector<std::int64_t> &value, byte_buffer &buffer, const char *identifier = nullptr) {
        buffer.clear();
        auto builder = make_builder(buffer);
        finish_and_copy(builder, create_int64_series(builder, value), buffer, identifier);
    }

    static byte_buffer encode(const std::vector<std::int64_t> &value) {
        byte_buffer buffer;
        encode_into(value, buffer);
        return buffer;
    }

    static void encode_into(const std::vector<double> &value, byte_buffer &buffer, const char *identifier = nullptr) {
        buffer.clear();
        auto builder = make_builder(buffer);
        finish_and_copy(builder, create_float64_series(builder, value), buffer, identifier);
    }

    static byte_buffer encode(const std::vector<double> &value) {
        byte_buffer buffer;
        encode_into(value, buffer);
        return buffer;
    }

    template <typename T> static std::uint64_t decode_checksum(const byte_buffer &buffer);
};

template <> std::uint64_t flatbuffers_codec::decode_checksum<FlatRecord>(const byte_buffer &buffer) {
    return checksum_flat_record(checked_flatbuffer_root<fb::FlatRecord>(buffer));
}

template <> std::uint64_t flatbuffers_codec::decode_checksum<RecordBatch>(const byte_buffer &buffer) {
    const auto &value = checked_flatbuffer_root<fb::RecordBatch>(buffer);
    auto        seed  = std::uint64_t{0x1020304050607080ULL};
    if (auto records = value.records(); records != nullptr) {
        seed = mix(seed, records->size());
        for (flatbuffers::uoffset_t index = 0; index < records->size(); ++index) {
            seed = mix(seed, checksum_flat_record(*records->Get(index)));
        }
    } else {
        seed = mix(seed, 0);
    }
    return seed;
}

template <> std::uint64_t flatbuffers_codec::decode_checksum<NestedSnapshot>(const byte_buffer &buffer) {
    const auto &value = checked_flatbuffer_root<fb::NestedSnapshot>(buffer);
    auto        seed  = std::uint64_t{0x8877665544332211ULL};
    if (auto title = value.title(); title != nullptr) {
        seed = mix_string(seed, std::string_view{title->c_str(), title->size()});
    } else {
        seed = mix_string(seed, std::string_view{});
    }
    if (auto records = value.records(); records != nullptr) {
        seed = mix(seed, records->size());
        for (flatbuffers::uoffset_t index = 0; index < records->size(); ++index) {
            seed = mix(seed, checksum_flat_record(*records->Get(index)));
        }
    } else {
        seed = mix(seed, 0);
    }
    if (auto counters = value.counters(); counters != nullptr) {
        seed = mix(seed, counters->size());
        for (flatbuffers::uoffset_t index = 0; index < counters->size(); ++index) {
            seed = mix(seed, checksum_key_value(*counters->Get(index)));
        }
    } else {
        seed = mix(seed, 0);
    }
    if (auto payload = value.payload(); payload != nullptr) {
        seed = mix(seed, payload->size());
        for (flatbuffers::uoffset_t index = 0; index < payload->size(); ++index) {
            seed = mix(seed, payload->Get(index));
        }
    } else {
        seed = mix(seed, 0);
    }
    return seed;
}

template <> std::uint64_t flatbuffers_codec::decode_checksum<std::vector<std::int32_t>>(const byte_buffer &buffer) {
    const auto &value = checked_flatbuffer_root<fb::Int32Series>(buffer);
    return checksum_flatbuffer_numeric_vector<std::int32_t>(value.values());
}

template <> std::uint64_t flatbuffers_codec::decode_checksum<std::vector<std::int64_t>>(const byte_buffer &buffer) {
    const auto &value = checked_flatbuffer_root<fb::Int64Series>(buffer);
    return checksum_flatbuffer_numeric_vector<std::int64_t>(value.values());
}

template <> std::uint64_t flatbuffers_codec::decode_checksum<std::vector<double>>(const byte_buffer &buffer) {
    const auto &value = checked_flatbuffer_root<fb::Float64Series>(buffer);
    return checksum_flatbuffer_numeric_vector<double>(value.values());
}

struct flatbuffers_enveloped_codec {
    static constexpr std::string_view name = "flatbuffers_enveloped";

    template <typename T> static void encode_into(const T &value, byte_buffer &buffer) {
        flatbuffers_codec::encode_into(value, buffer, flatbuffer_identifier<T>());
    }

    template <typename T> static byte_buffer encode(const T &value) {
        byte_buffer buffer;
        encode_into(value, buffer);
        return buffer;
    }

    template <typename T> static std::uint64_t decode_checksum(const byte_buffer &buffer);
};

void require_flatbuffer_identifier(const byte_buffer &buffer, const char *identifier) {
    constexpr auto minimum_identifier_buffer_size = std::size_t{8};
    if (buffer.size() < minimum_identifier_buffer_size || !flatbuffers::BufferHasIdentifier(buffer.data(), identifier)) {
        throw std::runtime_error{"flatbuffers enveloped file identifier mismatch"};
    }
}

template <> std::uint64_t flatbuffers_enveloped_codec::decode_checksum<FlatRecord>(const byte_buffer &buffer) {
    require_flatbuffer_identifier(buffer, flatbuffer_identifier<FlatRecord>());
    return checksum_flat_record(checked_flatbuffer_root<fb::FlatRecord>(buffer, flatbuffer_identifier<FlatRecord>()));
}

template <> std::uint64_t flatbuffers_enveloped_codec::decode_checksum<RecordBatch>(const byte_buffer &buffer) {
    require_flatbuffer_identifier(buffer, flatbuffer_identifier<RecordBatch>());
    const auto &value = checked_flatbuffer_root<fb::RecordBatch>(buffer, flatbuffer_identifier<RecordBatch>());
    auto        seed  = std::uint64_t{0x1020304050607080ULL};
    if (auto records = value.records(); records != nullptr) {
        seed = mix(seed, records->size());
        for (flatbuffers::uoffset_t index = 0; index < records->size(); ++index) {
            seed = mix(seed, checksum_flat_record(*records->Get(index)));
        }
    } else {
        seed = mix(seed, 0);
    }
    return seed;
}

template <> std::uint64_t flatbuffers_enveloped_codec::decode_checksum<NestedSnapshot>(const byte_buffer &buffer) {
    require_flatbuffer_identifier(buffer, flatbuffer_identifier<NestedSnapshot>());
    const auto &value = checked_flatbuffer_root<fb::NestedSnapshot>(buffer, flatbuffer_identifier<NestedSnapshot>());
    auto        seed  = std::uint64_t{0x8877665544332211ULL};
    if (auto title = value.title(); title != nullptr) {
        seed = mix_string(seed, std::string_view{title->c_str(), title->size()});
    } else {
        seed = mix_string(seed, std::string_view{});
    }
    if (auto records = value.records(); records != nullptr) {
        seed = mix(seed, records->size());
        for (flatbuffers::uoffset_t index = 0; index < records->size(); ++index) {
            seed = mix(seed, checksum_flat_record(*records->Get(index)));
        }
    } else {
        seed = mix(seed, 0);
    }
    if (auto counters = value.counters(); counters != nullptr) {
        seed = mix(seed, counters->size());
        for (flatbuffers::uoffset_t index = 0; index < counters->size(); ++index) {
            seed = mix(seed, checksum_key_value(*counters->Get(index)));
        }
    } else {
        seed = mix(seed, 0);
    }
    if (auto payload = value.payload(); payload != nullptr) {
        seed = mix(seed, payload->size());
        for (flatbuffers::uoffset_t index = 0; index < payload->size(); ++index) {
            seed = mix(seed, payload->Get(index));
        }
    } else {
        seed = mix(seed, 0);
    }
    return seed;
}

template <> std::uint64_t flatbuffers_enveloped_codec::decode_checksum<std::vector<std::int32_t>>(const byte_buffer &buffer) {
    require_flatbuffer_identifier(buffer, flatbuffer_identifier<std::vector<std::int32_t>>());
    const auto &value = checked_flatbuffer_root<fb::Int32Series>(buffer, flatbuffer_identifier<std::vector<std::int32_t>>());
    return checksum_flatbuffer_numeric_vector<std::int32_t>(value.values());
}

template <> std::uint64_t flatbuffers_enveloped_codec::decode_checksum<std::vector<std::int64_t>>(const byte_buffer &buffer) {
    require_flatbuffer_identifier(buffer, flatbuffer_identifier<std::vector<std::int64_t>>());
    const auto &value = checked_flatbuffer_root<fb::Int64Series>(buffer, flatbuffer_identifier<std::vector<std::int64_t>>());
    return checksum_flatbuffer_numeric_vector<std::int64_t>(value.values());
}

template <> std::uint64_t flatbuffers_enveloped_codec::decode_checksum<std::vector<double>>(const byte_buffer &buffer) {
    require_flatbuffer_identifier(buffer, flatbuffer_identifier<std::vector<double>>());
    const auto &value = checked_flatbuffer_root<fb::Float64Series>(buffer, flatbuffer_identifier<std::vector<double>>());
    return checksum_flatbuffer_numeric_vector<double>(value.values());
}

struct metric_row {
    std::string       fixture;
    std::string       library;
    std::size_t       encoded_size{};
    allocation_sample encode_cold_allocations;
    allocation_sample encode_reserved_allocations;
    allocation_sample decode_allocations;
    std::uint64_t     checksum{};
};

template <typename Codec, typename Fixture>
byte_buffer collect_codec_metrics(std::string_view fixture_name, const Fixture &fixture, std::vector<metric_row> &metrics) {
    const auto encoded          = Codec::encode(fixture);
    const auto expected_digest  = checksum(fixture);
    const auto decoded_checksum = Codec::template decode_checksum<Fixture>(encoded);
    if (decoded_checksum != expected_digest) {
        throw std::runtime_error{std::string{Codec::name} + " checksum mismatch for " + std::string{fixture_name}};
    }

    auto        encode_allocations = count_allocations([&] { return Codec::encode(fixture); });
    byte_buffer reserved_buffer;
    reserved_buffer.reserve(encoded.size());
    auto reserved_encode_allocations = count_allocations([&] {
        Codec::encode_into(fixture, reserved_buffer);
        return reserved_buffer.size();
    });
    auto decode_allocations          = count_allocations([&] { return Codec::template decode_checksum<Fixture>(encoded); });
    metrics.push_back(metric_row{.fixture                     = std::string{fixture_name},
                                 .library                     = std::string{Codec::name},
                                 .encoded_size                = encoded.size(),
                                 .encode_cold_allocations     = encode_allocations,
                                 .encode_reserved_allocations = reserved_encode_allocations,
                                 .decode_allocations          = decode_allocations,
                                 .checksum                    = expected_digest});
    return encoded;
}

template <typename Codec, typename Fixture>
void run_encode_cold_benchmark(std::string_view fixture_name, const Fixture &fixture, std::size_t encoded_size,
                               ankerl::nanobench::Bench &bench, std::vector<ankerl::nanobench::Result> &results) {
    const auto library_name = std::string{Codec::name};
    bench.title(std::string{fixture_name} + " encode cold").batch(encoded_size).run(library_name, [&] {
        auto output = Codec::encode(fixture);
        ankerl::nanobench::doNotOptimizeAway(output);
    });
    results.push_back(bench.results().back());
}

template <typename Codec, typename Fixture>
void run_encode_reused_benchmark(std::string_view fixture_name, const Fixture &fixture, std::size_t encoded_size,
                                 ankerl::nanobench::Bench &bench, std::vector<ankerl::nanobench::Result> &results) {
    const auto  library_name = std::string{Codec::name};
    byte_buffer buffer;
    buffer.reserve(encoded_size);
    bench.title(std::string{fixture_name} + " encode reused buffer").batch(encoded_size).run(library_name, [&] {
        Codec::encode_into(fixture, buffer);
        ankerl::nanobench::doNotOptimizeAway(buffer);
    });
    results.push_back(bench.results().back());
}

template <typename Codec, typename Fixture, typename Encoded>
void run_decode_benchmark(std::string_view fixture_name, const Encoded &encoded, ankerl::nanobench::Bench &bench,
                          std::vector<ankerl::nanobench::Result> &results) {
    const auto library_name = std::string{Codec::name};
    bench.title(std::string{fixture_name} + " decode").batch(encoded.size()).run(library_name, [&] {
        auto digest = Codec::template decode_checksum<Fixture>(encoded);
        ankerl::nanobench::doNotOptimizeAway(digest);
    });
    results.push_back(bench.results().back());
}

template <typename Fixture>
void run_fixture(std::string_view name, const Fixture &fixture, ankerl::nanobench::Bench &bench, std::vector<metric_row> &metrics,
                 std::vector<ankerl::nanobench::Result> &results) {
    const auto cbor_tags_encoded           = collect_codec_metrics<cbor_tags_codec>(name, fixture, metrics);
    const auto bitsery_encoded             = collect_codec_metrics<bitsery_codec>(name, fixture, metrics);
    const auto zpp_bits_encoded            = collect_codec_metrics<zpp_bits_codec>(name, fixture, metrics);
    const auto cereal_encoded              = collect_codec_metrics<cereal_codec>(name, fixture, metrics);
    const auto boost_serialization_encoded = collect_codec_metrics<boost_serialization_codec>(name, fixture, metrics);
    const auto glaze_cbor_encoded          = collect_codec_metrics<glaze_cbor_codec>(name, fixture, metrics);
    const auto flatbuffers_encoded         = collect_codec_metrics<flatbuffers_codec>(name, fixture, metrics);

    run_encode_cold_benchmark<cbor_tags_codec>(name, fixture, cbor_tags_encoded.size(), bench, results);
    run_encode_cold_benchmark<bitsery_codec>(name, fixture, bitsery_encoded.size(), bench, results);
    run_encode_cold_benchmark<zpp_bits_codec>(name, fixture, zpp_bits_encoded.size(), bench, results);
    run_encode_cold_benchmark<cereal_codec>(name, fixture, cereal_encoded.size(), bench, results);
    run_encode_cold_benchmark<boost_serialization_codec>(name, fixture, boost_serialization_encoded.size(), bench, results);
    run_encode_cold_benchmark<glaze_cbor_codec>(name, fixture, glaze_cbor_encoded.size(), bench, results);
    run_encode_cold_benchmark<flatbuffers_codec>(name, fixture, flatbuffers_encoded.size(), bench, results);

    run_encode_reused_benchmark<cbor_tags_codec>(name, fixture, cbor_tags_encoded.size(), bench, results);
    run_encode_reused_benchmark<bitsery_codec>(name, fixture, bitsery_encoded.size(), bench, results);
    run_encode_reused_benchmark<zpp_bits_codec>(name, fixture, zpp_bits_encoded.size(), bench, results);
    run_encode_reused_benchmark<cereal_codec>(name, fixture, cereal_encoded.size(), bench, results);
    run_encode_reused_benchmark<boost_serialization_codec>(name, fixture, boost_serialization_encoded.size(), bench, results);
    run_encode_reused_benchmark<glaze_cbor_codec>(name, fixture, glaze_cbor_encoded.size(), bench, results);
    run_encode_reused_benchmark<flatbuffers_codec>(name, fixture, flatbuffers_encoded.size(), bench, results);

    run_decode_benchmark<cbor_tags_codec, Fixture>(name, cbor_tags_encoded, bench, results);
    run_decode_benchmark<bitsery_codec, Fixture>(name, bitsery_encoded, bench, results);
    run_decode_benchmark<zpp_bits_codec, Fixture>(name, zpp_bits_encoded, bench, results);
    run_decode_benchmark<cereal_codec, Fixture>(name, cereal_encoded, bench, results);
    run_decode_benchmark<boost_serialization_codec, Fixture>(name, boost_serialization_encoded, bench, results);
    run_decode_benchmark<glaze_cbor_codec, Fixture>(name, glaze_cbor_encoded, bench, results);
    run_decode_benchmark<flatbuffers_codec, Fixture>(name, flatbuffers_encoded, bench, results);
}

template <typename Fixture>
void run_enveloped_fixture(std::string_view name, const Fixture &fixture, ankerl::nanobench::Bench &bench, std::vector<metric_row> &metrics,
                           std::vector<ankerl::nanobench::Result> &results) {
    const auto cbor_tags_encoded           = collect_codec_metrics<cbor_tags_enveloped_codec>(name, fixture, metrics);
    const auto bitsery_encoded             = collect_codec_metrics<bitsery_enveloped_codec>(name, fixture, metrics);
    const auto zpp_bits_encoded            = collect_codec_metrics<zpp_bits_enveloped_codec>(name, fixture, metrics);
    const auto cereal_encoded              = collect_codec_metrics<cereal_enveloped_codec>(name, fixture, metrics);
    const auto boost_serialization_encoded = collect_codec_metrics<boost_serialization_enveloped_codec>(name, fixture, metrics);
    const auto glaze_cbor_encoded          = collect_codec_metrics<glaze_cbor_enveloped_codec>(name, fixture, metrics);
    const auto flatbuffers_encoded         = collect_codec_metrics<flatbuffers_enveloped_codec>(name, fixture, metrics);

    run_encode_cold_benchmark<cbor_tags_enveloped_codec>(name, fixture, cbor_tags_encoded.size(), bench, results);
    run_encode_cold_benchmark<bitsery_enveloped_codec>(name, fixture, bitsery_encoded.size(), bench, results);
    run_encode_cold_benchmark<zpp_bits_enveloped_codec>(name, fixture, zpp_bits_encoded.size(), bench, results);
    run_encode_cold_benchmark<cereal_enveloped_codec>(name, fixture, cereal_encoded.size(), bench, results);
    run_encode_cold_benchmark<boost_serialization_enveloped_codec>(name, fixture, boost_serialization_encoded.size(), bench, results);
    run_encode_cold_benchmark<glaze_cbor_enveloped_codec>(name, fixture, glaze_cbor_encoded.size(), bench, results);
    run_encode_cold_benchmark<flatbuffers_enveloped_codec>(name, fixture, flatbuffers_encoded.size(), bench, results);

    run_encode_reused_benchmark<cbor_tags_enveloped_codec>(name, fixture, cbor_tags_encoded.size(), bench, results);
    run_encode_reused_benchmark<bitsery_enveloped_codec>(name, fixture, bitsery_encoded.size(), bench, results);
    run_encode_reused_benchmark<zpp_bits_enveloped_codec>(name, fixture, zpp_bits_encoded.size(), bench, results);
    run_encode_reused_benchmark<cereal_enveloped_codec>(name, fixture, cereal_encoded.size(), bench, results);
    run_encode_reused_benchmark<boost_serialization_enveloped_codec>(name, fixture, boost_serialization_encoded.size(), bench, results);
    run_encode_reused_benchmark<glaze_cbor_enveloped_codec>(name, fixture, glaze_cbor_encoded.size(), bench, results);
    run_encode_reused_benchmark<flatbuffers_enveloped_codec>(name, fixture, flatbuffers_encoded.size(), bench, results);

    run_decode_benchmark<cbor_tags_enveloped_codec, Fixture>(name, cbor_tags_encoded, bench, results);
    run_decode_benchmark<bitsery_enveloped_codec, Fixture>(name, bitsery_encoded, bench, results);
    run_decode_benchmark<zpp_bits_enveloped_codec, Fixture>(name, zpp_bits_encoded, bench, results);
    run_decode_benchmark<cereal_enveloped_codec, Fixture>(name, cereal_encoded, bench, results);
    run_decode_benchmark<boost_serialization_enveloped_codec, Fixture>(name, boost_serialization_encoded, bench, results);
    run_decode_benchmark<glaze_cbor_enveloped_codec, Fixture>(name, glaze_cbor_encoded, bench, results);
    run_decode_benchmark<flatbuffers_enveloped_codec, Fixture>(name, flatbuffers_encoded, bench, results);
}

template <typename Fixture>
typed_array_segments collect_typed_array_segment_metrics(std::string_view fixture_name, const Fixture &fixture,
                                                         std::vector<metric_row> &metrics) {
    using codec                 = cbor_tags_typed_array_segments_codec;
    const auto encoded          = codec::encode(fixture);
    const auto expected_digest  = checksum(fixture);
    const auto decoded_checksum = codec::template decode_checksum<Fixture>(encoded);
    if (decoded_checksum != expected_digest) {
        throw std::runtime_error{std::string{codec::name} + " checksum mismatch for " + std::string{fixture_name}};
    }

    auto                 encode_allocations = count_allocations([&] { return codec::encode(fixture); });
    typed_array_segments reserved_segments;
    auto                 reserved_encode_allocations = count_allocations([&] {
        codec::encode_into(fixture, reserved_segments);
        return reserved_segments.size();
    });
    auto                 decode_allocations          = count_allocations([&] { return codec::template decode_checksum<Fixture>(encoded); });
    metrics.push_back(metric_row{.fixture                     = std::string{fixture_name},
                                 .library                     = std::string{codec::name},
                                 .encoded_size                = encoded.size(),
                                 .encode_cold_allocations     = encode_allocations,
                                 .encode_reserved_allocations = reserved_encode_allocations,
                                 .decode_allocations          = decode_allocations,
                                 .checksum                    = expected_digest});
    return encoded;
}

template <typename Fixture>
void run_typed_array_segment_encode_reused_benchmark(std::string_view fixture_name, const Fixture &fixture, std::size_t encoded_size,
                                                     ankerl::nanobench::Bench &bench, std::vector<ankerl::nanobench::Result> &results) {
    const auto           library_name = std::string{cbor_tags_typed_array_segments_codec::name};
    typed_array_segments segments;
    bench.title(std::string{fixture_name} + " encode reused buffer").batch(encoded_size).run(library_name, [&] {
        cbor_tags_typed_array_segments_codec::encode_into(fixture, segments);
        ankerl::nanobench::doNotOptimizeAway(segments);
    });
    results.push_back(bench.results().back());
}

template <typename Fixture>
void run_typed_array_fixture(std::string_view name, const Fixture &fixture, ankerl::nanobench::Bench &bench,
                             std::vector<metric_row> &metrics, std::vector<ankerl::nanobench::Result> &results) {
    const auto range_encoded    = collect_codec_metrics<cbor_tags_typed_array_range_codec>(name, fixture, metrics);
    const auto bulk_encoded     = collect_codec_metrics<cbor_tags_typed_array_bulk_copy_codec>(name, fixture, metrics);
    const auto segments_encoded = collect_typed_array_segment_metrics(name, fixture, metrics);

    run_encode_cold_benchmark<cbor_tags_typed_array_range_codec>(name, fixture, range_encoded.size(), bench, results);
    run_encode_cold_benchmark<cbor_tags_typed_array_bulk_copy_codec>(name, fixture, bulk_encoded.size(), bench, results);
    run_encode_cold_benchmark<cbor_tags_typed_array_segments_codec>(name, fixture, segments_encoded.size(), bench, results);

    run_encode_reused_benchmark<cbor_tags_typed_array_range_codec>(name, fixture, range_encoded.size(), bench, results);
    run_encode_reused_benchmark<cbor_tags_typed_array_bulk_copy_codec>(name, fixture, bulk_encoded.size(), bench, results);
    run_typed_array_segment_encode_reused_benchmark(name, fixture, segments_encoded.size(), bench, results);

    run_decode_benchmark<cbor_tags_typed_array_range_codec, Fixture>(name, range_encoded, bench, results);
    run_decode_benchmark<cbor_tags_typed_array_bulk_copy_codec, Fixture>(name, bulk_encoded, bench, results);
    run_decode_benchmark<cbor_tags_typed_array_segments_codec, Fixture>(name, segments_encoded, bench, results);
}

template <typename Fn> void expect_failure(std::string_view name, Fn &&fn) {
    try {
        std::forward<Fn>(fn)();
    } catch (const std::exception &) { return; }
    throw std::runtime_error{"validation expected failure did not fail: " + std::string{name}};
}

template <typename Codec> void validate_envelope_rejects_wrong_type(const FlatRecord &record) {
    const auto encoded = Codec::encode(record);
    expect_failure(std::string{Codec::name} + " wrong object id", [&] { (void)Codec::template decode_checksum<RecordBatch>(encoded); });
}

template <typename T> byte_buffer encode_malformed_typed_array_length() {
    constexpr auto tag = typed_array_tag_v<T>;
    byte_buffer    buffer;
    byte_buffer    payload(sizeof(T) - 1U, 0xabU);
    auto           enc    = cbor::tags::make_encoder(buffer);
    auto           result = enc(cbor::tags::static_tag<tag>{}, cbor::tags::as_bstr_range(payload));
    if (!result) {
        throw std::runtime_error(std::string{"malformed typed array setup failed: "} +
                                 std::string{cbor::tags::status_message(result.error())});
    }
    return buffer;
}

void validate_benchmark_codecs() {
    const auto record = make_flat_record(7);
    validate_envelope_rejects_wrong_type<cbor_tags_enveloped_codec>(record);
    validate_envelope_rejects_wrong_type<bitsery_enveloped_codec>(record);
    validate_envelope_rejects_wrong_type<zpp_bits_enveloped_codec>(record);
    validate_envelope_rejects_wrong_type<cereal_enveloped_codec>(record);
    validate_envelope_rejects_wrong_type<boost_serialization_enveloped_codec>(record);
    validate_envelope_rejects_wrong_type<glaze_cbor_enveloped_codec>(record);
    validate_envelope_rejects_wrong_type<flatbuffers_enveloped_codec>(record);

    const auto int32_values = make_int32_series();
    const auto typed_int32  = cbor_tags_typed_array_range_codec::encode(int32_values);
    if (cbor_tags_typed_array_range_codec::decode_checksum<std::vector<std::int32_t>>(typed_int32) != checksum(int32_values)) {
        throw std::runtime_error{"cbor_tags typed int32 roundtrip checksum mismatch"};
    }
    if (cbor_tags_typed_array_bulk_copy_codec::decode_checksum<std::vector<std::int32_t>>(
            cbor_tags_typed_array_bulk_copy_codec::encode(int32_values)) != checksum(int32_values)) {
        throw std::runtime_error{"cbor_tags typed int32 bulk-copy checksum mismatch"};
    }
    if (cbor_tags_typed_array_segments_codec::decode_checksum<std::vector<std::int32_t>>(
            cbor_tags_typed_array_segments_codec::encode(int32_values)) != checksum(int32_values)) {
        throw std::runtime_error{"cbor_tags typed int32 segment checksum mismatch"};
    }
    const auto wrong_tag = encode_typed_array_as_tag<typed_array_tag_v<std::int64_t>>(int32_values);
    expect_failure("cbor_tags typed array tag mismatch",
                   [&] { (void)cbor_tags_typed_array_range_codec::decode_checksum<std::vector<std::int32_t>>(wrong_tag); });
    const auto malformed_length = encode_malformed_typed_array_length<std::int32_t>();
    expect_failure("cbor_tags typed array malformed length",
                   [&] { (void)cbor_tags_typed_array_range_codec::decode_checksum<std::vector<std::int32_t>>(malformed_length); });

    const auto int64_values = make_int64_series();
    const auto float_values = make_float64_series();
    if (cbor_tags_typed_array_range_codec::decode_checksum<std::vector<std::int64_t>>(
            cbor_tags_typed_array_range_codec::encode(int64_values)) != checksum(int64_values)) {
        throw std::runtime_error{"cbor_tags typed int64 roundtrip checksum mismatch"};
    }
    if (cbor_tags_typed_array_bulk_copy_codec::decode_checksum<std::vector<std::int64_t>>(
            cbor_tags_typed_array_bulk_copy_codec::encode(int64_values)) != checksum(int64_values)) {
        throw std::runtime_error{"cbor_tags typed int64 bulk-copy checksum mismatch"};
    }
    if (cbor_tags_typed_array_segments_codec::decode_checksum<std::vector<std::int64_t>>(
            cbor_tags_typed_array_segments_codec::encode(int64_values)) != checksum(int64_values)) {
        throw std::runtime_error{"cbor_tags typed int64 segment checksum mismatch"};
    }
    if (cbor_tags_typed_array_range_codec::decode_checksum<std::vector<double>>(cbor_tags_typed_array_range_codec::encode(float_values)) !=
        checksum(float_values)) {
        throw std::runtime_error{"cbor_tags typed float64 roundtrip checksum mismatch"};
    }
    if (cbor_tags_typed_array_bulk_copy_codec::decode_checksum<std::vector<double>>(
            cbor_tags_typed_array_bulk_copy_codec::encode(float_values)) != checksum(float_values)) {
        throw std::runtime_error{"cbor_tags typed float64 bulk-copy checksum mismatch"};
    }
    if (cbor_tags_typed_array_segments_codec::decode_checksum<std::vector<double>>(
            cbor_tags_typed_array_segments_codec::encode(float_values)) != checksum(float_values)) {
        throw std::runtime_error{"cbor_tags typed float64 segment checksum mismatch"};
    }
}

std::string compiler_id() {
#if defined(__clang__)
    return std::string{"Clang "} + __clang_version__;
#elif defined(__GNUC__)
    return std::string{"GCC "} + __VERSION__;
#elif defined(_MSC_VER)
    return "MSVC " + std::to_string(_MSC_VER);
#else
    return "unknown";
#endif
}

void write_report(const std::filesystem::path &output_dir, const std::vector<ankerl::nanobench::Result> &results,
                  std::string_view benchmark_markdown, const std::vector<metric_row> &metrics) {
    std::filesystem::create_directories(output_dir);

    {
        std::ofstream json(output_dir / "results.json", std::ios::out | std::ios::binary);
        ankerl::nanobench::render(ankerl::nanobench::templates::json(), results, json);
    }
    {
        std::ofstream csv(output_dir / "results.csv", std::ios::out | std::ios::binary);
        ankerl::nanobench::render(ankerl::nanobench::templates::csv(), results, csv);
    }

    std::ofstream report(output_dir / "report.md", std::ios::out | std::ios::binary);
    report << "# Serialization Comparison Benchmark\n\n";
    report << "This report compares representative serialization libraries with different wire formats and runtime contracts. Treat the "
              "performance rows as workload evidence, not as proof that the formats are semantically interchangeable.\n\n";

    report << "## Environment\n\n";
    report << "| Field | Value |\n";
    report << "|---|---|\n";
    report << "| Build type | " << CBOR_TAGS_BENCH_BUILD_TYPE << " |\n";
    report << "| Compiler | " << compiler_id() << " |\n";
    report << "| C++ standard | " << __cplusplus << " |\n";
    report << "| Output directory | `" << output_dir.string() << "` |\n\n";

    report << "## Dependency Matrix\n\n";
    report << "| Library | Version | Wire/adapter behavior | Decode behavior in this suite |\n";
    report << "|---|---:|---|---|\n";
    report << "| cbor_tags | working tree | CBOR, self-describing major types | Materializes native fixtures |\n";
    report << "| cbor_tags_rfc8746_typed_array_range | working tree | CBOR tag plus RFC 8746 typed-array byte string from a byte view | "
              "Materializes native numeric vectors |\n";
    report << "| cbor_tags_rfc8746_typed_array_bulk_copy | working tree | CBOR tag plus RFC 8746 typed-array byte string with bulk payload "
              "append | Materializes native numeric vectors |\n";
    report
        << "| cbor_tags_rfc8746_typed_array_segments | working tree | CBOR tag/bstr header plus borrowed raw payload segment | Validates "
           "header and reads numeric values as a zero-copy typed span |\n";
    report << "| bitsery | " << CBOR_TAGS_BENCH_BITSERY_VERSION
           << " | Fixed-width binary fields with explicit container limits | Materializes native fixtures |\n";
    report << "| zpp_bits | " << CBOR_TAGS_BENCH_ZPP_BITS_VERSION
           << " | Binary aggregate serialization, native-endian defaults | Materializes native fixtures |\n";
    report << "| cereal | " << CBOR_TAGS_BENCH_CEREAL_VERSION << " | BinaryArchive over iostreams | Materializes native fixtures |\n";
    report << "| Boost.Serialization | system Boost | Binary archive with `no_header` | Materializes native fixtures |\n";
    report << "| Glaze CBOR | " << CBOR_TAGS_BENCH_GLAZE_VERSION
           << " | CBOR object maps, byte strings for `uint8_t` vectors, and RFC 8746 typed arrays for numeric vectors | "
              "Materializes native fixtures |\n";
    report << "| FlatBuffers | " << CBOR_TAGS_BENCH_FLATBUFFERS_VERSION
           << " | Schema-generated table/vector format | Verifies and reads fields through zero-copy views |\n\n";

    report << "## Fixtures\n\n";
    report << "| Fixture | Shape |\n";
    report << "|---|---|\n";
    report << "| flat_record | Scalars, bool, float, double, string, and a fixed-size sample vector |\n";
    report << "| record_batch | 128 `flat_record` values |\n";
    report << "| nested_snapshot | 64 records, 24 string/integer counters, and a 2048-byte payload |\n";
    report << "| int32_series | 4096 signed 32-bit integer values |\n";
    report << "| int64_series | 4096 signed 64-bit integer values |\n";
    report << "| float64_series | 4096 double-precision floating-point values |\n";
    report << "| *_enveloped | Same payload as the base fixture with benchmark-local top-level type detection |\n";
    report << "| *_rfc8746_typed_array | Numeric vector encoded as a CBOR RFC 8746 typed-array tag plus byte string; library rows split "
              "range, bulk-copy, and borrowed-segment paths |\n\n";

    report << "## Size And Allocation Summary\n\n";
    report << "| Fixture | Library | Encoded bytes | Cold encode allocs | Cold encode bytes | Reserved encode allocs | Reserved encode "
              "bytes | Decode allocs | Decode bytes |\n";
    report << "|---|---|---:|---:|---:|---:|---:|---:|---:|\n";
    for (const auto &row : metrics) {
        report << "| " << row.fixture << " | " << row.library << " | " << row.encoded_size << " | " << row.encode_cold_allocations.count
               << " | " << row.encode_cold_allocations.bytes << " | " << row.encode_reserved_allocations.count << " | "
               << row.encode_reserved_allocations.bytes << " | " << row.decode_allocations.count << " | " << row.decode_allocations.bytes
               << " |\n";
    }
    report << "\n";

    report << "## Nanobench Results\n\n";
    report << benchmark_markdown << "\n";

    report << "## Interpretation Notes\n\n";
    report << "- The wire formats are intentionally different: CBOR is self-describing, FlatBuffers is schema-based, and several binary "
              "archives "
              "depend on C++ type/archive contracts.\n";
    report << "- Known-type rows keep the original benchmark contract. `*_enveloped` rows add top-level type detection: `cbor_tags` "
              "uses a benchmark-local tag, Glaze CBOR uses a two-element CBOR array, FlatBuffers uses a 4-byte file identifier, "
              "and the other binary archives prefix a `uint32_t` object id.\n";
    report << "- Glaze CBOR's native numeric-vector encoding is already RFC 8746 tag plus byte string, while its object fixtures use "
              "text-key maps and its `std::vector<uint8_t>` payload uses a byte string.\n";
    report << "- RFC 8746 typed-array rows use IANA CBOR tags 78, 79, and 86 for little-endian int32, int64, and float64 arrays. These "
              "rows are not wire-equivalent to generic CBOR arrays and require an RFC 8746-aware peer.\n";
    report << "- The RFC 8746 rows are benchmark-local helpers, not a public `cbor_tags` API. The range row uses the existing CBOR range "
              "encoder, the bulk-copy row materializes one contiguous CBOR buffer, and the segment row represents a scatter/gather "
              "header-plus-payload shape without joining the buffers.\n";
    report << "- The segment row is a little-endian zero-copy ceiling for this benchmark. It borrows the original vector payload and is "
              "not a standalone serialized byte buffer.\n";
    report
        << "- FlatBuffers decode rows measure verifier plus field access without native object materialization; that is a representative "
           "FlatBuffers usage mode and should not be read as a like-for-like materializing decode.\n";
    report << "- Cold encode allocation rows start from an empty destination buffer and include output growth. Reserved encode allocation "
              "rows "
              "reserve the final encoded size before measurement and count only additional encode-time allocations.\n";
    report
        << "- Reserved encode rows still include archive or builder internals for libraries that do not write directly into the caller's "
           "byte buffer.\n";
    report << "- Decode allocation rows count global `operator new` calls in one decode operation. They include materialized output "
              "objects and "
              "archive scaffolding, but not stack storage.\n";
    report << "- `results.json` and `results.csv` contain the raw nanobench measurements used by the Markdown report.\n";
}

int run(int argc, char **argv) {
    auto output_dir = std::filesystem::path{"build/benchmark-comparison"};
    for (int index = 1; index < argc; ++index) {
        const auto argument = std::string_view{argv[index]};
        if (argument == "--output-dir") {
            if (index + 1 >= argc) {
                throw std::runtime_error{"--output-dir requires a path"};
            }
            output_dir = argv[++index];
        } else if (argument == "--help" || argument == "-h") {
            std::cout << "usage: bench_serialization_compare [--output-dir PATH]\n";
            return 0;
        } else {
            throw std::runtime_error{"unknown argument: " + std::string{argument}};
        }
    }

    validate_benchmark_codecs();

    std::ostringstream       benchmark_markdown;
    ankerl::nanobench::Bench bench;
    bench.output(&benchmark_markdown).unit("byte").relative(true).performanceCounters(false).epochs(11).minEpochIterations(20);

    std::vector<metric_row>                metrics;
    std::vector<ankerl::nanobench::Result> results;
    const auto                             flat_record     = make_flat_record(42);
    const auto                             record_batch    = make_record_batch();
    const auto                             nested_snapshot = make_nested_snapshot();
    const auto                             int32_series    = make_int32_series();
    const auto                             int64_series    = make_int64_series();
    const auto                             float64_series  = make_float64_series();

    run_fixture("flat_record", flat_record, bench, metrics, results);
    run_fixture("record_batch", record_batch, bench, metrics, results);
    run_fixture("nested_snapshot", nested_snapshot, bench, metrics, results);
    run_fixture("int32_series", int32_series, bench, metrics, results);
    run_fixture("int64_series", int64_series, bench, metrics, results);
    run_fixture("float64_series", float64_series, bench, metrics, results);

    run_enveloped_fixture("flat_record_enveloped", flat_record, bench, metrics, results);
    run_enveloped_fixture("record_batch_enveloped", record_batch, bench, metrics, results);
    run_enveloped_fixture("nested_snapshot_enveloped", nested_snapshot, bench, metrics, results);
    run_enveloped_fixture("int32_series_enveloped", int32_series, bench, metrics, results);
    run_enveloped_fixture("int64_series_enveloped", int64_series, bench, metrics, results);
    run_enveloped_fixture("float64_series_enveloped", float64_series, bench, metrics, results);

    run_typed_array_fixture("int32_series_rfc8746_typed_array", int32_series, bench, metrics, results);
    run_typed_array_fixture("int64_series_rfc8746_typed_array", int64_series, bench, metrics, results);
    run_typed_array_fixture("float64_series_rfc8746_typed_array", float64_series, bench, metrics, results);

    write_report(output_dir, results, benchmark_markdown.str(), metrics);
    std::cout << "wrote " << (output_dir / "report.md") << '\n';
    std::cout << "wrote " << (output_dir / "results.json") << '\n';
    std::cout << "wrote " << (output_dir / "results.csv") << '\n';
    return 0;
}

} // namespace bench_compare

int main(int argc, char **argv) {
    try {
        return bench_compare::run(argc, argv);
    } catch (const std::exception &error) {
        std::cerr << "bench_serialization_compare: " << error.what() << '\n';
        return 1;
    }
}
