#include "cbor_tags/cbor_simple.h"
#include "cbor_tags/float16_ieee754.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <nameof.hpp>
#include <new>
#include <optional>
#include <random>
#include <string>
#include <vector>
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/extensions/smart_ptr.h>
#include <fmt/format.h>
#include <nanobench.h>
#include <small_generator.h>

using namespace std::string_view_literals;
using namespace cbor::tags;
using namespace cbor::tags::ext::smart_ptr;
namespace smart_ptr_detail = cbor::tags::ext::smart_ptr::detail;

namespace {

template <std::size_t Capacity> class unsafe_shared_graph_encode_array_session final : public shared_graph_encode_session_base {
  public:
    unsafe_shared_graph_encode_array_session() = default;
    ~unsafe_shared_graph_encode_array_session() override { reset_unchecked(); }

    unsafe_shared_graph_encode_array_session(const unsafe_shared_graph_encode_array_session &)            = delete;
    unsafe_shared_graph_encode_array_session &operator=(const unsafe_shared_graph_encode_array_session &) = delete;
    unsafe_shared_graph_encode_array_session(unsafe_shared_graph_encode_array_session &&)                 = delete;
    unsafe_shared_graph_encode_array_session &operator=(unsafe_shared_graph_encode_array_session &&)      = delete;

    [[nodiscard]] shared_graph_encode_lookup lookup() const noexcept override { return shared_graph_encode_lookup::linear_scan; }

  private:
    void reserve_unique_impl(std::size_t) override {}

    void reset_unchecked() override {
        while (size_ > 0U) {
            pop_encoded_object();
        }
    }

    [[nodiscard]] std::size_t encoded_size() const noexcept override { return size_; }

    [[nodiscard]] smart_ptr_detail::encoded_shared_object &encoded_object(std::size_t index) override { return *object_at(index); }

    void push_encoded_object(smart_ptr_detail::encoded_shared_object object) override {
        std::construct_at(object_at(size_), std::move(object));
        ++size_;
    }

    void pop_encoded_object() override {
        --size_;
        std::destroy_at(object_at(size_));
    }

    [[nodiscard]] std::optional<std::size_t> find_encoded_index(const void *address) const override {
        for (std::size_t index = 0; index < size_; ++index) {
            if (object_at(index)->address == address) {
                return index;
            }
        }
        return std::nullopt;
    }

    void index_encoded_address(const void *, std::size_t) override {}

    [[nodiscard]] smart_ptr_detail::encoded_shared_object *object_at(std::size_t index) noexcept {
        return std::launder(reinterpret_cast<smart_ptr_detail::encoded_shared_object *>(
            storage_.data() + index * sizeof(smart_ptr_detail::encoded_shared_object)));
    }

    [[nodiscard]] const smart_ptr_detail::encoded_shared_object *object_at(std::size_t index) const noexcept {
        return std::launder(reinterpret_cast<const smart_ptr_detail::encoded_shared_object *>(
            storage_.data() + index * sizeof(smart_ptr_detail::encoded_shared_object)));
    }

    alignas(smart_ptr_detail::encoded_shared_object)
        std::array<std::byte, sizeof(smart_ptr_detail::encoded_shared_object) * Capacity> storage_{};
    std::size_t size_{0};
};

template <std::size_t Capacity> class unsafe_typed_shared_graph_encode_array_session {
  private:
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    [[nodiscard]] std::optional<std::size_t> find(const void *address) const noexcept {
        for (std::size_t index = 0; index < size_; ++index) {
            if (addresses_[index] == address) {
                return index;
            }
        }
        return std::nullopt;
    }

    void push(const void *address) noexcept {
        addresses_[size_] = address;
        ++size_;
    }

    std::array<const void *, Capacity> addresses_{};
    std::size_t                        size_{0};

    template <typename Self, std::size_t CodecCapacity> friend struct unsafe_typed_shared_graph_codec_mixin;
};

template <std::size_t Capacity, typename T> struct unsafe_typed_shared_graph_encode_root {
    unsafe_typed_shared_graph_encode_array_session<Capacity> &session;
    const T                                                  &value;
};

template <std::size_t Capacity, typename T>
unsafe_typed_shared_graph_encode_root<Capacity, T>
as_unsafe_typed_shared_graph(unsafe_typed_shared_graph_encode_array_session<Capacity> &session, const T &value) {
    return {session, value};
}

template <std::size_t Capacity, typename T>
unsafe_typed_shared_graph_encode_root<Capacity, T> as_unsafe_typed_shared_graph(unsafe_typed_shared_graph_encode_array_session<Capacity> &,
                                                                                const T &&) = delete;

template <typename Self, std::size_t Capacity> struct unsafe_typed_shared_graph_codec_mixin : cbor_codec_mixin_base<Self> {
    using cbor_codec_mixin_base<Self>::decode;
    using cbor_codec_mixin_base<Self>::encode;

    template <typename T> void encode(unsafe_typed_shared_graph_encode_root<Capacity, T> root) {
        auto *previous = active_encode_session_;
        if (previous != nullptr && previous != &root.session) {
            throw std::runtime_error("nested unsafe shared graph sessions must use the same session object");
        }
        active_encode_session_ = &root.session;
        try {
            static_cast<Self &>(*this).encode(root.value);
            active_encode_session_ = previous;
        } catch (...) {
            active_encode_session_ = previous;
            throw;
        }
    }

    template <smart_ptr_detail::NullablePointerValue T> void encode(const std::shared_ptr<T> &value) {
        if (active_encode_session_ == nullptr) {
            throw std::runtime_error("unsafe shared_ptr graph encoding requires as_unsafe_typed_shared_graph(...)");
        }

        auto &enc = static_cast<Self &>(*this);
        if (!value) {
            enc.encode(as_array{1});
            enc.encode(std::uint64_t{0});
            return;
        }

        const auto *address = static_cast<const void *>(value.get());
        if (const auto existing = active_encode_session_->find(address)) {
            enc.encode(static_tag<smart_ptr_detail::sharedref_tag>{});
            enc.encode(static_cast<std::uint64_t>(*existing));
            return;
        }

        active_encode_session_->push(address);
        enc.encode(static_tag<smart_ptr_detail::shareable_tag>{});
        enc.encode(*value);
    }

  private:
    unsafe_typed_shared_graph_encode_array_session<Capacity> *active_encode_session_{};
};

template <std::size_t Capacity> struct unsafe_typed_shared_graph_codec {
    template <typename Self> using mixin = unsafe_typed_shared_graph_codec_mixin<Self, Capacity>;
};

} // namespace

