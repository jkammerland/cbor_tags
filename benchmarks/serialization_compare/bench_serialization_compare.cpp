#include "comparison_generated.h"

#include <algorithm>
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
#include <ios>
#include <iostream>
#include <nanobench.h>
#include <new>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
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

namespace bench_compare {

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

struct bitsery_codec {
    static constexpr std::string_view name = "bitsery";

    static constexpr std::size_t max_string_size = 128;
    static constexpr std::size_t max_samples     = 64;
    static constexpr std::size_t max_records     = 256;
    static constexpr std::size_t max_counters    = 64;
    static constexpr std::size_t max_payload     = 4096;

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

namespace fb = cbor_tags_bench;

flatbuffers::Offset<fb::FlatRecord> create_flat_record(flatbuffers::FlatBufferBuilder &builder, const FlatRecord &value) {
    auto name    = builder.CreateString(value.name);
    auto samples = builder.CreateVector(value.samples);
    return fb::CreateFlatRecord(builder, value.id, value.sequence, value.active, value.kind, value.temperature, value.score, name, samples);
}

flatbuffers::Offset<fb::KeyValue> create_key_value(flatbuffers::FlatBufferBuilder &builder, const KeyValue &value) {
    auto key = builder.CreateString(value.key);
    return fb::CreateKeyValue(builder, key, value.value);
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

template <typename FlatbufferRoot> const FlatbufferRoot &checked_flatbuffer_root(const byte_buffer &buffer) {
    flatbuffers::Verifier verifier(buffer.data(), buffer.size());
    if (!verifier.VerifyBuffer<FlatbufferRoot>(nullptr)) {
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

    static void encode_into(const FlatRecord &value, byte_buffer &buffer) {
        buffer.clear();
        auto builder = make_builder(buffer);
        builder.Finish(create_flat_record(builder, value));
        copy_finished(builder, buffer);
    }

    static byte_buffer encode(const FlatRecord &value) {
        byte_buffer buffer;
        encode_into(value, buffer);
        return buffer;
    }

    static void encode_into(const RecordBatch &value, byte_buffer &buffer) {
        buffer.clear();
        auto                                             builder = make_builder(buffer);
        std::vector<flatbuffers::Offset<fb::FlatRecord>> records;
        records.reserve(value.records.size());
        for (const auto &record : value.records) {
            records.push_back(create_flat_record(builder, record));
        }
        auto records_vector = builder.CreateVector(records);
        builder.Finish(fb::CreateRecordBatch(builder, records_vector));
        copy_finished(builder, buffer);
    }

    static byte_buffer encode(const RecordBatch &value) {
        byte_buffer buffer;
        encode_into(value, buffer);
        return buffer;
    }

    static void encode_into(const NestedSnapshot &value, byte_buffer &buffer) {
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

        builder.Finish(fb::CreateNestedSnapshot(builder, title, records_vector, counters_vector, payload_vector));
        copy_finished(builder, buffer);
    }

    static byte_buffer encode(const NestedSnapshot &value) {
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

template <typename Codec, typename Fixture>
void run_decode_benchmark(std::string_view fixture_name, const byte_buffer &encoded, ankerl::nanobench::Bench &bench,
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
    const auto flatbuffers_encoded         = collect_codec_metrics<flatbuffers_codec>(name, fixture, metrics);

    run_encode_cold_benchmark<cbor_tags_codec>(name, fixture, cbor_tags_encoded.size(), bench, results);
    run_encode_cold_benchmark<bitsery_codec>(name, fixture, bitsery_encoded.size(), bench, results);
    run_encode_cold_benchmark<zpp_bits_codec>(name, fixture, zpp_bits_encoded.size(), bench, results);
    run_encode_cold_benchmark<cereal_codec>(name, fixture, cereal_encoded.size(), bench, results);
    run_encode_cold_benchmark<boost_serialization_codec>(name, fixture, boost_serialization_encoded.size(), bench, results);
    run_encode_cold_benchmark<flatbuffers_codec>(name, fixture, flatbuffers_encoded.size(), bench, results);

    run_encode_reused_benchmark<cbor_tags_codec>(name, fixture, cbor_tags_encoded.size(), bench, results);
    run_encode_reused_benchmark<bitsery_codec>(name, fixture, bitsery_encoded.size(), bench, results);
    run_encode_reused_benchmark<zpp_bits_codec>(name, fixture, zpp_bits_encoded.size(), bench, results);
    run_encode_reused_benchmark<cereal_codec>(name, fixture, cereal_encoded.size(), bench, results);
    run_encode_reused_benchmark<boost_serialization_codec>(name, fixture, boost_serialization_encoded.size(), bench, results);
    run_encode_reused_benchmark<flatbuffers_codec>(name, fixture, flatbuffers_encoded.size(), bench, results);

    run_decode_benchmark<cbor_tags_codec, Fixture>(name, cbor_tags_encoded, bench, results);
    run_decode_benchmark<bitsery_codec, Fixture>(name, bitsery_encoded, bench, results);
    run_decode_benchmark<zpp_bits_codec, Fixture>(name, zpp_bits_encoded, bench, results);
    run_decode_benchmark<cereal_codec, Fixture>(name, cereal_encoded, bench, results);
    run_decode_benchmark<boost_serialization_codec, Fixture>(name, boost_serialization_encoded, bench, results);
    run_decode_benchmark<flatbuffers_codec, Fixture>(name, flatbuffers_encoded, bench, results);
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
    report << "| bitsery | " << CBOR_TAGS_BENCH_BITSERY_VERSION
           << " | Fixed-width binary fields with explicit container limits | Materializes native fixtures |\n";
    report << "| zpp_bits | " << CBOR_TAGS_BENCH_ZPP_BITS_VERSION
           << " | Binary aggregate serialization, native-endian defaults | Materializes native fixtures |\n";
    report << "| cereal | " << CBOR_TAGS_BENCH_CEREAL_VERSION << " | BinaryArchive over iostreams | Materializes native fixtures |\n";
    report << "| Boost.Serialization | system Boost | Binary archive with `no_header` | Materializes native fixtures |\n";
    report << "| FlatBuffers | " << CBOR_TAGS_BENCH_FLATBUFFERS_VERSION
           << " | Schema-generated table/vector format | Verifies and reads fields through zero-copy views |\n\n";

    report << "## Fixtures\n\n";
    report << "| Fixture | Shape |\n";
    report << "|---|---|\n";
    report << "| flat_record | Scalars, bool, float, double, string, and a fixed-size sample vector |\n";
    report << "| record_batch | 128 `flat_record` values |\n";
    report << "| nested_snapshot | 64 records, 24 string/integer counters, and a 2048-byte payload |\n\n";

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

    std::ostringstream       benchmark_markdown;
    ankerl::nanobench::Bench bench;
    bench.output(&benchmark_markdown).unit("byte").relative(true).performanceCounters(false).epochs(11).minEpochIterations(20);

    std::vector<metric_row>                metrics;
    std::vector<ankerl::nanobench::Result> results;
    run_fixture("flat_record", make_flat_record(42), bench, metrics, results);
    run_fixture("record_batch", make_record_batch(), bench, metrics, results);
    run_fixture("nested_snapshot", make_nested_snapshot(), bench, metrics, results);

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