struct benchmark_options {
    std::string_view unit{"Ops"};
    bool             relative{true};
};

// Function to run nanobench benchmark
template <typename Buffer> void run_encoding_benchmarks(ankerl::nanobench::Bench &bench) {
    auto seed = std::random_device{}();
    auto gen  = rng::small_generator{seed};

    bench.run("Encoding a uint", [&gen]() {
        Buffer data;
        auto   enc  = make_encoder(data);
        std::ignore = enc(gen());
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encoding a uint with check", [&gen]() {
        Buffer data;
        auto   enc = make_encoder(data);
        CHECK(enc(gen()));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encoding a int", [&gen]() {
        Buffer data;
        auto   enc  = make_encoder(data);
        std::ignore = enc(static_cast<int64_t>(gen()));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encoding a int with check", [&gen]() {
        Buffer data;
        auto   enc = make_encoder(data);
        CHECK(enc(static_cast<int64_t>(gen())));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encoding a bstr", [&gen]() {
        std::vector<uint8_t>     data;
        auto                     enc = make_encoder(data);
        std::array<std::byte, 4> bstr;
        for (auto &b : bstr) {
            b = std::byte(gen());
        }
        std::ignore = enc(bstr);
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encoding a bstr with check", [&gen]() {
        std::vector<uint8_t>     data;
        auto                     enc = make_encoder(data);
        std::array<std::byte, 4> bstr;
        for (auto &b : bstr) {
            b = std::byte(gen());
        }
        CHECK(enc(bstr));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encoding a tstr", [&gen]() {
        Buffer      data;
        auto        enc = make_encoder(data);
        std::string s;
        s.resize(4);
        std::transform(s.begin(), s.end(), s.begin(), [&gen](auto) { return static_cast<char>(gen.generate()); });
        std::ignore = enc(s);
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encoding a tstr with check", [&gen]() {
        Buffer      data;
        auto        enc = make_encoder(data);
        std::string s;
        s.resize(4);
        std::transform(s.begin(), s.end(), s.begin(), [&gen](auto) { return static_cast<char>(gen.generate()); });
        CHECK(enc(s));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encoding a array", [&gen]() {
        Buffer data;
        auto   enc  = make_encoder(data);
        std::ignore = enc(std::array<int16_t, 4>{static_cast<short>(gen()), static_cast<short>(gen()), static_cast<short>(gen()),
                                                 static_cast<short>(gen())});
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encoding an array with check", [&gen]() {
        std::vector<uint8_t>   data;
        auto                   enc = make_encoder(data);
        std::array<int16_t, 4> a{static_cast<short>(gen()), static_cast<short>(gen()), static_cast<short>(gen()),
                                 static_cast<short>(gen())};
        CHECK(enc(a));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encode a map", [&gen]() {
        Buffer data;
        auto   enc  = make_encoder(data);
        std::ignore = enc(std::map<int16_t, int32_t>{{static_cast<short>(gen()), static_cast<int>(gen())},
                                                     {static_cast<short>(gen()), static_cast<int>(gen())}});
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encode a map with check", [&gen]() {
        Buffer data;
        auto   enc = make_encoder(data);
        CHECK(enc(std::map<int16_t, int32_t>{{static_cast<short>(gen()), static_cast<int>(gen())},
                                             {static_cast<short>(gen()), static_cast<int>(gen())}}));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    struct A {
        static_tag<333> cbor_tag;
        int64_t         value;
    };

    bench.run("Encode a tag", [&gen]() {
        Buffer data;
        auto   enc  = make_encoder(data);
        std::ignore = enc(A{.cbor_tag = {}, .value = static_cast<int64_t>(gen())});
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encode a tag with check", [&gen]() {
        Buffer data;
        auto   enc = make_encoder(data);
        CHECK(enc(A{.cbor_tag = {}, .value = static_cast<int64_t>(gen())}));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encode a nullptr", []() {
        Buffer data;
        auto   enc = make_encoder(data);
        CHECK(enc(std::nullptr_t{}));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encode a bool with check", [&gen]() {
        Buffer data;
        auto   enc = make_encoder(data);
        CHECK(enc(static_cast<bool>(gen() % 2)));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encode a float16", [&gen]() {
        Buffer data;
        auto   enc  = make_encoder(data);
        std::ignore = enc(static_cast<float16_t>(static_cast<float>(gen())));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encode a float32", [&gen]() {
        Buffer data;
        auto   enc  = make_encoder(data);
        std::ignore = enc(static_cast<float>(gen()));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encode a float64", [&gen]() {
        Buffer data;
        auto   enc  = make_encoder(data);
        std::ignore = enc(static_cast<double>(gen()));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encode a float16_t with check", [&gen]() {
        Buffer data;
        auto   enc = make_encoder(data);
        CHECK(enc(static_cast<float16_t>(static_cast<float>(gen()))));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encode a float32 with check", [&gen]() {
        Buffer data;
        auto   enc = make_encoder(data);
        CHECK(enc(static_cast<float>(gen())));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encode a double with check", [&gen]() {
        Buffer data;
        auto   enc = make_encoder(data);
        CHECK(enc(static_cast<double>(gen())));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encode a simple with check", [&gen]() {
        Buffer data;
        auto   enc = make_encoder(data);
        simple s{static_cast<simple::value_type>(gen())};
        CHECK(enc(s));
        ankerl::nanobench::doNotOptimizeAway(data);
    });
}

std::vector<std::shared_ptr<std::uint64_t>> make_shared_graph_values(std::size_t unique_count, std::size_t repeat_count) {
    std::vector<std::shared_ptr<std::uint64_t>> unique_values;
    unique_values.reserve(unique_count);
    for (std::size_t index = 0; index < unique_count; ++index) {
        unique_values.push_back(std::make_shared<std::uint64_t>(index));
    }

    std::vector<std::shared_ptr<std::uint64_t>> values;
    values.reserve(unique_count * repeat_count);
    for (std::size_t repeat = 0; repeat < repeat_count; ++repeat) {
        values.insert(values.end(), unique_values.begin(), unique_values.end());
    }
    return values;
}

template <std::size_t UniqueCount> void run_shared_graph_encode_lookup_benchmarks_for_size(ankerl::nanobench::Bench &bench) {
    const auto values = make_shared_graph_values(UniqueCount, 2U);

    bench.run(fmt::format("shared_graph encode {} unique x2 unordered_map", UniqueCount), [&values]() {
        std::vector<std::uint8_t> data;
        data.reserve(values.size() * 8U);

        auto                        enc = make_encoder<shared_graph_codec>(data);
        shared_graph_encode_session graph{shared_graph_encode_lookup::unordered_map};
        auto                        result = enc(as_shared_graph(graph, values));

        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run(fmt::format("shared_graph encode {} unique x2 unordered_map_reserved", UniqueCount), [&values]() {
        std::vector<std::uint8_t> data;
        data.reserve(values.size() * 8U);

        auto                        enc = make_encoder<shared_graph_codec>(data);
        shared_graph_encode_session graph{shared_graph_encode_lookup::unordered_map};
        graph.reserve_unique(UniqueCount);
        auto result = enc(as_shared_graph(graph, values));

        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run(fmt::format("shared_graph encode {} unique x2 vector_scan_o_n", UniqueCount), [&values]() {
        std::vector<std::uint8_t> data;
        data.reserve(values.size() * 8U);

        auto                        enc = make_encoder<shared_graph_codec>(data);
        shared_graph_encode_session graph{shared_graph_encode_lookup::linear_scan};
        auto                        result = enc(as_shared_graph(graph, values));

        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run(fmt::format("shared_graph encode {} unique x2 vector_scan_o_n_reserved", UniqueCount), [&values]() {
        std::vector<std::uint8_t> data;
        data.reserve(values.size() * 8U);

        auto                        enc = make_encoder<shared_graph_codec>(data);
        shared_graph_encode_session graph{shared_graph_encode_lookup::linear_scan};
        graph.reserve_unique(UniqueCount);
        auto result = enc(as_shared_graph(graph, values));

        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run(fmt::format("shared_graph encode {} unique x2 array_scan_fixed", UniqueCount), [&values]() {
        std::vector<std::uint8_t> data;
        data.reserve(values.size() * 8U);

        auto                                           enc = make_encoder<shared_graph_codec>(data);
        shared_graph_encode_array_session<UniqueCount> graph;
        auto                                           result = enc(as_shared_graph(graph, values));

        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run(fmt::format("shared_graph encode {} unique x2 array_scan_unsafe_fixed", UniqueCount), [&values]() {
        std::vector<std::uint8_t> data;
        data.reserve(values.size() * 8U);

        auto                                                  enc = make_encoder<shared_graph_codec>(data);
        unsafe_shared_graph_encode_array_session<UniqueCount> graph;
        auto                                                  result = enc(as_shared_graph(graph, values));

        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run(fmt::format("shared_graph encode {} unique x2 array_scan_typed_unsafe", UniqueCount), [&values]() {
        std::vector<std::uint8_t> data;
        data.reserve(values.size() * 8U);

        auto enc = make_encoder<unsafe_typed_shared_graph_codec<UniqueCount>::template mixin>(data);
        unsafe_typed_shared_graph_encode_array_session<UniqueCount> graph;
        auto                                                        result = enc(as_unsafe_typed_shared_graph(graph, values));

        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(data);
    });
}

void run_shared_graph_encode_lookup_benchmarks(ankerl::nanobench::Bench &bench) {
    run_shared_graph_encode_lookup_benchmarks_for_size<4U>(bench);
    run_shared_graph_encode_lookup_benchmarks_for_size<16U>(bench);
    run_shared_graph_encode_lookup_benchmarks_for_size<64U>(bench);
    run_shared_graph_encode_lookup_benchmarks_for_size<256U>(bench);
}

template <typename ContainerType> void run_benchmark() {
    ankerl::nanobench::Bench bench;
    benchmark_options        options;

    bench.title(std::string(nameof::nameof_type<ContainerType>()) + " buffer");
    bench.minEpochIterations(100);
    bench.unit(options.unit.data());
    bench.performanceCounters(true);
    bench.relative(options.relative);

    run_encoding_benchmarks<ContainerType>(bench);
}

TEST_CASE("Encoding benchmarks") {
    run_benchmark<std::vector<uint8_t>>();
    run_benchmark<std::deque<uint8_t>>();
    run_benchmark<std::array<uint8_t, 20>>();
}

TEST_CASE("Shared graph encode lookup benchmarks") {
    ankerl::nanobench::Bench bench;
    benchmark_options        options;

    bench.title("shared_graph encode lookup");
    bench.minEpochIterations(20);
    bench.unit(options.unit.data());
    bench.performanceCounters(true);
    bench.relative(options.relative);

    run_shared_graph_encode_lookup_benchmarks(bench);
}

// Main function to run tests and benchmarks
int main(int argc, char **argv) {
    // Run Doctest tests
    int result = doctest::Context(argc, argv).run();
    return result;
}
